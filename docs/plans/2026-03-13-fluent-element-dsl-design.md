# Fluent Element DSL (Builder Chaining) Design

**Date:** 2026-03-13

## Goal

Upgrade UI authoring from the current props-lambda style to a fluent builder DSL that makes the element tree structure visually dominant and easy to read.

The DSL must remain lightweight, header-only, and keep runtime behavior unchanged.

## Context

Current authoring style (before this change):
- `AppRoot()` builds a `View/Text/Button/Input` tree by mutating `*Props` via lambdas.

Reference style:
- cycfi/elements examples compose the UI with readable tree expressions and local fluent setters, while layout is expressed via composition.

## Non-goals

- Changing reconciliation, hook semantics, batching behavior, or SkPicture caching.
- Adding new runtime element types.
- Introducing external dependencies.

## Design

### API surface

All DSL APIs live in `src/element_dsl.hpp` under `namespace reactcpp::ui`.

Provide node/builder public types:
- `ViewNode`
- `TextNode`
- `ButtonNode`
- `InputNode`

Each node:
- Owns its corresponding `*Props` value.
- Exposes chainable setters for the fields used by the demo.
- Can be converted into an `Element` by building via existing runtime factories (`View/Text/Button/Input`).

### Tree composition

Use `ViewNode::operator()(children...)` as the primary composition syntax.

Example shape:

```cpp
return view().padding(24).bg(...)(
  text().margin(6).value(...),
  button().margin(6).size(260,48).on_click(...),
  input().margin(6).size(380,44).value("")
);
```

This keeps the tree structure explicit in the expression shape.

### Optional layout sizing semantics

`FlexStyle::width/height` are `std::optional<float>`. The DSL provides:
- `.width(v)`, `.height(v)`, `.size(w,h)` to set explicit sizes
- `.auto_width()`, `.auto_height()` to reset to `std::nullopt`

### Event handlers

The DSL exposes `.on_click(fn)` on view-like nodes. It stores callbacks in `ViewProps::on_click` using the existing type `std::shared_ptr<const std::function<void()>>` so runtime equality semantics remain unchanged.

## Verification

- Build: `cmake --build build -j --target react_cpp_sdl_skia_demo`
- Smoke-run: `timeout 1s ./build/react_cpp_sdl_skia_demo` (must not crash)

## Files

- Modify: `src/element_dsl.hpp`
- Modify: `src/main.cpp`
- Modify: `AGENTS.md`
- Add: `docs/plans/2026-03-13-fluent-element-dsl-design.md`
