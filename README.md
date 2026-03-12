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
cmake --build build -j --target react_cpp_sdl_skia_demo
```

Run:

```bash
./build/react_cpp_sdl_skia_demo
```

## Notes

If `SKIA_BUILDER_ROOT` is missing, CMake will fail with a message that explains
how to clone/build skia-builder and how to pass `-DSKIA_BUILDER_ROOT=...`.
