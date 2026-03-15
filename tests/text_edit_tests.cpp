#include <cstddef>
#include <string>

#include "text_edit.hpp"
#include "text_buffer.hpp"

#include "test_util.hpp"

static void test_utf8_boundaries() {
    const std::string s = std::string("a") + "\xF0\x9F\x98\x8A" + "b";
    REACTCPP_TEST_ASSERT(s.size() == 6);

    std::size_t c = 0;
    c = reactcpp::text::utf8_next_boundary(s, c);
    REACTCPP_TEST_ASSERT(c == 1);
    c = reactcpp::text::utf8_next_boundary(s, c);
    REACTCPP_TEST_ASSERT(c == 5);
    c = reactcpp::text::utf8_next_boundary(s, c);
    REACTCPP_TEST_ASSERT(c == 6);

    c = reactcpp::text::utf8_prev_boundary(s, c);
    REACTCPP_TEST_ASSERT(c == 5);
    c = reactcpp::text::utf8_prev_boundary(s, c);
    REACTCPP_TEST_ASSERT(c == 1);
    c = reactcpp::text::utf8_prev_boundary(s, c);
    REACTCPP_TEST_ASSERT(c == 0);
}

static void test_selection_erase_and_insert() {
    reactcpp::text::Selection sel;
    sel.active = true;
    sel.start = 1;
    sel.end = 4;

    reactcpp::text::TextBuffer v("hello");
    std::size_t cursor = 5;

    reactcpp::text::erase_selection(v, cursor, sel);
    REACTCPP_TEST_ASSERT(v.to_string() == "ho");
    REACTCPP_TEST_ASSERT(cursor == 1);
    REACTCPP_TEST_ASSERT(!sel.active);

    sel.active = true;
    sel.start = 1;
    sel.end = 2;
    cursor = 2;

    reactcpp::text::insert_text(v, cursor, sel, "X");
    REACTCPP_TEST_ASSERT(v.to_string() == "hX");
    REACTCPP_TEST_ASSERT(cursor == 2);
    REACTCPP_TEST_ASSERT(!sel.active);
}

static void test_selection_from_anchor() {
    {
        const auto s = reactcpp::text::selection_from_anchor(2, 5, 10);
        REACTCPP_TEST_ASSERT(s.active);
        REACTCPP_TEST_ASSERT(s.start == 2);
        REACTCPP_TEST_ASSERT(s.end == 5);
    }
    {
        const auto s = reactcpp::text::selection_from_anchor(5, 2, 10);
        REACTCPP_TEST_ASSERT(s.active);
        REACTCPP_TEST_ASSERT(s.start == 2);
        REACTCPP_TEST_ASSERT(s.end == 5);
    }
    {
        const auto s = reactcpp::text::selection_from_anchor(3, 3, 10);
        REACTCPP_TEST_ASSERT(!s.active);
        REACTCPP_TEST_ASSERT(s.start == 0);
        REACTCPP_TEST_ASSERT(s.end == 0);
    }
}

static void test_undo_history() {
    reactcpp::text::UndoHistory h(3);

    reactcpp::text::UndoSnapshot s1;
    s1.value = "a";
    s1.cursor = 1;

    reactcpp::text::UndoSnapshot s2;
    s2.value = "ab";
    s2.cursor = 2;

    reactcpp::text::UndoSnapshot s3;
    s3.value = "abc";
    s3.cursor = 3;

    reactcpp::text::UndoSnapshot s4;
    s4.value = "abcd";
    s4.cursor = 4;

    h.push(s1);
    h.push(s2);
    h.push(s3);
    h.push(s4);

    REACTCPP_TEST_ASSERT(h.size() == 3);

    auto u = h.pop();
    REACTCPP_TEST_ASSERT(u.has_value());
    REACTCPP_TEST_ASSERT(u->value == "abcd");

    u = h.pop();
    REACTCPP_TEST_ASSERT(u.has_value());
    REACTCPP_TEST_ASSERT(u->value == "abc");
}

static void test_word_selection_runs() {
    const std::string s = std::string("hello") + "\xE4\xB8\x96\xE7\x95\x8C" + ",foo_bar123 " + "\xE4\xB8\xAD\xE6\x96\x87";

    {
        const auto sel = reactcpp::text::word_selection_at(s, 1);
        REACTCPP_TEST_ASSERT(sel.active);
        REACTCPP_TEST_ASSERT(sel.start == 0);
        REACTCPP_TEST_ASSERT(sel.end == 5);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 5);
        REACTCPP_TEST_ASSERT(sel.active);
        REACTCPP_TEST_ASSERT(sel.start == 5);
        REACTCPP_TEST_ASSERT(sel.end == 11);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 11);
        REACTCPP_TEST_ASSERT(sel.active);
        REACTCPP_TEST_ASSERT(sel.start == 11);
        REACTCPP_TEST_ASSERT(sel.end == 12);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 13);
        REACTCPP_TEST_ASSERT(sel.active);
        REACTCPP_TEST_ASSERT(sel.start == 12);
        REACTCPP_TEST_ASSERT(sel.end == 22);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 22);
        REACTCPP_TEST_ASSERT(!sel.active);
        REACTCPP_TEST_ASSERT(sel.start == 0);
        REACTCPP_TEST_ASSERT(sel.end == 0);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 23);
        REACTCPP_TEST_ASSERT(sel.active);
        REACTCPP_TEST_ASSERT(sel.end - sel.start == 6);
    }
}

static void test_byte_index_for_x_monospaced() {
    const std::string s = "abcd";
    auto measure = [](std::size_t bytes) { return static_cast<float>(bytes); };

    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, -1.0f, measure) == 0);
    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, 0.0f, measure) == 0);
    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, 0.49f, measure) == 0);
    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, 0.51f, measure) == 1);
    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, 1.2f, measure) == 1);
    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, 1.8f, measure) == 2);
    REACTCPP_TEST_ASSERT(reactcpp::text::byte_index_for_x(s, 10.0f, measure) == 4);
}

int main() {
    test_utf8_boundaries();
    test_selection_erase_and_insert();
    test_selection_from_anchor();
    test_undo_history();
    test_word_selection_runs();
    test_byte_index_for_x_monospaced();
    return 0;
}
