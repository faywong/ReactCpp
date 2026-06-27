
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

static void test_canvas_drawio_frame_records() {
    CanvasProps canvas;
    canvas.style.width = 320.0f;
    canvas.style.height = 160.0f;
    canvas.drawio_xml = R"drawio(
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
        return View(root, {Canvas(canvas)});
    };

    SkiaRuntime rt(app, reactcpp::PlatformBridge{}, 800, 600);
    const reactcpp::Frame frame = rt.render_to_frame(340, 180);
    REACTCPP_TEST_ASSERT(frame.picture);
    REACTCPP_TEST_ASSERT(!frame.retained_pictures.empty());
}

int main() {
    test_frame_retains_nested_cached_pictures_across_rerecord();
    test_canvas_drawio_frame_records();
    return 0;
}
