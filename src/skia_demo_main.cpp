#include "skia_runtime.hpp"

Element AppRoot() {
    auto counter = use_state<int>(0);

    int next_value = counter.get() + 1;
    counter.set(next_value);

    float t = (counter.get() % 240) / 240.0f;
    float r = t;
    float g = 0.5f;
    float b = 1.0f - t;

    std::vector<Element> children;
    children.push_back(Rect(r, g, b));
    return View(children);
}

int main() {
    return run_skia_app([]() {
        return AppRoot();
    });
}
