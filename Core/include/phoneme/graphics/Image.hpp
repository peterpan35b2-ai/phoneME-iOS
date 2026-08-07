#pragma once

#include <span>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/graphics/Color.hpp"

namespace phoneme::graphics {

enum class Transform : i32 {
    none = 0,
    mirror_rotate_180 = 1,
    mirror = 2,
    rotate_180 = 3,
    mirror_rotate_270 = 4,
    rotate_90 = 5,
    rotate_270 = 6,
    mirror_rotate_90 = 7,
};

struct Size final {
    i32 width {0};
    i32 height {0};
};

struct ImageRegion final {
    i32 x {0};
    i32 y {0};
    i32 width {0};
    i32 height {0};
};

class Image final {
public:
    [[nodiscard]] static Result<Image> create_mutable(i32 width,
                                                       i32 height);
    [[nodiscard]] static Result<Image> create_mutable_argb(i32 width,
                                                            i32 height,
                                                            Pixel initial_pixel = 0U);
    [[nodiscard]] static Result<Image> create_immutable(
        i32 width,
        i32 height,
        std::span<const Pixel> pixels);
    [[nodiscard]] static Result<Image> create_immutable_owned(
        i32 width,
        i32 height,
        std::vector<Pixel> pixels);
    [[nodiscard]] static Result<Image> transformed_region(
        const Image& source,
        i32 x,
        i32 y,
        i32 width,
        i32 height,
        Transform transform);

    [[nodiscard]] i32 width() const noexcept { return width_; }
    [[nodiscard]] i32 height() const noexcept { return height_; }
    [[nodiscard]] bool is_mutable() const noexcept { return mutable_; }
    [[nodiscard]] std::span<const Pixel> pixels() const noexcept {
        return pixels_;
    }
    [[nodiscard]] std::span<Pixel> mutable_pixels() noexcept {
        return pixels_;
    }
    [[nodiscard]] bool has_dirty_region() const noexcept { return dirty_; }
    [[nodiscard]] ImageRegion dirty_region() const noexcept {
        return dirty_region_;
    }
    void clear_dirty_region() noexcept;
    void mark_dirty_region(i32 x, i32 y, i32 width, i32 height) noexcept;

    [[nodiscard]] Result<Pixel> pixel(i32 x, i32 y) const;
    [[nodiscard]] Status set_pixel(i32 x,
                                   i32 y,
                                   Pixel pixel,
                                   bool blend);

private:
    Image(i32 width,
          i32 height,
          bool mutable_image,
          std::vector<Pixel> pixels) noexcept;

    i32 width_ {0};
    i32 height_ {0};
    bool mutable_ {false};
    std::vector<Pixel> pixels_;
    bool dirty_ {false};
    ImageRegion dirty_region_ {};
};

[[nodiscard]] Result<Transform> transform_from_int(i32 value);
[[nodiscard]] Size transformed_size(i32 width,
                                    i32 height,
                                    Transform transform) noexcept;
[[nodiscard]] Result<Size> validate_dimensions(i32 width, i32 height);
[[nodiscard]] Result<usize> validated_pixel_count(i32 width, i32 height);

} // namespace phoneme::graphics
