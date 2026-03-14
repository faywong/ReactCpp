# Input Selection Highlight + Shift Selection — Design

**Date:** 2026-03-14

## Goal

Improve the Input selection UX:

- Selected text is visually highlighted with a dark-blue background.
- Selected text is drawn in white for contrast.
- Selection range can be expanded/shrunk with `Shift + Left/Right`.

## Scope

- Single-line Input only (current runtime behavior).
- Selection applies only to committed `InputState::value`.
- IME composition (`preedit`) is not included and does not render selection highlight.

## Data model

`InstanceNode::InputState` adds:

- `sel_start`, `sel_end`, `has_selection` (existing logical selection)
- `sel_anchor` (new): fixed anchor byte-offset used while extending selection with Shift.

## Key handling

When focused and not composing (`preedit` empty):

- `Left/Right` (no shift): clear selection; move caret by UTF-8 boundary.
- `Shift + Left/Right`: keep `sel_anchor` fixed; move caret; selection becomes `[min(anchor,cursor), max(anchor,cursor)]`.
  - If selection collapses (cursor == anchor), `has_selection` becomes false.

`Ctrl/Cmd + A` sets:

- `sel_anchor = 0`
- `cursor = value.size()`
- `has_selection = true`, `sel_start=0`, `sel_end=value.size()`

## Rendering

When `has_selection` and `preedit` is empty:

1) Draw selection highlight rectangle behind selected substring.
   - X positions computed via `SkFont::measureText` on UTF-8 byte ranges.
   - Y extents computed via `SkFontMetrics` ascent/descent around the baseline.
2) Draw text in three segments: left (normal), selected (white), right (normal).

## Verification

- Unit tests for selection-from-anchor behavior in `tests/text_edit_tests.cpp`.
- Build + `ctest`.
