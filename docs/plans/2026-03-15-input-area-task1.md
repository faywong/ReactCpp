# InputArea (Task 1) Implementation Plan

> NOTE (2026-03-15): This was an early Task 1 plan drafted before `InputArea` was fully implemented.
> The `feat/input-area` branch now contains a complete `InputArea` implementation (wrapping, selection, caret, scrolling, IME),
> plus a demo case in `src/main.cpp`.

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a new `InputArea` host element type (props + DSL + demo usage) that currently renders like `View` and compiles cleanly.

**Architecture:** `InputArea` is a new host `TypeId` with `InputAreaProps : ViewProps` stored in `ElementProps`. Rendering reuses the existing `ViewRenderer` (via the default “unknown type -> view renderer” path), so no new renderer/input behavior is introduced yet.

**Tech Stack:** C++20, std::variant-based props (`ElementProps`), existing Skia renderer + Yoga layout.

---

### Task 1: Add InputArea props + public API surface

**Files:**
- Modify: `src/skia_runtime.hpp`

**Step 1: Add `InputAreaProps`**

- Define `struct InputAreaProps : ViewProps` with fields matching `InputProps` that will be used later:
  - `std::string value`, `std::string placeholder`
  - `float text_size`, `text_r/g/b`
  - `float border_r/g/b`
- Provide `bool operator==(const InputAreaProps&) const = default;` (base `ViewProps::operator==` already ignores handler identity).

**Step 2: Extend `ElementProps` + factory declarations**

- Add `InputAreaProps` to the `ElementProps` variant.
- Declare:
  - `TypeId host_type_input_area();`
  - `Element InputArea(const InputAreaProps& props, std::vector<Element> children = {});`

### Task 2: Add InputArea host type + factory implementation

**Files:**
- Modify: `src/skia_runtime.cpp`

**Step 1: Implement host type**

- Add `TypeId host_type_input_area()` in the host type section (same pattern as view/button/text/input).

**Step 2: Implement `Element InputArea(...)`**

- Construct an `Element` with:
  - `type = host_type_input_area()`
  - `props = props`
  - `children = std::move(children)`

**Step 3: Ensure view-like rendering works**

- Update `ViewRenderer` to read props via `props_as_view_ref(node.current_vnode)` instead of `std::get<ViewProps>(...)`, so view-like derived props (including `InputAreaProps`) can be rendered without adding a new renderer.

### Task 3: Add DSL builder for InputArea

**Files:**
- Modify: `src/element_dsl.hpp`

**Step 1: Implement `InputAreaNode`**

- `class InputAreaNode final : public ViewLikeNode<InputAreaNode, InputAreaProps>`
- Provide builder methods (mirroring `InputNode`) for:
  - `value(std::string)`
  - `placeholder(std::string)`
  - `text_size(float)`
  - `text_color(float r, float g, float b)`
  - `border_color(float r, float g, float b)`
- Support children like `ViewNode`:
  - `add(Element child)`
  - `operator()(Children&&... children)`
- `build()` returns `InputArea(props_, children_)`.
- Add `inline InputAreaNode input_area()`.

### Task 4: Update demo to instantiate InputArea

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add an `input_area()` example**

- Insert an `input_area()` element into the demo tree (e.g., below the existing input) with a fixed size + background and an inner `text()` child so it’s visible.
- Do not change existing Input behavior; this is additive.

### Task 5: Verification

**Step 1: LSP diagnostics**

Run `lsp_diagnostics` on:

- `src/skia_runtime.hpp`
- `src/skia_runtime.cpp`
- `src/element_dsl.hpp`
- `src/main.cpp`

Expected: no errors.

**Step 2: Build**

In the worktree:

```bash
cmake --build build -j --target reactcpp_demo
```

Expected: build succeeds.
