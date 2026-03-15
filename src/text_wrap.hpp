#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "text_edit.hpp"

namespace reactcpp::text {

struct WrappedSpan {
    std::size_t start{0};
    std::size_t end{0};

    bool operator==(const WrappedSpan&) const = default;
};

template <typename Measure>
inline std::vector<WrappedSpan> wrap_text_spans(std::string_view text, float max_width, Measure measure) {
    std::vector<WrappedSpan> out;
    out.reserve(8);

    auto skip_spaces = [&](std::size_t i, std::size_t end) -> std::size_t {
        while (i < end) {
            char32_t cp = 0;
            std::size_t next = i;
            (void)utf8_decode_one(text, i, cp, next);
            if (!is_space_cp(cp)) break;
            i = next;
        }
        return i;
    };

    auto wrap_paragraph = [&](std::size_t para_begin, std::size_t para_end) {
        std::size_t i = skip_spaces(para_begin, para_end);

        if (i >= para_end) {
            out.push_back(WrappedSpan{para_end, para_end});
            return;
        }

        while (i < para_end) {
            std::size_t last_good = i;
            std::size_t best_break = static_cast<std::size_t>(-1);
            bool best_break_is_space = false;

            std::size_t j = i;
            while (j < para_end) {
                const std::size_t next = utf8_next_boundary(text, j);
                const std::size_t capped_next = std::min(next, para_end);
                const float w = measure(text.substr(i, capped_next - i));

                if (w > max_width) {
                    if (last_good == i) {
                        last_good = capped_next;
                    }
                    break;
                }

                last_good = capped_next;

                char32_t cp = 0;
                std::size_t cp_next = j;
                (void)utf8_decode_one(text, j, cp, cp_next);

                if (is_space_cp(cp)) {
                    best_break = j;
                    best_break_is_space = true;
                } else if (is_punct_cp(cp)) {
                    best_break = capped_next;
                    best_break_is_space = false;
                }

                j = capped_next;
            }

            std::size_t line_end = last_good;
            bool broke_on_space = false;
            if (last_good < para_end && best_break != static_cast<std::size_t>(-1) && best_break > i && best_break <= last_good) {
                const float w_best = measure(text.substr(i, best_break - i));
                const float gap_ratio = max_width > 0.0f ? ((max_width - w_best) / max_width) : 0.0f;
                if (gap_ratio <= 0.25f) {
                    line_end = best_break;
                    broke_on_space = best_break_is_space;
                }
            }

            out.push_back(WrappedSpan{i, line_end});
            i = line_end;
            if (broke_on_space || i < para_end) {
                i = skip_spaces(i, para_end);
            }

            if (i >= para_end) break;
        }
    };

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            wrap_paragraph(start, text.size());
            break;
        }

        wrap_paragraph(start, nl);
        start = nl + 1;
    }

    return out;
}

template <typename Measure>
inline std::size_t byte_index_for_wrapped_point(
    std::string_view text,
    float max_width,
    Measure measure,
    float local_x,
    float local_y,
    float line_height,
    float scroll_y
) {
    if (text.empty()) return 0;
    if (line_height <= 0.0f) line_height = 1.0f;

    const auto spans = wrap_text_spans(text, max_width, measure);
    if (spans.empty()) return 0;

    const float content_y = local_y + scroll_y;
    std::size_t line_index = 0;
    if (content_y > 0.0f) {
        line_index = static_cast<std::size_t>(content_y / line_height);
    }
    if (line_index >= spans.size()) {
        line_index = spans.size() - 1;
    }

    const WrappedSpan& sp = spans[line_index];
    const std::size_t line_start = std::min(sp.start, text.size());
    const std::size_t line_end = std::min(sp.end, text.size());
    if (line_end < line_start) return line_start;

    const std::string_view line_sv = text.substr(line_start, line_end - line_start);
    const std::size_t within = reactcpp::text::byte_index_for_x(
        line_sv,
        local_x,
        [&](std::size_t bytes) {
            bytes = std::min(bytes, line_sv.size());
            return measure(line_sv.substr(0, bytes));
        }
    );
    return line_start + within;
}

template <typename Measure>
inline std::vector<std::string> wrap_text(std::string_view text, float max_width, Measure measure) {
    std::vector<std::string> out;
    out.reserve(8);

    auto skip_spaces = [&](std::string_view s, std::size_t i) -> std::size_t {
        while (i < s.size()) {
            char32_t cp = 0;
            std::size_t next = i;
            (void)utf8_decode_one(s, i, cp, next);
            if (!is_space_cp(cp)) break;
            i = next;
        }
        return i;
    };

    auto wrap_paragraph = [&](std::string_view para) {
        std::size_t i = 0;
        i = skip_spaces(para, i);

        if (i >= para.size()) {
            out.emplace_back();
            return;
        }

        while (i < para.size()) {
            std::size_t last_good = i;
            std::size_t best_break = static_cast<std::size_t>(-1);
            bool best_break_is_space = false;

            std::size_t j = i;
            while (j < para.size()) {
                const std::size_t next = utf8_next_boundary(para, j);
                const float w = measure(para.substr(i, next - i));

                if (w > max_width) {
                    if (last_good == i) {
                        last_good = next;
                    }
                    break;
                }

                last_good = next;

                char32_t cp = 0;
                std::size_t cp_next = j;
                (void)utf8_decode_one(para, j, cp, cp_next);

                if (is_space_cp(cp)) {
                    best_break = j;
                    best_break_is_space = true;
                } else if (is_punct_cp(cp)) {
                    best_break = next;
                    best_break_is_space = false;
                }

                j = next;
            }

            std::size_t end = last_good;
            bool broke_on_space = false;
            if (last_good < para.size() && best_break != static_cast<std::size_t>(-1) && best_break > i && best_break <= last_good) {
                const float w_best = measure(para.substr(i, best_break - i));
                const float gap_ratio = max_width > 0.0f ? ((max_width - w_best) / max_width) : 0.0f;
                if (gap_ratio <= 0.25f) {
                    end = best_break;
                    broke_on_space = best_break_is_space;
                }
            }

            out.emplace_back(std::string(para.substr(i, end - i)));
            i = end;
            if (broke_on_space || i < para.size()) {
                i = skip_spaces(para, i);
            }

            if (i >= para.size()) break;
        }
    };

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            wrap_paragraph(text.substr(start));
            break;
        }

        wrap_paragraph(text.substr(start, nl - start));
        start = nl + 1;
    }

    return out;
}

}
