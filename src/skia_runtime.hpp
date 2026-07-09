#pragma once

#include <any>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cstddef>

#include "text_edit.hpp"
#include "text_buffer.hpp"

namespace reactcpp {

void request_repaint();

struct LinePoint {
    double x{0.0};
    double y{0.0};

    bool operator==(const LinePoint&) const = default;
};

class DataSourceBase {
public:
    virtual ~DataSourceBase() = default;
    virtual std::size_t revision() const = 0;
};

class RiveInputs : public DataSourceBase {
public:
    struct Snapshot {
        std::map<std::string, double> numbers;
        std::map<std::string, bool> bools;
        double time_scale{1.0};
        std::size_t revision{0};
    };

    std::size_t revision() const override {
        return revision_.load(std::memory_order_acquire);
    }

    void set_number(std::string name, double value) {
        {
            std::lock_guard lock(mu_);
            numbers_[std::move(name)] = value;
        }
        bump_revision();
    }

    void set_bool(std::string name, bool value) {
        {
            std::lock_guard lock(mu_);
            bools_[std::move(name)] = value;
        }
        bump_revision();
    }

    void set_time_scale(double value) {
        {
            std::lock_guard lock(mu_);
            time_scale_ = value;
        }
        bump_revision();
    }

    Snapshot snapshot() const {
        std::lock_guard lock(mu_);
        return Snapshot{
            numbers_,
            bools_,
            time_scale_,
            revision_.load(std::memory_order_acquire)
        };
    }

private:
    void bump_revision() {
        revision_.fetch_add(1, std::memory_order_release);
        reactcpp::request_repaint();
    }

    mutable std::mutex mu_;
    std::map<std::string, double> numbers_;
    std::map<std::string, bool> bools_;
    double time_scale_{1.0};
    std::atomic_size_t revision_{0};
};

template <typename T>
struct DataPointTimeExtractor {
    static std::optional<double> get(const T&) {
        return std::nullopt;
    }
};

template <typename T>
class VectorDataSource : public DataSourceBase {
public:
    explicit VectorDataSource(std::vector<T> initial = {})
        : data_(std::move(initial)) {
        prune_locked();
    }

    VectorDataSource(const VectorDataSource&) = delete;
    VectorDataSource& operator=(const VectorDataSource&) = delete;

    void set_window_size(std::size_t max_points) {
        {
            std::lock_guard lock(mu_);
            max_points_ = max_points;
            prune_locked();
        }
        bump_revision();
    }

    void set_time_window(std::optional<double> seconds) {
        {
            std::lock_guard lock(mu_);
            if (seconds && *seconds > 0.0) {
                time_window_ = seconds;
            } else {
                time_window_.reset();
            }
            prune_locked();
        }
        bump_revision();
    }

    void resize_window(std::size_t max_points) {
        set_window_size(max_points);
    }

    std::size_t window_size() const {
        std::lock_guard lock(mu_);
        return max_points_;
    }

    std::optional<double> time_window() const {
        std::lock_guard lock(mu_);
        return time_window_;
    }

    std::size_t revision() const override {
        return revision_.load(std::memory_order_acquire);
    }

    std::size_t size() const {
        std::lock_guard lock(mu_);
        return data_.size();
    }

    void clear() {
        {
            std::lock_guard lock(mu_);
            data_.clear();
        }
        bump_revision();
    }

    void replace(std::vector<T> next) {
        {
            std::lock_guard lock(mu_);
            data_ = std::move(next);
            prune_locked();
        }
        bump_revision();
    }

    template <typename Fn>
    void update(Fn&& fn) {
        {
            std::lock_guard lock(mu_);
            fn(data_);
            prune_locked();
        }
        bump_revision();
    }

    void push_back(T value) {
        {
            std::lock_guard lock(mu_);
            data_.push_back(std::move(value));
            prune_locked();
        }
        bump_revision();
    }

    std::vector<T> snapshot() const {
        std::lock_guard lock(mu_);
        return data_;
    }

private:
    void prune_locked() {
        if (max_points_ > 0 && data_.size() > max_points_) {
            const auto drop = data_.size() - max_points_;
            data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(drop));
        }

        if (!time_window_ || data_.empty()) {
            return;
        }

        const auto newest_t = DataPointTimeExtractor<T>::get(data_.back());
        if (!newest_t) {
            return;
        }

        const double cutoff = *newest_t - *time_window_;
        while (!data_.empty()) {
            const auto front_t = DataPointTimeExtractor<T>::get(data_.front());
            if (!front_t || *front_t >= cutoff) {
                break;
            }
            data_.erase(data_.begin());
        }
    }

    void bump_revision() {
        revision_.fetch_add(1, std::memory_order_release);
        reactcpp::request_repaint();
    }

    mutable std::mutex mu_;
    std::vector<T> data_;
    std::atomic_size_t revision_{0};
    std::size_t max_points_{0};
    std::optional<double> time_window_;
};

template <>
struct DataPointTimeExtractor<LinePoint> {
    static std::optional<double> get(const LinePoint& p) {
        return p.x;
    }
};
}

class SkCanvas;
class SkPicture;

using TypeId = const void*;

enum class ChartLegendPlacement {
    None,
    Right,
    Bottom
};

enum class ChartMarker {
    Circle,
    Square,
    Triangle
};

enum class HistogramAlgorithm {
    Automatic,
    Scott,
    FD,
    Sturges,
    Sqrt,
    Integers
};

enum class HistogramNormalization {
    Count,
    CountDensity,
    Probability,
    PDF,
    CDF
};

enum class ChartGrid {
    None,
    Horizontal,
    Vertical,
    Both
};

enum class ChartTheme {
    Matplotlib,
    Light,
    Dark,
    Blue,
    Seaborn,
    SolarizedDark,
    SolarizedLight,
    Monochrome,
    HighContrast
};

struct ChartStyle {
    std::string title{};
    bool show_title{true};
    float title_size{22.0f};
    float title_r{0.15f};
    float title_g{0.18f};
    float title_b{0.23f};
    float title_a{1.0f};

    std::string x_label{};
    std::string y_label{};
    bool show_axis_labels{true};
    float axis_label_size{14.0f};
    float axis_label_r{0.20f};
    float axis_label_g{0.20f};
    float axis_label_b{0.20f};
    float axis_label_a{1.0f};

    float tick_label_size{10.0f};
    float tick_label_r{0.42f};
    float tick_label_g{0.44f};
    float tick_label_b{0.50f};
    float tick_label_a{1.0f};

    bool draw_axes{true};
    bool show_tick_labels{true};
    int tick_count{6};
    bool draw_grid{true};
    ChartGrid grid_mode{ChartGrid::Both};

    float axis_width{1.0f};
    float axis_r{0.20f};
    float axis_g{0.23f};
    float axis_b{0.28f};
    float axis_a{1.0f};

    float grid_width{0.8f};
    bool grid_dashed{false};
    float grid_r{0.63f};
    float grid_g{0.67f};
    float grid_b{0.72f};
    float grid_a{0.45f};

    float palette_line_r{0.067f};
    float palette_line_g{0.204f};
    float palette_line_b{0.561f};
    float palette_line_a{1.0f};

    float palette_marker_r{0.067f};
    float palette_marker_g{0.204f};
    float palette_marker_b{0.561f};
    float palette_marker_a{1.0f};

    float palette_fill_r{0.067f};
    float palette_fill_g{0.204f};
    float palette_fill_b{0.561f};
    float palette_fill_a{0.25f};

    ChartTheme theme{ChartTheme::Matplotlib};

    bool operator==(const ChartStyle&) const = default;
};

inline void apply_chart_theme_preset(ChartStyle& style, ChartTheme theme) {
    style.theme = theme;

    if (theme == ChartTheme::Matplotlib || theme == ChartTheme::Light) {
        style.title_r = 0.15f;
        style.title_g = 0.18f;
        style.title_b = 0.23f;
        style.axis_label_r = 0.20f;
        style.axis_label_g = 0.20f;
        style.axis_label_b = 0.20f;
        style.tick_label_r = 0.42f;
        style.tick_label_g = 0.44f;
        style.tick_label_b = 0.50f;
        style.axis_r = 0.20f;
        style.axis_g = 0.23f;
        style.axis_b = 0.28f;
        style.grid_r = 0.63f;
        style.grid_g = 0.67f;
        style.grid_b = 0.72f;
        style.grid_dashed = false;

        style.palette_line_r = 0.067f;
        style.palette_line_g = 0.204f;
        style.palette_line_b = 0.561f;
        style.palette_line_a = 1.0f;

        style.palette_marker_r = 0.071f;
        style.palette_marker_g = 0.247f;
        style.palette_marker_b = 0.596f;
        style.palette_marker_a = 1.0f;

        style.palette_fill_r = 0.067f;
        style.palette_fill_g = 0.204f;
        style.palette_fill_b = 0.561f;
        style.palette_fill_a = 0.25f;
        return;
    }

    if (theme == ChartTheme::Dark) {
        style.title_r = 0.95f;
        style.title_g = 0.96f;
        style.title_b = 0.99f;
        style.axis_label_r = 0.95f;
        style.axis_label_g = 0.95f;
        style.axis_label_b = 0.95f;
        style.tick_label_r = 0.86f;
        style.tick_label_g = 0.90f;
        style.tick_label_b = 0.96f;
        style.axis_r = 0.80f;
        style.axis_g = 0.85f;
        style.axis_b = 0.90f;
        style.grid_r = 0.45f;
        style.grid_g = 0.50f;
        style.grid_b = 0.58f;
        style.grid_dashed = true;
        style.axis_a = 0.95f;
        style.grid_a = 0.55f;

        style.palette_line_r = 0.49f;
        style.palette_line_g = 0.78f;
        style.palette_line_b = 1.0f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.49f;
        style.palette_marker_g = 0.78f;
        style.palette_marker_b = 1.0f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.49f;
        style.palette_fill_g = 0.78f;
        style.palette_fill_b = 1.0f;
        style.palette_fill_a = 0.22f;
        return;
    }

    if (theme == ChartTheme::Blue) {
        style.title_r = 0.12f;
        style.title_g = 0.26f;
        style.title_b = 0.60f;
        style.axis_label_r = 0.15f;
        style.axis_label_g = 0.31f;
        style.axis_label_b = 0.63f;
        style.tick_label_r = 0.24f;
        style.tick_label_g = 0.33f;
        style.tick_label_b = 0.52f;
        style.axis_r = 0.20f;
        style.axis_g = 0.36f;
        style.axis_b = 0.72f;
        style.grid_r = 0.63f;
        style.grid_g = 0.76f;
        style.grid_b = 0.96f;
        style.grid_dashed = false;

        style.palette_line_r = 0.11f;
        style.palette_line_g = 0.43f;
        style.palette_line_b = 0.82f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.11f;
        style.palette_marker_g = 0.33f;
        style.palette_marker_b = 0.55f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.11f;
        style.palette_fill_g = 0.43f;
        style.palette_fill_b = 0.82f;
        style.palette_fill_a = 0.25f;
        return;
    }

    if (theme == ChartTheme::Seaborn) {
        style.title_r = 0.20f;
        style.title_g = 0.30f;
        style.title_b = 0.52f;
        style.axis_label_r = 0.24f;
        style.axis_label_g = 0.27f;
        style.axis_label_b = 0.33f;
        style.tick_label_r = 0.43f;
        style.tick_label_g = 0.47f;
        style.tick_label_b = 0.55f;
        style.axis_r = 0.28f;
        style.axis_g = 0.33f;
        style.axis_b = 0.43f;
        style.grid_r = 0.85f;
        style.grid_g = 0.87f;
        style.grid_b = 0.92f;
        style.grid_dashed = true;
        style.axis_a = 0.96f;
        style.grid_a = 0.45f;

        style.palette_line_r = 0.14f;
        style.palette_line_g = 0.33f;
        style.palette_line_b = 0.73f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.20f;
        style.palette_marker_g = 0.38f;
        style.palette_marker_b = 0.76f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.14f;
        style.palette_fill_g = 0.33f;
        style.palette_fill_b = 0.73f;
        style.palette_fill_a = 0.20f;
        return;
    }

    if (theme == ChartTheme::SolarizedDark) {
        style.title_r = 0.66f;
        style.title_g = 0.80f;
        style.title_b = 0.82f;
        style.axis_label_r = 0.51f;
        style.axis_label_g = 0.58f;
        style.axis_label_b = 0.59f;
        style.tick_label_r = 0.47f;
        style.tick_label_g = 0.54f;
        style.tick_label_b = 0.56f;
        style.axis_r = 0.70f;
        style.axis_g = 0.77f;
        style.axis_b = 0.80f;
        style.grid_r = 0.34f;
        style.grid_g = 0.39f;
        style.grid_b = 0.44f;
        style.grid_dashed = true;
        style.axis_a = 0.92f;
        style.grid_a = 0.36f;

        style.palette_line_r = 0.51f;
        style.palette_line_g = 0.58f;
        style.palette_line_b = 0.73f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.51f;
        style.palette_marker_g = 0.58f;
        style.palette_marker_b = 0.73f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.51f;
        style.palette_fill_g = 0.58f;
        style.palette_fill_b = 0.73f;
        style.palette_fill_a = 0.22f;
        return;
    }

    if (theme == ChartTheme::SolarizedLight) {
        style.title_r = 0.12f;
        style.title_g = 0.23f;
        style.title_b = 0.31f;
        style.axis_label_r = 0.28f;
        style.axis_label_g = 0.48f;
        style.axis_label_b = 0.50f;
        style.tick_label_r = 0.47f;
        style.tick_label_g = 0.59f;
        style.tick_label_b = 0.58f;
        style.axis_r = 0.38f;
        style.axis_g = 0.49f;
        style.axis_b = 0.57f;
        style.grid_r = 0.84f;
        style.grid_g = 0.87f;
        style.grid_b = 0.86f;
        style.grid_dashed = false;
        style.axis_a = 0.95f;
        style.grid_a = 0.50f;

        style.palette_line_r = 0.15f;
        style.palette_line_g = 0.49f;
        style.palette_line_b = 0.65f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.14f;
        style.palette_marker_g = 0.54f;
        style.palette_marker_b = 0.71f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.15f;
        style.palette_fill_g = 0.49f;
        style.palette_fill_b = 0.65f;
        style.palette_fill_a = 0.20f;
        return;
    }

    if (theme == ChartTheme::Monochrome) {
        style.title_r = 0.10f;
        style.title_g = 0.10f;
        style.title_b = 0.10f;
        style.axis_label_r = 0.20f;
        style.axis_label_g = 0.20f;
        style.axis_label_b = 0.20f;
        style.tick_label_r = 0.33f;
        style.tick_label_g = 0.33f;
        style.tick_label_b = 0.33f;
        style.axis_r = 0.12f;
        style.axis_g = 0.12f;
        style.axis_b = 0.12f;
        style.grid_r = 0.73f;
        style.grid_g = 0.73f;
        style.grid_b = 0.73f;
        style.grid_dashed = true;
        style.axis_a = 0.92f;
        style.grid_a = 0.42f;

        style.palette_line_r = 0.06f;
        style.palette_line_g = 0.06f;
        style.palette_line_b = 0.06f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.20f;
        style.palette_marker_g = 0.20f;
        style.palette_marker_b = 0.20f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.28f;
        style.palette_fill_g = 0.28f;
        style.palette_fill_b = 0.28f;
        style.palette_fill_a = 0.22f;
        return;
    }

    if (theme == ChartTheme::HighContrast) {
        style.title_r = 0.00f;
        style.title_g = 0.00f;
        style.title_b = 0.00f;
        style.axis_label_r = 0.00f;
        style.axis_label_g = 0.00f;
        style.axis_label_b = 0.00f;
        style.tick_label_r = 0.00f;
        style.tick_label_g = 0.00f;
        style.tick_label_b = 0.00f;
        style.axis_r = 0.00f;
        style.axis_g = 0.00f;
        style.axis_b = 0.00f;
        style.grid_r = 0.25f;
        style.grid_g = 0.25f;
        style.grid_b = 0.25f;
        style.grid_dashed = true;
        style.axis_a = 1.00f;
        style.grid_a = 0.9f;

        style.palette_line_r = 0.00f;
        style.palette_line_g = 0.00f;
        style.palette_line_b = 0.00f;
        style.palette_line_a = 1.0f;
        style.palette_marker_r = 0.00f;
        style.palette_marker_g = 0.00f;
        style.palette_marker_b = 0.00f;
        style.palette_marker_a = 1.0f;
        style.palette_fill_r = 0.00f;
        style.palette_fill_g = 0.00f;
        style.palette_fill_b = 0.00f;
        style.palette_fill_a = 0.28f;
        return;
    }
}

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
    std::shared_ptr<const std::function<void()>> on_focus;
    std::shared_ptr<const std::function<void()>> on_blur;

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

struct InputAreaProps : ViewProps {
    std::string value;
    std::string placeholder;
    float text_size{18.0f};
    float text_r{0.1f};
    float text_g{0.1f};
    float text_b{0.1f};
    float border_r{0.7f};
    float border_g{0.7f};
    float border_b{0.7f};

    bool operator==(const InputAreaProps&) const = default;
};

struct CanvasProps : ViewProps {
    std::string drawio_xml;
    float diagram_padding{16.0f};

    bool operator==(const CanvasProps&) const = default;
};

struct RiveProps : ViewProps {
    std::string source;
    std::string artboard;
    std::string state_machine;
    std::shared_ptr<const reactcpp::RiveInputs> inputs_source;
    std::map<std::string, double> number_inputs;
    std::map<std::string, bool> bool_inputs;
    double time_scale{1.0};
    std::size_t inputs_revision{0};

    bool operator==(const RiveProps&) const = default;
};

struct LineChartProps : ViewProps {
    std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> points_source;
    std::vector<reactcpp::LinePoint> points{};
    std::size_t data_revision{0};
    std::vector<double> x{};
    std::vector<double> y{};

    float line_r{0.067f};
    float line_g{0.204f};
    float line_b{0.561f};
    float line_a{1.0f};
    float line_width{2.0f};

    float marker_r{0.067f};
    float marker_g{0.204f};
    float marker_b{0.561f};
    float marker_a{1.0f};
    float marker_size{3.0f};
    bool show_markers{true};
    bool draw_line{true};
    bool line_color_set{false};
    bool marker_color_set{false};
    bool palette_set{false};
    float palette_r{0.067f};
    float palette_g{0.204f};
    float palette_b{0.561f};
    float palette_a{1.0f};

    float left_padding{36.0f};
    float right_padding{12.0f};
    float top_padding{12.0f};
    float bottom_padding{34.0f};

    float x_min{0.0f};
    float x_max{0.0f};
    float y_min{0.0f};
    float y_max{0.0f};

    bool auto_x_range{true};
    bool auto_y_range{true};
    bool draw_axes{true};
    bool draw_grid{true};
    ChartGrid grid_mode{ChartGrid::Both};
    ChartStyle chart_style{};
    bool fill_area{false};

    bool operator==(const LineChartProps&) const = default;
};

struct ScatterChartProps : LineChartProps {
    float point_r{0.95f};
    float point_g{0.31f};
    float point_b{0.0f};
    float point_a{1.0f};
    ChartMarker marker_kind{ChartMarker::Circle};

    bool operator==(const ScatterChartProps&) const = default;
};

struct AreaChartProps : LineChartProps {
    bool stacked{false};
    bool show_base_line{true};
    std::vector<double> base{};
    float fill_r{0.22f};
    float fill_g{0.6f};
    float fill_b{0.98f};
    float fill_a{0.2f};

    bool operator==(const AreaChartProps&) const = default;
};

struct BarChartProps : ViewProps {
    std::shared_ptr<const reactcpp::VectorDataSource<double>> values_source;
    std::shared_ptr<const reactcpp::VectorDataSource<double>> x_source;
    std::vector<double> values{};
    std::vector<double> x{};
    std::size_t data_revision{0};

    float bar_width{0.7f};
    float bar_gap{0.12f};
    float fill_r{0.18f};
    float fill_g{0.35f};
    float fill_b{0.75f};
    float fill_a{1.0f};
    float stroke_r{0.18f};
    float stroke_g{0.35f};
    float stroke_b{0.75f};
    float stroke_a{1.0f};
    float stroke_width{1.0f};
    bool auto_x_range{true};
    bool auto_y_range{true};
    float x_min{0.0f};
    float x_max{0.0f};
    float y_min{0.0f};
    float y_max{0.0f};

    bool draw_axes{true};
    bool draw_grid{true};
    ChartStyle chart_style{};

    bool operator==(const BarChartProps&) const = default;
};

struct CircleChartProps : ViewProps {
    std::shared_ptr<const reactcpp::VectorDataSource<double>> x_source;
    std::shared_ptr<const reactcpp::VectorDataSource<double>> y_source;
    std::shared_ptr<const reactcpp::VectorDataSource<double>> radius_source;
    std::vector<double> x{};
    std::vector<double> y{};
    std::vector<double> radius{};
    std::size_t x_revision{0};
    std::size_t y_revision{0};
    std::size_t r_revision{0};

    float fill_r{0.91f};
    float fill_g{0.1f};
    float fill_b{0.18f};
    float fill_a{1.0f};
    float stroke_r{0.2f};
    float stroke_g{0.2f};
    float stroke_b{0.2f};
    float stroke_a{1.0f};
    float stroke_width{1.2f};

    float start_angle{0.0f};
    float end_angle{360.0f};

    float left_padding{36.0f};
    float right_padding{12.0f};
    float top_padding{12.0f};
    float bottom_padding{34.0f};
    bool auto_range{true};
    float x_min{0.0f};
    float x_max{0.0f};
    float y_min{0.0f};
    float y_max{0.0f};
    bool draw_axes{true};
    bool draw_grid{true};
    ChartStyle chart_style{};

    bool operator==(const CircleChartProps&) const = default;
};

struct HistogramChartProps : ViewProps {
    std::shared_ptr<const reactcpp::VectorDataSource<double>> samples_source;
    std::vector<double> samples{};
    std::size_t data_revision{0};

    std::vector<double> bin_edges{};
    size_t fixed_bin_count{0};
    HistogramAlgorithm algorithm{HistogramAlgorithm::Automatic};
    HistogramNormalization normalization{HistogramNormalization::Count};

    float fill_r{0.28f};
    float fill_g{0.63f};
    float fill_b{0.98f};
    float fill_a{1.0f};
    float stroke_r{0.06f};
    float stroke_g{0.24f};
    float stroke_b{0.48f};
    float stroke_a{1.0f};
    float stroke_width{0.6f};
    float bar_width_factor{0.8f};

    bool auto_range{true};
    float x_min{0.0f};
    float x_max{0.0f};
    float y_min{0.0f};
    float y_max{0.0f};
    bool draw_axes{true};
    bool draw_grid{true};
    ChartStyle chart_style{};

    bool operator==(const HistogramChartProps&) const = default;
};

using ElementProps = std::variant<
    ViewProps,
    ButtonProps,
    TextProps,
    InputProps,
    InputAreaProps,
    CanvasProps,
    RiveProps,
    LineChartProps,
    ScatterChartProps,
    AreaChartProps,
    BarChartProps,
    CircleChartProps,
    HistogramChartProps
>;

bool props_equal(const ElementProps& lhs, const ElementProps& rhs);

struct Element {
    TypeId type{};
    ElementProps props{ViewProps{}};
    std::vector<Element> children;
    bool dirty{false};

    bool operator==(const Element& other) const {
        return type == other.type
               && props_equal(props, other.props)
               && children == other.children;
    }
};

inline bool props_equal(const ElementProps& lhs, const ElementProps& rhs) {
    if (lhs.index() != rhs.index()) return false;

    switch (lhs.index()) {
    case 0:
        return std::get<ViewProps>(lhs) == std::get<ViewProps>(rhs);
    case 1:
        return std::get<ButtonProps>(lhs) == std::get<ButtonProps>(rhs);
    case 2:
        return std::get<TextProps>(lhs) == std::get<TextProps>(rhs);
    case 3:
        return std::get<InputProps>(lhs) == std::get<InputProps>(rhs);
    case 4:
        return std::get<InputAreaProps>(lhs) == std::get<InputAreaProps>(rhs);
    case 5:
        return std::get<CanvasProps>(lhs) == std::get<CanvasProps>(rhs);
    case 6:
        return std::get<RiveProps>(lhs) == std::get<RiveProps>(rhs);
    case 7:
        return std::get<LineChartProps>(lhs) == std::get<LineChartProps>(rhs);
    case 8:
        return std::get<ScatterChartProps>(lhs) == std::get<ScatterChartProps>(rhs);
    case 9:
        return std::get<AreaChartProps>(lhs) == std::get<AreaChartProps>(rhs);
    case 10:
        return std::get<BarChartProps>(lhs) == std::get<BarChartProps>(rhs);
    case 11:
        return std::get<CircleChartProps>(lhs) == std::get<CircleChartProps>(rhs);
    case 12:
        return std::get<HistogramChartProps>(lhs) == std::get<HistogramChartProps>(rhs);
    default:
        return false;
    }
}

TypeId host_type_view();
TypeId host_type_button();
TypeId host_type_text();
TypeId host_type_input();
TypeId host_type_input_area();
TypeId host_type_canvas();
TypeId host_type_rive();
TypeId host_type_line_chart();
TypeId host_type_scatter_chart();
TypeId host_type_area_chart();
TypeId host_type_bar_chart();
TypeId host_type_circle_chart();
TypeId host_type_histogram_chart();

Element View(const ViewProps& props, std::vector<Element> children = {});
Element Button(const ButtonProps& props);
Element Text(const TextProps& props);
Element Input(const InputProps& props);
Element InputArea(const InputAreaProps& props, std::vector<Element> children = {});
Element Canvas(const CanvasProps& props);
Element Rive(const RiveProps& props);
Element LineChart(const LineChartProps& props);
Element ScatterChart(const ScatterChartProps& props);
Element AreaChart(const AreaChartProps& props);
Element BarChart(const BarChartProps& props);
Element CircleChart(const CircleChartProps& props);
Element HistogramChart(const HistogramChartProps& props);

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
    mutable std::shared_ptr<void> native_state;

    bool focused{false};

    LayoutRect layout{};

    std::uintptr_t yoga_node_handle{0};

    struct EditableTextState {
        reactcpp::text::TextBuffer value;
        std::size_t cursor{0};

        std::size_t sel_start{0};
        std::size_t sel_end{0};
        std::size_t sel_anchor{0};
        bool has_selection{false};

        bool focused{false};

        float scroll_x{0.0f};
        float scroll_y{0.0f};

        std::string preedit;
        int preedit_start{-1};
        int preedit_length{-1};

        reactcpp::text::UndoHistory undo{100};
    };
    std::optional<EditableTextState> editable_state;
};

struct HookDispatcher {
    InstanceNode* current_instance{nullptr};
    std::uint32_t current_index{0};

    void (*request_update)(void* ctx){nullptr};
    void* request_update_ctx{nullptr};
};

HookDispatcher& get_hook_dispatcher();

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

        HookDispatcher& d = get_hook_dispatcher();
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

        HookDispatcher& d = get_hook_dispatcher();
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
        HookDispatcher& d = get_hook_dispatcher();
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

int run_react_app(const AppRenderFunc& app);
