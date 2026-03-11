#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using TypeId = const void*;

struct Props {
    std::string text;
};

struct Element {
    TypeId type{};
    std::optional<std::string> key;
    Props props;
    std::vector<Element> children;
};

TypeId host_type_view();
TypeId host_type_text();

Element View(const std::vector<Element>& children);
Element Text(const std::string& text);

enum class HookKind : std::uint8_t {
    State
};

struct HookSlot {
    HookKind kind{};
    std::uint32_t generation_tag{0};
    std::any payload;
};

using InstanceId = std::uint64_t;

struct InstanceNode {
    InstanceId id{0};
    TypeId type{};
    Element current_vnode{};
    InstanceNode* parent{nullptr};
    std::vector<std::unique_ptr<InstanceNode>> children;

    std::vector<HookSlot> hooks;
    std::uint32_t hook_cursor{0};
};

struct HookDispatcher {
    InstanceNode* current_instance{nullptr};
    std::uint32_t current_index{0};
};

extern thread_local HookDispatcher g_dispatcher;

template <typename T>
struct StateHandle {
    const T& get() const { return *value_ptr; }

    void set(const T& new_value) const {
        if (!instance || !value_ptr) return;
        if (*value_ptr == new_value) return;
        *value_ptr = new_value;
    }

    InstanceNode* instance{nullptr};
    std::uint32_t hook_index{0};
    std::uint32_t generation{0};
    T* value_ptr{nullptr};
};

template <typename T>
StateHandle<T> use_state(const T& initial) {
    HookDispatcher& d = g_dispatcher;
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

struct Runtime;

using ComponentFunc = Element(*)(const Props&);
using AppRenderFunc = std::function<Element()>;

struct Runtime {
    AppRenderFunc app_render;
    std::unique_ptr<InstanceNode> root_instance;
    InstanceId next_id{1};

    explicit Runtime(AppRenderFunc fn);

    InstanceNode* create_instance(const Element& vnode, InstanceNode* parent);
    void reconcile(InstanceNode* inst, const Element& vnode);
    void reconcile_children(InstanceNode* inst, const std::vector<Element>& new_children);

    void render_to_console();
};

int run_app(const AppRenderFunc& app);
