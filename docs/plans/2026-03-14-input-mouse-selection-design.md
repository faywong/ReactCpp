# Input Mouse Editing (Click/Drag/Double-Click) — Design

**Date:** 2026-03-14

## Goal

Extend the Input editing UX from keyboard to mouse:

- Single click: place caret at clicked x position
- Drag (mouse down + move): update selection range continuously
- Double click: select a "word" using natural boundaries for mixed Chinese/English/punctuation

## Constraints

- Single-line Input only
- Selection applies to committed `InputState::value` only (IME `preedit` is excluded)
- No ICU or dictionary-based segmentation

## Event routing

SDL3 events:

- `SDL_EVENT_MOUSE_BUTTON_DOWN` (left)
  - Focus target
  - If target is Input: caret placement and/or word selection
  - Start selection drag and call `SDL_CaptureMouse(true)`
- `SDL_EVENT_MOUSE_MOTION`
  - If drag active: update caret and selection end
- `SDL_EVENT_MOUSE_BUTTON_UP` (left)
  - End drag and call `SDL_CaptureMouse(false)`

Double click is detected via `event.button.clicks == 2`.

## Mapping mouse x to cursor byte index

- Convert window coordinates to surface coordinates (so hit-testing and text metrics match rendering)
- Compute input-local x = surface_x - input_abs_x - text_padding_x
- Convert x to byte index by binary searching UTF-8 boundary offsets, using `SkFont::measureText` for prefix widths

## Word boundary heuristic

Double click selects contiguous runs by character class:

- Latin word run: ASCII letters/digits/underscore
- CJK run: contiguous CJK Unified Ideographs (U+4E00–U+9FFF)
- Punctuation run: ASCII punct and common Unicode punctuation blocks
- Space: selects nothing

## Drag selection model

- On mouse-down in Input, set `sel_anchor` to the caret index at press time
- On motion, update `cursor` from current x and set selection as `[min(anchor,cursor), max(anchor,cursor)]`
