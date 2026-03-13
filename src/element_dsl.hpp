#pragma once

#include "skia_runtime.hpp"

#include <concepts>
#include <type_traits>
#include <utility>
#include <vector>

namespace reactcpp::ui {

namespace detail {

template <typename Fn, typename Props>
concept PropsFnFor = requires(Fn fn, Props& p) {
    fn(p);
};

template <typename T>
concept ElementLike = std::same_as<std::remove_cvref_t<T>, Element>;

inline void append_child(std::vector<Element>& out, Element child) {
    out.push_back(std::move(child));
}

template <typename... Children>
requires (ElementLike<Children> && ...)
std::vector<Element> collect_children(Children&&... children) {
    std::vector<Element> out;
    out.reserve(sizeof...(Children));
    (append_child(out, std::forward<Children>(children)), ...);
    return out;
}

}
inline Element text() {
    return Text(TextProps{});
}

template <typename PropsFn>
requires detail::PropsFnFor<PropsFn, TextProps>
Element text(PropsFn&& props_fn) {
    TextProps p;
    std::forward<PropsFn>(props_fn)(p);
    return Text(p);
}

inline Element button() {
    return Button(ButtonProps{});
}

template <typename PropsFn>
requires detail::PropsFnFor<PropsFn, ButtonProps>
Element button(PropsFn&& props_fn) {
    ButtonProps p;
    std::forward<PropsFn>(props_fn)(p);
    return Button(p);
}

inline Element input() {
    return Input(InputProps{});
}

template <typename PropsFn>
requires detail::PropsFnFor<PropsFn, InputProps>
Element input(PropsFn&& props_fn) {
    InputProps p;
    std::forward<PropsFn>(props_fn)(p);
    return Input(p);
}

inline Element view() {
    return View(ViewProps{}, {});
}

template <typename... Children>
requires (detail::ElementLike<Children> && ...)
Element view(Children&&... children) {
    return View(ViewProps{}, detail::collect_children(std::forward<Children>(children)...));
}

template <typename PropsFn, typename... Children>
requires detail::PropsFnFor<PropsFn, ViewProps>
Element view(PropsFn&& props_fn, Children&&... children) {
    ViewProps p;
    std::forward<PropsFn>(props_fn)(p);
    return View(p, detail::collect_children(std::forward<Children>(children)...));
}

}
