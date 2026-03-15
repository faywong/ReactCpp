# Dual-thread renderer (SkPicture record + OpenGL replay) design

Date: 2026-03-15

## Goal

Introduce a dual-thread rendering architecture where Skia draw commands are recorded into immutable `SkPicture` objects, and the actual replay (drawing the `SkPicture`) happens in an OpenGL/GPU-backed render loop.

## SDL3 constraints

SDL3 documents `SDL_GL_MakeCurrent` and `SDL_GL_SwapWindow` as main-thread-only. Therefore, obeying SDL3 constraints implies:

- Main thread must own OpenGL context and swap.
- A worker thread performs app logic + picture recording.

This still meets the intent (record/replay split; GPU work isolated to the render loop).

## Thread model

- Main thread (RenderThread): SDL window + GL context + Skia Ganesh; polls events; applies platform side-effects (text input, clipboard, capture mouse); draws latest frame picture; swaps.
- Worker thread (RecordThread): owns instance tree and UI state; applies incoming events; reconciles/layouts; records a whole-frame `SkPicture`; publishes latest-wins.

## Data flow

Main -> Worker:
- `UiEvent` queue.

Worker -> Main:
- `FrameMailbox` with latest `sk_sp<SkPicture>`.
- `PlatformCommandQueue` for SDL calls that must run on main.
