#pragma once

#include <mutex>
#include <optional>
#include <span>
#include <utility>
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

struct FrameRegionUpdate final {
    i32 x {0};
    i32 y {0};
    i32 width {0};
    i32 height {0};
    std::span<const u8> rgba;
};

struct FrameDamageRegion final {
    i32 x {0};
    i32 y {0};
    i32 width {0};
    i32 height {0};
};

class Framebuffer final {
public:
    static constexpr i32 kMaximumDimension = 4'096;

    class ReadLease final {
    public:
        ReadLease(ReadLease&&) noexcept = default;
        ReadLease& operator=(ReadLease&&) noexcept = default;
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;

        [[nodiscard]] const FrameMetadata& metadata() const noexcept {
            return metadata_;
        }
        [[nodiscard]] std::span<const u8> pixels() const noexcept {
            return pixels_;
        }
        [[nodiscard]] std::span<const FrameDamageRegion> damage_regions()
            const noexcept {
            return damage_regions_;
        }

    private:
        friend class Framebuffer;
        ReadLease(std::unique_lock<std::mutex> lock,
                  FrameMetadata metadata,
                  std::span<const u8> pixels,
                  std::span<const FrameDamageRegion> damage_regions) noexcept
            : lock_(std::move(lock)),
              metadata_(metadata),
              pixels_(pixels),
              damage_regions_(damage_regions) {}

        std::unique_lock<std::mutex> lock_;
        FrameMetadata metadata_;
        std::span<const u8> pixels_;
        std::span<const FrameDamageRegion> damage_regions_;
    };

    [[nodiscard]] Status resize(Dimensions dimensions);
    [[nodiscard]] Status replace(Dimensions dimensions,
                                 std::span<const u8> rgba);
    [[nodiscard]] Status replace_exchange(Dimensions dimensions,
                                          std::vector<u8>& rgba);
    [[nodiscard]] Status update_region(Dimensions dimensions,
                                       i32 x,
                                       i32 y,
                                       i32 width,
                                       i32 height,
                                       std::span<const u8> rgba);
    [[nodiscard]] Status update_regions(
        Dimensions dimensions,
        std::span<const FrameRegionUpdate> updates);
    [[nodiscard]] FrameMetadata metadata() const noexcept;
    [[nodiscard]] FrameMetadata copy_rgba(std::span<u8> destination) const noexcept;
    [[nodiscard]] std::optional<FrameMetadata> copy_rgba_since(
        u64 previous_generation,
        std::span<u8> destination) const noexcept;
    [[nodiscard]] std::optional<ReadLease> acquire_rgba_since(
        u64 previous_generation) const noexcept;
    [[nodiscard]] FrameSnapshot snapshot() const;
    void clear() noexcept;

private:
    mutable std::mutex mutex_;
    Dimensions dimensions_;
    u64 generation_ {0};
    std::vector<u8> rgba_;
    std::vector<FrameDamageRegion> damage_regions_;
};

} // namespace phoneme::runtime
