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
    // Keep the 14-pixel LCDUI line box while glyph advances remain
    // proportional to the bundled phoneME bitmap font.
    return 14;
}

i32 Font::baseline() const noexcept {
    return 11;
}

i32 Font::char_width(char32_t character) const noexcept {
    const std::span<const char32_t> glyph(&character, 1U);
    if (auto width = platform_text_width(*this, glyph)) {
        return *width;
    }
    return character > 0xFFFF ? 18 : 9;
}

i32 Font::chars_width(std::span<const char32_t> characters) const noexcept {
    if (auto width = platform_text_width(*this, characters)) {
        return *width;
    }

    i64 code_units = 0;
    for (const char32_t character : characters) {
        code_units += character > 0xFFFF ? 2 : 1;
        if (code_units > std::numeric_limits<i32>::max() / 9) {
            return std::numeric_limits<i32>::max();
        }
    }
    return static_cast<i32>(code_units * 9);
}

} // namespace phoneme::graphics
