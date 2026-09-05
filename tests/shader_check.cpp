// Headless GLSL check for shaders/blackhole.frag.
//
// The viewer's physics lives in a shader, which means it is invisible to the
// C++ test suite and to any CI machine without a display. This harness creates
// a surfaceless EGL context (Mesa's software rasteriser is enough), compiles
// the real shader, renders one frame, and asserts the image has the properties
// the physics demands:
//
//   * the centre of the frame is dark -- the shadow is there;
//   * the frame is not uniformly dark -- the disk and sky are rendering;
//   * with relativity on, the approaching side of the disk is the brighter one.
//
// The last of those is the same assertion the CPU suite makes, so the two
// independent implementations are pinned to the same physics.
//
// Build with -DSRE_BUILD_SHADER_CHECK=ON (needs EGL).

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "gl_loader.hpp"
#include "shader_source.hpp"
#include "sre/camera.hpp"
#include "sre/disk.hpp"
#include "sre/image.hpp"

using namespace glapi;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_failures;
}

GLuint compile(GLenum type, const char* src, const char* label) {
    const GLuint sh = CreateShader(type);
    ShaderSource(sh, 1, &src, nullptr);
    CompileShader(sh);
    GLint ok = 0;
    GetShaderiv(sh, kCompileStatus, &ok);
    if (!ok) {
        GLint len = 0;
        GetShaderiv(sh, kInfoLogLength, &len);
        std::vector<char> log(len > 1 ? len : 1);
        GetShaderInfoLog(sh, len, nullptr, log.data());
        std::fprintf(stderr, "\n%s shader compile error:\n%s\n", label, log.data());
        return 0;
    }
    return sh;
}

}  // namespace

int main(int argc, char** argv) {
    // --save writes the GPU's own frame to a PNG, which makes it easy to put
    // the shader's output side by side with sre-render's.
    std::string savePath;
    int kWidth = 160, kHeight = 90;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--save" && i + 1 < argc) { savePath = argv[++i]; kWidth = 640; kHeight = 360; }
        else if (a == "--size" && i + 2 < argc) { kWidth = std::atoi(argv[++i]); kHeight = std::atoi(argv[++i]); }
    }

    std::printf("GLSL shader check (headless EGL)\n");
    std::printf("================================\n\n");

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        std::fprintf(stderr, "no EGL display available; skipping\n");
        return 77;  // CTest "skipped"
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        std::fprintf(stderr, "eglInitialize failed; skipping\n");
        return 77;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        std::fprintf(stderr, "no desktop OpenGL via EGL; skipping\n");
        return 77;
    }

    const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                    EGL_NONE};
    EGLConfig config;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        std::fprintf(stderr, "no suitable EGL config; skipping\n");
        return 77;
    }

    const EGLint pbufferAttribs[] = {EGL_WIDTH, kWidth, EGL_HEIGHT, kHeight, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        std::fprintf(stderr, "could not create a pbuffer; skipping\n");
        return 77;
    }

    const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                                     EGL_CONTEXT_MINOR_VERSION, 3,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                     EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        std::fprintf(stderr, "could not create an OpenGL 3.3 core context; skipping\n");
        return 77;
    }
    eglMakeCurrent(display, surface, surface, context);

    if (const char* missing = load(eglGetProcAddress)) {
        std::fprintf(stderr, "missing GL entry point %s; skipping\n", missing);
        return 77;
    }
    std::printf("  renderer: %s\n", reinterpret_cast<const char*>(GetString(kRenderer)));
    std::printf("  GL:       %s\n\n", reinterpret_cast<const char*>(GetString(kVersion)));

    // --- compile the real shader --------------------------------------------
    const GLuint vs = compile(kVertexShader, kVertexShaderSource, "vertex");
    check(vs != 0, "vertex shader compiles");
    const GLuint fs = compile(kFragmentShader, kFragmentShaderSource, "fragment");
    check(fs != 0, "fragment shader compiles");
    if (!vs || !fs) return 1;

    const GLuint prog = CreateProgram();
    AttachShader(prog, vs);
    AttachShader(prog, fs);
    LinkProgram(prog);
    GLint linked = 0;
    GetProgramiv(prog, kLinkStatus, &linked);
    if (!linked) {
        GLint len = 0;
        GetProgramiv(prog, kInfoLogLength, &len);
        std::vector<char> log(len > 1 ? len : 1);
        GetProgramInfoLog(prog, len, nullptr, log.data());
        std::fprintf(stderr, "link error:\n%s\n", log.data());
        return 1;
    }
    check(true, "program links");

    GLuint vao = 0;
    GenVertexArrays(1, &vao);
    BindVertexArray(vao);
    UseProgram(prog);
    Viewport(0, 0, kWidth, kHeight);

    sre::DiskParams disk;
    auto U = [&](const char* n) { return GetUniformLocation(prog, n); };

    // Render one frame with the given orbit direction and relativity setting.
    auto renderFrame = [&](double prograde, int relativistic, std::vector<unsigned char>& out) {
        sre::Camera cam = sre::Camera::orbit(0.0, 5.0 * sre::phys::pi / 180.0, 22.0,
                                             {0, 0, 0}, disk.axis);
        cam.fovDegrees = 55.0;
        sre::Vec3 fwd, right, up;
        cam.basis(fwd, right, up);

        Uniform2f(U("uResolution"), float(kWidth), float(kHeight));
        Uniform3f(U("uCamPos"), float(cam.position.x), float(cam.position.y), float(cam.position.z));
        Uniform3f(U("uCamForward"), float(fwd.x), float(fwd.y), float(fwd.z));
        Uniform3f(U("uCamRight"), float(right.x), float(right.y), float(right.z));
        Uniform3f(U("uCamUp"), float(up.x), float(up.y), float(up.z));
        Uniform1f(U("uTanHalfFov"), float(std::tan(0.5 * 55.0 * sre::phys::pi / 180.0)));

        Uniform3f(U("uDiskAxis"), 0.0f, 1.0f, 0.0f);
        Uniform1f(U("uDiskInner"), float(disk.innerRadius));
        Uniform1f(U("uDiskOuter"), float(disk.outerRadius));
        Uniform1f(U("uDiskTemp"), float(disk.peakTemperature));
        Uniform1f(U("uDiskBrightness"), 1.0f);
        Uniform1f(U("uTurbulence"), 0.0f);
        Uniform1f(U("uProgradeSign"), float(prograde));
        Uniform1f(U("uTime"), 0.0f);
        Uniform1i(U("uDiskEnabled"), 1);
        Uniform1i(U("uRelativistic"), relativistic);

        Uniform1f(U("uStarDensity"), 1.0f);
        Uniform1f(U("uStarBrightness"), 1.0f);
        Uniform1f(U("uGalaxyBrightness"), 0.0f);
        Uniform3f(U("uGalaxyAxis"), 0.32f, 0.86f, 0.4f);
        Uniform1ui(U("uSeed"), 12345u);
        Uniform1i(U("uSkyEnabled"), 1);

        Uniform1f(U("uExposure"), 0.5f);
        Uniform1i(U("uMaxSteps"), 400);
        Uniform1f(U("uStepSize"), 0.025f);
        Uniform1f(U("uEscapeRadius"), 900.0f);

        ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        Clear(kColorBufferBit);
        DrawArrays(kTriangles, 0, 3);

        out.assign(static_cast<size_t>(kWidth) * kHeight * 3, 0);
        PixelStorei(kPackAlignment, 1);
        ReadPixels(0, 0, kWidth, kHeight, kRGB, kUnsignedByte, out.data());
    };

    auto meanHalf = [&](const std::vector<unsigned char>& px, int x0, int x1) {
        double total = 0.0;
        int n = 0;
        for (int y = 0; y < kHeight; ++y)
            for (int x = x0; x < x1; ++x) {
                const size_t i = (static_cast<size_t>(y) * kWidth + x) * 3;
                total += px[i] + px[i + 1] + px[i + 2];
                n += 3;
            }
        return n ? total / n : 0.0;
    };

    std::vector<unsigned char> frame;
    renderFrame(1.0, 1, frame);

    // The frame must contain light.
    double brightest = 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < frame.size(); ++i) {
        brightest = std::max(brightest, static_cast<double>(frame[i]));
        sum += frame[i];
    }
    check(brightest > 40.0, "the shader renders visible light");
    check(sum / frame.size() < 200.0, "the frame is not uniformly saturated");

    // The shadow: a small patch at the centre of the frame must be dark.
    // The camera looks straight at the hole, so the centre pixel is captured.
    double centre = 0.0;
    int centreCount = 0;
    for (int y = kHeight / 2 - 1; y <= kHeight / 2 + 1; ++y)
        for (int x = kWidth / 2 - 1; x <= kWidth / 2 + 1; ++x) {
            const size_t i = (static_cast<size_t>(y) * kWidth + x) * 3;
            centre += frame[i] + frame[i + 1] + frame[i + 2];
            centreCount += 3;
        }
    check(centre / centreCount < 12.0, "the centre of the frame is the black hole shadow");

    // Relativistic beaming, checked the same way the CPU suite checks it.
    const double left = meanHalf(frame, 0, kWidth / 2);
    const double right = meanHalf(frame, kWidth / 2, kWidth);
    check(left > 1.2 * right, "the approaching side of the disk is brighter");

    std::vector<unsigned char> reversed;
    renderFrame(-1.0, 1, reversed);
    check(meanHalf(reversed, kWidth / 2, kWidth) > 1.2 * meanHalf(reversed, 0, kWidth / 2),
          "reversing the orbit swaps the bright side");

    std::vector<unsigned char> flat;
    renderFrame(1.0, 0, flat);
    const double flatLeft = meanHalf(flat, 0, kWidth / 2);
    const double flatRight = meanHalf(flat, kWidth / 2, kWidth);
    check(std::fabs(flatLeft - flatRight) < 0.15 * std::max(flatLeft, 1.0),
          "with relativity off the disk is left-right symmetric");

    if (!savePath.empty()) {
        // GL's origin is bottom-left, PNG's is top-left.
        std::vector<unsigned char> flipped(frame.size());
        const size_t stride = static_cast<size_t>(kWidth) * 3;
        for (int y = 0; y < kHeight; ++y)
            std::memcpy(flipped.data() + y * stride,
                        frame.data() + static_cast<size_t>(kHeight - 1 - y) * stride, stride);
        const std::vector<uint8_t> png = sre::png::encodeRGB8(flipped.data(), kWidth, kHeight);
        if (FILE* f = std::fopen(savePath.c_str(), "wb")) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
            std::printf("\n  saved %s\n", savePath.c_str());
        }
    }

    DeleteProgram(prog);
    DeleteVertexArrays(1, &vao);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    std::printf("\n%s\n", g_failures == 0 ? "All shader checks passed." : "Shader checks FAILED.");
    return g_failures == 0 ? 0 : 1;
}
