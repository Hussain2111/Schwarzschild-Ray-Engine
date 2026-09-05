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
option(SRE_FETCH_GLFW "Download and build GLFW if it is not installed" OFF)

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
    target_compile_options(sre_warnings INTERFACE /W4 /permissive- /utf-8)
    if(SRE_WARNINGS_AS_ERRORS)
        target_compile_options(sre_warnings INTERFACE /WX)
    endif()
    # MSVC deprecates parts of the standard C library that ISO C++ does not.
    # std::fopen is standard and correct here; silencing C4996 is the normal
    # fix and avoids littering the code with _s variants that exist nowhere else.
    target_compile_definitions(sre_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sre_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(SRE_WARNINGS_AS_ERRORS)
        target_compile_options(sre_warnings INTERFACE -Werror)
    endif()
endif()

if(WIN32)
    # NOMINMAX keeps windows.h from defining min/max as macros, which would
    # break every std::min/std::max call in the engine if anything in the
    # toolchain pulls that header in.
    target_compile_definitions(sre_core INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MINGW)
    # Static-link the GCC runtime so the produced .exe runs on a machine that
    # has never seen MinGW, rather than failing on a missing libstdc++-6.dll.
    target_link_options(sre_core INTERFACE -static -static-libgcc -static-libstdc++)
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

    # Three ways to find GLFW, in order of preference:
    #   1. a CMake package  (vcpkg, Homebrew, conda, a manual install)
    #   2. pkg-config       (how it ships on most Linux distros)
    #   3. build it from source, if SRE_FETCH_GLFW is on
    # The third exists mainly for Windows, where there is no system package
    # manager to lean on and "just build it" is genuinely the shortest path.
    set(SRE_GLFW_LINK "")
    find_package(glfw3 QUIET)
    if(glfw3_FOUND)
        set(SRE_GLFW_LINK glfw)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GLFW3 QUIET glfw3)
        endif()
        if(GLFW3_FOUND)
            set(SRE_GLFW_LINK ${GLFW3_LIBRARIES})
        elseif(SRE_FETCH_GLFW)
            message(STATUS "GLFW not found; fetching and building it from source")
            if(UNIX AND NOT APPLE)
                message(STATUS
                    "   note: building GLFW from source on Linux needs X11 development\n"
                    "   headers (libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev\n"
                    "   libxi-dev). Installing libglfw3-dev is usually easier.")
            endif()
            include(FetchContent)
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
            # On Windows and macOS the native backend needs nothing extra. On
            # Linux, building GLFW from source needs display-server headers,
            # and the Wayland backend additionally needs wayland-scanner --
            # so default to X11 only and let the user opt back in. Linux users
            # are better served by their distro's libglfw3-dev anyway.
            if(UNIX AND NOT APPLE)
                set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
            endif()
            FetchContent_Declare(glfw
                GIT_REPOSITORY https://github.com/glfw/glfw.git
                GIT_TAG        3.4
                GIT_SHALLOW    TRUE)
            FetchContent_MakeAvailable(glfw)
            set(SRE_GLFW_LINK glfw)
        endif()
    endif()

    if(OpenGL_FOUND AND SRE_GLFW_LINK)
        # Bake the shader sources into a generated header.
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blackhole.vert SRE_VERTEX_SHADER)
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blackhole.frag SRE_FRAGMENT_SHADER)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader_source.hpp.in
                       ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_source.hpp @ONLY)

        add_executable(sre-viewer src/viewer.cpp)
        target_include_directories(sre-viewer PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(sre-viewer PRIVATE sre_core sre_warnings OpenGL::GL ${SRE_GLFW_LINK})
        if(GLFW3_FOUND AND NOT glfw3_FOUND)
            target_include_directories(sre-viewer PRIVATE ${GLFW3_INCLUDE_DIRS})
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
            "sre-viewer skipped: GLFW and/or OpenGL not found. "
            "The offline renderer (sre-render) does not need them.\n"
            "   Debian/Ubuntu : sudo apt install libglfw3-dev libgl1-mesa-dev\n"
            "   macOS         : brew install glfw\n"
            "   Windows / any : re-run cmake with -DSRE_FETCH_GLFW=ON to build it from source")
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
