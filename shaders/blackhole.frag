#version 330 core

// Real-time Schwarzschild ray tracer.
//
// This is the same physics as include/sre/geodesic.hpp, ported to run once per
// pixel per frame. Two deliberate differences from the CPU path:
//
//   * fixed-step RK4 instead of adaptive Cash-Karp. Adaptive stepping makes
//     neighbouring pixels take wildly different numbers of steps, and on a GPU
//     the whole warp pays for its slowest lane. A fixed step of ~0.02 rad in
//     phi is well inside RK4's accuracy here and keeps every lane in lockstep.
//
//   * an analytic Planckian-locus fit instead of integrating the CIE colour
//     matching functions. tests/ checks the two agree to a few percent.

in vec2 vUV;
out vec4 fragColor;

uniform vec2  uResolution;
uniform vec3  uCamPos;          // in Schwarzschild radii
uniform vec3  uCamForward;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;

uniform vec3  uDiskAxis;
uniform float uDiskInner;
uniform float uDiskOuter;
uniform float uDiskTemp;        // kelvin at the profile peak
uniform float uDiskBrightness;
uniform float uTurbulence;
uniform float uProgradeSign;
uniform float uTime;            // disk animation phase
uniform int   uDiskEnabled;
uniform int   uRelativistic;

uniform float uStarDensity;
uniform float uStarBrightness;
uniform float uGalaxyBrightness;
uniform vec3  uGalaxyAxis;
uniform uint  uSeed;
uniform int   uSkyEnabled;

uniform float uExposure;
uniform int   uMaxSteps;
uniform float uStepSize;        // radians of phi
uniform float uEscapeRadius;

const float PI = 3.14159265358979;

// ---------------------------------------------------------------------------
// Geodesic
// ---------------------------------------------------------------------------

// y = (u, du/dphi) with u = r_s / r. Units: r_s = 1, c = 1.
vec2 rhs(vec2 y) { return vec2(y.y, -y.x + 1.5 * y.x * y.x); }

vec2 rk4(vec2 y, float h) {
    vec2 k1 = rhs(y);
    vec2 k2 = rhs(y + 0.5 * h * k1);
    vec2 k3 = rhs(y + 0.5 * h * k2);
    vec2 k4 = rhs(y + h * k3);
    return y + (h / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

// b = |L|/E, conserved along the geodesic.
float impactParameter(vec2 y) {
    float inv_b2 = y.y * y.y + y.x * y.x * (1.0 - y.x);
    return inv_b2 > 0.0 ? inversesqrt(inv_b2) : 1e20;
}

// Cubic Hermite interpolation of u inside one step.
float hermiteU(vec2 y0, vec2 y1, float h, float s) {
    float s2 = s * s, s3 = s2 * s;
    return (2.0 * s3 - 3.0 * s2 + 1.0) * y0.x
         + h * (s3 - 2.0 * s2 + s) * y0.y
         + (-2.0 * s3 + 3.0 * s2) * y1.x
         + h * (s3 - s2) * y1.y;
}

float hermiteDU(vec2 y0, vec2 y1, float h, float s) {
    float s2 = s * s;
    if (h == 0.0) return y0.y;
    return ((6.0 * s2 - 6.0 * s) * y0.x + (-6.0 * s2 + 6.0 * s) * y1.x) / h
         + (3.0 * s2 - 4.0 * s + 1.0) * y0.y + (3.0 * s2 - 2.0 * s) * y1.y;
}

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

vec3 xyzToRGB(vec3 c) {
    return vec3( 3.2404542 * c.x - 1.5371385 * c.y - 0.4985314 * c.z,
                -0.9692660 * c.x + 1.8760108 * c.y + 0.0415560 * c.z,
                 0.0556434 * c.x - 0.2040259 * c.y + 1.0572252 * c.z);
}

// Blackbody colour from the Planckian locus (Kim et al. 2002 cubic fit),
// normalised to a peak channel of 1 so brightness stays a separate control.
vec3 blackbodyRGB(float T) {
    T = clamp(T, 1000.0, 40000.0);
    float t = 1.0 / T, t2 = t * t, t3 = t2 * t;
    float x = (T < 4000.0)
        ? -0.2661239e9 * t3 - 0.2343589e6 * t2 + 0.8776956e3 * t + 0.179910
        : -3.0258469e9 * t3 + 2.1070379e6 * t2 + 0.2226347e3 * t + 0.240390;
    float x2 = x * x, x3 = x2 * x;
    float y;
    if (T < 2222.0)      y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    else if (T < 4000.0) y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    else                 y =  3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;

    if (y <= 0.0) return vec3(1.0);
    vec3 rgb = xyzToRGB(vec3(x / y, 1.0, (1.0 - x - y) / y));
    rgb -= min(min(rgb.r, min(rgb.g, rgb.b)), 0.0);   // desaturate into gamut
    return rgb / max(max(rgb.r, max(rgb.g, rgb.b)), 1e-6);
}

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float acesCurve(float v) {
    v = max(v, 0.0);
    return clamp((v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14), 0.0, 1.0);
}

// Hue-preserving tone map: the curve is applied to luminance so the disk's
// temperature gradient survives instead of clipping to white.
vec3 toneMap(vec3 c) {
    float y = luminance(c);
    if (y <= 0.0) return vec3(0.0);
    vec3 outc = c * (acesCurve(y) / y);
    float peak = max(outc.r, max(outc.g, outc.b));
    if (peak > 1.0) {
        float k = clamp((peak - 1.0) / peak, 0.0, 1.0) * 0.55;
        outc = mix(outc, vec3(acesCurve(y)), k);
        outc /= max(1.0, max(outc.r, max(outc.g, outc.b)));
    }
    return clamp(outc, 0.0, 1.0);
}

vec3 linearToSRGB(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// ---------------------------------------------------------------------------
// Noise and sky
// ---------------------------------------------------------------------------

float hash31(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p), f = p - i;
    vec3 w = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0, 0, 0)), n100 = hash31(i + vec3(1, 0, 0));
    float n010 = hash31(i + vec3(0, 1, 0)), n110 = hash31(i + vec3(1, 1, 0));
    float n001 = hash31(i + vec3(0, 0, 1)), n101 = hash31(i + vec3(1, 0, 1));
    float n011 = hash31(i + vec3(0, 1, 1)), n111 = hash31(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, w.x), mix(n010, n110, w.x), w.y),
               mix(mix(n001, n101, w.x), mix(n011, n111, w.x), w.y), w.z);
}

float fbm(vec3 p, int octaves) {
    float amp = 0.5, sum = 0.0, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise(p);
        norm += amp;
        p *= 2.03;
        amp *= 0.5;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

uint hashU32(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint hash3(int a, int b, int c, uint seed) {
    uint h = seed;
    h = hashU32(h ^ uint(a) * 0x9e3779b9u);
    h = hashU32(h ^ uint(b) * 0x85ebca6bu);
    h = hashU32(h ^ uint(c) * 0xc2b2ae35u);
    return h;
}

float toUnit(uint h) { return float(h >> 8) * (1.0 / 16777216.0); }

void cubeFace(vec3 d, out int face, out float u, out float v) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0.0 ? 0 : 1; u = d.z / a.x * (d.x > 0.0 ? -1.0 : 1.0); v = d.y / a.x;
    } else if (a.y >= a.z) {
        face = d.y > 0.0 ? 2 : 3; u = d.x / a.y; v = d.z / a.y * (d.y > 0.0 ? -1.0 : 1.0);
    } else {
        face = d.z > 0.0 ? 4 : 5; u = d.x / a.z * (d.z > 0.0 ? 1.0 : -1.0); v = d.y / a.z;
    }
}

vec3 stars(vec3 dir) {
    const int CELLS = 220;
    int face; float u, v;
    cubeFace(dir, face, u, v);

    float cu = (u * 0.5 + 0.5) * float(CELLS);
    float cv = (v * 0.5 + 0.5) * float(CELLS);
    int ci = int(floor(cu)), cj = int(floor(cv));

    float cellAngle = 1.5708 / float(CELLS);
    float sigma = cellAngle * 0.16;

    vec3 acc = vec3(0.0);
    for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
            int i = ci + di, j = cj + dj;
            if (i < 0 || j < 0 || i >= CELLS || j >= CELLS) continue;

            uint h = hash3(i, j, face * 7919 + 1, uSeed);
            if (toUnit(hashU32(h)) > 0.09 * uStarDensity) continue;

            float su = float(i) + toUnit(hashU32(h ^ 0xa511e9b3u));
            float sv = float(j) + toUnit(hashU32(h ^ 0x63d5a1c7u));
            float ddu = (cu - su) * cellAngle;
            float ddv = (cv - sv) * cellAngle;
            float d2 = ddu * ddu + ddv * ddv;
            float falloff = exp(-0.5 * d2 / (sigma * sigma));
            if (falloff < 1e-4) continue;

            float q = toUnit(hashU32(h ^ 0x1b873593u));
            float mag = pow(q, 6.0) * 9.0 + 0.02;
            float tq = toUnit(hashU32(h ^ 0x5bd1e995u));
            float kelvin = 2600.0 + pow(tq, 2.2) * 22000.0;
            acc += blackbodyRGB(kelvin) * (mag * falloff);
        }
    }
    return acc * uStarBrightness;
}

vec3 sampleSky(vec3 dir) {
    if (uSkyEnabled == 0) return vec3(0.0);
    vec3 c = stars(dir);
    if (uGalaxyBrightness > 0.0) {
        float s = dot(dir, normalize(uGalaxyAxis));
        float band = exp(-0.5 * (s / 0.16) * (s / 0.16));
        float clumps = fbm(dir * 9.0, 4) * 0.7 + fbm(dir * 31.0, 3) * 0.3;
        c += vec3(0.55, 0.62, 0.95) * (band * (0.35 + 0.9 * clumps) * uGalaxyBrightness);
    }
    return c;
}

// ---------------------------------------------------------------------------
// Accretion disk
// ---------------------------------------------------------------------------

// Shakura-Sunyaev profile, normalised to peak at 1.
float temperatureProfile(float r, float rIn) {
    if (r <= rIn) return 0.0;
    float shape = pow(rIn / r, 0.75) * pow(max(1.0 - sqrt(rIn / r), 0.0), 0.25);
    return shape / 0.48808848;
}

// g = nu_obs / nu_emit for matter on a circular orbit, seen from infinity.
float redshiftFactor(float r, float bSigned) {
    float s = 1.0 - 1.5 / r;
    if (s <= 0.0) return 0.0;
    float ut = inversesqrt(s);
    float omega = uProgradeSign * sqrt(0.5 / (r * r * r));
    float denom = ut * (1.0 - omega * bSigned);
    return denom > 1e-6 ? 1.0 / denom : 0.0;
}

float diskTexture(float r, float azimuth) {
    if (uTurbulence <= 0.0) return 1.0;
    float sheared = azimuth - uProgradeSign * sqrt(0.5 / (r * r * r)) * uTime;
    float ring = 7.0;
    float broad = fbm(vec3(cos(sheared) * ring, sin(sheared) * ring, r * 1.1), 4);
    float fine  = fbm(vec3(cos(sheared) * ring * 2.5, sin(sheared) * ring * 2.5, r * 2.8), 3);
    float n = 0.65 * broad + 0.35 * fine;
    return max(0.0, 1.0 + uTurbulence * (2.0 * n - 1.0));
}

float edgeFade(float r) {
    float w = 0.08 * (uDiskOuter - uDiskInner);
    if (w <= 0.0) return 1.0;
    return clamp((r - uDiskInner) / w, 0.0, 1.0) * clamp((uDiskOuter - r) / w, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    vec3 dir = normalize(uCamForward
                       + uCamRight * (ndc.x * aspect * uTanHalfFov)
                       + uCamUp * (ndc.y * uTanHalfFov));

    float r0 = length(uCamPos);
    if (r0 <= 1.0) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }

    // Build the geodesic's orbital plane.
    vec3 e1 = uCamPos / r0;
    float radialComp = dot(dir, e1);
    vec3 tangential = dir - e1 * radialComp;
    float tangComp = length(tangential);
    vec3 e2 = tangComp > 1e-8 ? tangential / tangComp
                              : normalize(cross(e1, abs(e1.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    tangComp = max(tangComp, 1e-8);
    vec3 normal = cross(e1, e2);

    // The direction is measured in the static observer's local orthonormal
    // frame, so the radial component carries a factor of sqrt(f(r)) when
    // converted to Schwarzschild coordinates. This is what puts the edge of
    // the shadow at the correct sin(theta) = b_c sqrt(f) / r. See
    // sre::makeFrame() in include/sre/geodesic.hpp.
    float sqrtF = sqrt(max(0.0, 1.0 - 1.0 / r0));
    vec2 y = vec2(1.0 / r0, -sqrtF * radialComp / (r0 * tangComp));
    float phi = 0.0;
    float h = uStepSize;

    // The disk plane test reduces to the sign of A cos(phi) + B sin(phi):
    // r cancels out, so the crossing angles are independent of the trajectory.
    vec3 axis = normalize(uDiskAxis);
    float A = dot(e1, axis), B = dot(e2, axis);
    bool coplanar = (abs(A) < 1e-7 && abs(B) < 1e-7);

    // Disk-plane basis for texturing.
    vec3 du = normalize(abs(axis.y) < 0.9 ? cross(axis, vec3(0, 1, 0)) : cross(axis, vec3(1, 0, 0)));
    vec3 dv = cross(axis, du);

    vec3 radiance = vec3(0.0);
    bool terminated = false;
    float uEscape = 1.0 / uEscapeRadius;

    for (int step = 0; step < uMaxSteps; ++step) {
        vec2 yPrev = y;
        float phiPrev = phi;

        y = rk4(y, h);
        phi += h;

        if (y.x <= 0.0 || y.x != y.x) { y = yPrev; phi = phiPrev; break; }

        // --- disk plane crossing inside this step ---------------------------
        if (uDiskEnabled != 0 && !coplanar) {
            float s0 = A * cos(phiPrev) + B * sin(phiPrev);
            float s1 = A * cos(phi) + B * sin(phi);
            if (s0 != 0.0 && (s0 < 0.0) != (s1 < 0.0)) {
                float lo = phiPrev, hi = phi;
                for (int k = 0; k < 20; ++k) {
                    float mid = 0.5 * (lo + hi);
                    if ((A * cos(mid) + B * sin(mid) < 0.0) == (s0 < 0.0)) lo = mid; else hi = mid;
                }
                float phiCross = 0.5 * (lo + hi);
                float sLocal = clamp((phiCross - phiPrev) / h, 0.0, 1.0);
                float uCross = hermiteU(yPrev, y, h, sLocal);

                if (uCross > 0.0) {
                    float r = 1.0 / uCross;
                    if (r >= uDiskInner && r <= uDiskOuter) {
                        vec2 yCross = vec2(uCross, hermiteDU(yPrev, y, h, sLocal));
                        // Backward ray tracing: the physical photon's angular
                        // momentum is opposite to the marching direction.
                        float bAxis = -impactParameter(yCross) * dot(normal, axis);
                        float g = (uRelativistic != 0) ? redshiftFactor(r, bAxis) : 1.0;

                        float shape = temperatureProfile(r, uDiskInner);
                        float tObs = g * uDiskTemp * shape;
                        if (tObs > 0.0) {
                            float ratio = tObs / uDiskTemp;
                            float intensity = ratio * ratio * ratio * ratio;
                            vec3 pos = (e1 * cos(phiCross) + e2 * sin(phiCross)) * r;
                            float az = atan(dot(pos, dv), dot(pos, du));
                            radiance += blackbodyRGB(tObs) * intensity * uDiskBrightness
                                      * diskTexture(r, az) * edgeFade(r);
                        }
                        terminated = true;   // optically thick
                        break;
                    }
                }
            }
        }

        if (y.x >= 0.999) { terminated = true; break; }   // through the horizon
        if (y.x <= uEscape) break;                        // out to the sky
    }

    if (!terminated && y.x > 0.0 && y.x <= uEscape) {
        float r = 1.0 / y.x;
        float drdphi = -y.y / (y.x * y.x);
        vec3 radialHat = e1 * cos(phi) + e2 * sin(phi);
        vec3 tangentHat = -e1 * sin(phi) + e2 * cos(phi);
        radiance += sampleSky(normalize(radialHat * drdphi + tangentHat * r));
    }

    fragColor = vec4(linearToSRGB(toneMap(radiance * uExposure)), 1.0);
}
