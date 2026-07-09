#pragma once

#include "skia_runtime.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace reactcpp::ui {

namespace {
inline void apply_chart_style_theme(ChartStyle& style, ChartTheme theme) {
    apply_chart_theme_preset(style, theme);
}
}

template <typename Props>
concept ViewPropsLike = std::is_base_of_v<ViewProps, Props> || std::same_as<ViewProps, Props>;

template <typename T>
concept ElementConvertible = std::convertible_to<T, Element>;

template <typename Derived, typename Props>
requires ViewPropsLike<Props>
class ViewLikeNode {
public:
    Derived& padding(float v) & {
        props_.style.padding = v;
        return self();
    }
    Derived&& padding(float v) && {
        props_.style.padding = v;
        return std::move(self());
    }

    Derived& margin(float v) & {
        props_.style.margin = v;
        return self();
    }
    Derived&& margin(float v) && {
        props_.style.margin = v;
        return std::move(self());
    }

    Derived& width(float v) & {
        props_.style.width = v;
        return self();
    }
    Derived&& width(float v) && {
        props_.style.width = v;
        return std::move(self());
    }

    Derived& height(float v) & {
        props_.style.height = v;
        return self();
    }
    Derived&& height(float v) && {
        props_.style.height = v;
        return std::move(self());
    }

    Derived& size(float w, float h) & {
        props_.style.width = w;
        props_.style.height = h;
        return self();
    }
    Derived&& size(float w, float h) && {
        props_.style.width = w;
        props_.style.height = h;
        return std::move(self());
    }

    Derived& auto_width() & {
        props_.style.width = std::nullopt;
        return self();
    }
    Derived&& auto_width() && {
        props_.style.width = std::nullopt;
        return std::move(self());
    }

    Derived& auto_height() & {
        props_.style.height = std::nullopt;
        return self();
    }
    Derived&& auto_height() && {
        props_.style.height = std::nullopt;
        return std::move(self());
    }

    Derived& row() & {
        props_.style.flex_direction = FlexDirection::Row;
        return self();
    }
    Derived&& row() && {
        props_.style.flex_direction = FlexDirection::Row;
        return std::move(self());
    }

    Derived& column() & {
        props_.style.flex_direction = FlexDirection::Column;
        return self();
    }
    Derived&& column() && {
        props_.style.flex_direction = FlexDirection::Column;
        return std::move(self());
    }

    Derived& justify(JustifyContent v) & {
        props_.style.justify_content = v;
        return self();
    }
    Derived&& justify(JustifyContent v) && {
        props_.style.justify_content = v;
        return std::move(self());
    }

    Derived& align(AlignItems v) & {
        props_.style.align_items = v;
        return self();
    }
    Derived&& align(AlignItems v) && {
        props_.style.align_items = v;
        return std::move(self());
    }

    Derived& bg(float r, float g, float b, float a = 1.0f) & {
        props_.bg_r = r;
        props_.bg_g = g;
        props_.bg_b = b;
        props_.bg_a = a;
        return self();
    }
    Derived&& bg(float r, float g, float b, float a = 1.0f) && {
        props_.bg_r = r;
        props_.bg_g = g;
        props_.bg_b = b;
        props_.bg_a = a;
        return std::move(self());
    }

    template <std::invocable Fn>
    Derived& on_click(Fn&& fn) & {
        props_.on_click = std::make_shared<const std::function<void()>>(
            std::function<void()>(std::forward<Fn>(fn))
        );
        return self();
    }
    template <std::invocable Fn>
    Derived&& on_click(Fn&& fn) && {
        props_.on_click = std::make_shared<const std::function<void()>>(
            std::function<void()>(std::forward<Fn>(fn))
        );
        return std::move(self());
    }

    template <std::invocable Fn>
    Derived& on_focus(Fn&& fn) & {
        props_.on_focus = std::make_shared<const std::function<void()>>(
            std::function<void()>(std::forward<Fn>(fn))
        );
        return self();
    }
    template <std::invocable Fn>
    Derived&& on_focus(Fn&& fn) && {
        props_.on_focus = std::make_shared<const std::function<void()>>(
            std::function<void()>(std::forward<Fn>(fn))
        );
        return std::move(self());
    }

    template <std::invocable Fn>
    Derived& on_blur(Fn&& fn) & {
        props_.on_blur = std::make_shared<const std::function<void()>>(
            std::function<void()>(std::forward<Fn>(fn))
        );
        return self();
    }
    template <std::invocable Fn>
    Derived&& on_blur(Fn&& fn) && {
        props_.on_blur = std::make_shared<const std::function<void()>>(
            std::function<void()>(std::forward<Fn>(fn))
        );
        return std::move(self());
    }

protected:
    Props props_{};

private:
    Derived& self() {
        return static_cast<Derived&>(*this);
    }
};

template <typename Derived, typename Props>
class ChartStyleNode : public ViewLikeNode<Derived, Props> {
public:
    Derived& title(std::string v) & {
        style().title = std::move(v);
        return static_cast<Derived&>(*this);
    }
    Derived&& title(std::string v) && {
        style().title = std::move(v);
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& show_title(bool v) & {
        style().show_title = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& show_title(bool v) && {
        style().show_title = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& title_size(float v) & {
        style().title_size = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& title_size(float v) && {
        style().title_size = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& title_color(float r, float g, float b, float a = 1.0f) & {
        style().title_r = r;
        style().title_g = g;
        style().title_b = b;
        style().title_a = a;
        return static_cast<Derived&>(*this);
    }
    Derived&& title_color(float r, float g, float b, float a = 1.0f) && {
        style().title_r = r;
        style().title_g = g;
        style().title_b = b;
        style().title_a = a;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& xlabel(std::string v) & {
        style().x_label = std::move(v);
        return static_cast<Derived&>(*this);
    }
    Derived&& xlabel(std::string v) && {
        style().x_label = std::move(v);
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& ylabel(std::string v) & {
        style().y_label = std::move(v);
        return static_cast<Derived&>(*this);
    }
    Derived&& ylabel(std::string v) && {
        style().y_label = std::move(v);
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& show_axis_labels(bool v) & {
        style().show_axis_labels = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& show_axis_labels(bool v) && {
        style().show_axis_labels = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& axis_label_size(float v) & {
        style().axis_label_size = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& axis_label_size(float v) && {
        style().axis_label_size = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& axis_label_color(float r, float g, float b, float a = 1.0f) & {
        style().axis_label_r = r;
        style().axis_label_g = g;
        style().axis_label_b = b;
        style().axis_label_a = a;
        return static_cast<Derived&>(*this);
    }
    Derived&& axis_label_color(float r, float g, float b, float a = 1.0f) && {
        style().axis_label_r = r;
        style().axis_label_g = g;
        style().axis_label_b = b;
        style().axis_label_a = a;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& tick_label_size(float v) & {
        style().tick_label_size = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& tick_label_size(float v) && {
        style().tick_label_size = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& tick_label_color(float r, float g, float b, float a = 1.0f) & {
        style().tick_label_r = r;
        style().tick_label_g = g;
        style().tick_label_b = b;
        style().tick_label_a = a;
        return static_cast<Derived&>(*this);
    }
    Derived&& tick_label_color(float r, float g, float b, float a = 1.0f) && {
        style().tick_label_r = r;
        style().tick_label_g = g;
        style().tick_label_b = b;
        style().tick_label_a = a;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& axis_width(float v) & {
        style().axis_width = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& axis_width(float v) && {
        style().axis_width = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& axis_color(float r, float g, float b, float a = 1.0f) & {
        style().axis_r = r;
        style().axis_g = g;
        style().axis_b = b;
        style().axis_a = a;
        return static_cast<Derived&>(*this);
    }
    Derived&& axis_color(float r, float g, float b, float a = 1.0f) && {
        style().axis_r = r;
        style().axis_g = g;
        style().axis_b = b;
        style().axis_a = a;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& grid_color(float r, float g, float b, float a = 1.0f) & {
        style().grid_r = r;
        style().grid_g = g;
        style().grid_b = b;
        style().grid_a = a;
        return static_cast<Derived&>(*this);
    }
    Derived&& grid_color(float r, float g, float b, float a = 1.0f) && {
        style().grid_r = r;
        style().grid_g = g;
        style().grid_b = b;
        style().grid_a = a;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& grid_width(float v) & {
        style().grid_width = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& grid_width(float v) && {
        style().grid_width = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& grid_dashed(bool v) & {
        style().grid_dashed = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& grid_dashed(bool v) && {
        style().grid_dashed = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& tick_count(int v) & {
        style().tick_count = std::max(2, v);
        return static_cast<Derived&>(*this);
    }
    Derived&& tick_count(int v) && {
        style().tick_count = std::max(2, v);
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& tick_mark_count(int v) & {
        return tick_count(v);
    }

    Derived&& tick_mark_count(int v) && {
        return std::move(tick_count(v));
    }

    Derived& show_grid(bool v) & {
        style().draw_grid = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& show_grid(bool v) && {
        style().draw_grid = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& grid(ChartGrid mode) & {
        style().grid_mode = mode;
        return static_cast<Derived&>(*this);
    }
    Derived&& grid(ChartGrid mode) && {
        style().grid_mode = mode;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& show_axes(bool v) & {
        style().draw_axes = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& show_axes(bool v) && {
        style().draw_axes = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& show_tick_labels(bool v) & {
        style().show_tick_labels = v;
        return static_cast<Derived&>(*this);
    }
    Derived&& show_tick_labels(bool v) && {
        style().show_tick_labels = v;
        return std::move(static_cast<Derived&>(*this));
    }

    Derived& theme(ChartTheme t) & {
        apply_chart_style_theme(style(), t);
        return static_cast<Derived&>(*this);
    }
    Derived&& theme(ChartTheme t) && {
        apply_chart_style_theme(style(), t);
        return std::move(static_cast<Derived&>(*this));
    }

private:
    ChartStyle& style() {
        return static_cast<Derived&>(*this).props_.chart_style;
    }
};

class TextNode final : public ViewLikeNode<TextNode, TextProps> {
public:
    TextNode& value(std::string v) & {
        props_.text = std::move(v);
        return *this;
    }
    TextNode&& value(std::string v) && {
        props_.text = std::move(v);
        return std::move(*this);
    }

    TextNode& text_size(float v) & {
        props_.text_size = v;
        return *this;
    }
    TextNode&& text_size(float v) && {
        props_.text_size = v;
        return std::move(*this);
    }

    TextNode& text_color(float r, float g, float b) & {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return *this;
    }
    TextNode&& text_color(float r, float g, float b) && {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return std::move(*this);
    }

    Element build() const & {
        return Text(props_);
    }
    Element build() && {
        return Text(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class ButtonNode final : public ViewLikeNode<ButtonNode, ButtonProps> {
public:
    ButtonNode& label(std::string v) & {
        props_.label = std::move(v);
        return *this;
    }
    ButtonNode&& label(std::string v) && {
        props_.label = std::move(v);
        return std::move(*this);
    }

    ButtonNode& text_size(float v) & {
        props_.text_size = v;
        return *this;
    }
    ButtonNode&& text_size(float v) && {
        props_.text_size = v;
        return std::move(*this);
    }

    ButtonNode& text_color(float r, float g, float b) & {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return *this;
    }
    ButtonNode&& text_color(float r, float g, float b) && {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return std::move(*this);
    }

    Element build() const & {
        return Button(props_);
    }
    Element build() && {
        return Button(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class InputNode final : public ViewLikeNode<InputNode, InputProps> {
public:
    InputNode& value(std::string v) & {
        props_.value = std::move(v);
        return *this;
    }
    InputNode&& value(std::string v) && {
        props_.value = std::move(v);
        return std::move(*this);
    }

    InputNode& placeholder(std::string v) & {
        props_.placeholder = std::move(v);
        return *this;
    }
    InputNode&& placeholder(std::string v) && {
        props_.placeholder = std::move(v);
        return std::move(*this);
    }

    InputNode& text_size(float v) & {
        props_.text_size = v;
        return *this;
    }
    InputNode&& text_size(float v) && {
        props_.text_size = v;
        return std::move(*this);
    }

    InputNode& text_color(float r, float g, float b) & {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return *this;
    }
    InputNode&& text_color(float r, float g, float b) && {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return std::move(*this);
    }

    Element build() const & {
        return Input(props_);
    }
    Element build() && {
        return Input(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class InputAreaNode final : public ViewLikeNode<InputAreaNode, InputAreaProps> {
public:
    InputAreaNode& value(std::string v) & {
        props_.value = std::move(v);
        return *this;
    }
    InputAreaNode&& value(std::string v) && {
        props_.value = std::move(v);
        return std::move(*this);
    }

    InputAreaNode& placeholder(std::string v) & {
        props_.placeholder = std::move(v);
        return *this;
    }
    InputAreaNode&& placeholder(std::string v) && {
        props_.placeholder = std::move(v);
        return std::move(*this);
    }

    InputAreaNode& text_size(float v) & {
        props_.text_size = v;
        return *this;
    }
    InputAreaNode&& text_size(float v) && {
        props_.text_size = v;
        return std::move(*this);
    }

    InputAreaNode& text_color(float r, float g, float b) & {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return *this;
    }
    InputAreaNode&& text_color(float r, float g, float b) && {
        props_.text_r = r;
        props_.text_g = g;
        props_.text_b = b;
        return std::move(*this);
    }

    InputAreaNode& border_color(float r, float g, float b) & {
        props_.border_r = r;
        props_.border_g = g;
        props_.border_b = b;
        return *this;
    }
    InputAreaNode&& border_color(float r, float g, float b) && {
        props_.border_r = r;
        props_.border_g = g;
        props_.border_b = b;
        return std::move(*this);
    }

    InputAreaNode& add(Element child) & {
        children_.push_back(std::move(child));
        return *this;
    }
    InputAreaNode&& add(Element child) && {
        children_.push_back(std::move(child));
        return std::move(*this);
    }

    template <ElementConvertible... Children>
    InputAreaNode& operator()(Children&&... children) & {
        children_.clear();
        children_.reserve(sizeof...(Children));
        (children_.push_back(static_cast<Element>(std::forward<Children>(children))), ...);
        return *this;
    }

    template <ElementConvertible... Children>
    InputAreaNode&& operator()(Children&&... children) && {
        children_.clear();
        children_.reserve(sizeof...(Children));
        (children_.push_back(static_cast<Element>(std::forward<Children>(children))), ...);
        return std::move(*this);
    }

    Element build() const & {
        return InputArea(props_, children_);
    }
    Element build() && {
        return InputArea(props_, std::move(children_));
    }
    operator Element() && {
        return std::move(*this).build();
    }

private:
    std::vector<Element> children_{};
};

class CanvasNode final : public ViewLikeNode<CanvasNode, CanvasProps> {
public:
    CanvasNode& drawio_xml(std::string v) & {
        props_.drawio_xml = std::move(v);
        return *this;
    }
    CanvasNode&& drawio_xml(std::string v) && {
        props_.drawio_xml = std::move(v);
        return std::move(*this);
    }

    CanvasNode& diagram_padding(float v) & {
        props_.diagram_padding = v;
        return *this;
    }
    CanvasNode&& diagram_padding(float v) && {
        props_.diagram_padding = v;
        return std::move(*this);
    }

    Element build() const & {
        return Canvas(props_);
    }
    Element build() && {
        return Canvas(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class RiveNode final : public ViewLikeNode<RiveNode, RiveProps> {
public:
    RiveNode& source(std::string v) & {
        props_.source = std::move(v);
        return *this;
    }
    RiveNode&& source(std::string v) && {
        props_.source = std::move(v);
        return std::move(*this);
    }

    RiveNode& artboard(std::string v) & {
        props_.artboard = std::move(v);
        return *this;
    }
    RiveNode&& artboard(std::string v) && {
        props_.artboard = std::move(v);
        return std::move(*this);
    }

    RiveNode& state_machine(std::string v) & {
        props_.state_machine = std::move(v);
        return *this;
    }
    RiveNode&& state_machine(std::string v) && {
        props_.state_machine = std::move(v);
        return std::move(*this);
    }

    RiveNode& inputs(std::shared_ptr<const reactcpp::RiveInputs> source) & {
        props_.inputs_source = std::move(source);
        props_.inputs_revision = props_.inputs_source ? props_.inputs_source->revision() : 0;
        return *this;
    }
    RiveNode&& inputs(std::shared_ptr<const reactcpp::RiveInputs> source) && {
        props_.inputs_source = std::move(source);
        props_.inputs_revision = props_.inputs_source ? props_.inputs_source->revision() : 0;
        return std::move(*this);
    }

    RiveNode& number(std::string name, double value) & {
        props_.number_inputs[std::move(name)] = value;
        props_.inputs_source.reset();
        props_.inputs_revision = 0;
        return *this;
    }
    RiveNode&& number(std::string name, double value) && {
        props_.number_inputs[std::move(name)] = value;
        props_.inputs_source.reset();
        props_.inputs_revision = 0;
        return std::move(*this);
    }

    RiveNode& boolean(std::string name, bool value) & {
        props_.bool_inputs[std::move(name)] = value;
        props_.inputs_source.reset();
        props_.inputs_revision = 0;
        return *this;
    }
    RiveNode&& boolean(std::string name, bool value) && {
        props_.bool_inputs[std::move(name)] = value;
        props_.inputs_source.reset();
        props_.inputs_revision = 0;
        return std::move(*this);
    }

    RiveNode& time_scale(double value) & {
        props_.time_scale = value;
        return *this;
    }
    RiveNode&& time_scale(double value) && {
        props_.time_scale = value;
        return std::move(*this);
    }

    Element build() const & {
        return Rive(props_);
    }
    Element build() && {
        return Rive(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class LineChartNode final : public ChartStyleNode<LineChartNode, LineChartProps> {
public:
    LineChartNode& data(std::vector<reactcpp::LinePoint> v) & {
        props_.points = std::move(v);
        props_.points_source.reset();
        props_.data_revision = 0;
        return *this;
    }
    LineChartNode&& data(std::vector<reactcpp::LinePoint> v) && {
        props_.points = std::move(v);
        props_.points_source.reset();
        props_.data_revision = 0;
        return std::move(*this);
    }

    LineChartNode& source(std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> s) & {
        props_.points_source = std::move(s);
        props_.data_revision = props_.points_source ? props_.points_source->revision() : 0;
        return *this;
    }
    LineChartNode&& source(std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> s) && {
        props_.points_source = std::move(s);
        props_.data_revision = props_.points_source ? props_.points_source->revision() : 0;
        return std::move(*this);
    }

    LineChartNode& line_color(float r, float g, float b, float a = 1.0f) & {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        props_.line_color_set = true;
        return *this;
    }
    LineChartNode&& line_color(float r, float g, float b, float a = 1.0f) && {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        props_.line_color_set = true;
        return std::move(*this);
    }

    LineChartNode& color(float r, float g, float b, float a = 1.0f) & {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        props_.line_color_set = true;
        return *this;
    }
    LineChartNode&& color(float r, float g, float b, float a = 1.0f) && {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        props_.line_color_set = true;
        return std::move(*this);
    }

    LineChartNode& marker_color(float r, float g, float b, float a = 1.0f) & {
        props_.marker_r = r;
        props_.marker_g = g;
        props_.marker_b = b;
        props_.marker_a = a;
        props_.marker_color_set = true;
        return *this;
    }
    LineChartNode&& marker_color(float r, float g, float b, float a = 1.0f) && {
        props_.marker_r = r;
        props_.marker_g = g;
        props_.marker_b = b;
        props_.marker_a = a;
        props_.marker_color_set = true;
        return std::move(*this);
    }

    LineChartNode& palette(float r, float g, float b, float a = 1.0f) & {
        props_.palette_r = r;
        props_.palette_g = g;
        props_.palette_b = b;
        props_.palette_a = a;
        props_.palette_set = true;
        return *this;
    }
    LineChartNode&& palette(float r, float g, float b, float a = 1.0f) && {
        props_.palette_r = r;
        props_.palette_g = g;
        props_.palette_b = b;
        props_.palette_a = a;
        props_.palette_set = true;
        return std::move(*this);
    }

    LineChartNode& line_width(float v) & {
        props_.line_width = v;
        return *this;
    }
    LineChartNode&& line_width(float v) && {
        props_.line_width = v;
        return std::move(*this);
    }

    LineChartNode& marker_size(float v) & {
        props_.marker_size = v;
        return *this;
    }
    LineChartNode&& marker_size(float v) && {
        props_.marker_size = v;
        return std::move(*this);
    }

    LineChartNode& show_markers(bool v) & {
        props_.show_markers = v;
        return *this;
    }
    LineChartNode&& show_markers(bool v) && {
        props_.show_markers = v;
        return std::move(*this);
    }

    LineChartNode& fill_area(bool v) & {
        props_.fill_area = v;
        return *this;
    }
    LineChartNode&& fill_area(bool v) && {
        props_.fill_area = v;
        return std::move(*this);
    }

    LineChartNode& draw_grid(bool v) & {
        props_.draw_grid = v;
        return *this;
    }
    LineChartNode&& draw_grid(bool v) && {
        props_.draw_grid = v;
        return std::move(*this);
    }

    Element build() const & {
        return LineChart(props_);
    }
    Element build() && {
        return LineChart(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class ScatterChartNode final : public ChartStyleNode<ScatterChartNode, ScatterChartProps> {
public:
    ScatterChartNode& data(std::vector<reactcpp::LinePoint> v) & {
        props_.points = std::move(v);
        props_.points_source.reset();
        props_.data_revision = 0;
        return *this;
    }
    ScatterChartNode&& data(std::vector<reactcpp::LinePoint> v) && {
        props_.points = std::move(v);
        props_.points_source.reset();
        props_.data_revision = 0;
        return std::move(*this);
    }
    ScatterChartNode& source(std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> s) & {
        props_.points_source = std::move(s);
        props_.data_revision = props_.points_source ? props_.points_source->revision() : 0;
        return *this;
    }
    ScatterChartNode&& source(std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> s) && {
        props_.points_source = std::move(s);
        props_.data_revision = props_.points_source ? props_.points_source->revision() : 0;
        return std::move(*this);
    }
    ScatterChartNode& line_color(float r, float g, float b, float a = 1.0f) & {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        return *this;
    }
    ScatterChartNode&& line_color(float r, float g, float b, float a = 1.0f) && {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        return std::move(*this);
    }
    ScatterChartNode& line_width(float v) & {
        props_.line_width = v;
        return *this;
    }
    ScatterChartNode&& line_width(float v) && {
        props_.line_width = v;
        return std::move(*this);
    }
    ScatterChartNode& marker_color(float r, float g, float b, float a = 1.0f) & {
        props_.marker_r = r;
        props_.marker_g = g;
        props_.marker_b = b;
        props_.marker_a = a;
        return *this;
    }
    ScatterChartNode&& marker_color(float r, float g, float b, float a = 1.0f) && {
        props_.marker_r = r;
        props_.marker_g = g;
        props_.marker_b = b;
        props_.marker_a = a;
        return std::move(*this);
    }
    ScatterChartNode& marker_size(float v) & {
        props_.marker_size = v;
        return *this;
    }
    ScatterChartNode&& marker_size(float v) && {
        props_.marker_size = v;
        return std::move(*this);
    }
    ScatterChartNode& show_markers(bool v) & {
        props_.show_markers = v;
        return *this;
    }
    ScatterChartNode&& show_markers(bool v) && {
        props_.show_markers = v;
        return std::move(*this);
    }
    ScatterChartNode& fill_area(bool v) & {
        props_.fill_area = v;
        return *this;
    }
    ScatterChartNode&& fill_area(bool v) && {
        props_.fill_area = v;
        return std::move(*this);
    }
    ScatterChartNode& draw_grid(bool v) & {
        props_.draw_grid = v;
        return *this;
    }
    ScatterChartNode&& draw_grid(bool v) && {
        props_.draw_grid = v;
        return std::move(*this);
    }

    ScatterChartNode& point_color(float r, float g, float b, float a = 1.0f) & {
        props_.point_r = r;
        props_.point_g = g;
        props_.point_b = b;
        props_.point_a = a;
        return *this;
    }
    ScatterChartNode&& point_color(float r, float g, float b, float a = 1.0f) && {
        props_.point_r = r;
        props_.point_g = g;
        props_.point_b = b;
        props_.point_a = a;
        return std::move(*this);
    }

    Element build() const & {
        return ScatterChart(props_);
    }
    Element build() && {
        return ScatterChart(props_);
    }
    operator Element() && {
        return std::move(*this).build();
    }
};

class AreaChartNode final : public ChartStyleNode<AreaChartNode, AreaChartProps> {
public:
    AreaChartNode& data(std::vector<reactcpp::LinePoint> v) & {
        props_.points = std::move(v);
        props_.points_source.reset();
        props_.data_revision = 0;
        return *this;
    }
    AreaChartNode&& data(std::vector<reactcpp::LinePoint> v) && {
        props_.points = std::move(v);
        props_.points_source.reset();
        props_.data_revision = 0;
        return std::move(*this);
    }
    AreaChartNode& source(std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> s) & {
        props_.points_source = std::move(s);
        props_.data_revision = props_.points_source ? props_.points_source->revision() : 0;
        return *this;
    }
    AreaChartNode&& source(std::shared_ptr<const reactcpp::VectorDataSource<reactcpp::LinePoint>> s) && {
        props_.points_source = std::move(s);
        props_.data_revision = props_.points_source ? props_.points_source->revision() : 0;
        return std::move(*this);
    }

    AreaChartNode& fill(float r, float g, float b, float a = 1.0f) & {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return *this;
    }
    AreaChartNode&& fill(float r, float g, float b, float a = 1.0f) && {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return std::move(*this);
    }
    AreaChartNode& stacked(bool v) & {
        props_.stacked = v;
        return *this;
    }
    AreaChartNode&& stacked(bool v) && {
        props_.stacked = v;
        return std::move(*this);
    }
    AreaChartNode& line_color(float r, float g, float b, float a = 1.0f) & {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        props_.line_color_set = true;
        return *this;
    }
    AreaChartNode&& line_color(float r, float g, float b, float a = 1.0f) && {
        props_.line_r = r;
        props_.line_g = g;
        props_.line_b = b;
        props_.line_a = a;
        props_.line_color_set = true;
        return std::move(*this);
    }
    AreaChartNode& palette(float r, float g, float b, float a = 1.0f) & {
        props_.palette_r = r;
        props_.palette_g = g;
        props_.palette_b = b;
        props_.palette_a = a;
        props_.palette_set = true;
        return *this;
    }
    AreaChartNode&& palette(float r, float g, float b, float a = 1.0f) && {
        props_.palette_r = r;
        props_.palette_g = g;
        props_.palette_b = b;
        props_.palette_a = a;
        props_.palette_set = true;
        return std::move(*this);
    }
    AreaChartNode& line_width(float v) & {
        props_.line_width = v;
        return *this;
    }
    AreaChartNode&& line_width(float v) && {
        props_.line_width = v;
        return std::move(*this);
    }

    Element build() const & { return AreaChart(props_); }
    Element build() && { return AreaChart(std::move(props_)); }
    operator Element() && { return std::move(*this).build(); }
};

class BarChartNode final : public ChartStyleNode<BarChartNode, BarChartProps> {
public:
    BarChartNode& data(std::vector<double> v) & {
        props_.values = std::move(v);
        props_.values_source.reset();
        props_.data_revision = 0;
        return *this;
    }
    BarChartNode&& data(std::vector<double> v) && {
        props_.values = std::move(v);
        props_.values_source.reset();
        props_.data_revision = 0;
        return std::move(*this);
    }
    BarChartNode& x(std::vector<double> v) & {
        props_.x = std::move(v);
        props_.x_source.reset();
        return *this;
    }
    BarChartNode&& x(std::vector<double> v) && {
        props_.x = std::move(v);
        props_.x_source.reset();
        return std::move(*this);
    }
    BarChartNode& values_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) & {
        props_.values_source = std::move(s);
        props_.data_revision = props_.values_source ? props_.values_source->revision() : 0;
        return *this;
    }
    BarChartNode&& values_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) && {
        props_.values_source = std::move(s);
        props_.data_revision = props_.values_source ? props_.values_source->revision() : 0;
        return std::move(*this);
    }
    BarChartNode& x_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) & {
        props_.x_source = std::move(s);
        if (props_.x_source) {
            props_.data_revision ^= props_.x_source->revision();
        }
        return *this;
    }
    BarChartNode&& x_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) && {
        props_.x_source = std::move(s);
        if (props_.x_source) {
            props_.data_revision ^= props_.x_source->revision();
        }
        return std::move(*this);
    }

    BarChartNode& color(float r, float g, float b, float a = 1.0f) & {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return *this;
    }
    BarChartNode&& color(float r, float g, float b, float a = 1.0f) && {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return std::move(*this);
    }

    Element build() const & { return BarChart(props_); }
    Element build() && { return BarChart(props_); }
    operator Element() && { return std::move(*this).build(); }
};

class CircleChartNode final : public ChartStyleNode<CircleChartNode, CircleChartProps> {
public:
    CircleChartNode& data(std::vector<double> x, std::vector<double> y, std::vector<double> radius = {}) & {
        props_.x = std::move(x);
        props_.y = std::move(y);
        props_.radius = std::move(radius);
        props_.x_source.reset();
        props_.y_source.reset();
        props_.radius_source.reset();
        props_.x_revision = 0;
        props_.y_revision = 0;
        props_.r_revision = 0;
        return *this;
    }
    CircleChartNode&& data(std::vector<double> x, std::vector<double> y, std::vector<double> radius = {}) && {
        props_.x = std::move(x);
        props_.y = std::move(y);
        props_.radius = std::move(radius);
        props_.x_source.reset();
        props_.y_source.reset();
        props_.radius_source.reset();
        props_.x_revision = 0;
        props_.y_revision = 0;
        props_.r_revision = 0;
        return std::move(*this);
    }
    CircleChartNode& x_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) & {
        props_.x_source = std::move(s);
        props_.x_revision = props_.x_source ? props_.x_source->revision() : 0;
        return *this;
    }
    CircleChartNode&& x_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) && {
        props_.x_source = std::move(s);
        props_.x_revision = props_.x_source ? props_.x_source->revision() : 0;
        return std::move(*this);
    }
    CircleChartNode& y_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) & {
        props_.y_source = std::move(s);
        props_.y_revision = props_.y_source ? props_.y_source->revision() : 0;
        return *this;
    }
    CircleChartNode&& y_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) && {
        props_.y_source = std::move(s);
        props_.y_revision = props_.y_source ? props_.y_source->revision() : 0;
        return std::move(*this);
    }
    CircleChartNode& radius_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) & {
        props_.radius_source = std::move(s);
        props_.r_revision = props_.radius_source ? props_.radius_source->revision() : 0;
        return *this;
    }
    CircleChartNode&& radius_source(std::shared_ptr<const reactcpp::VectorDataSource<double>> s) && {
        props_.radius_source = std::move(s);
        props_.r_revision = props_.radius_source ? props_.radius_source->revision() : 0;
        return std::move(*this);
    }
    CircleChartNode& fill_color(float r, float g, float b, float a = 1.0f) & {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return *this;
    }
    CircleChartNode&& fill_color(float r, float g, float b, float a = 1.0f) && {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return std::move(*this);
    }

    Element build() const & { return CircleChart(props_); }
    Element build() && { return CircleChart(props_); }
    operator Element() && { return std::move(*this).build(); }
};

class HistogramChartNode final : public ChartStyleNode<HistogramChartNode, HistogramChartProps> {
public:
    HistogramChartNode& data(std::vector<double> samples) & {
        props_.samples = std::move(samples);
        props_.samples_source.reset();
        props_.data_revision = 0;
        return *this;
    }
    HistogramChartNode&& data(std::vector<double> samples) && {
        props_.samples = std::move(samples);
        props_.samples_source.reset();
        props_.data_revision = 0;
        return std::move(*this);
    }
    HistogramChartNode& source(std::shared_ptr<const reactcpp::VectorDataSource<double>> source) & {
        props_.samples_source = std::move(source);
        props_.data_revision = props_.samples_source ? props_.samples_source->revision() : 0;
        return *this;
    }
    HistogramChartNode&& source(std::shared_ptr<const reactcpp::VectorDataSource<double>> source) && {
        props_.samples_source = std::move(source);
        props_.data_revision = props_.samples_source ? props_.samples_source->revision() : 0;
        return std::move(*this);
    }
    HistogramChartNode& algorithm(HistogramAlgorithm a) & {
        props_.algorithm = a;
        return *this;
    }
    HistogramChartNode&& algorithm(HistogramAlgorithm a) && {
        props_.algorithm = a;
        return std::move(*this);
    }
    HistogramChartNode& normalization(HistogramNormalization n) & {
        props_.normalization = n;
        return *this;
    }
    HistogramChartNode&& normalization(HistogramNormalization n) && {
        props_.normalization = n;
        return std::move(*this);
    }
    HistogramChartNode& bin_count(size_t c) & {
        props_.fixed_bin_count = c;
        return *this;
    }
    HistogramChartNode&& bin_count(size_t c) && {
        props_.fixed_bin_count = c;
        return std::move(*this);
    }
    HistogramChartNode& color(float r, float g, float b, float a = 1.0f) & {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return *this;
    }
    HistogramChartNode&& color(float r, float g, float b, float a = 1.0f) && {
        props_.fill_r = r;
        props_.fill_g = g;
        props_.fill_b = b;
        props_.fill_a = a;
        return std::move(*this);
    }

    Element build() const & { return HistogramChart(props_); }
    Element build() && { return HistogramChart(props_); }
    operator Element() && { return std::move(*this).build(); }
};

class ViewNode final : public ViewLikeNode<ViewNode, ViewProps> {
public:
    ViewNode& add(Element child) & {
        children_.push_back(std::move(child));
        return *this;
    }
    ViewNode&& add(Element child) && {
        children_.push_back(std::move(child));
        return std::move(*this);
    }

    template <ElementConvertible... Children>
    ViewNode& operator()(Children&&... children) & {
        children_.clear();
        children_.reserve(sizeof...(Children));
        (children_.push_back(static_cast<Element>(std::forward<Children>(children))), ...);
        return *this;
    }

    template <ElementConvertible... Children>
    ViewNode&& operator()(Children&&... children) && {
        children_.clear();
        children_.reserve(sizeof...(Children));
        (children_.push_back(static_cast<Element>(std::forward<Children>(children))), ...);
        return std::move(*this);
    }

    Element build() const & {
        return View(props_, children_);
    }
    Element build() && {
        return View(props_, std::move(children_));
    }
    operator Element() && {
        return std::move(*this).build();
    }

private:
    std::vector<Element> children_{};
};

inline ViewNode view() {
    return ViewNode{};
}

inline TextNode text() {
    return TextNode{};
}

inline ButtonNode button() {
    return ButtonNode{};
}

inline InputNode input() {
    return InputNode{};
}

inline InputAreaNode input_area() {
    return InputAreaNode{};
}

inline CanvasNode canvas() {
    return CanvasNode{};
}

inline RiveNode rive() {
    return RiveNode{};
}

inline LineChartNode line_chart() {
    return LineChartNode{};
}

inline ScatterChartNode scatter_chart() {
    return ScatterChartNode{};
}

inline AreaChartNode area_chart() {
    return AreaChartNode{};
}

inline BarChartNode bar_chart() {
    return BarChartNode{};
}

inline CircleChartNode circle_chart() {
    return CircleChartNode{};
}

inline HistogramChartNode histogram_chart() {
    return HistogramChartNode{};
}

}
