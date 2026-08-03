#include <cstddef>
#include <cstdint>
#include <span>

#include "phoneme/runtime/JadParser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const auto bytes = std::span<const phoneme::u8>(data, size);
    (void)phoneme::runtime::JadParser::parse(bytes);
    return 0;
}
