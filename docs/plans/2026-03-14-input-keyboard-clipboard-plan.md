# Input Keyboard Shortcuts + Clipboard Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add caret movement (Left/Right), `Ctrl+A` select-all, and `Ctrl+X/C/V` clipboard cut/copy/paste for the `Input` element.

**Architecture:** Introduce a small pure helper module for UTF-8 boundary and selection-aware edit ops, unit-test it with CTest, then integrate these ops into `SkiaRuntime` key handling.

**Tech Stack:** C++20, CMake/CTest, SDL3 clipboard (`SDL_SetClipboardText`, `SDL_GetClipboardText`, `SDL_free`).

---

### Task 1: Add a failing unit test executable

**Files:**
- Create: `tests/text_edit_tests.cpp`

**Step 1: Write failing tests (RED)**

Write tests for:

- UTF-8 right boundary movement for multi-byte chars
- Selection replace behavior (insert replaces selection)

**Step 2: Run build to verify failure**

Run:

```bash
cmake -S . -B build
cmake --build build -j --target react_cpp_text_edit_tests
```

Expected: compile/link failure because the helper module doesn’t exist yet.

### Task 2: Add CTest wiring

**Files:**
- Modify: `CMakeLists.txt`

**Step 1: Add `enable_testing()`**

Add `enable_testing()` and define a test executable target:

- `add_executable(react_cpp_text_edit_tests tests/text_edit_tests.cpp)`
- `add_test(NAME text_edit_tests COMMAND react_cpp_text_edit_tests)`

**Step 2: Run CTest to confirm test is discovered**

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -N
```

Expected: `text_edit_tests` is listed.

### Task 3: Implement the helper module (GREEN)

**Files:**
- Create: `src/text_edit.hpp`

**Step 1: Implement UTF-8 boundaries**

Implement:

- `utf8_prev_boundary(std::string_view, size_t)`
- `utf8_next_boundary(std::string_view, size_t)`

**Step 2: Implement minimal selection-aware ops**

- selection struct (`has_selection`, `start`, `end`)
- erase selection
- insert text (replaces selection)

**Step 3: Re-run tests (GREEN)**

```bash
cmake --build build -j --target react_cpp_text_edit_tests
ctest --test-dir build --output-on-failure
```

Expected: PASS.

### Task 4: Integrate with `SkiaRuntime` input handling

**Files:**
- Modify: `src/skia_runtime.hpp`
- Modify: `src/skia_runtime.cpp`

**Step 1: Extend `InstanceNode::InputState`**

Add selection fields (`sel_start`, `sel_end`, `has_selection`). Ensure init and reconcile clamp/clear appropriately.

**Step 2: Update text insertion/backspace to be selection-aware**

- Text input inserts at cursor, but replaces selection if present.
- Backspace deletes selection if present; otherwise keep current UTF-8 backspace behavior.

**Step 3: Add key handling for Left/Right and Ctrl shortcuts**

- Handle `SDLK_LEFT` / `SDLK_RIGHT` to move caret.
- Handle `Ctrl/GUI + A/C/X/V` using SDL clipboard.

### Task 5: Verification

**Step 1: LSP diagnostics**

Run `lsp_diagnostics` on:

- `src/skia_runtime.hpp`
- `src/skia_runtime.cpp`
- `src/text_edit.hpp`
- `tests/text_edit_tests.cpp`

**Step 2: Build + test**

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

**Step 3: Build demo target**

```bash
cmake --build build -j --target react_cpp_sdl_skia_demo
```

Expected: build succeeds.
