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

int main() {
    test_utf8_boundaries();
    test_selection_erase_and_insert();
    test_selection_from_anchor();
    return 0;
}
