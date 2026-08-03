#include <cstddef>
#include <cstdint>
#include <string_view>

#include "phoneme/network/Url.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const auto text = std::string_view(reinterpret_cast<const char*>(data), size);
    (void)phoneme::network::Url::parse(text);
    return 0;
}
