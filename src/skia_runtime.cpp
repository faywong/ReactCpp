#include "skia_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "text_edit.hpp"
#include "text_wrap.hpp"

#include <yoga/Yoga.h>

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkFont.h"
#include "core/SkFontMetrics.h"
#include "core/SkFontMgr.h"
#include "core/SkFontStyle.h"
#include "core/SkPaint.h"
#include "core/SkPicture.h"
#include "core/SkPictureRecorder.h"
#include "core/SkPoint.h"
#include "core/SkRRect.h"
#include "core/SkRect.h"
#include "core/SkRefCnt.h"
#include "core/SkSpan.h"
#include "core/SkSurface.h"
#include "core/SkTypeface.h"

#include "gpu/ganesh/GrDirectContext.h"
#include "gpu/ganesh/GrBackendSurface.h"
#include "gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "gpu/ganesh/gl/GrGLDirectContext.h"
#include "gpu/ganesh/gl/GrGLInterface.h"
#include "gpu/ganesh/gl/GrGLTypes.h"
#include "gpu/ganesh/SkSurfaceGanesh.h"

#include "effects/SkGradientShader.h"
#include "effects/SkDashPathEffect.h"

#if defined(__linux__) || defined(__CYGWIN__)
#include "ports/SkFontMgr_fontconfig.h"
#include "ports/SkFontScanner_FreeType.h"

#include <fontconfig/fontconfig.h>
#elif defined(__APPLE__)
#include "ports/SkFontMgr_mac_ct.h"
#endif

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3/SDL_opengl.h>

#include "render_thread.hpp"

#include "rive/animation/state_machine_input_instance.hpp"
#include "rive/animation/state_machine_instance.hpp"
#include "rive/artboard.hpp"
#include "rive/file.hpp"
#include "rive/static_scene.hpp"
#include "rive/viewmodel/viewmodel_instance.hpp"
#include "skia_factory.hpp"
#include "skia_renderer.hpp"


#define REACTCPP_SDL_EVENT_QUIT SDL_EVENT_QUIT
#define REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN SDL_EVENT_MOUSE_BUTTON_DOWN
#define REACTCPP_SDL_EVENT_MOUSE_BUTTON_UP SDL_EVENT_MOUSE_BUTTON_UP
#define REACTCPP_SDL_EVENT_MOUSE_MOTION SDL_EVENT_MOUSE_MOTION
#define REACTCPP_SDL_EVENT_TEXT_INPUT SDL_EVENT_TEXT_INPUT
#define REACTCPP_SDL_EVENT_TEXT_EDITING SDL_EVENT_TEXT_EDITING
#define REACTCPP_SDL_EVENT_KEY_DOWN SDL_EVENT_KEY_DOWN
#define REACTCPP_SDL_EVENT_MOUSE_WHEEL SDL_EVENT_MOUSE_WHEEL

thread_local HookDispatcher g_skia_dispatcher;

HookDispatcher& get_hook_dispatcher() {
    return g_skia_dispatcher;
}

namespace {

std::atomic<reactcpp::UiEventQueue*> g_repaint_event_queue{nullptr};
std::atomic<bool> g_runtime_repaint_requested{false};
std::atomic<bool> g_repaint_tick_pending{false};

void request_repaint_impl() {
    g_runtime_repaint_requested.store(true, std::memory_order_release);
    if (reactcpp::UiEventQueue* queue = g_repaint_event_queue.load(std::memory_order_acquire)) {
        if (!g_repaint_tick_pending.exchange(true, std::memory_order_acq_rel)) {
            queue->push(reactcpp::UiEvent{reactcpp::UiEventType::Tick});
        }
    }
}

void register_repaint_queue(reactcpp::UiEventQueue* queue) {
    g_repaint_event_queue.store(queue, std::memory_order_release);
}

void unregister_repaint_queue(reactcpp::UiEventQueue* queue) {
    reactcpp::UiEventQueue* expected = queue;
    g_repaint_event_queue.compare_exchange_strong(expected, nullptr);
}

bool take_runtime_repaint_request() {
    return g_runtime_repaint_requested.exchange(false, std::memory_order_acq_rel);
}

void clear_repaint_tick_pending() {
    g_repaint_tick_pending.store(false, std::memory_order_release);
}
}

namespace reactcpp {

void request_repaint() {
    request_repaint_impl();
}

} // namespace reactcpp

static SkRect make_safe_rect(float x, float y, float w, float h) {
    const float ww = std::max(w, 1.0f);
    const float hh = std::max(h, 1.0f);
    return SkRect::MakeXYWH(x, y, ww, hh);
}

namespace {

struct DrawContext {
    int surface_width{0};
    int surface_height{0};
    sk_sp<SkFontMgr> font_mgr;
};

thread_local sk_sp<SkFontMgr> g_font_mgr;

static sk_sp<SkTypeface> pick_typeface(const sk_sp<SkFontMgr>&);
static SkColor make_color(float r, float g, float b, float a);
static void draw_chart_empty_message(SkCanvas* canvas, const DrawContext& ctx, const LayoutRect& plot, const char* text);

struct RiveDrawData {
    reactcpp::RiveInputs::Snapshot inputs;
    std::string error;
    bool rendered{false};
};

struct RiveRuntimeState {
    std::string source;
    std::string artboard_name;
    std::string state_machine_name;
    rive::rcp<rive::File> file;
    std::unique_ptr<rive::ArtboardInstance> artboard;
    std::unique_ptr<rive::Scene> scene;
    rive::rcp<rive::ViewModelInstance> view_model_instance;
    rive::SkiaFactory factory;
    std::chrono::steady_clock::time_point last_advance{};
    bool wants_repaint{false};
    std::string error;
};

static std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) {
        return {};
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in) {
        return {};
    }
    return out;
}

static RiveRuntimeState* rive_state_for(const InstanceNode& node) {
    return static_cast<RiveRuntimeState*>(node.native_state.get());
}

static std::shared_ptr<RiveRuntimeState> ensure_rive_state(const InstanceNode& node, const RiveProps& props) {
    auto state = std::static_pointer_cast<RiveRuntimeState>(node.native_state);
    const bool matches = state
        && state->source == props.source
        && state->artboard_name == props.artboard
        && state->state_machine_name == props.state_machine;
    if (matches) {
        return state;
    }

    state = std::make_shared<RiveRuntimeState>();
    state->source = props.source;
    state->artboard_name = props.artboard;
    state->state_machine_name = props.state_machine;
    node.native_state = state;

    if (props.source.empty()) {
        state->error = "Rive: source is empty";
        return state;
    }

    std::vector<std::uint8_t> bytes = read_binary_file(props.source);
    if (bytes.empty()) {
        state->error = "Rive: could not read " + props.source;
        return state;
    }

    rive::ImportResult result = rive::ImportResult::malformed;
    state->file = rive::File::import(bytes, &state->factory, &result);
    if (!state->file || result != rive::ImportResult::success) {
        state->error = "Rive: import failed for " + props.source;
        return state;
    }

    state->artboard = props.artboard.empty()
        ? state->file->artboardDefault()
        : state->file->artboardNamed(props.artboard);
    if (!state->artboard) {
        state->error = "Rive: artboard not found";
        return state;
    }

    state->view_model_instance = state->file->createViewModelInstance(state->artboard.get());
    if (state->view_model_instance) {
        state->artboard->bindViewModelInstance(state->view_model_instance);
    }

    state->scene = props.state_machine.empty()
        ? state->artboard->defaultStateMachine()
        : state->artboard->stateMachineNamed(props.state_machine);
    if (!state->scene && state->artboard->stateMachineCount() > 0) {
        state->scene = state->artboard->stateMachineAt(0);
    }
    if (!state->scene) {
        state->scene = std::make_unique<rive::StaticScene>(state->artboard.get());
    }
    if (state->scene && state->view_model_instance) {
        state->scene->bindViewModelInstance(state->view_model_instance);
    }

    state->artboard->advance(0.0f);
    if (state->scene) {
        state->wants_repaint = state->scene->advanceAndApply(0.0f);
    }
    state->last_advance = std::chrono::steady_clock::now();
    return state;
}

static bool rive_wants_repaint(const InstanceNode& node) {
    if (node.type != host_type_rive()) {
        return false;
    }
    const auto* state = rive_state_for(node);
    return state && state->wants_repaint;
}

static void apply_rive_inputs(rive::Scene* scene, const reactcpp::RiveInputs::Snapshot& inputs) {
    if (!scene) {
        return;
    }
    for (const auto& [name, value] : inputs.numbers) {
        if (auto* input = scene->getNumber(name)) {
            input->value(static_cast<float>(value));
        }
    }
    for (const auto& [name, value] : inputs.bools) {
        if (auto* input = scene->getBool(name)) {
            input->value(value);
        }
    }
}

static bool draw_rive_backend(
    const InstanceNode& node,
    const RiveProps& props,
    SkCanvas* canvas,
    const LayoutRect& r,
    RiveDrawData& data
) {
    auto state = ensure_rive_state(node, props);
    if (!state || !state->artboard || !state->scene || !state->error.empty()) {
        data.error = state ? state->error : "Rive: failed to create runtime state";
        return false;
    }

    apply_rive_inputs(state->scene.get(), data.inputs);

    const auto now = std::chrono::steady_clock::now();
    double dt = 0.0;
    if (state->last_advance.time_since_epoch().count() != 0) {
        dt = std::chrono::duration<double>(now - state->last_advance).count();
    }
    state->last_advance = now;
    dt = std::clamp(dt * data.inputs.time_scale, 0.0, 0.25);

    bool keep_animating = false;
    keep_animating = state->scene->advanceAndApply(static_cast<float>(dt));

    const float art_w = std::max(state->scene->width(), 1.0f);
    const float art_h = std::max(state->scene->height(), 1.0f);
    const float scale = std::min(r.width / art_w, r.height / art_h);
    const float draw_w = art_w * scale;
    const float draw_h = art_h * scale;
    const float dx = (r.width - draw_w) * 0.5f;
    const float dy = (r.height - draw_h) * 0.5f;

    canvas->save();
    canvas->translate(dx, dy);
    canvas->scale(scale, scale);
    rive::SkiaRenderer renderer(canvas);
    state->scene->draw(&renderer);
    canvas->restore();

    state->wants_repaint = keep_animating;
    if (state->wants_repaint) {
        reactcpp::request_repaint();
    }
    data.rendered = true;
    return true;
}

static bool is_finite_double(double v) {
    return std::isfinite(v);
}

static float clamp_f(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

static std::string trim_chart_value_zeros(std::string value) {
    const auto dot_pos = value.find('.');
    if (dot_pos == std::string::npos) {
        return value;
    }
    auto end = value.size();
    while (end > dot_pos + 1 && value[end - 1] == '0') {
        --end;
    }
    if (end > dot_pos + 1 && value[end - 1] == '.') {
        --end;
    }
    value.erase(end);
    if (value.empty()) {
        value = "0";
    }
    return value;
}

static std::string format_chart_tick_label(double value) {
    if (!is_finite_double(value)) {
        return "";
    }

    std::ostringstream oss;
    const double abs_val = std::fabs(value);
    if ((abs_val >= 1e4 && abs_val < std::numeric_limits<double>::infinity()) ||
        (abs_val > 0.0 && abs_val < 1e-3)) {
        oss << std::scientific << std::setprecision(4) << value;
    } else if (abs_val >= 1e3) {
        oss << std::fixed << std::setprecision(1) << value;
    } else {
        oss << std::fixed << std::setprecision(4) << value;
    }
    return trim_chart_value_zeros(oss.str());
}

static ChartStyle resolve_chart_style(const ChartStyle& in) {
    ChartStyle out = in;
    apply_chart_theme_preset(out, out.theme);
    out.tick_count = std::max(2, out.tick_count);
    return out;
}

struct LineChartResolvedColors {
    float line_r{0.067f};
    float line_g{0.204f};
    float line_b{0.561f};
    float line_a{1.0f};

    float marker_r{0.067f};
    float marker_g{0.204f};
    float marker_b{0.561f};
    float marker_a{1.0f};

    float fill_r{0.067f};
    float fill_g{0.204f};
    float fill_b{0.561f};
    float fill_a{0.16f};
};

static LineChartResolvedColors resolve_line_colors(const LineChartProps& props, const ChartStyle& resolved_style) {
    LineChartResolvedColors colors;

    if (props.line_color_set) {
        colors.line_r = props.line_r;
        colors.line_g = props.line_g;
        colors.line_b = props.line_b;
        colors.line_a = props.line_a;
    } else if (props.palette_set) {
        colors.line_r = props.palette_r;
        colors.line_g = props.palette_g;
        colors.line_b = props.palette_b;
        colors.line_a = props.palette_a;
    } else {
        colors.line_r = resolved_style.palette_line_r;
        colors.line_g = resolved_style.palette_line_g;
        colors.line_b = resolved_style.palette_line_b;
        colors.line_a = resolved_style.palette_line_a;
    }

    if (props.marker_color_set) {
        colors.marker_r = props.marker_r;
        colors.marker_g = props.marker_g;
        colors.marker_b = props.marker_b;
        colors.marker_a = props.marker_a;
    } else if (props.palette_set) {
        colors.marker_r = props.palette_r;
        colors.marker_g = props.palette_g;
        colors.marker_b = props.palette_b;
        colors.marker_a = props.palette_a;
    } else {
        colors.marker_r = resolved_style.palette_marker_r;
        colors.marker_g = resolved_style.palette_marker_g;
        colors.marker_b = resolved_style.palette_marker_b;
        colors.marker_a = resolved_style.palette_marker_a;
    }

    colors.fill_r = resolved_style.palette_fill_r;
    colors.fill_g = resolved_style.palette_fill_g;
    colors.fill_b = resolved_style.palette_fill_b;
    colors.fill_a = resolved_style.palette_fill_a;

    return colors;
}

static std::vector<reactcpp::LinePoint> resolve_line_points(const LineChartProps& props) {
    if (props.points_source) {
        return props.points_source->snapshot();
    }
    if (!props.points.empty()) {
        return props.points;
    }
    if (!props.x.empty() && !props.y.empty()) {
        const std::size_t n = std::min(props.x.size(), props.y.size());
        std::vector<reactcpp::LinePoint> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(reactcpp::LinePoint{props.x[i], props.y[i]});
        }
        return out;
    }
    return {};
}

static std::vector<double> resolve_series(
    const std::vector<double>& static_data,
    const std::shared_ptr<const reactcpp::VectorDataSource<double>>& source
) {
    if (source) {
        return source->snapshot();
    }
    return static_data;
}

static void sanitize_range(double& lo, double& hi) {
    if (!is_finite_double(lo) || !is_finite_double(hi)) {
        lo = 0.0;
        hi = 1.0;
        return;
    }
    if (hi < lo) {
        std::swap(lo, hi);
    }
    if (lo == hi) {
        lo -= 0.5;
        hi += 0.5;
        if (lo == hi) {
            lo -= 0.5;
            hi += 0.5;
        }
    }
}

static void grow_range_from_double(double v, double& lo, double& hi, bool& init) {
    if (!is_finite_double(v)) {
        return;
    }
    if (!init) {
        lo = hi = v;
        init = true;
        return;
    }
    if (v < lo) lo = v;
    if (v > hi) hi = v;
}

static void draw_chart_background(SkCanvas* canvas, const LayoutRect& r, const ViewProps& view_props) {
    const SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height);
    SkPaint fill;
    fill.setAntiAlias(true);
    fill.setColor(make_color(view_props.bg_r, view_props.bg_g, view_props.bg_b, view_props.bg_a));
    canvas->drawRect(bounds, fill);
}

static void draw_grid_lines(
    SkCanvas* canvas,
    const DrawContext& ctx,
    const LayoutRect& host,
    float left,
    float top,
    float width,
    float height,
    double x_min,
    double x_max,
    double y_min,
    double y_max,
    const ChartStyle& style_in
) {
    if (width <= 0.0f || height <= 0.0f) return;
    if (host.width <= 0.0f || host.height <= 0.0f) return;

    const ChartStyle style = resolve_chart_style(style_in);

    const float sx = width / static_cast<float>(x_max - x_min);
    const float sy = height / static_cast<float>(y_max - y_min);

    const auto map_x = [&](double x) -> float {
        return left + static_cast<float>(x - x_min) * sx;
    };
    const auto map_y = [&](double y) -> float {
        return top + height - static_cast<float>(y - y_min) * sy;
    };

    const float axis_x = (0.0 >= x_min && 0.0 <= x_max)
        ? map_x(0.0)
        : left;
    const float axis_y = (0.0 >= y_min && 0.0 <= y_max)
        ? map_y(0.0)
        : top + height;

    const bool show_grid = style.draw_grid;
    const ChartGrid grid_mode = style.grid_mode;

    const SkColor axis_color = make_color(style.axis_r, style.axis_g, style.axis_b, style.axis_a);
    const SkColor grid_color = make_color(style.grid_r, style.grid_g, style.grid_b, style.grid_a);
    const SkColor tick_label_color = make_color(style.tick_label_r, style.tick_label_g, style.tick_label_b, style.tick_label_a);
    const SkColor title_color = make_color(style.title_r, style.title_g, style.title_b, style.title_a);
    const SkColor axis_label_color = make_color(style.axis_label_r, style.axis_label_g, style.axis_label_b, style.axis_label_a);

    if (style.draw_axes) {
        SkPaint axis;
        axis.setColor(axis_color);
        axis.setStyle(SkPaint::kStroke_Style);
        axis.setStrokeWidth(std::max(0.5f, style.axis_width));
        axis.setAntiAlias(true);
        canvas->drawLine(left, axis_y, left + width, axis_y, axis);
        canvas->drawLine(axis_x, top, axis_x, top + height, axis);
    }

    if (show_grid && grid_mode != ChartGrid::None) {
        SkPaint grid;
        grid.setColor(grid_color);
        grid.setStyle(SkPaint::kStroke_Style);
        grid.setStrokeWidth(std::max(0.4f, style.grid_width));
        grid.setAntiAlias(true);
        if (style.grid_dashed) {
            grid.setPathEffect(SkDashPathEffect::Make({4.0f, 3.0f}, 0));
        }

        const int ticks = std::max(2, style.tick_count);
        if (grid_mode == ChartGrid::Vertical || grid_mode == ChartGrid::Both) {
            for (int i = 0; i <= ticks; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(ticks);
                const float gx = left + width * t;
                canvas->drawLine(gx, top, gx, top + height, grid);
            }
        }
        if (grid_mode == ChartGrid::Horizontal || grid_mode == ChartGrid::Both) {
            for (int i = 0; i <= ticks; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(ticks);
                const float gy = top + height * t;
                canvas->drawLine(left, gy, left + width, gy, grid);
            }
        }
    }

    if (style.show_tick_labels) {
        SkFont tick_font;
        tick_font.setSize(std::max(8.0f, style.tick_label_size));
        tick_font.setTypeface(pick_typeface(ctx.font_mgr));

        SkPaint tick_paint;
        tick_paint.setAntiAlias(true);
        tick_paint.setColor(tick_label_color);

        const int ticks = std::max(2, style.tick_count);
        for (int i = 0; i <= ticks; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(ticks);
            const double xv = x_min + (x_max - x_min) * static_cast<double>(t);
            const double yv = y_min + (y_max - y_min) * static_cast<double>(t);
            const float gx = left + width * t;
            const float gy = top + height * t;

            if (grid_mode == ChartGrid::Vertical || grid_mode == ChartGrid::Both) {
                const std::string label = format_chart_tick_label(xv);
                const float tw = tick_font.measureText(label.c_str(), label.size(), SkTextEncoding::kUTF8);
                const float lx = gx - tw * 0.5f;
                const float ly = top + height + tick_font.getSize() + 4.0f;
                canvas->drawString(label.c_str(), std::max(0.0f, lx), ly, tick_font, tick_paint);
            }

            if (grid_mode == ChartGrid::Horizontal || grid_mode == ChartGrid::Both) {
                const std::string label = format_chart_tick_label(yv);
                const float tw = tick_font.measureText(label.c_str(), label.size(), SkTextEncoding::kUTF8);
                const float ly = gy + tick_font.getSize() * 0.35f;
                const float lx = std::max(0.0f, left - 4.0f - tw);
                canvas->drawString(label.c_str(), lx, ly, tick_font, tick_paint);
            }
        }
    }

    if (style.show_title && !style.title.empty()) {
        SkFont title_font;
        title_font.setSize(std::max(12.0f, style.title_size));
        title_font.setTypeface(pick_typeface(ctx.font_mgr));

        SkPaint title_paint;
        title_paint.setAntiAlias(true);
        title_paint.setColor(title_color);

        const float title_w = title_font.measureText(style.title.c_str(), style.title.size(), SkTextEncoding::kUTF8);
        const float tx = host.x + (host.width - title_w) * 0.5f;
        canvas->drawString(style.title.c_str(), std::max(0.0f, tx), title_font.getSize(), title_font, title_paint);
    }

    if (style.show_axis_labels) {
        SkFont axis_font;
        axis_font.setSize(std::max(10.0f, style.axis_label_size));
        axis_font.setTypeface(pick_typeface(ctx.font_mgr));

        SkPaint axis_paint;
        axis_paint.setAntiAlias(true);
        axis_paint.setColor(axis_label_color);

        if (!style.x_label.empty()) {
            const float label_w = axis_font.measureText(style.x_label.c_str(), style.x_label.size(), SkTextEncoding::kUTF8);
            const float lx = host.x + (host.width - label_w) * 0.5f;
            const float ly = top + height + axis_font.getSize() * 1.75f + (style.show_tick_labels ? style.tick_label_size : 0.0f);
            canvas->drawString(style.x_label.c_str(), std::max(0.0f, lx), ly, axis_font, axis_paint);
        }

        if (!style.y_label.empty()) {
            const float lx = std::max(0.0f, host.x + 4.0f);
            const float ly = top + height * 0.5f;
            const float label_w = axis_font.measureText(style.y_label.c_str(), style.y_label.size(), SkTextEncoding::kUTF8);
            canvas->save();
            canvas->translate(lx, ly + label_w * 0.5f);
            canvas->rotate(-90.0f);
            canvas->drawString(style.y_label.c_str(), 0.0f, 0.0f, axis_font, axis_paint);
            canvas->restore();
        }
    }
}

static void draw_marker(
    SkCanvas* canvas,
    float x,
    float y,
    ChartMarker kind,
    float size,
    SkColor fill_color
) {
    if (size <= 0.001f) return;
    const float half = std::max(0.2f, size * 0.5f);
    if (kind == ChartMarker::Square) {
        SkRect rr = SkRect::MakeLTRB(x - half, y - half, x + half, y + half);
        SkPaint p;
        p.setAntiAlias(true);
        p.setStyle(SkPaint::kFill_Style);
        p.setColor(fill_color);
        canvas->drawRect(rr, p);
    } else if (kind == ChartMarker::Triangle) {
        const SkPoint tri_points[3] = {
            SkPoint::Make(x, y - half),
            SkPoint::Make(x + half, y + half),
            SkPoint::Make(x - half, y + half),
        };
        const SkPath tri_path = SkPath::Polygon(tri_points, true);
        SkPaint p;
        p.setAntiAlias(true);
        p.setStyle(SkPaint::kFill_Style);
        p.setColor(fill_color);
        canvas->drawPath(tri_path, p);
    } else {
        SkPaint p;
        p.setAntiAlias(true);
        p.setStyle(SkPaint::kFill_Style);
        p.setColor(fill_color);
        canvas->drawCircle(x, y, half, p);
    }
}

static std::vector<double> histogram_compute_edges(
    const std::vector<double>& samples,
    std::size_t fixed_bin_count,
    HistogramAlgorithm algorithm
) {
    std::vector<double> finite;
    finite.reserve(samples.size());
    for (double v : samples) {
        if (is_finite_double(v)) finite.push_back(v);
    }
    if (finite.empty()) return {};

    std::sort(finite.begin(), finite.end());
    finite.erase(std::unique(finite.begin(), finite.end()), finite.end());
    if (finite.empty()) return {};

    double lo = finite.front();
    double hi = finite.back();

    if (lo == hi) {
        const double start = std::floor(lo) - 0.5;
        return {start, start + 1.0};
    }

    auto select_algorithm = algorithm;
    if (algorithm == HistogramAlgorithm::Automatic) {
        bool integer_like = true;
        for (double v : finite) {
            if (!is_finite_double(v) || std::fabs(v - std::round(v)) > 1e-6) {
                integer_like = false;
                break;
            }
        }
        select_algorithm = integer_like ? HistogramAlgorithm::Integers : HistogramAlgorithm::Scott;
    }

    std::size_t bins = 10;
    if (fixed_bin_count > 0) {
        bins = std::max<std::size_t>(1, fixed_bin_count);
    } else {
        const std::size_t n = finite.size();
        switch (select_algorithm) {
        case HistogramAlgorithm::Scott: {
            double sum = 0.0;
            for (double v : samples) sum += v;
            const double mean = sum / static_cast<double>(samples.size());
            double m2 = 0.0;
            for (double v : samples) {
                const double d = v - mean;
                m2 += d * d;
            }
            const double stddev = std::sqrt(m2 / std::max<std::size_t>(1, samples.size()));
            double width = (stddev > 0.0) ? 3.5 * stddev / std::pow(samples.size(), 1.0 / 3.0) : 0.0;
            bins = width > 0.0 ? static_cast<std::size_t>(std::ceil((hi - lo) / width)) : 10;
            break;
        }
        case HistogramAlgorithm::FD: {
            auto q = [&](double p) {
                if (finite.empty()) return 0.0;
                const double scaled = p * (static_cast<double>(finite.size() - 1));
                const std::size_t i0 = static_cast<std::size_t>(std::floor(scaled));
                const std::size_t i1 = std::min(i0 + 1, finite.size() - 1);
                const double t = scaled - static_cast<double>(i0);
                return finite[i0] + (finite[i1] - finite[i0]) * t;
            };
            const double q25 = q(0.25);
            const double q75 = q(0.75);
            const double iqr = q75 - q25;
            const double width = iqr > 0.0 ? 2.0 * iqr / std::pow(samples.size(), 1.0 / 3.0) : 0.0;
            bins = width > 0.0 ? static_cast<std::size_t>(std::ceil((hi - lo) / width)) : 10;
            break;
        }
        case HistogramAlgorithm::Sturges:
            bins = static_cast<std::size_t>(std::ceil(std::log2(std::max<std::size_t>(1, finite.size()))) + 1);
            break;
        case HistogramAlgorithm::Sqrt:
            bins = static_cast<std::size_t>(std::ceil(std::sqrt(std::max<std::size_t>(1, finite.size()))));
            break;
        case HistogramAlgorithm::Integers: {
            const double start = std::floor(lo);
            const double end = std::ceil(hi);
            bins = static_cast<std::size_t>(std::max(1.0, end - start));
            lo = start;
            hi = end;
            break;
        }
        case HistogramAlgorithm::Automatic:
            bins = 10;
            break;
        }
    }

    bins = std::max<std::size_t>(1, bins);

    if (select_algorithm == HistogramAlgorithm::Integers) {
        std::vector<double> edges;
        edges.reserve(static_cast<std::size_t>(bins) + 1);
        const double start = std::floor(lo);
        for (std::size_t i = 0; i <= bins; ++i) {
            edges.push_back(start + static_cast<double>(i));
        }
        if (edges.front() > lo) edges.front() = lo;
        if (edges.back() < hi) edges.back() = hi;
        if (edges.size() >= 2) return edges;
    }

    const double span = hi - lo;
    const double width = span / static_cast<double>(bins);
    std::vector<double> edges;
    edges.reserve(bins + 1);
    for (std::size_t i = 0; i <= bins; ++i) {
        edges.push_back(lo + width * static_cast<double>(i));
    }
    edges.front() = std::min(edges.front(), lo);
    edges.back() = std::max(edges.back(), hi);
    return edges;
}

static std::vector<double> histogram_counts(const std::vector<double>& samples, const std::vector<double>& edges) {
    if (edges.size() < 2) return {};
    std::vector<double> counts(std::max<std::size_t>(1, edges.size() - 1), 0.0);
    if (samples.empty()) return counts;
    for (double s : samples) {
        if (!is_finite_double(s)) continue;
        auto it = std::upper_bound(edges.begin(), edges.end(), s);
        if (it == edges.begin()) {
            continue;
        }
        if (it == edges.end()) {
            if (s == edges.back()) {
                counts.back() += 1.0;
            }
            continue;
        }
        const std::size_t i = static_cast<std::size_t>(it - edges.begin()) - 1;
        if (i < counts.size()) counts[i] += 1.0;
    }
    return counts;
}

static std::vector<double> histogram_normalize(
    const std::vector<double>& counts,
    const std::vector<double>& edges,
    HistogramNormalization norm
) {
    if (counts.empty()) return {};
    std::vector<double> out(counts.size(), 0.0);
    double total = 0.0;
    for (double c : counts) total += c;
    if (total <= 0.0) return out;

    if (norm == HistogramNormalization::Count) {
        return counts;
    }

    if (norm == HistogramNormalization::CDF) {
        double acc = 0.0;
        for (std::size_t i = 0; i < counts.size(); ++i) {
            acc += counts[i];
            out[i] = acc / total;
        }
        return out;
    }

    if (norm == HistogramNormalization::Probability) {
        for (std::size_t i = 0; i < counts.size(); ++i) {
            out[i] = counts[i] / total;
        }
        return out;
    }

    for (std::size_t i = 0; i < counts.size(); ++i) {
        const double width = std::max(1e-12, edges[i + 1] - edges[i]);
        out[i] = counts[i] / (total * width);
    }
    return out;
}
static SDL_DisplayID pick_primary_display_id() {
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (primary != 0) return primary;

    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID first = 0;
    if (displays) {
        first = displays[0];
        SDL_free(displays);
    }
    return first;
}

static std::pair<int, int> compute_initial_window_size_60pct() {
    const SDL_DisplayID display = pick_primary_display_id();

    int base_w = 0;
    int base_h = 0;

    if (display != 0) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(display, &usable)) {
            base_w = usable.w;
            base_h = usable.h;
        } else {
            if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(display)) {
                base_w = dm->w;
                base_h = dm->h;
            }
        }
    }

    if (base_w <= 0 || base_h <= 0) {
        base_w = 1280;
        base_h = 720;
    }

    const float kScale = 0.6f;
    int w = static_cast<int>(static_cast<float>(base_w) * kScale);
    int h = static_cast<int>(static_cast<float>(base_h) * kScale);

    w = std::max(w, 640);
    h = std::max(h, 480);

    return {w, h};
}

static void log_gl_to_cpu_fallback(const std::string& reason) {
    const char* driver = SDL_GetCurrentVideoDriver();
    std::fprintf(
        stderr,
        "[reactcpp][WARN][GL->CPU] Ganesh GL init failed; falling back to CPU raster.\n"
        "  reason: %s\n"
        "  sdl_video_driver: %s\n",
        reason.c_str(),
        driver ? driver : "(unknown)"
    );
}

struct FontconfigFontMatch {
    std::string file;
    int ttc_index{0};
};

#if defined(__linux__) || defined(__CYGWIN__)
static std::optional<FontconfigFontMatch> match_cjk_font_with_fontconfig() {
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) return std::nullopt;

    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        FcConfigDestroy(config);
        return std::nullopt;
    }

    FcCharSet* charset = FcCharSetCreate();
    if (!charset) {
        FcPatternDestroy(pattern);
        FcConfigDestroy(config);
        return std::nullopt;
    }

    FcCharSetAddChar(charset, 0x4E2D); // 中
    FcCharSetAddChar(charset, 0x6587); // 文
    FcCharSetAddChar(charset, 0x56FD); // 国
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
    FcCharSetDestroy(charset);

    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    FcPatternAddString(pattern, FC_LANG, reinterpret_cast<const FcChar8*>("zh-cn"));
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>("Noto Sans CJK SC"));
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>("Noto Sans CJK"));
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>("Source Han Sans SC"));
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>("WenQuanYi Micro Hei"));

    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(config, pattern, &result);
    FcPatternDestroy(pattern);

    std::optional<FontconfigFontMatch> out;
    if (match && result == FcResultMatch) {
        FcChar8* file = nullptr;
        int index = 0;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            (void)FcPatternGetInteger(match, FC_INDEX, 0, &index);
            out = FontconfigFontMatch{reinterpret_cast<const char*>(file), index};
        }
    }

    if (match) FcPatternDestroy(match);
    FcConfigDestroy(config);
    return out;
}
#endif

static sk_sp<SkTypeface> pick_typeface(const sk_sp<SkFontMgr>& mgr) {
    if (!mgr) return nullptr;
    thread_local sk_sp<SkTypeface> cjk_typeface;
    if (!cjk_typeface) {
#if defined(__linux__) || defined(__CYGWIN__)
        static const std::optional<FontconfigFontMatch> cjk_match = match_cjk_font_with_fontconfig();
        if (cjk_match) {
            cjk_typeface = mgr->makeFromFile(cjk_match->file.c_str(), cjk_match->ttc_index);
        }
#endif
        if (!cjk_typeface) {
            const char* zh[] = {"zh", "zh-CN", "zh-Hans"};
            cjk_typeface = mgr->matchFamilyStyleCharacter(
                nullptr,
                SkFontStyle::Normal(),
                zh,
                static_cast<int>(sizeof(zh) / sizeof(zh[0])),
                0x4E2D
            );
        }
    }
    if (cjk_typeface) return cjk_typeface;

    sk_sp<SkTypeface> tf = mgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
    if (tf) return tf;
    tf = mgr->matchFamilyStyle("Noto Sans", SkFontStyle::Normal());
    if (tf) return tf;
    return mgr->matchFamilyStyle("DejaVu Sans", SkFontStyle::Normal());
}

static sk_sp<SkFontMgr> create_font_manager() {
#if defined(__linux__) || defined(__CYGWIN__)
    return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#elif defined(__APPLE__)
    return SkFontMgr_New_CoreText(nullptr);
#else
    return nullptr;
#endif
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

static void draw_chart_empty_message(SkCanvas* canvas, const DrawContext& ctx, const LayoutRect& plot, const char* text) {
    SkFont font;
    font.setSize(14.0f);
    font.setTypeface(pick_typeface(ctx.font_mgr));
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(static_cast<SkColor>(0xFF6B7280));
    canvas->drawString(text, 12.0f, 24.0f, font, paint);
    (void)plot;
}

static SkScalar baseline_for_centered_text(const SkFont& font, SkScalar box_height) {
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    const SkScalar text_height = metrics.fDescent - metrics.fAscent;
    return (box_height - text_height) * 0.5f - metrics.fAscent;
}

struct DrawioCell {
    std::string id;
    std::string value;
    std::string style;
    std::string source;
    std::string target;
    bool vertex{false};
    bool edge{false};
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
    bool has_geometry{false};
    std::vector<SkPoint> points;
};

struct ParsedDrawioDiagram {
    std::vector<DrawioCell> cells;
};

static bool starts_with_at(std::string_view s, std::size_t pos, std::string_view needle) {
    return pos <= s.size() && needle.size() <= s.size() - pos && s.substr(pos, needle.size()) == needle;
}

static std::string decode_xml_entities(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }

        const std::size_t semi = in.find(';', i + 1);
        if (semi == std::string_view::npos) {
            out.push_back(in[i]);
            continue;
        }

        const std::string_view ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else {
            out.append(in.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

static std::string strip_html_tags(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    bool in_tag = false;
    std::string tag;
    for (char c : in) {
        if (c == '<') {
            in_tag = true;
            tag.clear();
            continue;
        }
        if (c == '>') {
            std::string lower;
            lower.reserve(tag.size());
            for (char tc : tag) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(tc))));
            }
            if (lower == "br" || lower == "br/" || lower == "/div" || lower == "/p") {
                if (!out.empty() && out.back() != '\n') out.push_back('\n');
            }
            in_tag = false;
            continue;
        }
        if (in_tag) {
            tag.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string xml_attr(std::string_view tag, std::string_view name) {
    std::size_t pos = 0;
    while ((pos = tag.find(name, pos)) != std::string_view::npos) {
        const bool boundary_before = pos == 0 || std::isspace(static_cast<unsigned char>(tag[pos - 1])) || tag[pos - 1] == '<';
        const std::size_t after_name = pos + name.size();
        if (!boundary_before || after_name >= tag.size() || tag[after_name] != '=') {
            pos = after_name;
            continue;
        }

        const std::size_t quote_pos = after_name + 1;
        if (quote_pos >= tag.size() || (tag[quote_pos] != '"' && tag[quote_pos] != '\'')) return {};
        const char quote = tag[quote_pos];
        const std::size_t value_start = quote_pos + 1;
        const std::size_t value_end = tag.find(quote, value_start);
        if (value_end == std::string_view::npos) return {};
        return decode_xml_entities(tag.substr(value_start, value_end - value_start));
    }
    return {};
}

static float xml_attr_float(std::string_view tag, std::string_view name, float fallback = 0.0f) {
    const std::string value = xml_attr(tag, name);
    if (value.empty()) return fallback;
    char* end = nullptr;
    const float out = std::strtof(value.c_str(), &end);
    return end && end != value.c_str() ? out : fallback;
}

static bool xml_attr_bool(std::string_view tag, std::string_view name) {
    const std::string value = xml_attr(tag, name);
    return value == "1" || value == "true";
}

static std::string drawio_style_value(std::string_view style, std::string_view key) {
    std::size_t pos = 0;
    while (pos < style.size()) {
        const std::size_t end = style.find(';', pos);
        const std::string_view item = style.substr(pos, end == std::string_view::npos ? style.size() - pos : end - pos);
        const std::size_t eq = item.find('=');
        if (eq != std::string_view::npos && item.substr(0, eq) == key) {
            return std::string(item.substr(eq + 1));
        }
        if (end == std::string_view::npos) break;
        pos = end + 1;
    }
    return {};
}

static bool drawio_style_flag(std::string_view style, std::string_view key) {
    return drawio_style_value(style, key) == "1";
}

static float drawio_style_float(std::string_view style, std::string_view key, float fallback) {
    const std::string value = drawio_style_value(style, key);
    if (value.empty()) return fallback;
    char* end = nullptr;
    const float out = std::strtof(value.c_str(), &end);
    return end && end != value.c_str() ? out : fallback;
}

static SkColor parse_drawio_color(const std::string& value, SkColor fallback) {
    if (value.empty() || value == "none") return fallback;
    if (value.size() == 7 && value[0] == '#') {
        const long raw = std::strtol(value.c_str() + 1, nullptr, 16);
        return SkColorSetARGB(0xFF, (raw >> 16) & 0xFF, (raw >> 8) & 0xFF, raw & 0xFF);
    }
    return fallback;
}

static std::string extract_drawio_model_xml(std::string_view xml) {
    if (xml.find("<mxGraphModel") != std::string_view::npos) {
        return std::string(xml);
    }
    if (xml.find("&lt;mxGraphModel") != std::string_view::npos) {
        return decode_xml_entities(xml);
    }
    return std::string(xml);
}

static ParsedDrawioDiagram parse_drawio_diagram(std::string_view xml_input) {
    const std::string xml = extract_drawio_model_xml(xml_input);
    ParsedDrawioDiagram diagram;

    std::size_t pos = 0;
    while ((pos = xml.find("<mxCell", pos)) != std::string::npos) {
        const std::size_t tag_end = xml.find('>', pos);
        if (tag_end == std::string::npos) break;

        const std::string_view tag(xml.data() + pos, tag_end - pos + 1);
        const bool self_closing = tag.size() >= 2 && tag[tag.size() - 2] == '/';
        std::size_t block_end = tag_end + 1;
        std::string_view block = tag;
        if (!self_closing) {
            const std::size_t close = xml.find("</mxCell>", tag_end + 1);
            if (close == std::string::npos) break;
            block_end = close + 9;
            block = std::string_view(xml.data() + pos, block_end - pos);
        }

        DrawioCell cell;
        cell.id = xml_attr(tag, "id");
        cell.value = strip_html_tags(decode_xml_entities(xml_attr(tag, "value")));
        cell.style = xml_attr(tag, "style");
        cell.source = xml_attr(tag, "source");
        cell.target = xml_attr(tag, "target");
        cell.vertex = xml_attr_bool(tag, "vertex");
        cell.edge = xml_attr_bool(tag, "edge");

        const std::size_t geom_pos = block.find("<mxGeometry");
        if (geom_pos != std::string_view::npos) {
            const std::size_t geom_end = block.find('>', geom_pos);
            if (geom_end != std::string_view::npos) {
                const std::string_view geom = block.substr(geom_pos, geom_end - geom_pos + 1);
                cell.x = xml_attr_float(geom, "x");
                cell.y = xml_attr_float(geom, "y");
                cell.width = xml_attr_float(geom, "width");
                cell.height = xml_attr_float(geom, "height");
                cell.has_geometry = true;
            }
        }

        std::size_t point_pos = 0;
        while ((point_pos = block.find("<mxPoint", point_pos)) != std::string_view::npos) {
            const std::size_t point_end = block.find('>', point_pos);
            if (point_end == std::string_view::npos) break;
            const std::string_view point = block.substr(point_pos, point_end - point_pos + 1);
            cell.points.push_back(SkPoint::Make(xml_attr_float(point, "x"), xml_attr_float(point, "y")));
            point_pos = point_end + 1;
        }

        if ((!cell.id.empty()) && (cell.vertex || cell.edge)) {
            diagram.cells.push_back(std::move(cell));
        }
        pos = block_end;
    }

    return diagram;
}

static reactcpp::text::Selection selection_from(const InstanceNode::EditableTextState& s) {
    reactcpp::text::Selection sel;
    sel.active = s.has_selection;
    sel.start = s.sel_start;
    sel.end = s.sel_end;
    reactcpp::text::normalize_selection(sel, s.value.size());
    return sel;
}

static void selection_to(InstanceNode::EditableTextState& s, const reactcpp::text::Selection& sel) {
    s.sel_start = std::min(sel.start, s.value.size());
    s.sel_end = std::min(sel.end, s.value.size());
    s.has_selection = sel.active && s.sel_start < s.sel_end;
}

static void clear_selection(InstanceNode::EditableTextState& s) {
    s.sel_start = 0;
    s.sel_end = 0;
    s.sel_anchor = 0;
    s.has_selection = false;
}

static void clear_selection_keep_anchor(InstanceNode::EditableTextState& s) {
    s.sel_start = 0;
    s.sel_end = 0;
    s.has_selection = false;
}

static void clear_preedit(InstanceNode::EditableTextState& s) {
    s.preedit.clear();
    s.preedit_start = -1;
    s.preedit_length = -1;
}

static reactcpp::text::UndoSnapshot snapshot_from_input(const InstanceNode::EditableTextState& s) {
    reactcpp::text::UndoSnapshot snap;
    snap.value = s.value.to_string();
    snap.cursor = s.cursor;
    snap.sel_start = s.sel_start;
    snap.sel_end = s.sel_end;
    snap.sel_anchor = s.sel_anchor;
    snap.has_selection = s.has_selection;
    return snap;
}

static void apply_snapshot(InstanceNode::EditableTextState& s, const reactcpp::text::UndoSnapshot& snap) {
    s.value.set_string(snap.value);
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
        const auto& props = props_as_view_ref(node.current_vnode);
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

class DrawioDiagramRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<DrawioDiagramProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);

        const SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height);
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRect(bounds, fill);

        const ParsedDrawioDiagram diagram = parse_drawio_diagram(props.drawio_xml);
        if (diagram.cells.empty()) {
            draw_empty_message(canvas, ctx, bounds);
            return;
        }

        std::unordered_map<std::string, const DrawioCell*> by_id;
        by_id.reserve(diagram.cells.size());

        bool have_bounds = false;
        float min_x = 0.0f;
        float min_y = 0.0f;
        float max_x = 0.0f;
        float max_y = 0.0f;
        for (const DrawioCell& cell : diagram.cells) {
            by_id[cell.id] = &cell;
            if (!cell.vertex || !cell.has_geometry) continue;
            const float x0 = cell.x;
            const float y0 = cell.y;
            const float x1 = cell.x + std::max(cell.width, 1.0f);
            const float y1 = cell.y + std::max(cell.height, 1.0f);
            if (!have_bounds) {
                min_x = x0;
                min_y = y0;
                max_x = x1;
                max_y = y1;
                have_bounds = true;
            } else {
                min_x = std::min(min_x, x0);
                min_y = std::min(min_y, y0);
                max_x = std::max(max_x, x1);
                max_y = std::max(max_y, y1);
            }
        }

        if (!have_bounds) {
            draw_empty_message(canvas, ctx, bounds);
            return;
        }

        const float pad = std::max(props.diagram_padding, 0.0f);
        const float diagram_w = std::max(max_x - min_x, 1.0f);
        const float diagram_h = std::max(max_y - min_y, 1.0f);
        const float available_w = std::max(r.width - pad * 2.0f, 1.0f);
        const float available_h = std::max(r.height - pad * 2.0f, 1.0f);
        const float scale = std::min(available_w / diagram_w, available_h / diagram_h);
        const float offset_x = pad + (available_w - diagram_w * scale) * 0.5f;
        const float offset_y = pad + (available_h - diagram_h * scale) * 0.5f;

        auto map_x = [&](float x) {
            return offset_x + (x - min_x) * scale;
        };
        auto map_y = [&](float y) {
            return offset_y + (y - min_y) * scale;
        };
        auto center_of = [&](const DrawioCell& cell) {
            return SkPoint::Make(map_x(cell.x + cell.width * 0.5f), map_y(cell.y + cell.height * 0.5f));
        };
        auto vertex_rect = [&](const DrawioCell& cell) {
            return SkRect::MakeXYWH(
                map_x(cell.x),
                map_y(cell.y),
                std::max(cell.width * scale, 1.0f),
                std::max(cell.height * scale, 1.0f)
            );
        };
        auto connection_point = [&](const DrawioCell& cell, SkPoint toward) {
            const SkRect rect = vertex_rect(cell);
            const SkPoint center = SkPoint::Make(rect.centerX(), rect.centerY());
            const float dx = toward.x() - center.x();
            const float dy = toward.y() - center.y();
            if (std::abs(dx) <= 0.01f && std::abs(dy) <= 0.01f) {
                return center;
            }

            const float hw = std::max(rect.width() * 0.5f, 0.5f);
            const float hh = std::max(rect.height() * 0.5f, 0.5f);
            const std::string shape = drawio_style_value(cell.style, "shape");

            float t = 1.0f;
            if (shape == "ellipse") {
                const float denom = std::sqrt((dx * dx) / (hw * hw) + (dy * dy) / (hh * hh));
                t = denom > 0.0f ? 1.0f / denom : 0.0f;
            } else if (shape == "rhombus" || shape == "diamond") {
                const float denom = std::abs(dx) / hw + std::abs(dy) / hh;
                t = denom > 0.0f ? 1.0f / denom : 0.0f;
            } else {
                float tx = std::numeric_limits<float>::max();
                float ty = std::numeric_limits<float>::max();
                if (std::abs(dx) > 0.01f) tx = hw / std::abs(dx);
                if (std::abs(dy) > 0.01f) ty = hh / std::abs(dy);
                t = std::min(tx, ty);
            }

            return SkPoint::Make(center.x() + dx * t, center.y() + dy * t);
        };

        canvas->save();
        canvas->clipRect(bounds, true);

        for (const DrawioCell& cell : diagram.cells) {
            if (!cell.edge) continue;

            std::vector<SkPoint> points;
            const DrawioCell* source = nullptr;
            const DrawioCell* target = nullptr;
            const auto source_it = by_id.find(cell.source);
            if (source_it != by_id.end() && source_it->second->vertex) {
                source = source_it->second;
                points.push_back(center_of(*source));
            }
            for (const SkPoint& point : cell.points) {
                points.push_back(SkPoint::Make(map_x(point.x()), map_y(point.y())));
            }
            const auto target_it = by_id.find(cell.target);
            if (target_it != by_id.end() && target_it->second->vertex) {
                target = target_it->second;
                points.push_back(center_of(*target));
            }
            if (points.size() < 2) continue;

            if (source) {
                points.front() = connection_point(*source, points[1]);
            }
            if (target) {
                points.back() = connection_point(*target, points[points.size() - 2]);
            }

            SkPaint stroke;
            stroke.setAntiAlias(true);
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeCap(SkPaint::kRound_Cap);
            stroke.setStrokeJoin(SkPaint::kRound_Join);
            stroke.setStrokeWidth(std::max(1.2f, scale * drawio_style_float(cell.style, "strokeWidth", 1.4f)));
            stroke.setColor(parse_drawio_color(drawio_style_value(cell.style, "strokeColor"), static_cast<SkColor>(0xFF6B7280)));
            if (drawio_style_flag(cell.style, "dashed")) {
                const SkScalar intervals[] = {6.0f, 4.0f};
                stroke.setPathEffect(SkDashPathEffect::Make(SkSpan<const SkScalar>(intervals, 2), 0.0f));
            }

            for (std::size_t i = 1; i < points.size(); ++i) {
                canvas->drawLine(points[i - 1], points[i], stroke);
            }

            if (drawio_style_value(cell.style, "endArrow") != "none") {
                draw_arrow_head(canvas, points[points.size() - 2], points.back(), stroke.getColor(), stroke.getStrokeWidth());
            }

            if (!cell.value.empty()) {
                const SkPoint mid = points[points.size() / 2];
                draw_label(cell.value, canvas, ctx, mid.x() - 80.0f, mid.y() - 12.0f, 160.0f, 24.0f, cell.style);
            }
        }

        for (const DrawioCell& cell : diagram.cells) {
            if (!cell.vertex || !cell.has_geometry) continue;
            draw_vertex(cell, canvas, ctx, map_x(cell.x), map_y(cell.y), std::max(cell.width * scale, 1.0f), std::max(cell.height * scale, 1.0f));
        }

        canvas->restore();
    }

private:
    static void draw_arrow_head(SkCanvas* canvas, SkPoint from, SkPoint to, SkColor color, float stroke_width) {
        const float dx = to.x() - from.x();
        const float dy = to.y() - from.y();
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.01f) return;

        const float ux = dx / len;
        const float uy = dy / len;
        const float size = std::max(8.0f, stroke_width * 5.0f);
        const float wing = size * 0.55f;

        const SkPoint base = SkPoint::Make(to.x() - ux * size, to.y() - uy * size);
        const SkPoint left = SkPoint::Make(base.x() - uy * wing, base.y() + ux * wing);
        const SkPoint right = SkPoint::Make(base.x() + uy * wing, base.y() - ux * wing);

        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(std::max(1.0f, stroke_width));
        stroke.setStrokeCap(SkPaint::kRound_Cap);
        stroke.setColor(color);
        canvas->drawLine(to, left, stroke);
        canvas->drawLine(to, right, stroke);
    }

    static void draw_label(
        std::string_view text,
        SkCanvas* canvas,
        const DrawContext& ctx,
        float x,
        float y,
        float w,
        float h,
        std::string_view style
    ) {
        if (text.empty() || w <= 1.0f || h <= 1.0f) return;

        SkFont font;
        font.setSize(std::clamp(drawio_style_float(style, "fontSize", std::clamp(h * 0.20f, 10.0f, 16.0f)), 8.0f, 28.0f));
        font.setTypeface(pick_typeface(ctx.font_mgr));

        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        const float line_height = font.getSize() * 1.25f;

        auto measure = [&](std::string_view sv) -> float {
            return font.measureText(sv.data(), sv.size(), SkTextEncoding::kUTF8);
        };

        const float pad = 4.0f;
        const float max_w = std::max(w - pad * 2.0f, 1.0f);
        const auto spans = reactcpp::text::wrap_text_spans(text, max_w, measure);
        if (spans.empty()) return;

        const float total_h = static_cast<float>(spans.size()) * line_height;
        float baseline = y + (h - total_h) * 0.5f - metrics.fAscent;

        SkPaint text_paint;
        text_paint.setAntiAlias(true);
        text_paint.setColor(parse_drawio_color(drawio_style_value(style, "fontColor"), static_cast<SkColor>(0xFF111827)));

        canvas->save();
        canvas->clipRect(SkRect::MakeXYWH(x, y, w, h), true);
        for (const auto& span : spans) {
            const std::size_t start = std::min(span.start, text.size());
            const std::size_t end = std::min(span.end, text.size());
            if (end < start) continue;
            const std::string line(text.substr(start, end - start));
            const float line_w = measure(line);
            float line_x = x + pad;
            if (drawio_style_value(style, "align") == "right") {
                line_x = x + w - pad - line_w;
            } else if (drawio_style_value(style, "align") != "left") {
                line_x = x + (w - line_w) * 0.5f;
            }
            canvas->drawString(line.c_str(), line_x, baseline, font, text_paint);
            baseline += line_height;
        }
        canvas->restore();
    }

    static void draw_empty_message(SkCanvas* canvas, const DrawContext& ctx, SkRect bounds) {
        SkFont font;
        font.setSize(14.0f);
        font.setTypeface(pick_typeface(ctx.font_mgr));
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(static_cast<SkColor>(0xFF6B7280));
        canvas->drawString("No draw.io diagram", bounds.left() + 12.0f, bounds.top() + 24.0f, font, paint);
    }

    static void draw_vertex(const DrawioCell& cell, SkCanvas* canvas, const DrawContext& ctx, float x, float y, float w, float h) {
        const SkRect rect = SkRect::MakeXYWH(x, y, w, h);
        const std::string shape = drawio_style_value(cell.style, "shape");
        const bool ellipse = shape == "ellipse";
        const bool rounded = drawio_style_flag(cell.style, "rounded");

        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setStyle(SkPaint::kFill_Style);
        fill.setColor(parse_drawio_color(drawio_style_value(cell.style, "fillColor"), SK_ColorWHITE));

        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(std::max(1.0f, drawio_style_float(cell.style, "strokeWidth", 1.4f)));
        stroke.setColor(parse_drawio_color(drawio_style_value(cell.style, "strokeColor"), static_cast<SkColor>(0xFF374151)));
        if (drawio_style_flag(cell.style, "dashed")) {
            const SkScalar intervals[] = {6.0f, 4.0f};
            stroke.setPathEffect(SkDashPathEffect::Make(SkSpan<const SkScalar>(intervals, 2), 0.0f));
        }

        if (ellipse) {
            canvas->drawOval(rect, fill);
            canvas->drawOval(rect, stroke);
        } else if (shape == "rhombus" || shape == "diamond") {
            const SkPoint top = SkPoint::Make(rect.centerX(), rect.top());
            const SkPoint right = SkPoint::Make(rect.right(), rect.centerY());
            const SkPoint bottom = SkPoint::Make(rect.centerX(), rect.bottom());
            const SkPoint left = SkPoint::Make(rect.left(), rect.centerY());
            canvas->drawLine(top, right, stroke);
            canvas->drawLine(right, bottom, stroke);
            canvas->drawLine(bottom, left, stroke);
            canvas->drawLine(left, top, stroke);
        } else if (shape == "cylinder") {
            const float cap_h = std::min(h * 0.22f, 22.0f);
            canvas->drawRect(SkRect::MakeLTRB(rect.left(), rect.top() + cap_h * 0.5f, rect.right(), rect.bottom() - cap_h * 0.5f), fill);
            canvas->drawOval(SkRect::MakeXYWH(x, y, w, cap_h), fill);
            canvas->drawOval(SkRect::MakeXYWH(x, y + h - cap_h, w, cap_h), fill);
            canvas->drawLine(rect.left(), rect.top() + cap_h * 0.5f, rect.left(), rect.bottom() - cap_h * 0.5f, stroke);
            canvas->drawLine(rect.right(), rect.top() + cap_h * 0.5f, rect.right(), rect.bottom() - cap_h * 0.5f, stroke);
            canvas->drawOval(SkRect::MakeXYWH(x, y, w, cap_h), stroke);
            canvas->drawArc(SkRect::MakeXYWH(x, y + h - cap_h, w, cap_h), 0.0f, 180.0f, false, stroke);
        } else if (shape == "swimlane") {
            const float radius = rounded ? std::min(w, h) * 0.08f : 0.0f;
            const float header_h = std::clamp(drawio_style_float(cell.style, "startSize", 28.0f), 18.0f, std::max(18.0f, h * 0.45f));
            if (radius > 0.0f) {
                canvas->drawRoundRect(rect, radius, radius, fill);
                canvas->drawRoundRect(rect, radius, radius, stroke);
            } else {
                canvas->drawRect(rect, fill);
                canvas->drawRect(rect, stroke);
            }
            canvas->drawLine(rect.left(), rect.top() + header_h, rect.right(), rect.top() + header_h, stroke);
        } else if (shape == "image") {
            canvas->drawRect(rect, fill);
            canvas->drawRect(rect, stroke);
            SkPaint mark = stroke;
            mark.setColor(static_cast<SkColor>(0xFFCBD5E1));
            canvas->drawLine(rect.left() + 6.0f, rect.top() + 6.0f, rect.right() - 6.0f, rect.bottom() - 6.0f, mark);
            canvas->drawLine(rect.right() - 6.0f, rect.top() + 6.0f, rect.left() + 6.0f, rect.bottom() - 6.0f, mark);
        } else if (rounded) {
            const float radius = std::min(w, h) * 0.12f;
            canvas->drawRoundRect(rect, radius, radius, fill);
            canvas->drawRoundRect(rect, radius, radius, stroke);
        } else {
            canvas->drawRect(rect, fill);
            canvas->drawRect(rect, stroke);
        }

        if (cell.value.empty()) return;
        draw_label(cell.value, canvas, ctx, x, y, w, h, cell.style);
    }
};

class LineChartRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<LineChartProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        const auto points = resolve_line_points(props);
        if (points.size() < 2) {
            draw_chart_empty_message(canvas, ctx, r, "LineChart: require at least two valid points");
            return;
        }

        double x_min = props.x_min;
        double x_max = props.x_max;
        double y_min = props.y_min;
        double y_max = props.y_max;
        if (props.auto_x_range || props.auto_y_range) {
            bool init_x = false;
            bool init_y = false;
            for (const auto& p : points) {
                grow_range_from_double(p.x, x_min, x_max, init_x);
                grow_range_from_double(p.y, y_min, y_max, init_y);
            }

            if (props.auto_x_range && !init_x) {
                x_min = 0.0;
                x_max = 1.0;
            } else if (props.auto_x_range) {
                sanitize_range(x_min, x_max);
            }
            if (props.auto_y_range && !init_y) {
                y_min = 0.0;
                y_max = 1.0;
            } else if (props.auto_y_range) {
                sanitize_range(y_min, y_max);
            }
        } else {
            sanitize_range(x_min, x_max);
            sanitize_range(y_min, y_max);
        }

        const float plot_x = props.left_padding;
        const float plot_y = props.top_padding;
        const float plot_w = std::max(1.0f, r.width - props.left_padding - props.right_padding);
        const float plot_h = std::max(1.0f, r.height - props.top_padding - props.bottom_padding);
        if (plot_w <= 1.0f || plot_h <= 1.0f) return;

        auto style = resolve_chart_style(props.chart_style);
        const auto colors = resolve_line_colors(props, style);
        style.draw_axes = props.draw_axes;
        style.draw_grid = props.draw_grid;
        style.grid_mode = props.draw_grid ? props.grid_mode : ChartGrid::None;

        draw_grid_lines(
            canvas,
            ctx,
            r,
            plot_x,
            plot_y,
            plot_w,
            plot_h,
            x_min,
            x_max,
            y_min,
            y_max,
            style
        );

        const float sx = plot_w / static_cast<float>(x_max - x_min);
        const float sy = plot_h / static_cast<float>(y_max - y_min);
        const auto map_x = [&](double x) -> float { return plot_x + static_cast<float>(x - x_min) * sx; };
        const auto map_y = [&](double y) -> float { return plot_y + plot_h - static_cast<float>(y - y_min) * sy; };

        if (props.fill_area) {
            const double baseline = (0.0 >= y_min && 0.0 <= y_max) ? 0.0 : y_min;
            const float base_y = map_y(baseline);
            std::vector<SkPoint> area_points;
            area_points.reserve(points.size() + 2);
            area_points.push_back(SkPoint::Make(map_x(points.front().x), base_y));
            for (const auto& p : points) {
                area_points.push_back(SkPoint::Make(map_x(p.x), map_y(p.y)));
            }
            area_points.push_back(SkPoint::Make(map_x(points.back().x), base_y));
            const SkPath area_path = SkPath::Polygon(area_points, true);
            SkPaint fill;
            fill.setColor(make_color(colors.fill_r, colors.fill_g, colors.fill_b, colors.fill_a));
            fill.setStyle(SkPaint::kFill_Style);
            fill.setAntiAlias(true);
            canvas->drawPath(area_path, fill);
        }

        if (props.draw_line && points.size() >= 2) {
            SkPaint stroke;
            stroke.setColor(make_color(props.line_r, props.line_g, props.line_b, props.line_a));
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(std::max(0.5f, static_cast<float>(props.line_width)));
            stroke.setAntiAlias(true);
            for (std::size_t i = 1; i < points.size(); ++i) {
                const auto& p0 = points[i - 1];
                const auto& p1 = points[i];
                canvas->drawLine(
                    map_x(p0.x), map_y(p0.y),
                    map_x(p1.x), map_y(p1.y),
                    stroke
                );
            }
        }

        if (props.show_markers) {
            const SkColor mcolor = make_color(colors.marker_r, colors.marker_g, colors.marker_b, colors.marker_a);
            for (const auto& p : points) {
                draw_marker(canvas, map_x(p.x), map_y(p.y), ChartMarker::Circle, static_cast<float>(props.marker_size), mcolor);
            }
        }
    }
};

class ScatterChartRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<ScatterChartProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        const auto points = resolve_line_points(props);
        if (points.empty()) {
            draw_chart_empty_message(canvas, ctx, r, "ScatterChart: empty data");
            return;
        }

        double x_min = props.x_min;
        double x_max = props.x_max;
        double y_min = props.y_min;
        double y_max = props.y_max;
        bool init_x = false;
        bool init_y = false;
        for (const auto& p : points) {
            grow_range_from_double(p.x, x_min, x_max, init_x);
            grow_range_from_double(p.y, y_min, y_max, init_y);
        }
        if (props.auto_x_range) {
            if (!init_x) {
                x_min = 0.0;
                x_max = 1.0;
            } else sanitize_range(x_min, x_max);
        } else {
            sanitize_range(x_min, x_max);
        }
        if (props.auto_y_range) {
            if (!init_y) {
                y_min = 0.0;
                y_max = 1.0;
            } else sanitize_range(y_min, y_max);
        } else {
            sanitize_range(y_min, y_max);
        }

        const float plot_x = props.left_padding;
        const float plot_y = props.top_padding;
        const float plot_w = std::max(1.0f, r.width - props.left_padding - props.right_padding);
        const float plot_h = std::max(1.0f, r.height - props.top_padding - props.bottom_padding);
        if (plot_w <= 1.0f || plot_h <= 1.0f) return;

        auto style = resolve_chart_style(props.chart_style);
        const auto colors = resolve_line_colors(props, style);
        style.draw_axes = props.draw_axes;
        style.draw_grid = props.draw_grid;
        style.grid_mode = props.draw_grid ? props.grid_mode : ChartGrid::None;

        draw_grid_lines(
            canvas,
            ctx,
            r,
            plot_x,
            plot_y,
            plot_w,
            plot_h,
            x_min,
            x_max,
            y_min,
            y_max,
            style
        );

        const float sx = plot_w / static_cast<float>(x_max - x_min);
        const float sy = plot_h / static_cast<float>(y_max - y_min);
        const auto map_x = [&](double x) -> float { return plot_x + static_cast<float>(x - x_min) * sx; };
        const auto map_y = [&](double y) -> float { return plot_y + plot_h - static_cast<float>(y - y_min) * sy; };

        const SkColor mcolor = make_color(props.point_r, props.point_g, props.point_b, props.point_a);
        for (const auto& p : points) {
            draw_marker(canvas, map_x(p.x), map_y(p.y), props.marker_kind, static_cast<float>(props.marker_size), mcolor);
        }
    }
};

class AreaChartRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<AreaChartProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        const auto points = resolve_line_points(props);
        if (points.empty()) {
            draw_chart_empty_message(canvas, ctx, r, "AreaChart: empty data");
            return;
        }

        std::vector<double> base_y(points.size(), 0.0);
        for (std::size_t i = 0; i < points.size() && i < props.base.size(); ++i) {
            base_y[i] = props.base[i];
        }

        double x_min = props.x_min;
        double x_max = props.x_max;
        double y_min = props.y_min;
        double y_max = props.y_max;
        bool init_x = false;
        bool init_y = false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            grow_range_from_double(points[i].x, x_min, x_max, init_x);
            grow_range_from_double(points[i].y, y_min, y_max, init_y);
            grow_range_from_double(base_y[i], y_min, y_max, init_y);
        }
        if (props.auto_x_range) {
            if (!init_x) {
                x_min = 0.0;
                x_max = 1.0;
            } else sanitize_range(x_min, x_max);
        } else {
            sanitize_range(x_min, x_max);
        }
        if (props.auto_y_range) {
            if (!init_y) {
                y_min = 0.0;
                y_max = 1.0;
            } else sanitize_range(y_min, y_max);
        } else {
            sanitize_range(y_min, y_max);
        }

        const float plot_x = props.left_padding;
        const float plot_y = props.top_padding;
        const float plot_w = std::max(1.0f, r.width - props.left_padding - props.right_padding);
        const float plot_h = std::max(1.0f, r.height - props.top_padding - props.bottom_padding);
        if (plot_w <= 1.0f || plot_h <= 1.0f) return;

        auto style = resolve_chart_style(props.chart_style);
        style.draw_axes = props.draw_axes;
        style.draw_grid = props.draw_grid;
        style.grid_mode = props.draw_grid ? props.grid_mode : ChartGrid::None;

        draw_grid_lines(
            canvas,
            ctx,
            r,
            plot_x,
            plot_y,
            plot_w,
            plot_h,
            x_min,
            x_max,
            y_min,
            y_max,
            style
        );

        const float sx = plot_w / static_cast<float>(x_max - x_min);
        const float sy = plot_h / static_cast<float>(y_max - y_min);
        const auto map_x = [&](double x) -> float { return plot_x + static_cast<float>(x - x_min) * sx; };
        const auto map_y = [&](double y) -> float { return plot_y + plot_h - static_cast<float>(y - y_min) * sy; };

        std::vector<SkPoint> area_points;
        area_points.reserve(points.size() * 2 + 2);
        for (std::size_t i = 0; i < points.size(); ++i) {
            area_points.push_back(SkPoint::Make(map_x(points[i].x), map_y(points[i].y)));
        }
        for (std::size_t i = points.size(); i > 0; --i) {
            const std::size_t j = i - 1;
            area_points.push_back(SkPoint::Make(map_x(points[j].x), map_y(base_y[j])));
        }
        const SkPath area_path = SkPath::Polygon(area_points, true);

        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setStyle(SkPaint::kFill_Style);
        fill.setColor(make_color(props.fill_r, props.fill_g, props.fill_b, props.fill_a));
        canvas->drawPath(area_path, fill);

        if (points.size() >= 2) {
            SkPaint stroke;
            stroke.setAntiAlias(true);
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(std::max(0.5f, static_cast<float>(props.line_width)));
            stroke.setColor(make_color(props.line_r, props.line_g, props.line_b, props.line_a));

            for (std::size_t i = 1; i < points.size(); ++i) {
                canvas->drawLine(
                    map_x(points[i - 1].x), map_y(points[i - 1].y),
                    map_x(points[i].x), map_y(points[i].y),
                    stroke
                );
            }
        }

        if (props.show_base_line && points.size() >= 2) {
            SkPaint base;
            base.setAntiAlias(true);
            base.setStyle(SkPaint::kStroke_Style);
            base.setStrokeWidth(1.0f);
            base.setColor(make_color(props.line_r, props.line_g, props.line_b, 0.75f));
            for (std::size_t i = 1; i < points.size(); ++i) {
                canvas->drawLine(
                    map_x(points[i - 1].x), map_y(base_y[i - 1]),
                    map_x(points[i].x), map_y(base_y[i]),
                    base
                );
            }
        }
    }
};

class BarChartRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<BarChartProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        std::vector<double> values = resolve_series(props.values, props.values_source);
        if (values.empty()) {
            draw_chart_empty_message(canvas, ctx, r, "BarChart: empty data");
            return;
        }

        std::vector<double> x = props.x_source ? props.x_source->snapshot() : props.x;
        if (x.empty()) {
            x.resize(values.size());
            for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i);
        }
        if (x.size() > values.size()) {
            x.resize(values.size());
        } else if (x.size() < values.size()) {
            values.resize(x.size());
        }
        if (values.empty()) {
            draw_chart_empty_message(canvas, ctx, r, "BarChart: no paired x / y values");
            return;
        }

        double x_min = props.x_min;
        double x_max = props.x_max;
        double y_min = props.y_min;
        double y_max = props.y_max;

        bool init_x = false;
        bool init_y = false;
        for (double v : x) {
            grow_range_from_double(v, x_min, x_max, init_x);
        }
        for (double v : values) {
            grow_range_from_double(v, y_min, y_max, init_y);
        }

        if (props.auto_x_range) {
            if (!init_x) {
                x_min = 0.0;
                x_max = 1.0;
            } else sanitize_range(x_min, x_max);
        } else {
            sanitize_range(x_min, x_max);
        }
        if (props.auto_y_range) {
            if (!init_y) {
                y_min = 0.0;
                y_max = 1.0;
            } else sanitize_range(y_min, y_max);
        } else {
            sanitize_range(y_min, y_max);
        }

        const float plot_x = 12.0f;
        const float plot_y = 12.0f;
        const float plot_w = std::max(1.0f, r.width - 24.0f);
        const float plot_h = std::max(1.0f, r.height - 24.0f);
        if (plot_w <= 1.0f || plot_h <= 1.0f) return;

        {
            auto style = resolve_chart_style(props.chart_style);
            style.draw_axes = props.draw_axes;
            style.draw_grid = props.draw_grid;
            style.grid_mode = props.draw_grid ? ChartGrid::Both : ChartGrid::None;
            draw_grid_lines(
                canvas,
                ctx,
                r,
                plot_x,
                plot_y,
                plot_w,
                plot_h,
                x_min,
                x_max,
                y_min,
                y_max,
                style
            );
        }

        const float sx = plot_w / static_cast<float>(x_max - x_min);
        const float sy = plot_h / static_cast<float>(y_max - y_min);
        const auto map_x = [&](double v) -> float { return plot_x + static_cast<float>(v - x_min) * sx; };
        const auto map_y = [&](double v) -> float { return plot_y + plot_h - static_cast<float>(v - y_min) * sy; };

        double min_step = std::numeric_limits<double>::infinity();
        for (std::size_t i = 1; i < x.size(); ++i) {
            const double d = std::fabs(x[i] - x[i - 1]);
            if (d > 0.0 && d < min_step) min_step = d;
        }
        if (!is_finite_double(min_step) || min_step <= 0.0) min_step = 1.0;
        const float width_ratio = std::max(0.05f, static_cast<float>(props.bar_width - props.bar_gap));
        const float max_bar_w = std::fabs(plot_w / static_cast<float>(std::max<std::size_t>(1, x.size())));
        float bar_w = static_cast<float>(min_step) * width_ratio;
        bar_w = std::clamp(bar_w, 0.8f, max_bar_w);

        const double baseline = (0.0 >= y_min && 0.0 <= y_max) ? 0.0 : y_min;
        const float base_y = map_y(baseline);

        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setStyle(SkPaint::kFill_Style);
        fill.setColor(make_color(props.fill_r, props.fill_g, props.fill_b, props.fill_a));

        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(std::max(0.5f, static_cast<float>(props.stroke_width)));
        stroke.setColor(make_color(props.stroke_r, props.stroke_g, props.stroke_b, props.stroke_a));

        for (std::size_t i = 0; i < values.size() && i < x.size(); ++i) {
            const float cx = map_x(x[i]);
            const float left = cx - bar_w * 0.5f;
            const float right = cx + bar_w * 0.5f;
            const float top = map_y(values[i]);
            const SkRect rect = make_safe_rect(left, std::min(base_y, top), right - left, std::fabs(base_y - top));
            canvas->drawRect(rect, fill);
            canvas->drawRect(rect, stroke);
        }
    }
};

class CircleChartRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<CircleChartProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        std::vector<double> x = resolve_series(props.x, props.x_source);
        std::vector<double> y = resolve_series(props.y, props.y_source);
        std::vector<double> rad = resolve_series(props.radius, props.radius_source);

        const std::size_t n = std::min({x.size(), y.size(), rad.empty() ? y.size() : rad.size()});
        if (x.empty() || y.empty() || n == 0) {
            draw_chart_empty_message(canvas, ctx, r, "CircleChart: need x/y data");
            return;
        }

        if (x.size() < n) x.resize(n, 0.0);
        if (y.size() < n) y.resize(n, 0.0);
        if (rad.empty()) {
            rad.assign(n, 3.0);
        } else if (rad.size() < n) {
            rad.resize(n, rad.back());
        }

        double x_min = props.x_min;
        double x_max = props.x_max;
        double y_min = props.y_min;
        double y_max = props.y_max;
        bool init_x = false;
        bool init_y = false;
        for (std::size_t i = 0; i < n; ++i) {
            grow_range_from_double(x[i], x_min, x_max, init_x);
            grow_range_from_double(y[i], y_min, y_max, init_y);
        }
        if (props.auto_range) {
            if (!init_x) {
                x_min = 0.0;
                x_max = 1.0;
            } else sanitize_range(x_min, x_max);
            if (!init_y) {
                y_min = 0.0;
                y_max = 1.0;
            } else sanitize_range(y_min, y_max);
        } else {
            sanitize_range(x_min, x_max);
            sanitize_range(y_min, y_max);
        }

        const float plot_x = props.left_padding;
        const float plot_y = props.top_padding;
        const float plot_w = std::max(1.0f, r.width - props.left_padding - props.right_padding);
        const float plot_h = std::max(1.0f, r.height - props.top_padding - props.bottom_padding);
        if (plot_w <= 1.0f || plot_h <= 1.0f) return;

        {
            auto style = resolve_chart_style(props.chart_style);
            style.draw_axes = props.draw_axes;
            style.draw_grid = props.draw_grid;
            style.grid_mode = props.draw_grid ? ChartGrid::Both : ChartGrid::None;
            draw_grid_lines(
                canvas,
                ctx,
                r,
                plot_x,
                plot_y,
                plot_w,
                plot_h,
                x_min,
                x_max,
                y_min,
                y_max,
                style
            );
        }

        const float sx = plot_w / static_cast<float>(x_max - x_min);
        const float sy = plot_h / static_cast<float>(y_max - y_min);
        const auto map_x = [&](double v) -> float { return plot_x + static_cast<float>(v - x_min) * sx; };
        const auto map_y = [&](double v) -> float { return plot_y + plot_h - static_cast<float>(v - y_min) * sy; };

        const SkColor fill = make_color(props.fill_r, props.fill_g, props.fill_b, props.fill_a);
        const SkColor stroke = make_color(props.stroke_r, props.stroke_g, props.stroke_b, props.stroke_a);
        const SkPaint f;
        const SkPaint s;
        for (std::size_t i = 0; i < n; ++i) {
            const float cx = map_x(x[i]);
            const float cy = map_y(y[i]);
            const float cr = clamp_f(static_cast<float>(std::max(0.0, rad[i])), 0.5f, 80.0f);
            SkPaint paint_fill;
            paint_fill.setAntiAlias(true);
            paint_fill.setColor(fill);
            paint_fill.setStyle(SkPaint::kFill_Style);

            SkPaint paint_stroke;
            paint_stroke.setAntiAlias(true);
            paint_stroke.setStyle(SkPaint::kStroke_Style);
            paint_stroke.setStrokeWidth(std::max(0.5f, static_cast<float>(props.stroke_width)));
            paint_stroke.setColor(stroke);

            canvas->drawCircle(cx, cy, cr, paint_fill);
            canvas->drawCircle(cx, cy, cr, paint_stroke);
        }
    }
};

class HistogramChartRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<HistogramChartProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        const std::vector<double> samples = resolve_series(props.samples, props.samples_source);
        if (samples.empty()) {
            draw_chart_empty_message(canvas, ctx, r, "HistogramChart: empty samples");
            return;
        }

        std::vector<double> edges = props.bin_edges;
        if (edges.size() < 2) {
            edges = histogram_compute_edges(samples, props.fixed_bin_count, props.algorithm);
        }
        if (edges.size() < 2) {
            draw_chart_empty_message(canvas, ctx, r, "HistogramChart: invalid bins");
            return;
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        if (edges.size() < 2) {
            draw_chart_empty_message(canvas, ctx, r, "HistogramChart: invalid bins");
            return;
        }

        std::vector<double> bins = histogram_counts(samples, edges);
        std::vector<double> heights = histogram_normalize(bins, edges, props.normalization);

        double x_min = props.x_min;
        double x_max = props.x_max;
        double y_min = props.y_min;
        double y_max = props.y_max;

        bool init_x = false;
        bool init_y = false;
        for (double e : edges) grow_range_from_double(e, x_min, x_max, init_x);
        for (double h : heights) grow_range_from_double(h, y_min, y_max, init_y);
        if (props.auto_range) {
            if (!init_x) {
                x_min = edges.front();
                x_max = edges.back();
            }
            sanitize_range(x_min, x_max);
            if (!init_y) {
                y_min = 0.0;
                y_max = 1.0;
            } else {
                sanitize_range(y_min, y_max);
            }
        } else {
            sanitize_range(x_min, x_max);
            sanitize_range(y_min, y_max);
        }

        const float plot_x = 12.0f;
        const float plot_y = 12.0f;
        const float plot_w = std::max(1.0f, r.width - 24.0f);
        const float plot_h = std::max(1.0f, r.height - 24.0f);
        if (plot_w <= 1.0f || plot_h <= 1.0f) return;

        {
            auto style = resolve_chart_style(props.chart_style);
            style.draw_axes = props.draw_axes;
            style.draw_grid = props.draw_grid;
            style.grid_mode = props.draw_grid ? ChartGrid::Both : ChartGrid::None;
            draw_grid_lines(
                canvas,
                ctx,
                r,
                plot_x,
                plot_y,
                plot_w,
                plot_h,
                x_min,
                x_max,
                y_min,
                y_max,
                style
            );
        }

        const float sx = plot_w / static_cast<float>(x_max - x_min);
        const float sy = plot_h / static_cast<float>(y_max - y_min);
        const auto map_x = [&](double v) -> float { return plot_x + static_cast<float>(v - x_min) * sx; };
        const auto map_y = [&](double v) -> float { return plot_y + plot_h - static_cast<float>(v - y_min) * sy; };

        const float base = map_y(0.0);
        const float width_factor = clamp_f(static_cast<float>(props.bar_width_factor), 0.05f, 0.99f);
        for (std::size_t i = 0; i < heights.size() && i + 1 < edges.size(); ++i) {
            const float cx = static_cast<float>((edges[i] + edges[i + 1]) * 0.5);
            const float bw = static_cast<float>(edges[i + 1] - edges[i]) * width_factor;
            if (bw <= 0.0f) continue;

            const float half = bw * static_cast<float>(sx) * 0.5f;
            const float x0 = map_x(edges[i]);
            const float x1 = map_x(edges[i + 1]);
            const float left = std::min(x0, x1) + (std::fabs(x1 - x0) - half) * 0.5f;
            const float right = std::max(x0, x1) - (std::fabs(x1 - x0) - half) * 0.5f;
            const float top = map_y(heights[i]);
            const SkRect bar = make_safe_rect(left, std::min(base, top), right - left, std::fabs(base - top));

            SkPaint fill;
            fill.setAntiAlias(true);
            fill.setStyle(SkPaint::kFill_Style);
            fill.setColor(make_color(props.fill_r, props.fill_g, props.fill_b, props.fill_a));
            canvas->drawRect(bar, fill);

            SkPaint stroke;
            stroke.setAntiAlias(true);
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setColor(make_color(props.stroke_r, props.stroke_g, props.stroke_b, props.stroke_a));
            stroke.setStrokeWidth(std::max(0.5f, static_cast<float>(props.stroke_width)));
            canvas->drawRect(bar, stroke);
        }
    }
};

class InputRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<InputProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);

        const std::string value = node.editable_state ? node.editable_state->value.to_string() : props.value;
        const bool composing = node.focused && node.editable_state && !node.editable_state->preedit.empty();
        const std::string preedit = composing ? node.editable_state->preedit : std::string();
        const float scroll_x = node.editable_state ? node.editable_state->scroll_x : 0.0f;

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

        const float pad_x = std::max(props.style.padding, 4.0f);
        const float pad_y = std::max(props.style.padding, 4.0f);
        const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);
        const float content_h = std::max(r.height - pad_y * 2.0f, 1.0f);
        const SkRect content_rect = SkRect::MakeXYWH(pad_x, pad_y, content_w, content_h);

        const bool showing_placeholder = value.empty() && preedit.empty() && !props.placeholder.empty();

        const float draw_scroll_x = showing_placeholder ? 0.0f : scroll_x;

        canvas->save();
        SkRRect clip_rr;
        clip_rr.setRectXY(bounds, 6.0f, 6.0f);
        canvas->clipRRect(clip_rr, true);
        canvas->clipRect(content_rect, true);
        canvas->translate(-draw_scroll_x, 0.0f);

        if (showing_placeholder) {
            SkPaint paint;
            paint.setColor(make_color(0.55f, 0.55f, 0.55f));
            canvas->drawString(props.placeholder.c_str(), text_x, text_y, font, paint);
            canvas->restore();
            return;
        }

        SkPaint paint;
        paint.setColor(make_color(props.text_r, props.text_g, props.text_b));

        if (!preedit.empty() && node.editable_state) {
            const std::size_t cursor = std::min(node.editable_state->cursor, value.size());
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
            if (node.editable_state && node.editable_state->has_selection && node.editable_state->sel_start < node.editable_state->sel_end) {
                const std::size_t start = std::min(node.editable_state->sel_start, value.size());
                const std::size_t end = std::min(node.editable_state->sel_end, value.size());

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

        canvas->restore();
    }
};

class InputAreaRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<InputAreaProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);

        const float scroll_y = node.editable_state ? node.editable_state->scroll_y : 0.0f;

        const std::string value = node.editable_state ? node.editable_state->value.to_string() : props.value;
        const bool composing = node.focused && node.editable_state && !node.editable_state->preedit.empty();
        const std::string preedit = composing ? node.editable_state->preedit : std::string();

        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, r.width, r.height);
        SkPaint fill;
        fill.setColor(make_color(props.bg_r, props.bg_g, props.bg_b, props.bg_a));
        canvas->drawRoundRect(bounds, 6.0f, 6.0f, fill);

        SkPaint border;
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(2.0f);
        border.setColor(make_color(props.border_r, props.border_g, props.border_b));
        canvas->drawRoundRect(bounds, 6.0f, 6.0f, border);

        const float pad_x = std::max(props.style.padding, 4.0f);
        const float pad_y = std::max(props.style.padding, 4.0f);
        const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);
        const float content_h = std::max(r.height - pad_y * 2.0f, 1.0f);
        const SkRect content_rect = SkRect::MakeXYWH(pad_x, pad_y, content_w, content_h);

        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        const float line_height = props.text_size * 1.4f;

        auto measure = [&](std::string_view s) -> float {
            return font.measureText(s.data(), s.size(), SkTextEncoding::kUTF8);
        };

        const bool showing_placeholder = value.empty() && preedit.empty() && !props.placeholder.empty();
        const std::string display_text = [&]() {
            if (!composing || !node.editable_state) return value;
            const std::size_t cursor = std::min(node.editable_state->cursor, value.size());
            return value.substr(0, cursor) + preedit + value.substr(cursor);
        }();

        const std::string_view to_wrap = showing_placeholder ? std::string_view(props.placeholder) : std::string_view(display_text);
        const auto spans = reactcpp::text::wrap_text_spans(to_wrap, content_w, measure);

        SkPaint paint;
        paint.setColor(showing_placeholder ? make_color(0.55f, 0.55f, 0.55f)
                                           : make_color(props.text_r, props.text_g, props.text_b));

        canvas->save();
        SkRRect clip_rr;
        clip_rr.setRectXY(bounds, 6.0f, 6.0f);
        canvas->clipRRect(clip_rr, true);
        canvas->clipRect(content_rect, true);
        canvas->translate(0.0f, -scroll_y);

        if (!showing_placeholder && !composing && node.editable_state
            && node.editable_state->has_selection && node.editable_state->sel_start < node.editable_state->sel_end) {
            const std::size_t sel_start = std::min(node.editable_state->sel_start, value.size());
            const std::size_t sel_end = std::min(node.editable_state->sel_end, value.size());

            SkPaint highlight;
            highlight.setColor(static_cast<SkColor>(0xFF1E3A8A));
            highlight.setAntiAlias(true);
            highlight.setStyle(SkPaint::kFill_Style);

            for (std::size_t line = 0; line < spans.size(); ++line) {
                const auto& sp = spans[line];
                const std::size_t line_start = sp.start;
                const std::size_t line_end = sp.end;

                const std::size_t inter_start = std::max(line_start, sel_start);
                const std::size_t inter_end = std::min(line_end, sel_end);
                if (inter_start >= inter_end) continue;

                const float baseline = pad_y + static_cast<float>(line) * line_height - metrics.fAscent;
                const float left_w = measure(to_wrap.substr(line_start, inter_start - line_start));
                const float mid_w = measure(to_wrap.substr(inter_start, inter_end - inter_start));

                const float x0 = pad_x + left_w;
                const float x1 = x0 + mid_w;
                const float top = baseline + metrics.fAscent - 1.0f;
                const float bottom = baseline + metrics.fDescent + 1.0f;

                canvas->drawRoundRect(SkRect::MakeLTRB(x0, top, x1, bottom), 2.0f, 2.0f, highlight);
            }
        }

        SkPaint selected_paint = paint;
        selected_paint.setColor(SK_ColorWHITE);

        const std::size_t preedit_begin = (composing && node.editable_state)
            ? std::min(node.editable_state->cursor, value.size())
            : 0;
        const std::size_t preedit_end = (composing && node.editable_state)
            ? (preedit_begin + preedit.size())
            : 0;

        SkPaint underline;
        underline.setAntiAlias(true);
        underline.setColor(make_color(props.text_r, props.text_g, props.text_b));
        underline.setStrokeWidth(1.5f);

        const bool draw_selection_overlay = !showing_placeholder && !composing && node.editable_state
            && node.editable_state->has_selection && node.editable_state->sel_start < node.editable_state->sel_end;
        const std::size_t sel_start = (draw_selection_overlay && node.editable_state)
            ? std::min(node.editable_state->sel_start, value.size())
            : 0;
        const std::size_t sel_end = (draw_selection_overlay && node.editable_state)
            ? std::min(node.editable_state->sel_end, value.size())
            : 0;

        for (std::size_t line = 0; line < spans.size(); ++line) {
            const auto& sp = spans[line];
            const float baseline = pad_y + static_cast<float>(line) * line_height - metrics.fAscent;

            const float line_top = baseline + metrics.fAscent;
            const float line_bottom = baseline + metrics.fDescent;
            if (line_bottom < scroll_y - line_height) {
                continue;
            }
            if (line_top > scroll_y + content_h + line_height) {
                break;
            }

            const std::string_view line_sv = to_wrap.substr(sp.start, sp.end - sp.start);
            std::string line_str(line_sv);
            canvas->drawString(line_str.c_str(), pad_x, baseline, font, paint);

            if (!showing_placeholder && composing && !preedit.empty()) {
                const std::size_t line_start = sp.start;
                const std::size_t line_end = sp.end;
                const std::size_t inter_start = std::max(line_start, preedit_begin);
                const std::size_t inter_end = std::min(line_end, preedit_end);
                if (inter_start < inter_end) {
                    const float left_w = measure(to_wrap.substr(line_start, inter_start - line_start));
                    const float mid_w = measure(to_wrap.substr(inter_start, inter_end - inter_start));
                    const float x0 = pad_x + left_w;
                    canvas->drawLine(x0, baseline + 2.0f, x0 + mid_w, baseline + 2.0f, underline);
                }
            }

            if (draw_selection_overlay) {
                const std::size_t line_start = sp.start;
                const std::size_t line_end = sp.end;
                const std::size_t inter_start = std::max(line_start, sel_start);
                const std::size_t inter_end = std::min(line_end, sel_end);
                if (inter_start < inter_end) {
                    const float left_w = measure(to_wrap.substr(line_start, inter_start - line_start));
                    const std::string_view mid_sv = to_wrap.substr(inter_start, inter_end - inter_start);
                    std::string mid_str(mid_sv);
                    canvas->drawString(mid_str.c_str(), pad_x + left_w, baseline, font, selected_paint);
                }
            }
        }

        canvas->restore();
    }
};

class RiveRenderer final : public ElementRenderer {
public:
    void on_draw(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) const override {
        const auto& props = std::get<RiveProps>(node.current_vnode.props);
        const LayoutRect r = layout_for_node(node);
        draw_chart_background(canvas, r, props);
        if (r.width <= 1.0f || r.height <= 1.0f) return;

        RiveDrawData data;
        data.inputs.time_scale = props.time_scale;
        data.inputs.numbers = props.number_inputs;
        data.inputs.bools = props.bool_inputs;
        data.inputs.revision = props.inputs_revision;
        if (props.inputs_source) {
            data.inputs = props.inputs_source->snapshot();
        }

        if (draw_rive_backend(node, props, canvas, r, data)) {
            return;
        }

        SkPaint panel;
        panel.setAntiAlias(true);
        panel.setStyle(SkPaint::kFill_Style);
        panel.setColor(static_cast<SkColor>(0xFFF8FAFC));
        canvas->drawRoundRect(SkRect::MakeXYWH(8.0f, 8.0f, std::max(1.0f, r.width - 16.0f), std::max(1.0f, r.height - 16.0f)), 10.0f, 10.0f, panel);

        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(1.0f);
        stroke.setColor(static_cast<SkColor>(0xFFCBD5E1));
        canvas->drawRoundRect(SkRect::MakeXYWH(8.0f, 8.0f, std::max(1.0f, r.width - 16.0f), std::max(1.0f, r.height - 16.0f)), 10.0f, 10.0f, stroke);

        SkPaint title_paint;
        title_paint.setAntiAlias(true);
        title_paint.setColor(static_cast<SkColor>(0xFF0F172A));
        SkFont title_font;
        title_font.setTypeface(pick_typeface(ctx.font_mgr));
        title_font.setSize(18.0f);

        const std::string title = props.source.empty() ? "Rive" : ("Rive: " + props.source);
        canvas->drawString(title.c_str(), 20.0f, 34.0f, title_font, title_paint);

        SkPaint text_paint;
        text_paint.setAntiAlias(true);
        text_paint.setColor(static_cast<SkColor>(0xFF334155));
        SkFont text_font;
        text_font.setTypeface(pick_typeface(ctx.font_mgr));
        text_font.setSize(13.0f);

        float y = 58.0f;
        auto draw_line = [&](const std::string& s) {
            if (y > r.height - 16.0f) return;
            canvas->drawString(s.c_str(), 20.0f, y, text_font, text_paint);
            y += 19.0f;
        };

        if (!data.error.empty()) {
            draw_line(data.error);
        }
        if (!props.artboard.empty()) draw_line("artboard: " + props.artboard);
        if (!props.state_machine.empty()) draw_line("state machine: " + props.state_machine);
        draw_line("time_scale: " + trim_chart_value_zeros(std::to_string(data.inputs.time_scale)));
        for (const auto& [name, value] : data.inputs.numbers) {
            draw_line(name + ": " + trim_chart_value_zeros(std::to_string(value)));
        }
        for (const auto& [name, value] : data.inputs.bools) {
            draw_line(name + ": " + std::string(value ? "true" : "false"));
        }
    }
};

static const ElementRenderer& renderer_for(TypeId type) {
    static ViewRenderer view_renderer;
    static ButtonRenderer button_renderer;
    static TextRenderer text_renderer;
    static InputRenderer input_renderer;
    static InputAreaRenderer input_area_renderer;
    static DrawioDiagramRenderer drawio_diagram_renderer;
    static RiveRenderer rive_renderer;
    static LineChartRenderer line_chart_renderer;
    static ScatterChartRenderer scatter_chart_renderer;
    static AreaChartRenderer area_chart_renderer;
    static BarChartRenderer bar_chart_renderer;
    static CircleChartRenderer circle_chart_renderer;
    static HistogramChartRenderer histogram_chart_renderer;

    if (type == host_type_view()) return view_renderer;
    if (type == host_type_button()) return button_renderer;
    if (type == host_type_text()) return text_renderer;
    if (type == host_type_input()) return input_renderer;
    if (type == host_type_input_area()) return input_area_renderer;
    if (type == host_type_drawio_diagram()) return drawio_diagram_renderer;
    if (type == host_type_rive()) return rive_renderer;
    if (type == host_type_line_chart()) return line_chart_renderer;
    if (type == host_type_scatter_chart()) return scatter_chart_renderer;
    if (type == host_type_area_chart()) return area_chart_renderer;
    if (type == host_type_bar_chart()) return bar_chart_renderer;
    if (type == host_type_circle_chart()) return circle_chart_renderer;
    if (type == host_type_histogram_chart()) return histogram_chart_renderer;
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
    const float padding_x = inst->type == host_type_text() ? 0.0f : 16.0f;
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
    if (node.type == host_type_text() && !style.width) {
        YGNodeStyleSetAlignSelf(yn, YGAlignFlexStart);
    }

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
    SkiaRuntime(AppRenderFunc app, reactcpp::PlatformBridge platform, int surface_w, int surface_h)
        : app_render_(std::move(app))
        , surface_width_(surface_w)
        , surface_height_(surface_h)
        , platform_(platform) {
        font_mgr_ = create_font_manager();

        g_skia_dispatcher.request_update = &SkiaRuntime::request_update_trampoline;
        g_skia_dispatcher.request_update_ctx = this;
    }

#if defined(REACTCPP_INTERNAL_TESTING)
    void test_set_focused_input(InstanceNode* node) {
        focused_node_ = node;
        focused_input_ = node;
    }

    void test_set_update_requested(bool v) { update_requested_ = v; }
    bool test_update_requested() const { return update_requested_; }
    const InstanceNode& test_root_instance() const { return root_instance_; }
#endif

    Element render_frame() {
        g_skia_dispatcher.current_instance = &root_instance_;
        g_skia_dispatcher.current_index = 0;

        Element new_root = app_render_();
        reconcile(root_instance_, new_root);
        g_skia_dispatcher.current_instance = nullptr;
        return root_instance_.current_vnode;
    }

    void perform_update_if_needed() {
        const bool repaint_requested = take_runtime_repaint_request();
        if (update_requested_) {
            update_requested_ = false;
            (void)render_frame();
            return;
        }
        if (!repaint_requested) return;
        refresh_data_driven_nodes(root_instance_);
    }

    void draw(SkCanvas* canvas, int width, int height) {
        surface_width_ = width;
        surface_height_ = height;

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
        draw_context_menu(canvas, ctx);
    }

    reactcpp::Frame render_to_frame(int width, int height) {
        SkPictureRecorder recorder;
        SkCanvas* record_canvas = recorder.beginRecording(
            SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height))
        );

        this->perform_update_if_needed();
        this->draw(record_canvas, width, height);

        reactcpp::Frame out;
        out.picture = recorder.finishRecordingAsPicture();
        collect_cached_pictures(root_instance_, out.retained_pictures);
        return out;
    }

    void handle_mouse_down(float x, float y) {
        handle_mouse_button_down(x, y, 1);
    }

    void handle_mouse_button_down(float win_x, float win_y, std::uint8_t clicks, std::uint8_t button = SDL_BUTTON_LEFT) {
        const float x = win_x;
        const float y = win_y;

        if (button == SDL_BUTTON_LEFT && context_menu_.open) {
            if (handle_context_menu_click(x, y)) {
                return;
            }
        }

        InstanceNode* hit = hit_test_at(root_instance_, x, y);
        if (button == SDL_BUTTON_RIGHT) {
            mouse_selecting_ = false;
            mouse_select_target_ = nullptr;
            this->set_mouse_capture(false);

            set_focus(hit);
            if (hit) {
                open_context_menu(x, y, *hit);
            } else {
                close_context_menu();
            }
            request_update();
            return;
        }

        close_context_menu();

        InstanceNode* hit_input = find_ancestor_by_type(hit, host_type_input());
        InstanceNode* hit_input_area = find_ancestor_by_type(hit, host_type_input_area());
        InstanceNode* hit_editable = hit_input ? hit_input : hit_input_area;
        set_focus(hit_editable ? hit_editable : hit);

        if (!hit_editable || !hit_editable->editable_state) {
            mouse_selecting_ = false;
            mouse_select_target_ = nullptr;
            this->set_mouse_capture(false);
            if (hit) {
                (void)dispatch_click_bubble(hit);
            }
            return;
        }

        if (hit_editable != focused_input_) {
            return;
        }

        auto& state = *focused_input_->editable_state;
        if (!state.preedit.empty()) {
            return;
        }

        float abs_x = 0.0f;
        float abs_y = 0.0f;
        absolute_origin_for(focused_input_, abs_x, abs_y);

        std::size_t caret = 0;
        if (focused_input_->type == host_type_input()) {
            const auto& props = std::get<InputProps>(focused_input_->current_vnode.props);
            SkFont font;
            font.setSize(props.text_size);
            font.setTypeface(pick_typeface(font_mgr_));
            const float local_x = (x - abs_x) - 8.0f + state.scroll_x;
            caret = byte_index_for_x(state.value.view(), local_x, font);
        } else if (focused_input_->type == host_type_input_area()) {
            const auto& props = std::get<InputAreaProps>(focused_input_->current_vnode.props);
            const LayoutRect r = layout_for_node(*focused_input_);

            SkFont font;
            font.setSize(props.text_size);
            font.setTypeface(pick_typeface(font_mgr_));
            const float line_height = props.text_size * 1.4f;

            const float pad_x = std::max(props.style.padding, 4.0f);
            const float pad_y = std::max(props.style.padding, 4.0f);
            const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);
            const float local_x = (x - abs_x) - pad_x;
            const float local_y = (y - abs_y) - pad_y;

            auto measure = [&](std::string_view sv) -> float {
                return font.measureText(sv.data(), sv.size(), SkTextEncoding::kUTF8);
            };

            caret = reactcpp::text::byte_index_for_wrapped_point(
                state.value.view(),
                content_w,
                measure,
                local_x,
                local_y,
                line_height,
                state.scroll_y
            );
        } else {
            return;
        }

        if (clicks >= 2) {
            const auto word = reactcpp::text::word_selection_at(state.value.view(), caret);
            state.cursor = word.active ? word.end : caret;
            state.sel_anchor = word.active ? word.start : caret;
            state.sel_start = word.start;
            state.sel_end = word.end;
            state.has_selection = word.active;
        } else {
            clear_selection(state);
            state.cursor = caret;
            state.sel_anchor = caret;
        }

        if (focused_input_->type == host_type_input_area()) {
            ensure_input_area_caret_visible(*focused_input_);
        } else if (focused_input_->type == host_type_input()) {
            ensure_input_caret_visible(*focused_input_);
        }

        mouse_selecting_ = true;
        mouse_select_target_ = focused_input_;
        this->set_mouse_capture(true);
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_mouse_move(float win_x, float win_y) {
        if (!mouse_selecting_ || !mouse_select_target_ || !mouse_select_target_->editable_state) return;
        auto& state = *mouse_select_target_->editable_state;
        if (!state.preedit.empty()) return;

        const float x = win_x;
        const float y = win_y;

        float abs_x = 0.0f;
        float abs_y = 0.0f;
        absolute_origin_for(mouse_select_target_, abs_x, abs_y);

        if (mouse_select_target_->type == host_type_input()) {
            const auto& props = std::get<InputProps>(mouse_select_target_->current_vnode.props);
            SkFont font;
            font.setSize(props.text_size);
            font.setTypeface(pick_typeface(font_mgr_));
            const float local_x = (x - abs_x) - 8.0f + state.scroll_x;
            state.cursor = byte_index_for_x(state.value.view(), local_x, font);
        } else if (mouse_select_target_->type == host_type_input_area()) {
            const auto& props = std::get<InputAreaProps>(mouse_select_target_->current_vnode.props);
            const LayoutRect r = layout_for_node(*mouse_select_target_);

            SkFont font;
            font.setSize(props.text_size);
            font.setTypeface(pick_typeface(font_mgr_));
            const float line_height = props.text_size * 1.4f;

            const float pad_x = std::max(props.style.padding, 4.0f);
            const float pad_y = std::max(props.style.padding, 4.0f);
            const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);
            const float local_x = (x - abs_x) - pad_x;
            const float local_y = (y - abs_y) - pad_y;

            auto measure = [&](std::string_view sv) -> float {
                return font.measureText(sv.data(), sv.size(), SkTextEncoding::kUTF8);
            };

            state.cursor = reactcpp::text::byte_index_for_wrapped_point(
                state.value.view(),
                content_w,
                measure,
                local_x,
                local_y,
                line_height,
                state.scroll_y
            );
        } else {
            return;
        }

        const auto sel = reactcpp::text::selection_from_anchor(state.sel_anchor, state.cursor, state.value.size());
        state.sel_start = sel.start;
        state.sel_end = sel.end;
        state.has_selection = sel.active;

        if (mouse_select_target_->type == host_type_input_area()) {
            ensure_input_area_caret_visible(*mouse_select_target_);
        } else if (mouse_select_target_->type == host_type_input()) {
            ensure_input_caret_visible(*mouse_select_target_);
        }

        mark_dirty(mouse_select_target_);
        request_update();
    }

    void handle_mouse_button_up(float, float) {
        this->set_mouse_capture(false);
        mouse_selecting_ = false;
        mouse_select_target_ = nullptr;
    }

    void set_mouse_capture(bool enabled) {
        if (!platform_.cmds) return;
        reactcpp::PlatformCommand cmd;
        cmd.type = reactcpp::PlatformCmdType::SetMouseCapture;
        cmd.mouse_capture = enabled;
        platform_.cmds->push(std::move(cmd));
    }

    void handle_mouse_wheel(float wheel_y) {
        if (wheel_y == 0.0f) return;

        InstanceNode* target = (focused_input_ && focused_input_->type == host_type_input_area()) ? focused_input_ : nullptr;
        if (!target || !target->editable_state) return;

        auto metrics = input_area_caret_metrics(*target);
        if (!metrics) return;

        auto& state = *target->editable_state;
        const auto& props = std::get<InputAreaProps>(target->current_vnode.props);
        const float line_height = props.text_size * 1.4f;
        const float step = line_height * 3.0f;
        const float max_scroll = std::max(0.0f, metrics->content_h - metrics->visible_h);

        state.scroll_y = std::clamp(state.scroll_y - wheel_y * step, 0.0f, max_scroll);
        state.scroll_y = std::round(state.scroll_y);
        mark_dirty(target);
        request_update();
    }

    void handle_text_input(const char* text) {
        if (!focused_input_ || !focused_input_->editable_state) return;
        auto& state = *focused_input_->editable_state;
        const std::string inserted(text ? text : "");
        if (!inserted.empty()) {
            state.undo.push(snapshot_from_input(state));
        }
        reactcpp::text::Selection sel = selection_from(state);
        reactcpp::text::insert_text(state.value, state.cursor, sel, inserted);
        selection_to(state, sel);
        state.sel_anchor = state.cursor;
        clear_preedit(state);

        if (focused_input_->type == host_type_input_area()) {
            ensure_input_area_caret_visible(*focused_input_);
        } else if (focused_input_->type == host_type_input()) {
            ensure_input_caret_visible(*focused_input_);
        }
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_text_editing(const char* text, int start, int length) {
        if (!focused_input_ || !focused_input_->editable_state) return;
        auto& state = *focused_input_->editable_state;
        state.preedit = text ? std::string(text) : std::string();
        state.preedit_start = start;
        state.preedit_length = length;

        if (focused_input_->type == host_type_input_area()) {
            ensure_input_area_caret_visible(*focused_input_);
        } else if (focused_input_->type == host_type_input()) {
            ensure_input_caret_visible(*focused_input_);
        }
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_backspace() {
        if (!focused_input_ || !focused_input_->editable_state) return;
        auto& state = *focused_input_->editable_state;
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

                if (focused_input_->type == host_type_input_area()) {
                    ensure_input_area_caret_visible(*focused_input_);
                } else if (focused_input_->type == host_type_input()) {
                    ensure_input_caret_visible(*focused_input_);
                }
                mark_dirty(focused_input_);
                request_update();
                return;
            }
        }

        if (state.cursor == 0 || state.value.empty()) return;
        const std::size_t cursor = std::min(state.cursor, state.value.size());
        const std::string_view v = state.value.view();
        const std::size_t prev = reactcpp::text::utf8_prev_boundary(v, cursor);
        if (prev >= cursor) return;
        state.undo.push(snapshot_from_input(state));
        state.value.erase(prev, cursor - prev);
        state.cursor = prev;
        state.sel_anchor = state.cursor;

        if (focused_input_->type == host_type_input_area()) {
            ensure_input_area_caret_visible(*focused_input_);
        } else if (focused_input_->type == host_type_input()) {
            ensure_input_caret_visible(*focused_input_);
        }
        mark_dirty(focused_input_);
        request_update();
    }

    void handle_key_down(SDL_Keycode key, SDL_Keymod mod, bool) {
        if (!focused_input_ || !focused_input_->editable_state) return;
        auto& state = *focused_input_->editable_state;

        const bool accel = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        const bool shift = (mod & SDL_KMOD_SHIFT) != 0;

        if (accel) {
            if (!state.preedit.empty()) {
                this->clear_composition();
                clear_preedit(state);
            }

            reactcpp::text::Selection sel = selection_from(state);

            if (key == SDLK_Z) {
                auto snap = state.undo.pop();
                if (snap) {
                    if (!state.preedit.empty()) {
                        this->clear_composition();
                    }
                    apply_snapshot(state, *snap);
                    clear_preedit(state);

                    if (focused_input_->type == host_type_input_area()) {
                        ensure_input_area_caret_visible(*focused_input_);
                    } else if (focused_input_->type == host_type_input()) {
                        ensure_input_caret_visible(*focused_input_);
                    }
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

                if (focused_input_->type == host_type_input_area()) {
                    ensure_input_area_caret_visible(*focused_input_);
                } else if (focused_input_->type == host_type_input()) {
                    ensure_input_caret_visible(*focused_input_);
                }
                mark_dirty(focused_input_);
                request_update();
                return;
            }

            if (key == SDLK_C) {
                const std::string copy = reactcpp::text::selected_substr(state.value, sel);
                if (!copy.empty()) {
                    this->set_clipboard_text(copy);
                }
                return;
            }

            if (key == SDLK_X) {
                const std::string cut = reactcpp::text::selected_substr(state.value, sel);
                if (!cut.empty()) {
                    this->set_clipboard_text(cut);
                    state.undo.push(snapshot_from_input(state));
                    reactcpp::text::erase_selection(state.value, state.cursor, sel);
                    selection_to(state, sel);
                    state.sel_anchor = state.cursor;

                    if (focused_input_->type == host_type_input_area()) {
                        ensure_input_area_caret_visible(*focused_input_);
                    } else if (focused_input_->type == host_type_input()) {
                        ensure_input_caret_visible(*focused_input_);
                    }
                    mark_dirty(focused_input_);
                    request_update();
                }
                return;
            }

            if (key == SDLK_V) {
                const std::string paste = this->get_clipboard_text();
                if (!paste.empty()) {
                    state.undo.push(snapshot_from_input(state));
                    reactcpp::text::insert_text(state.value, state.cursor, sel, paste);
                    selection_to(state, sel);
                    state.sel_anchor = state.cursor;

                    if (focused_input_->type == host_type_input_area()) {
                        ensure_input_area_caret_visible(*focused_input_);
                    } else if (focused_input_->type == host_type_input()) {
                        ensure_input_caret_visible(*focused_input_);
                    }
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

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (focused_input_->type != host_type_input_area()) {
                return;
            }

            state.undo.push(snapshot_from_input(state));
            reactcpp::text::Selection sel = selection_from(state);
            reactcpp::text::insert_text(state.value, state.cursor, sel, "\n");
            selection_to(state, sel);
            state.sel_anchor = state.cursor;
            clear_preedit(state);

            ensure_input_area_caret_visible(*focused_input_);
            mark_dirty(focused_input_);
            request_update();
            return;
        }

            if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                state.cursor = std::min(state.cursor, state.value.size());

                const std::size_t before = state.cursor;
                const std::string_view v = state.value.view();
                if (key == SDLK_LEFT) {
                    state.cursor = reactcpp::text::utf8_prev_boundary(v, state.cursor);
                } else {
                    state.cursor = reactcpp::text::utf8_next_boundary(v, state.cursor);
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

            if (focused_input_->type == host_type_input_area()) {
                ensure_input_area_caret_visible(*focused_input_);
            } else if (focused_input_->type == host_type_input()) {
                ensure_input_caret_visible(*focused_input_);
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

    struct ContextMenuState {
        bool open{false};
        float x{0.0f};
        float y{0.0f};
        std::string copy_text;
        std::string label{"Copy"};
    };

    static constexpr float kContextMenuWidth = 136.0f;
    static constexpr float kContextMenuItemHeight = 34.0f;
    static constexpr float kContextMenuPadding = 6.0f;

    static SkRect context_menu_rect(const ContextMenuState& menu, int surface_w, int surface_h) {
        float x = menu.x;
        float y = menu.y;
        const float w = kContextMenuWidth;
        const float h = kContextMenuItemHeight;
        x = std::clamp(x, kContextMenuPadding, std::max(kContextMenuPadding, static_cast<float>(surface_w) - w - kContextMenuPadding));
        y = std::clamp(y, kContextMenuPadding, std::max(kContextMenuPadding, static_cast<float>(surface_h) - h - kContextMenuPadding));
        return SkRect::MakeXYWH(x, y, w, h);
    }

    static void append_copyable_text(const InstanceNode& node, std::string& out) {
        std::visit([&](const auto& props) {
            using P = std::decay_t<decltype(props)>;
            if constexpr (std::is_same_v<P, TextProps>) {
                out += props.text;
            } else if constexpr (std::is_same_v<P, ButtonProps>) {
                out += props.label;
            } else if constexpr (std::is_same_v<P, InputProps> || std::is_same_v<P, InputAreaProps>) {
                if (node.editable_state) {
                    const auto sel = selection_from(*node.editable_state);
                    if (reactcpp::text::has_non_empty_selection(sel)) {
                        out += reactcpp::text::selected_substr(node.editable_state->value, sel);
                    } else {
                        out += node.editable_state->value.to_string();
                    }
                } else {
                    out += props.value;
                }
            } else if constexpr (std::is_same_v<P, DrawioDiagramProps>) {
                out += props.drawio_xml;
            } else if constexpr (std::is_same_v<P, RiveProps>) {
                out += props.source;
            }
        }, node.current_vnode.props);

        for (const auto& child : node.children) {
            std::string child_text;
            append_copyable_text(*child, child_text);
            if (!child_text.empty()) {
                if (!out.empty()) out.push_back('\n');
                out += child_text;
            }
        }
    }

    static std::string copyable_text_for(const InstanceNode& node) {
        std::string out;
        append_copyable_text(node, out);
        return out;
    }

    void open_context_menu(float x, float y, const InstanceNode& target) {
        context_menu_.open = true;
        context_menu_.x = x;
        context_menu_.y = y;
        context_menu_.copy_text = copyable_text_for(target);
    }

    void close_context_menu() {
        if (!context_menu_.open) return;
        context_menu_.open = false;
        context_menu_.copy_text.clear();
    }

    bool handle_context_menu_click(float x, float y) {
        const SkRect rect = context_menu_rect(context_menu_, surface_width_, surface_height_);
        const bool inside = rect.contains(x, y);
        if (inside && !context_menu_.copy_text.empty()) {
            set_clipboard_text(context_menu_.copy_text);
        }
        close_context_menu();
        request_update();
        return inside;
    }

    void draw_context_menu(SkCanvas* canvas, const DrawContext& ctx) const {
        if (!context_menu_.open) return;

        const SkRect rect = context_menu_rect(context_menu_, ctx.surface_width, ctx.surface_height);
        const bool enabled = !context_menu_.copy_text.empty();

        SkPaint shadow;
        shadow.setAntiAlias(true);
        shadow.setColor(SkColorSetARGB(55, 0, 0, 0));
        canvas->drawRoundRect(rect.makeOffset(0.0f, 2.0f), 6.0f, 6.0f, shadow);

        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(SK_ColorWHITE);
        canvas->drawRoundRect(rect, 6.0f, 6.0f, fill);

        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(1.0f);
        stroke.setColor(static_cast<SkColor>(0xFFE5E7EB));
        canvas->drawRoundRect(rect, 6.0f, 6.0f, stroke);

        SkFont font;
        font.setSize(15.0f);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        SkPaint text;
        text.setAntiAlias(true);
        text.setColor(enabled ? static_cast<SkColor>(0xFF111827) : static_cast<SkColor>(0xFF9CA3AF));

        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        const float baseline = rect.top() + (rect.height() - (metrics.fDescent - metrics.fAscent)) * 0.5f - metrics.fAscent;
        canvas->drawString(context_menu_.label.c_str(), rect.left() + 14.0f, baseline, font, text);
    }

    void clear_composition() {
        if (!platform_.cmds) return;
        reactcpp::PlatformCommand cmd;
        cmd.type = reactcpp::PlatformCmdType::ClearComposition;
        platform_.cmds->push(std::move(cmd));
    }

    void start_text_input(bool multiline) {
        if (!platform_.cmds) return;
        reactcpp::PlatformCommand cmd;
        cmd.type = reactcpp::PlatformCmdType::StartTextInput;
        cmd.multiline = multiline;
        platform_.cmds->push(std::move(cmd));
    }

    void stop_text_input() {
        if (!platform_.cmds) return;
        reactcpp::PlatformCommand cmd;
        cmd.type = reactcpp::PlatformCmdType::StopTextInput;
        platform_.cmds->push(std::move(cmd));
    }

    void set_clipboard_text(const std::string& text) {
        if (!platform_.cmds) return;
        reactcpp::PlatformCommand cmd;
        cmd.type = reactcpp::PlatformCmdType::SetClipboardText;
        cmd.text = text;
        platform_.cmds->push(std::move(cmd));
    }

    std::string get_clipboard_text() {
        if (!platform_.cmds || !platform_.clipboard) return {};
        const std::uint64_t id = platform_.clipboard->new_request_id();
        reactcpp::PlatformCommand cmd;
        cmd.type = reactcpp::PlatformCmdType::GetClipboardText;
        cmd.request_id = id;
        platform_.cmds->push(std::move(cmd));
        return platform_.clipboard->wait_response(id);
    }

    static std::size_t byte_index_for_x(std::string_view s, float local_x, const SkFont& font) {
        return reactcpp::text::byte_index_for_x(
            s,
            local_x,
            [&](std::size_t bytes) {
                return font.measureText(s.data(), bytes, SkTextEncoding::kUTF8);
            }
        );
    }

    struct InputAreaCaretMetrics {
        float top{0.0f};
        float bottom{0.0f};
        float visible_h{0.0f};
        float content_h{0.0f};
    };

    std::optional<InputAreaCaretMetrics> input_area_caret_metrics(const InstanceNode& node) const {
        if (node.type != host_type_input_area()) return std::nullopt;
        if (!node.editable_state) return std::nullopt;

        const auto& props = std::get<InputAreaProps>(node.current_vnode.props);
        const auto& state = *node.editable_state;
        const LayoutRect r = layout_for_node(node);

        const float pad_x = std::max(props.style.padding, 4.0f);
        const float pad_y = std::max(props.style.padding, 4.0f);
        const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);
        const float visible_h = std::max(r.height - pad_y * 2.0f, 1.0f);

        const std::string value = state.value.to_string();
        const std::string preedit = state.preedit;
        const bool composing = !preedit.empty();

        const std::size_t cursor = std::min(state.cursor, value.size());
        const std::string display = composing
            ? (value.substr(0, cursor) + preedit + value.substr(cursor))
            : value;
        const std::size_t caret_index = composing ? (cursor + preedit.size()) : cursor;

        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(font_mgr_));

        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        const float line_height = props.text_size * 1.4f;

        auto measure = [&](std::string_view sv) -> float {
            return font.measureText(sv.data(), sv.size(), SkTextEncoding::kUTF8);
        };

        const auto spans = reactcpp::text::wrap_text_spans(display, content_w, measure);
        if (spans.empty()) return std::nullopt;

        std::size_t line_index = spans.size() - 1;
        std::size_t within = std::min(caret_index, display.size());
        for (std::size_t i = 0; i < spans.size(); ++i) {
            const auto& sp = spans[i];
            if (within < sp.start) {
                line_index = i;
                within = sp.start;
                break;
            }
            if (within <= sp.end) {
                line_index = i;
                within = std::min(within, sp.end);
                break;
            }
        }

        const float baseline = pad_y + static_cast<float>(line_index) * line_height - metrics.fAscent;
        const float top = baseline + metrics.fAscent;
        const float bottom = baseline + metrics.fDescent;
        const float content_h = pad_y + static_cast<float>(spans.size()) * line_height;

        InputAreaCaretMetrics out;
        out.top = top;
        out.bottom = bottom;
        out.visible_h = visible_h;
        out.content_h = content_h;
        return out;
    }

    void ensure_input_area_caret_visible(InstanceNode& node) {
        if (node.type != host_type_input_area()) return;
        if (!node.editable_state) return;

        auto m = input_area_caret_metrics(node);
        if (!m) return;

        auto& state = *node.editable_state;
        const float margin = 4.0f;
        const float max_scroll = std::max(0.0f, m->content_h - m->visible_h);

        float next = std::clamp(state.scroll_y, 0.0f, max_scroll);
        if (m->top < next + margin) {
            next = m->top - margin;
        } else if (m->bottom > next + m->visible_h - margin) {
            next = m->bottom - m->visible_h + margin;
        }
        next = std::clamp(next, 0.0f, max_scroll);
        next = std::round(next);

        if (next != state.scroll_y) {
            state.scroll_y = next;
            mark_dirty(&node);
        }
    }

    void ensure_input_caret_visible(InstanceNode& node) {
        if (node.type != host_type_input()) return;
        if (!node.editable_state) return;

        const auto& props = std::get<InputProps>(node.current_vnode.props);
        auto& state = *node.editable_state;

        const LayoutRect r = layout_for_node(node);
        const float pad_x = 8.0f;
        const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);

        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(font_mgr_));

        const std::string value = state.value.to_string();
        const std::size_t cursor = std::min(state.cursor, value.size());
        const std::string preedit = state.preedit;

        const bool composing = !preedit.empty();
        const std::string display = composing
            ? (value.substr(0, cursor) + preedit + value.substr(cursor))
            : value;

        const std::string_view display_view(display);
        auto measure = [&](std::size_t bytes) -> float {
            bytes = std::min(bytes, display_view.size());
            return font.measureText(display_view.data(), bytes, SkTextEncoding::kUTF8);
        };

        const std::size_t caret_index = composing ? (cursor + preedit.size()) : cursor;
        const float caret_x = measure(caret_index);
        const float total_w = measure(display_view.size());

        const float max_scroll = std::max(0.0f, total_w - content_w);
        float next = std::clamp(state.scroll_x, 0.0f, max_scroll);

        const float margin = 12.0f;
        if (caret_x < next + margin) {
            next = caret_x - margin;
        } else if (caret_x > next + content_w - margin) {
            next = caret_x - (content_w - margin);
        }
        next = std::clamp(next, 0.0f, max_scroll);

        if (next != state.scroll_x) {
            state.scroll_x = next;
            mark_dirty(&node);
        }
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

        const bool was_input = focused_node_ && (focused_node_->type == host_type_input() || focused_node_->type == host_type_input_area());

        if (focused_node_) {
            focused_node_->focused = false;
            if ((focused_node_->type == host_type_input() || focused_node_->type == host_type_input_area()) && focused_node_->editable_state) {
                focused_node_->editable_state->focused = false;
                clear_preedit(*focused_node_->editable_state);
                clear_selection(*focused_node_->editable_state);
            }
            if (auto on_blur = blur_handler_for(focused_node_->current_vnode.props)) {
                (*on_blur)();
            }
        }

        if (was_input) {
            this->clear_composition();
            this->stop_text_input();
            last_text_input_area_.reset();
            last_text_input_cursor_px_ = -1;
        }

        focused_node_ = node;
        focused_input_ = (focused_node_ && (focused_node_->type == host_type_input() || focused_node_->type == host_type_input_area()))
            ? focused_node_
            : nullptr;

        if (focused_node_) {
            focused_node_->focused = true;
            if ((focused_node_->type == host_type_input() || focused_node_->type == host_type_input_area()) && focused_node_->editable_state) {
                focused_node_->editable_state->focused = true;
                focused_node_->editable_state->cursor = std::min(
                    focused_node_->editable_state->cursor,
                    focused_node_->editable_state->value.size()
                );
                reactcpp::text::Selection sel = selection_from(*focused_node_->editable_state);
                selection_to(*focused_node_->editable_state, sel);

                if (focused_node_->type == host_type_input_area()) {
                    ensure_input_area_caret_visible(*focused_node_);
                } else if (focused_node_->type == host_type_input()) {
                    ensure_input_caret_visible(*focused_node_);
                }
            }
            if (auto on_focus = focus_handler_for(focused_node_->current_vnode.props)) {
                (*on_focus)();
            }
        }

        if (focused_node_ && (focused_node_->type == host_type_input() || focused_node_->type == host_type_input_area())) {
            const bool multiline = focused_node_->type == host_type_input_area();
            this->start_text_input(multiline);
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
        if (!focused_input_ || !focused_input_->editable_state) return;

        (void)ctx;

        const LayoutRect lr = layout_for_node(*focused_input_);
        float abs_x = 0.0f;
        float abs_y = 0.0f;
        absolute_origin_for(focused_input_, abs_x, abs_y);

        float rect_x_f = abs_x;
        float rect_y_f = abs_y;
        float rect_w_f = std::max(lr.width, 1.0f);
        float rect_h_f = std::max(lr.height, 1.0f);

        SDL_Rect rect;
        rect.x = static_cast<int>(std::lround(rect_x_f));
        rect.y = static_cast<int>(std::lround(rect_y_f));
        rect.w = static_cast<int>(std::lround(std::max(rect_w_f, 1.0f)));
        rect.h = static_cast<int>(std::lround(std::max(rect_h_f, 1.0f)));

        float text_size = 18.0f;
        if (focused_input_->type == host_type_input()) {
            text_size = std::get<InputProps>(focused_input_->current_vnode.props).text_size;
        } else if (focused_input_->type == host_type_input_area()) {
            text_size = std::get<InputAreaProps>(focused_input_->current_vnode.props).text_size;
        } else {
            return;
        }

        const auto& state = *focused_input_->editable_state;

        const std::size_t cursor = std::min(state.cursor, state.value.size());
        const std::string left_text = state.value.substr(0, cursor);

        SkFont font;
        font.setSize(text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        float cursor_x = 8.0f + font.measureText(left_text.c_str(), left_text.size(), SkTextEncoding::kUTF8);
        float cursor_line_y = 0.0f;
        std::optional<float> ime_line_height_surface;
        if (focused_input_->type == host_type_input_area()) {
            const auto& props = std::get<InputAreaProps>(focused_input_->current_vnode.props);
            const float pad_x = std::max(props.style.padding, 4.0f);
            const float pad_y = std::max(props.style.padding, 4.0f);
            const float content_w = std::max(lr.width - pad_x * 2.0f, 1.0f);

            SkFontMetrics metrics;
            font.getMetrics(&metrics);
            const float line_height = text_size * 1.4f;
            ime_line_height_surface = line_height;

            const std::string value = state.value.to_string();
            const std::size_t cursor = std::min(state.cursor, value.size());
            const std::string preedit = state.preedit;
            const bool composing = !preedit.empty();

            const std::string display = composing
                ? (value.substr(0, cursor) + preedit + value.substr(cursor))
                : value;
            const std::size_t caret_index = composing ? (cursor + preedit.size()) : cursor;

            auto measure = [&](std::string_view s) -> float {
                return font.measureText(s.data(), s.size(), SkTextEncoding::kUTF8);
            };

            const auto spans = reactcpp::text::wrap_text_spans(display, content_w, measure);
            if (!spans.empty()) {
                std::size_t line_index = spans.size() - 1;
                std::size_t line_start = spans.back().start;
                std::size_t within = std::min(caret_index, display.size());

                for (std::size_t i = 0; i < spans.size(); ++i) {
                    const auto& sp = spans[i];
                    if (within < sp.start) {
                        line_index = i;
                        line_start = sp.start;
                        within = sp.start;
                        break;
                    }
                    if (within <= sp.end) {
                        line_index = i;
                        line_start = sp.start;
                        within = std::min(within, sp.end);
                        break;
                    }
                }

                cursor_x = pad_x + measure(std::string_view(display).substr(line_start, within - line_start));
                cursor_line_y = pad_y + static_cast<float>(line_index) * line_height;
                if (composing) {
                    cursor_line_y -= state.scroll_y;
                }
            }
        } else {
            if (!state.preedit.empty()) {
                cursor_x += font.measureText(state.preedit.c_str(), state.preedit.size(), SkTextEncoding::kUTF8);
            }

            cursor_x -= state.scroll_x;
        }

        float cursor_off_f = cursor_x;
        rect_y_f += cursor_line_y;

        rect.y = static_cast<int>(std::lround(rect_y_f));

        if (ime_line_height_surface) {
            const float line_h_f = std::max(*ime_line_height_surface, 1.0f);
            rect.h = static_cast<int>(std::lround(std::max(line_h_f, 1.0f)));
        }

        const int cursor_px = rect.x + static_cast<int>(std::lround(cursor_off_f));

        if (last_text_input_area_ && rect.x == last_text_input_area_->x && rect.y == last_text_input_area_->y
            && rect.w == last_text_input_area_->w && rect.h == last_text_input_area_->h
            && cursor_px == last_text_input_cursor_px_) {
            return;
        }

        if (platform_.cmds) {
            reactcpp::PlatformCommand cmd;
            cmd.type = reactcpp::PlatformCmdType::SetTextInputArea;
            cmd.rect = rect;
            cmd.cursor_px = cursor_px;
            platform_.cmds->push(std::move(cmd));
        }

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
        if (!node.editable_state) return;

        const auto& props = std::get<InputProps>(node.current_vnode.props);
        const std::string value = node.editable_state->value.to_string();
        const std::string preedit = node.editable_state->preedit;
        const std::size_t cursor = std::min(node.editable_state->cursor, value.size());
        const float scroll_x = node.editable_state->scroll_x;

        const float text_x = 8.0f;
        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        const std::string left_text = value.substr(0, cursor);
        float cursor_x = text_x + font.measureText(left_text.c_str(), left_text.size(), SkTextEncoding::kUTF8);
        if (!preedit.empty()) {
            cursor_x += font.measureText(preedit.c_str(), preedit.size(), SkTextEncoding::kUTF8);
        }

        cursor_x -= scroll_x;

        const LayoutRect r = layout_for_node(node);
        const float pad_x = 8.0f;
        const float pad_y = 8.0f;
        const float content_w = std::max(r.width - pad_x * 2.0f, 1.0f);
        const float content_h = std::max(height - pad_y * 2.0f, 1.0f);
        const SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, std::max(r.width, 1.0f), std::max(height, 1.0f));
        const SkRect content_rect = SkRect::MakeXYWH(pad_x, pad_y, content_w, content_h);

        canvas->save();
        SkRRect clip_rr;
        clip_rr.setRectXY(bounds, 6.0f, 6.0f);
        canvas->clipRRect(clip_rr, true);
        canvas->clipRect(content_rect, true);
        SkPaint caret;
        caret.setAntiAlias(true);
        caret.setColor(make_color(0.2f, 0.2f, 0.2f));
        caret.setStrokeWidth(1.5f);
        canvas->drawLine(cursor_x, 8.0f, cursor_x, height - 8.0f, caret);
        canvas->restore();
    }

    static void draw_input_area_caret_if_focused(const InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx, float width, float height) {
        if (!node.focused) return;
        if (node.type != host_type_input_area()) return;
        if (!node.editable_state) return;

        const auto& props = std::get<InputAreaProps>(node.current_vnode.props);
        const auto& state = *node.editable_state;
        const std::string value = state.value.to_string();
        const std::string preedit = state.preedit;
        const bool composing = !preedit.empty();

        const std::size_t cursor = std::min(state.cursor, value.size());
        const std::string display = composing
            ? (value.substr(0, cursor) + preedit + value.substr(cursor))
            : value;
        const std::size_t caret_index = composing ? (cursor + preedit.size()) : cursor;

        const float pad_x = std::max(props.style.padding, 4.0f);
        const float pad_y = std::max(props.style.padding, 4.0f);
        const float content_w = std::max(width - pad_x * 2.0f, 1.0f);
        const float content_h = std::max(height - pad_y * 2.0f, 1.0f);
        const SkRect content_rect = SkRect::MakeXYWH(pad_x, pad_y, content_w, content_h);

        SkFont font;
        font.setSize(props.text_size);
        font.setTypeface(pick_typeface(ctx.font_mgr));

        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        const float line_height = props.text_size * 1.4f;

        auto measure = [&](std::string_view s) -> float {
            return font.measureText(s.data(), s.size(), SkTextEncoding::kUTF8);
        };

        const auto spans = reactcpp::text::wrap_text_spans(display, content_w, measure);
        if (spans.empty()) return;

        std::size_t line_index = spans.size() - 1;
        std::size_t line_start = spans.back().start;
        std::size_t line_end = spans.back().end;
        std::size_t within = std::min(caret_index, display.size());

        for (std::size_t i = 0; i < spans.size(); ++i) {
            const auto& sp = spans[i];
            if (within < sp.start) {
                line_index = i;
                line_start = sp.start;
                line_end = sp.end;
                within = sp.start;
                break;
            }
            if (within <= sp.end) {
                line_index = i;
                line_start = sp.start;
                line_end = sp.end;
                within = std::min(within, sp.end);
                break;
            }
        }

        (void)line_end;
        const float baseline = pad_y + static_cast<float>(line_index) * line_height - metrics.fAscent;
        const float caret_x = pad_x + measure(std::string_view(display).substr(line_start, within - line_start));

        const float top = baseline + metrics.fAscent;
        const float bottom = baseline + metrics.fDescent;

        SkRect bounds = SkRect::MakeXYWH(0.0f, 0.0f, width, height);
        canvas->save();
        SkRRect clip_rr;
        clip_rr.setRectXY(bounds, 6.0f, 6.0f);
        canvas->clipRRect(clip_rr, true);
        canvas->clipRect(content_rect, true);
        canvas->translate(0.0f, -state.scroll_y);

        SkPaint caret;
        caret.setAntiAlias(true);
        caret.setColor(make_color(0.2f, 0.2f, 0.2f));
        caret.setStrokeWidth(1.5f);
        canvas->drawLine(caret_x, top, caret_x, bottom, caret);
        canvas->restore();
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
        auto init_from_value = [&](const std::string& v) {
            InstanceNode::EditableTextState state;
            state.value = reactcpp::text::TextBuffer(v);
            state.value.set_kind(reactcpp::text::TextBuffer::Kind::Gap);
            state.cursor = state.value.size();
            state.sel_start = 0;
            state.sel_end = 0;
            state.sel_anchor = state.cursor;
            state.has_selection = false;
            state.focused = false;
            state.scroll_y = 0.0f;
            clear_preedit(state);
            node.editable_state = std::move(state);
        };

        if (node.type == host_type_input()) {
            const auto& props = std::get<InputProps>(node.current_vnode.props);
            init_from_value(props.value);
        } else if (node.type == host_type_input_area()) {
            const auto& props = std::get<InputAreaProps>(node.current_vnode.props);
            init_from_value(props.value);
        } else {
            node.editable_state.reset();
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
            inst.native_state.reset();
            inst.dirty = true;
            inst.focused = false;
            init_node_state(inst);
            reconcile_children(inst, vnode.children);
            return true;
        }

        if (!props_equal(inst.current_vnode.props, vnode.props)) {
            local_changed = true;
            if ((inst.type == host_type_input() || inst.type == host_type_input_area()) && inst.editable_state) {
                std::string next_value;
                if (inst.type == host_type_input()) {
                    next_value = std::get<InputProps>(vnode.props).value;
                } else {
                    next_value = std::get<InputAreaProps>(vnode.props).value;
                }

                const std::string current = inst.editable_state->value.to_string();
                if (current != next_value) {
                    inst.editable_state->value.set_string(next_value);
                    inst.editable_state->cursor = std::min(inst.editable_state->cursor, inst.editable_state->value.size());
                    clear_preedit(*inst.editable_state);
                    reactcpp::text::Selection sel = selection_from(*inst.editable_state);
                    selection_to(*inst.editable_state, sel);
                    inst.editable_state->sel_anchor = inst.editable_state->cursor;
                }
            }
        }

        inst.current_vnode = vnode;
        (void)reconcile_children(inst, vnode.children);

        if (local_changed || vnode.dirty) {
            inst.dirty = true;
            inst.cached_picture.reset();
            if (inst.type == host_type_rive()) {
                inst.native_state.reset();
            }
        }
        return local_changed;
    }

    static bool refresh_data_driven_props(ElementProps& props) {
        bool changed = false;
        std::visit([&](auto& p) {
            using P = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<P, LineChartProps>
                       || std::is_same_v<P, ScatterChartProps>
                       || std::is_same_v<P, AreaChartProps>) {
                if (p.points_source) {
                    const std::size_t revision = p.points_source->revision();
                    if (p.data_revision != revision) {
                        p.data_revision = revision;
                        changed = true;
                    }
                }
            } else if constexpr (std::is_same_v<P, BarChartProps>) {
                std::size_t revision = 0;
                if (p.values_source) {
                    revision = p.values_source->revision();
                }
                if (p.x_source) {
                    revision ^= p.x_source->revision();
                }
                if ((p.values_source || p.x_source) && p.data_revision != revision) {
                    p.data_revision = revision;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<P, CircleChartProps>) {
                if (p.x_source) {
                    const std::size_t revision = p.x_source->revision();
                    if (p.x_revision != revision) {
                        p.x_revision = revision;
                        changed = true;
                    }
                }
                if (p.y_source) {
                    const std::size_t revision = p.y_source->revision();
                    if (p.y_revision != revision) {
                        p.y_revision = revision;
                        changed = true;
                    }
                }
                if (p.radius_source) {
                    const std::size_t revision = p.radius_source->revision();
                    if (p.r_revision != revision) {
                        p.r_revision = revision;
                        changed = true;
                    }
                }
            } else if constexpr (std::is_same_v<P, HistogramChartProps>) {
                if (p.samples_source) {
                    const std::size_t revision = p.samples_source->revision();
                    if (p.data_revision != revision) {
                        p.data_revision = revision;
                        changed = true;
                    }
                }
            } else if constexpr (std::is_same_v<P, RiveProps>) {
                if (p.inputs_source) {
                    const std::size_t revision = p.inputs_source->revision();
                    if (p.inputs_revision != revision) {
                        p.inputs_revision = revision;
                        changed = true;
                    }
                }
            }
        }, props);
        return changed;
    }

    void refresh_data_driven_nodes(InstanceNode& node) {
        if (refresh_data_driven_props(node.current_vnode.props) || rive_wants_repaint(node)) {
            mark_dirty(&node);
        }
        for (auto& child : node.children) {
            refresh_data_driven_nodes(*child);
        }
    }

    static SkRect local_recording_bounds(const Element& vnode) {
        const auto& props = props_as_view_ref(vnode);
        const float w = std::max(props.style.width.value_or(1.0f), 1.0f);
        const float h = std::max(props.style.height.value_or(1.0f), 1.0f);
        return SkRect::MakeXYWH(0.0f, 0.0f, w, h);
    }

    static bool bypass_node_picture_cache(TypeId type) {
        return type == host_type_rive()
            || type == host_type_line_chart()
            || type == host_type_scatter_chart()
            || type == host_type_area_chart()
            || type == host_type_bar_chart()
            || type == host_type_circle_chart()
            || type == host_type_histogram_chart();
    }

    void render_cached_node(InstanceNode& node, SkCanvas* canvas, const DrawContext& ctx) {
        const LayoutRect r = layout_for_node(node);
        canvas->save();
        canvas->translate(r.x, r.y);

        if (bypass_node_picture_cache(node.type)) {
            node.cached_picture.reset();
            renderer_for(node.type).on_draw(node, canvas, ctx);
            node.dirty = false;
        } else if (node.dirty || !node.cached_picture) {
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

        if (node.cached_picture) {
            canvas->drawPicture(node.cached_picture.get());
        }
        for (auto& child : node.children) {
            render_cached_node(*child, canvas, ctx);
        }

        draw_input_caret_if_focused(node, canvas, ctx, r.height);
        draw_input_area_caret_if_focused(node, canvas, ctx, r.width, r.height);
        draw_focus_ring(node, canvas, r.width, r.height);

        canvas->restore();
    }

    static void collect_cached_pictures(
        const InstanceNode& node,
        std::vector<std::shared_ptr<SkPicture>>& out
    ) {
        if (node.cached_picture) {
            out.push_back(node.cached_picture);
        }
        for (const auto& child : node.children) {
            collect_cached_pictures(*child, out);
        }
    }

    AppRenderFunc app_render_;
    InstanceNode root_instance_{};
    InstanceNode* focused_node_{nullptr};
    InstanceNode* focused_input_{nullptr};
    sk_sp<SkFontMgr> font_mgr_;
    bool update_requested_{true};

    bool mouse_selecting_{false};
    InstanceNode* mouse_select_target_{nullptr};
    ContextMenuState context_menu_{};

    int surface_width_{0};
    int surface_height_{0};

    reactcpp::PlatformBridge platform_{};
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

TypeId host_type_input_area() {
    static int dummy;
    return &dummy;
}

TypeId host_type_drawio_diagram() {
    static int dummy;
    return &dummy;
}

TypeId host_type_rive() {
    static int dummy;
    return &dummy;
}

TypeId host_type_line_chart() {
    static int dummy;
    return &dummy;
}

TypeId host_type_scatter_chart() {
    static int dummy;
    return &dummy;
}

TypeId host_type_area_chart() {
    static int dummy;
    return &dummy;
}

TypeId host_type_bar_chart() {
    static int dummy;
    return &dummy;
}

TypeId host_type_circle_chart() {
    static int dummy;
    return &dummy;
}

TypeId host_type_histogram_chart() {
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

Element InputArea(const InputAreaProps& props, std::vector<Element> children) {
    Element e;
    e.type = host_type_input_area();
    e.props = props;
    e.children = std::move(children);
    return e;
}

Element DrawioDiagram(const DrawioDiagramProps& props) {
    Element e;
    e.type = host_type_drawio_diagram();
    e.props = props;
    return e;
}

Element Rive(const RiveProps& props) {
    Element e;
    e.type = host_type_rive();
    e.props = props;
    return e;
}

Element LineChart(const LineChartProps& props) {
    Element e;
    e.type = host_type_line_chart();
    e.props = props;
    return e;
}

Element ScatterChart(const ScatterChartProps& props) {
    Element e;
    e.type = host_type_scatter_chart();
    e.props = props;
    return e;
}

Element AreaChart(const AreaChartProps& props) {
    Element e;
    e.type = host_type_area_chart();
    e.props = props;
    return e;
}

Element BarChart(const BarChartProps& props) {
    Element e;
    e.type = host_type_bar_chart();
    e.props = props;
    return e;
}

Element CircleChart(const CircleChartProps& props) {
    Element e;
    e.type = host_type_circle_chart();
    e.props = props;
    return e;
}

Element HistogramChart(const HistogramChartProps& props) {
    Element e;
    e.type = host_type_histogram_chart();
    e.props = props;
    return e;
}

int run_react_app(const AppRenderFunc& app) {
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    std::optional<std::string> gl_failure;

    try {

    (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    (void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    (void)SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    (void)SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const auto [initial_w, initial_h] = compute_initial_window_size_60pct();

    SDL_Window* window = SDL_CreateWindow(
        "ReactCpp GUI Demo",
        initial_w,
        initial_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) {
        SDL_DestroyWindow(window);
        throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    if (!SDL_GL_MakeCurrent(window, glctx)) {
        SDL_GL_DestroyContext(glctx);
        SDL_DestroyWindow(window);
        throw std::runtime_error(std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError());
    }
    (void)SDL_GL_SetSwapInterval(1);

    auto get_proc = [](void*, const char name[]) -> GrGLFuncPtr {
        return reinterpret_cast<GrGLFuncPtr>(SDL_GL_GetProcAddress(name));
    };

    auto gl_interface = GrGLMakeAssembledInterface(nullptr, get_proc);
    if (!gl_interface || !gl_interface->validate()) {
        SDL_GL_DestroyContext(glctx);
        SDL_DestroyWindow(window);
        throw std::runtime_error("GrGLMakeAssembledInterface failed");
    }

    auto gr = GrDirectContexts::MakeGL(gl_interface);
    if (!gr) {
        SDL_GL_DestroyContext(glctx);
        SDL_DestroyWindow(window);
        throw std::runtime_error("GrDirectContexts::MakeGL failed");
    }

    using GlBindFramebufferFn = void (*)(GLenum, GLuint);
    const GlBindFramebufferFn gl_bind_framebuffer =
        reinterpret_cast<GlBindFramebufferFn>(SDL_GL_GetProcAddress("glBindFramebuffer"));

    auto make_surface = [&](int w, int h) -> sk_sp<SkSurface> {
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        GLint samples = 0;
        glGetIntegerv(GL_SAMPLES, &samples);
        GLint stencil = 0;
        glGetIntegerv(GL_STENCIL_BITS, &stencil);

        GrGLFramebufferInfo fb_info;
        fb_info.fFBOID = static_cast<GrGLuint>(fbo);
        fb_info.fFormat = GL_RGBA8;

        GrBackendRenderTarget backend_rt = GrBackendRenderTargets::MakeGL(w, h, samples, stencil, fb_info);
        if (!backend_rt.isValid()) return nullptr;

        return SkSurfaces::WrapBackendRenderTarget(
            gr.get(),
            backend_rt,
            kBottomLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType,
            nullptr,
            nullptr
        );
    };

    GLint last_bound_fbo = -1;

    int pix_w = initial_w;
    int pix_h = initial_h;
    (void)SDL_GetWindowSizeInPixels(window, &pix_w, &pix_h);
    if (pix_w <= 0 || pix_h <= 0) {
        pix_w = initial_w;
        pix_h = initial_h;
    }

    sk_sp<SkSurface> surface = make_surface(pix_w, pix_h);
    if (!surface) {
        SDL_GL_DestroyContext(glctx);
        SDL_DestroyWindow(window);
        throw std::runtime_error("SkSurfaces::WrapBackendRenderTarget failed");
    }

    reactcpp::UiEventQueue ui_events;
    reactcpp::FrameMailbox frames;
    reactcpp::PlatformCommandQueue platform_cmds;
    reactcpp::ClipboardRpc clipboard;
    reactcpp::PlatformBridge platform{&platform_cmds, &clipboard};

    std::atomic<int> shared_pix_w{pix_w};
    std::atomic<int> shared_pix_h{pix_h};

    std::atomic<bool> worker_running{true};
    const int worker_start_w = shared_pix_w.load(std::memory_order_acquire);
    const int worker_start_h = shared_pix_h.load(std::memory_order_acquire);
    std::thread worker([
        app,
        platform,
        &ui_events,
        &frames,
        &shared_pix_w,
        &shared_pix_h,
        &worker_running,
        worker_start_w,
        worker_start_h
    ] {
        register_repaint_queue(&ui_events);
        SkiaRuntime runtime(app, platform, worker_start_w, worker_start_h);
        std::uint64_t frame_id = 0;

        auto publish = [&] {
            const int w = shared_pix_w.load(std::memory_order_acquire);
            const int h = shared_pix_h.load(std::memory_order_acquire);
            reactcpp::Frame f = runtime.render_to_frame(w, h);
            f.frame_id = ++frame_id;
            frames.publish(std::move(f));
        };

        publish();

        while (worker_running.load(std::memory_order_acquire)) {
            reactcpp::UiEvent ev;
            if (!ui_events.pop_wait(ev)) break;
            if (ev.type == reactcpp::UiEventType::Quit) break;

            switch (ev.type) {
            case reactcpp::UiEventType::MouseButtonDown:
                runtime.handle_mouse_button_down(ev.x, ev.y, ev.clicks, ev.mouse_button);
                break;
            case reactcpp::UiEventType::MouseMotion:
                runtime.handle_mouse_move(ev.x, ev.y);
                break;
            case reactcpp::UiEventType::MouseButtonUp:
                runtime.handle_mouse_button_up(ev.x, ev.y);
                break;
            case reactcpp::UiEventType::MouseWheel:
                runtime.handle_mouse_wheel(ev.wheel_y);
                break;
            case reactcpp::UiEventType::Resize:
                break;
            case reactcpp::UiEventType::Tick:
                runtime.perform_update_if_needed();
                clear_repaint_tick_pending();
                break;
            case reactcpp::UiEventType::TextEditing:
                runtime.handle_text_editing(ev.text.c_str(), ev.edit_start, ev.edit_length);
                break;
            case reactcpp::UiEventType::TextInput:
                runtime.handle_text_input(ev.text.c_str());
                break;
            case reactcpp::UiEventType::KeyDown:
                runtime.handle_key_down(ev.key, ev.mod, ev.repeat);
                break;
            case reactcpp::UiEventType::Quit:
                break;
            }

            publish();
        }

        unregister_repaint_queue(&ui_events);
        ui_events.stop();
    });

    auto apply_platform_cmds = [&] {
        while (true) {
            auto opt = platform_cmds.try_pop();
            if (!opt) break;
            const auto& cmd = *opt;
            switch (cmd.type) {
            case reactcpp::PlatformCmdType::StartTextInput: {
                const SDL_PropertiesID props = SDL_CreateProperties();
                if (props != 0) {
                    (void)SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, cmd.multiline);
                    (void)SDL_StartTextInputWithProperties(window, props);
                    SDL_DestroyProperties(props);
                } else {
                    (void)SDL_StartTextInput(window);
                }
                break;
            }
            case reactcpp::PlatformCmdType::StopTextInput:
                (void)SDL_StopTextInput(window);
                break;
            case reactcpp::PlatformCmdType::ClearComposition:
                (void)SDL_ClearComposition(window);
                break;
            case reactcpp::PlatformCmdType::SetTextInputArea:
                (void)SDL_SetTextInputArea(window, &cmd.rect, cmd.cursor_px);
                break;
            case reactcpp::PlatformCmdType::SetClipboardText:
                (void)SDL_SetClipboardText(cmd.text.c_str());
                break;
            case reactcpp::PlatformCmdType::GetClipboardText: {
                char* clip = SDL_GetClipboardText();
                std::string text = clip ? std::string(clip) : std::string();
                if (clip) SDL_free(clip);
                clipboard.set_response(cmd.request_id, std::move(text));
                break;
            }
            case reactcpp::PlatformCmdType::SetMouseCapture:
                (void)SDL_CaptureMouse(cmd.mouse_capture);
                break;
            }
        }
    };

    reactcpp::Frame last_frame;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == REACTCPP_SDL_EVENT_QUIT) {
                running = false;
                break;
            }

            if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN
                    && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::MouseButtonDown;
                ev.x = static_cast<float>(e.button.x);
                ev.y = static_cast<float>(e.button.y);
                ev.clicks = e.button.clicks;
                ev.mouse_button = e.button.button;
                ui_events.push(std::move(ev));
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_MOTION) {
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    reactcpp::UiEvent ev;
                    ev.type = reactcpp::UiEventType::MouseMotion;
                    ev.x = static_cast<float>(e.motion.x);
                    ev.y = static_cast<float>(e.motion.y);
                    ui_events.push(std::move(ev));
                }
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::MouseButtonUp;
                ev.x = static_cast<float>(e.button.x);
                ev.y = static_cast<float>(e.button.y);
                ui_events.push(std::move(ev));
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_WHEEL) {
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::MouseWheel;
                ev.wheel_y = e.wheel.y;
                ui_events.push(std::move(ev));
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_EDITING) {
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::TextEditing;
                ev.text = e.edit.text ? std::string(e.edit.text) : std::string();
                ev.edit_start = e.edit.start;
                ev.edit_length = e.edit.length;
                ui_events.push(std::move(ev));
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_INPUT) {
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::TextInput;
                ev.text = e.text.text ? std::string(e.text.text) : std::string();
                ui_events.push(std::move(ev));
            } else if (e.type == REACTCPP_SDL_EVENT_KEY_DOWN) {
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::KeyDown;
                ev.key = e.key.key;
                ev.mod = e.key.mod;
                ev.repeat = e.key.repeat;
                ui_events.push(std::move(ev));
            }
        }

        apply_platform_cmds();

        bool need_present = false;
        reactcpp::Frame f;
        if (frames.try_consume(f) && f.picture) {
            last_frame = std::move(f);
            need_present = true;
        }

        int next_w = pix_w;
        int next_h = pix_h;
        if (SDL_GetWindowSizeInPixels(window, &next_w, &next_h) && next_w > 0 && next_h > 0) {
            if (next_w != pix_w || next_h != pix_h) {
                pix_w = next_w;
                pix_h = next_h;
                sk_sp<SkSurface> next_surface = make_surface(pix_w, pix_h);
                if (next_surface) {
                    surface = std::move(next_surface);
                    glViewport(0, 0, pix_w, pix_h);
                }

                shared_pix_w.store(pix_w, std::memory_order_release);
                shared_pix_h.store(pix_h, std::memory_order_release);
                reactcpp::UiEvent ev;
                ev.type = reactcpp::UiEventType::Resize;
                ev.width = pix_w;
                ev.height = pix_h;
                ui_events.push(std::move(ev));

                need_present = true;
            }
        }

        if (!need_present) {
            SDL_Delay(1);
            continue;
        }

        GLint bound_fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound_fbo);
        if (bound_fbo != last_bound_fbo) {
            last_bound_fbo = bound_fbo;
            sk_sp<SkSurface> next_surface = make_surface(pix_w, pix_h);
            if (next_surface) {
                surface = std::move(next_surface);
            }
        }

        SkCanvas* canvas = surface->getCanvas();
        if (last_frame.picture) {
            canvas->drawPicture(last_frame.picture);
        } else {
            canvas->clear(SK_ColorWHITE);
        }
        skgpu::ganesh::FlushAndSubmit(surface.get());
        gr->submit(GrSyncCpu::kYes);
        if (gl_bind_framebuffer) {
            gl_bind_framebuffer(GL_FRAMEBUFFER, 0);
        }
        (void)SDL_GL_SwapWindow(window);

        SDL_Delay(1);
    }

    worker_running.store(false, std::memory_order_release);
    ui_events.push(reactcpp::UiEvent{reactcpp::UiEventType::Quit});
    ui_events.stop();
    if (worker.joinable()) {
        worker.join();
    }

    surface.reset();
    gr.reset();
    SDL_GL_DestroyContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
    } catch (const std::exception& e) {
        gl_failure = e.what();
    }

    if (gl_failure) {
        log_gl_to_cpu_fallback(*gl_failure);
    }

    const auto [width, height] = compute_initial_window_size_60pct();

    SDL_Window* window = SDL_CreateWindow(
        "ReactCpp GUI Demo",
        width,
        height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    int pix_w = width;
    int pix_h = height;
    (void)SDL_GetWindowSizeInPixels(window, &pix_w, &pix_h);
    if (pix_w <= 0 || pix_h <= 0) {
        pix_w = width;
        pix_h = height;
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
        pix_w,
        pix_h
    );
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    }

    SkImageInfo info = SkImageInfo::Make(
        pix_w,
        pix_h,
        kBGRA_8888_SkColorType,
        kPremul_SkAlphaType
    );

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(pix_w) * pix_h);
    auto surface = SkSurfaces::WrapPixels(
        info,
        pixels.data(),
        static_cast<size_t>(pix_w) * 4
    );
    if (!surface) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SkSurface::MakeRasterDirect failed");
    }

    reactcpp::PlatformCommandQueue platform_cmds;
    reactcpp::ClipboardRpc clipboard;
    reactcpp::PlatformBridge platform{&platform_cmds, &clipboard};

    SkiaRuntime runtime(app, platform, pix_w, pix_h);
    runtime.perform_update_if_needed();

    auto apply_platform_cmds = [&] {
        while (true) {
            auto opt = platform_cmds.try_pop();
            if (!opt) break;
            const auto& cmd = *opt;
            switch (cmd.type) {
            case reactcpp::PlatformCmdType::StartTextInput: {
                const SDL_PropertiesID props = SDL_CreateProperties();
                if (props != 0) {
                    (void)SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, cmd.multiline);
                    (void)SDL_StartTextInputWithProperties(window, props);
                    SDL_DestroyProperties(props);
                } else {
                    (void)SDL_StartTextInput(window);
                }
                break;
            }
            case reactcpp::PlatformCmdType::StopTextInput:
                (void)SDL_StopTextInput(window);
                break;
            case reactcpp::PlatformCmdType::ClearComposition:
                (void)SDL_ClearComposition(window);
                break;
            case reactcpp::PlatformCmdType::SetTextInputArea:
                (void)SDL_SetTextInputArea(window, &cmd.rect, cmd.cursor_px);
                break;
            case reactcpp::PlatformCmdType::SetClipboardText:
                (void)SDL_SetClipboardText(cmd.text.c_str());
                break;
            case reactcpp::PlatformCmdType::GetClipboardText: {
                char* clip = SDL_GetClipboardText();
                std::string text = clip ? std::string(clip) : std::string();
                if (clip) SDL_free(clip);
                clipboard.set_response(cmd.request_id, std::move(text));
                break;
            }
            case reactcpp::PlatformCmdType::SetMouseCapture:
                (void)SDL_CaptureMouse(cmd.mouse_capture);
                break;
            }
        }
    };

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == REACTCPP_SDL_EVENT_QUIT) {
                running = false;
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_DOWN
                    && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                runtime.handle_mouse_button_down(
                    static_cast<float>(e.button.x),
                    static_cast<float>(e.button.y),
                    e.button.clicks,
                    e.button.button
                );
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_MOTION) {
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    runtime.handle_mouse_move(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
                }
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                runtime.handle_mouse_button_up(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
            } else if (e.type == REACTCPP_SDL_EVENT_MOUSE_WHEEL) {
                runtime.handle_mouse_wheel(e.wheel.y);
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_EDITING) {
                runtime.handle_text_editing(e.edit.text, e.edit.start, e.edit.length);
            } else if (e.type == REACTCPP_SDL_EVENT_TEXT_INPUT) {
                runtime.handle_text_input(e.text.text);
            } else if (e.type == REACTCPP_SDL_EVENT_KEY_DOWN) {
                runtime.handle_key_down(e.key.key, e.key.mod, e.key.repeat);
            }
        }

        apply_platform_cmds();

        int next_w = pix_w;
        int next_h = pix_h;
        if (SDL_GetWindowSizeInPixels(window, &next_w, &next_h) && next_w > 0 && next_h > 0) {
            if (next_w != pix_w || next_h != pix_h) {
                pix_w = next_w;
                pix_h = next_h;

                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(
                    renderer,
                    SDL_PIXELFORMAT_BGRA8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    pix_w,
                    pix_h
                );
                if (!texture) {
                    throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
                }

                info = SkImageInfo::Make(
                    pix_w,
                    pix_h,
                    kBGRA_8888_SkColorType,
                    kPremul_SkAlphaType
                );
                pixels.assign(static_cast<std::size_t>(pix_w) * pix_h, 0);
                surface = SkSurfaces::WrapPixels(
                    info,
                    pixels.data(),
                    static_cast<size_t>(pix_w) * 4
                );
                if (!surface) {
                    throw std::runtime_error("SkSurface::MakeRasterDirect failed");
                }
            }
        }

        runtime.perform_update_if_needed();

        SkCanvas* canvas = surface->getCanvas();
        runtime.draw(canvas, pix_w, pix_h);

        void* texPixels = nullptr;
        int pitch = 0;
        if (!SDL_LockTexture(texture, nullptr, &texPixels, &pitch)) {
            throw std::runtime_error(std::string("SDL_LockTexture failed: ") + SDL_GetError());
        }

        for (int y = 0; y < pix_h; ++y) {
            std::memcpy(
                static_cast<std::uint8_t*>(texPixels) + y * pitch,
                pixels.data() + static_cast<std::size_t>(y) * pix_w,
                static_cast<std::size_t>(pix_w) * 4
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
