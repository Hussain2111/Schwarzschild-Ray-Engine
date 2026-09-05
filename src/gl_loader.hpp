// Minimal OpenGL 3.3 core loader.
//
// The engine needs about two dozen GL entry points. Pulling in GLEW or glad
// for that would add a build dependency (and, for glad, a generated source file
// to check in) for no benefit, so the handful of pointers are resolved here.
//
// The loader takes the platform's getProcAddress as an argument rather than
// calling one directly, so the same code serves the GLFW viewer and the
// headless EGL shader test. GL types are declared locally, so this header
// needs no GL headers at all.
#pragma once

#include <cstddef>
#include <cstdint>

namespace glapi {

using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;

// Only the constants actually used. Named with a k prefix so they cannot
// collide with the GL_* macros a system header might still introduce.
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84;
constexpr GLbitfield kColorBufferBit = 0x00004000;
constexpr GLenum kTriangles = 0x0004;
constexpr GLenum kRGB = 0x1907;
constexpr GLenum kUnsignedByte = 0x1401;
constexpr GLenum kPackAlignment = 0x0D05;
constexpr GLenum kVersion = 0x1F02;
constexpr GLenum kRenderer = 0x1F01;

#define SRE_GL_FUNCTIONS(X)                                                             \
    X(GLuint, CreateShader, (GLenum))                                                   \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))        \
    X(void, CompileShader, (GLuint))                                                    \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                      \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                     \
    X(void, DeleteShader, (GLuint))                                                     \
    X(GLuint, CreateProgram, ())                                                        \
    X(void, AttachShader, (GLuint, GLuint))                                             \
    X(void, LinkProgram, (GLuint))                                                      \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                     \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                    \
    X(void, DeleteProgram, (GLuint))                                                    \
    X(void, UseProgram, (GLuint))                                                       \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                               \
    X(void, Uniform1i, (GLint, GLint))                                                  \
    X(void, Uniform1ui, (GLint, GLuint))                                                \
    X(void, Uniform1f, (GLint, GLfloat))                                                \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                       \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                              \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                        \
    X(void, BindVertexArray, (GLuint))                                                  \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                               \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                       \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                 \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                           \
    X(void, Clear, (GLbitfield))                                                        \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))        \
    X(void, PixelStorei, (GLenum, GLint))                                               \
    X(const GLubyte*, GetString, (GLenum))

#define SRE_DECLARE(ret, name, args) inline ret(*name) args = nullptr;
SRE_GL_FUNCTIONS(SRE_DECLARE)
#undef SRE_DECLARE

/// Signature of a platform getProcAddress (glfwGetProcAddress, eglGetProcAddress).
using ProcLoader = void (*(*)(const char*))();

/// Resolve every entry point through `getProc`. Returns the name of the first
/// one that failed, or nullptr on success.
template <typename GetProc>
inline const char* load(GetProc getProc) {
#define SRE_LOAD(ret, name, args)                                                    \
    name = reinterpret_cast<ret (*) args>(getProc("gl" #name));                      \
    if (!name) return "gl" #name;
    SRE_GL_FUNCTIONS(SRE_LOAD)
#undef SRE_LOAD
    return nullptr;
}

}  // namespace glapi
