#include "runtime.hpp"

Element AppRoot() {
    std::vector<Element> children;
    children.push_back(Text("Hello from console runtime"));
    return View(children);
}

int main() {
    return run_app([]() {
        return AppRoot();
    });
}
