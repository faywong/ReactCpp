#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace reactcpp::text {

inline bool is_utf8_continuation_byte(unsigned char c) {
    return (c & 0xC0) == 0x80;
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

struct Selection {
    std::size_t start{0};
    std::size_t end{0};
    bool active{false};
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

inline void erase_selection(std::string& value, std::size_t& cursor, Selection& sel) {
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

inline void insert_text(std::string& value, std::size_t& cursor, Selection& sel, std::string_view inserted) {
    erase_selection(value, cursor, sel);
    cursor = std::min(cursor, value.size());
    value.insert(cursor, inserted);
    cursor += inserted.size();
}

}
