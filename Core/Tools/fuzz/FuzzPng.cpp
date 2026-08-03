#include <cstddef>
#include <cstdint>
#include <span>

#include "phoneme/graphics/PngDecoder.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const auto bytes = std::span<const phoneme::u8>(data, size);
    (void)phoneme::graphics::decode_png(bytes);
    return 0;
}
