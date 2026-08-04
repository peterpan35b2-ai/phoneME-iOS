#include "phoneme/graphics/Font.hpp"

#include <algorithm>
#include <limits>

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
    // phoneME's reference gx_putpixel font exposes a fixed 9x14 logical cell.
    // Face/style/size are metadata only and must not change LCDUI layout.
    return 14;
}

i32 Font::baseline() const noexcept {
    return 11;
}

i32 Font::char_width(char32_t character) const noexcept {
    // MIDP measures UTF-16 code units; supplementary characters occupy two
    // fixed cells in the reference implementation.
    return character > 0xFFFF ? 18 : 9;
}

i32 Font::chars_width(std::span<const char32_t> characters) const noexcept {
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
