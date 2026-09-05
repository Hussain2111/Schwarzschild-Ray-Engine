// Spectral colour: Planck's law -> CIE XYZ -> linear sRGB, plus tone mapping.
//
// An accretion disk glows because it is *hot*, so its colour is not a paint
// swatch we get to pick, it is a blackbody spectrum at a temperature the
// physics hands us. Doing the conversion properly is what makes the inner disk
// come out blue-white and the outer disk orange, and it is what lets Doppler
// shift change the *hue* across the disk rather than only the brightness.
#pragma once

#include <algorithm>
#include <cmath>

#include "sre/constants.hpp"
#include "sre/vec.hpp"

namespace sre {

using Color = Vec3;  // linear RGB, unbounded (this is radiance, not a pixel)

namespace color {

/// Piecewise Gaussian used by the CIE colour-matching fits below.
inline double pieceGauss(double x, double mu, double s1, double s2) {
    const double t = (x - mu) * (x < mu ? 1.0 / s1 : 1.0 / s2);
    return std::exp(-0.5 * t * t);
}

/// Analytic fits to the CIE 1931 2-degree standard observer.
/// Wyman, Sloan & Shirley, "Simple Analytic Approximations to the CIE XYZ
/// Colour Matching Functions", JCGT 2(2), 2013. Accurate to well under a
/// percent, and far more compact than a tabulated observer.
inline Vec3 cieXYZBar(double lambdaNm) {
    const double x = 1.056 * pieceGauss(lambdaNm, 599.8, 37.9, 31.0)
                   + 0.362 * pieceGauss(lambdaNm, 442.0, 16.0, 26.7)
                   - 0.065 * pieceGauss(lambdaNm, 501.1, 20.4, 26.2);
    const double y = 0.821 * pieceGauss(lambdaNm, 568.8, 46.9, 40.5)
                   + 0.286 * pieceGauss(lambdaNm, 530.9, 16.3, 31.1);
    const double z = 1.217 * pieceGauss(lambdaNm, 437.0, 11.8, 36.0)
                   + 0.681 * pieceGauss(lambdaNm, 459.0, 26.0, 13.8);
    return {x, y, z};
}

/// Planck spectral radiance B_lambda(T), W / (m^2 sr m). Only its *shape*
/// matters here; the overall scale is normalised away.
inline double planck(double lambdaNm, double kelvin) {
    if (kelvin <= 0.0) return 0.0;
    const double l = lambdaNm * 1e-9;
    const double l5 = l * l * l * l * l;
    const double a = 2.0 * phys::h_planck * phys::c * phys::c / l5;
    const double x = phys::h_planck * phys::c / (l * phys::k_boltz * kelvin);
    if (x > 700.0) return 0.0;  // exp() would overflow; the radiance is ~0 anyway
    return a / std::expm1(x);
}

/// CIE XYZ -> linear sRGB (Rec.709 primaries, D65 white).
inline Color xyzToLinearSRGB(const Vec3& xyz) {
    return { 3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
            -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
             0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z};
}

/// Colour of a blackbody at `kelvin`, normalised to unit luminance so that
/// brightness can be controlled independently of hue.
///
/// Negative components (temperatures outside the sRGB gamut, which happens for
/// very hot blue sources) are desaturated towards white rather than clipped,
/// which keeps the hue stable instead of shifting it as the disk heats up.
inline Color blackbodyRGB(double kelvin) {
    if (!(kelvin > 0.0)) return {0.0, 0.0, 0.0};

    Vec3 xyz{0, 0, 0};
    constexpr double lo = 380.0, hi = 780.0;
    constexpr int samples = 80;
    const double step = (hi - lo) / samples;
    for (int i = 0; i < samples; ++i) {
        const double lambda = lo + (i + 0.5) * step;
        const double radiance = planck(lambda, kelvin);
        xyz += cieXYZBar(lambda) * radiance * step;
    }
    if (xyz.y <= 0.0) return {0.0, 0.0, 0.0};
    xyz = xyz / xyz.y;  // unit luminance

    Color rgb = xyzToLinearSRGB(xyz);
    const double minC = std::min({rgb.x, rgb.y, rgb.z});
    if (minC < 0.0) rgb -= Vec3{minC, minC, minC};  // desaturate into gamut
    const double maxC = std::max({rgb.x, rgb.y, rgb.z});
    if (maxC > 0.0) rgb = rgb / maxC;
    return rgb;
}

/// 256-entry cache over a log-temperature range. The renderer asks for a
/// blackbody colour once per disk hit; doing the 80-sample spectral integral
/// every time would dominate the frame.
class BlackbodyTable {
public:
    static constexpr int kSize = 256;
    static constexpr double kMinK = 300.0;
    static constexpr double kMaxK = 200000.0;

    BlackbodyTable() {
        for (int i = 0; i < kSize; ++i) {
            const double t = static_cast<double>(i) / (kSize - 1);
            table_[i] = blackbodyRGB(kMinK * std::pow(kMaxK / kMinK, t));
        }
    }

    Color operator()(double kelvin) const {
        if (kelvin <= kMinK) return table_[0];
        if (kelvin >= kMaxK) return table_[kSize - 1];
        const double t = std::log(kelvin / kMinK) / std::log(kMaxK / kMinK);
        const double f = t * (kSize - 1);
        const int i = static_cast<int>(f);
        return lerp(table_[i], table_[std::min(i + 1, kSize - 1)], f - i);
    }

private:
    Color table_[kSize];
};

inline const BlackbodyTable& blackbodyTable() {
    static const BlackbodyTable t;
    return t;
}

// --- Display transform ------------------------------------------------------

/// ACES filmic tone curve (Narkowicz's fit). Maps the very large dynamic range
/// of the disk -- the inner edge is orders of magnitude brighter than the outer
/// edge, before Doppler beaming widens the gap further -- onto a display
/// without either clipping the core to a white disc or crushing the outskirts.
inline Color acesToneMap(const Color& x) {
    constexpr double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    auto f = [](double v) {
        v = std::max(0.0, v);
        return std::clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0, 1.0);
    };
    return {f(x.x), f(x.y), f(x.z)};
}

/// Rec.709 relative luminance.
inline double luminance(const Color& c) {
    return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
}

/// Hue-preserving tone map.
///
/// Applying a tone curve per channel (the usual approach) desaturates
/// highlights towards white, because the brightest channel compresses hardest.
/// That is fine for photographic footage but it is actively wrong here: the
/// disk's colour *is* the physics, and washing the inner disk to white throws
/// away the temperature information the spectral pipeline just computed.
///
/// So the curve is applied to luminance only and the chroma is carried
/// through, with a controlled desaturation that engages only where a channel
/// would otherwise clip.
inline Color toneMap(const Color& c, double desaturateHighlights = 0.55) {
    const double y = luminance(c);
    if (y <= 0.0) return {0, 0, 0};

    const double mapped = acesToneMap(Color{y, y, y}).x;
    Color out = c * (mapped / y);

    // Where a channel still exceeds 1, mix towards the (in-range) luminance
    // rather than hard clipping, which would shift the hue instead of fading it.
    const double peak = std::max({out.x, out.y, out.z});
    if (peak > 1.0) {
        const double t = std::clamp((peak - 1.0) / peak, 0.0, 1.0) * desaturateHighlights;
        out = lerp(out, Color{mapped, mapped, mapped}, t);
        out = out / std::max(1.0, std::max({out.x, out.y, out.z}));
    }
    return {std::clamp(out.x, 0.0, 1.0), std::clamp(out.y, 0.0, 1.0), std::clamp(out.z, 0.0, 1.0)};
}

/// Linear -> sRGB electro-optical transfer function.
inline double linearToSRGB(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

inline Color linearToSRGB(const Color& c) {
    return {linearToSRGB(c.x), linearToSRGB(c.y), linearToSRGB(c.z)};
}

}  // namespace color
}  // namespace sre
