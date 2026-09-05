// Geometrically thin, optically thick accretion disk with full relativistic
// transfer: gravitational redshift, Doppler shift, and relativistic beaming.
//
// This is where a black hole picture stops being a lens demo and starts
// looking like the real thing. Three effects stack on the same g factor:
//
//   * gravitational redshift  - photons climbing out of the well lose energy,
//                               so the inner disk is reddened and dimmed
//                               relative to its emitted temperature;
//   * Doppler shift           - the disk orbits at a large fraction of c, so
//                               the approaching side is blueshifted;
//   * relativistic beaming    - that same motion concentrates the radiation
//                               forward, and because specific intensity scales
//                               as g^4 the approaching side is *dramatically*
//                               brighter.
//
// The g^4 beaming is why real images of an inclined disk (and the EHT images
// of M87* and Sgr A*) are lopsided rather than symmetric rings.
#pragma once

#include <algorithm>
#include <cmath>

#include "sre/blackhole.hpp"
#include "sre/color.hpp"
#include "sre/geodesic.hpp"
#include "sre/noise.hpp"
#include "sre/vec.hpp"

namespace sre {

struct DiskParams {
    double innerRadius = BlackHole::isco();  ///< r_s units; ISCO by default
    double outerRadius = 13.0;
    Vec3 axis{0.0, 1.0, 0.0};                ///< rotation axis (unit)
    double peakTemperature = 5500.0;        ///< K, at the temperature maximum
    double brightness = 1.0;
    bool enabled = true;
    bool opaque = true;                      ///< stop at the first valid crossing
    bool relativistic = true;                ///< apply the g factor (off = flat comparison)
    double prograde = 1.0;                   ///< +1 or -1; flips the orbit direction
    double turbulence = 0.55;                ///< 0 = smooth analytic disk, 1 = very clumpy
    double time = 0.0;                       ///< animation phase, in r_s/c
};

/// Everything the shader needs about one intersection with the disk plane.
struct DiskHit {
    bool valid = false;
    double radius = 0.0;        ///< r at the crossing, in r_s
    double phi = 0.0;           ///< orbital angle along the geodesic
    Vec3 position{};
    double g = 1.0;             ///< nu_observed / nu_emitted
    double temperature = 0.0;   ///< emitted temperature, K
};

namespace disk {

/// Shakura-Sunyaev thin-disk effective temperature profile.
///
///     T(r) ~ r^(-3/4) [1 - sqrt(r_in/r)]^(1/4)
///
/// The bracket is the zero-torque inner boundary condition: no stress is
/// transmitted across the ISCO, so the emission falls to zero exactly at the
/// inner edge instead of diverging there. Normalised so the profile peaks at 1
/// (the maximum sits at r = (49/36) r_in).
inline double temperatureProfile(double r, double rIn) {
    if (r <= rIn) return 0.0;
    const double shape = std::pow(rIn / r, 0.75) * std::pow(1.0 - std::sqrt(rIn / r), 0.25);
    // Peak value of the same expression. Writing x = sqrt(r_in/r) turns the
    // profile into x^(3/2) (1-x)^(1/4), whose maximum is at x = 6/7 (that is,
    // r = (49/36) r_in), giving (6/7)^(3/2) (1/7)^(1/4) exactly.
    constexpr double kPeak = 0.487871339232;
    return shape / kPeak;
}

/// Peak effective temperature of a Shakura-Sunyaev thin disk, in kelvin, from
/// the black hole mass and an accretion rate given as a fraction of the
/// Eddington rate.
///
///     T_eff(r) = [ 3 G M Mdot / (8 pi sigma_SB r^3) (1 - sqrt(r_in/r)) ]^(1/4)
///
/// This is the honest answer to "what colour is it really?", and the honest
/// answer is usually *not* orange. A disk around a stellar-mass black hole
/// peaks in soft X-rays at ~10^7 K; even a supermassive one runs to ~10^5 K,
/// far into the ultraviolet, and renders as featureless blue-white. The warm
/// orange disks in films are a deliberate artistic choice, so the engine keeps
/// peakTemperature as a free parameter and offers this function for when you
/// want the real number instead.
inline double physicalPeakTemperature(double massKg, double eddingtonRatio) {
    constexpr double sigmaSB = 5.670374419e-8;      // W m^-2 K^-4
    constexpr double thomson = 6.6524587e-29;       // m^2
    constexpr double protonMass = 1.67262192e-27;   // kg
    constexpr double efficiency = 0.0572;           // 1 - sqrt(8/9), Schwarzschild ISCO

    // Eddington luminosity, then the mass accretion rate that produces it.
    const double lEdd = 4.0 * phys::pi * phys::G * massKg * protonMass * phys::c / thomson;
    const double mdot = eddingtonRatio * lEdd / (efficiency * phys::c * phys::c);

    const double rs = 2.0 * phys::G * massKg / (phys::c * phys::c);
    const double rIn = 3.0 * rs;  // ISCO

    // The profile peaks at r = (49/36) r_in; evaluate there.
    const double rPeak = (49.0 / 36.0) * rIn;
    const double flux = 3.0 * phys::G * massKg * mdot /
                        (8.0 * phys::pi * sigmaSB * rPeak * rPeak * rPeak) *
                        (1.0 - std::sqrt(rIn / rPeak));
    return std::pow(std::max(flux, 0.0), 0.25);
}

/// The redshift factor g = nu_obs / nu_emit for a photon arriving at a distant
/// static observer from matter on a circular orbit at radius r.
///
///     g = 1 / [ u^t (1 - Omega L/E) ],   u^t = 1/sqrt(1 - 3 r_s / 2r)
///
/// `bSigned` is the photon's angular momentum about the *disk axis* divided by
/// its energy. Note the sign flip: we trace rays backwards from the camera, so
/// the physical photon's momentum is opposite to the marching direction, and
/// its angular momentum is -b (n_hat . axis).
///
/// Sign convention: bSigned > 0 means the emitting matter orbits in the same
/// sense as the photon's angular momentum, i.e. it is moving along the
/// direction the photon was emitted, and the light is blueshifted (g > 1). In
/// the far field this reduces to the ordinary Doppler factor 1/(1 - v/c).
inline double redshiftFactor(double r, double bSigned, double prograde) {
    const double ut = BlackHole::circularOrbitGamma(r);
    if (ut <= 0.0) return 0.0;
    const double omega = prograde * BlackHole::orbitalOmega(r);
    const double denom = ut * (1.0 - omega * bSigned);
    return denom > 1e-9 ? 1.0 / denom : 0.0;
}

/// Multiplicative brightness texture. The azimuth is advanced by the *local*
/// Keplerian angular velocity, so the disk shears differentially: the inner
/// annuli visibly outrun the outer ones over an animation.
inline double texture(double r, double azimuth, const DiskParams& d) {
    if (d.turbulence <= 0.0) return 1.0;
    const double sheared = azimuth - d.prograde * BlackHole::orbitalOmega(r) * d.time;

    // Sampling on a cos/sin ring avoids a seam at azimuth = +-pi. The radial
    // scale is much coarser than the azimuthal one, which stretches the noise
    // into arcs along the flow instead of leaving it as isotropic blobs.
    const double ring = 7.0;
    const Vec3 p{std::cos(sheared) * ring, std::sin(sheared) * ring, r * 1.1};
    const double broad = noise::fbm(p, 4);
    const Vec3 q{std::cos(sheared) * ring * 2.5, std::sin(sheared) * ring * 2.5, r * 2.8};
    const double fine = noise::fbm(q, 3);

    const double n = 0.65 * broad + 0.35 * fine;
    return std::max(0.0, 1.0 + d.turbulence * (2.0 * n - 1.0));
}

/// Soft radial falloff at both edges so the disk does not terminate on a hard
/// aliased line.
inline double edgeFade(double r, const DiskParams& d) {
    const double w = 0.08 * (d.outerRadius - d.innerRadius);
    if (w <= 0.0) return 1.0;
    const double inner = std::clamp((r - d.innerRadius) / w, 0.0, 1.0);
    const double outer = std::clamp((d.outerRadius - r) / w, 0.0, 1.0);
    return inner * outer;
}

/// Emitted radiance and observed colour for one disk crossing.
///
/// A blackbody seen through a redshift factor g is *still* a blackbody, at
/// temperature g*T. So shifting the temperature and then integrating the
/// spectrum reproduces the beaming factor exactly: the bolometric intensity
/// goes as (gT)^4 = g^4 T^4, which is the Liouville result I_obs = g^4 I_emit.
inline Color shade(const DiskHit& hit, const DiskParams& d) {
    const double shape = temperatureProfile(hit.radius, d.innerRadius);
    if (shape <= 0.0) return {0, 0, 0};

    const double tEmit = d.peakTemperature * shape;
    const double tObs = d.relativistic ? hit.g * tEmit : tEmit;
    if (tObs <= 0.0) return {0, 0, 0};

    // (T_obs / T_peak)^4 keeps the numbers near unity so the exposure control
    // has a sane range regardless of the temperature the user dials in.
    const double ratio = tObs / d.peakTemperature;
    const double intensity = ratio * ratio * ratio * ratio;

    return color::blackbodyTable()(tObs) * (intensity * d.brightness);
}

}  // namespace disk

/// Locates crossings of the disk plane along a marched geodesic.
///
/// The plane test simplifies beautifully. A point on the geodesic is
/// r(phi) [cos(phi) e1 + sin(phi) e2], and r > 0 always, so the *sign* of the
/// height above the disk plane is just
///
///     s(phi) = A cos(phi) + B sin(phi),   A = e1.axis, B = e2.axis
///
/// which does not involve r at all: the crossing angles are known in closed
/// form. We only need r there, which the Hermite interpolant supplies to third
/// order without shrinking the step.
class DiskIntersector {
public:
    DiskIntersector(const GeodesicFrame& frame, const DiskParams& params)
        : frame_(frame), params_(params),
          a_(dot(frame.e1, params.axis)), b_(dot(frame.e2, params.axis)) {
        // Ray plane and disk plane coincident: every point is "in" the disk.
        // Treat as a grazing miss rather than dividing by zero.
        coplanar_ = (std::fabs(a_) < 1e-9 && std::fabs(b_) < 1e-9);
    }

    /// Height function above the disk plane, up to a positive factor of r.
    double heightSign(double phi) const { return a_ * std::cos(phi) + b_ * std::sin(phi); }

    /// Test one integrator step for a crossing. Returns a hit only if the
    /// crossing radius lies within the disk annulus.
    DiskHit test(const MarchSegment& seg) const {
        DiskHit hit;
        if (coplanar_ || !params_.enabled) return hit;

        const double s0 = heightSign(seg.phi0);
        const double s1 = heightSign(seg.phi1);
        if (s0 == 0.0 || (s0 < 0.0) == (s1 < 0.0)) return hit;  // no sign change

        // Bisect the (smooth, monotonic across one small step) height function.
        double lo = seg.phi0, hi = seg.phi1;
        for (int i = 0; i < 40; ++i) {
            const double mid = 0.5 * (lo + hi);
            if ((heightSign(mid) < 0.0) == (s0 < 0.0)) lo = mid; else hi = mid;
        }
        const double phiCross = 0.5 * (lo + hi);

        const double h = seg.phi1 - seg.phi0;
        const double s = h != 0.0 ? (phiCross - seg.phi0) / h : 0.0;
        const double u = hermiteU(seg.y0, seg.y1, h, std::clamp(s, 0.0, 1.0));
        if (!(u > 0.0)) return hit;

        const double r = 1.0 / u;
        if (r < params_.innerRadius || r > params_.outerRadius) return hit;

        hit.valid = true;
        hit.radius = r;
        hit.phi = phiCross;
        hit.position = frame_.positionAt(phiCross, u);

        // Impact parameter about the disk axis, with the backward-tracing sign
        // flip described in disk::redshiftFactor().
        const PhotonState yCross{u, interpDerivative(seg, s)};
        const double bMag = impactParameter(yCross);
        const double bAxis = -bMag * dot(frame_.normal, params_.axis);
        hit.g = params_.relativistic ? disk::redshiftFactor(r, bAxis, params_.prograde) : 1.0;

        hit.temperature = params_.peakTemperature *
                          disk::temperatureProfile(r, params_.innerRadius);
        return hit;
    }

    /// Azimuth of a hit within the disk plane, for texturing.
    double azimuthOf(const DiskHit& hit, const Vec3& u_hat, const Vec3& v_hat) const {
        return std::atan2(dot(hit.position, v_hat), dot(hit.position, u_hat));
    }

private:
    /// du/dphi from the derivative of the same cubic Hermite used for u.
    static double interpDerivative(const MarchSegment& seg, double s) {
        s = std::clamp(s, 0.0, 1.0);
        const double h = seg.phi1 - seg.phi0;
        const double s2 = s * s;
        const double dh00 = 6.0 * s2 - 6.0 * s;
        const double dh10 = 3.0 * s2 - 4.0 * s + 1.0;
        const double dh01 = -6.0 * s2 + 6.0 * s;
        const double dh11 = 3.0 * s2 - 2.0 * s;
        if (h == 0.0) return seg.y0[1];
        return (dh00 * seg.y0[0] + dh01 * seg.y1[0]) / h + dh10 * seg.y0[1] + dh11 * seg.y1[1];
    }

    GeodesicFrame frame_;
    DiskParams params_;
    double a_, b_;
    bool coplanar_ = false;
};

}  // namespace sre
