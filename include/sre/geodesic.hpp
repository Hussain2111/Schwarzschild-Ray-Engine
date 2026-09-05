// Null geodesics of the Schwarzschild metric.
//
// ---------------------------------------------------------------------------
// The formulation, and why it is not the one in v1
// ---------------------------------------------------------------------------
//
// v1 integrated the second-order geodesic equations for r(lambda) and
// phi(lambda) directly. That works on paper but is a poor choice numerically:
// the affine parameter lambda has no geometric meaning, dr/dlambda passes
// through zero at periapsis (so the step size has to be tiny there for no good
// reason), and dt/dlambda = E/f blows up at the horizon.
//
// Every Schwarzschild geodesic is planar, because the spacetime is spherically
// symmetric and the photon's angular momentum vector is conserved. So we can
// reduce the whole 3D problem to one ODE in the plane spanned by the ray's
// starting position and its direction. Substituting u = 1/r and eliminating
// the affine parameter gives the Binet-style orbit equation
//
//     d^2u/dphi^2 = -u + (3GM/c^2) u^2                     (general form)
//                 = -u + (3/2) u^2                         (in units r_s = 1)
//
// This is the equation to integrate. It has no singularity outside the
// horizon, phi is monotonic along the ray so it is a proper independent
// variable, and the Newtonian limit is visible by inspection: drop the u^2
// term and you get a straight line in polar coordinates.
//
// The conserved impact parameter provides a free, exact error check at every
// step:
//
//     (du/dphi)^2 + u^2 (1 - u) = 1/b^2 = const
//
// tests/ uses it to verify the integrator, and Ray::impactParameter() uses it
// to get the redshift right.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "sre/constants.hpp"
#include "sre/integrator.hpp"
#include "sre/vec.hpp"

namespace sre {

/// Photon state along its orbit: y[0] = u = r_s/r, y[1] = du/dphi.
using PhotonState = State<2>;

/// Right-hand side of the null orbit equation, in units where r_s = 1.
inline PhotonState nullGeodesicRHS(double /*phi*/, const PhotonState& y) {
    return {y[1], -y[0] + 1.5 * y[0] * y[0]};
}

/// b = |L|/E, the impact parameter, from the conserved combination above.
/// Constant along a geodesic; drift in this value *is* the integration error.
inline double impactParameter(const PhotonState& y) {
    const double inv_b2 = y[1] * y[1] + y[0] * y[0] * (1.0 - y[0]);
    if (inv_b2 <= 0.0) return std::numeric_limits<double>::infinity();
    return 1.0 / std::sqrt(inv_b2);
}

/// Cubic Hermite interpolation of u across one accepted step.
///
/// The integrator gives us u and du/dphi at both ends of a step, which is
/// exactly the data a cubic Hermite needs. It is third-order accurate, so it
/// costs nothing against the fifth-order step and lets us pin down events
/// (disk crossings, radius thresholds) *inside* a step instead of forcing the
/// step size down until they land on a boundary.
inline double hermiteU(const PhotonState& y0, const PhotonState& y1, double h, double s) {
    const double s2 = s * s, s3 = s2 * s;
    const double h00 = 2.0 * s3 - 3.0 * s2 + 1.0;
    const double h10 = s3 - 2.0 * s2 + s;
    const double h01 = -2.0 * s3 + 3.0 * s2;
    const double h11 = s3 - s2;
    return h00 * y0[0] + h * h10 * y0[1] + h01 * y1[0] + h * h11 * y1[1];
}

/// Find s in [0,1] where the Hermite interpolant of u equals `target`.
/// Assumes u(0) - target and u(1) - target have opposite signs.
inline double hermiteSolveU(const PhotonState& y0, const PhotonState& y1, double h, double target) {
    double lo = 0.0, hi = 1.0;
    const double fLo = y0[0] - target;
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double fMid = hermiteU(y0, y1, h, mid) - target;
        if ((fLo < 0.0) == (fMid < 0.0)) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

/// The plane a geodesic lives in, plus the initial conditions expressed in it.
///
/// `e1` points from the black hole to the ray origin, `e2` is the in-plane
/// direction of increasing phi (chosen so the ray always moves towards larger
/// phi), and `normal = e1 x e2` is the direction of the photon's conserved
/// angular momentum.
struct GeodesicFrame {
    Vec3 e1{1, 0, 0};
    Vec3 e2{0, 1, 0};
    Vec3 normal{0, 0, 1};
    PhotonState start{1.0, 0.0};
    bool radial = false;  ///< no angular momentum: the plane is arbitrary

    /// Position in 3D at orbital angle phi, given u = 1/r there.
    Vec3 positionAt(double phi, double u) const {
        const double r = 1.0 / u;
        return (e1 * std::cos(phi) + e2 * std::sin(phi)) * r;
    }

    /// Unit tangent at phi in *coordinate* components.
    ///
    /// This is the right thing for the sky lookup, which happens at a radius
    /// where f -> 1 and the coordinate direction is the asymptotic direction
    /// of travel. Use localDirectionAt() for what an observer sitting at that
    /// radius would actually measure.
    Vec3 directionAt(double phi, const PhotonState& y) const {
        const double u = y[0];
        const double r = 1.0 / u;
        const double drdphi = -y[1] / (u * u);
        const Vec3 radial_hat = e1 * std::cos(phi) + e2 * std::sin(phi);
        const Vec3 tangent_hat = e1 * -std::sin(phi) + e2 * std::cos(phi);
        return normalize(radial_hat * drdphi + tangent_hat * r);
    }

    /// Unit tangent at phi as measured by a static observer there -- the exact
    /// inverse of the tetrad conversion makeFrame() applies to the incoming
    /// direction, so makeFrame() and this function round-trip.
    Vec3 localDirectionAt(double phi, const PhotonState& y) const {
        const double u = y[0];
        const double r = 1.0 / u;
        const double sqrtF = std::sqrt(std::max(1e-12, 1.0 - u));
        const double drdphi = -y[1] / (u * u);
        const Vec3 radial_hat = e1 * std::cos(phi) + e2 * std::sin(phi);
        const Vec3 tangent_hat = e1 * -std::sin(phi) + e2 * std::cos(phi);
        return normalize(radial_hat * (drdphi / sqrtF) + tangent_hat * r);
    }
};

/// Build the geodesic frame for a photon launched from `origin` (in r_s units,
/// with the black hole at the coordinate origin) along unit vector `dir`.
///
/// `dir` is the direction as measured in the *local orthonormal frame of a
/// static observer* at `origin` -- that is, what the camera actually sees.
/// Converting it to Schwarzschild coordinate components needs the tetrad
///
///     e_r = sqrt(f) d/dr,    e_theta = (1/r) d/dtheta,    e_phi = (1/r) d/dphi
///
/// so the radial component picks up a factor of sqrt(f(r)) that the transverse
/// components do not. Skipping it (the obvious flat-space shortcut) is
/// harmless at r ~ 100 r_s but visibly wrong up close: it puts the edge of the
/// shadow at asin(b_c/r) instead of the correct asin(b_c sqrt(f)/r), an 8%
/// error at r = 6 r_s and worse nearer in. With the factor included, the
/// relation between a pixel's angle and the photon's impact parameter,
///
///     sin(theta) = b sqrt(f(r)) / r
///
/// comes out exactly right at any radius outside the horizon.
inline GeodesicFrame makeFrame(const Vec3& origin, const Vec3& dir) {
    GeodesicFrame f;
    const double r0 = length(origin);
    f.e1 = origin / r0;

    const double radialComponent = dot(dir, f.e1);
    Vec3 tangential = dir - f.e1 * radialComponent;
    const double tangentialComponent = length(tangential);

    // A purely radial ray has zero angular momentum; any perpendicular vector
    // will do for e2 and the orbit equation degenerates to a straight fall.
    if (tangentialComponent < 1e-12) {
        Vec3 t, b;
        orthonormalBasis(f.e1, t, b);
        f.e2 = t;
        f.normal = cross(f.e1, f.e2);
        f.radial = true;
        f.start = {1.0 / r0, radialComponent > 0.0 ? -std::numeric_limits<double>::infinity()
                                                   : std::numeric_limits<double>::infinity()};
        // Represent radial motion with a finite but very steep du/dphi so the
        // generic marcher still behaves; callers should check `radial`.
        f.start[1] = radialComponent > 0.0 ? -1e12 : 1e12;
        return f;
    }

    f.e2 = tangential / tangentialComponent;
    f.normal = cross(f.e1, f.e2);

    // dr/dphi = r sqrt(f) (n_r / n_t)  =>  du/dphi = -sqrt(f) n_r / (r n_t).
    const double sqrtF = std::sqrt(std::max(0.0, 1.0 - 1.0 / r0));
    f.start = {1.0 / r0, -sqrtF * radialComponent / (r0 * tangentialComponent)};
    return f;
}

/// Why a traced ray stopped.
enum class RayOutcome {
    Horizon,   ///< fell through r = r_s; contributes no light
    Escaped,   ///< reached the far-field radius; sample the sky
    Absorbed,  ///< terminated by a callback (e.g. hit an opaque disk)
    Exhausted  ///< hit the step budget; treated as a shadow
};

/// Configuration for marching a single photon.
struct MarchConfig {
    double escapeRadius = 2000.0;  ///< r (in r_s) beyond which we call it escaped
    double horizonEpsilon = 1.001; ///< stop just outside r_s to avoid u -> 1 stiffness
    int maxSteps = 4096;
    Tolerance tolerance{};
    double initialStep = 1e-2;     ///< in radians of phi
};

/// A single accepted step of the march, handed to the visitor.
struct MarchSegment {
    double phi0, phi1;
    PhotonState y0, y1;
};

/// Integrate a photon from its frame until it is captured, escapes, or a
/// visitor stops it.
///
/// `visitor(seg)` is called for every accepted step and returns true to
/// continue. It receives the state at both ends of the step so it can do its
/// own interpolation (see disk.hpp, which uses Hermite interpolation to find
/// exactly where the ray punches through the disk plane).
template <typename Visitor>
RayOutcome marchPhoton(const GeodesicFrame& frame, const MarchConfig& cfg,
                       Visitor&& visitor, PhotonState* finalState = nullptr,
                       double* finalPhi = nullptr) {
    PhotonState y = frame.start;
    double phi = 0.0;
    double h = cfg.initialStep;

    const double uHorizon = 1.0 / cfg.horizonEpsilon;
    const double uEscape = 1.0 / cfg.escapeRadius;

    RayOutcome outcome = RayOutcome::Exhausted;

    for (int step = 0; step < cfg.maxSteps; ++step) {
        const PhotonState yPrev = y;
        const double phiPrev = phi;

        const StepResult res = adaptiveStep(y, phi, h, cfg.tolerance, nullGeodesicRHS);
        h = res.next;

        if (!res.accepted) {
            // The controller has already shrunk h. If it cannot shrink further
            // we are effectively at the horizon, where u -> 1 makes the problem
            // stiff; call it captured rather than spinning.
            if (h <= cfg.tolerance.minStep * 1.0000001) {
                outcome = (yPrev[0] > 0.5) ? RayOutcome::Horizon : RayOutcome::Exhausted;
                break;
            }
            continue;
        }

        if (!std::isfinite(y[0]) || y[0] <= 0.0) {
            // u <= 0 means r ran off to infinity between samples.
            y = yPrev;
            phi = phiPrev;
            outcome = RayOutcome::Escaped;
            break;
        }

        if (!visitor(MarchSegment{phiPrev, phi, yPrev, y})) {
            outcome = RayOutcome::Absorbed;
            break;
        }

        if (y[0] >= uHorizon) { outcome = RayOutcome::Horizon; break; }
        if (y[0] <= uEscape)  { outcome = RayOutcome::Escaped; break; }
    }

    if (finalState) *finalState = y;
    if (finalPhi) *finalPhi = phi;
    return outcome;
}

/// Total deflection angle of a photon that passes the black hole with impact
/// parameter `b`, in radians.
///
/// The weak-field limit is the classic Einstein result alpha -> 2 r_s / b
/// (= 4GM/(bc^2)), the prediction Eddington's 1919 eclipse expedition
/// confirmed. As b approaches b_c = 3 sqrt(3)/2 r_s the deflection diverges
/// logarithmically: the photon loops the black hole arbitrarily many times,
/// which is what produces the infinite stack of higher-order images around
/// the shadow. Returns a negative value if the photon is captured.
///
/// The integration necessarily starts at a finite radius r0, where the ray has
/// not yet swept its full asymptotic angle. A straight line with impact
/// parameter b sweeps exactly pi - 2 asin(b/r0) between r0 and r0, so
/// subtracting *that* rather than pi removes the entire leading finite-r0
/// error and leaves only an O(r_s/r0) remainder.
inline double deflectionAngle(double b, double startRadius = 0.0,
                              const Tolerance& tol = Tolerance{1e-14, 1e-12, 1e-10, 0.05, 0.9}) {
    if (b <= 0.0) return -1.0;
    const double r0 = startRadius > 0.0 ? startRadius : std::max(1e8, 1e6 * b);
    if (r0 <= b) return -1.0;

    const double u0 = 1.0 / r0;
    const double inv_b2 = 1.0 / (b * b);
    const double dudphi2 = inv_b2 - u0 * u0 * (1.0 - u0);
    if (dudphi2 <= 0.0) return -1.0;

    PhotonState y{u0, std::sqrt(dudphi2)};  // inbound branch
    double phi = 0.0;
    double h = 1e-4;

    for (int i = 0; i < 1000000; ++i) {
        const PhotonState yPrev = y;
        const double phiPrev = phi;

        const StepResult res = adaptiveStep(y, phi, h, tol, nullGeodesicRHS);
        h = res.next;
        if (!res.accepted) {
            if (h <= tol.minStep * 1.0000001) return -1.0;
            continue;
        }
        if (y[0] >= 1.0) return -1.0;  // crossed the horizon

        // Outbound and back past the start radius: pin the crossing inside the
        // step rather than accepting up to a full step of overshoot in phi.
        if (y[0] <= u0 && y[1] < 0.0 && yPrev[0] > u0) {
            const double s = hermiteSolveU(yPrev, y, res.used, u0);
            const double phiCross = phiPrev + s * res.used;
            return phiCross - phys::pi + 2.0 * std::asin(b / r0);
        }
        if (phi > 200.0 * phys::pi) return -1.0;  // spiralling in; captured
    }
    return -1.0;
}

}  // namespace sre
