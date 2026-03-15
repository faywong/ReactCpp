#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include "core/SkPicture.h"

#include <SDL3/SDL.h>

namespace reactcpp {

enum class UiEventType {
    Quit,
    MouseButtonDown,
    MouseButtonUp,
    MouseMotion,
    MouseWheel,
    TextInput,
    TextEditing,
    KeyDown,
};

struct UiEvent {
    UiEventType type{UiEventType::Quit};

    float x{0.0f};
    float y{0.0f};
    float wheel_y{0.0f};
    std::uint8_t clicks{0};
    std::uint8_t mouse_button{0};

    SDL_Keycode key{0};
    SDL_Keymod mod{SDL_KMOD_NONE};
    bool repeat{false};

    std::string text;
    int edit_start{0};
    int edit_length{0};
};

class UiEventQueue {
public:
    void push(UiEvent ev) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            q_.push_back(std::move(ev));
        }
        cv_.notify_one();
    }

    bool pop_wait(UiEvent& out) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return stop_ || !q_.empty(); });
        if (stop_) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<UiEvent> q_;
    bool stop_{false};
};

enum class PlatformCmdType {
    StartTextInput,
    StopTextInput,
    ClearComposition,
    SetTextInputArea,
    SetClipboardText,
    GetClipboardText,
    SetMouseCapture,
};

struct PlatformCommand {
    PlatformCmdType type{PlatformCmdType::StopTextInput};

    bool multiline{false};
    bool mouse_capture{false};

    SDL_Rect rect{0, 0, 0, 0};
    int cursor_px{0};

    std::string text;

    std::uint64_t request_id{0};
};

class PlatformCommandQueue {
public:
    void push(PlatformCommand cmd) {
        std::lock_guard<std::mutex> lock(mu_);
        q_.push_back(std::move(cmd));
    }

    std::optional<PlatformCommand> try_pop() {
        std::lock_guard<std::mutex> lock(mu_);
        if (q_.empty()) return std::nullopt;
        PlatformCommand cmd = std::move(q_.front());
        q_.pop_front();
        return cmd;
    }

private:
    std::mutex mu_;
    std::deque<PlatformCommand> q_;
};

class ClipboardRpc {
public:
    std::uint64_t new_request_id() {
        std::lock_guard<std::mutex> lock(mu_);
        return ++next_id_;
    }

    void set_response(std::uint64_t id, std::string text) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            last_response_id_ = id;
            last_text_ = std::move(text);
        }
        cv_.notify_all();
    }

    std::string wait_response(std::uint64_t id) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return last_response_id_ == id; });
        return last_text_;
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::uint64_t next_id_{0};
    std::uint64_t last_response_id_{0};
    std::string last_text_;
};

struct Frame {
    sk_sp<SkPicture> picture;
    std::uint64_t frame_id{0};
};

class FrameMailbox {
public:
    void publish(Frame f) {
        std::lock_guard<std::mutex> lock(mu_);
        latest_ = std::move(f);
        has_latest_ = true;
    }

    bool try_consume(Frame& out) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!has_latest_) return false;
        out = std::move(latest_);
        has_latest_ = false;
        return true;
    }

private:
    std::mutex mu_;
    Frame latest_{};
    bool has_latest_{false};
};

struct PlatformBridge {
    PlatformCommandQueue* cmds{};
    ClipboardRpc* clipboard{};
};

}
