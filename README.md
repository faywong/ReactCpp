# ReactCpp

This is a small experimental C++20 reactive UI runtime PoC.

## Prerequisites

### 1) Prepare the ReactCpp Skia SDK

Skia is still the rendering backend, but the preferred setup path is now the
ReactCpp Skia SDK wrapper. It clones/updates `skia-builder`, builds a
ReactCpp-specific Skia profile, and installs the SDK under `.reactcpp/skia-sdk/`.

```bash
python3 scripts/skia/setup_skia_sdk.py
```

The SDK layout is:

- Skia headers under: `.reactcpp/skia-sdk/<platform>-<arch>/skia/include/...`
- Skia libraries under: `.reactcpp/skia-sdk/<platform>-<arch>/lib/`
- A manifest at: `.reactcpp/skia-sdk/<platform>-<arch>/reactcpp-skia-sdk.json`

The profile enables the capabilities this runtime currently needs:

- Ganesh GL
- CPU raster-compatible core rendering
- Linux text dependencies: freetype, fontconfig, harfbuzz

CMake still supports a legacy external `skia-builder` checkout through
`-DSKIA_BUILDER_ROOT=/path/to/skia-builder`, but the SDK path is preferred.

If `gh` is installed and authenticated, you can install the latest successful
daily artifact instead of building Skia locally:

```bash
python3 scripts/skia/setup_skia_sdk.py --from-github-artifact
```

### 2) SDL3

SDL3 development headers/libs must be installed on your machine.

## Build

Configure:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target reactcpp_demo
```

If your SDK lives outside the default `.reactcpp/skia-sdk/<platform>-<arch>`
location, pass:

```bash
cmake -S . -B build -DREACTCPP_SKIA_SDK_ROOT=/path/to/reactcpp-skia-sdk
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

If neither the ReactCpp Skia SDK nor the legacy `SKIA_BUILDER_ROOT` checkout is
available, CMake skips the SDL+Skia demo target and still builds the non-renderer
unit tests.

## Skia SDK automation

`.github/workflows/skia-sdk-daily.yml` builds ReactCpp Skia SDK archives every
day and on manual dispatch. The workflow uploads platform-specific artifacts:

- `reactcpp-skia-sdk-linux-x64`
- `reactcpp-skia-sdk-macos-arm64`
- `reactcpp-skia-sdk-windows-x64`

This keeps Skia as the only supported renderer while moving the hard part into a
repeatable SDK build pipeline.

For maintainers, `scripts/skia/build_skia_sdk.py` is the lower-level build
script used by both local setup and CI.
