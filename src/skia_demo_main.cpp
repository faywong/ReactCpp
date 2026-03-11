#include "skia_runtime.hpp"

Element AppRoot() {
    ViewProps root_props;
    root_props.style.flex_direction = FlexDirection::Column;
    root_props.style.justify_content = JustifyContent::FlexStart;
    root_props.style.align_items = AlignItems::Stretch;
    root_props.style.padding = 24.0f;
    root_props.bg_r = 0.95f;
    root_props.bg_g = 0.96f;
    root_props.bg_b = 0.98f;

    TextProps title;
    title.style.margin = 6.0f;
    title.text = "Skia Reactive UI Runtime";
    title.text_size = 30.0f;
    title.text_r = 0.12f;
    title.text_g = 0.15f;
    title.text_b = 0.24f;

    ButtonProps button;
    button.style.margin = 6.0f;
    button.style.width = 260.0f;
    button.style.height = 48.0f;
    button.bg_r = 0.25f;
    button.bg_g = 0.52f;
    button.bg_b = 0.93f;
    button.label = "Skia Button";
    button.text_size = 20.0f;
    button.text_r = 1.0f;
    button.text_g = 1.0f;
    button.text_b = 1.0f;

    TextProps label;
    label.style.margin = 6.0f;
    label.text = "Editable Input:";
    label.text_size = 18.0f;

    InputProps input;
    input.style.margin = 6.0f;
    input.style.width = 380.0f;
    input.style.height = 44.0f;
    input.bg_r = 1.0f;
    input.bg_g = 1.0f;
    input.bg_b = 1.0f;
    input.placeholder = "Click to focus, type text, backspace works";
    input.value = "";

    return View(root_props, {
        Text(title),
        Button(button),
        Text(label),
        Input(input)
    });
}

int main() {
    return run_skia_app([]() {
        return AppRoot();
    });
}
