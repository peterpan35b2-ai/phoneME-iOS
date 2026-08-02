#pragma once

#include <string>
#include <string_view>

#include "phoneme/base/Error.hpp"

namespace phoneme::vm {

[[nodiscard]] Result<std::u16string> decode_modified_utf8(
    std::string_view encoded);
[[nodiscard]] Result<std::string> encode_modified_utf8(
    std::u16string_view text);

} // namespace phoneme::vm
