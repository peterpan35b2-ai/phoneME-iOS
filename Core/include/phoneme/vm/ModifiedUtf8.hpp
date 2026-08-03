#pragma once

#include <string>
#include <string_view>

#include "phoneme/base/Error.hpp"

namespace phoneme::vm {

enum class ModifiedUtf8Mode {
    strict,
    allow_raw_nul,
};

[[nodiscard]] Result<std::u16string> decode_modified_utf8(
    std::string_view encoded,
    ModifiedUtf8Mode mode = ModifiedUtf8Mode::strict);
[[nodiscard]] Result<std::string> encode_modified_utf8(
    std::u16string_view text);

} // namespace phoneme::vm
