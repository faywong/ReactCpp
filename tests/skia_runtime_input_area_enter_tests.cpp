#include <cassert>
#include <string>

#include "skia_runtime.hpp"

#define private public
#define protected public
#include "skia_runtime.cpp"
#undef protected
#undef private

static InstanceNode make_focused_input_area_node(const std::string& initial_value, float w, float h) {
    InputAreaProps props;
    props.text_size = 18.0f;

    InstanceNode node;
    node.current_vnode = InputArea(props);
    node.type = node.current_vnode.type;
    node.layout.width = w;
    node.layout.height = h;
    node.focused = true;

    node.editable_state = InstanceNode::EditableTextState{
        .value = reactcpp::text::TextBuffer(initial_value),
        .cursor = initial_value.size(),
        .sel_start = 0,
        .sel_end = 0,
        .sel_anchor = initial_value.size(),
        .has_selection = false,
        .focused = true,
        .scroll_y = 0.0f,
        .preedit = std::string(),
        .preedit_start = -1,
        .preedit_length = -1,
        .undo = reactcpp::text::UndoHistory{100},
    };
    return node;
}

static InstanceNode make_focused_input_node(const std::string& initial_value) {
    InputProps props;
    props.text_size = 18.0f;

    InstanceNode node;
    node.current_vnode = Input(props);
    node.type = node.current_vnode.type;
    node.layout.width = 200.0f;
    node.layout.height = 30.0f;
    node.focused = true;

    node.editable_state = InstanceNode::EditableTextState{
        .value = reactcpp::text::TextBuffer(initial_value),
        .cursor = initial_value.size(),
        .sel_start = 0,
        .sel_end = 0,
        .sel_anchor = initial_value.size(),
        .has_selection = false,
        .focused = true,
        .scroll_y = 0.0f,
        .preedit = std::string(),
        .preedit_start = -1,
        .preedit_length = -1,
        .undo = reactcpp::text::UndoHistory{100},
    };
    return node;
}

static void test_enter_inserts_newline_in_input_area_and_requests_update() {
    SkiaRuntime rt([] { return Element{}; }, nullptr, 800, 600);

    InstanceNode node = make_focused_input_area_node("ab", 300.0f, 20.0f);
    node.editable_state->has_selection = true;
    node.editable_state->sel_start = 0;
    node.editable_state->sel_end = 1;
    node.editable_state->sel_anchor = 0;
    node.editable_state->cursor = 1;

    rt.focused_node_ = &node;
    rt.focused_input_ = &node;
    rt.update_requested_ = false;

    const std::size_t undo_before = node.editable_state->undo.size();
    rt.handle_key_down(SDLK_RETURN, static_cast<SDL_Keymod>(0), false);

    assert(node.editable_state->value.to_string() == std::string("\n") + "b");
    assert(node.editable_state->cursor == 1);
    assert(!node.editable_state->has_selection);
    assert(node.editable_state->sel_anchor == node.editable_state->cursor);
    assert(node.editable_state->undo.size() == undo_before + 1);
    assert(node.editable_state->preedit.empty());
    assert(node.dirty);
    assert(rt.update_requested_);
    assert(node.editable_state->scroll_y > 0.0f);
}

static void test_enter_is_ignored_when_preedit_is_non_empty() {
    SkiaRuntime rt([] { return Element{}; }, nullptr, 800, 600);

    InstanceNode node = make_focused_input_area_node("a", 300.0f, 20.0f);
    node.editable_state->preedit = "x";
    node.editable_state->preedit_start = 0;
    node.editable_state->preedit_length = 1;

    rt.focused_node_ = &node;
    rt.focused_input_ = &node;
    rt.update_requested_ = false;

    const std::size_t undo_before = node.editable_state->undo.size();
    rt.handle_key_down(SDLK_RETURN, static_cast<SDL_Keymod>(0), false);

    assert(node.editable_state->value.to_string() == "a");
    assert(node.editable_state->undo.size() == undo_before);
    assert(node.editable_state->preedit == "x");
    assert(!rt.update_requested_);
}

static void test_enter_does_not_insert_newline_in_single_line_input() {
    SkiaRuntime rt([] { return Element{}; }, nullptr, 800, 600);

    InstanceNode node = make_focused_input_node("a");

    rt.focused_node_ = &node;
    rt.focused_input_ = &node;
    rt.update_requested_ = false;

    const std::size_t undo_before = node.editable_state->undo.size();
    rt.handle_key_down(SDLK_RETURN, static_cast<SDL_Keymod>(0), false);

    assert(node.editable_state->value.to_string() == "a");
    assert(node.editable_state->undo.size() == undo_before);
    assert(!rt.update_requested_);
}

int main() {
    test_enter_inserts_newline_in_input_area_and_requests_update();
    test_enter_is_ignored_when_preedit_is_non_empty();
    test_enter_does_not_insert_newline_in_single_line_input();
    return 0;
}
