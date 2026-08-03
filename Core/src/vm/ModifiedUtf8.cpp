#include "phoneme/vm/ModifiedUtf8.hpp"

#include <limits>

namespace phoneme::vm {
namespace {

[[nodiscard]] bool continuation(unsigned char byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

} // namespace

Result<std::u16string> decode_modified_utf8(std::string_view encoded,
                                             ModifiedUtf8Mode mode) {
    std::u16string result;
    result.reserve(encoded.size());

    usize index = 0;
    while (index < encoded.size()) {
        const auto first = static_cast<unsigned char>(encoded[index]);
        if (first == 0) {
            if (mode == ModifiedUtf8Mode::allow_raw_nul) {
                result.push_back(u'\0');
                ++index;
                continue;
            }
            return fail(ErrorCode::malformed_class,
                        "modified UTF-8 contains an unencoded null byte");
        }
        if (first <= 0x7FU) {
            result.push_back(static_cast<char16_t>(first));
            ++index;
            continue;
        }

        if ((first & 0xE0U) == 0xC0U) {
            if (index + 1 >= encoded.size()) {
                return fail(ErrorCode::malformed_class,
                            "truncated two-byte modified UTF-8 sequence");
            }
            const auto second = static_cast<unsigned char>(encoded[index + 1]);
            if (!continuation(second)) {
                return fail(ErrorCode::malformed_class,
                            "invalid modified UTF-8 continuation byte");
            }
            const u16 value = static_cast<u16>(
                (static_cast<u16>(first & 0x1FU) << 6U) |
                static_cast<u16>(second & 0x3FU));
            if (value == 0) {
                if (first != 0xC0U || second != 0x80U) {
                    return fail(ErrorCode::malformed_class,
                                "invalid modified UTF-8 null encoding");
                }
            } else if (value < 0x80U) {
                return fail(ErrorCode::malformed_class,
                            "overlong modified UTF-8 sequence");
            }
            result.push_back(static_cast<char16_t>(value));
            index += 2;
            continue;
        }

        if ((first & 0xF0U) == 0xE0U) {
            if (index + 2 >= encoded.size()) {
                return fail(ErrorCode::malformed_class,
                            "truncated three-byte modified UTF-8 sequence");
            }
            const auto second = static_cast<unsigned char>(encoded[index + 1]);
            const auto third = static_cast<unsigned char>(encoded[index + 2]);
            if (!continuation(second) || !continuation(third)) {
                return fail(ErrorCode::malformed_class,
                            "invalid modified UTF-8 continuation byte");
            }
            const u16 value = static_cast<u16>(
                (static_cast<u16>(first & 0x0FU) << 12U) |
                (static_cast<u16>(second & 0x3FU) << 6U) |
                static_cast<u16>(third & 0x3FU));
            if (value < 0x800U) {
                return fail(ErrorCode::malformed_class,
                            "overlong modified UTF-8 sequence");
            }
            result.push_back(static_cast<char16_t>(value));
            index += 3;
            continue;
        }

        return fail(ErrorCode::malformed_class,
                    "four-byte UTF-8 is not valid modified UTF-8");
    }

    return result;
}

Result<std::string> encode_modified_utf8(std::u16string_view text) {
    if (text.size() > std::numeric_limits<usize>::max() / 3U) {
        return fail(ErrorCode::overflow,
                    "modified UTF-8 output size overflow");
    }

    std::string result;
    result.reserve(text.size() * 3U);
    for (const char16_t character : text) {
        const u16 value = static_cast<u16>(character);
        if (value == 0) {
            result.push_back(static_cast<char>(0xC0U));
            result.push_back(static_cast<char>(0x80U));
        } else if (value <= 0x7FU) {
            result.push_back(static_cast<char>(value));
        } else if (value <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            result.push_back(static_cast<char>(
                0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }
    return result;
}

} // namespace phoneme::vm
