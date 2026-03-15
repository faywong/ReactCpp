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

}
