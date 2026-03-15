#include "element_dsl.hpp"

#include <cstdio>
#include <exception>
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
            .value(std::string("ReactCpp GUI, clicked for ") + std::to_string(counter.get()) + " times")
            .text_size(30.0f)
            .text_color(0.12f, 0.15f, 0.24f),

        button()
            .margin(6.0f)
            .size(260.0f, 48.0f)
            .bg(0.25f, 0.52f, 0.93f)
            .label("Test Button")
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

        ,
        text()
            .margin(6.0f)
            .value("Editable InputArea (wrap + scroll + Enter newline):")
            .text_size(18.0f)

        ,
        input_area()
            .margin(6.0f)
            .size(380.0f, 220.0f)
            .bg(1.0f, 1.0f, 1.0f)
            .border_color(0.70f, 0.70f, 0.70f)
            .text_size(16.0f)
            .text_color(0.10f, 0.10f, 0.10f)
            .placeholder("Click to focus; Enter inserts newline; mouse drag selects; wheel scrolls")
            .value(
                "A very long line to test wrapping: The_quick_brown_fox_jumps_over_the_lazy_dog_0123456789_" \
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz\n"
                "Line 2: 中文换行测试：这是一段很长很长的中文文本，用来验证在超过宽度之后是否会自动换行，并且光标、选区依然正确。\n"
                "Line 3: Try selecting across wrapped lines, then press Backspace.\n"
                "Line 4: Scroll with mouse wheel when content exceeds the view height."
            )
    );
}

int main() {
    try {
        return run_skia_app([]() {
            return AppRoot();
        });
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
