# The physics behind the engine

This document derives what the code does and states plainly what it does not
do. Every equation here corresponds to something in `include/sre/` or
`shaders/blackhole.frag`, and most are pinned by a case in `tests/`.

## Units

All ray tracing happens in **geometrised units with the Schwarzschild radius as
the length unit**: `r_s = 1` and `c = 1`.

This is not a cosmetic choice. Version 1 of this project integrated in SI, with
`r ~ 1e12 m` and `dr/dt ~ 1e8 m/s`. The terms of the geodesic equation then
span some twenty orders of magnitude, and double precision loses most of its
significant digits to cancellation before the ray has gone anywhere. In `r_s`
units every quantity the integrator touches is `O(1..100)`, so the error budget
goes into the truncation error of the scheme, where it belongs.

The geometry is scale free: a stellar-mass black hole and Sagittarius A* bend
light through *identical* angles at the same `r/r_s`. The renderer therefore
never needs to know the mass at all. Mass enters only when converting back to
metres or seconds, and when computing a physically motivated disk temperature.

Landmarks, in these units:

| Quantity | Value | In terms of `M = GM/c^2` |
|---|---|---|
| Event horizon | `r = 1` | `2M` |
| Photon sphere | `r = 1.5` | `3M` |
| ISCO | `r = 3` | `6M` |
| Critical impact parameter `b_c` | `2.598076` | `3 sqrt(3) M` |
| Apparent shadow diameter | `5.196` | `6 sqrt(3) M` |

## The metric

The Schwarzschild solution, the unique static spherically symmetric vacuum
solution of Einstein's field equations:

```
ds^2 = -f(r) c^2 dt^2 + dr^2 / f(r) + r^2 (dtheta^2 + sin^2(theta) dphi^2)

f(r) = 1 - r_s / r,     r_s = 2GM/c^2
```

`f` vanishing at `r = r_s` is a coordinate artefact, not a physical
singularity; nothing in the renderer needs to cross it, because a photon that
reaches the horizon never comes back.

## Null geodesics: the equation actually integrated

Spherical symmetry means the photon's angular momentum vector is conserved, so
**every geodesic is planar**. That collapses the 3D problem to a single ODE in
the plane spanned by the ray's starting position and its direction.

Two Killing vectors give two conserved quantities along the geodesic:

```
E = f(r) dt/dlambda          (energy)
L = r^2 dphi/dlambda         (angular momentum)
```

Substituting `u = r_s / r` and eliminating the affine parameter `lambda` in
favour of `phi` gives the orbit equation this engine integrates:

```
d^2u/dphi^2 = -u + (3/2) u^2
```

The first term alone gives `u = A cos(phi + B)`, which is a straight line in
polar coordinates — the flat-space answer. The `(3/2) u^2` term *is* general
relativity, and it is the entire difference between a straight line and the
image on the front page.

**Why this form rather than the second-order equations in `r` and `phi`?**

- `phi` is monotonic along the ray, so it is a genuine independent variable,
  whereas `dr/dlambda` passes through zero at periapsis and forces a tiny step
  there for no physical reason.
- No term blows up outside the horizon. Integrating in `lambda` requires
  `dt/dlambda = E/f`, which diverges as `r -> r_s`.
- The affine parameter has no geometric meaning and never needs to be computed.

The conserved impact parameter provides a free, exact error check at every step:

```
(du/dphi)^2 + u^2 (1 - u) = 1/b^2 = constant,    b = |L| / E
```

`tests/test_main.cpp` asserts this drifts by less than `1e-8` over a full
traversal. Drift in this number *is* the integration error, which makes it a
far better test than comparing against a stored image.

## Integration

`include/sre/integrator.hpp` implements **Cash-Karp 5(4)**: a fifth-order
Runge-Kutta step with an embedded fourth-order solution. The difference between
them estimates the local error, and the step size is steered to keep it inside
a tolerance. Near the photon sphere the step collapses automatically; far away
it stretches out and costs nothing.

Version 1 used forward Euler at a fixed step. Euler is first order, and worse,
it is *unstable* on an oscillatory problem like a bound photon orbit: the
amplitude grows without bound however small the step. `tests/` measures the
observed convergence order of the RK4 path and requires it to be ~4.

The GPU path in `shaders/blackhole.frag` uses **fixed-step RK4** instead.
Adaptive stepping makes neighbouring pixels take wildly different numbers of
steps, and a GPU warp runs at the speed of its slowest lane. A fixed step of
~0.02 rad in `phi` is comfortably inside RK4's accuracy for this problem.

### Locating events inside a step

Both the disk crossing and the far-field cutoff need a value *between* two
integrator samples. Since the scheme gives `u` and `du/dphi` at both ends of a
step, a **cubic Hermite interpolant** is available for free and is third-order
accurate — enough to place an event precisely without shrinking the step.

The disk-plane test has a pleasant simplification. A point on the geodesic is
`r(phi) [cos(phi) e1 + sin(phi) e2]` and `r > 0` always, so the sign of the
height above the disk plane is

```
s(phi) = A cos(phi) + B sin(phi),    A = e1 . axis,  B = e2 . axis
```

which does not involve `r` at all. The crossing angles are known in closed form
and only `r` there has to be interpolated.

## The camera is a static observer

Directions coming out of `sre::Camera` are unit vectors in the **local
orthonormal frame** of an observer at rest at that radius — the angles a real
observer there would measure. Converting them to Schwarzschild coordinate
components requires the tetrad

```
e_r = sqrt(f) d/dr,    e_theta = (1/r) d/dtheta,    e_phi = (1/r) d/dphi
```

so the radial component picks up a factor of `sqrt(f(r))` that the transverse
components do not. `sre::makeFrame()` applies it. With the factor included, the
relation between a pixel's angle and the photon's impact parameter is exactly

```
sin(theta) = b sqrt(f(r)) / r
```

and the edge of the shadow lands at `sin(theta) = b_c sqrt(f) / r` at *any*
radius. The obvious flat-space shortcut (omitting `sqrt(f)`) is harmless at
`r ~ 100 r_s` but is an 8% error at `r = 6 r_s`. `tests/` checks the shadow
radius at 6, 12 and 60 `r_s`.

## Light deflection

Integrating the orbit equation from far away, through periapsis, and back out
gives the total deflection. Two limits check the implementation from opposite
ends:

**Weak field.** `alpha -> 2 r_s / b = 4GM/(bc^2)`, the prediction Eddington's
1919 eclipse expedition confirmed. For light grazing the Sun this is 1.75
arcseconds, and `tests/` checks that number directly.

The next term in the expansion is also recovered:

```
alpha = 2 (r_s/b) + (15 pi / 16) (r_s/b)^2 + O((r_s/b)^3)
```

Matching the second-order coefficient to a fraction of a percent is a much
stronger statement about the integrator than matching the leading term.

**Strong field.** As `b -> b_c = 3 sqrt(3) M` from above, `alpha` diverges
*logarithmically*: the photon winds around the hole arbitrarily many times
before escaping. This produces the infinite stack of ever-fainter higher-order
images pressed against the edge of the shadow. Below `b_c` the photon is
captured. `tests/` bisects on capture-versus-escape and requires the boundary
to land on the analytic `b_c` to one part in `1e6`.

## The accretion disk

A geometrically thin, optically thick disk of matter on circular Keplerian
orbits between an inner radius (the ISCO by default) and an outer radius.

### Temperature

The Shakura-Sunyaev thin-disk effective temperature profile:

```
T(r) ~ r^(-3/4) [1 - sqrt(r_in/r)]^(1/4)
```

The bracket is the zero-torque inner boundary condition: no stress is
transmitted across the inner edge, so emission falls to zero exactly there
rather than diverging. Writing `x = sqrt(r_in/r)` turns the profile into
`x^(3/2) (1-x)^(1/4)`, which peaks at `x = 6/7`, i.e. `r = (49/36) r_in`.

### Redshift, Doppler shift, and beaming

All three are one number. For a photon reaching a distant static observer from
matter on a circular orbit at radius `r`:

```
g = nu_observed / nu_emitted = 1 / [ u^t (1 - Omega L/E) ]

u^t   = 1 / sqrt(1 - 3 r_s / 2r)      time dilation of the orbiting matter
Omega = sqrt(GM/r^3)                  coordinate angular velocity (exact here)
L/E                                   photon angular momentum about the disk axis
```

- With `L = 0` this reduces to `g = sqrt(1 - 3 r_s / 2r)`, pure gravitational
  redshift.
- Far away it reduces to `1/(1 - v/c)`, the ordinary special-relativistic
  Doppler factor. `tests/` checks both limits.
- `u^t` diverges at `r = 1.5 r_s`, which is why no material circular orbit
  exists at or inside the photon sphere.

Specific intensity transforms as `I_obs = g^4 I_emit` (Liouville's theorem
applied to `I_nu / nu^3`). Conveniently, a redshifted blackbody is still a
blackbody, at temperature `g T`. So the engine simply shades at `g T` and lets
`sigma (gT)^4 = g^4 sigma T^4` reproduce the beaming factor exactly, with the
hue shift coming along for free.

**That `g^4` is why the images are lopsided.** The disk's inner regions orbit at
a large fraction of `c`; the approaching side is beamed towards the observer and
comes out several times brighter than the receding side. It is the same effect
that makes the EHT images of M87* and Sgr A* asymmetric rings rather than
uniform ones.

### Sign convention

Rays are traced *backwards*, from the camera into the scene, so the physical
photon's momentum is opposite to the marching direction and its angular
momentum is `-b (n_hat . axis)`. Getting this sign wrong flips which side of
the disk is bright — so the test suite checks the brightness asymmetry
end-to-end, in image space, against an independently computed orbital velocity.

## Colour

The disk glows because it is hot, so its colour is not a free parameter. The
pipeline is:

1. **Planck's law** at the observed temperature `gT`, sampled across 380-780 nm.
2. **CIE 1931 colour matching functions**, using the analytic multi-lobe
   Gaussian fits of Wyman, Sloan & Shirley (JCGT 2013).
3. **XYZ to linear sRGB** (Rec.709 primaries, D65 white), desaturating rather
   than clipping any out-of-gamut result so hue stays stable.
4. **Hue-preserving tone mapping**: the ACES curve is applied to *luminance*
   and chroma is carried through. Applying a tone curve per channel — the usual
   approach — washes highlights to white, which would throw away exactly the
   temperature information steps 1-3 just computed.
5. **sRGB transfer function** for display.

`tests/` checks the spectral peak against Wien's displacement law, the ordering
of blue/red ratio with temperature, and that a saturated orange survives tone
mapping as an orange.

The GPU path substitutes an analytic Planckian-locus fit (Kim et al. 2002) for
steps 1-2, because an 80-sample spectral integral per pixel per frame is not
affordable in real time.

### A note on realism

The default 5500 K disk is an **artistic choice**, not a prediction. A real
thin disk around a stellar-mass black hole peaks in soft X-rays at ~10^7 K;
even a supermassive one runs to ~10^5 K, well into the ultraviolet. Rendered
honestly, both are featureless blue-white. The warm golden disks in films are
deliberate.

`sre::disk::physicalPeakTemperature(mass, eddingtonRatio)` computes the real
number from

```
T_eff(r) = [ 3 G M Mdot / (8 pi sigma_SB r^3) (1 - sqrt(r_in/r)) ]^(1/4)
```

with `Mdot` set by a fraction of the Eddington rate at the Schwarzschild
radiative efficiency `eta = 1 - sqrt(8/9) = 0.057`. `tests/` checks it lands in
the X-ray band for a stellar-mass hole, the UV for a supermassive one, and
scales as `M^(-1/4)`.

## What is deliberately not modelled

Being explicit about this matters more than the feature list:

- **Rotation (the Kerr metric).** Real black holes spin. Spin drags the ISCO
  inwards (to `r_s/2` for a maximal spin), makes the shadow non-circular, and
  adds frame dragging. This engine is Schwarzschild only: non-rotating,
  uncharged. Kerr needs the full four-dimensional geodesic equations, because
  the orbits are no longer planar.
- **A moving camera.** The observer is static. A camera on a circular orbit
  would see the whole image aberrated and Doppler shifted by its own velocity,
  on top of everything the disk does.
- **Disk thickness and radiative transfer.** The disk is infinitely thin and
  either fully opaque or crudely attenuated. There is no scattering, no
  absorption along the path, no self-shadowing of a geometrically thick flow.
- **Secondary illumination.** The disk does not light itself, and there is no
  returning radiation — light that leaves the disk, is bent around the hole,
  and lands back on the disk. In a real system that is a real effect.
- **Time delay.** Different paths to the camera have different travel times.
  The animation advances all radii by the same coordinate time.
- **Polarisation**, which the EHT actually measures.

## References

- Schwarzschild, K. (1916), *Über das Gravitationsfeld eines Massenpunktes nach
  der Einsteinschen Theorie*.
- Misner, Thorne & Wheeler (1973), *Gravitation*, chapter 25 — geodesics in
  Schwarzschild geometry.
- Shakura & Sunyaev (1973), *Black holes in binary systems: observational
  appearance*, A&A 24, 337.
- Luminet, J.-P. (1979), *Image of a spherical black hole with thin accretion
  disk*, A&A 75, 228 — the first such image ever computed.
- James, von Tunzelmann, Franklin & Thorne (2015), *Gravitational lensing by
  spinning black holes in astrophysics, and in the movie Interstellar*,
  Class. Quantum Grav. 32, 065001.
- Event Horizon Telescope Collaboration (2019, 2022), first images of M87* and
  Sgr A*.
- Cash & Karp (1990), *A variable order Runge-Kutta method for initial value
  problems with rapidly varying right-hand sides*, ACM TOMS 16, 201.
- Wyman, Sloan & Shirley (2013), *Simple Analytic Approximations to the CIE XYZ
  Color Matching Functions*, JCGT 2(2).
