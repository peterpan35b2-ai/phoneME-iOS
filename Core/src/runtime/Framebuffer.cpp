#include "phoneme/runtime/Framebuffer.hpp"

#include <algorithm>
#include <utility>

namespace phoneme::runtime
{
  namespace
  {

    [[nodiscard]] Result<usize> rgba_size(Dimensions dimensions)
    {
      if (!dimensions.valid() || dimensions.width > Framebuffer::kMaximumDimension ||
          dimensions.height > Framebuffer::kMaximumDimension)
      {
        return fail(ErrorCode::invalid_argument,
                    "framebuffer dimensions are outside the supported range");
      }
      auto width = checked_narrow<usize>(dimensions.width);
      auto height = checked_narrow<usize>(dimensions.height);
      if (!width || !height)
      {
        return fail(ErrorCode::overflow, "framebuffer dimension conversion failed");
      }
      auto pixels = checked_multiply(*width, *height);
      if (!pixels)
      {
        return std::unexpected(pixels.error());
      }
      return checked_multiply(*pixels, 4);
    }

  } // namespace

  Status Framebuffer::resize(Dimensions dimensions)
  {
    auto required = rgba_size(dimensions);
    if (!required)
    {
      return std::unexpected(required.error());
    }

    std::vector<u8> replacement(*required, 0);
    for (usize offset = 3; offset < replacement.size(); offset += 4)
    {
      replacement[offset] = 0xFFU;
    }

    std::scoped_lock lock(mutex_);
    dimensions_ = dimensions;
    rgba_ = std::move(replacement);
    ++generation_;
    return {};
  }

  Status Framebuffer::replace(Dimensions dimensions,
                              std::span<const u8> rgba)
  {
    auto required = rgba_size(dimensions);
    if (!required)
    {
      return std::unexpected(required.error());
    }
    if (rgba.size() != *required)
    {
      return fail(ErrorCode::invalid_argument,
                  "RGBA frame size does not match its dimensions");
    }

    std::vector<u8> replacement(rgba.begin(), rgba.end());
    std::scoped_lock lock(mutex_);
    dimensions_ = dimensions;
    rgba_ = std::move(replacement);
    ++generation_;
    return {};
  }

  Status Framebuffer::replace_exchange(Dimensions dimensions,
                                       std::vector<u8>& rgba)
  {
    auto required = rgba_size(dimensions);
    if (!required)
    {
      return std::unexpected(required.error());
    }
    if (rgba.size() != *required)
    {
      return fail(ErrorCode::invalid_argument,
                  "RGBA frame size does not match its dimensions");
    }

    std::scoped_lock lock(mutex_);
    dimensions_ = dimensions;
    rgba_.swap(rgba);
    ++generation_;
    return {};
  }

  FrameMetadata Framebuffer::metadata() const noexcept
  {
    std::scoped_lock lock(mutex_);
    return FrameMetadata{
        .dimensions = dimensions_,
        .generation = generation_,
        .byte_count = rgba_.size(),
    };
  }

  FrameMetadata Framebuffer::copy_rgba(
      std::span<u8> destination) const noexcept
  {
    std::scoped_lock lock(mutex_);
    const FrameMetadata result{
        .dimensions = dimensions_,
        .generation = generation_,
        .byte_count = rgba_.size(),
    };
    if (!rgba_.empty() && destination.size() >= rgba_.size())
    {
      std::copy(rgba_.begin(), rgba_.end(), destination.begin());
    }
    return result;
  }

  std::optional<FrameMetadata> Framebuffer::copy_rgba_since(
      u64 previous_generation,
      std::span<u8> destination) const noexcept
  {
    std::scoped_lock lock(mutex_);
    if (generation_ == previous_generation)
    {
      return std::nullopt;
    }
    const FrameMetadata result{
        .dimensions = dimensions_,
        .generation = generation_,
        .byte_count = rgba_.size(),
    };
    if (!rgba_.empty() && destination.size() >= rgba_.size())
    {
      std::copy(rgba_.begin(), rgba_.end(), destination.begin());
    }
    return result;
  }

  std::optional<Framebuffer::ReadLease> Framebuffer::acquire_rgba_since(
      u64 previous_generation) const noexcept
  {
    std::unique_lock lock(mutex_);
    if (generation_ == previous_generation || rgba_.empty())
    {
      return std::nullopt;
    }
    const FrameMetadata metadata{
        .dimensions = dimensions_,
        .generation = generation_,
        .byte_count = rgba_.size(),
    };
    return ReadLease(
        std::move(lock),
        metadata,
        std::span<const u8>(rgba_.data(), rgba_.size()));
  }

  FrameSnapshot Framebuffer::snapshot() const
  {
    std::scoped_lock lock(mutex_);
    return FrameSnapshot{
        .dimensions = dimensions_,
        .generation = generation_,
        .rgba = rgba_,
    };
  }

  void Framebuffer::clear() noexcept
  {
    std::scoped_lock lock(mutex_);
    dimensions_ = {};
    rgba_.clear();
    ++generation_;
  }

} // namespace phoneme::runtime
