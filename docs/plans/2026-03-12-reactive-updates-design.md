# Reactive Updates (use_state + onClick) Design

**Date:** 2026-03-12

## Goal

Enable reactive UI updates in the Skia/SDL demo:

- AppRoot uses `use_state<int>` to hold a counter.
- Button click increments the counter.
- Title text displays the counter.
- `setState` does **not** synchronously re-render; it only requests an update.
- Runtime performs one `perform_update()` at a safe frame boundary (after event dispatch), then reconciles and redraws.

## Non-goals (for this iteration)

- Dedicated render thread.
- General event system beyond Button click.
- Hook types beyond `use_state`.

## Current constraints / findings

- `run_skia_app()` runs a single-threaded SDL event loop.
- Runtime currently calls `render_frame()` every frame.
- `use_state` exists but `HookKind` has only `State`, making uninitialized slots ambiguous.
- `StateHandle` currently stores a raw pointer into `std::any` payload, which is unsafe.
- No Button hit-testing; `handle_mouse_down()` currently focuses only Input.

## Proposed changes

### 1) Scheduling model (single thread)

- Introduce a global `request_update()` callable from `StateHandle::set()`.
- `request_update()` sets an atomic/flag `pending_update`.
- SDL loop runs handlers, then checks `pending_update`:
  - If set: call `runtime.perform_update()` once.
  - Always draw every frame (or draw only if needed later).

This prevents re-entrancy: `setState` never calls reconcile/paint directly.

### 2) Make `use_state` safe

- Add `HookKind::None` and default slots to None.
- Change `StateHandle` to store `{instance, hook_index, generation}` only.
- `get()` and `set()` re-locate the payload via `std::any_cast<T>` on demand.
- `set()` validates `generation_tag` to avoid stale handles.
- `set()` calls `request_update()`.

### 3) Button onClick without breaking props comparability

`ElementProps` uses equality; `ButtonProps` currently has `operator== default`.

- Add `ButtonProps::on_click` as `std::shared_ptr<const std::function<void()>>`.
- Replace `ButtonProps::operator==` with a custom implementation that ignores `on_click` (handlers do not affect visuals).

### 4) Click dispatch

- Implement hit-testing for `host_type_button()` similar to Input.
- On mouse down:
  - If hit is Input: focus.
  - If hit is Button: invoke `on_click` if present.
  - Otherwise: clear focus.

### 5) Demo wiring

- In `src/main.cpp`, build counter state and render it into title text.
- Create a Button handler that increments counter using the `StateHandle` setter.

## Verification

- Build: `cmake --build build -j --target react_cpp_sdl_skia_demo`
- Run: click button and verify title counter increments.
- Ensure no infinite loops / event re-entrancy.
