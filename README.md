# ReactCpp

This is a small experimental C++20 reactive UI runtime PoC.

![ReactCpp demo showing text input, multiline editing, and a draw.io canvas architecture diagram](docs/images/reactcpp-demo.png)

## Rendering engine features

- **Declarative C++ UI**: author UI with React/Revery-style pure functions that return `Element` trees.
- **Hooks and batched updates**: `use_state<T>` stores ordered hook slots on instance nodes and coalesces state updates until the next frame boundary.
- **Virtual/instance reconciliation**: the runtime diffs new virtual trees against retained instance nodes and invalidates only changed nodes.
- **Yoga layout**: host nodes use Yoga Flexbox layout, with measured text/input nodes and retained layout results.
- **Skia rendering**: Ganesh OpenGL is attempted first, with CPU raster fallback when GL initialization fails.
- **SkPicture caching**: each instance records its own cached `SkPicture`; dirty nodes re-record while unchanged nodes reuse cached drawing.
- **Threaded Ganesh path**: the GL mode records UI frames on a worker thread and presents on the SDL main thread through a frame mailbox.
- **SDL3 input**: mouse hit testing, click bubbling, focus, IME text input, caret placement, selection, clipboard, undo, and multiline editing are handled in the runtime.
- **System font selection**: Linux text rendering uses fontconfig-backed Skia font management and chooses a system CJK font for Chinese text.
- **Draw.io canvas**: `Canvas` renders a raw/uncompressed draw.io `mxGraphModel` subset directly with Skia.

## Component usage

The fluent DSL lives in `src/element_dsl.hpp` under `reactcpp::ui`.

### View

`view()` is the flex container primitive. It supports common box/layout props from `ViewProps`:

```cpp
view()
    .column()
    .padding(24.0f)
    .bg(0.95f, 0.96f, 0.98f)
(
    text().value("Hello ReactCpp")
);
```

Useful helpers:

- `.row()` / `.column()`
- `.justify(...)` and `.align(...)`
- `.padding(...)`, `.margin(...)`, `.width(...)`, `.height(...)`, `.size(w, h)`
- `.bg(r, g, b, a)`
- `.on_click(...)`, `.on_focus(...)`, `.on_blur(...)`

### Text

`text()` renders a single text label.

```cpp
text()
    .margin(6.0f)
    .value("中文 text renders through system CJK fonts")
    .text_size(24.0f)
    .text_color(0.12f, 0.15f, 0.24f);
```

### Button

`button()` renders a clickable rounded rectangle with a label.

```cpp
auto counter = use_state<int>(0);

button()
    .size(260.0f, 48.0f)
    .bg(0.25f, 0.52f, 0.93f)
    .label("Increment")
    .text_size(20.0f)
    .text_color(1.0f, 1.0f, 1.0f)
    .on_click([counter]() {
        counter.update([](int v) { return v + 1; });
    });
```

### Input

`input()` is a single-line editable text field. It supports focus, IME input, caret movement, selection, clipboard shortcuts, undo, and horizontal scrolling.

```cpp
input()
    .size(380.0f, 44.0f)
    .bg(1.0f, 1.0f, 1.0f)
    .placeholder("Click to focus")
    .value("");
```

### InputArea

`input_area()` is a multiline editor with wrapping, vertical scrolling, Enter insertion, selection, clipboard, undo, and IME candidate positioning.

```cpp
input_area()
    .size(520.0f, 180.0f)
    .bg(1.0f, 1.0f, 1.0f)
    .border_color(0.70f, 0.70f, 0.70f)
    .text_size(16.0f)
    .text_color(0.10f, 0.10f, 0.10f)
    .placeholder("Type multiple lines")
    .value("Line 1\nLine 2: 中文换行测试");
```

### Canvas

`canvas()` renders a draw.io diagram from raw/uncompressed `mxGraphModel` XML. The first implementation supports common `mxCell` vertices and edges, including rectangles, rounded rectangles, ellipses, labels, and source/target connector lines.

```cpp
canvas()
    .size(720.0f, 230.0f)
    .bg(1.0f, 1.0f, 1.0f)
    .diagram_padding(18.0f)
    .drawio_xml(R"drawio(
<mxGraphModel>
  <root>
    <mxCell id="0"/>
    <mxCell id="1" parent="0"/>
    <mxCell id="a" value="App" style="rounded=1;fillColor=#DBEAFE;strokeColor=#2563EB;" vertex="1" parent="1">
      <mxGeometry x="20" y="20" width="120" height="56" as="geometry"/>
    </mxCell>
  </root>
</mxGraphModel>
)drawio");
```

Compressed draw.io `<diagram>` payloads are not inflated yet; pass raw `mxGraphModel` XML for now.

## Prerequisites

### 1) Prepare the ReactCpp Skia SDK

Skia is still the rendering backend. The quickest setup path is to download the
latest SDK artifact produced by GitHub Actions instead of building Skia locally.

1. Open the latest successful
   [Skia SDK Daily workflow run](https://github.com/faywong/ReactCpp/actions/workflows/skia-sdk-daily.yml?query=branch%3Amain+is%3Asuccess).
2. Download the artifact for your platform:
   - `reactcpp-skia-sdk-linux-x64`
   - `reactcpp-skia-sdk-macos-arm64`
   - `reactcpp-skia-sdk-windows-x64`
3. Unzip the downloaded GitHub artifact. It contains an SDK archive such as
   `reactcpp-skia-sdk-linux-x64.zip`.
4. Install that SDK archive:

   ```bash
   python3 scripts/skia/setup_skia_sdk.py --archive /path/to/reactcpp-skia-sdk-linux-x64.zip
   ```

If you need to build the SDK locally, run the setup wrapper without `--archive`.
That clones/updates `skia-builder`, builds the ReactCpp-specific Skia profile,
and installs the SDK under `.reactcpp/skia-sdk/`.

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
