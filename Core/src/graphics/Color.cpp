#include "phoneme/graphics/Color.hpp"

namespace phoneme::graphics {

Pixel source_over(Pixel source, Pixel destination) noexcept {
    const u32 source_alpha = alpha(source);
    if (source_alpha == 0U) {
        return destination;
    }
    if (source_alpha == 255U) {
        return source;
    }

    const u32 destination_alpha = alpha(destination);
    const u32 inverse_source = 255U - source_alpha;
    const u32 output_alpha = source_alpha +
        ((destination_alpha * inverse_source + 127U) / 255U);
    if (output_alpha == 0U) {
        return 0U;
    }

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
