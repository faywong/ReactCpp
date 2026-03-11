#include "skia_runtime.hpp"

Element AppRoot() {
    ViewProps root_props;
    root_props.x = 0.0f;
    root_props.y = 0.0f;
    root_props.width = 800.0f;
    root_props.height = 600.0f;
    root_props.bg_r = 0.95f;
    root_props.bg_g = 0.96f;
    root_props.bg_b = 0.98f;

    TextProps title;
    title.x = 32.0f;
    title.y = 32.0f;
    title.width = 700.0f;
    title.height = 40.0f;
    title.text = "Skia Reactive UI Runtime";
    title.text_size = 30.0f;
    title.text_r = 0.12f;
    title.text_g = 0.15f;
    title.text_b = 0.24f;

    ButtonProps button;
    button.x = 32.0f;
    button.y = 100.0f;
    button.width = 240.0f;
    button.height = 48.0f;
    button.bg_r = 0.25f;
    button.bg_g = 0.52f;
    button.bg_b = 0.93f;
    button.label = "Skia Button";
    button.text_size = 20.0f;
    button.text_r = 1.0f;
    button.text_g = 1.0f;
    button.text_b = 1.0f;

    TextProps label;
    label.x = 32.0f;
    label.y = 180.0f;
    label.width = 320.0f;
    label.height = 30.0f;
    label.text = "Editable Input:";
    label.text_size = 18.0f;

    InputProps input;
    input.x = 32.0f;
    input.y = 216.0f;
    input.width = 360.0f;
    input.height = 44.0f;
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
