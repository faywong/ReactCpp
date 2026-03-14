#+#+#+#+#+#+#+#+━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Input Keyboard Shortcuts + Clipboard (SDL3) — Design

**Date:** 2026-03-14

## Goal

Extend the existing `Input` element editing behavior to support:

- Left/Right arrow keys move the caret (UTF-8 codepoint-aware)
- `Ctrl+A` select all
- `Ctrl+C` copy selection to system clipboard
- `Ctrl+X` cut selection to system clipboard
- `Ctrl+V` paste from system clipboard

This is implemented in the SDL3+Skia runtime (`src/skia_runtime.*`).

## Non-goals (for this iteration)

- Rendering a selection highlight (selection exists logically but not drawn yet)
- Shift+arrow selection expansion
- Word navigation (Ctrl+Left/Right), Home/End, Delete, etc.

## Current architecture (baseline)

- `InstanceNode::InputState` stores per-input state:
  - `value` (UTF-8 bytes)
  - `cursor` (byte index into `value`)
  - `preedit` and related fields for IME composition
- SDL events are handled in `run_skia_app()` and routed into `SkiaRuntime`:
  - `SDL_EVENT_TEXT_INPUT` inserts committed text at `cursor`
  - `SDL_EVENT_TEXT_EDITING` updates `preedit`
  - `SDL_EVENT_KEY_DOWN` previously handled only Backspace

## Proposed design

### 1) Selection model

Add minimal logical selection state to `InputState`:

- `sel_start` / `sel_end` (byte indices into `value`)
- `has_selection` boolean

Selection invariants:

- Clamp to `[0, value.size()]`
- Normalize so `sel_start <= sel_end`

### 2) Caret movement

- Left arrow: move `cursor` to previous UTF-8 codepoint boundary.
- Right arrow: move `cursor` to next UTF-8 codepoint boundary.

Implementation uses UTF-8 boundary helpers that treat `cursor` as a byte index.

### 3) Clipboard shortcuts (SDL3)

Use SDL3 clipboard text APIs:

- `SDL_SetClipboardText(const char*)`
- `SDL_GetClipboardText()` (caller frees with `SDL_free()`)

Shortcut behavior (when an input is focused):

- `Ctrl+A` (or `GUI+A`): select all (`[0, value.size()]`) and set `cursor = sel_end`.
- `Ctrl+C` (or `GUI+C`): copy selected substring to clipboard.
- `Ctrl+X` (or `GUI+X`): copy selected substring, then delete selection.
- `Ctrl+V` (or `GUI+V`): paste clipboard text, replacing selection if present.

Notes:

- The accelerator modifier is treated as `Ctrl` **or** `GUI` so macOS Command works too.
- Clipboard operations are only performed if there is an active non-empty selection.

### 4) Interaction with IME preedit

To avoid breaking IME composition:

- When `preedit` is non-empty, arrow keys may be intercepted by the IME; the runtime should not attempt to edit committed `value` based on those keys.
- Clipboard and selection operations apply only to committed `value`.

### 5) Text insertion/deletion with selection

- Text input insertion replaces the selection if one exists.
- Backspace deletes the selection if one exists; otherwise it deletes the previous UTF-8 codepoint.

## Verification

Add a minimal CTest-based unit test executable that validates:

- `utf8_next_boundary` and `utf8_prev_boundary` behavior for ASCII + multi-byte UTF-8
- Selection replace and erase behavior in pure helper functions

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
