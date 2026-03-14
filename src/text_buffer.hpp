#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace reactcpp::text {

class TextBuffer {
public:
    enum class Kind {
        String,
        Gap
    };

    TextBuffer() : kind_(Kind::String) {}

    explicit TextBuffer(std::string_view s, Kind kind = Kind::String) : kind_(kind) {
        set_string(s);
        if (kind_ == Kind::Gap) {
            ensure_gap_min_(default_gap_size_);
        }
    }

    Kind kind() const {
        return kind_;
    }

    void set_kind(Kind k) {
        if (k == kind_) return;
        const std::string snapshot = to_string();
        kind_ = k;
        set_string(snapshot);
        if (kind_ == Kind::Gap) {
            ensure_gap_min_(default_gap_size_);
        }
    }

    std::string_view view() const {
        if (kind_ == Kind::String) {
            return str_;
        }
        if (!cache_valid_) {
            cache_.clear();
            cache_.reserve(size());
            cache_.append(buf_.data(), gap_start_);
            cache_.append(buf_.data() + gap_end_, buf_.size() - gap_end_);
            cache_valid_ = true;
        }
        return cache_;
    }

    bool empty() const {
        return size() == 0;
    }

    std::size_t size() const {
        if (kind_ == Kind::String) {
            return str_.size();
        }
        return buf_.size() - (gap_end_ - gap_start_);
    }

    void clear() {
        if (kind_ == Kind::String) {
            str_.clear();
            cache_valid_ = false;
            return;
        }
        buf_.assign(default_gap_size_, '\0');
        gap_start_ = 0;
        gap_end_ = buf_.size();
        cache_valid_ = false;
    }

    void set_string(std::string_view s) {
        if (kind_ == Kind::String) {
            str_.assign(s);
            cache_valid_ = false;
            return;
        }

        buf_.assign(s.begin(), s.end());
        gap_start_ = buf_.size();
        buf_.resize(buf_.size() + default_gap_size_, '\0');
        gap_end_ = buf_.size();
        cache_valid_ = false;
    }

    std::string to_string() const {
        const std::string_view v = view();
        return std::string(v);
    }

    std::string substr(std::size_t pos, std::size_t len) const {
        const std::size_t n = size();
        pos = std::min(pos, n);
        len = std::min(len, n - pos);
        if (kind_ == Kind::String) {
            return str_.substr(pos, len);
        }
        std::string out;
        out.reserve(len);
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(byte_at(pos + i));
        }
        return out;
    }

    char byte_at(std::size_t pos) const {
        const std::size_t n = size();
        if (pos >= n) return '\0';
        if (kind_ == Kind::String) {
            return str_[pos];
        }
        if (pos < gap_start_) {
            return buf_[pos];
        }
        return buf_[pos + (gap_end_ - gap_start_)];
    }

    void insert(std::size_t pos, std::string_view bytes) {
        if (bytes.empty()) return;
        pos = std::min(pos, size());
        if (kind_ == Kind::String) {
            str_.insert(pos, bytes);
            cache_valid_ = false;
            return;
        }
        move_gap_to_(pos);
        ensure_gap_min_(bytes.size());
        std::copy(bytes.begin(), bytes.end(), buf_.begin() + static_cast<std::ptrdiff_t>(gap_start_));
        gap_start_ += bytes.size();
        assert(gap_start_ <= gap_end_);
        cache_valid_ = false;
    }

    void erase(std::size_t pos, std::size_t len) {
        const std::size_t n = size();
        pos = std::min(pos, n);
        len = std::min(len, n - pos);
        if (len == 0) return;
        if (kind_ == Kind::String) {
            str_.erase(pos, len);
            cache_valid_ = false;
            return;
        }
        move_gap_to_(pos);
        gap_end_ += len;
        if (gap_end_ > buf_.size()) {
            gap_end_ = buf_.size();
        }
        cache_valid_ = false;
    }

private:
    void ensure_gap_min_(std::size_t min_free) {
        const std::size_t free = gap_end_ - gap_start_;
        if (free >= min_free) return;

        const std::size_t grow = std::max(default_gap_size_, min_free - free);
        const std::size_t old_size = buf_.size();
        buf_.resize(old_size + grow, '\0');

        const std::size_t tail_size = old_size - gap_end_;
        std::move_backward(
            buf_.begin() + static_cast<std::ptrdiff_t>(gap_end_),
            buf_.begin() + static_cast<std::ptrdiff_t>(old_size),
            buf_.begin() + static_cast<std::ptrdiff_t>(buf_.size())
        );

        gap_end_ += grow;
        (void)tail_size;
        cache_valid_ = false;
    }

    void move_gap_to_(std::size_t pos) {
        pos = std::min(pos, size());
        if (pos == gap_start_) return;

        if (pos < gap_start_) {
            const std::size_t shift = gap_start_ - pos;
            std::move_backward(
                buf_.begin() + static_cast<std::ptrdiff_t>(pos),
                buf_.begin() + static_cast<std::ptrdiff_t>(gap_start_),
                buf_.begin() + static_cast<std::ptrdiff_t>(gap_end_)
            );
            gap_start_ -= shift;
            gap_end_ -= shift;
        } else {
            const std::size_t shift = pos - gap_start_;
            std::move(
                buf_.begin() + static_cast<std::ptrdiff_t>(gap_end_),
                buf_.begin() + static_cast<std::ptrdiff_t>(gap_end_ + shift),
                buf_.begin() + static_cast<std::ptrdiff_t>(gap_start_)
            );
            gap_start_ += shift;
            gap_end_ += shift;
        }
        assert(gap_start_ <= gap_end_);
        cache_valid_ = false;
    }

    static constexpr std::size_t default_gap_size_ = 64;

    Kind kind_{Kind::String};
    std::string str_;
    mutable std::string cache_;
    mutable bool cache_valid_{false};
    std::vector<char> buf_;
    std::size_t gap_start_{0};
    std::size_t gap_end_{0};
};

}
