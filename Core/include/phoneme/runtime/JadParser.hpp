#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::runtime {

enum class DuplicatePropertyPolicy : u8 {
    reject,
    first_wins,
    last_wins,
};

struct AttributeParserLimits final {
    usize maximum_document_bytes {64U * 1024U};
    usize maximum_physical_line_bytes {8U * 1024U};
    usize maximum_logical_value_bytes {32U * 1024U};
    usize maximum_properties {512U};
    usize maximum_key_bytes {128U};
    DuplicatePropertyPolicy duplicate_policy {DuplicatePropertyPolicy::reject};
    bool stop_at_first_blank_line {false};
};

struct AttributeDocument final {
    std::unordered_map<std::string, std::string> properties;
    std::vector<std::string> order;

    [[nodiscard]] const std::string* find(std::string_view key) const noexcept;
};

class JadParser final {
public:
    [[nodiscard]] static Result<AttributeDocument> parse(
        std::span<const u8> bytes,
        const AttributeParserLimits& limits = {});

    [[nodiscard]] static Result<AttributeDocument> parse_file(
        const std::string& path,
        const AttributeParserLimits& limits = {});

    [[nodiscard]] static Result<std::u16string> decode_utf8(
        std::string_view text);

    [[nodiscard]] static bool is_valid_utf8(std::string_view text) noexcept;
};

} // namespace phoneme::runtime
