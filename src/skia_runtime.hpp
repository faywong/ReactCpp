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

    bool operator==(const ViewProps&) const = default;
};

struct ButtonProps : ViewProps {
    std::string label;
    float text_size{18.0f};
    float text_r{0.1f};
    float text_g{0.1f};
    float text_b{0.1f};

    bool operator==(const ButtonProps&) const = default;
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
