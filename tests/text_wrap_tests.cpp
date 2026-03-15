#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "text_edit.hpp"
#include "text_wrap.hpp"

static float measure_codepoints(std::string_view s) {
    float w = 0.0f;
    std::size_t i = 0;
    while (i < s.size()) {
        i = reactcpp::text::utf8_next_boundary(s, i);
        w += 1.0f;
    }
    return w;
}

static void expect_lines(std::string_view input, float max_width, const std::vector<std::string>& expected) {
    const auto lines = reactcpp::text::wrap_text(input, max_width, measure_codepoints);
    assert(lines == expected);
}

static void expect_spans(std::string_view input, float max_width, const std::vector<std::string>& expected) {
    const auto spans = reactcpp::text::wrap_text_spans(input, max_width, measure_codepoints);
    std::vector<std::string> got;
    got.reserve(spans.size());
    for (const auto& sp : spans) {
        assert(sp.start <= sp.end);
        assert(sp.end <= input.size());
        got.emplace_back(std::string(input.substr(sp.start, sp.end - sp.start)));
    }
    assert(got == expected);
}

static void test_hard_breaks_on_newline() {
    expect_lines("a\nb", 999.0f, {"a", "b"});
    expect_lines("a\n\nb", 999.0f, {"a", "", "b"});

    expect_spans("a\nb", 999.0f, {"a", "b"});
    expect_spans("a\n\nb", 999.0f, {"a", "", "b"});
}

static void test_prefers_spaces() {
    expect_lines("hello world", 5.0f, {"hello", "world"});
    expect_lines("hello   world", 5.0f, {"hello", "world"});

    expect_spans("hello world", 5.0f, {"hello", "world"});
    expect_spans("hello   world", 5.0f, {"hello", "world"});
}

static void test_does_not_wrap_when_text_fits() {
    expect_lines("hello world", 999.0f, {"hello world"});
    expect_spans("hello world", 999.0f, {"hello world"});

    expect_lines("a b c", 999.0f, {"a b c"});
    expect_spans("a b c", 999.0f, {"a b c"});
}

static void test_prefers_punctuation_breaks() {
    expect_lines("hello,world", 6.0f, {"hello,", "world"});
    expect_lines("a,b,c", 2.0f, {"a,", "b,", "c"});

    expect_spans("hello,world", 6.0f, {"hello,", "world"});
    expect_spans("a,b,c", 2.0f, {"a,", "b,", "c"});
}

static void test_falls_back_to_utf8_boundary_when_no_breakpoints() {
    expect_lines("abcdef", 2.0f, {"ab", "cd", "ef"});
    expect_spans("abcdef", 2.0f, {"ab", "cd", "ef"});

    const std::string s = std::string("a") + "\xF0\x9F\x98\x8A" + "b";
    expect_lines(s, 2.0f, {std::string("a") + "\xF0\x9F\x98\x8A", "b"});
    expect_spans(s, 2.0f, {std::string("a") + "\xF0\x9F\x98\x8A", "b"});
}

static void test_breaks_long_word_after_space_to_fill_line() {
    expect_lines("hello ABCDEFGHIJK", 8.0f, {"hello AB", "CDEFGHIJ", "K"});
    expect_spans("hello ABCDEFGHIJK", 8.0f, {"hello AB", "CDEFGHIJ", "K"});
}

static void test_breaks_word_to_avoid_large_right_gap() {
    expect_lines("hello wonderful", 10.0f, {"hello wond", "erful"});
    expect_spans("hello wonderful", 10.0f, {"hello wond", "erful"});
}

static void test_point_to_byte_index_in_wrapped_text_basic() {
    const std::string s = "hello world";
    const float max_width = 5.0f;
    const float line_height = 10.0f;

    assert(reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 0.0f, 0.0f, line_height, 0.0f) == 0);
    assert(reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 2.0f, 0.0f, line_height, 0.0f) == 2);
    assert(reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 999.0f, 0.0f, line_height, 0.0f) == 5);

    assert(reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 0.0f, line_height + 0.1f, line_height, 0.0f) == 6);
    assert(reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 3.0f, line_height + 0.1f, line_height, 0.0f) == 9);
    assert(reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 999.0f, line_height + 0.1f, line_height, 0.0f) == s.size());
}

static void test_point_to_byte_index_in_wrapped_text_respects_scroll_y() {
    const std::string s = "foo bar";
    const float max_width = 3.0f;
    const float line_height = 10.0f;

    const std::size_t caret = reactcpp::text::byte_index_for_wrapped_point(s, max_width, measure_codepoints, 1.0f, 0.0f, line_height, line_height);
    assert(caret == 5);

    const auto word = reactcpp::text::word_selection_at(s, caret);
    assert(word.active);
    assert(word.start == 4);
    assert(word.end == 7);
}

int main() {
    test_hard_breaks_on_newline();
    test_prefers_spaces();
    test_does_not_wrap_when_text_fits();
    test_prefers_punctuation_breaks();
    test_falls_back_to_utf8_boundary_when_no_breakpoints();
    test_breaks_long_word_after_space_to_fill_line();
    test_breaks_word_to_avoid_large_right_gap();
    test_point_to_byte_index_in_wrapped_text_basic();
    test_point_to_byte_index_in_wrapped_text_respects_scroll_y();
    return 0;
}
