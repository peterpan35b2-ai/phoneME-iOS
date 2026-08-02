#include "phoneme/runtime/JadParser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#include "phoneme/base/Checked.hpp"

namespace phoneme::runtime {
namespace {

[[nodiscard]] std::string trim_ascii(std::string_view value) {
    usize first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }

    usize last = value.size();
    while (last > first &&
           (value[last - 1U] == ' ' || value[last - 1U] == '\t')) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

[[nodiscard]] bool valid_key(std::string_view key) noexcept {
    if (key.empty()) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte >= 0x21U && byte <= 0x7EU && value != ':';
    });
}

[[nodiscard]] bool valid_text_controls(std::string_view text) noexcept {
    return std::none_of(text.begin(), text.end(), [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte == 0U ||
               (byte < 0x20U && byte != '\t' && byte != '\r' &&
                byte != '\n');
    });
}

[[nodiscard]] Result<std::vector<u8>> read_file_bytes(const std::string& path,
                                                       usize maximum_bytes) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return fail(ErrorCode::io_error, "unable to open JAD/manifest file: " + path);
    }

    const std::streampos end = stream.tellg();
    if (end < 0) {
        return fail(ErrorCode::io_error, "unable to determine JAD/manifest size");
    }
    const auto size64 = static_cast<u64>(end);
    if (size64 > static_cast<u64>(maximum_bytes)) {
        return fail(ErrorCode::out_of_range, "JAD/manifest exceeds configured size limit");
    }
    auto size = checked_narrow<usize>(size64);
    if (!size) {
        return std::unexpected(size.error());
    }

    std::vector<u8> bytes(*size);
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream || static_cast<usize>(stream.gcount()) != bytes.size()) {
            return fail(ErrorCode::io_error, "unable to read complete JAD/manifest file");
        }
    }
    return bytes;
}

[[nodiscard]] bool decode_one(std::string_view text,
                              usize& offset,
                              u32& code_point) noexcept {
    const auto first = static_cast<u8>(text[offset]);
    if (first <= 0x7FU) {
        code_point = first;
        ++offset;
        return true;
    }

    usize length = 0;
    u32 value = 0;
    u32 minimum = 0;
    if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        value = static_cast<u32>(first & 0x1FU);
        minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        value = static_cast<u32>(first & 0x0FU);
        minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        value = static_cast<u32>(first & 0x07U);
        minimum = 0x10000U;
    } else {
        return false;
    }

    if (length > text.size() - offset) {
        return false;
    }
    for (usize index = 1; index < length; ++index) {
        const auto continuation = static_cast<u8>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            return false;
        }
        value = (value << 6U) | static_cast<u32>(continuation & 0x3FU);
    }

    if (value < minimum || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
        return false;
    }

    offset += length;
    code_point = value;
    return true;
}

} // namespace

const std::string* AttributeDocument::find(std::string_view key) const noexcept {
    const auto iterator = properties.find(std::string(key));
    return iterator == properties.end() ? nullptr : &iterator->second;
}

Result<AttributeDocument> JadParser::parse(
    std::span<const u8> bytes,
    const AttributeParserLimits& limits) {
    if (limits.maximum_document_bytes == 0 ||
        limits.maximum_physical_line_bytes == 0 ||
        limits.maximum_logical_value_bytes == 0 ||
        limits.maximum_properties == 0 || limits.maximum_key_bytes == 0) {
        return fail(ErrorCode::invalid_argument, "attribute parser limits must be non-zero");
    }
    if (bytes.size() > limits.maximum_document_bytes) {
        return fail(ErrorCode::out_of_range, "JAD/manifest exceeds configured size limit");
    }

    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!is_valid_utf8(text)) {
        return fail(ErrorCode::invalid_argument, "JAD/manifest is not valid UTF-8");
    }
    if (!valid_text_controls(text)) {
        return fail(ErrorCode::invalid_argument,
                    "JAD/manifest contains a null or unsupported control character");
    }

    if (text.size() >= 3U && static_cast<u8>(text[0]) == 0xEFU &&
        static_cast<u8>(text[1]) == 0xBBU && static_cast<u8>(text[2]) == 0xBFU) {
        text.erase(0, 3U);
    }

    std::vector<std::string> logical_lines;
    usize offset = 0;
    while (offset <= text.size()) {
        const usize line_end = text.find('\n', offset);
        const usize physical_end = line_end == std::string::npos ? text.size() : line_end;
        usize content_end = physical_end;
        if (content_end > offset && text[content_end - 1U] == '\r') {
            --content_end;
        }
        const std::string_view physical(text.data() + offset, content_end - offset);
        if (physical.size() > limits.maximum_physical_line_bytes) {
            return fail(ErrorCode::out_of_range,
                        "JAD/manifest physical line exceeds configured size limit");
        }

        if (physical.empty()) {
            if (limits.stop_at_first_blank_line && !logical_lines.empty()) {
                break;
            }
            logical_lines.emplace_back();
        } else if (physical.front() == ' ') {
            if (logical_lines.empty() || logical_lines.back().empty()) {
                return fail(ErrorCode::invalid_argument,
                            "JAD/manifest continuation has no previous property");
            }
            const std::string_view continuation = physical.substr(1U);
            if (logical_lines.back().size() >
                limits.maximum_logical_value_bytes -
                    std::min(limits.maximum_logical_value_bytes,
                             continuation.size())) {
                return fail(ErrorCode::out_of_range,
                            "JAD/manifest continued value exceeds configured size limit");
            }
            logical_lines.back().append(continuation);
        } else {
            logical_lines.emplace_back(physical);
        }

        if (line_end == std::string::npos) {
            break;
        }
        offset = line_end + 1U;
    }

    AttributeDocument document;
    document.properties.reserve(std::min(logical_lines.size(), limits.maximum_properties));
    document.order.reserve(std::min(logical_lines.size(), limits.maximum_properties));

    for (const std::string& logical_line : logical_lines) {
        if (logical_line.empty()) {
            continue;
        }
        const usize separator = logical_line.find(':');
        if (separator == std::string::npos) {
            return fail(ErrorCode::invalid_argument,
                        "JAD/manifest property is missing ':' separator");
        }

        std::string key = trim_ascii(std::string_view(logical_line).substr(0, separator));
        std::string value = trim_ascii(std::string_view(logical_line).substr(separator + 1U));
        if (!valid_key(key)) {
            return fail(ErrorCode::invalid_argument, "JAD/manifest contains an invalid key");
        }
        if (key.size() > limits.maximum_key_bytes ||
            value.size() > limits.maximum_logical_value_bytes) {
            return fail(ErrorCode::out_of_range,
                        "JAD/manifest key or value exceeds configured size limit");
        }

        const auto existing = document.properties.find(key);
        if (existing != document.properties.end()) {
            switch (limits.duplicate_policy) {
            case DuplicatePropertyPolicy::reject:
                return fail(ErrorCode::invalid_argument,
                            "duplicate JAD/manifest property: " + key);
            case DuplicatePropertyPolicy::first_wins:
                continue;
            case DuplicatePropertyPolicy::last_wins:
                existing->second = std::move(value);
                continue;
            }
        }

        if (document.properties.size() >= limits.maximum_properties) {
            return fail(ErrorCode::out_of_range,
                        "JAD/manifest contains too many properties");
        }
        document.order.push_back(key);
        document.properties.emplace(std::move(key), std::move(value));
    }

    return document;
}

Result<AttributeDocument> JadParser::parse_file(
    const std::string& path,
    const AttributeParserLimits& limits) {
    auto bytes = read_file_bytes(path, limits.maximum_document_bytes);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return parse(*bytes, limits);
}

Result<std::u16string> JadParser::decode_utf8(std::string_view text) {
    std::u16string result;
    result.reserve(text.size());

    usize offset = 0;
    while (offset < text.size()) {
        u32 code_point = 0;
        if (!decode_one(text, offset, code_point)) {
            return fail(ErrorCode::invalid_argument, "text is not valid UTF-8");
        }
        if (code_point <= 0xFFFFU) {
            result.push_back(static_cast<char16_t>(code_point));
        } else {
            const u32 adjusted = code_point - 0x10000U;
            result.push_back(static_cast<char16_t>(0xD800U + (adjusted >> 10U)));
            result.push_back(static_cast<char16_t>(0xDC00U + (adjusted & 0x3FFU)));
        }
    }
    return result;
}

bool JadParser::is_valid_utf8(std::string_view text) noexcept {
    usize offset = 0;
    while (offset < text.size()) {
        u32 code_point = 0;
        if (!decode_one(text, offset, code_point)) {
            return false;
        }
    }
    return true;
}

} // namespace phoneme::runtime
