#pragma once

#include <mutex>
#include <span>
#include <vector>

#include "phoneme/base/Checked.hpp"

namespace phoneme::runtime {

struct FrameMetadata final {
    Dimensions dimensions;
    u64 generation {0};
    usize byte_count {0};
};

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
    [[nodiscard]] Status replace_exchange(Dimensions dimensions,
                                          std::vector<u8>& rgba);
    [[nodiscard]] FrameMetadata metadata() const noexcept;
    [[nodiscard]] FrameMetadata copy_rgba(std::span<u8> destination) const noexcept;
    [[nodiscard]] FrameSnapshot snapshot() const;
    void clear() noexcept;

private:
    mutable std::mutex mutex_;
    Dimensions dimensions_;
    u64 generation_ {0};
    std::vector<u8> rgba_;
};

} // namespace phoneme::runtime
