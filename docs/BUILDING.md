# Building

The engine is CMake + C++17. There are two build products:

| Target | Needs | Runs where |
|---|---|---|
| `sre-render` | a C++17 compiler, nothing else | anywhere, including headless servers and CI |
| `sre-viewer` | GLFW + OpenGL 3.3 | a desktop with a GPU driver |

The viewer is optional. If GLFW is not found, CMake prints why, skips that
target, and still builds the renderer and the tests.

---

## Windows

You need **Visual Studio 2019 or newer** with the *Desktop development with C++*
workload (that installs MSVC, the Windows SDK, and CMake). The standalone
**Build Tools** edition works just as well -- it is the same compiler without
the IDE. Everything below is run from the *Developer Command Prompt for VS* or
*Developer PowerShell*, which put the compiler on your `PATH`.

Check the toolchain is complete before you start:

```powershell
cl /?      # should print the MSVC banner
cmake --version
```

If `cmake` is missing (this happens on some Build Tools installs), open the
Visual Studio Installer, choose *Modify*, and tick **C++ CMake tools for
Windows** under Individual Components.

> **Start by moving somewhere writable.** The Developer Command Prompt and
> Developer PowerShell open *inside the Visual Studio install directory*
> (`C:\Program Files (x86)\Microsoft Visual Studio\...`), which is
> read-only for normal users. Cloning there fails with
> `fatal: could not create work tree dir ...: Permission denied`. That is the
> shell's starting directory, not a broken install -- just `cd` to your own
> folder first.

### The short version

```powershell
cd $HOME
mkdir source\repos -Force | Out-Null
cd source\repos

git clone https://github.com/Hussain2111/Schwarzschild-Ray-Engine.git
cd Schwarzschild-Ray-Engine
cmake -S . -B build -DSRE_FETCH_GLFW=ON
cmake --build build --config Release
```

(In `cmd.exe` rather than PowerShell, use `cd /d %USERPROFILE%` and
`mkdir source\repos` instead of the first three lines.)

Then:

```bat
build\Release\sre-render.exe
build\Release\sre-viewer.exe
ctest --test-dir build --build-config Release
```

`sre-render.exe` writes `blackhole.png` into whatever directory you run it from.

`-DSRE_FETCH_GLFW=ON` makes CMake download GLFW and build it as part of the
project. On Windows this needs nothing but the Windows SDK you already have, so
it is the shortest path to a working viewer — no package manager involved. It
does need `git` and a network connection the first time you configure.

> **Note on paths.** Visual Studio is a *multi-configuration* generator, so the
> binaries land in `build\Release\`, not `build\`, and `--config Release` /
> `--build-config Release` are required. This trips people up coming from
> Makefile-style builds.

### If you already use vcpkg

```bat
vcpkg install glfw3:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Drop `-DSRE_FETCH_GLFW=ON` here — vcpkg supplies GLFW and CMake will find it.

### Renderer only, no graphics stack

If you only want the offline renderer and the physics tests:

```bat
cmake -S . -B build -DSRE_BUILD_VIEWER=OFF -DSRE_BUILD_SHADER_CHECK=OFF
cmake --build build --config Release
```

This needs no GLFW, no OpenGL, and no network access.

### MSYS2 / MinGW

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

MinGW is a single-configuration generator, so binaries land directly in
`build/` and no `--config` is needed. The build statically links the GCC
runtime, so the resulting `.exe` runs on machines without MinGW installed.

### WSL

WSL works too, and is the closest thing to the Linux instructions below. The
renderer is fine headless. For the viewer you need WSLg (Windows 11, or Windows
10 with a recent update), plus `libglfw3-dev` and `libgl1-mesa-dev`. If the
viewer cannot open a window, use `sre-render` instead — it needs no display.

### Troubleshooting

**`fatal: could not create work tree dir '...': Permission denied`**
You are still in the Visual Studio install directory, which is not writable.
`cd $HOME` (or any folder you own) and clone again.

**`cmake` or `cl` is not recognised**
You are in a plain PowerShell or cmd window rather than a *Developer* one.
Launch "Developer PowerShell for VS" from the Start menu. If `cl` works but
`cmake` does not, install the CMake component as described above.

**`sre-render.exe` is not found after a successful build**
Visual Studio is a multi-configuration generator, so the binaries are in
`build\Release\`, not `build\`. See the note above.

**The build succeeded but `sre-viewer.exe` is missing**
GLFW was not found and not fetched. Re-run the configure step with
`-DSRE_FETCH_GLFW=ON`, and delete the `build` folder first if you are changing
options on an existing configuration.

### Known Windows limitations

- **The headless GLSL check (`sre-shader-check`) is not built on Windows.** It
  needs EGL, which is a Linux/Mesa interface. CMake detects this and skips the
  target, so `ctest` runs the physics suite only. The shader itself is still
  exercised every time you run the viewer.
- **The viewer needs a real OpenGL 3.3 driver.** Over plain RDP, or in a VM
  without GPU passthrough, you may only get OpenGL 1.1 and the viewer will
  exit with a message. `sre-render` has no such requirement.
- `tools/turntable.sh` is a bash script. Use Git Bash or WSL, or call
  `sre-render.exe --frames 120 --spin 360` directly.

---

## Linux

```bash
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev
# optional, for the headless GLSL check:
sudo apt install libegl1-mesa-dev libgl1-mesa-dri

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Prefer your distribution's `libglfw3-dev` over `-DSRE_FETCH_GLFW=ON`. Building
GLFW from source on Linux additionally needs the X11 development headers
(`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`), which the
distro package already handles for you.

## macOS

```bash
brew install cmake glfw
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The shader check is skipped on macOS for the same reason as Windows: no EGL.

---

## CMake options

| Option | Default | Effect |
|---|---|---|
| `SRE_BUILD_VIEWER` | `ON` | Build the real-time viewer, if GLFW and OpenGL are found |
| `SRE_BUILD_TESTS` | `ON` | Build the physics test suite |
| `SRE_BUILD_SHADER_CHECK` | `ON` | Build the headless GLSL check, if EGL is found |
| `SRE_FETCH_GLFW` | `OFF` | Download and build GLFW when it is not installed |
| `SRE_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX`; used by CI |

## Verifying a build

```bash
ctest --test-dir build --output-on-failure     # add --build-config Release on MSVC
```

The physics suite runs in well under a second and needs no graphics stack. The
shader check reports *skipped* (exit code 77) rather than failing wherever EGL
is unavailable, so a green `ctest` on Windows or macOS means the physics passed
and the GLSL check was legitimately not applicable.
