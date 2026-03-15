# RenderThread (OpenGL) + RecordThread (SkPicture) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Split rendering into two threads: main thread owns SDL3 OpenGL + Skia Ganesh and replays a whole-frame `SkPicture`, worker thread runs UI logic and records the `SkPicture`.

**Architecture:** Main thread polls SDL events, forwards them to worker, applies platform commands, draws latest picture and swaps. Worker thread owns instance tree/state, applies events, runs reconcile/layout, records a frame picture with `SkPictureRecorder`, and publishes it latest-wins.

**Tech Stack:** C++20, SDL3 OpenGL, Skia Ganesh GL.
