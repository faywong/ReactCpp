# Ganesh GL Default + CPU Raster Opt-in Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the SDL+Skia demo use Skia Ganesh (OpenGL) by default; CPU raster becomes an explicit opt-in CMake option. Fix Wayland failures caused by `GrGLMakeNativeInterface()` by switching to an assembled GL interface using `SDL_GL_GetProcAddress`. If GL initialization fails and CPU raster is enabled, automatically fall back to CPU with a loud, developer-visible log.

**Architecture:**
- The demo remains a single binary (`reactcpp_demo`) with two backends.
- Backend selection is primarily compile-time (what is compiled/linked), with a runtime attempt/fallback behavior:
  - Prefer GL when compiled.
  - If GL initialization fails at runtime:
    - If CPU raster code is compiled in, fall back to CPU raster and print a loud warning.
    - Otherwise, fail fast with the GL error.

**Tech Stack:** CMake, SDL3, Skia Ganesh (OpenGL), Skia software raster, Yoga.

---

## Constraints / Requirements

1. **Default backend = Ganesh GL** (no extra flags needed in a clean environment).
2. **CPU raster must be explicitly enabled** via CMake to compile/enable it.
3. **Wayland compatibility:** do not rely on `GrGLMakeNativeInterface()`; use Skia assembled interface with `SDL_GL_GetProcAddress`.
4. **Fallback behavior (user confirmed policy B):** If GL fails, only fall back to CPU when CPU raster is enabled. When falling back, print an obvious warning to stderr.

---

## Task 1: Add/adjust CMake options (GL default, CPU explicit)

**Files:**
- Modify: `CMakeLists.txt`

**Step 1: Update options**
- Change the default for `REACTCPP_USE_GANESH_GL` to `ON`.
- Add a new option `REACTCPP_ENABLE_CPU_RASTER` default `OFF`.

**Step 2: Wire compile definitions**
- When `REACTCPP_USE_GANESH_GL` is ON:
  - Define `REACTCPP_USE_GANESH_GL=1`
  - Link `OpenGL::GL`
- When `REACTCPP_ENABLE_CPU_RASTER` is ON:
  - Define `REACTCPP_ENABLE_CPU_RASTER=1`

**Step 3: Configure-time and runtime expectations**
- Default configure/build should compile the GL path.
- CPU raster path is only compiled if the CPU option is enabled.

**Verification command (configure):**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSKIA_BUILDER_ROOT=/path/to/skia-builder
```
Expected: configuration succeeds; OpenGL found.

---

## Task 2: Replace `GrGLMakeNativeInterface` with assembled interface (Wayland-safe)

**Files:**
- Modify: `src/skia_runtime.cpp`

**Step 1: Include header for assembled interface**
- Add:
  - `#include "gpu/ganesh/gl/GrGLAssembleInterface.h"`

**Step 2: Create Skia GL interface via SDL proc loader**
- After `SDL_GL_MakeCurrent(window, glctx)` create:
  - A `GrGLGetProc` compatible function that calls `SDL_GL_GetProcAddress(name)`.
  - Call `GrGLMakeAssembledInterface(nullptr, getProc)`.
  - Validate interface (`interface && interface->validate()`).

**Step 3: Create `GrDirectContext`**
- Use `GrDirectContexts::MakeGL(interface)`.

**Verification:**
- Grep the repo for `GrGLMakeNativeInterface` and ensure it is no longer used.

---

## Task 3: Refactor backend execution and add loud fallback log

**Files:**
- Modify: `src/skia_runtime.cpp`

**Step 1: Split into helper functions (internal)**
- Introduce internal helpers in an unnamed namespace:
  - `static int run_with_ganesh_gl(const AppRenderFunc& app)`
  - `#if defined(REACTCPP_ENABLE_CPU_RASTER) static int run_with_cpu_raster(const AppRenderFunc& app) #endif`

**Step 2: Implement fallback logic in `run_reactcpp_app`**
- If GL compiled:
  - `try` GL path
  - `catch (const std::exception& e)`:
    - If CPU raster compiled:
      - Print loud stderr warning and continue with CPU path.
    - Else rethrow.
- If GL not compiled:
  - If CPU compiled, run CPU.
  - Else throw with a clear message about CMake options.

**Step 3: Loud warning content**
- Print to stderr with a stable prefix, e.g.
  - `[reactcpp][WARN][GL->CPU] ...`
- Include:
  - the exception message
  - SDL video driver (`SDL_GetCurrentVideoDriver()`)
  - `GL_VERSION` string if available

---

## Task 4: Verification matrix

**Build A (default, GL-only):**
```bash
cmake -S . -B build_gl_default -DCMAKE_BUILD_TYPE=Release \
  -DSKIA_BUILDER_ROOT=/path/to/skia-builder
cmake --build build_gl_default -j
```

**Run (Wayland):**
```bash
timeout 2s env SDL_VIDEODRIVER=wayland ./build_gl_default/reactcpp_demo
```
Expected: stays running until timeout (exit 124), no `GrGLMakeNativeInterface failed`.

**Build B (GL + CPU raster fallback enabled):**
```bash
cmake -S . -B build_gl_plus_cpu -DCMAKE_BUILD_TYPE=Release \
  -DSKIA_BUILDER_ROOT=/path/to/skia-builder \
  -DREACTCPP_ENABLE_CPU_RASTER=ON
cmake --build build_gl_plus_cpu -j
```

**Simulate GL failure to exercise fallback log**
- Example: force a failing GL profile/version attribute (temporary env/config), or run in an environment without GL.
Expected: program prints `[reactcpp][WARN][GL->CPU] ...` and continues via CPU raster.

---

## Task 5: Run tests

**Commands:**
```bash
ctest --test-dir build_gl_default --output-on-failure
ctest --test-dir build_gl_plus_cpu --output-on-failure
```
Expected: all tests pass.
