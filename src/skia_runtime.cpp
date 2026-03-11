#include "skia_runtime.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkFontMgr.h"
#include "core/SkImageInfo.h"
#include "core/SkPaint.h"
#include "core/SkPicture.h"
#include "core/SkPictureRecorder.h"
#include "core/SkRect.h"
#include "core/SkSurface.h"
#include "core/SkTypes.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextStyle.h"

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define REACTCPP_USE_SDL3 1
#else
#include <SDL2/SDL.h>
#define REACTCPP_USE_SDL3 0
#endif

#if REACTCPP_USE_SDL3
#define REACTCPP_SDL_EVENT_QUIT SDL_EVENT_QUIT
#define REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN SDL_EVENT_MOUSE_BUTTON_DOWN
#define REACTCPP_SDL_EVENT_TEXT_INPUT SDL_EVENT_TEXT_INPUT
#define REACTCPP_SDL_EVENT_KEY_DOWN SDL_EVENT_KEY_DOWN
#else
#define REACTCPP_SDL_EVENT_QUIT SDL_QUIT
#define REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define REACTCPP_SDL_EVENT_TEXT_INPUT SDL_TEXTINPUT
#define REACTCPP_SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#endif

thread_local HookDispatcher g_skia_dispatcher;

namespace {

namespace tl = skia::textlayout;

struct DrawContext {
    int surface_width{0};
    int surface_height{0};
    sk_sp<tl::FontCollection> fonts;
};

static ViewProps props_as_view(const Element& el) {
    return std::visit([](const auto& props) {
        return static_cast<ViewProps>(props);
    }, el.props);
}

static SkColor make_color(float r, float g, float b, float a = 1.0f) {
    const auto to_u8 = [](float v) -> U8CPU {
        const float clamped = std::clamp(v, 0.0f, 1.0f);
        return static_cast<U8CPU>(clamped * 255.0f);
    };
    return SkColorSetARGB(to_u8(a), to_u8(r), to_u8(g), to_u8(b));
}

static std::unique_ptr<tl::Paragraph> build_paragraph(
    const std::string& text,
    float text_size,
    SkColor color,
    const sk_sp<tl::FontCollection>& fonts
) {
    tl::ParagraphStyle paragraph_style;
    tl::TextStyle text_style;
    text_style.setFontSize(text_size);
    text_style.setColor(color);
    text_style.setFontFamilies({SkString("sans-serif")});

    auto builder = tl::ParagraphBuilder::make(paragraph_style, fonts);
    builder->pushStyle(text_style);
    builder->addText(text);
    builder->pop();
    return builder->Build();
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
        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRect(SkRect::MakeXYWH(0.0f, 0.0f, props.width, props.height), fill);
    }
};

class ButtonRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<ButtonProps>(node.current_vnode.props);
        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, props.width, props.height);

        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRoundRect(bounds, 8.0f, 8.0f, fill);

        auto paragraph = build_paragraph(
            props.label,
            props.text_size,
            make_color(props.text_r, props.text_g, props.text_b),
            ctx.fonts
        );
        paragraph->layout(props.width - 16.0f);

        const float text_x = 8.0f;
        const float text_y = (props.height - paragraph->getHeight()) * 0.5f;
        paragraph->paint(canvas, text_x, text_y);
    }
};

class TextRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<TextProps>(node.current_vnode.props);
        const float layout_width = props.width > 1.0f
            ? props.width
            : std::max(1.0f, static_cast<float>(ctx.surface_width));
        auto paragraph = build_paragraph(
            props.text,
            props.text_size,
            make_color(props.text_r, props.text_g, props.text_b),
            ctx.fonts
        );
        paragraph->layout(layout_width);
        paragraph->paint(canvas, 0.0f, 0.0f);
    }
};

class InputRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<InputProps>(node.current_vnode.props);

        const std::string value = node.input_state ? node.input_state->value : props.value;
        const std::size_t cursor = node.input_state ? node.input_state->cursor : value.size();
        const bool focused = node.input_state && node.input_state->focused;

        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, props.width, props.height);
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

        auto paragraph = build_paragraph(text, props.text_size, text_color, ctx.fonts);
        paragraph->layout(props.width - 16.0f);
        const float text_x = 8.0f;
        const float text_y = (props.height - paragraph->getHeight()) * 0.5f;
        paragraph->paint(canvas, text_x, text_y);

        if (focused) {
            const std::string left_text = value.substr(0, std::min(cursor, value.size()));
            auto left_para = build_paragraph(left_text, props.text_size, text_color, ctx.fonts);
            left_para->layout(props.width - 16.0f);
            const float cursor_x = text_x + left_para->getMaxIntrinsicWidth();
            SkPaint caret;
            caret.setColor(make_color(0.2f, 0.2f, 0.2f));
            caret.setStrokeWidth(1.5f);
            canvas->drawLine(cursor_x, 8.0f, cursor_x, props.height - 8.0f, caret);
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

static SkRect node_bounds(const Element& vnode, int surface_width, int surface_height) {
    const auto props = props_as_view(vnode);
    const float w = std::max(props.width, 1.0f);
    const float h = std::max(props.height, 1.0f);
    const float x = std::clamp(props.x, -10000.0f, static_cast<float>(surface_width) + 10000.0f);
    const float y = std::clamp(props.y, -10000.0f, static_cast<float>(surface_height) + 10000.0f);
    return SkRect::MakeXYWH(x, y, w, h);
}

class SkiaRuntime {
public:
    explicit SkiaRuntime(AppRenderFunc app)
        : app_render_(std::move(app)) {
        font_collection_ = sk_make_sp<tl::FontCollection>();
        font_collection_->setDefaultFontManager(SkFontMgr::RefDefault());
    }

    Element render_frame() {
        g_skia_dispatcher.current_instance = &root_instance_;
        g_skia_dispatcher.current_index = 0;

        Element new_root = app_render_();
        reconcile(root_instance_, new_root);
        return root_instance_.current_vnode;
    }

    void draw(SkCanvas* canvas, int width, int height) {
        DrawContext ctx;
        ctx.surface_width = width;
        ctx.surface_height = height;
        ctx.fonts = font_collection_;

        canvas->clear(SK_ColorWHITE);
        render_cached_node(root_instance_, canvas, ctx);
    }

    void handle_mouse_down(float x, float y) {
        InstanceNode* hit = find_input_at(root_instance_, x, y);
        set_focus(hit);
    }

    void handle_text_input(const char* text) {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        const std::string inserted(text ? text : "");
        state.value.insert(state.cursor, inserted);
        state.cursor += inserted.size();
        mark_dirty(focused_input_);
    }

    void handle_backspace() {
        if (!focused_input_ || !focused_input_->input_state) return;
        auto& state = *focused_input_->input_state;
        if (state.cursor == 0 || state.value.empty()) return;
        state.value.erase(state.cursor - 1, 1);
        state.cursor -= 1;
        mark_dirty(focused_input_);
    }

private:
    static bool is_descendant_or_self(const InstanceNode* node, const InstanceNode* possible_ancestor) {
        for (auto* current = node; current != nullptr; current = current->parent) {
            if (current == possible_ancestor) {
                return true;
            }
        }
        return false;
    }

    static bool point_in_bounds(const InstanceNode& node, float x, float y) {
        const auto p = props_as_view(node.current_vnode);
        return x >= p.x && x <= (p.x + p.width) && y >= p.y && y <= (p.y + p.height);
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

    InstanceNode* find_input_at(InstanceNode& node, float x, float y) {
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            if (InstanceNode* found = find_input_at(*it->get(), x, y)) {
                return found;
            }
        }

        if (node.type == host_type_input() && point_in_bounds(node, x, y)) {
            return &node;
        }
        return nullptr;
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
            if (focused_input_ && is_descendant_or_self(focused_input_, &inst)) {
                focused_input_ = nullptr;
            }
            inst.type = vnode.type;
            inst.current_vnode = vnode;
            inst.hooks.clear();
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
        const auto props = props_as_view(vnode);
        const float w = std::max(props.width, 1.0f);
        const float h = std::max(props.height, 1.0f);
        return SkRect::MakeXYWH(0.0f, 0.0f, w, h);
    }

    void render_cached_node(InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) {
        const auto p = props_as_view(node.current_vnode);
        canvas->save();
        canvas->translate(p.x, p.y);

        if (node.dirty || !node.cached_picture) {
            SkPictureRecorder recorder;
            const SkRect bounds = local_recording_bounds(node.current_vnode);
            SkCanvas* record_canvas = recorder.beginRecording(bounds);

            renderer_for(node.type).on_draw(node, record_canvas, ctx);

            sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();
            node.cached_picture = std::shared_ptr<SkPicture>(
                picture.release(),
                [](SkPicture* p) {
                    if (p) p->unref();
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
    sk_sp<tl::FontCollection> font_collection_;
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
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error("SDL_Init failed");
    }

    const int width = 800;
    const int height = 600;

#if REACTCPP_USE_SDL3
    SDL_Window* window = SDL_CreateWindow(
        "Skia Reactive Demo",
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );
#else
    SDL_Window* window = SDL_CreateWindow(
        "Skia Reactive Demo",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );
#endif
    if (!window) {
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed");
    }

#if REACTCPP_USE_SDL3
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
#else
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
#endif
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SDL_CreateRenderer failed");
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
        throw std::runtime_error("SDL_CreateTexture failed");
    }

    SkImageInfo info = SkImageInfo::Make(
        width,
        height,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType
    );

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    auto surface = SkSurface::MakeRasterDirect(
        info,
        pixels.data(),
        width * 4
    );
    if (!surface) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SkSurface::MakeRasterDirect failed");
    }

    SkiaRuntime runtime(app);
    runtime.render_frame();
    SDL_StartTextInput();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) == 1) {
            if (e.type == REACTCPP_SDL_EVENT_QUIT) {
                running = false;
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                runtime.handle_mouse_down(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_INPUT) {
                runtime.handle_text_input(e.text.text);
            } else if (e.type == REACTCPP_SDL_EVENT_KEY_DOWN && e.key.keysym.sym == SDLK_BACKSPACE) {
                runtime.handle_backspace();
            }
        }

        runtime.render_frame();

        SkCanvas* canvas = surface->getCanvas();
        runtime.draw(canvas, width, height);

        void* texPixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture, nullptr, &texPixels, &pitch) != 0) {
            break;
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
#if REACTCPP_USE_SDL3
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
#else
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
#endif
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_StopTextInput();
    SDL_Quit();
    return 0;
}
