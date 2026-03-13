#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <cstddef>

class SkCanvas;
class SkPicture;

using TypeId = const void*;

struct LayoutRect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};

    bool operator==(const LayoutRect&) const = default;
};

enum class FlexDirection : std::uint8_t {
    Row,
    Column
};

enum class JustifyContent : std::uint8_t {
    FlexStart,
    Center,
    FlexEnd,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class AlignItems : std::uint8_t {
    Stretch,
    FlexStart,
    Center,
    FlexEnd
};

struct FlexStyle {
    FlexDirection flex_direction{FlexDirection::Column};
    JustifyContent justify_content{JustifyContent::FlexStart};
    AlignItems align_items{AlignItems::Stretch};

    float flex_grow{0.0f};
    float flex_shrink{0.0f};

    float padding{0.0f};
    float margin{0.0f};

    std::optional<float> width;
    std::optional<float> height;

    bool operator==(const FlexStyle&) const = default;
};

struct ViewProps {
    FlexStyle style{};
    float bg_r{1.0f};
    float bg_g{1.0f};
    float bg_b{1.0f};
    float bg_a{1.0f};
    std::shared_ptr<const std::function<void()>> on_click;

    bool operator==(const ViewProps& other) const {
        return style == other.style
            && bg_r == other.bg_r
            && bg_g == other.bg_g
            && bg_b == other.bg_b
            && bg_a == other.bg_a;
    }
};

struct ButtonProps : ViewProps {
    std::string label;
    float text_size{18.0f};
    float text_r{0.1f};
    float text_g{0.1f};
    float text_b{0.1f};

    bool operator==(const ButtonProps& other) const {
        return static_cast<const ViewProps&>(*this) == static_cast<const ViewProps&>(other)
            && label == other.label
            && text_size == other.text_size
            && text_r == other.text_r
            && text_g == other.text_g
            && text_b == other.text_b;
    }
};

struct TextProps : ViewProps {
    std::string text;
    float text_size{18.0f};
    float text_r{0.15f};
    float text_g{0.15f};
    float text_b{0.15f};

    bool operator==(const TextProps&) const = default;
};

struct InputProps : ViewProps {
    std::string value;
    std::string placeholder;
    float text_size{18.0f};
    float text_r{0.1f};
    float text_g{0.1f};
    float text_b{0.1f};
    float border_r{0.7f};
    float border_g{0.7f};
    float border_b{0.7f};

    bool operator==(const InputProps&) const = default;
};

using ElementProps = std::variant<ViewProps, ButtonProps, TextProps, InputProps>;

struct Element {
    TypeId type{};
    ElementProps props{ViewProps{}};
    std::vector<Element> children;
    bool dirty{false};

    bool operator==(const Element& other) const {
        return type == other.type && props == other.props && children == other.children;
    }
};

TypeId host_type_view();
TypeId host_type_button();
TypeId host_type_text();
TypeId host_type_input();

Element View(const ViewProps& props, std::vector<Element> children = {});
Element Button(const ButtonProps& props);
Element Text(const TextProps& props);
Element Input(const InputProps& props);

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
    InstanceNode* parent{nullptr};
    std::vector<std::unique_ptr<InstanceNode>> children;
    std::vector<HookSlot> hooks;
    bool dirty{true};
    std::shared_ptr<SkPicture> cached_picture;

    LayoutRect layout{};

    std::uintptr_t yoga_node_handle{0};

    struct InputState {
        std::string value;
        std::size_t cursor{0};
        bool focused{false};
    };
    std::optional<InputState> input_state;
};

struct HookDispatcher {
    InstanceNode* current_instance{nullptr};
    std::uint32_t current_index{0};

    void (*request_update)(void* ctx){nullptr};
    void* request_update_ctx{nullptr};
};

extern thread_local HookDispatcher g_skia_dispatcher;

template <typename T>
struct StateHandle {
    const T& get() const {
        if (!instance) {
            throw std::runtime_error("StateHandle::get called on null instance");
        }
        if (hook_index >= instance->hooks.size()) {
            throw std::runtime_error("StateHandle::get hook index out of range");
        }

        HookSlot& slot = instance->hooks[hook_index];
        if (slot.kind != HookKind::State || slot.generation_tag != generation) {
            throw std::runtime_error("StateHandle::get stale handle");
        }

        auto* ptr = std::any_cast<T>(&slot.payload);
        if (!ptr) {
            throw std::runtime_error("StateHandle::get type mismatch");
        }
        return *ptr;
    }

    void set(const T& new_value) const {
        if (!instance) return;
        if (hook_index >= instance->hooks.size()) return;
        HookSlot& slot = instance->hooks[hook_index];
        if (slot.kind != HookKind::State || slot.generation_tag != generation) {
            return;
        }

        auto* ptr = std::any_cast<T>(&slot.payload);
        if (!ptr) {
            return;
        }

        *ptr = new_value;

        HookDispatcher& d = g_skia_dispatcher;
        if (d.request_update) {
            d.request_update(d.request_update_ctx);
        }
    }

    template <typename Fn>
    void update(Fn&& fn) const {
        if (!instance) return;
        if (hook_index >= instance->hooks.size()) return;
        HookSlot& slot = instance->hooks[hook_index];
        if (slot.kind != HookKind::State || slot.generation_tag != generation) {
            return;
        }

        auto* ptr = std::any_cast<T>(&slot.payload);
        if (!ptr) {
            return;
        }

        *ptr = static_cast<T>(fn(*ptr));

        HookDispatcher& d = g_skia_dispatcher;
        if (d.request_update) {
            d.request_update(d.request_update_ctx);
        }
    }

    InstanceNode* instance{nullptr};
    std::uint32_t hook_index{0};
    std::uint32_t generation{0};
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

    if (slot.kind != HookKind::State || !slot.payload.has_value()) {
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
    return handle;
}

using AppRenderFunc = std::function<Element()>;

int run_skia_app(const AppRenderFunc& app);
