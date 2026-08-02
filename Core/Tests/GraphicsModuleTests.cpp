#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include "phoneme/graphics/Graphics.hpp"
#include "phoneme/graphics/PngDecoder.hpp"
#include "phoneme/graphics/TextRasterizer.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void test_clip_translate_and_alpha() {
    auto image = phoneme::graphics::Image::create_mutable(8, 8);
    require(image.has_value(), "create mutable image");

    phoneme::graphics::GraphicsContext context;
    context.target_key = 1U;
    context.clip = phoneme::graphics::target_bounds(*image);
    context.color = 0xFF112233U;
    require(phoneme::graphics::fill_rect(*image, context, 0, 0, 8, 8)
                .has_value(),
            "fill image background");

    require(phoneme::graphics::set_clip(context, *image, 1, 1, 4, 4)
                .has_value(),
            "set graphics clip");
    phoneme::graphics::translate(context, 1, 1);
    context.color = 0xFFFF0000U;
    require(phoneme::graphics::fill_rect(*image, context, 0, 0, 4, 4)
                .has_value(),
            "fill translated clipped rectangle");
    require(image->pixel(0, 0).value() == 0xFF112233U,
            "clip preserves outside pixel");
    require(image->pixel(1, 1).value() == 0xFFFF0000U,
            "translate moves draw origin");
    require(image->pixel(4, 4).value() == 0xFFFF0000U,
            "clip includes final covered pixel");
    require(image->pixel(5, 5).value() == 0xFF112233U,
            "clip excludes following pixel");

    auto alpha_image = phoneme::graphics::Image::create_mutable(1, 1);
    require(alpha_image.has_value(), "create alpha target");
    phoneme::graphics::GraphicsContext alpha_context;
    alpha_context.target_key = 2U;
    alpha_context.clip = phoneme::graphics::target_bounds(*alpha_image);
    constexpr std::array<phoneme::graphics::Pixel, 1> source {0x800000FFU};
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
    require(alpha_image->pixel(0, 0).value() == 0xFF7F7FFFU,
            "source-over alpha matches MIDP ARGB expectation");
}

void test_transform_and_anchor() {
    constexpr std::array<phoneme::graphics::Pixel, 4> pixels {
        0xFFFF0000U, 0xFF00FF00U,
        0xFF0000FFU, 0xFFFFFFFFU,
    };
    auto source = phoneme::graphics::Image::create_immutable(2, 2, pixels);
    require(source.has_value(), "create immutable source image");
    auto rotated = phoneme::graphics::Image::transformed_region(
        *source, 0, 0, 2, 2, phoneme::graphics::Transform::rotate_90);
    require(rotated.has_value(), "rotate image region");
    require(rotated->pixel(0, 0).value() == 0xFF0000FFU &&
                rotated->pixel(1, 0).value() == 0xFFFF0000U &&
                rotated->pixel(0, 1).value() == 0xFFFFFFFFU &&
                rotated->pixel(1, 1).value() == 0xFF00FF00U,
            "ROT90 maps source pixels exactly");

    auto target = phoneme::graphics::Image::create_mutable(4, 4);
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
}

void test_png_and_font() {
    constexpr std::array<phoneme::u8, 71> png {
        137, 80, 78, 71, 13, 10, 26, 10,
        0, 0, 0, 13, 73, 72, 68, 82,
        0, 0, 0, 2, 0, 0, 0, 1,
        8, 6, 0, 0, 0, 244, 34, 127, 138,
        0, 0, 0, 14, 73, 68, 65, 84,
        120, 156, 99, 248, 207, 192, 0, 66,
        13, 0, 15, 122, 3, 126, 119, 233,
        127, 151, 0, 0, 0, 0, 73, 69,
        78, 68, 174, 66, 96, 130,
    };
    auto decoded = phoneme::graphics::decode_png(png);
    require(decoded.has_value(), "decode RGBA PNG");
    require(decoded->width() == 2 && decoded->height() == 1 &&
                !decoded->is_mutable(),
            "decoded PNG is immutable and preserves dimensions");
    require(decoded->pixel(0, 0).value() == 0xFFFF0000U &&
                decoded->pixel(1, 0).value() == 0x800000FFU,
            "PNG decoder preserves straight alpha ARGB pixels");

    auto font = phoneme::graphics::Font::create(
        static_cast<phoneme::i32>(phoneme::graphics::FontFace::monospace),
        phoneme::graphics::style_bold |
            phoneme::graphics::style_underlined,
        static_cast<phoneme::i32>(phoneme::graphics::FontSize::small));
    require(font.has_value(), "create MIDP font");
    constexpr std::array<char32_t, 2> text {U'A', U'B'};
    require(font->is_bold() && font->is_underlined() &&
                font->height() > font->baseline() &&
                font->chars_width(text) >= font->char_width(U'A'),
            "font flags and text metrics are internally consistent");

    auto unicode_font = phoneme::graphics::Font::create(
        static_cast<phoneme::i32>(phoneme::graphics::FontFace::system),
        phoneme::graphics::style_plain,
        static_cast<phoneme::i32>(phoneme::graphics::FontSize::medium));
    require(unicode_font.has_value(), "create Unicode system font");
    constexpr std::u32string_view unicode_text = U"Tiếng Việt 日本語";
    const std::span<const char32_t> characters(unicode_text.data(),
                                               unicode_text.size());
    auto metrics = phoneme::graphics::platform_font_metrics(*unicode_font);
    auto unicode_width = phoneme::graphics::platform_text_width(
        *unicode_font, characters);
    require(metrics.has_value() && unicode_width.has_value() &&
                *unicode_width > 0,
            "CoreText measures Vietnamese and Japanese text");
    auto text_image = phoneme::graphics::Image::create_mutable(
        *unicode_width + 8, metrics->height + 4);
    require(text_image.has_value(), "create Unicode text target");
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
            "CoreText rasterizes Vietnamese and Japanese glyphs");
    phoneme::usize changed_pixels = 0;
    for (const phoneme::graphics::Pixel pixel : text_image->pixels()) {
        if (pixel != 0xFFFFFFFFU) {
            ++changed_pixels;
        }
    }
    require(changed_pixels > 20U,
            "Unicode rasterizer emits visible anti-aliased glyph pixels");
}

} // namespace

int main() {
    test_clip_translate_and_alpha();
    test_transform_and_anchor();
    test_png_and_font();
    std::cout << "Graphics module tests passed\n";
    return 0;
}
