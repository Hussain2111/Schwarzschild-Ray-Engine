// sre-viewer -- interactive real-time black hole.
//
// A fullscreen fragment shader traces one null geodesic per pixel per frame.
// Everything the CPU does here is bookkeeping: the physics lives in
// shaders/blackhole.frag, which mirrors include/sre/geodesic.hpp.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// GLFW must not pull in a system GL header: its macros would collide with the
// constants gl_loader.hpp declares.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "gl_loader.hpp"
#include "shader_source.hpp"  // generated: kVertexShaderSource / kFragmentShaderSource
#include "sre/blackhole.hpp"
#include "sre/image.hpp"
#include "sre/scene.hpp"

using namespace glapi;

namespace {

struct Viewer {
    // Camera, in Schwarzschild radii.
    double azimuth = 0.0;
    double elevation = 12.0 * sre::phys::pi / 180.0;
    double distance = 30.0;
    double fov = 55.0;

    // Scene knobs, mirrored into uniforms every frame.
    sre::DiskParams disk{};
    sre::SkyParams sky{};
    double exposure = 0.5;
    int maxSteps = 400;
    double stepSize = 0.025;
    double escapeRadius = 900.0;

    bool paused = false;
    double diskTime = 0.0;
    double timeScale = 30.0;

    // Interaction state.
    bool dragging = false;
    double lastX = 0.0, lastY = 0.0;
    int screenshotIndex = 0;
    bool showHelp = true;
};

Viewer g;

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GLuint compile(GLenum type, const std::string& src, const char* label) {
    const GLuint shader = CreateShader(type);
    const char* ptr = src.c_str();
    ShaderSource(shader, 1, &ptr, nullptr);
    CompileShader(shader);

    GLint ok = 0;
    GetShaderiv(shader, kCompileStatus, &ok);
    if (!ok) {
        GLint len = 0;
        GetShaderiv(shader, kInfoLogLength, &len);
        std::vector<char> log(len > 1 ? len : 1);
        GetShaderInfoLog(shader, len, nullptr, log.data());
        std::fprintf(stderr, "%s shader failed to compile:\n%s\n", label, log.data());
        DeleteShader(shader);
        return 0;
    }
    return shader;
}

/// Build the program. Prefers shader files on disk when `dir` is non-empty, so
/// the shader can be edited and reloaded with R without rebuilding; otherwise
/// uses the copy baked into the binary at build time.
GLuint buildProgram(const std::string& dir) {
    std::string vsrc = kVertexShaderSource;
    std::string fsrc = kFragmentShaderSource;
    if (!dir.empty()) {
        const std::string v = readFile(dir + "/blackhole.vert");
        const std::string f = readFile(dir + "/blackhole.frag");
        if (!v.empty()) vsrc = v;
        if (!f.empty()) fsrc = f;
    }

    const GLuint vs = compile(kVertexShader, vsrc, "vertex");
    if (!vs) return 0;
    const GLuint fs = compile(kFragmentShader, fsrc, "fragment");
    if (!fs) { DeleteShader(vs); return 0; }

    const GLuint prog = CreateProgram();
    AttachShader(prog, vs);
    AttachShader(prog, fs);
    LinkProgram(prog);
    DeleteShader(vs);
    DeleteShader(fs);

    GLint ok = 0;
    GetProgramiv(prog, kLinkStatus, &ok);
    if (!ok) {
        GLint len = 0;
        GetProgramiv(prog, kInfoLogLength, &len);
        std::vector<char> log(len > 1 ? len : 1);
        GetProgramInfoLog(prog, len, nullptr, log.data());
        std::fprintf(stderr, "program failed to link:\n%s\n", log.data());
        DeleteProgram(prog);
        return 0;
    }
    return prog;
}

void printHelp() {
    std::printf(
        "\nControls\n"
        "  drag / arrows   orbit the camera\n"
        "  scroll, +/-     zoom (camera radius, in r_s)\n"
        "  [ / ]           exposure down / up\n"
        "  , / .           disk inner radius (down to the photon sphere)\n"
        "  ; / '           disk outer radius\n"
        "  T / Shift+T     disk temperature down / up\n"
        "  N               cycle turbulence off / low / high\n"
        "  D               toggle the accretion disk\n"
        "  S               toggle the star field\n"
        "  G               toggle relativistic beaming and redshift\n"
        "  O               reverse the disk's orbital direction\n"
        "  Space           pause the disk animation\n"
        "  Q / W           integrator steps per ray, down / up\n"
        "  R               reload the shader from disk\n"
        "  P               save a PNG screenshot\n"
        "  H               print this help\n"
        "  Esc             quit\n\n");
}

void keyCallback(GLFWwindow* win, int key, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;

    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, 1); break;
        case GLFW_KEY_LEFT:   g.azimuth -= 0.05; break;
        case GLFW_KEY_RIGHT:  g.azimuth += 0.05; break;
        case GLFW_KEY_UP:     g.elevation = std::min(g.elevation + 0.03, 1.5); break;
        case GLFW_KEY_DOWN:   g.elevation = std::max(g.elevation - 0.03, -1.5); break;
        case GLFW_KEY_EQUAL:  g.distance = std::max(3.0, g.distance * 0.95); break;
        case GLFW_KEY_MINUS:  g.distance = std::min(400.0, g.distance * 1.05); break;
        case GLFW_KEY_LEFT_BRACKET:  g.exposure = std::max(0.02, g.exposure / 1.15); break;
        case GLFW_KEY_RIGHT_BRACKET: g.exposure = std::min(50.0, g.exposure * 1.15); break;
        case GLFW_KEY_COMMA:
            g.disk.innerRadius = std::max(sre::BlackHole::photonSphere(), g.disk.innerRadius - 0.25);
            break;
        case GLFW_KEY_PERIOD:
            g.disk.innerRadius = std::min(g.disk.outerRadius - 0.5, g.disk.innerRadius + 0.25);
            break;
        case GLFW_KEY_SEMICOLON:
            g.disk.outerRadius = std::max(g.disk.innerRadius + 0.5, g.disk.outerRadius - 0.5);
            break;
        case GLFW_KEY_APOSTROPHE:
            g.disk.outerRadius = std::min(80.0, g.disk.outerRadius + 0.5);
            break;
        case GLFW_KEY_T:
            g.disk.peakTemperature = shift ? std::min(60000.0, g.disk.peakTemperature * 1.1)
                                           : std::max(1200.0, g.disk.peakTemperature / 1.1);
            std::printf("disk peak temperature: %.0f K\n", g.disk.peakTemperature);
            break;
        case GLFW_KEY_N:
            g.disk.turbulence = g.disk.turbulence <= 0.0 ? 0.35
                              : (g.disk.turbulence < 0.5 ? 0.75 : 0.0);
            break;
        case GLFW_KEY_D: g.disk.enabled = !g.disk.enabled; break;
        case GLFW_KEY_S: g.sky.enabled = !g.sky.enabled; break;
        case GLFW_KEY_G:
            g.disk.relativistic = !g.disk.relativistic;
            std::printf("Doppler + gravitational shift: %s\n", g.disk.relativistic ? "on" : "off");
            break;
        case GLFW_KEY_O: g.disk.prograde = -g.disk.prograde; break;
        case GLFW_KEY_SPACE: g.paused = !g.paused; break;
        case GLFW_KEY_Q: g.maxSteps = std::max(60, g.maxSteps - 40); break;
        case GLFW_KEY_W: g.maxSteps = std::min(4000, g.maxSteps + 40); break;
        case GLFW_KEY_H: printHelp(); break;
        default: break;
    }
}

void scrollCallback(GLFWwindow*, double, double dy) {
    g.distance = std::clamp(g.distance * std::pow(0.9, dy), 3.0, 400.0);
}

void mouseButtonCallback(GLFWwindow* win, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    g.dragging = (action == GLFW_PRESS);
    if (g.dragging) glfwGetCursorPos(win, &g.lastX, &g.lastY);
}

void cursorCallback(GLFWwindow*, double x, double y) {
    if (!g.dragging) return;
    g.azimuth += (x - g.lastX) * 0.006;
    g.elevation = std::clamp(g.elevation - (y - g.lastY) * 0.006, -1.5, 1.5);
    g.lastX = x;
    g.lastY = y;
}

/// Read the framebuffer back and write a PNG through the engine's own encoder.
void screenshot(int width, int height) {
    std::vector<unsigned char> rgb(static_cast<size_t>(width) * height * 3);
    PixelStorei(kPackAlignment, 1);
    ReadPixels(0, 0, width, height, kRGB, kUnsignedByte, rgb.data());

    // GL's origin is bottom-left; PNG's is top-left.
    const size_t stride = static_cast<size_t>(width) * 3;
    std::vector<unsigned char> flipped(rgb.size());
    for (int y = 0; y < height; ++y)
        std::memcpy(flipped.data() + y * stride,
                    rgb.data() + static_cast<size_t>(height - 1 - y) * stride, stride);

    char name[64];
    std::snprintf(name, sizeof(name), "screenshot_%03d.png", g.screenshotIndex++);
    const std::vector<uint8_t> png = sre::png::encodeRGB8(flipped.data(), width, height);
    if (FILE* f = std::fopen(name, "wb")) {
        std::fwrite(png.data(), 1, png.size(), f);
        std::fclose(f);
        std::printf("saved %s\n", name);
    } else {
        std::fprintf(stderr, "could not write %s\n", name);
    }
}

}  // namespace

int main(int argc, char** argv) {
    int width = 1280, height = 720;
    std::string shaderDir = SRE_SHADER_DIR;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "-w" || a == "--width") && i + 1 < argc) width = std::atoi(argv[++i]);
        else if ((a == "-h" || a == "--height") && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (a == "--shader-dir" && i + 1 < argc) shaderDir = argv[++i];
        else if (a == "--embedded-shader") shaderDir.clear();
        else if (a == "--help") {
            std::printf("sre-viewer [-w N] [-h N] [--shader-dir DIR] [--embedded-shader]\n");
            printHelp();
            return 0;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "error: glfwInit failed. On a headless machine use sre-render instead.\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(width, height, "Schwarzschild Ray Engine", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "error: could not create an OpenGL 3.3 window.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (const char* missing = load(glfwGetProcAddress)) {
        std::fprintf(stderr, "error: missing OpenGL entry point %s\n", missing);
        glfwTerminate();
        return 1;
    }

    std::printf("Schwarzschild Ray Engine -- interactive viewer\n");
    std::printf("  renderer: %s\n", reinterpret_cast<const char*>(GetString(kRenderer)));
    std::printf("  GL:       %s\n", reinterpret_cast<const char*>(GetString(kVersion)));

    GLuint program = buildProgram(shaderDir);
    if (!program) { glfwTerminate(); return 1; }

    GLuint vao = 0;
    GenVertexArrays(1, &vao);
    BindVertexArray(vao);

    glfwSetKeyCallback(win, keyCallback);
    glfwSetScrollCallback(win, scrollCallback);
    glfwSetMouseButtonCallback(win, mouseButtonCallback);
    glfwSetCursorPosCallback(win, cursorCallback);

    printHelp();

    double lastTime = glfwGetTime();
    double fpsAccum = 0.0;
    int fpsFrames = 0;

    while (!glfwWindowShouldClose(win)) {
        const double now = glfwGetTime();
        const double dt = now - lastTime;
        lastTime = now;
        if (!g.paused) g.diskTime += dt * g.timeScale;

        // Deferred key actions that need the GL context or window size.
        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) {
            if (GLuint p = buildProgram(shaderDir)) {
                DeleteProgram(program);
                program = p;
                std::printf("shader reloaded\n");
            }
            while (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) glfwPollEvents();
        }

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw <= 0 || fbh <= 0) { glfwPollEvents(); continue; }
        Viewport(0, 0, fbw, fbh);

        // Camera basis, matching sre::Camera exactly.
        sre::Camera cam = sre::Camera::orbit(g.azimuth, g.elevation, g.distance,
                                             {0, 0, 0}, g.disk.axis);
        cam.fovDegrees = g.fov;
        sre::Vec3 fwd, right, up;
        cam.basis(fwd, right, up);

        UseProgram(program);
        auto U = [&](const char* n) { return GetUniformLocation(program, n); };

        Uniform2f(U("uResolution"), static_cast<float>(fbw), static_cast<float>(fbh));
        Uniform3f(U("uCamPos"), static_cast<float>(cam.position.x),
                  static_cast<float>(cam.position.y), static_cast<float>(cam.position.z));
        Uniform3f(U("uCamForward"), static_cast<float>(fwd.x), static_cast<float>(fwd.y),
                  static_cast<float>(fwd.z));
        Uniform3f(U("uCamRight"), static_cast<float>(right.x), static_cast<float>(right.y),
                  static_cast<float>(right.z));
        Uniform3f(U("uCamUp"), static_cast<float>(up.x), static_cast<float>(up.y),
                  static_cast<float>(up.z));
        Uniform1f(U("uTanHalfFov"), static_cast<float>(std::tan(0.5 * g.fov * sre::phys::pi / 180.0)));

        Uniform3f(U("uDiskAxis"), static_cast<float>(g.disk.axis.x),
                  static_cast<float>(g.disk.axis.y), static_cast<float>(g.disk.axis.z));
        Uniform1f(U("uDiskInner"), static_cast<float>(g.disk.innerRadius));
        Uniform1f(U("uDiskOuter"), static_cast<float>(g.disk.outerRadius));
        Uniform1f(U("uDiskTemp"), static_cast<float>(g.disk.peakTemperature));
        Uniform1f(U("uDiskBrightness"), static_cast<float>(g.disk.brightness));
        Uniform1f(U("uTurbulence"), static_cast<float>(g.disk.turbulence));
        Uniform1f(U("uProgradeSign"), static_cast<float>(g.disk.prograde));
        Uniform1f(U("uTime"), static_cast<float>(g.diskTime));
        Uniform1i(U("uDiskEnabled"), g.disk.enabled ? 1 : 0);
        Uniform1i(U("uRelativistic"), g.disk.relativistic ? 1 : 0);

        Uniform1f(U("uStarDensity"), static_cast<float>(g.sky.starDensity));
        Uniform1f(U("uStarBrightness"), static_cast<float>(g.sky.starBrightness));
        Uniform1f(U("uGalaxyBrightness"), static_cast<float>(g.sky.galaxyBrightness));
        Uniform3f(U("uGalaxyAxis"), static_cast<float>(g.sky.galaxyAxis.x),
                  static_cast<float>(g.sky.galaxyAxis.y), static_cast<float>(g.sky.galaxyAxis.z));
        Uniform1ui(U("uSeed"), g.sky.seed);
        Uniform1i(U("uSkyEnabled"), g.sky.enabled ? 1 : 0);

        Uniform1f(U("uExposure"), static_cast<float>(g.exposure));
        Uniform1i(U("uMaxSteps"), g.maxSteps);
        Uniform1f(U("uStepSize"), static_cast<float>(g.stepSize));
        Uniform1f(U("uEscapeRadius"), static_cast<float>(g.escapeRadius));

        ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        Clear(kColorBufferBit);
        DrawArrays(kTriangles, 0, 3);

        if (glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS) {
            screenshot(fbw, fbh);
            while (glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS) glfwPollEvents();
        }

        glfwSwapBuffers(win);
        glfwPollEvents();

        fpsAccum += dt;
        if (++fpsFrames >= 60) {
            char title[192];
            std::snprintf(title, sizeof(title),
                          "Schwarzschild Ray Engine  |  %.0f fps  |  r=%.1f r_s  |  "
                          "disk %.1f-%.1f r_s  |  %.0f K  |  %d steps",
                          fpsFrames / fpsAccum, g.distance, g.disk.innerRadius,
                          g.disk.outerRadius, g.disk.peakTemperature, g.maxSteps);
            glfwSetWindowTitle(win, title);
            fpsAccum = 0.0;
            fpsFrames = 0;
        }
    }

    DeleteVertexArrays(1, &vao);
    DeleteProgram(program);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
