#pragma once

#include <span>

#include "phoneme/base/Error.hpp"

namespace phoneme::graphics {

enum class FontFace : i32 {
    system = 0,
    monospace = 32,
    proportional = 64,
};

enum FontStyle : i32 {
    style_plain = 0,
    style_bold = 1,
    style_italic = 2,
    style_underlined = 4,
};

enum class FontSize : i32 {
    medium = 0,
    small = 8,
    large = 16,
};

class Font final {
public:
    [[nodiscard]] static Result<Font> create(i32 face,
                                              i32 style,
                                              i32 size);
    [[nodiscard]] static Font default_font() noexcept;

    [[nodiscard]] i32 face() const noexcept {
        return static_cast<i32>(face_);
    }
    [[nodiscard]] i32 style() const noexcept { return style_; }
    [[nodiscard]] i32 size() const noexcept {
        return static_cast<i32>(size_);
    }
    [[nodiscard]] bool is_plain() const noexcept { return style_ == 0; }
    [[nodiscard]] bool is_bold() const noexcept {
        return (style_ & style_bold) != 0;
    }
    [[nodiscard]] bool is_italic() const noexcept {
        return (style_ & style_italic) != 0;
    }
    [[nodiscard]] bool is_underlined() const noexcept {
        return (style_ & style_underlined) != 0;
    }

    [[nodiscard]] i32 height() const noexcept;
    [[nodiscard]] i32 baseline() const noexcept;
    [[nodiscard]] i32 char_width(char32_t character) const noexcept;
    [[nodiscard]] i32 chars_width(
        std::span<const char32_t> characters) const noexcept;

private:
    Font(FontFace face, i32 style, FontSize size) noexcept
        : face_(face), style_(style), size_(size) {}

    FontFace face_ {FontFace::system};
    i32 style_ {style_plain};
    FontSize size_ {FontSize::medium};
};

} // namespace phoneme::graphics
