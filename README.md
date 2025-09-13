# Black Hole Light Ray Simulation

A C++ implementation of light ray trajectories around black holes using Einstein's general relativity equations.

## Overview

This simulation solves the **Schwarzschild geodesic equations** to demonstrate how light rays curve around black holes in curved spacetime. It implements the core physics behind black hole visualizations used in scientific research and movies like Interstellar.

## Features

- **Accurate Physics**: Implements Einstein's field equations for Schwarzschild spacetime
- **Multiple Ray Types**: Simulates rays with different angular momentum values
- **Real Black Hole Parameters**: Uses Sagittarius A* (our galaxy's central black hole) specifications
- **Event Horizon Detection**: Tracks when light rays cross the point of no return

## Physics Implemented

### Core Equations
- **Schwarzschild Metric**: Describes curved spacetime around a non-rotating black hole
- **Geodesic Equations**: Determines how light follows the curvature of spacetime
- **Conserved Quantities**: Energy (E) and angular momentum (L) remain constant along light paths

### Key Constants
```cpp
double c = 299792458;        // Speed of light (m/s)
double G = 6.67430e-11;      // Gravitational constant
double mass = 8.54e36;       // Mass of Sagittarius A* (kg)
```

## Code Structure

### Classes
- **`BlackHole`**: Represents the black hole with mass and Schwarzschild radius
- **`Ray`**: Represents a light ray with position, velocity, and conserved quantities

### Key Functions
- **`calAcc()`**: Calculates acceleration from geodesic equations
- **`stepRay()`**: Updates ray position using numerical integration
- **Multiple ray simulation**: Demonstrates different trajectory types

## Compilation

```bash
g++ 1.cpp -o blackhole
./blackhole
```

### Requirements
- C++11 or later
- Standard math library

## Sample Output

```
Mass: 8.54e+36 kg
Schwarzschild radius: 1.26839e+10 meters
Position: (0, 0)

=== Multiple Ray Simulation ===
Initial conditions:
Ray 1 - L=0 (radial)
Ray 2 - L=1.49896e+17 (low angular momentum)
Ray 3 - L=4.49689e+17 (high angular momentum)

Step 0:
Ray1: r=470.021e9, phi=0
Ray2: r=476.017e9, phi=6.57105e-05
Ray3: r=485.01e9, phi=0.000190661
A ray hit the event horizon!
```

## Ray Types Demonstrated

1. **Radial Ray** (L=0): Falls straight toward black hole
2. **Low Angular Momentum**: Slight spiral trajectory
3. **High Angular Momentum**: More pronounced curvature before infall

## Mathematical Foundation

The simulation solves these fundamental equations:

**Energy Conservation:**
```
E = f * sqrt(dr²/f² + r²dφ²/f)
```

**Angular Momentum:**
```
L = r² * dφ/dt
```

**Geodesic Equations:**
```
d²r/dt² = -(rs/2r²)f(dt/dλ)² + (rs/2r²f)(dr/dλ)² + (r-rs)(dφ/dλ)²
d²φ/dt² = -2(dr/dλ)(dφ/dλ)/r
```

Where:
- `f = 1 - rs/r` (metric factor)
- `rs = 2GM/c²` (Schwarzschild radius)

## Development Status

- ✅ Core physics engine (Schwarzschild geodesics)
- ✅ Multiple ray simulation
- ✅ Numerical integration (Euler method)
- ⏳ Graphics visualization (planned)
- ⏳ Interactive controls (planned)
- ⏳ 3D extension (future work)

## Future Enhancements

- **Graphics**: Real-time visualization with OpenGL
- **Advanced Integration**: Runge-Kutta 4th order method
- **Spinning Black Holes**: Kerr metric implementation  
- **Accretion Disk**: Hot gas visualization
- **Ray Tracing**: Photorealistic image generation

## Educational Value

This project demonstrates:
- General relativity in action
- Numerical methods for differential equations
- Conservation laws in curved spacetime
- Real astrophysics calculations

## References

- Schwarzschild, K. (1916). "On the Gravitational Field of a Mass Point"
- Misner, Thorne, Wheeler. "Gravitation" (1973)
- Modern computational astrophysics techniques

---

**Note**: This simulation uses the same mathematical framework as professional black hole visualization software. The physics is accurate to general relativity - only the graphics rendering is simplified.
