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

## Rendering backend

By default the demo uses **Skia Ganesh (OpenGL)**.

The demo always attempts Ganesh GL first. If GL initialization fails at runtime,
it automatically falls back to the CPU raster backend and prints a loud warning
to stderr with a stable prefix:

```
[reactcpp][WARN][GL->CPU] ...
```

The initial window size is computed at runtime as **0.6x** the primary display's
usable desktop bounds (no hard-coded 800x600).

## Notes

If `SKIA_BUILDER_ROOT` is missing, CMake will fail with a message that explains
how to clone/build skia-builder and how to pass `-DSKIA_BUILDER_ROOT=...`.
