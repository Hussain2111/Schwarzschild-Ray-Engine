cmake_minimum_required(VERSION 3.16)
project(SchwarzschildRayEngine
        VERSION 2.0.0
        DESCRIPTION "General-relativistic ray tracer for Schwarzschild black holes"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SRE_BUILD_VIEWER "Build the real-time OpenGL viewer (needs GLFW + OpenGL)" ON)
option(SRE_BUILD_TESTS  "Build the physics test suite" ON)
option(SRE_BUILD_SHADER_CHECK "Build the headless GLSL check (needs EGL)" ON)
option(SRE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Core: header-only physics and rendering library
# ---------------------------------------------------------------------------
add_library(sre_core INTERFACE)
target_include_directories(sre_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(sre_core INTERFACE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(sre_core INTERFACE Threads::Threads)

# A shared set of warnings, applied to our targets only.
add_library(sre_warnings INTERFACE)
if(MSVC)
    target_compile_options(sre_warnings INTERFACE /W4)
    if(SRE_WARNINGS_AS_ERRORS)
        target_compile_options(sre_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(sre_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(SRE_WARNINGS_AS_ERRORS)
        target_compile_options(sre_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Offline CPU renderer
# ---------------------------------------------------------------------------
add_executable(sre-render src/render.cpp)
target_link_libraries(sre-render PRIVATE sre_core sre_warnings)

# ---------------------------------------------------------------------------
# Real-time viewer
# ---------------------------------------------------------------------------
if(SRE_BUILD_VIEWER)
    find_package(OpenGL)
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        # Fall back to pkg-config, which is how GLFW ships on most distros.
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GLFW3 QUIET glfw3)
        endif()
    endif()

    if(OpenGL_FOUND AND (glfw3_FOUND OR GLFW3_FOUND))
        # Bake the shader sources into a generated header.
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blackhole.vert SRE_VERTEX_SHADER)
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blackhole.frag SRE_FRAGMENT_SHADER)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader_source.hpp.in
                       ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_source.hpp @ONLY)

        add_executable(sre-viewer src/viewer.cpp)
        target_include_directories(sre-viewer PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(sre-viewer PRIVATE sre_core sre_warnings OpenGL::GL)

        if(glfw3_FOUND)
            target_link_libraries(sre-viewer PRIVATE glfw)
        else()
            target_include_directories(sre-viewer PRIVATE ${GLFW3_INCLUDE_DIRS})
            target_link_libraries(sre-viewer PRIVATE ${GLFW3_LIBRARIES})
            target_link_directories(sre-viewer PRIVATE ${GLFW3_LIBRARY_DIRS})
        endif()

        # Live shader reload reads from the source tree when it is still there.
        target_compile_definitions(sre-viewer PRIVATE
            SRE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

        # Rebuild the generated header whenever a shader changes.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blackhole.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blackhole.frag)
    else()
        message(STATUS
            "sre-viewer skipped: GLFW3 and/or OpenGL not found. "
            "The offline renderer (sre-render) does not need them. "
            "On Debian/Ubuntu: sudo apt install libglfw3-dev libgl1-mesa-dev")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SRE_BUILD_TESTS)
    enable_testing()
    add_executable(sre-tests tests/test_main.cpp)
    target_link_libraries(sre-tests PRIVATE sre_core sre_warnings)
    add_test(NAME physics COMMAND sre-tests)

    # The viewer's physics lives in GLSL, invisible to the C++ suite. This
    # check compiles the real shader against a surfaceless EGL context (Mesa's
    # software rasteriser is enough) and asserts the rendered frame agrees with
    # the CPU renderer on shadow placement and beaming direction. It reports
    # exit code 77 -- "skipped" -- when no EGL implementation is present, so it
    # never fails a build on a machine that simply has no GPU stack.
    if(SRE_BUILD_SHADER_CHECK)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(EGL QUIET egl)
        endif()
        if(EGL_FOUND AND EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_source.hpp)
            add_executable(sre-shader-check tests/shader_check.cpp)
            target_include_directories(sre-shader-check PRIVATE
                ${CMAKE_CURRENT_BINARY_DIR}/generated
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${EGL_INCLUDE_DIRS})
            target_link_libraries(sre-shader-check PRIVATE sre_core sre_warnings ${EGL_LIBRARIES})
            target_link_directories(sre-shader-check PRIVATE ${EGL_LIBRARY_DIRS})
            add_test(NAME shader COMMAND sre-shader-check)
            set_tests_properties(shader PROPERTIES SKIP_RETURN_CODE 77)
        else()
            message(STATUS "sre-shader-check skipped: EGL or generated shader header not found")
        endif()
    endif()
endif()

install(TARGETS sre-render RUNTIME DESTINATION bin)
if(TARGET sre-viewer)
    install(TARGETS sre-viewer RUNTIME DESTINATION bin)
endif()
