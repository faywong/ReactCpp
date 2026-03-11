# PROJECT KNOWLEDGE BASE

**Generated:** 2026-03-11

## OVERVIEW
Small experimental repo for a native reactive GUI framework PoC built in C++20, with Skia (CPU, static lib in ./skia) and SDL3 planned as the windowing backend. The design goal is a React/Revery-style declarative UI model with hooks, Virtual/Instance trees, and SkPicture-based rendering caches.

## DESIGN GOALS & CONSTRAINTS
- **Developer experience first**
  - Reactive, pure-functional style authoring (React/Revery-like components, hooks, declarative trees).
  - Minimize boilerplate needed to express GUI structure, state, and rendering behavior.
- **Runtime performance first-class**
  - Use Skia's display list (SkPicture) and CPU raster backend for high throughput.
  - Cache per-widget drawing and reuse between frames when props/state/layout are unchanged.
- **Language choices**
  - **Primary**: Modern C++ (C++20 and beyond, using newer/experimental features when they clearly improve expressiveness or safety).
  - **Fallback/extension**: Zig, but only when C++ cannot provide both developer-friendly expressiveness and maximal performance/concurrency.
- **Windowing & IO**
  - SDL3 for window management and event handling (already installed on host).
- **Rendering backend**
  - Prebuilt Skia static library under `./skia` (CPU raster-only configuration; no GL/Vulkan in current args.gn).

## CURRENT STRUCTURE
```text
./
├── CMakeLists.txt        # Single-target CMake config for console PoC
├── build/                # CMake build artifacts (generated)
├── skia/                 # Prebuilt Skia static libs + ninja files
│   ├── libskia.a
│   ├── libskcms.a
│   ├── args.gn           # Skia build configuration (CPU, no GL, no fonts, etc.)
│   └── obj/              # Object files for Skia build (do not edit)
└── src/
    ├── main.cpp          # Console PoC entry (Virtual/Instance tree, no graphics yet)
    ├── runtime.hpp       # Minimal Element + InstanceNode + use_state skeleton
    └── runtime.cpp       # Simple reconciler and console renderer
```

## WHERE TO LOOK (TODAY)
| Task                                    | Location          | Notes |
|----------------------------------------|-------------------|-------|
| Project build entry                     | `CMakeLists.txt`  | Currently only builds `react_cpp_demo` console executable. |
| Core reactive runtime skeleton          | `src/runtime.*`   | Contains Element, InstanceNode, and a basic use_state implementation. |
| Current demo behavior (console render)  | `src/main.cpp`    | Prints a simple tree; currently not using GUI or Skia. |
| Skia static libraries & build metadata  | `skia/`           | `libskia.a`, `libskcms.a`, ninja/args.gn from Skia's build. |

## ROADMAP: FROM PoC TO MINIMAL REACTIVE GUI

1. **Stabilize Runtime Core (C++)**
   - Define a clean Virtual Tree API (Element factories like `View`, `Text`, `Rect`, etc.).
   - Implement a stable Instance Tree with hooks:
     - `use_state<T>` with ordered slot storage and generation tags.
     - Hook dispatcher tied to the currently rendering Instance.
   - Implement a small but correct reconciler:
     - Diff by `(type, key)` where keys exist; position-based otherwise.
     - Maintain minimal Instance reuse and subtree replacement.

2. **Introduce Skia Rendering**
   - Add a second executable (e.g., `react_cpp_sdl_skia_demo`) that:
     - Uses SDL3 for window and event loop.
     - Creates a CPU raster `SkSurface` (`SkSurface::MakeRasterN32Premul` or `MakeRasterDirect`).
     - Renders the Element tree to a `SkCanvas` each frame (simple shapes first).

3. **Add SkPicture-Based Caching**
   - For each widget/node, maintain a `sk_sp<SkPicture>` cache:
     - Mark node as dirty when its props/state/layout change.
     - On render:
       - If dirty → record via `SkPictureRecorder` and store.
       - If clean → `canvas->drawPicture(cachedPicture)`.
   - Gradually separate **local** vs **composite** caches for complex subtrees.

4. **Integrate SDL3 Events with State Updates**
   - Map SDL3 events to internal `UIEvent` and hit-test against the layout tree.
   - Allow components to register handlers (e.g., `onClick`) that call `setState`.
   - Batch state updates per frame to avoid redundant diffs/renders.

5. **Yoga Layout (Future Step)**
   - Introduce Yoga nodes bound to InstanceNodes for Flexbox layout.
   - Map style props (e.g., `flexDirection`, `padding`, `width`, `height`) to Yoga.
   - Only mark nodes as layout-dirty when style or constraints change; layout-dirty drives SkPicture invalidation.

6. **Evaluate Zig Surface (Optional)**
   - When C++ templates/type-erasure for hooks and high-concurrency become too awkward, prototype a Zig runtime:
     - Keep the architecture identical (Virtual → Instance → Skia), but rewrite runtime core.
     - Use Zig's tagged unions and explicit allocators for clearer hook storage and scheduling.

## IMPLEMENTATION CONSTRAINTS & PREFERENCES

### Developer Experience
- Aim for React/Revery-like component authoring:
  - Components are pure functions of props returning `Element` trees.
  - Hooks (`use_state`, `use_effect`, `use_memo`) provide local state and side effects.
- Prefer modern C++ features that reduce boilerplate and improve safety:
  - `std::optional`, `std::variant`/`std::any` (or better typed wrappers), lambdas, `constexpr` helpers.
  - Concepts and constrained templates where they help express intent (for advanced iterations).
- Do not leak Skia/SDL/Yoga internals into application code; keep them behind a friendly API.

### Performance
- Favor **retained mode** UI with partial updates over full re-render each frame.
- Use dirty flags & subtree-level invalidation to minimize SkPicture re-recording.
- Keep the hot path (render loop, reconciler, layout) allocation-light and branch-predictable.

### Language Selection Rules
- Default to **C++ (latest standard available)** for:
  - Integration with Skia, SDL, Yoga.
  - Rich ecosystem and tooling on the host.
- Consider **Zig** when BOTH conditions hold:
  1. C++ abstractions for hooks/state/scheduling become too complex or error-prone (UB risk, template bloat).
  2. Zig can demonstrably provide:
     - More ergonomic APIs for functional/reactive-style components, AND
     - Equal or better runtime characteristics (esp. concurrency, allocator control).

### Concurrency
- Initial iterations can run single-threaded for simplicity.
- Future directions:
  - Explore background reconciliation/render planning on worker threads.
  - Consider using Skia CPU rendering on dedicated worker threads with double-buffered surfaces.

## CONVENTIONS
- **Directory usage**
  - `src/` is for framework/runtime and demos.
  - `skia/` is treated as a vendor/prebuilt third-party dependency; do not modify its contents by hand.
- **Build system**
  - CMake is the single source of truth for builds.
  - Additional executables (e.g., Skia/SDL demo, future Zig prototypes) should be declared in the root CMakeLists.
- **Coding style**
  - Prefer simple, explicit data structures over over-abstracted metaprogramming.
  - Avoid undefined behavior; favor clear ownership and lifetimes in Instance trees and hooks.

## ANTI-PATTERNS (FOR THIS REPO)
- Adding application-specific logic directly into the Skia/SDL integration layer instead of through components.
- Leaking Skia/SDL/Yoga types into public component APIs.
- Implementing hooks without strict ordering & generation checks (invites subtle bugs).
- Overgeneralizing types too early (complex template hierarchies that obscure intent).
- Treating `skia/` as editable source; it is prebuilt and should remain read-only.

## NEXT STEPS FOR CONTRIBUTORS
- Implement a second executable with SDL3 + Skia integration that uses the existing runtime skeleton.
- Evolve `src/runtime.*` toward a clean Virtual/Instance/Render separation and add `use_state`-driven demos.
- Introduce a minimal SkPicture cache at the widget level, then iterate into subtree caching.
- Only consider a Zig-based runtime after the C++ design reaches clear limits in ergonomics or safety.
