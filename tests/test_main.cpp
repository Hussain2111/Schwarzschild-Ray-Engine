// Physics test suite for the Schwarzschild Ray Engine.
//
// These are not smoke tests. Almost every case below compares the code against
// a number that general relativity fixes independently -- a closed-form limit,
// an exactly conserved quantity, or a known convergence order -- so a wrong
// answer means the physics is wrong, not that a golden file drifted.
//
// No test framework: a tiny harness keeps the suite buildable anywhere.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sre/blackhole.hpp"
#include "sre/color.hpp"
#include "sre/disk.hpp"
#include "sre/geodesic.hpp"
#include "sre/image.hpp"
#include "sre/scene.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;
std::string g_currentTest;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("    FAIL  %s\n", what.c_str());
    }
}

void checkClose(double got, double expected, double relTol, const std::string& what) {
    const double denom = std::max(std::fabs(expected), 1e-30);
    const double rel = std::fabs(got - expected) / denom;
    ++g_checks;
    if (!(rel <= relTol)) {
        ++g_failures;
        std::printf("    FAIL  %s\n          got %.12g, expected %.12g (rel err %.3g > %.3g)\n",
                    what.c_str(), got, expected, rel, relTol);
    }
}

/// Absolute-tolerance comparison. Needed wherever the expected value is zero,
/// where a relative error is undefined.
void checkNear(double got, double expected, double absTol, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(got - expected) <= absTol)) {
        ++g_failures;
        std::printf("    FAIL  %s\n          got %.12g, expected %.12g (abs err %.3g > %.3g)\n",
                    what.c_str(), got, expected, std::fabs(got - expected), absTol);
    }
}

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Register {
    Register(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TEST(name)                                        \
    void name();                                          \
    Register reg_##name(#name, name);                     \
    void name()

using namespace sre;

// ---------------------------------------------------------------------------
// Metric landmarks
// ---------------------------------------------------------------------------

TEST(schwarzschild_radius_matches_definition) {
    // Sgr A*: r_s = 2GM/c^2. Independently: ~1.27e10 m for 4.3 million suns.
    const BlackHole sgr(phys::M_sgr_a);
    const double expected = 2.0 * phys::G * phys::M_sgr_a / (phys::c * phys::c);
    checkClose(sgr.schwarzschildRadius(), expected, 1e-15, "r_s = 2GM/c^2");
    checkClose(sgr.schwarzschildRadius(), 1.2684e10, 1e-3, "Sgr A* r_s ~ 1.27e10 m");

    // The Sun's Schwarzschild radius is a famous ~2.95 km.
    checkClose(BlackHole::fromSolarMasses(1.0).schwarzschildRadius(), 2953.25, 1e-3,
               "solar r_s ~ 2.95 km");

    // Scale invariance: r_s doubles with mass.
    checkClose(BlackHole::fromSolarMasses(20.0).schwarzschildRadius(),
               2.0 * BlackHole::fromSolarMasses(10.0).schwarzschildRadius(), 1e-14,
               "r_s is linear in M");
}

TEST(characteristic_radii) {
    checkClose(BlackHole::photonSphere(), 1.5, 1e-15, "photon sphere at 1.5 r_s (3GM/c^2)");
    checkClose(BlackHole::isco(), 3.0, 1e-15, "ISCO at 3 r_s (6GM/c^2)");
    checkClose(BlackHole::criticalImpactParameter(), 1.5 * std::sqrt(3.0), 1e-15,
               "b_c = 3 sqrt(3) GM/c^2");
    checkClose(BlackHole::criticalImpactParameter(), 2.59807621135, 1e-10, "b_c ~ 2.598 r_s");

    // f(r) vanishes exactly at the horizon and tends to 1 far away.
    checkNear(BlackHole::f(1.0), 0.0, 1e-15, "f(r_s) = 0");
    checkClose(BlackHole::f(1e9), 1.0, 1e-8, "f -> 1 at large r");
}

TEST(circular_orbit_kinematics) {
    // u^t diverges at the photon sphere: no material circular orbit exists there.
    check(BlackHole::circularOrbitGamma(1.5) == 0.0 ||
              !std::isfinite(BlackHole::circularOrbitGamma(1.5001)) ||
              BlackHole::circularOrbitGamma(1.5001) > 50.0,
          "u^t blows up approaching the photon sphere");

    // At the ISCO, u^t = 1/sqrt(1 - 3/(2*3)) = sqrt(2).
    checkClose(BlackHole::circularOrbitGamma(3.0), std::sqrt(2.0), 1e-12,
               "u^t = sqrt(2) at the ISCO");

    // Far away the orbit is Newtonian: v = sqrt(GM/r) -> Omega^2 r^3 = GM.
    const double r = 1e6;
    checkClose(BlackHole::orbitalOmega(r) * BlackHole::orbitalOmega(r) * r * r * r, 0.5, 1e-12,
               "Omega^2 r^3 = GM/c^2 = 0.5 r_s");
    checkClose(BlackHole::circularOrbitGamma(1e8), 1.0, 1e-7, "u^t -> 1 far from the hole");
}

// ---------------------------------------------------------------------------
// Light bending
// ---------------------------------------------------------------------------

TEST(weak_field_deflection_matches_einstein) {
    // alpha -> 2 r_s / b = 4GM/(bc^2). This is the 1919 eclipse prediction.
    for (const double b : {1e4, 1e3}) {
        const double alpha = deflectionAngle(b);
        check(alpha > 0.0, "ray with large b escapes");
        checkClose(alpha, 2.0 / b, 3e-3, "alpha = 2 r_s / b at b = " + std::to_string(b));
    }

    // Grazing the Sun: b = R_sun, r_s = 2.95 km. The classic 1.75 arcsec.
    const double rsSun = BlackHole::fromSolarMasses(1.0).schwarzschildRadius();
    const double bSun = 6.957e8 / rsSun;  // solar radius in units of the Sun's r_s
    const double arcsec = deflectionAngle(bSun) * 180.0 / phys::pi * 3600.0;
    checkClose(arcsec, 1.75, 5e-3, "light grazing the Sun bends by 1.75 arcsec");
}

TEST(second_order_deflection_term) {
    // The post-Newtonian expansion is
    //     alpha = 2 (r_s/b) + (15 pi / 16) (r_s/b)^2 + O((r_s/b)^3).
    // Recovering the second term to a fraction of a percent is a much stronger
    // statement about the integrator than matching the leading term alone.
    const double b = 100.0;
    const double alpha = deflectionAngle(b);
    const double predicted = 2.0 / b + (15.0 * phys::pi / 16.0) / (b * b);
    checkClose(alpha, predicted, 1e-3, "second-order deflection term at b = 100");
}

TEST(critical_impact_parameter_by_bisection) {
    // Bisect on capture-vs-escape. The boundary must be b_c = 3 sqrt(3)/2 r_s.
    double captured = 2.0, escaped = 4.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (captured + escaped);
        if (deflectionAngle(mid) < 0.0) captured = mid; else escaped = mid;
    }
    checkClose(0.5 * (captured + escaped), BlackHole::criticalImpactParameter(), 1e-6,
               "capture boundary equals the analytic b_c");
}

TEST(deflection_diverges_at_the_photon_sphere) {
    const double bc = BlackHole::criticalImpactParameter();
    // Approaching b_c from above, the deflection grows without bound and the
    // photon loops the hole more than once (alpha > 2 pi).
    const double a1 = deflectionAngle(bc * 1.01);
    const double a2 = deflectionAngle(bc * 1.0001);
    const double a3 = deflectionAngle(bc * 1.000001);
    check(a1 > 0.0 && a2 > a1 && a3 > a2, "deflection increases as b -> b_c");
    check(a3 > 2.0 * phys::pi, "photons near b_c wind more than a full turn");

    // The divergence is logarithmic, so each factor-of-100 step towards b_c
    // adds a roughly constant increment.
    const double d1 = a2 - a1, d2 = a3 - a2;
    check(d1 > 0.0 && d2 > 0.0 && std::fabs(d2 / d1 - 1.0) < 0.35,
          "divergence is logarithmic in (b - b_c)");
}

// ---------------------------------------------------------------------------
// The integrator itself
// ---------------------------------------------------------------------------

TEST(impact_parameter_is_conserved_along_a_ray) {
    // b is an exact constant of the motion, so its drift is pure numerical
    // error -- the cleanest possible integrator check.
    const GeodesicFrame frame = makeFrame({0.0, 0.0, 40.0}, normalize(Vec3{0.08, 0.0, -1.0}));
    const double b0 = impactParameter(frame.start);
    check(b0 > BlackHole::criticalImpactParameter(), "test ray is not captured");

    double worstDrift = 0.0;
    MarchConfig cfg;
    cfg.tolerance = Tolerance{1e-13, 1e-11, 1e-8, 0.2, 0.9};
    cfg.maxSteps = 20000;

    marchPhoton(frame, cfg, [&](const MarchSegment& seg) {
        worstDrift = std::max(worstDrift, std::fabs(impactParameter(seg.y1) - b0) / b0);
        return true;
    });
    check(worstDrift < 1e-8, "b drifts by less than 1e-8 over a full traversal (got " +
                                 std::to_string(worstDrift) + ")");
}

TEST(rk4_converges_at_fourth_order) {
    // Halving the step must cut the error by ~16x. This is what forward Euler,
    // used by v1 of this project, cannot do: it would only halve the error.
    auto errorForStep = [](double h) {
        PhotonState y{1.0 / 20.0, 0.02};
        const double b0 = impactParameter(y);
        double phi = 0.0;
        const int steps = static_cast<int>(1.0 / h);
        for (int i = 0; i < steps; ++i) {
            y = rk4Step(y, phi, h, nullGeodesicRHS);
            phi += h;
        }
        return std::fabs(impactParameter(y) - b0) / b0;
    };

    const double e1 = errorForStep(1.0 / 200.0);
    const double e2 = errorForStep(1.0 / 400.0);
    check(e1 > 0.0 && e2 > 0.0, "errors are non-zero and measurable");
    const double order = std::log2(e1 / e2);
    check(order > 3.5 && order < 4.6,
          "observed convergence order is ~4 (got " + std::to_string(order) + ")");
}

TEST(adaptive_stepper_respects_its_tolerance) {
    // A loose tolerance must produce a demonstrably larger error than a tight
    // one, otherwise the controller is not actually controlling anything.
    auto driftFor = [](double tol) {
        const GeodesicFrame frame = makeFrame({0.0, 0.0, 30.0}, normalize(Vec3{0.1, 0.0, -1.0}));
        const double b0 = impactParameter(frame.start);
        MarchConfig cfg;
        cfg.tolerance = Tolerance{tol * 1e-3, tol, 1e-9, 0.3, 0.9};
        cfg.maxSteps = 50000;
        double worst = 0.0;
        marchPhoton(frame, cfg, [&](const MarchSegment& seg) {
            worst = std::max(worst, std::fabs(impactParameter(seg.y1) - b0) / b0);
            return true;
        });
        return worst;
    };
    check(driftFor(1e-4) > driftFor(1e-10), "tighter tolerance yields a smaller error");
}

TEST(straight_line_limit_without_gravity) {
    // Far from the hole the orbit equation reduces to u'' = -u, whose solution
    // is a straight line in polar coordinates: r sin(phi + c) = const.
    PhotonState y{1.0 / 1e7, 0.0};  // periapsis of a very distant ray
    const double r0 = 1.0 / y[0];
    double phi = 0.0;
    const double h = 1e-3;
    for (int i = 0; i < 500; ++i) {
        y = rk4Step(y, phi, h, nullGeodesicRHS);
        phi += h;
    }
    // Straight line through periapsis r0 at phi = 0: r(phi) = r0 / cos(phi).
    checkClose(1.0 / y[0], r0 / std::cos(phi), 1e-6, "reduces to a straight line as r_s/r -> 0");
}

TEST(radial_ray_has_zero_angular_momentum) {
    const GeodesicFrame frame = makeFrame({0.0, 0.0, 25.0}, Vec3{0.0, 0.0, -1.0});
    check(frame.radial, "a ray aimed at the centre is flagged radial");
    checkNear(impactParameter(frame.start), 0.0, 1e-9, "b = 0 for a radial ray");
}

TEST(geodesic_frame_geometry) {
    const Vec3 origin{0.0, 0.0, 30.0};
    const Vec3 dir = normalize(Vec3{0.2, 0.1, -1.0});
    const GeodesicFrame f = makeFrame(origin, dir);

    checkClose(length(f.e1), 1.0, 1e-14, "e1 is a unit vector");
    checkClose(length(f.e2), 1.0, 1e-14, "e2 is a unit vector");
    checkNear(dot(f.e1, f.e2), 0.0, 1e-14, "e1 and e2 are orthogonal");
    checkNear(dot(f.normal, f.e1), 0.0, 1e-14, "the normal is perpendicular to the plane");

    // phi = 0 must reproduce the starting position exactly.
    const Vec3 p0 = f.positionAt(0.0, f.start[0]);
    checkNear(length(p0 - origin), 0.0, 1e-9, "positionAt(0) returns the ray origin");

    // The locally measured tangent at phi = 0 must be the launch direction:
    // makeFrame() and localDirectionAt() are inverse tetrad conversions.
    const Vec3 t0 = f.localDirectionAt(0.0, f.start);
    checkClose(dot(t0, dir), 1.0, 1e-12, "makeFrame/localDirectionAt round-trip exactly");

    // The coordinate-basis tangent differs from it by exactly sqrt(f) on the
    // radial component -- small at r = 30, but not zero.
    const Vec3 tCoord = f.directionAt(0.0, f.start);
    check(dot(tCoord, dir) < 1.0 - 1e-9,
          "the coordinate direction differs from the locally measured one");
    check(dot(tCoord, dir) > 0.9999, "and only slightly so at r = 30 r_s");

    // The direction must lie in the plane spanned by e1 and e2.
    checkNear(dot(dir, f.normal), 0.0, 1e-14, "the ray lies in its own orbital plane");
}

// ---------------------------------------------------------------------------
// Disk physics
// ---------------------------------------------------------------------------

TEST(temperature_profile_shape) {
    const double rIn = 3.0;
    checkNear(disk::temperatureProfile(rIn, rIn), 0.0, 1e-12,
              "zero-torque inner boundary: T = 0 at r_in");
    check(disk::temperatureProfile(2.0, rIn) == 0.0, "no emission inside r_in");

    // The maximum sits at r = (49/36) r_in and the profile is normalised to 1.
    const double rPeak = (49.0 / 36.0) * rIn;
    checkClose(disk::temperatureProfile(rPeak, rIn), 1.0, 1e-6, "profile peaks at 1");
    for (const double r : {4.5, 6.0, 10.0, 30.0})
        check(disk::temperatureProfile(r, rIn) <= 1.0 + 1e-9, "profile never exceeds its peak");

    // Far out it must fall off as r^-3/4.
    // Far enough out that the (1 - sqrt(r_in/r))^(1/4) boundary factor is
      // indistinguishable from 1 and the pure power law is left.
    const double t1 = disk::temperatureProfile(1e7, rIn);
    const double t2 = disk::temperatureProfile(4e7, rIn);
    checkClose(t1 / t2, std::pow(4.0, 0.75), 1e-4, "T ~ r^(-3/4) far from the inner edge");
}

TEST(redshift_factor_limits) {
    // Far away, with no transverse motion, there is no shift.
    checkClose(disk::redshiftFactor(1e8, 0.0, 1.0), 1.0, 1e-7, "g -> 1 far from the hole");

    // Purely gravitational (b = 0): g = sqrt(1 - 3 r_s / 2r) < 1, a redshift.
    const double gGrav = disk::redshiftFactor(6.0, 0.0, 1.0);
    checkClose(gGrav, std::sqrt(1.0 - 1.5 / 6.0), 1e-12, "g = 1/u^t when b = 0");
    check(gGrav < 1.0, "gravitational shift is a redshift");

    // Deeper in the well means a stronger redshift.
    check(disk::redshiftFactor(3.0, 0.0, 1.0) < disk::redshiftFactor(20.0, 0.0, 1.0),
          "redshift deepens closer to the hole");

    // Doppler. bSigned > 0 means the emitter orbits along the direction the
    // photon was emitted, so it is chasing its own light: a blueshift.
    const double r = 5.0;
    const double b = 3.0;
    const double blueshifted = disk::redshiftFactor(r, b, 1.0);
    const double redshifted = disk::redshiftFactor(r, -b, 1.0);
    check(blueshifted > redshifted, "co-rotating photon angular momentum is blueshifted");
    check(blueshifted > gGrav, "Doppler blueshift can overcome gravitational redshift");
    check(redshifted < gGrav, "receding motion deepens the gravitational redshift");

    // Reversing the disk's rotation must swap the two, since only the relative
    // sign of Omega and L matters.
    checkClose(disk::redshiftFactor(r, b, -1.0), redshifted, 1e-12,
               "flipping the orbit direction flips the shift");

    // Far-field limit: g -> 1/(1 - v/c) with v = Omega * b for a photon
    // emitted tangentially, recovering the ordinary special-relativistic
    // Doppler factor once the gravitational term switches off.
    const double rFar = 1e6;
    const double bFar = 1e4;
    const double omegaFar = BlackHole::orbitalOmega(rFar);
    checkClose(disk::redshiftFactor(rFar, bFar, 1.0), 1.0 / (1.0 - omegaFar * bFar), 1e-5,
               "reduces to the special-relativistic Doppler factor far away");
}

TEST(beaming_scales_as_g_to_the_fourth) {
    // Specific intensity transforms as I_obs = g^4 I_emit. Because a redshifted
    // blackbody is still a blackbody at temperature gT, shading at gT must
    // reproduce exactly that factor.
    DiskParams d;
    d.peakTemperature = 6000.0;
    d.turbulence = 0.0;

    const double rPeak = (49.0 / 36.0) * d.innerRadius;
    DiskHit a;
    a.valid = true;
    a.radius = rPeak;
    a.g = 1.0;
    DiskHit bHit = a;
    bHit.g = 1.5;

    const Color ca = disk::shade(a, d);
    const Color cb = disk::shade(bHit, d);
    const double ratio = color::luminance(cb) / color::luminance(ca);

    // g^4 in bolometric intensity, modulated by the change in the colour
    // vector's luminance as the blackbody hue shifts. Check the dominant
    // factor is g^4 to within the hue correction.
    check(ratio > 3.0 && ratio < 8.0,
          "brightness scales roughly as g^4 = 5.06 (got " + std::to_string(ratio) + ")");
}

TEST(disk_plane_crossing_is_found_exactly) {
    // Aim a ray from above the disk plane straight down through it and check
    // the intersector finds the crossing at the analytically known radius.
    DiskParams d;
    d.innerRadius = 2.0;
    d.outerRadius = 40.0;
    d.axis = Vec3{0.0, 1.0, 0.0};
    d.relativistic = false;

    const Vec3 origin{0.0, 12.0, 12.0};
    const Vec3 dir = normalize(Vec3{0.0, -1.0, 0.0});  // straight down
    const GeodesicFrame frame = makeFrame(origin, dir);
    const DiskIntersector intersector(frame, d);

    MarchConfig cfg;
    cfg.maxSteps = 8000;
    bool found = false;
    double hitRadius = 0.0;

    marchPhoton(frame, cfg, [&](const MarchSegment& seg) {
        const DiskHit h = intersector.test(seg);
        if (h.valid) {
            found = true;
            hitRadius = h.radius;
            checkNear(dot(h.position, d.axis), 0.0, 1e-6, "the hit lies in the disk plane");
            return false;
        }
        return true;
    });

    check(found, "a ray aimed through the disk plane registers a crossing");
    // Gravity pulls the ray inwards, so it lands at slightly less than z = 12.
    check(found && hitRadius > 9.0 && hitRadius <= 12.0,
          "crossing radius is bent inwards from the flat-space value of 12");
}

TEST(no_crossing_when_the_ray_misses_the_annulus) {
    DiskParams d;
    d.innerRadius = 3.0;
    d.outerRadius = 5.0;
    d.axis = Vec3{0.0, 1.0, 0.0};

    // Crosses the plane far outside the annulus.
    const Vec3 origin{0.0, 12.0, 60.0};
    const GeodesicFrame frame = makeFrame(origin, normalize(Vec3{0.0, -1.0, 0.0}));
    const DiskIntersector intersector(frame, d);

    bool found = false;
    MarchConfig cfg;
    cfg.maxSteps = 8000;
    marchPhoton(frame, cfg, [&](const MarchSegment& seg) {
        if (intersector.test(seg).valid) found = true;
        return true;
    });
    check(!found, "a plane crossing outside [r_in, r_out] is not a hit");
}

TEST(physical_disk_temperature_is_astrophysically_sane) {
    // A 10 solar-mass black hole accreting at 10% Eddington: soft X-rays,
    // ~10^7 K. A supermassive one is far cooler because T ~ M^(-1/4).
    const double stellar = disk::physicalPeakTemperature(10.0 * phys::M_sun, 0.1);
    check(stellar > 3e6 && stellar < 3e7,
          "stellar-mass disk peaks in soft X-rays (got " + std::to_string(stellar) + " K)");

    const double sgr = disk::physicalPeakTemperature(phys::M_sgr_a, 0.1);
    check(sgr > 1e4 && sgr < 1e6,
          "supermassive disk peaks in the UV (got " + std::to_string(sgr) + " K)");

    // T_peak ~ M^(-1/4) at fixed Eddington ratio.
    const double m1 = disk::physicalPeakTemperature(1e31, 0.1);
    const double m2 = disk::physicalPeakTemperature(1.6e32, 0.1);  // 16x the mass
    checkClose(m1 / m2, 2.0, 1e-6, "T_peak scales as M^(-1/4)");
}

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

TEST(blackbody_colours_are_physically_ordered) {
    const Color cool = color::blackbodyRGB(2000.0);
    const Color mid = color::blackbodyRGB(6500.0);
    const Color hot = color::blackbodyRGB(20000.0);

    check(cool.x > cool.z, "2000 K is red-dominant");
    check(hot.z > hot.x, "20000 K is blue-dominant");

    // D65-ish white at 6500 K: the channels should be within ~15% of each other.
    const double spread = (std::max({mid.x, mid.y, mid.z}) - std::min({mid.x, mid.y, mid.z}));
    check(spread < 0.25, "6500 K is near neutral white");

    // Blue/red ratio must increase monotonically with temperature.
    double previous = -1.0;
    for (const double t : {1500.0, 3000.0, 5000.0, 8000.0, 15000.0, 30000.0}) {
        const Color c = color::blackbodyRGB(t);
        const double ratio = c.z / std::max(c.x, 1e-9);
        check(ratio > previous, "blue/red ratio rises with temperature");
        previous = ratio;
    }
}

TEST(blackbody_table_matches_direct_computation) {
    for (const double t : {1000.0, 3000.0, 5778.0, 12000.0, 40000.0}) {
        const Color exact = color::blackbodyRGB(t);
        const Color cached = color::blackbodyTable()(t);
        checkClose(cached.x, exact.x, 0.05, "cached red channel at " + std::to_string(t) + " K");
        checkClose(cached.y, exact.y, 0.05, "cached green channel at " + std::to_string(t) + " K");
        checkClose(cached.z, exact.z, 0.05, "cached blue channel at " + std::to_string(t) + " K");
    }
}

TEST(planck_law_obeys_wien_displacement) {
    // The spectral peak must sit at lambda_max = b / T with b = 2.898e-3 m K.
    for (const double kelvin : {3000.0, 6000.0, 12000.0}) {
        double best = 0.0, bestLambda = 0.0;
        for (double l = 20.0; l < 4000.0; l += 0.2) {
            const double v = color::planck(l, kelvin);
            if (v > best) { best = v; bestLambda = l; }
        }
        checkClose(bestLambda * 1e-9 * kelvin, 2.897771955e-3, 2e-3,
                   "Wien's law at " + std::to_string(kelvin) + " K");
    }
}

TEST(tone_map_preserves_hue_and_range) {
    // Black stays black; the curve is monotonic and never leaves [0,1].
    const Color black = color::toneMap({0.0, 0.0, 0.0});
    checkNear(color::luminance(black), 0.0, 1e-15, "black maps to black");

    double previous = -1.0;
    for (const double v : {0.01, 0.1, 0.5, 1.0, 4.0, 100.0}) {
        const Color c = color::toneMap({v, v, v});
        check(c.x >= 0.0 && c.x <= 1.0, "tone mapped value stays in [0,1]");
        check(c.x > previous, "tone curve is monotonic");
        previous = c.x;
    }

    // A saturated orange must stay orange after mapping, not wash to white.
    const Color orange = color::toneMap(Color{4.0, 1.6, 0.4});
    check(orange.x > orange.y && orange.y > orange.z, "hue ordering survives tone mapping");
}

TEST(srgb_transfer_function_round_trips) {
    checkNear(color::linearToSRGB(0.0), 0.0, 1e-12, "sRGB(0) = 0");
    checkClose(color::linearToSRGB(1.0), 1.0, 1e-12, "sRGB(1) = 1");
    // Mid-grey: linear 0.2140 is sRGB 0.5.
    checkClose(color::linearToSRGB(0.21404114), 0.5, 1e-4, "linear 0.214 -> sRGB 0.5");
}

// ---------------------------------------------------------------------------
// Renderer integration
// ---------------------------------------------------------------------------

TEST(shadow_is_black_and_the_sky_is_not) {
    Scene scene;
    scene.camera = Camera::orbit(0.0, 0.0, 40.0);
    scene.disk.enabled = false;
    scene.sky.starDensity = 4.0;  // dense, so a random sky ray is very likely lit
    scene.quality.samplesPerPixel = 1;

    // Straight at the centre: must be captured.
    const Color centre = traceRay(scene, scene.camera.position,
                                  normalize(scene.camera.target - scene.camera.position));
    checkNear(color::luminance(centre), 0.0, 1e-12, "a ray aimed at the hole returns no light");

    // Aimed well away from the hole: must reach the sky. Average several
    // directions so the test does not depend on one star's placement.
    double total = 0.0;
    for (int i = 0; i < 64; ++i) {
        const double a = 2.0 * phys::pi * i / 64.0;
        const Vec3 dir = normalize(Vec3{std::cos(a), std::sin(a), -0.15});
        total += color::luminance(traceRay(scene, scene.camera.position, dir));
    }
    check(total > 0.0, "rays pointed away from the hole reach the star field");
}

TEST(apparent_shadow_radius_matches_theory) {
    // Sweep the ray angle away from the centre and find where capture stops.
    // For a static observer at radius r the shadow's angular radius satisfies
    //     sin(theta) = b_c sqrt(1 - r_s/r) / r
    // which is an image-space check of the 3 sqrt(3) M result *and* of the
    // tetrad factor in makeFrame(). Tested at three radii, including one close
    // enough in that dropping sqrt(f) would be an 8% error.
    Scene scene;
    for (const double robs : {60.0, 12.0, 6.0}) {
    scene.camera = Camera::orbit(0.0, 0.0, robs);
    scene.disk.enabled = false;
    scene.sky.enabled = false;

    const Vec3 origin = scene.camera.position;
    const Vec3 forward = normalize(scene.camera.target - origin);
    Vec3 right, up;
    orthonormalBasis(forward, right, up);

    auto captured = [&](double theta) {
        const Vec3 dir = normalize(forward * std::cos(theta) + right * std::sin(theta));
        PhotonState fs{};
        double fp = 0.0;
        const GeodesicFrame frame = makeFrame(origin, dir);
        return marchPhoton(frame, scene.quality.marchConfig(),
                           [](const MarchSegment&) { return true; }, &fs, &fp)
               != RayOutcome::Escaped;
    };

    double inside = 0.0, outside = 1.4;
    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (inside + outside);
        if (captured(mid)) inside = mid; else outside = mid;
    }
    const double theta = 0.5 * (inside + outside);
    const double expected = std::asin(BlackHole::criticalImpactParameter() *
                                      std::sqrt(1.0 - 1.0 / robs) / robs);
    checkClose(theta, expected, 2e-3,
               "shadow radius = asin(b_c sqrt(f) / r) at r = " + std::to_string(robs));
    }
}

TEST(lensing_makes_the_shadow_larger_than_the_horizon) {
    // The visual signature of strong lensing: the dark patch on the sky is
    // 2 b_c = 5.196 r_s across, more than 2.5x the horizon diameter of 2 r_s.
    const double ratio = 2.0 * BlackHole::criticalImpactParameter() / 2.0;
    check(ratio > 2.59 && ratio < 2.60, "the shadow is ~2.6x the horizon radius");
}

TEST(relativistic_beaming_brightens_the_approaching_side) {
    // End-to-end: render two strips of the disk and confirm the side rotating
    // towards the camera is the brighter one, and that switching relativity
    // off makes the image symmetric.
    Scene scene;
    scene.camera = Camera::orbit(0.0, 5.0 * phys::pi / 180.0, 22.0);
    scene.sky.enabled = false;
    scene.disk.turbulence = 0.0;
    scene.quality.samplesPerPixel = 1;

    const int w = 120, h = 68;
    auto halves = [&](double& left, double& right) {
        left = right = 0.0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const double lum = color::luminance(tracePixel(scene, x, y, w, h));
                (x < w / 2 ? left : right) += lum;
            }
    };

    double l = 0.0, r = 0.0;
    halves(l, r);
    check(l > 1.5 * r, "the approaching side is substantially brighter");

    scene.disk.relativistic = false;
    halves(l, r);
    checkNear(l, r, 1e-6 * std::max(l, 1.0),
              "without relativity the disk image is left-right symmetric");

    // And reversing the orbit must flip which side is bright.
    scene.disk.relativistic = true;
    scene.disk.prograde = -1.0;
    halves(l, r);
    check(r > 1.5 * l, "reversing the orbit swaps the bright side");
}

TEST(camera_ray_generation) {
    Camera cam;
    cam.position = Vec3{0.0, 0.0, 30.0};
    cam.target = Vec3{0.0, 0.0, 0.0};
    cam.fovDegrees = 60.0;

    const Vec3 centre = cam.rayDirection(0.0, 0.0, 16.0 / 9.0);
    checkClose(dot(centre, normalize(cam.target - cam.position)), 1.0, 1e-12,
               "the centre pixel looks straight at the target");

    // The half-angle at the top edge must equal fov/2.
    const Vec3 top = cam.rayDirection(0.0, 1.0, 16.0 / 9.0);
    const double angle = std::acos(std::clamp(dot(top, centre), -1.0, 1.0));
    checkClose(angle, 30.0 * phys::pi / 180.0, 1e-9, "vertical field of view is 60 degrees");

    for (const double aspect : {1.0, 16.0 / 9.0, 2.35})
        checkClose(length(cam.rayDirection(0.7, -0.3, aspect)), 1.0, 1e-12,
                   "generated rays are unit length");
}

TEST(png_encoder_produces_a_valid_file) {
    // Structural check of the hand-rolled encoder: signature, chunk layout,
    // declared size, and a well-formed zlib header.
    Image img(7, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 7; ++x) img.at(x, y) = Color{0.5, 0.25, 0.75};

    const std::vector<uint8_t> rgb = img.toSRGB8(1.0);
    check(rgb.size() == 7u * 5u * 3u, "8-bit buffer has the right size");

    const std::vector<uint8_t> data = png::encodeRGB8(rgb.data(), 7, 5);
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    bool sigOk = data.size() > 8;
    for (int i = 0; i < 8 && sigOk; ++i) sigOk = data[i] == sig[i];
    check(sigOk, "PNG signature is correct");

    // IHDR: width and height, big-endian, right after the 8-byte length+type.
    const uint32_t w = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    const uint32_t hh = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    check(w == 7 && hh == 5, "IHDR carries the image dimensions");
    check(data.size() > 60, "the file contains actual image data");

    // The stream must end with an IEND chunk.
    const size_t n = data.size();
    check(n > 12 && data[n - 8] == 'I' && data[n - 7] == 'E' && data[n - 6] == 'N' &&
              data[n - 5] == 'D',
          "the file ends with IEND");
}

TEST(bloom_conserves_the_dark_and_brightens_the_bright) {
    Image img(32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) img.at(x, y) = Color{0.0, 0.0, 0.0};
    img.at(16, 16) = Color{50.0, 50.0, 50.0};

    const double before = color::luminance(img.at(20, 16));
    img.applyBloom(0.9, 0.5, 6);
    check(color::luminance(img.at(20, 16)) > before, "light spreads into neighbouring pixels");
    check(color::luminance(img.at(0, 0)) >= 0.0, "far corners stay non-negative");
    check(color::luminance(img.at(16, 16)) > 50.0, "the source pixel keeps its energy");
}

}  // namespace

int main() {
    std::printf("Schwarzschild Ray Engine -- physics test suite\n");
    std::printf("=============================================\n\n");

    int failedTests = 0;
    for (const TestCase& t : registry()) {
        const int before = g_failures;
        std::printf("  %-52s", t.name);
        std::fflush(stdout);
        t.fn();
        if (g_failures > before) {
            ++failedTests;
            std::printf("  <-- FAILED\n");
        } else {
            std::printf("ok\n");
        }
    }

    std::printf("\n%d checks in %zu tests, %d failures\n", g_checks, registry().size(), g_failures);
    if (failedTests == 0) std::printf("All tests passed.\n");
    return failedTests == 0 ? 0 : 1;
}
