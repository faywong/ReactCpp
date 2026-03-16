
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

int main() {
    test_frame_retains_nested_cached_pictures_across_rerecord();
    return 0;
}
