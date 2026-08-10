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
    // Preserve the high RGB565 bits in-place and replicate their MSBs into the
    // discarded low bits. Besides being equivalent to the component-wise form,
    // this maps to a handful of bitwise WASM operations and vectorizes cleanly
    // with SIMD128 in full-row blits.
    return (pixel & 0xFFF8FCF8U) |
           ((pixel & 0x00E00000U) >> 5U) |
           ((pixel & 0x0000C000U) >> 6U) |
           ((pixel & 0x000000E0U) >> 5U);
}

[[nodiscard]] inline Pixel source_over(Pixel source,
                                       Pixel destination) noexcept {
    const u32 source_alpha = alpha(source);
    if (source_alpha == 0U) return destination;
    if (source_alpha == 255U) return source;

    const u32 destination_alpha = alpha(destination);
    const u32 inverse_source = 255U - source_alpha;
    const u32 output_alpha = source_alpha +
        ((destination_alpha * inverse_source + 127U) / 255U);
    if (output_alpha == 0U) return 0U;

    const auto composite = [&](u32 source_component,
                               u32 destination_component) -> u8 {
        const u32 source_premultiplied = source_component * source_alpha;
        const u32 destination_premultiplied =
            (destination_component * destination_alpha * inverse_source +
             127U) /
            255U;
        return static_cast<u8>((source_premultiplied +
                                destination_premultiplied +
                                output_alpha / 2U) /
                               output_alpha);
    };

    return argb(static_cast<u8>(output_alpha),
                composite(red(source), red(destination)),
                composite(green(source), green(destination)),
                composite(blue(source), blue(destination)));
}

} // namespace phoneme::graphics
