// ===========================================================================
// Version 1, kept for reference. This is NOT built and NOT used.
//
// The original single-file simulation: it integrated the Schwarzschild
// geodesic equations in SI units with forward Euler and printed r and phi to
// the console. The engine in ../include/sre/ replaces it. What changed, and
// why, in case any of it is useful to anyone reading both:
//
//  1. UNITS. Integrating with r ~ 1e12 m and dr/dt ~ 1e8 m/s makes the terms
//     of the geodesic equation span ~20 orders of magnitude, and double
//     precision loses most of its significant digits to cancellation before
//     the ray has gone anywhere. The engine works in units where r_s = 1 and
//     c = 1, keeping every quantity O(1..100).
//
//  2. THE INTEGRATOR. Forward Euler is first order, and on an oscillatory
//     problem like a bound photon orbit it is also unstable: the amplitude
//     grows without bound however small the step. The engine uses Cash-Karp
//     5(4) with adaptive step control, and tests/ measures the convergence
//     order to confirm it.
//
//  3. THE EQUATION. Integrating r(lambda) and phi(lambda) means dr/dlambda
//     passes through zero at periapsis and dt/dlambda = E/f diverges at the
//     horizon. Substituting u = 1/r and using phi as the independent variable
//     removes both problems and gives an equation with a free exact error
//     check (the impact parameter is conserved).
//
//  4. THE HARD-CODED MASS. Below, both Ray's constructor and calAcc() embed
//     8.54e36 kg directly rather than taking it from the BlackHole they are
//     supposed to be orbiting, so changing SagA's mass silently changed
//     nothing about the trajectories.
//
//  5. OUTPUT. There was no way to see the result.
//
// See ../docs/PHYSICS.md for the derivation the engine follows.
// ===========================================================================

#include <iostream>
#include <vector>
#include <cmath>

double c = 299792458; // Speed of light in m/s
double G = 6.67430e-11; // Gravitational constant in m^3 kg^-1 s^-2

class BlackHole {
public:
    double mass; // in kg
    double r_s; // in meters
    double x,y;

    BlackHole(double m, double posX, double posY){
        mass = m;
        x = posX;
        y = posY;
        r_s = 2* G * mass / ( c*c);
    }
    void printInfo(){
        std::cout << "Mass: " << mass << " kg" << std::endl;
        std::cout << "Schwarzschild radius: " << r_s << " meters" << std::endl;
        std::cout << "Position: (" << x << ", " << y << ")" << std::endl;
    };
};

class Ray {
    public:
    double r;
    double phi;
    double E; // Energy
    double L; // Angular momentum

    double dr;
    double dphi;

    Ray(double initR, double initPhi, double deltaR, double deltaPhi){
        r = initR;
        phi = initPhi;
        dr = deltaR;
        dphi = deltaPhi;
        
        double f = 1.0 - (2.0 * G * 8.54e36) / (r * c * c); // Example for Sagittarius A*
        E = f * sqrt(dr*dr/(f*f) + r*r*dphi*dphi/f);
        L = r*r*dphi;
    }
    void printInfo(){
    std::cout << "Ray position: (r: " << r << ", phi: " << phi << ")" << std::endl;
    std::cout << "Ray direction: (dr: " << dr << ", dphi: " << dphi << ")" << std::endl;
    std::cout << "Energy E=" << E << ", Angular momentum L=" << L << std::endl;  // ← Add this line
}

};

void calAcc(Ray& ray, double& d2r, double& d2phi){
    double r = ray.r;
    double dr = ray.dr;
    double dphi = ray.dphi;
    double E = ray.E;

    double rs = 2.0 * G *  8.54e36 / (c*c); // Schwarzschild radius for Sagittarius A*
    double f = 1.0 - rs / r;

    double dt_dl = E/f;

    d2r = -(rs/(2*r*r)) * f * (dt_dl*dt_dl) + (rs/(2*r*r*f)) * (dr*dr) + (r - rs) * (dphi*dphi);

    d2phi = -2.0 * dr * dphi / r;
}

void stepRay(Ray& ray, double dt){
    double d2r, d2phi;
    calAcc(ray, d2r, d2phi);
    ray.dr += d2r * dt;
    ray.dphi += d2phi * dt;
    ray.r += ray.dr * dt;
    ray.phi += ray.dphi * dt;
}

int main(){
    BlackHole SagA(8.54e36, 0, 0); // Mass of Sagittarius A* in kg
    SagA.printInfo();

    Ray test(1e12, 0.0, -c, 0.0 ); // Initial position and direction
    test.printInfo();

// Ray 1: Straight radial infall (like your current one)
Ray ray1(5e11, 0.0, -c, 0.0);

// Ray 2: Ray with some angular momentum (will curve)
Ray ray2(5e11, 0.0, -c*0.8, c*0.001/5e11);

// Ray 3: High angular momentum (might orbit or escape)
Ray ray3(5e11, 0.0, -c*0.5, c*0.003/5e11);

std::cout << "Initial conditions:" << std::endl;
std::cout << "Ray 1 - L=" << ray1.L << " (radial)" << std::endl;
std::cout << "Ray 2 - L=" << ray2.L << " (low angular momentum)" << std::endl;
std::cout << "Ray 3 - L=" << ray3.L << " (high angular momentum)" << std::endl;

// Simulate all three rays
for(int step = 0; step < 100; step++) {
    stepRay(ray1, 100.0);  // Larger time steps
    stepRay(ray2, 100.0);
    stepRay(ray3, 100.0);
    
    if(step % 20 == 0) {  // Print every 20th step
        std::cout << "\nStep " << step << ":" << std::endl;
        std::cout << "Ray1: r=" << ray1.r/1e9 << "e9, phi=" << ray1.phi << std::endl;
        std::cout << "Ray2: r=" << ray2.r/1e9 << "e9, phi=" << ray2.phi << std::endl;
        std::cout << "Ray3: r=" << ray3.r/1e9 << "e9, phi=" << ray3.phi << std::endl;
    }
    
    // Stop if any ray hits the event horizon
    if(ray1.r < SagA.r_s || ray2.r < SagA.r_s || ray3.r < SagA.r_s) {
        std::cout << "A ray hit the event horizon!" << std::endl;
        break;
    }
}

    return 0;
}

