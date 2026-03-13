#include "element_dsl.hpp"

#include <memory>
#include <string>

using namespace reactcpp::ui;

Element AppRoot() {
    auto counter = use_state<int>(0);

    return view([](ViewProps& p) {
        p.style.flex_direction = FlexDirection::Column;
        p.style.justify_content = JustifyContent::FlexStart;
        p.style.align_items = AlignItems::Stretch;
        p.style.padding = 24.0f;
        p.bg_r = 0.95f;
        p.bg_g = 0.96f;
        p.bg_b = 0.98f;
    },
        text([&](TextProps& p) {
            p.style.margin = 6.0f;
            p.text = std::string("Skia Reactive UI Runtime: ") + std::to_string(counter.get());
            p.text_size = 30.0f;
            p.text_r = 0.12f;
            p.text_g = 0.15f;
            p.text_b = 0.24f;
        }),
        button([&](ButtonProps& p) {
            p.style.margin = 6.0f;
            p.style.width = 260.0f;
            p.style.height = 48.0f;
            p.bg_r = 0.25f;
            p.bg_g = 0.52f;
            p.bg_b = 0.93f;
            p.label = "Skia Button";
            p.text_size = 20.0f;
            p.text_r = 1.0f;
            p.text_g = 1.0f;
            p.text_b = 1.0f;
            p.on_click = std::make_shared<std::function<void()>>([counter]() {
                counter.update([](int v) {
                    return v + 1;
                });
            });
        }),
        text([](TextProps& p) {
            p.style.margin = 6.0f;
            p.text = "Editable Input:";
            p.text_size = 18.0f;
        }),
        input([](InputProps& p) {
            p.style.margin = 6.0f;
            p.style.width = 380.0f;
            p.style.height = 44.0f;
            p.bg_r = 1.0f;
            p.bg_g = 1.0f;
            p.bg_b = 1.0f;
            p.placeholder = "Click to focus, type text, backspace works";
            p.value = "";
        })
    );
}

int main() {
    return run_skia_app([]() {
        return AppRoot();
    });
}
