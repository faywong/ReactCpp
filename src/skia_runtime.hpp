#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class SkCanvas;

using TypeId = const void*;

struct Props {
    float r{0.2f};
    float g{0.6f};
    float b{0.9f};
};

struct Element {
    TypeId type{};
    Props props;
    std::vector<Element> children;
};

TypeId host_type_view();
TypeId host_type_rect();

Element View(const std::vector<Element>& children);
Element Rect(float r, float g, float b);

enum class HookKind : std::uint8_t {
    State
};

struct HookSlot {
    HookKind kind{};
    std::uint32_t generation_tag{0};
    std::any payload;
};

struct InstanceNode {
    TypeId type{};
    Element current_vnode{};
    std::vector<HookSlot> hooks;
};

struct HookDispatcher {
    InstanceNode* current_instance{nullptr};
    std::uint32_t current_index{0};
};

extern thread_local HookDispatcher g_skia_dispatcher;

template <typename T>
struct StateHandle {
    const T& get() const { return *value_ptr; }

    void set(const T& new_value) const {
        if (!instance || !value_ptr) return;
        *value_ptr = new_value;
    }

    InstanceNode* instance{nullptr};
    std::uint32_t hook_index{0};
    std::uint32_t generation{0};
    T* value_ptr{nullptr};
};

template <typename T>
StateHandle<T> use_state(const T& initial) {
    HookDispatcher& d = g_skia_dispatcher;
    if (!d.current_instance) {
        throw std::runtime_error("use_state called outside of render");
    }
    InstanceNode* inst = d.current_instance;
    const std::uint32_t index = d.current_index++;

    if (index >= inst->hooks.size()) {
        inst->hooks.resize(index + 1);
    }

    HookSlot& slot = inst->hooks[index];

    if (slot.kind != HookKind::State) {
        slot.kind = HookKind::State;
        slot.generation_tag++;
        slot.payload = initial;
    }

    auto* value_ptr = std::any_cast<T>(&slot.payload);
    if (!value_ptr) {
        throw std::runtime_error("use_state type mismatch");
    }

    StateHandle<T> handle;
    handle.instance = inst;
    handle.hook_index = index;
    handle.generation = slot.generation_tag;
    handle.value_ptr = value_ptr;
    return handle;
}

using AppRenderFunc = std::function<Element()>;

void draw_element_tree(const Element& el, SkCanvas* canvas, int width, int height);

int run_skia_app(const AppRenderFunc& app);
