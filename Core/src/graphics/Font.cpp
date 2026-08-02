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
    if (auto metrics = platform_font_metrics(*this)) {
        return metrics->height;
    }
    switch (size_) {
    case FontSize::small:
        return 12;
    case FontSize::large:
        return 20;
    case FontSize::medium:
        return 16;
    }
    return 16;
}

i32 Font::baseline() const noexcept {
    if (auto metrics = platform_font_metrics(*this)) {
        return metrics->baseline;
    }
    return height() - std::max(2, height() / 5);
}

i32 Font::char_width(char32_t character) const noexcept {
    const char32_t single[] {character};
    if (auto width = platform_text_width(*this, single)) {
        return *width;
    }
    i32 base = 8;
    switch (size_) {
    case FontSize::small:
        base = 6;
        break;
    case FontSize::large:
        base = 10;
        break;
    case FontSize::medium:
        base = 8;
        break;
    }
    if (face_ == FontFace::monospace) {
        return base + (is_bold() ? 1 : 0);
    }
    if (character == U' ' || character == U'\t') {
        return std::max(2, base / 2);
    }
    if (character == U'i' || character == U'l' || character == U'I' ||
        character == U'!' || character == U'.' || character == U',' ||
        character == U':' || character == U';' || character == U'|') {
        return std::max(2, base / 2) + (is_bold() ? 1 : 0);
    }
    if (character == U'm' || character == U'w' || character == U'M' ||
        character == U'W' || character >= 0x2E80U) {
        return base + std::max(1, base / 4) + (is_bold() ? 1 : 0);
    }
    return base + (is_bold() ? 1 : 0);
}

i32 Font::chars_width(std::span<const char32_t> characters) const noexcept {
    if (auto width = platform_text_width(*this, characters)) {
        return *width;
    }
    i64 total = 0;
    for (char32_t character : characters) {
        total += char_width(character);
        if (total > std::numeric_limits<i32>::max()) {
            return std::numeric_limits<i32>::max();
        }
    }
    return static_cast<i32>(total);
}

} // namespace phoneme::graphics
