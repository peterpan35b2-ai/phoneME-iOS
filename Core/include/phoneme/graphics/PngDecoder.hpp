#pragma once

#include <span>

#include "phoneme/graphics/Image.hpp"

namespace phoneme::graphics {

[[nodiscard]] Result<Image> decode_png(std::span<const u8> bytes);

} // namespace phoneme::graphics
