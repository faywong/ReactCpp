# TextBuffer + Encoding/Index Strategy (ByteIndex-first)

**Date:** 2026-03-14

## Goal

Reduce the complexity of text editing as features expand (single-line now, multi-line later) by:

- Introducing a `TextBuffer` abstraction (`src/text_buffer.hpp`) to own buffer storage
- Keeping editing state (cursor/selection) consistent and safe

## Encoding decision

We keep **UTF-8 bytes** as the canonical storage and interchange format because:

- SDL3 text input events (`SDL_EVENT_TEXT_INPUT`, IME editing) provide UTF-8
- Skia text rendering and measurement APIs consume UTF-8 conveniently
- Clipboard text is UTF-8

This avoids repeated transcodes and keeps rendering/editing aligned.

## About "redefining char" / alternative encodings

In C++ `char` is a byte; trying to redefine it to represent a Unicode scalar is not practical.

Two alternatives exist:

1) **Store `char32_t` codepoints (e.g. `std::u32string`)**
   - Pros: cursor/selection can be stored as codepoint indices (fewer UTF-8 boundary details)
   - Cons: forces transcoding to/from UTF-8 for SDL/Skia/clipboard; emoji / grapheme clusters still need special handling; memory overhead

2) **Keep UTF-8 bytes but make indices explicit** (recommended)
   - Keep internal cursor/selection as byte offsets
   - Centralize UTF-8 boundary movement (`utf8_prev_boundary` / `utf8_next_boundary`)
   - Add helper APIs that always clamp to a safe UTF-8 boundary

Even with `char32_t`, grapheme clusters mean you still need separate logic for "visual cursor".

## Index safety recommendation

For future refactors (optional): introduce strong types to prevent mixing index domains:

- `ByteIndex` (offset into UTF-8 byte buffer)
- `CodepointIndex` (offset in decoded codepoints)

Then require explicit conversion functions (e.g., `byte_index_to_codepoint_index`, `codepoint_index_to_byte_index`) so call sites can’t accidentally mix them.

## Buffer backend

`TextBuffer` currently supports:

- `Kind::String` (simple `std::string` backend)
- `Kind::Gap` (simple internal gap buffer backend)

Inputs default to `Kind::Gap` today to reduce middle-edit churn while keeping the public interface stable.
