// sre-render -- offline CPU renderer for the Schwarzschild Ray Engine.
//
// Multithreaded, headless, and dependency free. Every pixel is an independent
// null geodesic traced backwards from the camera until it hits the disk, falls
// through the horizon, or escapes to the sky.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "sre/image.hpp"
#include "sre/scene.hpp"

namespace {

struct Options {
    int width = 960;
    int height = 540;
    std::string output = "blackhole.png";
    double azimuth = 0.0;         // degrees
    double elevation = 12.0;      // degrees
    double distance = 30.0;       // r_s
    double fov = 55.0;            // degrees
    int frames = 1;
    double spin = 360.0;          // degrees swept over the whole animation
    int threads = 0;              // 0 = hardware concurrency
    bool ppm = false;
    double bloom = 0.35;
    double bloomThreshold = 0.9;
    bool quiet = false;
};

void usage() {
    std::cout <<
R"(sre-render -- Schwarzschild black hole ray tracer

Usage: sre-render [options]

Image
  -w, --width N            image width         (default 960)
  -h, --height N           image height        (default 540)
  -o, --output PATH        output file         (default blackhole.png)
      --ppm                write binary PPM instead of PNG

Camera (distances in Schwarzschild radii)
      --azimuth DEG        camera azimuth      (default 0)
      --elevation DEG      camera elevation    (default 12)
      --distance R         orbit radius        (default 30)
      --fov DEG            field of view       (default 55)

Black hole
      --mass KG            mass in kilograms   (default Sgr A*, 8.54e36)
      --solar-masses M     mass in solar masses

Accretion disk
      --disk-inner R       inner radius        (default 3 = ISCO)
      --disk-outer R       outer radius        (default 13)
      --disk-temp K        peak temperature    (default 5500)
      --disk-brightness X  brightness scale    (default 1)
      --turbulence X       0..1 clumpiness     (default 0.55)
      --retrograde         reverse the disk's orbital direction
      --thin-disk          optically thin (see through the disk)
      --no-disk            disable the disk
      --no-relativity      disable Doppler/redshift (flat comparison image)

Sky
      --no-stars           disable the star field
      --star-density X     star count scale    (default 1)
      --seed N             star field seed     (default 12345)

Quality
  -s, --samples N          NxN samples/pixel   (default 2)
      --steps N            max integrator steps per ray (default 2048)
      --tolerance X        per-step error tolerance     (default 5e-7)
      --exposure X         exposure multiplier (default 0.5)
      --bloom X            bloom strength, 0 disables (default 0.35)
      --bloom-threshold X  luminance above which bloom kicks in (default 0.9)
      --glow X             photon-ring glow    (default 0)

Animation
      --frames N           render N frames, numbered output (default 1)
      --spin DEG           total azimuth swept across the frames (default 360)

Other
  -j, --threads N          worker threads      (default: all cores)
  -q, --quiet              suppress progress
      --help               this message
)";
}

bool parseArgs(int argc, char** argv, Options& o, sre::Scene& scene) {
    auto next = [&](int& i) -> const char* {
        if (i + 1 >= argc) {
            std::cerr << "error: " << argv[i] << " needs a value\n";
            std::exit(2);
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help") { usage(); return false; }
        else if (a == "-w" || a == "--width") o.width = std::atoi(next(i));
        else if (a == "-h" || a == "--height") o.height = std::atoi(next(i));
        else if (a == "-o" || a == "--output") o.output = next(i);
        else if (a == "--ppm") o.ppm = true;
        else if (a == "--azimuth") o.azimuth = std::atof(next(i));
        else if (a == "--elevation") o.elevation = std::atof(next(i));
        else if (a == "--distance") o.distance = std::atof(next(i));
        else if (a == "--fov") o.fov = std::atof(next(i));
        else if (a == "--mass") scene.hole = sre::BlackHole(std::atof(next(i)));
        else if (a == "--solar-masses") scene.hole = sre::BlackHole::fromSolarMasses(std::atof(next(i)));
        else if (a == "--disk-inner") scene.disk.innerRadius = std::atof(next(i));
        else if (a == "--disk-outer") scene.disk.outerRadius = std::atof(next(i));
        else if (a == "--disk-temp") scene.disk.peakTemperature = std::atof(next(i));
        else if (a == "--disk-brightness") scene.disk.brightness = std::atof(next(i));
        else if (a == "--turbulence") scene.disk.turbulence = std::atof(next(i));
        else if (a == "--retrograde") scene.disk.prograde = -1.0;
        else if (a == "--thin-disk") scene.disk.opaque = false;
        else if (a == "--no-disk") scene.disk.enabled = false;
        else if (a == "--no-relativity") scene.disk.relativistic = false;
        else if (a == "--no-stars") scene.sky.enabled = false;
        else if (a == "--star-density") scene.sky.starDensity = std::atof(next(i));
        else if (a == "--seed") scene.sky.seed = static_cast<uint32_t>(std::atoi(next(i)));
        else if (a == "-s" || a == "--samples") scene.quality.samplesPerPixel = std::atoi(next(i));
        else if (a == "--steps") scene.quality.maxSteps = std::atoi(next(i));
        else if (a == "--tolerance") scene.quality.tolerance = std::atof(next(i));
        else if (a == "--exposure") scene.exposure = std::atof(next(i));
        else if (a == "--bloom") o.bloom = std::atof(next(i));
        else if (a == "--bloom-threshold") o.bloomThreshold = std::atof(next(i));
        else if (a == "--glow") scene.photonRingGlow = std::atof(next(i));
        else if (a == "--frames") o.frames = std::atoi(next(i));
        else if (a == "--spin") o.spin = std::atof(next(i));
        else if (a == "-j" || a == "--threads") o.threads = std::atoi(next(i));
        else if (a == "-q" || a == "--quiet") o.quiet = true;
        else {
            std::cerr << "error: unknown option '" << a << "' (try --help)\n";
            std::exit(2);
        }
    }

    if (o.width < 1 || o.height < 1) {
        std::cerr << "error: image dimensions must be positive\n";
        std::exit(2);
    }
    if (scene.disk.outerRadius <= scene.disk.innerRadius) {
        std::cerr << "error: --disk-outer must exceed --disk-inner\n";
        std::exit(2);
    }
    if (o.distance <= 1.0) {
        std::cerr << "error: --distance must be greater than 1 (the horizon)\n";
        std::exit(2);
    }
    return true;
}

/// Render one frame with a work-stealing row queue.
///
/// Rows are handed out atomically rather than split into equal blocks: cost per
/// pixel varies enormously (a ray that skims the photon sphere takes orders of
/// magnitude more steps than one that misses by a mile), so a static split
/// leaves most threads idle waiting for whoever drew the middle of the image.
void renderFrame(const sre::Scene& scene, sre::Image& image, int threads, bool quiet) {
    const int height = image.height();
    std::atomic<int> nextRow{0};
    std::atomic<int> done{0};

    auto worker = [&]() {
        for (;;) {
            const int y = nextRow.fetch_add(1);
            if (y >= height) return;
            for (int x = 0; x < image.width(); ++x)
                image.at(x, y) = sre::tracePixel(scene, x, y, image.width(), height);
            const int d = done.fetch_add(1) + 1;
            if (!quiet && (d % 32 == 0 || d == height)) {
                std::fprintf(stderr, "\r  rendering %3d%%", 100 * d / height);
                std::fflush(stderr);
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (!quiet) std::fprintf(stderr, "\r  rendering 100%%\n");
}

std::string frameName(const std::string& base, int index) {
    const size_t dot = base.find_last_of('.');
    const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : base.substr(dot);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "_%04d", index);
    return stem + buf + ext;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    sre::Scene scene;
    if (!parseArgs(argc, argv, opt, scene)) return 0;

    const int threads = opt.threads > 0
                            ? opt.threads
                            : std::max(1u, std::thread::hardware_concurrency());

    if (!opt.quiet) {
        const sre::BlackHole& bh = scene.hole;
        std::cout << "Schwarzschild Ray Engine\n"
                  << "  mass                 " << bh.mass() << " kg ("
                  << bh.mass() / sre::phys::M_sun << " Msun)\n"
                  << "  Schwarzschild radius " << bh.schwarzschildRadius() << " m\n"
                  << "  photon sphere        " << sre::BlackHole::photonSphere() << " r_s\n"
                  << "  ISCO                 " << sre::BlackHole::isco() << " r_s\n"
                  << "  shadow radius        " << sre::BlackHole::criticalImpactParameter()
                  << " r_s (apparent)\n"
                  << "  image                " << opt.width << "x" << opt.height << ", "
                  << scene.quality.samplesPerPixel << "x" << scene.quality.samplesPerPixel
                  << " samples, " << threads << " threads\n";
    }

    const double deg = sre::phys::pi / 180.0;
    const auto t0 = std::chrono::steady_clock::now();

    for (int frame = 0; frame < opt.frames; ++frame) {
        const double t = opt.frames > 1 ? static_cast<double>(frame) / opt.frames : 0.0;
        const double azimuth = (opt.azimuth + t * opt.spin) * deg;

        scene.camera = sre::Camera::orbit(azimuth, opt.elevation * deg, opt.distance,
                                          {0, 0, 0}, scene.disk.axis);
        scene.camera.fovDegrees = opt.fov;

        // Advance the disk texture so an animation shows differential rotation.
        scene.disk.time = t * 220.0;

        sre::Image image(opt.width, opt.height);
        if (!opt.quiet && opt.frames > 1)
            std::fprintf(stderr, "frame %d/%d\n", frame + 1, opt.frames);
        renderFrame(scene, image, threads, opt.quiet);

        // Bloom works on the HDR buffer, before the tone curve. Radius scales
        // with resolution so the look is independent of image size.
        image.applyBloom(opt.bloomThreshold, opt.bloom,
                         std::max(2, std::min(opt.width, opt.height) / 45));

        const std::string path = opt.frames > 1 ? frameName(opt.output, frame) : opt.output;
        const bool ok = opt.ppm ? image.writePPM(path, scene.exposure)
                                : image.writePNG(path, scene.exposure);
        if (!ok) {
            std::cerr << "error: could not write " << path << "\n";
            return 1;
        }
        if (!opt.quiet) std::cout << "  wrote " << path << "\n";
    }

    if (!opt.quiet) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        std::cout << "  done in " << ms / 1000.0 << " s\n";
    }
    return 0;
}
