// Embedded Runge-Kutta integrators with adaptive step size control.
//
// v1 of this project used forward Euler with a fixed step. Euler is first
// order and, worse, it is *unstable* on an oscillatory problem like a bound
// photon orbit: the amplitude grows without bound no matter how small the
// step. Cash-Karp gives a fifth-order solution plus a free fourth-order
// estimate, and the difference between the two is a per-step error estimate
// we can steer the step size with. Near the photon sphere the step collapses
// automatically; far away it stretches out and costs nothing.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sre {

template <std::size_t N>
using State = std::array<double, N>;

/// Result of a single adaptive step.
struct StepResult {
    bool accepted = false;   ///< error was within tolerance
    double used = 0.0;       ///< step actually taken (0 if rejected)
    double next = 0.0;       ///< suggested step for the following attempt
    double error = 0.0;      ///< normalised error estimate (1.0 == exactly at tolerance)
};

/// Cash-Karp 5(4) coefficients.
namespace cashkarp {
inline constexpr double a2 = 1.0 / 5.0, a3 = 3.0 / 10.0, a4 = 3.0 / 5.0, a5 = 1.0, a6 = 7.0 / 8.0;

inline constexpr double b21 = 1.0 / 5.0;
inline constexpr double b31 = 3.0 / 40.0,       b32 = 9.0 / 40.0;
inline constexpr double b41 = 3.0 / 10.0,       b42 = -9.0 / 10.0,   b43 = 6.0 / 5.0;
inline constexpr double b51 = -11.0 / 54.0,     b52 = 5.0 / 2.0,     b53 = -70.0 / 27.0,     b54 = 35.0 / 27.0;
inline constexpr double b61 = 1631.0 / 55296.0, b62 = 175.0 / 512.0, b63 = 575.0 / 13824.0,  b64 = 44275.0 / 110592.0, b65 = 253.0 / 4096.0;

// Fifth-order solution weights.
inline constexpr double c1 = 37.0 / 378.0, c3 = 250.0 / 621.0, c4 = 125.0 / 594.0, c6 = 512.0 / 1771.0;
// Fourth-order (embedded) solution weights.
inline constexpr double d1 = 2825.0 / 27648.0, d3 = 18575.0 / 48384.0, d4 = 13525.0 / 55296.0,
                        d5 = 277.0 / 14336.0,  d6 = 1.0 / 4.0;
}  // namespace cashkarp

/// One Cash-Karp step of size h. Writes the 5th-order result to `out` and the
/// 5th-vs-4th difference to `err`. Does not adapt; see adaptiveStep().
template <std::size_t N, typename Deriv>
void cashKarpStep(const State<N>& y, double t, double h, Deriv&& dydt,
                  State<N>& out, State<N>& err) {
    using namespace cashkarp;
    State<N> tmp{};
    const State<N> k1 = dydt(t, y);

    for (std::size_t i = 0; i < N; ++i) tmp[i] = y[i] + h * b21 * k1[i];
    const State<N> k2 = dydt(t + a2 * h, tmp);

    for (std::size_t i = 0; i < N; ++i) tmp[i] = y[i] + h * (b31 * k1[i] + b32 * k2[i]);
    const State<N> k3 = dydt(t + a3 * h, tmp);

    for (std::size_t i = 0; i < N; ++i) tmp[i] = y[i] + h * (b41 * k1[i] + b42 * k2[i] + b43 * k3[i]);
    const State<N> k4 = dydt(t + a4 * h, tmp);

    for (std::size_t i = 0; i < N; ++i)
        tmp[i] = y[i] + h * (b51 * k1[i] + b52 * k2[i] + b53 * k3[i] + b54 * k4[i]);
    const State<N> k5 = dydt(t + a5 * h, tmp);

    for (std::size_t i = 0; i < N; ++i)
        tmp[i] = y[i] + h * (b61 * k1[i] + b62 * k2[i] + b63 * k3[i] + b64 * k4[i] + b65 * k5[i]);
    const State<N> k6 = dydt(t + a6 * h, tmp);

    for (std::size_t i = 0; i < N; ++i) {
        out[i] = y[i] + h * (c1 * k1[i] + c3 * k3[i] + c4 * k4[i] + c6 * k6[i]);
        const double y4 = y[i] + h * (d1 * k1[i] + d3 * k3[i] + d4 * k4[i] + d5 * k5[i] + d6 * k6[i]);
        err[i] = out[i] - y4;
    }
}

/// Classic fixed-step RK4. Kept because it is the right tool for the GPU path
/// (no divergent step counts across a warp) and because having two independent
/// schemes lets the test suite cross-check them.
template <std::size_t N, typename Deriv>
State<N> rk4Step(const State<N>& y, double t, double h, Deriv&& dydt) {
    State<N> tmp{}, out{};
    const State<N> k1 = dydt(t, y);
    for (std::size_t i = 0; i < N; ++i) tmp[i] = y[i] + 0.5 * h * k1[i];
    const State<N> k2 = dydt(t + 0.5 * h, tmp);
    for (std::size_t i = 0; i < N; ++i) tmp[i] = y[i] + 0.5 * h * k2[i];
    const State<N> k3 = dydt(t + 0.5 * h, tmp);
    for (std::size_t i = 0; i < N; ++i) tmp[i] = y[i] + h * k3[i];
    const State<N> k4 = dydt(t + h, tmp);
    for (std::size_t i = 0; i < N; ++i)
        out[i] = y[i] + (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    return out;
}

/// Tolerances for the adaptive controller.
struct Tolerance {
    double absolute = 1e-9;
    double relative = 1e-9;
    double minStep = 1e-7;
    double maxStep = 1.0;
    double safety = 0.9;   ///< shrink factor applied to the ideal step
};

/// Attempt one adaptive Cash-Karp step. On success `y` and `t` advance.
/// On failure they are left untouched and a smaller step is suggested.
template <std::size_t N, typename Deriv>
StepResult adaptiveStep(State<N>& y, double& t, double h, const Tolerance& tol, Deriv&& dydt) {
    State<N> out{}, err{};
    cashKarpStep(y, t, h, dydt, out, err);

    // Error normalised against a per-component tolerance, then reduced with an
    // L-infinity norm: the worst component sets the step.
    double worst = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        const double scale = tol.absolute + tol.relative * std::fabs(y[i]);
        worst = std::max(worst, std::fabs(err[i]) / scale);
    }

    StepResult r;
    r.error = worst;

    if (!std::isfinite(worst)) {
        r.accepted = false;
        r.next = std::max(tol.minStep, std::fabs(h) * 0.1);
        return r;
    }

    if (worst <= 1.0) {
        y = out;
        t += h;
        r.accepted = true;
        r.used = h;
        // Grow, but never by more than 5x in one go.
        const double grow = worst > 0.0 ? tol.safety * std::pow(worst, -0.2) : 5.0;
        r.next = std::min(tol.maxStep, std::fabs(h) * std::min(5.0, grow));
    } else {
        r.accepted = false;
        // Shrink, but never by more than 10x in one go.
        const double shrink = tol.safety * std::pow(worst, -0.25);
        r.next = std::max(tol.minStep, std::fabs(h) * std::max(0.1, shrink));
    }
    return r;
}

}  // namespace sre
