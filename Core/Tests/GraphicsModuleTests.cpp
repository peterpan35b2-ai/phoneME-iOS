#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "phoneme/graphics/Graphics.hpp"
#include "phoneme/graphics/GraphicsStore.hpp"
#include "phoneme/graphics/ImageDecoder.hpp"
#include "phoneme/graphics/PngDecoder.hpp"
#include "phoneme/graphics/TextRasterizer.hpp"

namespace {

using phoneme::i32;
using phoneme::i64;
using phoneme::u8;
using phoneme::u32;
using phoneme::u64;
using phoneme::usize;
using phoneme::graphics::Image;
using phoneme::graphics::Pixel;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

template <typename Left, typename Right>
void require_equal(const Left& actual,
                   const Right& expected,
                   const char* message) {
    if (actual != expected) {
        std::cerr << "FAILED: " << message << " actual=" << actual
                  << " expected=" << expected << '\n';
        std::abort();
    }
}

std::vector<u8> decode_base64(std::string_view text) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<u8> output;
    output.reserve((text.size() * 3U) / 4U);
    u32 accumulator = 0U;
    i32 available_bits = -8;
    for (const char character : text) {
        if (character == '=') {
            break;
        }
        const usize index = alphabet.find(character);
        require(index != std::string_view::npos,
                "base64 fixture contains only valid characters");
        accumulator = (accumulator << 6U) | static_cast<u32>(index);
        available_bits += 6;
        if (available_bits >= 0) {
            output.push_back(static_cast<u8>(
                (accumulator >> static_cast<u32>(available_bits)) & 0xFFU));
            available_bits -= 8;
        }
    }
    return output;
}

void require_rgb_near(Pixel actual,
                      u8 expected_red,
                      u8 expected_green,
                      u8 expected_blue,
                      i32 tolerance,
                      const char* message) {
    const auto near = [tolerance](u8 value, u8 expected) {
        return std::abs(static_cast<i32>(value) -
                        static_cast<i32>(expected)) <= tolerance;
    };
    const u8 alpha = static_cast<u8>(actual >> 24U);
    const u8 red = static_cast<u8>(actual >> 16U);
    const u8 green = static_cast<u8>(actual >> 8U);
    const u8 blue = static_cast<u8>(actual);
    require(alpha == 0xFFU &&
                near(red, expected_red) &&
                near(green, expected_green) &&
                near(blue, expected_blue),
            message);
}

void append_be32(std::vector<u8>& output, u32 value) {
    output.push_back(static_cast<u8>(value >> 24U));
    output.push_back(static_cast<u8>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<u8>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<u8>(value & 0xFFU));
}

void append_chunk(std::vector<u8>& output,
                  std::string_view type,
                  std::span<const u8> data) {
    require(type.size() == 4U, "PNG test chunk type length");
    require(data.size() <= std::numeric_limits<u32>::max(),
            "PNG test chunk length");
    append_be32(output, static_cast<u32>(data.size()));
    const usize type_offset = output.size();
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc,
                reinterpret_cast<const Bytef*>(output.data() + type_offset),
                static_cast<uInt>(4U + data.size()));
    append_be32(output, static_cast<u32>(crc));
}

std::vector<u8> compressed_bytes(std::span<const u8> input) {
    uLongf capacity = compressBound(static_cast<uLong>(input.size()));
    std::vector<u8> output(static_cast<usize>(capacity));
    const int status = compress2(reinterpret_cast<Bytef*>(output.data()),
                                 &capacity,
                                 reinterpret_cast<const Bytef*>(input.data()),
                                 static_cast<uLong>(input.size()),
                                 Z_BEST_SPEED);
    require(status == Z_OK, "compress PNG test rows");
    output.resize(static_cast<usize>(capacity));
    return output;
}

std::vector<u8> png_prefix(i32 width,
                           i32 height,
                           u8 bit_depth,
                           u8 color_type,
                           u8 interlace) {
    std::vector<u8> output {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
    };
    std::array<u8, 13> header {};
    header[0] = static_cast<u8>(static_cast<u32>(width) >> 24U);
    header[1] = static_cast<u8>((static_cast<u32>(width) >> 16U) & 0xFFU);
    header[2] = static_cast<u8>((static_cast<u32>(width) >> 8U) & 0xFFU);
    header[3] = static_cast<u8>(static_cast<u32>(width) & 0xFFU);
    header[4] = static_cast<u8>(static_cast<u32>(height) >> 24U);
    header[5] = static_cast<u8>((static_cast<u32>(height) >> 16U) & 0xFFU);
    header[6] = static_cast<u8>((static_cast<u32>(height) >> 8U) & 0xFFU);
    header[7] = static_cast<u8>(static_cast<u32>(height) & 0xFFU);
    header[8] = bit_depth;
    header[9] = color_type;
    header[10] = 0U;
    header[11] = 0U;
    header[12] = interlace;
    append_chunk(output, "IHDR", header);
    return output;
}

std::vector<u8> make_png(i32 width,
                         i32 height,
                         u8 bit_depth,
                         u8 color_type,
                         u8 interlace,
                         std::span<const u8> filtered_rows,
                         std::span<const u8> palette = {},
                         std::span<const u8> transparency = {}) {
    auto output = png_prefix(width,
                             height,
                             bit_depth,
                             color_type,
                             interlace);
    if (!palette.empty()) {
        append_chunk(output, "PLTE", palette);
    }
    if (!transparency.empty()) {
        append_chunk(output, "tRNS", transparency);
    }
    const auto compressed = compressed_bytes(filtered_rows);
    append_chunk(output, "IDAT", compressed);
    append_chunk(output, "IEND", {});
    return output;
}

u64 image_hash(const Image& image) {
    u64 hash = 1469598103934665603ULL;
    const auto mix = [&](u8 value, u64& state) {
        state ^= value;
        state *= 1099511628211ULL;
    };
    for (const Pixel pixel : image.pixels()) {
        mix(static_cast<u8>(pixel >> 24U), hash);
        mix(static_cast<u8>((pixel >> 16U) & 0xFFU), hash);
        mix(static_cast<u8>((pixel >> 8U) & 0xFFU), hash);
        mix(static_cast<u8>(pixel & 0xFFU), hash);
    }
    mix(static_cast<u8>(image.width() & 0xFF), hash);
    mix(static_cast<u8>(image.height() & 0xFF), hash);
    return hash;
}

void test_clip_translate_alpha_and_dirty_region() {
    auto transparent = Image::create_mutable_argb(2, 2);
    require(transparent.has_value(), "create transparent mutable ARGB image");
    require(transparent->is_mutable(), "transparent ARGB image is mutable");
    require(transparent->pixel(0, 0).value() == 0x00000000U &&
                transparent->pixel(1, 1).value() == 0x00000000U,
            "mutable ARGB image starts transparent instead of MIDP white");
    phoneme::graphics::GraphicsContext transparent_context;
    transparent_context.clip = target_bounds(*transparent);
    transparent_context.color = 0xFFFF0000U;
    require(fill_rect(*transparent, transparent_context, 0, 0, 2, 2).has_value(),
            "fill transparent ARGB image before clear");
    require(clear_rect(*transparent, transparent_context, 1, 0, 1, 2).has_value(),
            "clear ARGB rectangle");
    require(transparent->pixel(0, 0).value() != 0x00000000U &&
                transparent->pixel(1, 0).value() == 0x00000000U &&
                transparent->pixel(1, 1).value() == 0x00000000U,
            "clear rectangle replaces destination alpha with transparent black");

    auto image = Image::create_mutable(8, 8);
    require(image.has_value(), "create mutable image");
    require(image->has_dirty_region(), "new mutable image starts dirty");
    image->clear_dirty_region();

    phoneme::graphics::GraphicsContext context;
    context.target_key = 1U;
    context.clip = phoneme::graphics::target_bounds(*image);
    context.color = 0xFF112233U;
    require(phoneme::graphics::fill_rect(*image, context, 0, 0, 8, 8)
                .has_value(),
            "fill image background");
    require(image->has_dirty_region(), "fill marks dirty image region");
    const auto full_dirty = image->dirty_region();
    require(full_dirty.x == 0 && full_dirty.y == 0 &&
                full_dirty.width == 8 && full_dirty.height == 8,
            "opaque fill coalesces full dirty rectangle");

    image->clear_dirty_region();
    require(phoneme::graphics::set_clip(context, *image, 1, 1, 4, 4)
                .has_value(),
            "set graphics clip");
    phoneme::graphics::translate(context, 1, 1);
    context.color = 0xFFFF0000U;
    require(phoneme::graphics::fill_rect(*image, context, 0, 0, 4, 4)
                .has_value(),
            "fill translated clipped rectangle");
    require(image->pixel(0, 0).value() == 0xFF102031U,
            "clip preserves the RGB565 background pixel");
    require(image->pixel(1, 1).value() == 0xFFFF0000U,
            "translate moves draw origin");
    require(image->pixel(4, 4).value() == 0xFFFF0000U,
            "clip includes final covered pixel");
    require(image->pixel(5, 5).value() == 0xFF102031U,
            "clip excludes the following RGB565 pixel");
    const auto dirty = image->dirty_region();
    require(dirty.x == 1 && dirty.y == 1 &&
                dirty.width == 4 && dirty.height == 4,
            "dirty rectangle follows clipped draw coverage");

    auto alpha_image = Image::create_mutable(2, 1);
    require(alpha_image.has_value(), "create alpha target");
    phoneme::graphics::GraphicsContext alpha_context;
    alpha_context.target_key = 2U;
    alpha_context.clip = phoneme::graphics::target_bounds(*alpha_image);
    constexpr std::array<Pixel, 1> source {0x800000FFU};
    require(phoneme::graphics::draw_rgb(*alpha_image,
                                        alpha_context,
                                        source,
                                        0,
                                        1,
                                        0,
                                        0,
                                        1,
                                        1,
                                        true)
                .has_value(),
            "blend ARGB source pixel");
    require(alpha_image->pixel(0, 0).value() == 0xFF7B7DFFU,
            "source-over alpha is stored through the MIDP RGB565 plane");

    constexpr std::array<Pixel, 1> transparent_red {0x00FF0000U};
    require(phoneme::graphics::draw_rgb(*alpha_image,
                                        alpha_context,
                                        transparent_red,
                                        0,
                                        1,
                                        1,
                                        0,
                                        1,
                                        1,
                                        false)
                .has_value(),
            "drawRGB ignores source alpha when processAlpha is false");
    require(alpha_image->pixel(1, 0).value() == 0xFFFF0000U,
            "drawRGB processAlpha=false writes opaque RGB");

    auto zero_stride_image = Image::create_mutable(2, 2);
    require(zero_stride_image.has_value(),
            "create zero-scanlength compatibility target");
    zero_stride_image->clear_dirty_region();
    phoneme::graphics::GraphicsContext zero_stride_context;
    zero_stride_context.target_key = 3U;
    zero_stride_context.clip =
        phoneme::graphics::target_bounds(*zero_stride_image);
    constexpr std::array<Pixel, 2> zero_stride_source {
        0xFFFF0000U, 0xFF00FF00U,
    };
    require(phoneme::graphics::draw_rgb(
                *zero_stride_image,
                zero_stride_context,
                zero_stride_source,
                0,
                0,
                0,
                0,
                2,
                2,
                true)
                .has_value(),
            "drawRGB accepts phoneME zero scanlength as a no-op");
    require(!zero_stride_image->has_dirty_region(),
            "zero-scanlength drawRGB leaves the target unchanged");

    constexpr std::array<Pixel, 1> undersized_zero_stride_source {
        0xFFFFFFFFU,
    };
    const auto out_of_bounds = phoneme::graphics::draw_rgb(
        *zero_stride_image,
        zero_stride_context,
        undersized_zero_stride_source,
        0,
        0,
        0,
        0,
        2,
        2,
        true);
    require(!out_of_bounds.has_value() &&
                out_of_bounds.error().code == phoneme::ErrorCode::out_of_range,
            "zero-scanlength drawRGB still validates the source slice");

    constexpr std::array<Pixel, 3> negative_dimension_source {
        0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU,
    };
    require(phoneme::graphics::draw_rgb(
                *zero_stride_image,
                zero_stride_context,
                negative_dimension_source,
                2,
                1,
                0,
                0,
                -1,
                1,
                true)
                .has_value(),
            "phoneME drawRGB accepts a bounds-valid negative-width no-op");
    require(!zero_stride_image->has_dirty_region(),
            "negative-width drawRGB leaves the target unchanged");

    constexpr std::array<Pixel, 0> empty_source {};
    const auto zero_size_out_of_bounds = phoneme::graphics::draw_rgb(
        *zero_stride_image,
        zero_stride_context,
        empty_source,
        0,
        0,
        0,
        0,
        0,
        0,
        true);
    require(!zero_size_out_of_bounds.has_value() &&
                zero_size_out_of_bounds.error().code ==
                    phoneme::ErrorCode::out_of_range,
            "phoneME drawRGB validates even a zero-size operation first");
}

void test_anchor_matrix_and_transform() {
    constexpr std::array<i32, 3> horizontal {
        phoneme::graphics::anchor_left,
        phoneme::graphics::anchor_right,
        phoneme::graphics::anchor_hcenter,
    };
    constexpr std::array<i32, 3> image_vertical {
        phoneme::graphics::anchor_top,
        phoneme::graphics::anchor_bottom,
        phoneme::graphics::anchor_vcenter,
    };
    constexpr std::array<i32, 3> text_vertical {
        phoneme::graphics::anchor_top,
        phoneme::graphics::anchor_bottom,
        phoneme::graphics::anchor_baseline,
    };
    for (const i32 horizontal_anchor : horizontal) {
        for (const i32 vertical_anchor : image_vertical) {
            auto rectangle = phoneme::graphics::anchored_rect(
                20, 30, 5, 7,
                horizontal_anchor | vertical_anchor,
                false);
            require(rectangle.has_value(),
                    "all legal image anchor combinations succeed");
        }
        for (const i32 vertical_anchor : text_vertical) {
            auto rectangle = phoneme::graphics::anchored_rect(
                20, 30, 5, 7,
                horizontal_anchor | vertical_anchor,
                true,
                6);
            require(rectangle.has_value(),
                    "all legal text anchor combinations succeed");
        }
    }
    auto default_anchor = phoneme::graphics::anchored_rect(
        4, 5, 2, 3, 0, false);
    require(default_anchor.has_value() && default_anchor->x == 4 &&
                default_anchor->y == 5,
            "zero anchor defaults to left-top");
    require(!phoneme::graphics::anchored_rect(
                 0, 0, 1, 1,
                 phoneme::graphics::anchor_left |
                     phoneme::graphics::anchor_right |
                     phoneme::graphics::anchor_top,
                 false)
                 .has_value(),
            "conflicting horizontal anchors fail");
    require(!phoneme::graphics::anchored_rect(
                 0, 0, 1, 1,
                 phoneme::graphics::anchor_left |
                     phoneme::graphics::anchor_baseline,
                 false)
                 .has_value(),
            "baseline anchor is invalid for images");
    require(!phoneme::graphics::anchored_rect(
                 0, 0, 1, 1,
                 phoneme::graphics::anchor_left |
                     phoneme::graphics::anchor_vcenter,
                 true,
                 1)
                 .has_value(),
            "vertical-center anchor is invalid for text");
    require(!phoneme::graphics::anchored_rect(
                 0, 0, 1, 1,
                 phoneme::graphics::anchor_left |
                     phoneme::graphics::anchor_top | 0x400,
                 false)
                 .has_value(),
            "unknown anchor bits fail");

    constexpr std::array<Pixel, 4> pixels {
        0xFFFF0000U, 0xFF00FF00U,
        0xFF0000FFU, 0xFFFFFFFFU,
    };
    auto source = Image::create_immutable(2, 2, pixels);
    require(source.has_value(), "create immutable source image");
    auto rotated = Image::transformed_region(
        *source, 0, 0, 2, 2, phoneme::graphics::Transform::rotate_90);
    require(rotated.has_value(), "rotate image region");
    require(rotated->pixel(0, 0).value() == 0xFF0000FFU &&
                rotated->pixel(1, 0).value() == 0xFFFF0000U &&
                rotated->pixel(0, 1).value() == 0xFFFFFFFFU &&
                rotated->pixel(1, 1).value() == 0xFF00FF00U,
            "ROT90 maps source pixels exactly");

    auto target = Image::create_mutable(4, 4);
    require(target.has_value(), "create anchor target");
    phoneme::graphics::GraphicsContext context;
    context.target_key = 3U;
    context.clip = phoneme::graphics::target_bounds(*target);
    require(phoneme::graphics::draw_image(
                *target,
                context,
                *source,
                4,
                4,
                phoneme::graphics::anchor_right |
                    phoneme::graphics::anchor_bottom)
                .has_value(),
            "draw image with right-bottom anchor");
    require(target->pixel(2, 2).value() == 0xFFFF0000U &&
                target->pixel(3, 3).value() == 0xFFFFFFFFU,
            "anchor places image relative to destination point");

    constexpr std::array<Pixel, 6> non_square_pixels {
        0xFFFF0000U, 0xFF00FF00U,
        0xFF0000FFU, 0xFFFFFFFFU,
        0xFFFFFF00U, 0xFFFF00FFU,
    };
    auto non_square_source = Image::create_immutable(
        2, 3, non_square_pixels);
    auto non_square_target = Image::create_mutable(8, 8);
    require(non_square_source.has_value() && non_square_target.has_value(),
            "create non-square drawRegion anchor fixtures");
    phoneme::graphics::GraphicsContext non_square_context;
    non_square_context.clip =
        phoneme::graphics::target_bounds(*non_square_target);
    require(phoneme::graphics::draw_region(
                *non_square_target,
                non_square_context,
                *non_square_source,
                0,
                0,
                2,
                3,
                phoneme::graphics::Transform::rotate_90,
                6,
                6,
                phoneme::graphics::anchor_right |
                    phoneme::graphics::anchor_bottom)
                .has_value(),
            "drawRegion rotates a non-square phoneME anchor fixture");
    require(non_square_target->pixel(4, 3).value() == 0xFFFFFF00U,
            "phoneME anchors rotated regions using source dimensions");
    require(non_square_target->pixel(3, 4).value() == 0xFFFFFFFFU,
            "drawRegion does not anchor using transformed dimensions");

    require(phoneme::graphics::draw_region(
                *non_square_target,
                non_square_context,
                *non_square_source,
                2,
                3,
                0,
                0,
                phoneme::graphics::Transform::none,
                0,
                0,
                phoneme::graphics::anchor_left |
                    phoneme::graphics::anchor_top)
                .has_value(),
            "phoneME accepts an in-bounds zero-size drawRegion no-op");
}

void test_primitive_golden_and_overflow_clipping() {
    auto image = Image::create_mutable(16, 16);
    require(image.has_value(), "create primitive golden target");
    phoneme::graphics::GraphicsContext context;
    context.clip = phoneme::graphics::target_bounds(*image);

    context.color = 0xFF102030U;
    require(phoneme::graphics::draw_line(*image, context, 0, 0, 15, 7)
                .has_value(),
            "draw solid line golden");
    context.stroke_style = phoneme::graphics::stroke_dotted;
    context.color = 0xFF405060U;
    require(phoneme::graphics::draw_line(*image, context, 0, 15, 15, 8)
                .has_value(),
            "draw dotted line golden");
    context.stroke_style = phoneme::graphics::stroke_solid;
    context.color = 0xFF708090U;
    require(phoneme::graphics::draw_rect(*image, context, 2, 2, 5, 4)
                .has_value(),
            "draw rectangle golden");
    context.color = 0xFFA01020U;
    require(phoneme::graphics::draw_round_rect(
                *image, context, 8, 1, 7, 6, 4, 4, false)
                .has_value(),
            "draw round rectangle golden");
    context.color = 0xFF20A040U;
    require(phoneme::graphics::fill_triangle(
                *image, context, 1, 8, 7, 14, 10, 7)
                .has_value(),
            "fill triangle golden");
    context.color = 0xFF2040A0U;
    require(phoneme::graphics::draw_arc(
                *image, context, 8, 8, 7, 7, 30, 240, false)
                .has_value(),
            "draw arc golden");

    constexpr u64 kExpectedGoldenHash = 2819450229388596111ULL;
    const u64 actual_hash = image_hash(*image);
    if (actual_hash != kExpectedGoldenHash) {
        std::cerr << "Primitive golden hash: " << actual_hash << '\n';
    }
    require_equal(actual_hash,
                  kExpectedGoldenHash,
                  "primitive RGBA golden hash remains stable");

    auto clipped = Image::create_mutable(4, 4);
    require(clipped.has_value(), "create overflow clip target");
    phoneme::graphics::GraphicsContext clipped_context;
    clipped_context.clip = phoneme::graphics::target_bounds(*clipped);
    clipped_context.stroke_style = phoneme::graphics::stroke_dotted;
    clipped_context.color = 0xFF000000U;
    require(phoneme::graphics::draw_line(
                *clipped,
                clipped_context,
                std::numeric_limits<i32>::min(),
                std::numeric_limits<i32>::min(),
                std::numeric_limits<i32>::max(),
                std::numeric_limits<i32>::max())
                .has_value(),
            "huge line is clipped before bounded rasterization");
    require(clipped->pixel(0, 0).value() == 0xFF000000U,
            "clipped huge line reaches first visible endpoint");

    phoneme::graphics::translate(clipped_context,
                                std::numeric_limits<i32>::max(),
                                std::numeric_limits<i32>::max());
    require(phoneme::graphics::set_clip(
                clipped_context,
                *clipped,
                std::numeric_limits<i32>::max(),
                std::numeric_limits<i32>::max(),
                std::numeric_limits<i32>::max(),
                std::numeric_limits<i32>::max())
                .has_value(),
            "clip and translate saturate without signed overflow");
    require(phoneme::graphics::empty(clipped_context.clip),
            "overflowed translated clip becomes safely empty");
}

void test_self_overlap_copy_and_region() {
    auto image = Image::create_mutable(8, 2);
    require(image.has_value(), "create overlap target");
    phoneme::graphics::GraphicsContext context;
    context.clip = phoneme::graphics::target_bounds(*image);
    // Use distinct values that are exactly representable in RGB565; tiny
    // 0x01 blue increments collapse to the same device pixel by design.
    constexpr std::array<Pixel, 8> row {
        0xFF000008U, 0xFF000010U, 0xFF000018U, 0xFF000021U,
        0xFF000029U, 0xFF000031U, 0xFF000039U, 0xFF000042U,
    };
    require(phoneme::graphics::draw_rgb(*image,
                                        context,
                                        row,
                                        0,
                                        8,
                                        0,
                                        0,
                                        8,
                                        1,
                                        false)
                .has_value(),
            "seed overlap row");
    image->clear_dirty_region();
    require(phoneme::graphics::copy_area(
                *image,
                context,
                0,
                0,
                7,
                1,
                1,
                0,
                phoneme::graphics::anchor_left |
                    phoneme::graphics::anchor_top)
                .has_value(),
            "copyArea supports rightward overlap");
    require(image->pixel(0, 0).value() == row[0],
            "copyArea keeps first source pixel");
    for (i32 column = 1; column < 8; ++column) {
        require(image->pixel(column, 0).value() ==
                    row[static_cast<usize>(column - 1)],
                "copyArea snapshots overlapping source before writes");
    }
    const auto dirty = image->dirty_region();
    require(dirty.x == 1 && dirty.y == 0 &&
                dirty.width == 7 && dirty.height == 1,
            "copyArea dirty region covers changed destination only");

    require(phoneme::graphics::draw_region(
                *image,
                context,
                *image,
                0,
                0,
                4,
                1,
                phoneme::graphics::Transform::mirror,
                4,
                1,
                phoneme::graphics::anchor_left |
                    phoneme::graphics::anchor_top)
                .has_value(),
            "drawRegion snapshots self source with transform");
    require(image->pixel(4, 1).value() == row[2] &&
                image->pixel(5, 1).value() == row[1] &&
                image->pixel(6, 1).value() == row[0] &&
                image->pixel(7, 1).value() == row[0],
            "self drawRegion mirror uses pre-draw pixels");
}

void test_png_variants_limits_and_fuzz() {
    constexpr std::array<u8, 2> grayscale_rows {0U, 0x40U};
    const auto grayscale_png = make_png(2, 1, 1, 0, 0, grayscale_rows);
    auto grayscale = phoneme::graphics::decode_png(grayscale_png);
    require(grayscale.has_value(), "decode one-bit grayscale PNG");
    require(grayscale->pixel(0, 0).value() == 0xFF000000U &&
                grayscale->pixel(1, 0).value() == 0xFFFFFFFFU,
            "one-bit grayscale samples scale to full range");

    constexpr std::array<u8, 2> palette_rows {0U, 0x18U};
    constexpr std::array<u8, 9> palette {
        255U, 0U, 0U,
        0U, 255U, 0U,
        0U, 0U, 255U,
    };
    constexpr std::array<u8, 3> palette_alpha {255U, 128U, 0U};
    const auto palette_png = make_png(3,
                                      1,
                                      2,
                                      3,
                                      0,
                                      palette_rows,
                                      palette,
                                      palette_alpha);
    auto indexed = phoneme::graphics::decode_png(palette_png);
    require(indexed.has_value(), "decode indexed PNG with tRNS");
    require(indexed->pixel(0, 0).value() == 0xFFFF0000U &&
                indexed->pixel(1, 0).value() == 0x8000FF00U &&
                indexed->pixel(2, 0).value() == 0x000000FFU,
            "palette and tRNS preserve straight ARGB");

    constexpr std::array<u8, 2> short_palette_rows {0U, 250U};
    constexpr std::array<u8, 3> short_palette {255U, 255U, 255U};
    const auto short_palette_png = make_png(
        1, 1, 8, 3, 0, short_palette_rows, short_palette);
    auto short_palette_decoded =
        phoneme::graphics::decode_png(short_palette_png);
    require(short_palette_decoded.has_value() &&
                short_palette_decoded->pixel(0, 0).value() == 0xFF000000U,
            "legacy palette indices beyond PLTE decode as opaque black");

    constexpr std::array<u8, 7> rgb_rows {
        0U, 1U, 2U, 3U, 4U, 5U, 6U,
    };
    constexpr std::array<u8, 6> transparent_rgb {
        0U, 1U, 0U, 2U, 0U, 3U,
    };
    const auto rgb_png = make_png(2,
                                  1,
                                  8,
                                  2,
                                  0,
                                  rgb_rows,
                                  {},
                                  transparent_rgb);
    auto truecolor = phoneme::graphics::decode_png(rgb_png);
    require(truecolor.has_value(), "decode truecolor PNG with tRNS");
    require(truecolor->pixel(0, 0).value() == 0x00010203U &&
                truecolor->pixel(1, 0).value() == 0xFF040506U,
            "truecolor tRNS matches exact source sample");

    constexpr std::array<u8, 5> gray_alpha_rows {
        0U, 0x12U, 0x34U, 0x80U, 0x00U,
    };
    const auto gray_alpha_png = make_png(1,
                                         1,
                                         16,
                                         4,
                                         0,
                                         gray_alpha_rows);
    auto gray_alpha = phoneme::graphics::decode_png(gray_alpha_png);
    require(gray_alpha.has_value(), "decode 16-bit grayscale-alpha PNG");
    require(gray_alpha->pixel(0, 0).value() == 0x80121212U,
            "16-bit channels use the high byte consistently");

    constexpr std::array<u8, 19> adam7_rows {
        0U, 255U, 0U, 0U, 255U,
        0U, 0U, 255U, 0U, 255U,
        0U, 0U, 0U, 255U, 255U, 255U, 255U, 255U, 255U,
    };
    const auto adam7_png = make_png(2, 2, 8, 6, 1, adam7_rows);
    auto adam7 = phoneme::graphics::decode_png(adam7_png);
    require(adam7.has_value(), "decode Adam7 interlaced RGBA PNG");
    require(adam7->pixel(0, 0).value() == 0xFFFF0000U &&
                adam7->pixel(1, 0).value() == 0xFF00FF00U &&
                adam7->pixel(0, 1).value() == 0xFF0000FFU &&
                adam7->pixel(1, 1).value() == 0xFFFFFFFFU,
            "Adam7 pass placement reconstructs all pixels");

    auto bad_crc = palette_png;
    bad_crc[bad_crc.size() / 2U] ^= 0x40U;
    require(!phoneme::graphics::decode_png(bad_crc).has_value(),
            "PNG CRC mutation is rejected");

    auto trailing = grayscale_png;
    trailing.push_back(0U);
    trailing.push_back(0x7FU);
    auto trailing_decoded = phoneme::graphics::decode_png(trailing);
    require(trailing_decoded.has_value() &&
                trailing_decoded->pixel(0, 0).value() ==
                    grayscale->pixel(0, 0).value(),
            "legacy alignment data after IEND is ignored");

    auto duplicate_palette = png_prefix(1, 1, 1, 3, 0);
    constexpr std::array<u8, 6> two_colors {
        0U, 0U, 0U, 255U, 255U, 255U,
    };
    append_chunk(duplicate_palette, "PLTE", two_colors);
    append_chunk(duplicate_palette, "PLTE", two_colors);
    constexpr std::array<u8, 2> indexed_row {0U, 0U};
    const auto indexed_compressed = compressed_bytes(indexed_row);
    append_chunk(duplicate_palette, "IDAT", indexed_compressed);
    append_chunk(duplicate_palette, "IEND", {});
    require(!phoneme::graphics::decode_png(duplicate_palette).has_value(),
            "duplicate PLTE is rejected");

    auto split_idat = png_prefix(1, 1, 8, 6, 0);
    constexpr std::array<u8, 5> rgba_row {0U, 1U, 2U, 3U, 255U};
    const auto rgba_compressed = compressed_bytes(rgba_row);
    const usize split = rgba_compressed.size() / 2U;
    append_chunk(split_idat,
                 "IDAT",
                 std::span<const u8>(rgba_compressed).first(split));
    append_chunk(split_idat, "tEXt", {});
    append_chunk(split_idat,
                 "IDAT",
                 std::span<const u8>(rgba_compressed).subspan(split));
    append_chunk(split_idat, "IEND", {});
    require(!phoneme::graphics::decode_png(split_idat).has_value(),
            "non-consecutive IDAT chunks are rejected");

    constexpr std::array<u8, 2> tiny_rows {0U, 0U};
    const auto huge_png = make_png(100'000,
                                   100'000,
                                   1,
                                   0,
                                   0,
                                   tiny_rows);
    auto huge = phoneme::graphics::decode_png(huge_png);
    require(!huge.has_value() &&
                huge.error().code == phoneme::ErrorCode::overflow,
            "PNG dimensions are rejected before large allocation");

    for (usize index = 0; index < palette_png.size(); ++index) {
        auto mutated = palette_png;
        mutated[index] ^= static_cast<u8>(1U << (index % 8U));
        auto decoded = phoneme::graphics::decode_png(mutated);
        if (decoded) {
            const u64 pixels = static_cast<u64>(decoded->width()) *
                               static_cast<u64>(decoded->height());
            require(pixels <= 64ULL * 1024ULL * 1024ULL,
                    "fuzz-success PNG stays within image budget");
        }
    }
    for (usize length = 0; length < palette_png.size(); ++length) {
        auto decoded = phoneme::graphics::decode_png(
            std::span<const u8>(palette_png).first(length));
        require(!decoded.has_value(),
                "all truncated PNG prefixes fail cleanly");
    }
}

void test_jpeg_gif_and_format_dispatch() {
    constexpr std::array<u8, 65> gif {
        0x47U, 0x49U, 0x46U, 0x38U, 0x39U, 0x61U, 0x06U, 0x00U,
        0x09U, 0x00U, 0x91U, 0x03U, 0x00U, 0xAFU, 0x00U, 0x00U,
        0xFFU, 0x76U, 0x76U, 0xF1U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x21U, 0xF9U, 0x04U, 0x01U, 0x00U, 0x00U, 0x03U,
        0x00U, 0x2CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x06U, 0x00U,
        0x09U, 0x00U, 0x00U, 0x02U, 0x12U, 0x4CU, 0x86U, 0x03U,
        0x96U, 0x20U, 0xACU, 0xC4U, 0x3BU, 0x4EU, 0x9AU, 0x0AU,
        0x40U, 0xCCU, 0x28U, 0xEBU, 0xFEU, 0x21U, 0x05U, 0x00U,
        0x3BU,
    };
    auto decoded_gif = phoneme::graphics::decode_image(gif);
    require(decoded_gif.has_value(), "decode GIF through format dispatcher");
    require(decoded_gif->width() == 6 && decoded_gif->height() == 9,
            "GIF metadata dimensions are preserved");
    require(decoded_gif->pixel(0, 0).value() == 0xFFFF7676U,
            "GIF first row is not vertically flipped");
    require((decoded_gif->pixel(2, 0).value() >> 24U) == 0U,
            "GIF transparent palette index preserves alpha");

    constexpr std::string_view jpeg_base64 =
        "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQH/2wBDAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQH/wAARCAACAAIDAREAAhEBAxEB/8QAFAABAAAAAAAAAAAAAAAAAAAACv/EABsQAAIDAQEBAAAAAAAAAAAAAAQFAwYHAggJ/8QAFQEBAQAAAAAAAAAAAAAAAAAABgf/xAAbEQACAwEBAQAAAAAAAAAAAAAEBgMFBwIIAf/aAAwDAQACEQMRAD8AWB84MIw+y/PHwZYrFjWUv7A/8YeXHT166zuoNXLpy1w+jHNGzZocnnNZM2Rs85h55k8xRhU0pBEsk0nffSD1D58wOq9MeiaurxDIK2trd01sCurgM0TAwQAQ39gHECCEHpYxxRBR444BxoI44YIY+IouOeOeefkfPxfHX0416esnzR0dnQslscXFsRFZjamxqY5u7hhZmZhuKoy3vmC9tzDLS5ubQwqxtLEok44mcmeWXr//2Q==";
    const auto jpeg = decode_base64(jpeg_base64);
    auto decoded_jpeg = phoneme::graphics::decode_image(jpeg);
    require(decoded_jpeg.has_value(), "decode JPEG through format dispatcher");
    require(decoded_jpeg->width() == 2 && decoded_jpeg->height() == 2,
            "JPEG metadata dimensions are preserved");
    require_rgb_near(decoded_jpeg->pixel(0, 0).value(),
                     255U, 0U, 0U, 8, "JPEG top-left red orientation");
    require_rgb_near(decoded_jpeg->pixel(1, 0).value(),
                     0U, 255U, 0U, 8, "JPEG top-right green orientation");
    require_rgb_near(decoded_jpeg->pixel(0, 1).value(),
                     0U, 0U, 255U, 8, "JPEG bottom-left blue orientation");
    require_rgb_near(decoded_jpeg->pixel(1, 1).value(),
                     255U, 255U, 255U, 8, "JPEG bottom-right white orientation");

    constexpr std::array<u8, 4> unsupported {'B', 'M', 0U, 0U};
    require(!phoneme::graphics::decode_image(unsupported).has_value(),
            "unsupported raster format is rejected cleanly");
}

void test_font_unicode_and_measurement() {
    auto font = phoneme::graphics::Font::create(
        static_cast<i32>(phoneme::graphics::FontFace::monospace),
        phoneme::graphics::style_bold |
            phoneme::graphics::style_underlined,
        static_cast<i32>(phoneme::graphics::FontSize::small));
    require(font.has_value(), "create MIDP font");
    constexpr std::array<char32_t, 2> text {U'A', U'B'};
    require(font->is_bold() && font->is_underlined() &&
                font->height() > font->baseline() &&
                font->chars_width(text) >= font->char_width(U'A'),
            "font flags and text metrics are internally consistent");
    require_equal(font->height(), 14,
                  "phoneME logical font exposes its 14-pixel cell height");
    require_equal(font->baseline(), 11,
                  "phoneME font.bin exposes its 11-pixel baseline");
    const i32 width_a = font->char_width(U'A');
    const i32 width_b = font->char_width(U'B');
    require(width_a > 0 && width_b > 0 &&
                font->chars_width(text) == width_a + width_b,
            "phoneME bitmap metrics preserve proportional glyph advances");

    auto unicode_font = phoneme::graphics::Font::create(
        static_cast<i32>(phoneme::graphics::FontFace::system),
        phoneme::graphics::style_plain,
        static_cast<i32>(phoneme::graphics::FontSize::medium));
    require(unicode_font.has_value(), "create Unicode system font");
    const i32 narrow_width = unicode_font->char_width(U'I');
    const i32 wide_width = unicode_font->char_width(U'W');
    require(narrow_width > 0 && wide_width > narrow_width,
            "phoneME bitmap metrics do not spread narrow glyphs into fixed cells");
    constexpr std::array<char32_t, 4> narrow_text {U'i', U'i', U'i', U'i'};
    constexpr std::array<char32_t, 4> wide_text {U'W', U'W', U'W', U'W'};
    require(unicode_font->chars_width(narrow_text) <
                unicode_font->chars_width(wide_text),
            "Canvas text keeps proportional spacing across complete strings");
    require(unicode_font->char_width(U'\U0001F642') > 0,
            "supplementary glyph fallback exposes a positive advance");
    constexpr std::u32string_view unicode_text = U"Tiếng Việt 日本語 \U0001F642";
    const std::span<const char32_t> characters(unicode_text.data(),
                                               unicode_text.size());
    auto metrics = phoneme::graphics::platform_font_metrics(*unicode_font);
    auto unicode_width = phoneme::graphics::platform_text_width(
        *unicode_font, characters);
    require(metrics.has_value() && unicode_width.has_value() &&
                *unicode_width > 0,
            "phoneME atlas and CoreText fallback measure mixed Unicode text");
    auto text_image = Image::create_mutable(*unicode_width + 8,
                                             metrics->height + 4);
    require(text_image.has_value(), "create Unicode text target");
    text_image->clear_dirty_region();
    const phoneme::graphics::Rect text_clip =
        phoneme::graphics::target_bounds(*text_image);
    require(phoneme::graphics::draw_platform_text(*text_image,
                                                   *unicode_font,
                                                   characters,
                                                   4,
                                                   2,
                                                   0xFF000000U,
                                                   text_clip)
                .has_value(),
            "phoneME atlas and CoreText fallback rasterize mixed Unicode text");
    usize changed_pixels = 0;
    for (const Pixel pixel : text_image->pixels()) {
        if (pixel != 0xFFFFFFFFU) {
            ++changed_pixels;
        }
    }
    require(changed_pixels > 20U,
            "Unicode rasterizer emits visible anti-aliased glyph pixels");
    require(text_image->has_dirty_region(),
            "text rasterization contributes a bounded dirty region");

    constexpr std::u32string_view vietnamese_nfc = U"Tiếng Việt";
    constexpr std::u32string_view vietnamese_nfd =
        U"Tie\u0302\u0301ng Vie\u0323\u0302t";
    const std::span<const char32_t> nfc_characters(vietnamese_nfc.data(),
                                                   vietnamese_nfc.size());
    const std::span<const char32_t> nfd_characters(vietnamese_nfd.data(),
                                                   vietnamese_nfd.size());
    const auto nfc_width = phoneme::graphics::platform_text_width(
        *unicode_font, nfc_characters);
    const auto nfd_width = phoneme::graphics::platform_text_width(
        *unicode_font, nfd_characters);
    require(nfc_width.has_value() && nfd_width == nfc_width,
            "decomposed Vietnamese normalizes to the bitmap atlas width");

    auto nfc_image = Image::create_mutable(*nfc_width + 4, metrics->height);
    auto nfd_image = Image::create_mutable(*nfd_width + 4, metrics->height);
    require(nfc_image.has_value() && nfd_image.has_value(),
            "create Vietnamese normalization targets");
    const auto nfc_clip = phoneme::graphics::target_bounds(*nfc_image);
    const auto nfd_clip = phoneme::graphics::target_bounds(*nfd_image);
    require(phoneme::graphics::draw_platform_text(*nfc_image,
                                                   *unicode_font,
                                                   nfc_characters,
                                                   2,
                                                   0,
                                                   0xFF000000U,
                                                   nfc_clip)
                .has_value() &&
                phoneme::graphics::draw_platform_text(*nfd_image,
                                                       *unicode_font,
                                                       nfd_characters,
                                                       2,
                                                       0,
                                                       0xFF000000U,
                                                       nfd_clip)
                    .has_value(),
            "rasterize precomposed and decomposed Vietnamese");
    require(std::equal(nfc_image->pixels().begin(),
                       nfc_image->pixels().end(),
                       nfd_image->pixels().begin(),
                       nfd_image->pixels().end()),
            "Vietnamese combining marks preserve the crisp bitmap glyphs");

    constexpr std::u32string_view mixed_bitmap_text = U"Tiếng Việt ⊤";
    const std::span<const char32_t> mixed_bitmap_characters(
        mixed_bitmap_text.data(), mixed_bitmap_text.size());
    const auto mixed_bitmap_width = phoneme::graphics::platform_text_width(
        *unicode_font, mixed_bitmap_characters);
    require(mixed_bitmap_width.has_value(),
            "measure mixed bitmap and fallback text runs");
    auto mixed_bitmap_image = Image::create_mutable(*mixed_bitmap_width + 4,
                                                     metrics->height);
    require(mixed_bitmap_image.has_value(),
            "create mixed bitmap and fallback target");
    require(phoneme::graphics::draw_platform_text(
                *mixed_bitmap_image,
                *unicode_font,
                mixed_bitmap_characters,
                2,
                0,
                0xFF000000U,
                phoneme::graphics::target_bounds(*mixed_bitmap_image))
                .has_value(),
            "rasterize mixed bitmap and fallback text runs");
    usize bitmap_ink = 0U;
    usize bitmap_gray = 0U;
    for (i32 row = 0; row < mixed_bitmap_image->height(); ++row) {
        for (i32 column = 2; column < 2 + *nfc_width; ++column) {
            const Pixel pixel = mixed_bitmap_image->pixel(column, row).value();
            if (pixel == 0xFF000000U) {
                ++bitmap_ink;
            } else if (pixel != 0xFFFFFFFFU) {
                ++bitmap_gray;
            }
        }
    }
    require(bitmap_ink > 20U && bitmap_gray == 0U,
            "unsupported glyphs do not downgrade Vietnamese to CoreText");

    constexpr std::array<char32_t, 1> orientation_text {U'T'};
    auto orientation_font = phoneme::graphics::Font::create(
        static_cast<i32>(phoneme::graphics::FontFace::monospace),
        phoneme::graphics::style_bold,
        static_cast<i32>(phoneme::graphics::FontSize::large));
    require(orientation_font.has_value(),
            "create asymmetric glyph orientation font");
    auto orientation_metrics =
        phoneme::graphics::platform_font_metrics(*orientation_font);
    auto orientation_width = phoneme::graphics::platform_text_width(
        *orientation_font, orientation_text);
    require(orientation_metrics.has_value() && orientation_width.has_value(),
            "measure asymmetric glyph orientation fixture");
    auto orientation_image = Image::create_mutable(
        *orientation_width + 4, orientation_metrics->height);
    require(orientation_image.has_value(),
            "create asymmetric glyph orientation target");
    require(phoneme::graphics::draw_platform_text(
                *orientation_image,
                *orientation_font,
                orientation_text,
                2,
                0,
                0xFF000000U,
                phoneme::graphics::target_bounds(*orientation_image))
                .has_value(),
            "rasterize asymmetric glyph orientation fixture");
    usize widest_row = 0U;
    usize widest_ink = 0U;
    for (i32 row = 0; row < orientation_image->height(); ++row) {
        usize row_ink = 0U;
        for (i32 column = 0; column < orientation_image->width(); ++column) {
            if (orientation_image->pixel(column, row).value() != 0xFFFFFFFFU) {
                ++row_ink;
            }
        }
        if (row_ink > widest_ink) {
            widest_ink = row_ink;
            widest_row = static_cast<usize>(row);
        }
    }
    require(widest_ink > 0U &&
                widest_row < static_cast<usize>(orientation_image->height() / 2),
            "phoneME bitmap glyph rows preserve top-down Canvas orientation");

    constexpr std::array<char32_t, 1> fallback_orientation_text {U'⊤'};
    auto fallback_orientation_width = phoneme::graphics::platform_text_width(
        *unicode_font, fallback_orientation_text);
    require(fallback_orientation_width.has_value(),
            "measure asymmetric CoreText fallback glyph");
    auto fallback_orientation_image = Image::create_mutable(
        *fallback_orientation_width + 4, metrics->height);
    require(fallback_orientation_image.has_value(),
            "create CoreText fallback orientation target");
    require(phoneme::graphics::draw_platform_text(
                *fallback_orientation_image,
                *unicode_font,
                fallback_orientation_text,
                2,
                0,
                0xFF000000U,
                phoneme::graphics::target_bounds(*fallback_orientation_image))
                .has_value(),
            "rasterize asymmetric CoreText fallback glyph");
    usize fallback_widest_row = 0U;
    usize fallback_widest_ink = 0U;
    for (i32 row = 0; row < fallback_orientation_image->height(); ++row) {
        usize row_ink = 0U;
        for (i32 column = 0;
             column < fallback_orientation_image->width();
             ++column) {
            if (fallback_orientation_image->pixel(column, row).value() !=
                0xFFFFFFFFU) {
                ++row_ink;
            }
        }
        if (row_ink > fallback_widest_ink) {
            fallback_widest_ink = row_ink;
            fallback_widest_row = static_cast<usize>(row);
        }
    }
    require(fallback_widest_ink > 0U &&
                fallback_widest_row < static_cast<usize>(
                    fallback_orientation_image->height() / 2),
            "CoreText fallback glyph rows preserve top-down Canvas orientation");

    auto narrow_target = Image::create_mutable(32, metrics->height + 2);
    require(narrow_target.has_value(), "create clipped text target");
    const phoneme::graphics::Rect narrow_clip =
        phoneme::graphics::target_bounds(*narrow_target);
    require(phoneme::graphics::draw_platform_text(*narrow_target,
                                                   *unicode_font,
                                                   characters,
                                                   -10'000,
                                                   1,
                                                   0xFF000000U,
                                                   narrow_clip)
                .has_value(),
            "far-clipped text avoids full measured-width mask allocation");
}

void test_dirty_update_contract() {
    phoneme::graphics::GraphicsStore store;
    auto image = Image::create_mutable(8, 8);
    require(image.has_value(), "create dirty update image");
    require(store.attach_image(101U, std::move(*image)).has_value(),
            "attach dirty update image");
    require(store.attach_context(202U, 101U, true).has_value(),
            "attach display graphics context");
    auto display_context = store.context(202U);
    require(display_context.has_value() && (*display_context)->display_target,
            "graphics store preserves display-target semantics");

    auto initial = store.consume_dirty_update(101U);
    require(initial.has_value() && initial->has_value(),
            "initial mutable image publishes one full dirty update");
    require_equal((*initial)->region.x, 0, "initial dirty update x");
    require_equal((*initial)->region.y, 0, "initial dirty update y");
    require_equal((*initial)->region.width, 8, "initial dirty update width");
    require_equal((*initial)->region.height, 8, "initial dirty update height");
    require_equal((*initial)->pixels.size(), static_cast<usize>(64),
                  "initial dirty update packs only covered pixels");

    auto empty_update = store.consume_dirty_update(101U);
    require(empty_update.has_value() && !empty_update->has_value(),
            "consuming a dirty update clears its generation");

    auto target = store.image(101U);
    require(target.has_value(), "lookup dirty update image");
    phoneme::graphics::GraphicsContext context;
    context.clip = phoneme::graphics::target_bounds(**target);
    context.color = 0xFF123456U;
    require(phoneme::graphics::fill_rect(**target, context, 2, 3, 2, 2)
                .has_value(),
            "draw bounded dirty rectangle");

    auto update = store.consume_dirty_update(101U);
    require(update.has_value() && update->has_value(),
            "consume bounded dirty rectangle");
    require_equal((*update)->image_width, 8, "dirty update image width");
    require_equal((*update)->image_height, 8, "dirty update image height");
    require_equal((*update)->region.x, 2, "dirty update x");
    require_equal((*update)->region.y, 3, "dirty update y");
    require_equal((*update)->region.width, 2, "dirty update width");
    require_equal((*update)->region.height, 2, "dirty update height");
    require_equal((*update)->pixels.size(), static_cast<usize>(4),
                  "dirty update excludes unchanged framebuffer pixels");
    for (const Pixel pixel : (*update)->pixels) {
        require_equal(pixel, static_cast<Pixel>(0xFF103452U),
                      "dirty update preserves packed RGB565-expanded pixels");
    }
}

void test_graphics_store_lookup_cache_invalidation() {
    phoneme::graphics::GraphicsStore store;

    auto first = Image::create_mutable(4, 4);
    require(first.has_value(), "create lookup-cache image");
    require(store.attach_image(401U, std::move(*first)).has_value(),
            "attach lookup-cache image");
    require(store.attach_context(501U, 401U, false).has_value(),
            "attach lookup-cache context");
    require(store.image(401U).has_value(),
            "prime image lookup cache");
    require(store.context(501U).has_value(),
            "prime context lookup cache");

    store.erase_image(401U);
    require(!store.image(401U).has_value(),
            "erase invalidates cached image lookup");
    require(!store.context(501U).has_value(),
            "erase image invalidates cached target context lookup");

    auto second = Image::create_mutable(4, 4);
    require(second.has_value(), "create prune lookup-cache image");
    require(store.attach_image(402U, std::move(*second)).has_value(),
            "attach prune lookup-cache image");
    require(store.attach_context(502U, 402U, false).has_value(),
            "attach prune lookup-cache context");
    require(store.image(402U).has_value() && store.context(502U).has_value(),
            "prime prune lookup caches");
    store.prune([](u64 key) { return key == 402U; });
    require(store.image(402U).has_value(),
            "prune preserves live cached image");
    require(!store.context(502U).has_value(),
            "prune invalidates dead cached context");

    require(store.attach_context(503U, 402U, false).has_value(),
            "reattach context before clear");
    require(store.context(503U).has_value(),
            "prime clear lookup cache");
    store.clear();
    require(!store.image(402U).has_value(),
            "clear invalidates cached image lookup");
    require(!store.context(503U).has_value(),
            "clear invalidates cached context lookup");
}

void test_graphics_allocation_requests_gc() {
    phoneme::graphics::GraphicsStore store;
    const usize baseline_bytes = store.estimated_bytes();
    require(!store.automatic_collection_due(),
            "fresh graphics store does not request collection");

    auto image = Image::create_mutable(2048, 2048);
    require(image.has_value(), "create graphics-GC threshold image");
    require(store.attach_image(303U, std::move(*image)).has_value(),
            "attach graphics-GC threshold image");
    require(store.automatic_collection_due(),
            "16 MiB of native image allocation requests Java GC pruning");
    require(store.estimated_bytes() >= baseline_bytes + 16U * 1024U * 1024U,
            "graphics store accounts retained native image bytes");

    store.prune([](u64) { return true; });
    require(!store.automatic_collection_due(),
            "graphics allocation counter resets after pruning");
    store.erase_image(303U);
    require_equal(store.estimated_bytes(), baseline_bytes,
                  "graphics accounting releases erased native image bytes");
}

void test_hidden_render_suppression() {
    auto target = Image::create_mutable(16, 16);
    const std::array<Pixel, 4> source_pixels {
        0xFFFF0000U, 0xFF00FF00U,
        0xFF0000FFU, 0xFFFFFFFFU,
    };
    auto source = Image::create_immutable(2, 2, source_pixels);
    require(target.has_value() && source.has_value(),
            "create hidden-render suppression images");
    target->clear_dirty_region();
    const std::vector<Pixel> before(target->pixels().begin(),
                                    target->pixels().end());

    phoneme::graphics::GraphicsContext context;
    context.clip = phoneme::graphics::target_bounds(*target);
    context.color = 0xFF123456U;
    context.rendering_enabled = false;
    require(phoneme::graphics::fill_rect(*target, context, 0, 0, 16, 16)
                .has_value(),
            "hidden fillRect is accepted as a no-op");
    require(phoneme::graphics::draw_image(
                *target, context, *source, 2, 2,
                phoneme::graphics::anchor_left |
                    phoneme::graphics::anchor_top)
                .has_value(),
            "hidden drawImage is accepted as a no-op");
    require(phoneme::graphics::draw_rgb(
                *target, context, source_pixels, 0, 2,
                4, 4, 2, 2, true)
                .has_value(),
            "hidden drawRGB is accepted as a no-op");
    constexpr std::array<char32_t, 4> text {U's', U'k', U'i', U'p'};
    require(phoneme::graphics::draw_text(
                *target, context, text, 0, 0,
                phoneme::graphics::anchor_left |
                    phoneme::graphics::anchor_top)
                .has_value(),
            "hidden text draw is accepted as a no-op");
    require(std::equal(before.begin(), before.end(), target->pixels().begin()),
            "hidden rendering leaves the backbuffer unchanged");
    require(!target->has_dirty_region(),
            "hidden rendering does not create dirty image work");

    context.rendering_enabled = true;
    require(phoneme::graphics::fill_rect(*target, context, 0, 0, 1, 1)
                .has_value(),
            "foreground rendering is restored");
    require(target->has_dirty_region(),
            "foreground rendering marks the backbuffer dirty again");
}

void test_sprite_heavy_benchmark() {
    constexpr std::array<Pixel, 16> sprite_pixels {
        0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU, 0xFFFFFFFFU,
        0x80000000U, 0x80FFFFFFU, 0xFF123456U, 0xFF654321U,
        0xFF102030U, 0xFF405060U, 0xFF708090U, 0xFFA0B0C0U,
        0xFF010203U, 0xFF040506U, 0xFF070809U, 0xFF0A0B0CU,
    };
    auto sprite = Image::create_immutable(4, 4, sprite_pixels);
    auto framebuffer = Image::create_mutable(320, 240);
    require(sprite.has_value() && framebuffer.has_value(),
            "create sprite benchmark images");
    phoneme::graphics::GraphicsContext context;
    context.clip = phoneme::graphics::target_bounds(*framebuffer);

    constexpr i32 kDrawCalls = 10'000;
    const auto draw_workload = [&]() {
        for (i32 index = 0; index < kDrawCalls; ++index) {
            const i32 x = (index * 37) % 317;
            const i32 y = (index * 53) % 237;
            require(phoneme::graphics::draw_image(
                        *framebuffer,
                        context,
                        *sprite,
                        x,
                        y,
                        phoneme::graphics::anchor_left |
                            phoneme::graphics::anchor_top)
                        .has_value(),
                    "sprite benchmark draw call");
        }
    };

    draw_workload();
    std::array<i64, 5> samples_us {};
    for (i64& sample : samples_us) {
        const auto start = std::chrono::steady_clock::now();
        draw_workload();
        sample = std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();
    }
    std::sort(samples_us.begin(), samples_us.end());
    const i64 median_us = samples_us[samples_us.size() / 2U];
    const i64 slowest_us = samples_us.back();
    constexpr i64 kSixtyFpsFrameBudgetUs = 16'667;
    constexpr i64 kHostRegressionCeilingUs = 250'000;
    const double calls_per_second =
        static_cast<double>(kDrawCalls) * 1'000'000.0 /
        static_cast<double>(std::max<i64>(median_us, 1));

    std::cout << "graphics_benchmark draws=" << kDrawCalls
              << " median_us=" << median_us
              << " slowest_us=" << slowest_us
              << " calls_per_second=" << static_cast<u64>(calls_per_second)
              << " core_only_60fps_budget="
              << (median_us <= kSixtyFpsFrameBudgetUs ? "pass" : "miss")
              << " host_regression_only=true"
              << " device_60fps_gate_required=true\n";
    require(median_us < kHostRegressionCeilingUs,
            "sprite benchmark avoids catastrophic full-image-copy regression");
}

} // namespace

int main() {
    test_clip_translate_alpha_and_dirty_region();
    test_anchor_matrix_and_transform();
    test_primitive_golden_and_overflow_clipping();
    test_self_overlap_copy_and_region();
    test_png_variants_limits_and_fuzz();
    test_jpeg_gif_and_format_dispatch();
    test_font_unicode_and_measurement();
    test_dirty_update_contract();
    test_graphics_store_lookup_cache_invalidation();
    test_graphics_allocation_requests_gc();
    test_hidden_render_suppression();
    test_sprite_heavy_benchmark();
    std::cout << "Graphics module tests passed\n";
    return 0;
}
