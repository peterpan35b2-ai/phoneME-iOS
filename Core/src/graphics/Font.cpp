#include "phoneme/graphics/Font.hpp"

#include <algorithm>
#include <limits>

#include "phoneme/graphics/TextRasterizer.hpp"

namespace phoneme::graphics {

Result<Font> Font::create(i32 face, i32 style, i32 size) {
    if (face != static_cast<i32>(FontFace::system) &&
        face != static_cast<i32>(FontFace::monospace) &&
        face != static_cast<i32>(FontFace::proportional)) {
        return fail(ErrorCode::invalid_argument, "invalid MIDP font face");
    }
    if (style < 0 || (style & ~(style_bold | style_italic |
                               style_underlined)) != 0) {
        return fail(ErrorCode::invalid_argument, "invalid MIDP font style");
    }
    if (size != static_cast<i32>(FontSize::medium) &&
        size != static_cast<i32>(FontSize::small) &&
        size != static_cast<i32>(FontSize::large)) {
        return fail(ErrorCode::invalid_argument, "invalid MIDP font size");
    }
    return Font(static_cast<FontFace>(face),
                style,
                static_cast<FontSize>(size));
}

Font Font::default_font() noexcept {
    return Font(FontFace::system, style_plain, FontSize::medium);
}

i32 Font::height() const noexcept {
    // Match the bundled phoneME font.bin metrics used by the original C port.
    return 13;
}

i32 Font::baseline() const noexcept {
    return 11;
}

i32 Font::char_width(char32_t character) const noexcept {
    const std::span<const char32_t> glyph(&character, 1U);
    if (auto width = platform_text_width(*this, glyph)) {
        return *width;
    }
    return 9;
}

i32 Font::chars_width(std::span<const char32_t> characters) const noexcept {
    if (auto width = platform_text_width(*this, characters)) {
        return *width;
    }
    constexpr i64 fallback_glyph_width = 9;
    if (characters.size() > static_cast<usize>(
            std::numeric_limits<i32>::max() / fallback_glyph_width)) {
        return std::numeric_limits<i32>::max();
    }
    return static_cast<i32>(characters.size() *
                            static_cast<usize>(fallback_glyph_width));
}

} // namespace phoneme::graphics
