#pragma once

#include <span>

#include "phoneme/base/Error.hpp"
#include "phoneme/graphics/Image.hpp"

namespace phoneme::graphics {

// Decodes the immutable raster formats commonly accepted by MIDP devices.
// PNG keeps the strict portable decoder; JPEG and GIF use Apple's bounded
// ImageIO decoder on the iOS/macOS hosts.
[[nodiscard]] Result<Image> decode_image(std::span<const u8> bytes);

} // namespace phoneme::graphics
