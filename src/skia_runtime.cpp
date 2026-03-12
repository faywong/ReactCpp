#include "skia_runtime.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "yoga_shim.hpp"

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkFont.h"
#include "core/SkFontMetrics.h"
#include "core/SkFontMgr.h"
#include "core/SkFontStyle.h"
#include "core/SkImageInfo.h"
#include "core/SkPaint.h"
#include "core/SkPicture.h"
#include "core/SkPictureRecorder.h"
#include "core/SkRect.h"
#include "core/SkRefCnt.h"
#include "core/SkSurface.h"
#include "core/SkTypeface.h"

#include "ports/SkFontMgr_fontconfig.h"
#include "ports/SkFontScanner_FreeType.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define REACTCPP_SDL_EVENT_QUIT SDL_EVENT_QUIT
#define REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN SDL_EVENT_MOUSE_BUTTON_DOWN
#define REACTCPP_SDL_EVENT_TEXT_INPUT SDL_EVENT_TEXT_INPUT
#define REACTCPP_SDL_EVENT_KEY_DOWN SDL_EVENT_KEY_DOWN

thread_local HookDispatcher g_skia_dispatcher;

namespace {

struct DrawContext {
    int surface_width{0};
    int surface_height{0};
    sk_sp<SkFontMgr> font_mgr;
};

thread_local sk_sp<SkFontMgr> g_font_mgr;

static sk_sp<SkTypeface> pick_typeface(const sk_sp<SkFontMgr>& mgr) {
    if (!mgr) return nullptr;
    sk_sp<SkTypeface> tf = mgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    if (tf) return tf;
    tf = mgr->matchFamilyStyle("Noto Sans", SkFontStyle::Normal());
    if (tf) return tf;
    return mgr->matchFamilyStyle("DejaVu Sans", SkFontStyle::Normal());
}

static const ViewProps& props_as_view_ref(const Element& el) {
    return std::visit([](const auto& props) -> const ViewProps& {
        return static_cast<const ViewProps&>(props);
    }, el.props);
}

static ViewProps& props_as_view_mut(Element& el) {
    return std::visit([](auto& props) -> ViewProps& {
        return static_cast<ViewProps&>(props);
    }, el.props);
}

static LayoutRect layout_for_node(const InstanceNode& node) {
    if (node.layout.width <= 0.0f || node.layout.height <= 0.0f) {
        const ViewProps& p = props_as_view_ref(node.current_vnode);
        LayoutRect r;
        r.x = 0.0f;
        r.y = 0.0f;
        r.width = p.style.width.value_or(1.0f);
        r.height = p.style.height.value_or(1.0f);
        return r;
    }
    return node.layout;
}

static SkColor make_color(float r, float g, float b, float a = 1.0f) {
    const auto to_u8 = [](float v) -> U8CPU {
        const float clamped = std::clamp(v, 0.0f, 1.0f);
        return static_cast<U8CPU>(clamped * 255.0f);
    };
    return SkColorSetARGB(to_u8(a), to_u8(r), to_u8(g), to_u8(b));
}

static SkScalar baseline_for_centered_text(const SkFont& font, SkScalar box_height) {
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    const SkScalar text_height = metrics.fDescent - metrics.fAscent;
    return (box_height - text_height) * 0.5f - metrics.fAscent;
}

class ElementRenderer {
public:
    virtual ~ElementRenderer() = default;
    virtual void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const = 0;
};

class ViewRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext&) const override {
        const auto& props = std::get<ViewProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRect(SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height), fill);
    }
};

class ButtonRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<ButtonProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height);

        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRoundRect(bounds, 8.0f, 8.0f, fill);

        const float text_x = 8.0f;
        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));
        SkPaint paint;
        paint.setColor(make_color(props.text_r, props.text_g, props.text_b));
        const float text_y = baseline_for_centered_text(font, r.height);
        (void)ctx;
        canvas->drawString(props.label.c_str(), text_x, text_y, font, paint);
    }
};

class TextRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<TextProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        (void)ctx;
        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));
        SkPaint paint;
        paint.setColor(make_color(props.text_r, props.text_g, props.text_b));
        const float text_y = baseline_for_centered_text(font, r.height > 0.0f ? r.height : props.text_size * 1.2f);
        canvas->drawString(props.text.c_str(), 0.0f, text_y, font, paint);
    }
};

class InputRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<InputProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);

        const std::string value = node.input_state ? node.input_state->value : props.value;
        const std::size_t cursor = node.input_state ? node.input_state->cursor : value.size();
        const bool focused = node.input_state && node.input_state->focused;

        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height);
        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRoundRect(bounds, 6.0f, 6.0f, fill);

        SkPaint border;
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(2.0f);
        border.setColor(focused
            ? make_color(0.2f, 0.45f, 0.95f)
            : make_color(props.border_r, props.border_g, props.border_b));
        canvas->drawRoundRect(bounds, 6.0f, 6.0f, border);

        const bool showing_placeholder = value.empty() && !props.placeholder.empty();
        const std::string text = showing_placeholder ? props.placeholder : value;
        const SkColor text_color = showing_placeholder
            ? make_color(0.55f, 0.55f, 0.55f)
            : make_color(props.text_r, props.text_g, props.text_b);

        const float text_x = 8.0f;
        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));
        SkPaint paint;
        paint.setColor(text_color);
        const float text_y = baseline_for_centered_text(font, r.height);
        (void)ctx;
        canvas->drawString(text.c_str(), text_x, text_y, font, paint);

        if (focused) {
            const std::string left_text = value.substr(0, std::min(cursor, value.size()));
            const float cursor_x = text_x + font.measureText(left_text.c_str(), left_text.size(), SkTextEncoding::kUTF8);
            SkPaint caret;
            caret.setColor(make_color(0.2f, 0.2f, 0.2f));
            caret.setStrokeWidth(1.5f);
            canvas->drawLine(cursor_x, 8.0f, cursor_x, r.height - 8.0f, caret);
        }
    }
};

static const ElementRenderer& renderer_for(TypeId type) {
    static ViewRenderer view_renderer;
    static ButtonRenderer button_renderer;
    static TextRenderer text_renderer;
    static InputRenderer input_renderer;

    if (type == host_type_view()) return view_renderer;
    if (type == host_type_button()) return button_renderer;
    if (type == host_type_text()) return text_renderer;
    if (type == host_type_input()) return input_renderer;
    return view_renderer;
}

static SkRect node_bounds(const LayoutRect& r, int surface_width, int surface_height) {
    const float w = std::max(r.width, 1.0f);
    const float h = std::max(r.height, 1.0f);
    const float x = std::clamp(r.x, -10000.0f, static_cast<float>(surface_width) + 10000.0f);
    const float y = std::clamp(r.y, -10000.0f, static_cast<float>(surface_height) + 10000.0f);
    return SkRect::MakeXYWH(x, y, w, h);
}

static YGFlexDirection to_yoga(FlexDirection v) {
    return v == FlexDirection::Row ? YGFlexDirectionRow : YGFlexDirectionColumn;
}

static YGJustify to_yoga(JustifyContent v) {
    switch (v) {
    case JustifyContent::FlexStart: return YGJustifyFlexStart;
    case JustifyContent::Center: return YGJustifyCenter;
    case JustifyContent::FlexEnd: return YGJustifyFlexEnd;
    case JustifyContent::SpaceBetween: return YGJustifySpaceBetween;
    case JustifyContent::SpaceAround: return YGJustifySpaceAround;
    case JustifyContent::SpaceEvenly: return YGJustifySpaceEvenly;
    }
    return YGJustifyFlexStart;
}

static YGAlign to_yoga(AlignItems v) {
    switch (v) {
    case AlignItems::Stretch: return YGAlignStretch;
    case AlignItems::FlexStart: return YGAlignFlexStart;
    case AlignItems::Center: return YGAlignCenter;
    case AlignItems::FlexEnd: return YGAlignFlexEnd;
    }
    return YGAlignStretch;
}

static void apply_style(YGNodeRef node, const FlexStyle& style) {
    YGNodeStyleSetFlexDirection(node, to_yoga(style.flex_direction));
    YGNodeStyleSetJustifyContent(node, to_yoga(style.justify_content));
    YGNodeStyleSetAlignItems(node, to_yoga(style.align_items));

    YGNodeStyleSetFlexGrow(node, style.flex_grow);
    YGNodeStyleSetFlexShrink(node, style.flex_shrink);

    YGNodeStyleSetPadding(node, YGEdgeAll, style.padding);
    YGNodeStyleSetMargin(node, YGEdgeAll, style.margin);

    if (style.width) {
        YGNodeStyleSetWidth(node, *style.width);
    } else {
        YGNodeStyleSetWidthAuto(node);
    }

    if (style.height) {
        YGNodeStyleSetHeight(node, *style.height);
    } else {
        YGNodeStyleSetHeightAuto(node);
    }
}

static YGSize measure_text_node(
    YGNodeConstRef yoga_node,
    float width,
    YGMeasureMode width_mode,
    float height,
    YGMeasureMode height_mode
) {
    (void)height;
    (void)height_mode;

    auto* inst = static_cast<const InstanceNode*>(YGNodeGetContext(yoga_node));
    if (!inst) {
        return YGSize{0.0f, 0.0f};
    }

    float font_size = 16.0f;
    std::string text;
    if (inst->type == host_type_text()) {
        const auto& props = std::get<TextProps>(inst->current_vnode.props);
        font_size = props.text_size;
        text = props.text;
    } else if (inst->type == host_type_button()) {
        const auto& props = std::get<ButtonProps>(inst->current_vnode.props);
        font_size = props.text_size;
        text = props.label;
    } else if (inst->type == host_type_input()) {
        const auto& props = std::get<InputProps>(inst->current_vnode.props);
        font_size = props.text_size;
        text = props.placeholder;
    }

    SkFont font;
    font.setSize(font_size);
    font.setTypeface(pick_typeface(g_font_mgr));
    const float measured = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);
    const float padding_x = 16.0f;
    float out_w = measured + padding_x;
    if (width_mode == YGMeasureModeExactly) {
        out_w = width;
    } else if (width_mode == YGMeasureModeAtMost) {
        out_w = std::min(out_w, width);
    }

    const float out_h = font_size * 1.4f;
    return YGSize{out_w, out_h};
}

static YGNodeRef ensure_yoga_node(InstanceNode& node) {
    if (node.yoga_node_handle != 0) {
        return reinterpret_cast<YGNodeRef>(node.yoga_node_handle);
    }
    YGNodeRef yn = YGNodeNew();
    node.yoga_node_handle = reinterpret_cast<std::uintptr_t>(yn);
    YGNodeSetContext(yn, &node);
    return yn;
}

static void free_yoga_tree(InstanceNode& node) {
    if (node.yoga_node_handle != 0) {
        YGNodeRef yn = reinterpret_cast<YGNodeRef>(node.yoga_node_handle);
        YGNodeFree(yn);
        node.yoga_node_handle = 0;
    }
    for (auto& child : node.children) {
        free_yoga_tree(*child);
    }
}

static void build_yoga_subtree(InstanceNode& node) {
    YGNodeRef yn = ensure_yoga_node(node);
    YGNodeRemoveAllChildren(yn);

    const FlexStyle& style = props_as_view_ref(node.current_vnode).style;
    apply_style(yn, style);

    if (node.type == host_type_text() || node.type == host_type_button() || node.type == host_type_input()) {
        YGNodeSetMeasureFunc(yn, measure_text_node);
    } else {
        YGNodeSetMeasureFunc(yn, nullptr);
    }

    for (std::size_t i = 0; i < node.children.size(); ++i) {
        InstanceNode& child = *node.children[i];
        build_yoga_subtree(child);
        YGNodeInsertChild(yn, reinterpret_cast<YGNodeRef>(child.yoga_node_handle), static_cast<uint32_t>(i));
    }
}

static void apply_layout_results(InstanceNode& node) {
    if (node.yoga_node_handle == 0) return;
    YGNodeRef yn = reinterpret_cast<YGNodeRef>(node.yoga_node_handle);
    LayoutRect next;
    next.x = YGNodeLayoutGetLeft(yn);
    next.y = YGNodeLayoutGetTop(yn);
    next.width = YGNodeLayoutGetWidth(yn);
    next.height = YGNodeLayoutGetHeight(yn);

    if (!(next == node.layout)) {
        node.layout = next;
        node.dirty = true;
        node.cached_picture.reset();
    }

    for (auto& child : node.children) {
        apply_layout_results(*child);
    }
}

class SkiaRuntime {
public:
    explicit SkiaRuntime(AppRenderFunc app)
        : app_render_(std::move(app)) {
        font_mgr_ = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());

        g_skia_dispatcher.request_update = &SkiaRuntime::request_update_trampoline;
        g_skia_dispatcher.request_update_ctx = this;
    }

    Element render_frame() {
        g_skia_dispatcher.current_instance = &root_instance_;
        g_skia_dispatcher.current_index = 0;

        Element new_root = app_render_();
        reconcile(root_instance_, new_root);
        g_skia_dispatcher.current_instance = nullptr;
        return root_instance_.current_vnode;
    }

    void perform_update_if_needed() {
        if (!update_requested_) return;
        update_requested_ = false;
        (void)render_frame();
    }

    void draw(SkCanvas* canvas, int width, int height) {
        DrawContext ctx;
        ctx.surface_width = width;
        ctx.surface_height = height;
        ctx.font_mgr = font_mgr_;
        g_font_mgr = font_mgr_;

        canvas->clear(SK_ColorWHITE);

        build_yoga_subtree(root_instance_);
        YGNodeRef root_yoga = reinterpret_cast<YGNodeRef>(root_instance_.yoga_node_handle);
        YGNodeCalculateLayout(root_yoga, static_cast<float>(width), static_cast<float>(height), YGDirectionLTR);
        apply_layout_results(root_instance_);

        render_cached_node(root_instance_, canvas, ctx);
    }

    void handle_mouse_down(float x, float y) {
        InstanceNode* hit = hit_test_at(root_instance_, x, y);
        InstanceNode* hit_input = find_ancestor_by_type(hit, host_type_input());
        set_focus(hit_input);
        (void)dispatch_click_bubble(hit);
    }

    void handle_text_input(const char* text) {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        const std::string inserted(text ? text : "");
        state.value.insert(state.cursor, inserted);
        state.cursor += inserted.size();
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_backspace() {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        if (state.cursor == 0 || state.value.empty()) return;
        state.value.erase(state.cursor - 1, 1);
        state.cursor -= 1;
        mark_dirty(focused_input_);
        request_update();
    }

private:
    static void request_update_trampoline(void* ctx) {
        if (!ctx) return;
        static_cast<SkiaRuntime*>(ctx)->request_update();
    }

    void request_update() {
        update_requested_ = true;
    }

    static bool is_descendant_or_self(const InstanceNode* node, const InstanceNode* possible_ancestor) {
        for (auto* current = node; current != nullptr; current = current->parent) {
            if (current == possible_ancestor) {
                return true;
            }
        }
        return false;
    }

    void set_focus(InstanceNode* input_node) {
        if (focused_input_ == input_node) return;

        if (focused_input_) {
            if (focused_input_->input_state) {
                focused_input_->input_state->focused = false;
                mark_dirty(focused_input_);
            }
        }

        focused_input_ = input_node;
        if (focused_input_ && focused_input_->input_state) {
            focused_input_->input_state->focused = true;
            focused_input_->input_state->cursor = std::min(
                focused_input_->input_state->cursor,
                focused_input_->input_state->value.size()
            );
            mark_dirty(focused_input_);
        }
    }

    static bool point_in_rect(const LayoutRect& r, float x, float y) {
        return x >= r.x && x <= (r.x + r.width) && y >= r.y && y <= (r.y + r.height);
    }

    static InstanceNode* find_ancestor_by_type(InstanceNode* node, TypeId type) {
        for (auto* current = node; current != nullptr; current = current->parent) {
            if (current->type == type) {
                return current;
            }
        }
        return nullptr;
    }

    static std::shared_ptr<const std::function<void()>> click_handler_for(const ElementProps& props) {
        return std::visit([](const auto& p) -> std::shared_ptr<const std::function<void()>> {
            using P = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<P, ButtonProps>) {
                return p.on_click;
            } else {
                return nullptr;
            }
        }, props);
    }

    static bool dispatch_click_bubble(InstanceNode* target) {
        for (auto* current = target; current != nullptr; current = current->parent) {
            auto handler = click_handler_for(current->current_vnode.props);
            if (handler) {
                (*handler)();
                return true;
            }
        }
        return false;
    }

    InstanceNode* hit_test_at(InstanceNode& node, float x, float y) {
        const LayoutRect self = layout_for_node(node);
        if (x < 0.0f || y < 0.0f || x > self.width || y > self.height) {
            return nullptr;
        }

        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            InstanceNode& child = *it->get();
            const LayoutRect r = layout_for_node(child);
            if (!point_in_rect(r, x, y)) {
                continue;
            }
            if (InstanceNode* found = hit_test_at(child, x - r.x, y - r.y)) {
                return found;
            }
            return &child;
        }

        return &node;
    }

    void mark_dirty(InstanceNode* node) {
        if (!node) return;
        node->dirty = true;
        node->cached_picture.reset();
    }

    bool reconcile_children(InstanceNode& inst, const std::vector<Element>& new_children) {
        bool local_structure_changed = false;

        if (inst.children.size() != new_children.size()) {
            local_structure_changed = true;
        }

        const std::size_t common = std::min(inst.children.size(), new_children.size());
        for (std::size_t i = 0; i < common; ++i) {
            inst.children[i]->parent = &inst;
            reconcile(*inst.children[i], new_children[i]);
        }

        if (inst.children.size() > new_children.size()) {
            for (std::size_t i = new_children.size(); i < inst.children.size(); ++i) {
                if (focused_input_ && is_descendant_or_self(focused_input_, inst.children[i].get())) {
                    focused_input_ = nullptr;
                }
            }
            inst.children.resize(new_children.size());
        }

        for (std::size_t i = common; i < new_children.size(); ++i) {
            auto child = std::make_unique<InstanceNode>();
            child->type = new_children[i].type;
            child->current_vnode = new_children[i];
            child->parent = &inst;
            child->dirty = true;
            child->cached_picture.reset();
            init_node_state(*child);
            reconcile_children(*child, new_children[i].children);
            inst.children.push_back(std::move(child));
            local_structure_changed = true;
        }

        return local_structure_changed;
    }

    void init_node_state(InstanceNode& node) {
        if (node.type == host_type_input()) {
            const auto& props = std::get<InputProps>(node.current_vnode.props);
            InstanceNode::InputState state;
            state.value = props.value;
            state.cursor = state.value.size();
            state.focused = false;
            node.input_state = std::move(state);
        } else {
            node.input_state.reset();
        }
    }

    bool reconcile(InstanceNode& inst, const Element& vnode) {
        bool local_changed = false;

        if (inst.type != vnode.type) {
            const TypeId old_type = inst.type;
            if (focused_input_ && is_descendant_or_self(focused_input_, &inst)) {
                focused_input_ = nullptr;
            }
            inst.type = vnode.type;
            inst.current_vnode = vnode;
            if (old_type != nullptr) {
                inst.hooks.clear();
            }
            inst.children.clear();
            inst.cached_picture.reset();
            inst.dirty = true;
            init_node_state(inst);
            reconcile_children(inst, vnode.children);
            return true;
        }

        if (inst.current_vnode.props != vnode.props) {
            local_changed = true;
            if (inst.type == host_type_input() && inst.input_state) {
                const auto& new_input = std::get<InputProps>(vnode.props);
                if (inst.input_state->value != new_input.value) {
                    inst.input_state->value = new_input.value;
                    inst.input_state->cursor = std::min(inst.input_state->cursor, inst.input_state->value.size());
                }
            }
        }

        inst.current_vnode = vnode;
        (void)reconcile_children(inst, vnode.children);

        if (local_changed || vnode.dirty) {
            inst.dirty = true;
            inst.cached_picture.reset();
        }
        return local_changed;
    }

    static SkRect local_recording_bounds(const Element& vnode) {
        const auto& props = props_as_view_ref(vnode);
        const float w = std::max(props.style.width.value_or(1.0f), 1.0f);
        const float h = std::max(props.style.height.value_or(1.0f), 1.0f);
        return SkRect::MakeXYWH(0.0f, 0.0f, w, h);
    }

    void render_cached_node(InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) {
        const LayoutRect r = layout_for_node(node);
        canvas->save();
        canvas->translate(r.x, r.y);

        if (node.dirty || !node.cached_picture) {
            SkPictureRecorder recorder;
            const SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, std::max(r.width, 1.0f), std::max(r.height, 1.0f));
            SkCanvas* record_canvas = recorder.beginRecording(bounds);

            renderer_for(node.type).on_draw(node, record_canvas, ctx);

            sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();
            node.cached_picture = std::shared_ptr<SkPicture>(
                picture.release(),
                [](SkPicture* p) {
                    SkSafeUnref(p);
                }
            );
            node.dirty = false;
        }

        canvas->drawPicture(node.cached_picture.get());
        for (auto& child : node.children) {
            render_cached_node(*child, canvas, ctx);
        }

        canvas->restore();
    }

    AppRenderFunc app_render_;
    InstanceNode root_instance_{};
    InstanceNode* focused_input_{nullptr};
    sk_sp<SkFontMgr> font_mgr_;
    bool update_requested_{true};

public:
    ~SkiaRuntime() {
        if (g_skia_dispatcher.request_update_ctx == this) {
            g_skia_dispatcher.request_update = nullptr;
            g_skia_dispatcher.request_update_ctx = nullptr;
        }
        free_yoga_tree(root_instance_);
    }
};

}

TypeId host_type_view() {
    static int dummy;
    return &dummy;
}

TypeId host_type_button() {
    static int dummy;
    return &dummy;
}

TypeId host_type_text() {
    static int dummy;
    return &dummy;
}

TypeId host_type_input() {
    static int dummy;
    return &dummy;
}

Element View(const ViewProps& props, std::vector<Element> children) {
    Element e;
    e.type = host_type_view();
    e.props = props;
    e.children = std::move(children);
    return e;
}

Element Button(const ButtonProps& props) {
    Element e;
    e.type = host_type_button();
    e.props = props;
    return e;
}

Element Text(const TextProps& props) {
    Element e;
    e.type = host_type_text();
    e.props = props;
    return e;
}

Element Input(const InputProps& props) {
    Element e;
    e.type = host_type_input();
    e.props = props;
    return e;
}

void draw_element_tree(const Element& el, SkCanvas* canvas, int width, int height) {
    SkiaRuntime runtime([&el]() { return el; });
    runtime.render_frame();
    runtime.draw(canvas, width, height);
}

int run_skia_app(const AppRenderFunc& app) {
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    const int width = 800;
    const int height = 600;

    SDL_Window* window = SDL_CreateWindow(
        "Skia Reactive Demo",
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    }

    SkImageInfo info = SkImageInfo::Make(
        width,
        height,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType
    );

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    auto surface = SkSurfaces::WrapPixels(
        info,
        pixels.data(),
        static_cast<size_t>(width) * 4
    );
    if (!surface) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SkSurface::MakeRasterDirect failed");
    }

    SkiaRuntime runtime(app);
    runtime.perform_update_if_needed();
    (void)SDL_StartTextInput(window);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == REACTCPP_SDL_EVENT_QUIT) {
                running = false;
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                runtime.handle_mouse_down(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_INPUT) {
                runtime.handle_text_input(e.text.text);
            } else if (e.type == REACTCPP_SDL_EVENT_KEY_DOWN && e.key.key == SDLK_BACKSPACE) {
                runtime.handle_backspace();
            }
        }

        runtime.perform_update_if_needed();

        SkCanvas* canvas = surface->getCanvas();
        runtime.draw(canvas, width, height);

        void* texPixels = nullptr;
        int pitch = 0;
        if (!SDL_LockTexture(texture, nullptr, &texPixels, &pitch)) {
            throw std::runtime_error(std::string("SDL_LockTexture failed: ") + SDL_GetError());
        }

        for (int y = 0; y < height; ++y) {
            std::memcpy(
                static_cast<std::uint8_t*>(texPixels) + y * pitch,
                pixels.data() + static_cast<std::size_t>(y) * width,
                static_cast<std::size_t>(width) * 4
            );
        }

        SDL_UnlockTexture(texture);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    (void)SDL_StopTextInput(window);
    SDL_Quit();
    return 0;
}
