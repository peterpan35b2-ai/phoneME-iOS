#pragma once

#include <optional>
#include <span>

#include "phoneme/graphics/Font.hpp"
#include "phoneme/graphics/Image.hpp"

namespace phoneme::graphics {

struct Rect;

struct PlatformFontMetrics final {
    i32 height {0};
    i32 baseline {0};
};

[[nodiscard]] std::optional<PlatformFontMetrics> platform_font_metrics(
    const Font& font) noexcept;
[[nodiscard]] std::optional<i32> platform_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept;
[[nodiscard]] Status draw_platform_text(
    Image& target,
    const Font& font,
    std::span<const char32_t> text,
    i32 x,
    i32 top,
    Pixel color,
    const Rect& clip);

} // namespace phoneme::graphics
