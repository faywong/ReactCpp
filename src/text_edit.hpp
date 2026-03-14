#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "text_buffer.hpp"

namespace reactcpp::text {

inline bool is_utf8_continuation_byte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

inline std::size_t utf8_clamp_to_boundary(std::string_view s, std::size_t i) {
    i = std::min(i, s.size());
    while (i > 0 && i < s.size() && is_utf8_continuation_byte(static_cast<unsigned char>(s[i]))) {
        --i;
    }
    return i;
}

inline bool utf8_decode_one(std::string_view s, std::size_t i, char32_t& out_cp, std::size_t& out_next) {
    if (i >= s.size()) return false;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if ((c0 & 0x80) == 0) {
        out_cp = static_cast<char32_t>(c0);
        out_next = i + 1;
        return true;
    }

    auto need = [&](int n) -> bool { return i + static_cast<std::size_t>(n) <= s.size(); };
    auto cont = [&](std::size_t j) -> unsigned char { return static_cast<unsigned char>(s[j]); };

    if ((c0 & 0xE0) == 0xC0 && need(2)) {
        unsigned char c1 = cont(i + 1);
        if (!is_utf8_continuation_byte(c1)) goto invalid;
        out_cp = static_cast<char32_t>(((c0 & 0x1F) << 6) | (c1 & 0x3F));
        out_next = i + 2;
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && need(3)) {
        unsigned char c1 = cont(i + 1);
        unsigned char c2 = cont(i + 2);
        if (!is_utf8_continuation_byte(c1) || !is_utf8_continuation_byte(c2)) goto invalid;
        out_cp = static_cast<char32_t>(((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F));
        out_next = i + 3;
        return true;
    }
    if ((c0 & 0xF8) == 0xF0 && need(4)) {
        unsigned char c1 = cont(i + 1);
        unsigned char c2 = cont(i + 2);
        unsigned char c3 = cont(i + 3);
        if (!is_utf8_continuation_byte(c1) || !is_utf8_continuation_byte(c2) || !is_utf8_continuation_byte(c3)) goto invalid;
        out_cp = static_cast<char32_t>(((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
        out_next = i + 4;
        return true;
    }

invalid:
    out_cp = static_cast<char32_t>(c0);
    out_next = i + 1;
    return true;
}

inline std::size_t utf8_prev_boundary(std::string_view s, std::size_t cursor) {
    cursor = std::min(cursor, s.size());
    if (cursor == 0) return 0;
    std::size_t i = cursor;
    do {
        --i;
    } while (i > 0 && is_utf8_continuation_byte(static_cast<unsigned char>(s[i])));
    return i;
}

inline std::size_t utf8_next_boundary(std::string_view s, std::size_t cursor) {
    cursor = std::min(cursor, s.size());
    if (cursor >= s.size()) return s.size();
    std::size_t i = cursor + 1;
    while (i < s.size() && is_utf8_continuation_byte(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    return i;
}

template <typename MeasurePrefixBytes>
inline std::size_t byte_index_for_x(std::string_view s, float local_x, MeasurePrefixBytes measure_prefix_bytes) {
    if (local_x <= 0.0f) return 0;
    if (s.empty()) return 0;

    std::vector<std::size_t> bounds;
    bounds.reserve(s.size() + 1);
    std::size_t i = 0;
    bounds.push_back(0);
    while (i < s.size()) {
        i = utf8_next_boundary(s, i);
        bounds.push_back(i);
    }

    std::size_t lo = 0;
    std::size_t hi = bounds.size() - 1;
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        const std::size_t bytes = bounds[mid];
        const float w = measure_prefix_bytes(bytes);
        if (w <= local_x) lo = mid;
        else hi = mid - 1;
    }

    const std::size_t floor_bytes = bounds[lo];
    const float floor_w = measure_prefix_bytes(floor_bytes);
    if (lo + 1 >= bounds.size()) return floor_bytes;

    const std::size_t ceil_bytes = bounds[lo + 1];
    const float ceil_w = measure_prefix_bytes(ceil_bytes);
    if (std::fabs(ceil_w - local_x) < std::fabs(local_x - floor_w)) return ceil_bytes;
    return floor_bytes;
}

struct Selection {
    std::size_t start{0};
    std::size_t end{0};
    bool active{false};
};

enum class WordClass : std::uint8_t {
    Space,
    Latin,
    CJK,
    Punct,
    Other
};

inline bool is_ascii_word(char32_t cp) {
    if (cp >= U'a' && cp <= U'z') return true;
    if (cp >= U'A' && cp <= U'Z') return true;
    if (cp >= U'0' && cp <= U'9') return true;
    if (cp == U'_') return true;
    return false;
}

inline bool is_space_cp(char32_t cp) {
    if (cp == 0x20 || cp == 0x3000) return true;
    if (cp == 0x09 || cp == 0x0A || cp == 0x0D) return true;
    return false;
}

inline bool is_cjk_cp(char32_t cp) {
    return cp >= 0x4E00 && cp <= 0x9FFF;
}

inline bool is_punct_cp(char32_t cp) {
    if (cp < 0x80) {
        if (cp >= 0x21 && cp <= 0x2F) return true;
        if (cp >= 0x3A && cp <= 0x40) return true;
        if (cp >= 0x5B && cp <= 0x60) return true;
        if (cp >= 0x7B && cp <= 0x7E) return true;
        return false;
    }
    if (cp >= 0x2000 && cp <= 0x206F) return true;
    if (cp >= 0x3000 && cp <= 0x303F) return true;
    if (cp >= 0xFF00 && cp <= 0xFFEF) return true;
    return false;
}

inline WordClass word_class(char32_t cp) {
    if (is_space_cp(cp)) return WordClass::Space;
    if (is_cjk_cp(cp)) return WordClass::CJK;
    if (is_ascii_word(cp)) return WordClass::Latin;
    if (is_punct_cp(cp)) return WordClass::Punct;
    return WordClass::Other;
}

inline Selection word_selection_at(std::string_view s, std::size_t byte_index) {
    if (s.empty()) return Selection{};
    byte_index = utf8_clamp_to_boundary(s, byte_index);
    if (byte_index >= s.size()) return Selection{};

    char32_t cp = 0;
    std::size_t next = byte_index;
    (void)utf8_decode_one(s, byte_index, cp, next);
    const WordClass cls = word_class(cp);
    if (cls == WordClass::Space) return Selection{};

    std::size_t start = byte_index;
    while (start > 0) {
        const std::size_t prev = utf8_prev_boundary(s, start);
        if (prev == start) break;
        char32_t pcp = 0;
        std::size_t pnext = prev;
        (void)utf8_decode_one(s, prev, pcp, pnext);
        if (word_class(pcp) != cls) break;
        start = prev;
    }

    std::size_t end = next;
    while (end < s.size()) {
        const std::size_t b = end;
        char32_t ncp = 0;
        std::size_t nnext = b;
        (void)utf8_decode_one(s, b, ncp, nnext);
        if (word_class(ncp) != cls) break;
        end = nnext;
    }

    Selection sel;
    sel.active = start < end;
    sel.start = start;
    sel.end = end;
    return sel;
}

struct UndoSnapshot {
    std::string value;
    std::size_t cursor{0};

    std::size_t sel_start{0};
    std::size_t sel_end{0};
    std::size_t sel_anchor{0};
    bool has_selection{false};

    bool operator==(const UndoSnapshot&) const = default;
};

class UndoHistory {
public:
    explicit UndoHistory(std::size_t max_depth = 100) : max_depth_(max_depth) {}

    void push(const UndoSnapshot& snap) {
        if (max_depth_ == 0) return;
        if (!stack_.empty() && stack_.back() == snap) return;
        stack_.push_back(snap);
        if (stack_.size() > max_depth_) {
            stack_.erase(stack_.begin());
        }
    }

    std::optional<UndoSnapshot> pop() {
        if (stack_.empty()) return std::nullopt;
        UndoSnapshot out = std::move(stack_.back());
        stack_.pop_back();
        return out;
    }

    void clear() {
        stack_.clear();
    }

    std::size_t size() const {
        return stack_.size();
    }

private:
    std::size_t max_depth_{100};
    std::vector<UndoSnapshot> stack_{};
};

inline Selection selection_from_anchor(std::size_t anchor, std::size_t cursor, std::size_t text_len) {
    anchor = std::min(anchor, text_len);
    cursor = std::min(cursor, text_len);
    if (anchor == cursor) {
        return Selection{};
    }
    Selection sel;
    sel.active = true;
    sel.start = std::min(anchor, cursor);
    sel.end = std::max(anchor, cursor);
    return sel;
}

inline void normalize_selection(Selection& sel, std::size_t text_len) {
    sel.start = std::min(sel.start, text_len);
    sel.end = std::min(sel.end, text_len);
    if (sel.start > sel.end) {
        std::swap(sel.start, sel.end);
    }
    if (sel.start == sel.end) {
        sel.active = false;
    }
}

inline void clear_selection(Selection& sel) {
    sel.active = false;
    sel.start = 0;
    sel.end = 0;
}

inline bool has_non_empty_selection(const Selection& sel) {
    return sel.active && sel.start < sel.end;
}

inline std::string selected_substr(const std::string& value, Selection sel) {
    normalize_selection(sel, value.size());
    if (!has_non_empty_selection(sel)) return std::string();
    return value.substr(sel.start, sel.end - sel.start);
}

inline std::string selected_substr(const TextBuffer& value, Selection sel) {
    normalize_selection(sel, value.size());
    if (!has_non_empty_selection(sel)) return std::string();
    return value.substr(sel.start, sel.end - sel.start);
}

inline void erase_selection(TextBuffer& value, std::size_t& cursor, Selection& sel) {
    normalize_selection(sel, value.size());
    if (!has_non_empty_selection(sel)) {
        clear_selection(sel);
        cursor = std::min(cursor, value.size());
        return;
    }
    value.erase(sel.start, sel.end - sel.start);
    cursor = sel.start;
    clear_selection(sel);
}

inline void insert_text(TextBuffer& value, std::size_t& cursor, Selection& sel, std::string_view inserted) {
    erase_selection(value, cursor, sel);
    cursor = std::min(cursor, value.size());
    value.insert(cursor, inserted);
    cursor += inserted.size();
}

}
