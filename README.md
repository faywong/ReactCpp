# ReactCpp

This is a small experimental C++20 reactive UI runtime PoC.

## Prerequisites

### 1) Build Skia via skia-builder

This project expects a local checkout of **skia-builder** that provides:

- Skia headers under: `skia/include/...`
- Static libraries under: `build/libskia.a` and `build/libskcms.a`

Steps:

```bash
git clone https://github.com/fonttools/skia-builder/ /path/to/your/skia-builder
cd /path/to/your/skia-builder
git submodule update --init --recursive
./build.sh
```

### 2) SDL3

SDL3 development headers/libs must be installed on your machine.

## Build

Configure with your skia-builder path:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSKIA_BUILDER_ROOT=/path/to/your/skia-builder
cmake --build build -j --target reactcpp_demo
```

Run:

```bash
./build/reactcpp_demo
```

## Rendering backend switches

By default the demo uses **Skia Ganesh (OpenGL)**.

- **Enable CPU raster backend (explicit opt-in)**:

```bash
cmake -S . -B build_cpu -DCMAKE_BUILD_TYPE=Release \
  -DSKIA_BUILDER_ROOT=/path/to/your/skia-builder \
  -DREACTCPP_USE_GANESH_GL=OFF \
  -DREACTCPP_ENABLE_CPU_RASTER=ON
cmake --build build_cpu -j --target reactcpp_demo
./build_cpu/reactcpp_demo
```

- **Allow GL -> CPU fallback with a loud warning log** (keep GL on, enable CPU raster too):

```bash
cmake -S . -B build_fallback -DCMAKE_BUILD_TYPE=Release \
  -DSKIA_BUILDER_ROOT=/path/to/your/skia-builder \
  -DREACTCPP_ENABLE_CPU_RASTER=ON
cmake --build build_fallback -j --target reactcpp_demo
./build_fallback/reactcpp_demo
```

To test the fallback log, you can force a GL init failure:

```bash
REACTCPP_FORCE_GL_FAIL=1 ./build_fallback/reactcpp_demo
```

## Notes

If `SKIA_BUILDER_ROOT` is missing, CMake will fail with a message that explains
how to clone/build skia-builder and how to pass `-DSKIA_BUILDER_ROOT=...`.
