# Input Selection Highlight + Shift Selection Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Render Input selection with dark-blue background + white selected text, and support `Shift + Left/Right` to expand/shrink selection.

**Architecture:** Keep selection logic in the runtime, add a stable `sel_anchor` to `InputState`, add pure helper to derive a selection range from (anchor, cursor), and render selection in `InputRenderer` using `measureText` and `SkFontMetrics`.

**Tech Stack:** C++20, Skia (SkCanvas/SkFont/SkFontMetrics), SDL3 key modifiers.

---

### Task 1: RED — Add failing unit tests for anchor-based selection

**Files:**
- Modify: `tests/text_edit_tests.cpp`

**Step 1: Write failing tests**

Add tests for a new helper:

- Given `anchor=2`, `cursor=5`, length 10 => selection active, start=2, end=5.
- Given `anchor=5`, `cursor=2`, length 10 => selection active, start=2, end=5.
- Given `anchor=3`, `cursor=3`, length 10 => selection inactive.

**Step 2: Run tests to verify failure**

```bash
cmake -S . -B build
cmake --build build -j --target react_cpp_text_edit_tests
ctest --test-dir build --output-on-failure
```

Expected: build fails because the helper function doesn’t exist.

### Task 2: GREEN — Implement helper for selection-from-anchor

**Files:**
- Modify: `src/text_edit.hpp`

**Step 1: Implement function**

Add:

- `Selection selection_from_anchor(std::size_t anchor, std::size_t cursor, std::size_t text_len)`

It returns:

- inactive selection if anchor==cursor
- otherwise active selection with normalized start/end clamped to text_len

**Step 2: Re-run tests (GREEN)**

```bash
cmake --build build -j --target react_cpp_text_edit_tests
ctest --test-dir build --output-on-failure
```

Expected: PASS.

### Task 3: Update InputState for selection anchor

**Files:**
- Modify: `src/skia_runtime.hpp`

**Step 1: Add field**

Add `std::size_t sel_anchor{0};`.

**Step 2: Initialize/reset it**

- In init_node_state: `sel_anchor = cursor`.
- On blur: reset anchor.
- After text insertion/deletion that clears selection: set anchor to current cursor.

### Task 4: Implement Shift+Left/Right selection behavior

**Files:**
- Modify: `src/skia_runtime.cpp`

**Step 1: Detect shift modifier**

- `bool shift = (mod & SDL_KMOD_SHIFT) != 0;`

**Step 2: Update arrow key branch**

- If shift: set anchor if selection not active; move cursor by UTF-8 boundary; set selection via `selection_from_anchor`.
- If not shift: clear selection and anchor; move cursor.

### Task 5: Render selection highlight in InputRenderer

**Files:**
- Modify: `src/skia_runtime.cpp` (InputRenderer::on_draw)

**Step 1: Only render highlight when preedit empty**

If `node.input_state` and `has_selection` and `preedit.empty()`:

- Compute selection rectangle using `SkFontMetrics` and `measureText`.
- Draw dark-blue fill rectangle.
- Draw selected substring in white.

### Task 6: Verification

**Step 1: LSP diagnostics**

Run `lsp_diagnostics` on:

- `src/text_edit.hpp`
- `src/skia_runtime.hpp`
- `src/skia_runtime.cpp`
- `tests/text_edit_tests.cpp`

**Step 2: Build + test**

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

**Step 3: Demo build**

```bash
cmake --build build -j --target react_cpp_sdl_skia_demo
```
