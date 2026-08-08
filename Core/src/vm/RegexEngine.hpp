#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/base/Types.hpp"

namespace phoneme::vm {

struct RegexCapture final {
    bool matched {false};
    usize start {0U};
    usize end {0U};
};

struct RegexMatch final {
    usize start {0U};
    usize end {0U};
    std::vector<RegexCapture> captures;
};

[[nodiscard]] Status validate_java_regex(std::u16string_view pattern,
                                         i32 flags = 0);

[[nodiscard]] Result<std::optional<RegexMatch>> find_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    usize from,
    i32 flags = 0);

[[nodiscard]] Result<std::optional<RegexMatch>> match_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    i32 flags = 0);

[[nodiscard]] Result<std::vector<std::u16string>> split_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    i32 limit = 0,
    i32 flags = 0);

[[nodiscard]] Result<std::u16string> replace_all_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    std::u16string_view replacement,
    i32 flags = 0);

} // namespace phoneme::vm
