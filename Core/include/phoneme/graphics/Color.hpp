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

[[nodiscard]] Pixel source_over(Pixel source, Pixel destination) noexcept;

} // namespace phoneme::graphics
