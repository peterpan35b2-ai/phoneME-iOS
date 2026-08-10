#include "phoneme/graphics/Image.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace phoneme::graphics {
namespace {

constexpr usize kMaximumPixels = 64U * 1024U * 1024U;

[[nodiscard]] std::pair<i32, i32> source_coordinate(
    i32 destination_x,
    i32 destination_y,
    i32 width,
    i32 height,
    Transform transform) noexcept {
    switch (transform) {
    case Transform::none:
        return {destination_x, destination_y};
    case Transform::mirror_rotate_180:
        return {destination_x, height - 1 - destination_y};
    case Transform::mirror:
        return {width - 1 - destination_x, destination_y};
    case Transform::rotate_180:
        return {width - 1 - destination_x,
                height - 1 - destination_y};
    case Transform::mirror_rotate_270:
        return {destination_y, destination_x};
    case Transform::rotate_90:
        return {destination_y, height - 1 - destination_x};
    case Transform::rotate_270:
        return {width - 1 - destination_y, destination_x};
    case Transform::mirror_rotate_90:
        return {width - 1 - destination_y,
                height - 1 - destination_x};
    }
    return {destination_x, destination_y};
}

} // namespace

Image::Image(i32 width,
             i32 height,
             bool mutable_image,
             std::vector<Pixel> pixels) noexcept
    : width_(width),
      height_(height),
      mutable_(mutable_image),
      pixels_(std::move(pixels)) {}

Result<Size> validate_dimensions(i32 width, i32 height) {
    if (width <= 0 || height <= 0) {
        return fail(ErrorCode::invalid_argument,
                    "image dimensions must be positive");
    }
    return Size {.width = width, .height = height};
}

Result<usize> validated_pixel_count(i32 width, i32 height) {
    auto validated = validate_dimensions(width, height);
    if (!validated) {
        return std::unexpected(validated.error());
    }
    const auto unsigned_width = static_cast<usize>(width);
    const auto unsigned_height = static_cast<usize>(height);
    if (unsigned_height != 0U &&
        unsigned_width > std::numeric_limits<usize>::max() / unsigned_height) {
        return fail(ErrorCode::overflow, "image pixel count overflows size_t");
    }
    const usize count = unsigned_width * unsigned_height;
    if (count > kMaximumPixels) {
        return fail(ErrorCode::overflow,
                    "image exceeds the portable graphics pixel budget");
    }
    return count;
}

Result<Image> Image::create_mutable(i32 width, i32 height) {
    return create_mutable_argb(width, height, 0xFFFFFFFFU);
}

Result<Image> Image::create_mutable_argb(i32 width,
                                         i32 height,
                                         Pixel initial_pixel) {
    auto count = validated_pixel_count(width, height);
    if (!count) {
        return std::unexpected(count.error());
    }
    Image image(width,
                height,
                true,
                std::vector<Pixel>(*count, initial_pixel));
    image.mark_dirty_region(0, 0, width, height);
    return image;
}

Result<Image> Image::create_immutable(i32 width,
                                      i32 height,
                                      std::span<const Pixel> pixels) {
    auto count = validated_pixel_count(width, height);
    if (!count) {
        return std::unexpected(count.error());
    }
    if (pixels.size() != *count) {
        return fail(ErrorCode::invalid_argument,
                    "immutable image pixel count does not match dimensions");
    }
    return Image(width,
                 height,
                 false,
                 std::vector<Pixel>(pixels.begin(), pixels.end()));
}

Result<Image> Image::create_immutable_owned(i32 width,
                                            i32 height,
                                            std::vector<Pixel> pixels) {
    auto count = validated_pixel_count(width, height);
    if (!count) {
        return std::unexpected(count.error());
    }
    if (pixels.size() != *count) {
        return fail(ErrorCode::invalid_argument,
                    "immutable image pixel count does not match dimensions");
    }
    return Image(width, height, false, std::move(pixels));
}

Result<Image> Image::transformed_region(const Image& source,
                                        i32 x,
                                        i32 y,
                                        i32 width,
                                        i32 height,
                                        Transform transform) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x > source.width_ - width || y > source.height_ - height) {
        return fail(ErrorCode::out_of_range,
                    "image region is outside the source image");
    }
    const Size output_size = transformed_size(width, height, transform);
    auto count = validated_pixel_count(output_size.width,
                                       output_size.height);
    if (!count) {
        return std::unexpected(count.error());
    }
    std::vector<Pixel> output(*count, 0U);
    for (i32 destination_y = 0;
         destination_y < output_size.height;
         ++destination_y) {
        for (i32 destination_x = 0;
             destination_x < output_size.width;
             ++destination_x) {
            const auto [source_x, source_y] = source_coordinate(
                destination_x,
                destination_y,
                width,
                height,
                transform);
            const usize source_index =
                static_cast<usize>(y + source_y) *
                    static_cast<usize>(source.width_) +
                static_cast<usize>(x + source_x);
            const usize destination_index =
                static_cast<usize>(destination_y) *
                    static_cast<usize>(output_size.width) +
                static_cast<usize>(destination_x);
            output[destination_index] = source.pixels_[source_index];
        }
    }
    return Image(output_size.width,
                 output_size.height,
                 false,
                 std::move(output));
}

void Image::clear_dirty_region() noexcept {
    dirty_ = false;
    dirty_region_ = {};
}

void Image::mark_dirty_region(i32 x,
                              i32 y,
                              i32 width,
                              i32 height) noexcept {
    if (!mutable_ || width <= 0 || height <= 0) {
        return;
    }
    const i64 left = std::max<i64>(0, x);
    const i64 top = std::max<i64>(0, y);
    const i64 right = std::min<i64>(
        width_, static_cast<i64>(x) + static_cast<i64>(width));
    const i64 bottom = std::min<i64>(
        height_, static_cast<i64>(y) + static_cast<i64>(height));
    if (right <= left || bottom <= top) {
        return;
    }
    if (!dirty_) {
        dirty_ = true;
        dirty_region_ = ImageRegion {
            .x = static_cast<i32>(left),
            .y = static_cast<i32>(top),
            .width = static_cast<i32>(right - left),
            .height = static_cast<i32>(bottom - top),
        };
        return;
    }
    const i64 dirty_right = static_cast<i64>(dirty_region_.x) +
                            dirty_region_.width;
    const i64 dirty_bottom = static_cast<i64>(dirty_region_.y) +
                             dirty_region_.height;
    const i64 combined_left = std::min<i64>(dirty_region_.x, left);
    const i64 combined_top = std::min<i64>(dirty_region_.y, top);
    const i64 combined_right = std::max(dirty_right, right);
    const i64 combined_bottom = std::max(dirty_bottom, bottom);
    dirty_region_ = ImageRegion {
        .x = static_cast<i32>(combined_left),
        .y = static_cast<i32>(combined_top),
        .width = static_cast<i32>(combined_right - combined_left),
        .height = static_cast<i32>(combined_bottom - combined_top),
    };
}

Result<Pixel> Image::pixel(i32 x, i32 y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return fail(ErrorCode::out_of_range,
                    "pixel coordinate is outside the image");
    }
    return pixels_[static_cast<usize>(y) * static_cast<usize>(width_) +
                   static_cast<usize>(x)];
}

Status Image::set_pixel(i32 x,
                        i32 y,
                        Pixel pixel_value,
                        bool blend) {
    if (!mutable_) {
        return fail(ErrorCode::invalid_state,
                    "cannot draw into an immutable image");
    }
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return {};
    }
    Pixel& destination =
        pixels_[static_cast<usize>(y) * static_cast<usize>(width_) +
                static_cast<usize>(x)];
    Pixel composited = destination;
    if (!blend) {
        composited = rgb565_roundtrip(pixel_value);
    } else {
        const u8 source_alpha = alpha(pixel_value);
        if (source_alpha == 0U) return {};
        composited = source_alpha == 255U
            ? rgb565_roundtrip(pixel_value)
            : rgb565_roundtrip(source_over(pixel_value, destination));
    }
    if (composited != destination) {
        destination = composited;
        mark_dirty_region(x, y, 1, 1);
    }
    return {};
}

Result<Transform> transform_from_int(i32 value) {
    if (value < static_cast<i32>(Transform::none) ||
        value > static_cast<i32>(Transform::mirror_rotate_90)) {
        return fail(ErrorCode::invalid_argument,
                    "invalid MIDP sprite transform");
    }
    return static_cast<Transform>(value);
}

Size transformed_size(i32 width,
                      i32 height,
                      Transform transform) noexcept {
    switch (transform) {
    case Transform::mirror_rotate_270:
    case Transform::rotate_90:
    case Transform::rotate_270:
    case Transform::mirror_rotate_90:
        return Size {.width = height, .height = width};
    default:
        return Size {.width = width, .height = height};
    }
}

} // namespace phoneme::graphics
