
#include <cmath>

#include "skia_runtime.hpp"

#include "test_util.hpp"

#define REACTCPP_INTERNAL_TESTING 1
#include "skia_runtime.cpp"

static void test_frame_retains_nested_cached_pictures_across_rerecord() {
    std::string text = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\nLine 7\nLine 8\nLine 9\nLine 10";

    auto app = [&] {
        ViewProps root;
        root.style.width = 420.0f;
        root.style.height = 260.0f;
        root.style.padding = 8.0f;

        InputAreaProps area;
        area.style.width = 360.0f;
        area.style.height = 160.0f;
        area.style.padding = 8.0f;
        area.value = text;

        return View(root, {InputArea(area)});
    };

    SkiaRuntime rt(app, reactcpp::PlatformBridge{}, 800, 600);

    const reactcpp::Frame f1 = rt.render_to_frame(420, 260);
    REACTCPP_TEST_ASSERT(f1.picture);
    REACTCPP_TEST_ASSERT(!f1.retained_pictures.empty());

    std::vector<std::uint32_t> ids1;
    ids1.reserve(f1.retained_pictures.size());
    for (const auto& p : f1.retained_pictures) {
        REACTCPP_TEST_ASSERT(p);
        ids1.push_back(p->uniqueID());
    }

    text += "\nLine 11";
    rt.test_set_update_requested(true);
    const reactcpp::Frame f2 = rt.render_to_frame(420, 260);
    REACTCPP_TEST_ASSERT(f2.picture);
    REACTCPP_TEST_ASSERT(!f2.retained_pictures.empty());

    bool any_changed = (f1.retained_pictures.size() != f2.retained_pictures.size());
    if (!any_changed) {
        for (std::size_t i = 0; i < f1.retained_pictures.size(); ++i) {
            if (f1.retained_pictures[i]->uniqueID() != f2.retained_pictures[i]->uniqueID()) {
                any_changed = true;
                break;
            }
        }
    }
    REACTCPP_TEST_ASSERT(any_changed);

    for (std::size_t i = 0; i < f1.retained_pictures.size(); ++i) {
        REACTCPP_TEST_ASSERT(f1.retained_pictures[i]);
        REACTCPP_TEST_ASSERT(f1.retained_pictures[i]->uniqueID() == ids1[i]);
    }
}

static void test_drawio_diagram_frame_records() {
    DrawioDiagramProps diagram;
    diagram.style.width = 320.0f;
    diagram.style.height = 160.0f;
    diagram.drawio_xml = R"drawio(
<mxGraphModel>
  <root>
    <mxCell id="0"/>
    <mxCell id="1" parent="0"/>
    <mxCell id="a" value="A" style="rounded=1;fillColor=#DBEAFE;strokeColor=#2563EB;" vertex="1" parent="1">
      <mxGeometry x="20" y="20" width="80" height="40" as="geometry"/>
    </mxCell>
    <mxCell id="b" value="B" style="ellipse;fillColor=#DCFCE7;strokeColor=#16A34A;" vertex="1" parent="1">
      <mxGeometry x="180" y="20" width="80" height="40" as="geometry"/>
    </mxCell>
    <mxCell id="e" style="strokeColor=#6B7280;" edge="1" parent="1" source="a" target="b">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
  </root>
</mxGraphModel>
)drawio";

    auto app = [&] {
        ViewProps root;
        root.style.width = 340.0f;
        root.style.height = 180.0f;
        return View(root, {DrawioDiagram(diagram)});
    };

    SkiaRuntime rt(app, reactcpp::PlatformBridge{}, 800, 600);
    const reactcpp::Frame frame = rt.render_to_frame(340, 180);
    REACTCPP_TEST_ASSERT(frame.picture);
    REACTCPP_TEST_ASSERT(!frame.retained_pictures.empty());
}

static void test_text_uses_intrinsic_width_inside_stretch_parent() {
    TextProps label;
    label.text = "Short label";
    label.text_size = 18.0f;

    auto app = [&] {
        ViewProps root;
        root.style.width = 420.0f;
        root.style.height = 120.0f;
        root.style.align_items = AlignItems::Stretch;
        return View(root, {Text(label)});
    };

    SkiaRuntime rt(app, reactcpp::PlatformBridge{}, 800, 600);
    const reactcpp::Frame frame = rt.render_to_frame(420, 120);
    REACTCPP_TEST_ASSERT(frame.picture);

    const InstanceNode& root = rt.test_root_instance();
    REACTCPP_TEST_ASSERT(root.children.size() == 1);
    const InstanceNode& text = *root.children[0];

    SkFont font;
    font.setSize(label.text_size);
    font.setTypeface(pick_typeface(g_font_mgr));
    const float measured = font.measureText(label.text.c_str(), label.text.size(), SkTextEncoding::kUTF8);

    REACTCPP_TEST_ASSERT(text.layout.width < 420.0f);
    REACTCPP_TEST_ASSERT(std::abs(text.layout.width - measured) < 1.0f);
}

static void test_context_menu_copies_selected_text_element() {
    TextProps label;
    label.text = "Copy this text";
    label.text_size = 18.0f;

    auto app = [&] {
        ViewProps root;
        root.style.width = 240.0f;
        root.style.height = 80.0f;
        return View(root, {Text(label)});
    };

    reactcpp::PlatformCommandQueue cmds;
    reactcpp::ClipboardRpc clipboard;
    SkiaRuntime rt(app, reactcpp::PlatformBridge{&cmds, &clipboard}, 240, 80);

    const reactcpp::Frame first = rt.render_to_frame(240, 80);
    REACTCPP_TEST_ASSERT(first.picture);

    rt.handle_mouse_button_down(4.0f, 4.0f, 1, SDL_BUTTON_RIGHT);
    const reactcpp::Frame menu_frame = rt.render_to_frame(240, 80);
    REACTCPP_TEST_ASSERT(menu_frame.picture);

    rt.handle_mouse_button_down(18.0f, 18.0f, 1, SDL_BUTTON_LEFT);

    bool found_clipboard = false;
    while (auto cmd = cmds.try_pop()) {
        if (cmd->type == reactcpp::PlatformCmdType::SetClipboardText) {
            REACTCPP_TEST_ASSERT(cmd->text == label.text);
            found_clipboard = true;
            break;
        }
    }
    REACTCPP_TEST_ASSERT(found_clipboard);
}

static void test_data_source_repaint_skips_app_render() {
    auto points = std::make_shared<reactcpp::VectorDataSource<reactcpp::LinePoint>>(
        std::vector<reactcpp::LinePoint>{{0.0, 1.0}, {1.0, 2.0}}
    );
    int render_count = 0;

    auto app = [&] {
        ++render_count;
        ViewProps root;
        root.style.width = 360.0f;
        root.style.height = 180.0f;

        LineChartProps chart;
        chart.style.width = 320.0f;
        chart.style.height = 140.0f;
        chart.points_source = points;
        chart.data_revision = points->revision();
        chart.show_markers = false;

        return View(root, {LineChart(chart)});
    };

    SkiaRuntime rt(app, reactcpp::PlatformBridge{}, 360, 180);
    const reactcpp::Frame first = rt.render_to_frame(360, 180);
    REACTCPP_TEST_ASSERT(first.picture);
    REACTCPP_TEST_ASSERT(render_count == 1);

    const InstanceNode& root_before = rt.test_root_instance();
    REACTCPP_TEST_ASSERT(root_before.children.size() == 1);
    REACTCPP_TEST_ASSERT(!root_before.children[0]->cached_picture);

    points->push_back({2.0, 3.0});
    const reactcpp::Frame second = rt.render_to_frame(360, 180);
    REACTCPP_TEST_ASSERT(second.picture);
    REACTCPP_TEST_ASSERT(render_count == 1);

    const InstanceNode& root_after = rt.test_root_instance();
    REACTCPP_TEST_ASSERT(root_after.children.size() == 1);
    REACTCPP_TEST_ASSERT(!root_after.children[0]->cached_picture);
    const auto& chart = std::get<LineChartProps>(root_after.children[0]->current_vnode.props);
    REACTCPP_TEST_ASSERT(chart.data_revision == points->revision());
}

int main() {
    test_frame_retains_nested_cached_pictures_across_rerecord();
    test_drawio_diagram_frame_records();
    test_text_uses_intrinsic_width_inside_stretch_parent();
    test_context_menu_copies_selected_text_element();
    test_data_source_repaint_skips_app_render();
    return 0;
}
