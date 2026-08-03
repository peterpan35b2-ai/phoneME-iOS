#pragma once

#include "phoneme/base/Types.hpp"

namespace phoneme::graphics {

using Pixel = u32;

[[nodiscard]] constexpr Pixel argb(u8 alpha,
                                   u8 red,
                                   u8 green,
                                   u8 blue) noexcept {
    return (static_cast<Pixel>(alpha) << 24U) |
           (static_cast<Pixel>(red) << 16U) |
           (static_cast<Pixel>(green) << 8U) |
           static_cast<Pixel>(blue);
}

[[nodiscard]] constexpr u8 alpha(Pixel pixel) noexcept {
    return static_cast<u8>((pixel >> 24U) & 0xFFU);
}

[[nodiscard]] constexpr u8 red(Pixel pixel) noexcept {
    return static_cast<u8>((pixel >> 16U) & 0xFFU);
}

[[nodiscard]] constexpr u8 green(Pixel pixel) noexcept {
    return static_cast<u8>((pixel >> 8U) & 0xFFU);
}

[[nodiscard]] constexpr u8 blue(Pixel pixel) noexcept {
    return static_cast<u8>(pixel & 0xFFU);
}

[[nodiscard]] constexpr Pixel opaque(Pixel pixel) noexcept {
    return pixel | 0xFF000000U;
}

// phoneME's gxj display and image buffers use RGB565 with a separate alpha
// plane. Convert through that representation when publishing the emulated LCD
// so host RGBA output matches the colors Java ME applications actually saw.
[[nodiscard]] constexpr Pixel rgb565_roundtrip(Pixel pixel) noexcept {
    const u8 red5 = static_cast<u8>(red(pixel) >> 3U);
    const u8 green6 = static_cast<u8>(green(pixel) >> 2U);
    const u8 blue5 = static_cast<u8>(blue(pixel) >> 3U);
    const u8 expanded_red = static_cast<u8>((red5 << 3U) | (red5 >> 2U));
    const u8 expanded_green = static_cast<u8>((green6 << 2U) | (green6 >> 4U));
    const u8 expanded_blue = static_cast<u8>((blue5 << 3U) | (blue5 >> 2U));
    return argb(alpha(pixel), expanded_red, expanded_green, expanded_blue);
}

[[nodiscard]] Pixel source_over(Pixel source, Pixel destination) noexcept;

} // namespace phoneme::graphics
