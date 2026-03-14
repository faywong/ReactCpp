#include "skia_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "text_edit.hpp"

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
#include "core/SkRRect.h"
#include "core/SkRect.h"
#include "core/SkRefCnt.h"
#include "core/SkSurface.h"
#include "core/SkTypeface.h"

#include "effects/SkGradientShader.h"

#include "ports/SkFontMgr_fontconfig.h"
#include "ports/SkFontScanner_FreeType.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define REACTCPP_SDL_EVENT_QUIT SDL_EVENT_QUIT
#define REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN SDL_EVENT_MOUSE_BUTTON_DOWN
#define REACTCPP_SDL_EVENT_TEXT_INPUT SDL_EVENT_TEXT_INPUT
#define REACTCPP_SDL_EVENT_TEXT_EDITING SDL_EVENT_TEXT_EDITING
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
    LayoutRect r = node.layout;
    if (r.width <= 0.0f || r.height <= 0.0f) {
        const ViewProps& p = props_as_view_ref(node.current_vnode);
        r.width = p.style.width.value_or(1.0f);
        r.height = p.style.height.value_or(1.0f);
    }
    return r;
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

static reactcpp::text::Selection selection_from(const InstanceNode::InputState& s) {
    reactcpp::text::Selection sel;
    sel.active = s.has_selection;
    sel.start = s.sel_start;
    sel.end = s.sel_end;
    reactcpp::text::normalize_selection(sel, s.value.size());
    return sel;
}

static void selection_to(InstanceNode::InputState& s, const reactcpp::text::Selection& sel) {
    s.sel_start = std::min(sel.start, s.value.size());
    s.sel_end = std::min(sel.end, s.value.size());
    s.has_selection = sel.active && s.sel_start < s.sel_end;
}

static void clear_selection(InstanceNode::InputState& s) {
    s.sel_start = 0;
    s.sel_end = 0;
    s.sel_anchor = 0;
    s.has_selection = false;
}

static void clear_selection_keep_anchor(InstanceNode::InputState& s) {
    s.sel_start = 0;
    s.sel_end = 0;
    s.has_selection = false;
}

static void clear_preedit(InstanceNode::InputState& s) {
    s.preedit.clear();
    s.preedit_start = -1;
    s.preedit_length = -1;
}

static reactcpp::text::UndoSnapshot snapshot_from_input(const InstanceNode::InputState& s) {
    reactcpp::text::UndoSnapshot snap;
    snap.value = s.value;
    snap.cursor = s.cursor;
    snap.sel_start = s.sel_start;
    snap.sel_end = s.sel_end;
    snap.sel_anchor = s.sel_anchor;
    snap.has_selection = s.has_selection;
    return snap;
}

static void apply_snapshot(InstanceNode::InputState& s, const reactcpp::text::UndoSnapshot& snap) {
    s.value = snap.value;
    s.cursor = std::min(snap.cursor, s.value.size());
    s.sel_start = std::min(snap.sel_start, s.value.size());
    s.sel_end = std::min(snap.sel_end, s.value.size());
    s.sel_anchor = std::min(snap.sel_anchor, s.value.size());
    s.has_selection = snap.has_selection && s.sel_start < s.sel_end;
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
        const bool composing = node.focused && node.input_state && !node.input_state->preedit.empty();
        const std::string preedit = composing ? node.input_state->preedit : std::string();

        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height);
        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRoundRect(bounds, 6.0f, 6.0f, fill);

        SkPaint border;
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(2.0f);
        border.setColor(make_color(props.border_r, props.border_g, props.border_b));
        canvas->drawRoundRect(bounds, 6.0f, 6.0f, border);

        const float text_x = 8.0f;
        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));
        const float text_y = baseline_for_centered_text(font, r.height);

        const bool showing_placeholder = value.empty() && preedit.empty() && !props.placeholder.empty();
        if (showing_placeholder) {
            SkPaint paint;
            paint.setColor(make_color(0.55f, 0.55f, 0.55f));
            canvas->drawString(props.placeholder.c_str(), text_x, text_y, font, paint);
            return;
        }

        SkPaint paint;
        paint.setColor(make_color(props.text_r, props.text_g, props.text_b));

        if (!preedit.empty() && node.input_state) {
            const std::size_t cursor = std::min(node.input_state->cursor, value.size());
            const std::string left = value.substr(0, cursor);
            const std::string right = value.substr(cursor);

            float x = text_x;
            canvas->drawString(left.c_str(), x, text_y, font, paint);
            x += font.measureText(left.c_str(), left.size(), SkTextEncoding::kUTF8);

            canvas->drawString(preedit.c_str(), x, text_y, font, paint);
            const float preedit_w = font.measureText(preedit.c_str(), preedit.size(), SkTextEncoding::kUTF8);

            SkPaint underline;
            underline.setAntiAlias(true);
            underline.setColor(make_color(props.text_r, props.text_g, props.text_b));
            underline.setStrokeWidth(1.5f);
            canvas->drawLine(x, text_y + 2.0f, x + preedit_w, text_y + 2.0f, underline);

            x += preedit_w;
            canvas->drawString(right.c_str(), x, text_y, font, paint);
        } else {
            if (node.input_state && node.input_state->has_selection && node.input_state->sel_start < node.input_state->sel_end) {
                const std::size_t start = std::min(node.input_state->sel_start, value.size());
                const std::size_t end = std::min(node.input_state->sel_end, value.size());

                const std::string left = value.substr(0, start);
                const std::string mid = value.substr(start, end - start);
                const std::string right = value.substr(end);

                SkFontMetrics metrics;
                font.getMetrics(&metrics);

                const float left_w = font.measureText(left.c_str(), left.size(), SkTextEncoding::kUTF8);
                const float mid_w = font.measureText(mid.c_str(), mid.size(), SkTextEncoding::kUTF8);

                SkPaint highlight;
                highlight.setColor(static_cast<SkColor>(0xFF1E3A8A));
                highlight.setAntiAlias(true);
                highlight.setStyle(SkPaint::kFill_Style);

                const float top = text_y + metrics.fAscent;
                const float bottom = text_y + metrics.fDescent;
                const float pad_y = 1.0f;
                const float left_x = text_x + left_w;

                canvas->save();
                SkRRect clip_rr;
                clip_rr.setRectXY(bounds, 6.0f, 6.0f);
                canvas->clipRRect(clip_rr, true);
                canvas->drawRoundRect(
                    SkRect::MakeLTRB(left_x, top - pad_y, left_x + mid_w, bottom + pad_y),
                    2.0f,
                    2.0f,
                    highlight
                );

                canvas->drawString(left.c_str(), text_x, text_y, font, paint);

                SkPaint selected_paint = paint;
                selected_paint.setColor(SK_ColorWHITE);
                canvas->drawString(mid.c_str(), left_x, text_y, font, selected_paint);

                canvas->drawString(right.c_str(), left_x + mid_w, text_y, font, paint);
                canvas->restore();
            } else {
                canvas->drawString(value.c_str(), text_x, text_y, font, paint);
            }
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
    SkiaRuntime(AppRenderFunc app, SDL_Window* window)
        : app_render_(std::move(app))
        , window_(window) {
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

        update_text_input_area_if_needed(ctx);

        render_cached_node(root_instance_, canvas, ctx);
    }

    void handle_mouse_down(float x, float y) {
        InstanceNode* hit = hit_test_at(root_instance_, x, y);
        set_focus(hit);
        if (hit) {
            (void)dispatch_click_bubble(hit);
        }
    }

    void handle_text_input(const char* text) {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        const std::string inserted(text ? text : "");
        if (!inserted.empty()) {
            state.undo.push(snapshot_from_input(state));
        }
        reactcpp::text::Selection sel = selection_from(state);
        reactcpp::text::insert_text(state.value, state.cursor, sel, inserted);
        selection_to(state, sel);
        state.sel_anchor = state.cursor;
        clear_preedit(state);
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_text_editing(const char* text, int start, int length) {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        state.preedit = text ? std::string(text) : std::string();
        state.preedit_start = start;
        state.preedit_length = length;
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_backspace() {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        state.cursor = std::min(state.cursor, state.value.size());
        if (!state.preedit.empty()) {
            return;
        }

        {
            reactcpp::text::Selection sel = selection_from(state);
            if (reactcpp::text::has_non_empty_selection(sel)) {
                state.undo.push(snapshot_from_input(state));
                reactcpp::text::erase_selection(state.value, state.cursor, sel);
                selection_to(state, sel);
                state.sel_anchor = state.cursor;
                mark_dirty(focused_input_);
                request_update();
                return;
            }
        }

        if (state.cursor == 0 || state.value.empty()) return;
        const std::size_t cursor = std::min(state.cursor, state.value.size());
        const std::size_t prev = reactcpp::text::utf8_prev_boundary(state.value, cursor);
        if (prev >= cursor) return;
        state.undo.push(snapshot_from_input(state));
        state.value.erase(prev, cursor - prev);
        state.cursor = prev;
        state.sel_anchor = state.cursor;
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_key_down(SDL_Keycode key, SDL_Keymod mod, bool) {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;

        const bool accel = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        const bool shift = (mod & SDL_KMOD_SHIFT) != 0;

        if (accel) {
            if (!state.preedit.empty() && window_) {
                (void)SDL_ClearComposition(window_);
                clear_preedit(state);
            }

            reactcpp::text::Selection sel = selection_from(state);

            if (key == SDLK_Z) {
                auto snap = state.undo.pop();
                if (snap) {
                    if (!state.preedit.empty() && window_) {
                        (void)SDL_ClearComposition(window_);
                    }
                    apply_snapshot(state, *snap);
                    clear_preedit(state);
                    mark_dirty(focused_input_);
                    request_update();
                }
                return;
            }

            if (key == SDLK_A) {
                sel.active = !state.value.empty();
                sel.start = 0;
                sel.end = state.value.size();
                state.cursor = sel.end;
                selection_to(state, sel);
                state.sel_anchor = 0;
                mark_dirty(focused_input_);
                request_update();
                return;
            }

            if (key == SDLK_C) {
                const std::string copy = reactcpp::text::selected_substr(state.value, sel);
                if (!copy.empty()) {
                    (void)SDL_SetClipboardText(copy.c_str());
                }
                return;
            }

            if (key == SDLK_X) {
                const std::string cut = reactcpp::text::selected_substr(state.value, sel);
                if (!cut.empty()) {
                    (void)SDL_SetClipboardText(cut.c_str());
                    state.undo.push(snapshot_from_input(state));
                    reactcpp::text::erase_selection(state.value, state.cursor, sel);
                    selection_to(state, sel);
                    state.sel_anchor = state.cursor;
                    mark_dirty(focused_input_);
                    request_update();
                }
                return;
            }

            if (key == SDLK_V) {
                char* clip = SDL_GetClipboardText();
                const std::string paste = clip ? std::string(clip) : std::string();
                if (clip) SDL_free(clip);
                if (!paste.empty()) {
                    state.undo.push(snapshot_from_input(state));
                    reactcpp::text::insert_text(state.value, state.cursor, sel, paste);
                    selection_to(state, sel);
                    state.sel_anchor = state.cursor;
                    mark_dirty(focused_input_);
                    request_update();
                }
                return;
            }
        }

        if (key == SDLK_BACKSPACE) {
            handle_backspace();
            return;
        }

        if (!state.preedit.empty()) {
            return;
        }

        if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            state.cursor = std::min(state.cursor, state.value.size());

            const std::size_t before = state.cursor;
            if (key == SDLK_LEFT) {
                state.cursor = reactcpp::text::utf8_prev_boundary(state.value, state.cursor);
            } else {
                state.cursor = reactcpp::text::utf8_next_boundary(state.value, state.cursor);
            }

            if (!shift) {
                clear_selection(state);
                state.sel_anchor = state.cursor;
            } else {
                if (!state.has_selection) {
                    state.sel_anchor = before;
                }
                const auto sel = reactcpp::text::selection_from_anchor(state.sel_anchor, state.cursor, state.value.size());
                state.sel_start = sel.start;
                state.sel_end = sel.end;
                state.has_selection = sel.active;
            }

            mark_dirty(focused_input_);
            request_update();
            return;
        }
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

    void set_focus(InstanceNode* node) {
        if (focused_node_ == node) return;

        const bool was_input = focused_node_ && focused_node_->type == host_type_input();

        if (focused_node_) {
            focused_node_->focused = false;
            if (focused_node_->type == host_type_input() && focused_node_->input_state) {
                focused_node_->input_state->focused = false;
                clear_preedit(*focused_node_->input_state);
                clear_selection(*focused_node_->input_state);
            }
            if (auto on_blur = blur_handler_for(focused_node_->current_vnode.props)) {
                (*on_blur)();
            }
        }

        if (window_ && was_input) {
            (void)SDL_ClearComposition(window_);
            (void)SDL_StopTextInput(window_);
            last_text_input_area_.reset();
            last_text_input_cursor_px_ = -1;
        }

        focused_node_ = node;
        focused_input_ = (focused_node_ && focused_node_->type == host_type_input()) ? focused_node_ : nullptr;

        if (focused_node_) {
            focused_node_->focused = true;
            if (focused_node_->type == host_type_input() && focused_node_->input_state) {
                focused_node_->input_state->focused = true;
                focused_node_->input_state->cursor = std::min(
                    focused_node_->input_state->cursor,
                    focused_node_->input_state->value.size()
                );
                reactcpp::text::Selection sel = selection_from(*focused_node_->input_state);
                selection_to(*focused_node_->input_state, sel);
            }
            if (auto on_focus = focus_handler_for(focused_node_->current_vnode.props)) {
                (*on_focus)();
            }
        }

        if (window_ && focused_node_ && focused_node_->type == host_type_input()) {
            (void)SDL_StartTextInput(window_);
        }
    }

    static void absolute_origin_for(const InstanceNode* node, float& out_x, float& out_y) {
        out_x = 0.0f;
        out_y = 0.0f;
        for (auto* cur = node; cur != nullptr; cur = cur->parent) {
            const LayoutRect r = layout_for_node(*cur);
            out_x += r.x;
            out_y += r.y;
        }
    }

    void update_text_input_area_if_needed(const DrawContext& ctx) {
        if (!window_) return;
        if (!focused_input_ || !focused_input_->input_state) return;

        int win_w = 0;
        int win_h = 0;
        (void)SDL_GetWindowSize(window_, &win_w, &win_h);
        if (win_w <= 0 || win_h <= 0) return;

        const float surface_w = std::max(static_cast<float>(ctx.surface_width), 1.0f);
        const float surface_h = std::max(static_cast<float>(ctx.surface_height), 1.0f);
        const float to_win_x = static_cast<float>(win_w) / surface_w;
        const float to_win_y = static_cast<float>(win_h) / surface_h;

        const LayoutRect lr = layout_for_node(*focused_input_);
        float abs_x = 0.0f;
        float abs_y = 0.0f;
        absolute_origin_for(focused_input_, abs_x, abs_y);

        float rect_x_f = abs_x * to_win_x;
        float rect_y_f = abs_y * to_win_y;
        float rect_w_f = std::max(lr.width, 1.0f) * to_win_x;
        float rect_h_f = std::max(lr.height, 1.0f) * to_win_y;

        SDL_Rect rect;
        rect.x = static_cast<int>(std::lround(rect_x_f));
        rect.y = static_cast<int>(std::lround(rect_y_f));
        rect.w = static_cast<int>(std::lround(std::max(rect_w_f, 1.0f)));
        rect.h = static_cast<int>(std::lround(std::max(rect_h_f, 1.0f)));

        const auto& props = std::get<InputProps>(focused_input_->current_vnode.props);
        const auto& state = *focused_input_->input_state;

        const std::size_t cursor = std::min(state.cursor, state.value.size());
        const std::string left_text = state.value.substr(0, cursor);

        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        const float text_x = 8.0f;
        float cursor_x = text_x + font.measureText(left_text.c_str(), left_text.size(), SkTextEncoding::kUTF8);
        if (!state.preedit.empty()) {
            cursor_x += font.measureText(state.preedit.c_str(), state.preedit.size(), SkTextEncoding::kUTF8);
        }

        float cursor_off_f = cursor_x * to_win_x;

        if (const char* driver = SDL_GetCurrentVideoDriver(); driver && std::strcmp(driver, "wayland") == 0) {
            int pix_w = 0;
            int pix_h = 0;
            if (SDL_GetWindowSizeInPixels(window_, &pix_w, &pix_h) && pix_w > 0 && pix_h > 0) {
                const float to_px_x = static_cast<float>(pix_w) / static_cast<float>(win_w);
                const float to_px_y = static_cast<float>(pix_h) / static_cast<float>(win_h);
                rect.x = static_cast<int>(std::lround(static_cast<float>(rect.x) * to_px_x));
                rect.y = static_cast<int>(std::lround(static_cast<float>(rect.y) * to_px_y));
                rect.w = static_cast<int>(std::lround(std::max(static_cast<float>(rect.w) * to_px_x, 1.0f)));
                rect.h = static_cast<int>(std::lround(std::max(static_cast<float>(rect.h) * to_px_y, 1.0f)));
                cursor_off_f *= to_px_x;
            }
        }

        const int cursor_px = static_cast<int>(std::lround(cursor_off_f));

        if (last_text_input_area_ && rect.x == last_text_input_area_->x && rect.y == last_text_input_area_->y
            && rect.w == last_text_input_area_->w && rect.h == last_text_input_area_->h
            && cursor_px == last_text_input_cursor_px_) {
            return;
        }

        (void)SDL_SetTextInputArea(window_, &rect, cursor_px);
        last_text_input_area_ = rect;
        last_text_input_cursor_px_ = cursor_px;
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

    static std::shared_ptr<const std::function<void()>> focus_handler_for(const ElementProps& props) {
        return std::visit([](const auto& p) -> std::shared_ptr<const std::function<void()>> {
            return p.on_focus;
        }, props);
    }

    static std::shared_ptr<const std::function<void()>> blur_handler_for(const ElementProps& props) {
        return std::visit([](const auto& p) -> std::shared_ptr<const std::function<void()>> {
            return p.on_blur;
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

    static float focus_radius_for(const InstanceNode& node) {
        if (node.type == host_type_button()) return 8.0f;
        if (node.type == host_type_input()) return 6.0f;
        if (node.type == host_type_text()) return 2.0f;
        return 0.0f;
    }

    static void draw_focus_ring(const InstanceNode& node, SkCanvas* canvas, float width, float height) {
        if (!node.focused) return;
        if (width <= 0.0f || height <= 0.0f) return;

        const float offset = 2.0f;
        const float w = std::max(width + offset * 2.0f, 1.0f);
        const float h = std::max(height + offset * 2.0f, 1.0f);
        SkRect rect = SkRect::MakeXYWH(-offset, -offset, w, h);

        const SkPoint pts[2] = {
            SkPoint::Make(rect.left(), rect.top()),
            SkPoint::Make(rect.left(), rect.bottom())
        };

        const SkColor ring_colors[] = {
            SkColorSetARGB(230, 120, 190, 255),
            SkColorSetARGB(230, 51, 115, 242),
            SkColorSetARGB(230, 30, 80, 210)
        };
        const SkColor glow_colors[] = {
            SkColorSetARGB(70, 120, 190, 255),
            SkColorSetARGB(70, 51, 115, 242),
            SkColorSetARGB(70, 30, 80, 210)
        };
        const SkScalar pos[] = {0.0f, 0.5f, 1.0f};

        sk_sp<SkShader> ring_shader = SkGradientShader::MakeLinear(pts, ring_colors, pos, 3, SkTileMode::kClamp);
        sk_sp<SkShader> glow_shader = SkGradientShader::MakeLinear(pts, glow_colors, pos, 3, SkTileMode::kClamp);

        SkPaint glow;
        glow.setAntiAlias(true);
        glow.setStyle(SkPaint::kStroke_Style);
        glow.setStrokeWidth(6.0f);
        glow.setColor(glow_colors[1]);
        if (glow_shader) {
            glow.setShader(glow_shader);
        }

        SkPaint ring;
        ring.setAntiAlias(true);
        ring.setStyle(SkPaint::kStroke_Style);
        ring.setStrokeWidth(2.0f);
        ring.setColor(ring_colors[1]);
        if (ring_shader) {
            ring.setShader(ring_shader);
        }

        const float radius = focus_radius_for(node);
        if (radius > 0.0f) {
            canvas->drawRoundRect(rect, radius + offset, radius + offset, glow);
            canvas->drawRoundRect(rect, radius + offset, radius + offset, ring);
        } else {
            canvas->drawRect(rect, glow);
            canvas->drawRect(rect, ring);
        }
    }

    static void draw_input_caret_if_focused(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx, float height) {
        if (!node.focused) return;
        if (node.type != host_type_input()) return;
        if (!node.input_state) return;

        const auto& props = std::get<InputProps>(node.current_vnode.props);
        const std::string value = node.input_state->value;
        const std::string preedit = node.input_state->preedit;
        const std::size_t cursor = std::min(node.input_state->cursor, value.size());

        const float text_x = 8.0f;
        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        const std::string left_text = value.substr(0, cursor);
        float cursor_x = text_x + font.measureText(left_text.c_str(), left_text.size(), SkTextEncoding::kUTF8);
        if (!preedit.empty()) {
            cursor_x += font.measureText(preedit.c_str(), preedit.size(), SkTextEncoding::kUTF8);
        }
        SkPaint caret;
        caret.setAntiAlias(true);
        caret.setColor(make_color(0.2f, 0.2f, 0.2f));
        caret.setStrokeWidth(1.5f);
        canvas->drawLine(cursor_x, 8.0f, cursor_x, height - 8.0f, caret);
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
                if (focused_node_ && is_descendant_or_self(focused_node_, inst.children[i].get())) {
                    focused_node_ = nullptr;
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
            state.sel_start = 0;
            state.sel_end = 0;
            state.sel_anchor = state.cursor;
            state.has_selection = false;
            state.focused = false;
            clear_preedit(state);
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
            if (focused_node_ && is_descendant_or_self(focused_node_, &inst)) {
                focused_node_ = nullptr;
            }
            inst.type = vnode.type;
            inst.current_vnode = vnode;
            if (old_type != nullptr) {
                inst.hooks.clear();
            }
            inst.children.clear();
            inst.cached_picture.reset();
            inst.dirty = true;
            inst.focused = false;
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
                    clear_preedit(*inst.input_state);
                    reactcpp::text::Selection sel = selection_from(*inst.input_state);
                    selection_to(*inst.input_state, sel);
                    inst.input_state->sel_anchor = inst.input_state->cursor;
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

        draw_input_caret_if_focused(node, canvas, ctx, r.height);
        draw_focus_ring(node, canvas, r.width, r.height);

        canvas->restore();
    }

    AppRenderFunc app_render_;
    InstanceNode root_instance_{};
    InstanceNode* focused_node_{nullptr};
    InstanceNode* focused_input_{nullptr};
    sk_sp<SkFontMgr> font_mgr_;
    bool update_requested_{true};

    SDL_Window* window_{nullptr};
    std::optional<SDL_Rect> last_text_input_area_;
    int last_text_input_cursor_px_{-1};

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

int run_skia_app(const AppRenderFunc& app) {
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    const int width = 800;
    const int height = 600;

    SDL_Window* window = SDL_CreateWindow(
        "ReactCpp GUI Demo",
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
        SDL_PIXELFORMAT_BGRA8888,
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
        kBGRA_8888_SkColorType,
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

    SkiaRuntime runtime(app, window);
    runtime.perform_update_if_needed();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == REACTCPP_SDL_EVENT_QUIT) {
                running = false;
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                runtime.handle_mouse_down(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_EDITING) {
                runtime.handle_text_editing(e.edit.text, e.edit.start, e.edit.length);
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_INPUT) {
                runtime.handle_text_input(e.text.text);
            } else if (e.type == REACTCPP_SDL_EVENT_KEY_DOWN) {
                runtime.handle_key_down(e.key.key, e.key.mod, e.key.repeat);
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
    SDL_Quit();
    return 0;
}
