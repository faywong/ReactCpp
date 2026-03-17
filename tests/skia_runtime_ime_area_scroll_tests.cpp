
#include "skia_runtime.hpp"

#include "test_util.hpp"

#define REACTCPP_INTERNAL_TESTING 1
#include "skia_runtime.cpp"

static InstanceNode make_focused_input_area_node(const std::string& initial_value, float w, float h) {
    InputAreaProps props;
    props.text_size = 18.0f;
    props.style.padding = 8.0f;

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

static int drain_set_text_input_area_cmds(reactcpp::PlatformCommandQueue& q) {
    int count = 0;
    while (true) {
        auto opt = q.try_pop();
        if (!opt) break;
        if (opt->type == reactcpp::PlatformCmdType::SetTextInputArea) {
            ++count;
        }
    }
    return count;
}

static void test_input_area_scroll_without_composition_does_not_spam_ime_area_updates() {
    reactcpp::PlatformCommandQueue platform_cmds;
    reactcpp::ClipboardRpc clipboard;
    reactcpp::PlatformBridge platform{&platform_cmds, &clipboard};

    SkiaRuntime rt([] { return Element{}; }, platform, 800, 600);

    std::string text;
    for (int i = 0; i < 40; ++i) {
        text += "Line\n";
    }
    InstanceNode node = make_focused_input_area_node(text, 300.0f, 60.0f);
    rt.test_set_focused_input(&node);

    {
        SkPictureRecorder rec;
        SkCanvas* c = rec.beginRecording(SkRect::MakeWH(800.0f, 600.0f));
        rt.draw(c, 800, 600);
        (void)rec.finishRecordingAsPicture();
    }

    const int first = drain_set_text_input_area_cmds(platform_cmds);
    REACTCPP_TEST_ASSERT(first == 1);

    node.editable_state->scroll_y = 120.0f;
    {
        SkPictureRecorder rec;
        SkCanvas* c = rec.beginRecording(SkRect::MakeWH(800.0f, 600.0f));
        rt.draw(c, 800, 600);
        (void)rec.finishRecordingAsPicture();
    }
    const int after_scroll = drain_set_text_input_area_cmds(platform_cmds);
    REACTCPP_TEST_ASSERT(after_scroll == 0);

    node.editable_state->preedit = "x";
    {
        SkPictureRecorder rec;
        SkCanvas* c = rec.beginRecording(SkRect::MakeWH(800.0f, 600.0f));
        rt.draw(c, 800, 600);
        (void)rec.finishRecordingAsPicture();
    }
    const int during_comp = drain_set_text_input_area_cmds(platform_cmds);
    REACTCPP_TEST_ASSERT(during_comp == 1);
}

int main() {
    test_input_area_scroll_without_composition_does_not_spam_ime_area_updates();
    return 0;
}
