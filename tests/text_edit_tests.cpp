#include <cassert>
#include <cstddef>
#include <string>

#include "text_edit.hpp"

static void test_utf8_boundaries() {
    const std::string s = std::string("a") + "\xF0\x9F\x98\x8A" + "b";
    assert(s.size() == 6);

    std::size_t c = 0;
    c = reactcpp::text::utf8_next_boundary(s, c);
    assert(c == 1);
    c = reactcpp::text::utf8_next_boundary(s, c);
    assert(c == 5);
    c = reactcpp::text::utf8_next_boundary(s, c);
    assert(c == 6);

    c = reactcpp::text::utf8_prev_boundary(s, c);
    assert(c == 5);
    c = reactcpp::text::utf8_prev_boundary(s, c);
    assert(c == 1);
    c = reactcpp::text::utf8_prev_boundary(s, c);
    assert(c == 0);
}

static void test_selection_erase_and_insert() {
    reactcpp::text::Selection sel;
    sel.active = true;
    sel.start = 1;
    sel.end = 4;

    std::string v = "hello";
    std::size_t cursor = 5;

    reactcpp::text::erase_selection(v, cursor, sel);
    assert(v == "ho");
    assert(cursor == 1);
    assert(!sel.active);

    sel.active = true;
    sel.start = 1;
    sel.end = 2;
    cursor = 2;

    reactcpp::text::insert_text(v, cursor, sel, "X");
    assert(v == "hX");
    assert(cursor == 2);
    assert(!sel.active);
}

static void test_selection_from_anchor() {
    {
        const auto s = reactcpp::text::selection_from_anchor(2, 5, 10);
        assert(s.active);
        assert(s.start == 2);
        assert(s.end == 5);
    }
    {
        const auto s = reactcpp::text::selection_from_anchor(5, 2, 10);
        assert(s.active);
        assert(s.start == 2);
        assert(s.end == 5);
    }
    {
        const auto s = reactcpp::text::selection_from_anchor(3, 3, 10);
        assert(!s.active);
        assert(s.start == 0);
        assert(s.end == 0);
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

    assert(h.size() == 3);

    auto u = h.pop();
    assert(u.has_value());
    assert(u->value == "abcd");

    u = h.pop();
    assert(u.has_value());
    assert(u->value == "abc");
}

static void test_word_selection_runs() {
    const std::string s = std::string("hello") + "\xE4\xB8\x96\xE7\x95\x8C" + ",foo_bar123 " + "\xE4\xB8\xAD\xE6\x96\x87";

    {
        const auto sel = reactcpp::text::word_selection_at(s, 1);
        assert(sel.active);
        assert(sel.start == 0);
        assert(sel.end == 5);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 5);
        assert(sel.active);
        assert(sel.start == 5);
        assert(sel.end == 11);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 11);
        assert(sel.active);
        assert(sel.start == 11);
        assert(sel.end == 12);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 13);
        assert(sel.active);
        assert(sel.start == 12);
        assert(sel.end == 22);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 22);
        assert(!sel.active);
        assert(sel.start == 0);
        assert(sel.end == 0);
    }

    {
        const auto sel = reactcpp::text::word_selection_at(s, 23);
        assert(sel.active);
        assert(sel.end - sel.start == 6);
    }
}

static void test_byte_index_for_x_monospaced() {
    const std::string s = "abcd";
    auto measure = [](std::size_t bytes) { return static_cast<float>(bytes); };

    assert(reactcpp::text::byte_index_for_x(s, -1.0f, measure) == 0);
    assert(reactcpp::text::byte_index_for_x(s, 0.0f, measure) == 0);
    assert(reactcpp::text::byte_index_for_x(s, 0.49f, measure) == 0);
    assert(reactcpp::text::byte_index_for_x(s, 0.51f, measure) == 1);
    assert(reactcpp::text::byte_index_for_x(s, 1.2f, measure) == 1);
    assert(reactcpp::text::byte_index_for_x(s, 1.8f, measure) == 2);
    assert(reactcpp::text::byte_index_for_x(s, 10.0f, measure) == 4);
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
