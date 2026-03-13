#include "element_dsl.hpp"

#include <string>

using namespace reactcpp::ui;

Element AppRoot() {
    auto counter = use_state<int>(0);

    return view()
        .column()
        .justify(JustifyContent::FlexStart)
        .align(AlignItems::Stretch)
        .padding(24.0f)
        .bg(0.95f, 0.96f, 0.98f)
    (
        text()
            .margin(6.0f)
            .value(std::string("Skia Reactive UI Runtime: ") + std::to_string(counter.get()))
            .text_size(30.0f)
            .text_color(0.12f, 0.15f, 0.24f),

        button()
            .margin(6.0f)
            .size(260.0f, 48.0f)
            .bg(0.25f, 0.52f, 0.93f)
            .label("Skia Button")
            .text_size(20.0f)
            .text_color(1.0f, 1.0f, 1.0f)
            .on_click([counter]() {
                counter.update([](int v) {
                    return v + 1;
                });
            }),

        text()
            .margin(6.0f)
            .value("Editable Input:")
            .text_size(18.0f),

        input()
            .margin(6.0f)
            .size(380.0f, 44.0f)
            .bg(1.0f, 1.0f, 1.0f)
            .placeholder("Click to focus, type text, backspace works")
            .value("")
    );
}

int main() {
    return run_skia_app([]() {
        return AppRoot();
    });
}
