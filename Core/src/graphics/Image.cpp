#include "phoneme/graphics/Image.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace phoneme::graphics {
namespace {

constexpr usize kMaximumPixels = 64U * 1024U * 1024U;

[[nodiscard]] Result<usize> pixel_count(i32 width, i32 height) {
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

Result<Image> Image::create_mutable(i32 width, i32 height) {
    auto count = pixel_count(width, height);
    if (!count) {
        return std::unexpected(count.error());
    }
    return Image(width,
                 height,
                 true,
                 std::vector<Pixel>(*count, 0xFFFFFFFFU));
}

Result<Image> Image::create_immutable(i32 width,
                                      i32 height,
                                      std::span<const Pixel> pixels) {
    auto count = pixel_count(width, height);
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
    auto count = pixel_count(output_size.width, output_size.height);
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
    destination = blend ? source_over(pixel_value, destination) : pixel_value;
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
