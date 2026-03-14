# Input Mouse Editing (Click/Drag/Double-Click) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add mouse-based caret placement, drag selection, and double-click word selection to the Input control.

**Architecture:** Add pure word-boundary helpers to `src/text_edit.hpp` with unit tests. Integrate SDL3 mouse button/motion events into `SkiaRuntime`, including a small drag state and a robust x->cursor inverse mapping based on `SkFont::measureText`.

**Tech Stack:** C++20, SDL3 events, Skia SkFont/measureText.

---

### Task 1: RED — Add failing tests for word selection runs

**Files:**
- Modify: `tests/text_edit_tests.cpp`

**Step 1:** Add tests that assert `word_selection_at(text, byte_index)` selects:

- Latin run
- CJK run
- punctuation run
- space -> inactive

**Step 2:** Build tests and confirm they fail.

### Task 2: GREEN — Implement `word_selection_at` in `src/text_edit.hpp`

**Files:**
- Modify: `src/text_edit.hpp`

**Step 1:** Implement UTF-8 decode + character classification + run expansion.

**Step 2:** Re-run unit tests and confirm PASS.

### Task 3: Integrate mouse events in `SkiaRuntime`

**Files:**
- Modify: `src/skia_runtime.cpp`

**Step 1:** Add mouse handlers:

- `handle_mouse_button_down(x, y, clicks)`
- `handle_mouse_move(x, y)`
- `handle_mouse_button_up(x, y)`

**Step 2:** Add a drag state in `SkiaRuntime`:

- active flag
- target input pointer

**Step 3:** Implement x->cursor mapping:

- window->surface coordinate conversion
- input-local x
- binary search over UTF-8 boundary offsets + `measureText`

**Step 4:** Event loop wiring:

- route mouse down (with clicks)
- route mouse motion
- route mouse up

### Task 4: Verification

- `cmake --build build -j`
- `ctest --test-dir build --output-on-failure`
- build demo target
