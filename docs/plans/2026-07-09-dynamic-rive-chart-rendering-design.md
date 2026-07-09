# Dynamic Rive/Chart rendering design

**Date:** 2026-07-09

## Goal

Update the retained Skia rendering model so high-frequency dynamic components are
not captured into reusable node-level `SkPicture` display lists.

This applies to:

- `Rive`
- `line_chart`
- `scatter_chart`
- `area_chart`
- `bar_chart`
- `circle_chart`
- `histogram_chart`

These components are driven by animation state machines, interpolation/easing, or
live data sources. Their visual output can change every frame even when the
virtual node shape and static props are unchanged.

## Prior behavior

`render_cached_node()` used a uniform node cache path:

1. If an instance was dirty or missing `cached_picture`, record the node draw into
   a local `SkPicture`.
2. Store it in `InstanceNode::cached_picture`.
3. Replay the cached picture on later frames.

This is correct for mostly static nodes such as `View`, `Text`, `Button`,
`Input`, `InputArea`, and `Canvas`.

For Rive and charts, this cache can be the wrong abstraction because:

- Rive state machines advance through time and apply animation curves.
- Rive inputs can update from `RiveInputs` without rebuilding the VDOM.
- Charts can update from `VectorDataSource<T>` without app render/reconcile.
- Reusing a previously recorded picture can freeze algorithmic or real-time
  output unless every dynamic change perfectly invalidates the node cache.

## New behavior

`SkiaRuntime::render_cached_node()` now has a dynamic-component bypass:

- If the host type is `Rive` or any chart type:
  - clear `node.cached_picture`
  - call the component renderer directly on the current frame canvas
  - mark the node clean for bookkeeping
- Otherwise:
  - keep the existing dirty/missing `SkPicture` recording path
  - replay `node.cached_picture` for clean static nodes

Children, focus rings, carets, and overlays still use the same traversal order
after the local node draw.

## Repaint-only path

The repaint-only path remains:

1. `VectorDataSource<T>` or `RiveInputs` calls `request_repaint()`.
2. If no hook/state update is pending, `perform_update_if_needed()` skips
   `app_render_()` and `reconcile()`.
3. `refresh_data_driven_nodes()` updates data-driven revisions and marks affected
   instances dirty.
4. During draw, dynamic components bypass node-level `SkPicture` caching and draw
   directly into the current frame recording.

This preserves the main performance property: high-frequency data updates avoid
VDOM rebuild and reconciliation. It changes only the local draw strategy for
dynamic nodes.

## Threaded Ganesh implications

The worker thread still records a whole-frame `SkPicture` for publication through
`FrameMailbox`.

The change is specifically about per-node display-list caching:

- Static nodes may still contribute cached node pictures.
- Dynamic Rive/Chart nodes draw into the frame recording each frame.
- `Frame::retained_pictures` retains only the static node pictures referenced by
  the frame; dynamic nodes do not add node-level retained pictures.

This keeps cross-thread frame handoff unchanged while preventing dynamic widgets
from being represented by stale per-node display lists.

## API and demo naming

The public Rive component name is simplified:

- `RivePlayerProps` -> `RiveProps`
- `RivePlayer(...)` -> `Rive(...)`
- `reactcpp::ui::rive_player()` -> `reactcpp::ui::rive()`

The CMake demo target is simplified:

- `reactcpp_demo` -> `demo`

`reactcpp_demo` is no longer the target name used by local builds or release
packaging.

## Repository publishing note

`homelab` is the preferred push remote for the internal commercial version:

```text
https://repo.faywong.cc:5000/faywong/ReactCpp.git
```

GitHub `origin` remains the external open-source remote and may intentionally
lag behind internal features.
