// Physical constants and the unit system used throughout the engine.
#pragma once

namespace sre {
namespace phys {

// SI values (CODATA 2018).
inline constexpr double c          = 299792458.0;       // m / s
inline constexpr double G          = 6.67430e-11;       // m^3 kg^-1 s^-2
inline constexpr double h_planck   = 6.62607015e-34;    // J s
inline constexpr double k_boltz    = 1.380649e-23;      // J / K
inline constexpr double M_sun      = 1.98892e30;        // kg

// Reference objects, so the simulation can be pointed at something real.
inline constexpr double M_sgr_a    = 8.54e36;           // kg, Sagittarius A*
inline constexpr double M_m87      = 1.29e40;           // kg, M87*

inline constexpr double pi         = 3.14159265358979323846;

}  // namespace phys

// ---------------------------------------------------------------------------
// Unit system
// ---------------------------------------------------------------------------
//
// All ray tracing happens in *geometrised units with the Schwarzschild radius
// as the length unit*, i.e. r_s = 1 and c = 1.
//
// This matters more than it looks. v1 of this project integrated in SI, where
// r ~ 1e12 m and dr/dt ~ 1e8 m/s: the terms of the geodesic equation spanned
// ~20 orders of magnitude and double precision lost most of the significant
// digits before the ray had moved anywhere. In r_s units every quantity that
// the integrator touches is O(1..100), so the error budget goes into the
// truncation error of the scheme instead of into cancellation.
//
// Landmarks in these units:
//     event horizon    r = 1
//     photon sphere    r = 1.5      (= 3 GM/c^2)
//     ISCO             r = 3        (= 6 GM/c^2)
//     critical impact  b = 2.598076 (= 3 sqrt(3) GM/c^2)
//
// Convert to and from SI with BlackHole::toMeters() / fromMeters().

}  // namespace sre
