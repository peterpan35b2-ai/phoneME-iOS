#pragma once

#include <mutex>
#include <span>
#include <vector>

#include "phoneme/base/Checked.hpp"

namespace phoneme::runtime {

struct FrameSnapshot final {
    Dimensions dimensions;
    u64 generation {0};
    std::vector<u8> rgba;
};

class Framebuffer final {
public:
    static constexpr i32 kMaximumDimension = 4'096;

    [[nodiscard]] Status resize(Dimensions dimensions);
    [[nodiscard]] Status replace(Dimensions dimensions,
                                 std::span<const u8> rgba);
    [[nodiscard]] FrameSnapshot snapshot() const;
    void clear() noexcept;

private:
    mutable std::mutex mutex_;
    Dimensions dimensions_;
    u64 generation_ {0};
    std::vector<u8> rgba_;
};

} // namespace phoneme::runtime
