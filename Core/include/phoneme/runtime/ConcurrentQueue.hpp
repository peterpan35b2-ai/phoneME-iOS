#pragma once

#include <deque>
#include <mutex>
#include <optional>
#include <utility>

#include "phoneme/base/Types.hpp"

namespace phoneme::runtime {

template <typename T>
class ConcurrentQueue final {
public:
    explicit ConcurrentQueue(usize capacity) : capacity_(capacity) {}

    void push(T value) {
        std::scoped_lock lock(mutex_);
        if (capacity_ == 0) {
            return;
        }
        if (queue_.size() == capacity_) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(value));
    }

    [[nodiscard]] std::optional<T> pop() {
        std::scoped_lock lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    void clear() noexcept {
        std::scoped_lock lock(mutex_);
        queue_.clear();
    }

    [[nodiscard]] usize size() const noexcept {
        std::scoped_lock lock(mutex_);
        return queue_.size();
    }

private:
    usize capacity_ {0};
    mutable std::mutex mutex_;
    std::deque<T> queue_;
};

} // namespace phoneme::runtime
