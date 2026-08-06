#include "phoneme/translation/TranslationService.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "phoneme/network/Url.hpp"

namespace phoneme::translation {
namespace {

constexpr std::array<char, 8> kCacheMagic {{
    'P', 'M', 'T', 'R', 'C', '0', '1', '\n',
}};
constexpr usize kMaximumCacheRecordBytes = 16U * 1'024U;
constexpr usize kMaximumJsonDepth = 64U;
constexpr std::chrono::seconds kRetryDelay {30};
constexpr std::chrono::milliseconds kMinimumRequestInterval {350};
constexpr std::chrono::seconds kProviderFailureBackoff {5};
constexpr std::chrono::seconds kProviderRateLimitBackoff {60};
constexpr std::chrono::minutes kMaximumProviderBackoff {10};

struct CachedTranslation final {
    std::string utf8;
    std::shared_ptr<const std::vector<char32_t>> utf32;
};

struct PendingSource final {
    std::string source;
    bool force_single {false};
    bool prefetch {false};
};

struct CompletedTranslation final {
    std::string source;
    std::string translated;
    std::shared_ptr<CachedTranslation> cached;
    std::vector<TranslationService::Utf8Completion> completions;
};

struct ProviderHealth final {
    std::chrono::steady_clock::time_point blocked_until {};
    u32 consecutive_failures {0U};
};

void append_utf8(std::string& output, u32 code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
        return;
    }
    if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        return;
    }
    if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        return;
    }
    output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(
        0x80U | ((code_point >> 12U) & 0x3FU)));
    output.push_back(static_cast<char>(
        0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
}

[[nodiscard]] Result<std::vector<char32_t>> decode_utf8(
    std::string_view input) {
    std::vector<char32_t> output;
    output.reserve(input.size());
    for (usize cursor = 0U; cursor < input.size();) {
        const u32 lead = static_cast<u8>(input[cursor]);
        u32 code_point = 0U;
        usize length = 0U;
        u32 minimum = 0U;
        if (lead <= 0x7FU) {
            code_point = lead;
            length = 1U;
        } else if ((lead & 0xE0U) == 0xC0U) {
            code_point = lead & 0x1FU;
            length = 2U;
            minimum = 0x80U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            code_point = lead & 0x0FU;
            length = 3U;
            minimum = 0x800U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            code_point = lead & 0x07U;
            length = 4U;
            minimum = 0x1'0000U;
        } else {
            return fail(ErrorCode::invalid_argument,
                        "translation text contains invalid UTF-8");
        }
        if (cursor + length > input.size()) {
            return fail(ErrorCode::invalid_argument,
                        "translation text contains truncated UTF-8");
        }
        for (usize offset = 1U; offset < length; ++offset) {
            const u32 continuation = static_cast<u8>(input[cursor + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                return fail(ErrorCode::invalid_argument,
                            "translation text contains invalid UTF-8 continuation");
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10'FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return fail(ErrorCode::invalid_argument,
                        "translation text contains invalid Unicode scalar");
        }
        output.push_back(static_cast<char32_t>(code_point));
        cursor += length;
    }
    return output;
}

[[nodiscard]] std::string encode_utf8(std::span<const char32_t> input) {
    std::string output;
    output.reserve(input.size() * 3U);
    for (const char32_t character : input) {
        u32 code_point = static_cast<u32>(character);
        if (code_point > 0x10'FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            code_point = 0xFFFDU;
        }
        append_utf8(output, code_point);
    }
    return output;
}

[[nodiscard]] bool is_translatable_letter(u32 code_point) noexcept {
    if ((code_point >= 'A' && code_point <= 'Z') ||
        (code_point >= 'a' && code_point <= 'z')) {
        return true;
    }

    // Common alphabetic scripts used by J2ME games. Keep symbols, emoji,
    // counters and punctuation out of the request queue while allowing
    // Google's sl=auto detector to handle the actual source language.
    return (code_point >= 0x00C0U && code_point <= 0x02AFU) ||
           (code_point >= 0x0370U && code_point <= 0x052FU) ||
           (code_point >= 0x0531U && code_point <= 0x08FFU) ||
           (code_point >= 0x0900U && code_point <= 0x1FFFU) ||
           (code_point >= 0x2C00U && code_point <= 0x2DFFU) ||
           (code_point >= 0x3040U && code_point <= 0x318FU) ||
           (code_point >= 0x3400U && code_point <= 0x4DBFU) ||
           (code_point >= 0x4E00U && code_point <= 0x9FFFU) ||
           (code_point >= 0xA000U && code_point <= 0xA4CFU) ||
           (code_point >= 0xAC00U && code_point <= 0xD7AFU) ||
           (code_point >= 0xF900U && code_point <= 0xFAFFU) ||
           (code_point >= 0xFB50U && code_point <= 0xFDFFU) ||
           (code_point >= 0xFE70U && code_point <= 0xFEFFU) ||
           (code_point >= 0xFF21U && code_point <= 0xFF5AU) ||
           (code_point >= 0x1'0000U && code_point <= 0x1'EFFFU) ||
           (code_point >= 0x2'0000U && code_point <= 0x2'FA1FU) ||
           (code_point >= 0x3'0000U && code_point <= 0x3'134FU);
}

enum ScriptMask : u32 {
    script_latin = 1U << 0U,
    script_greek = 1U << 1U,
    script_cyrillic = 1U << 2U,
    script_hebrew = 1U << 3U,
    script_arabic = 1U << 4U,
    script_indic = 1U << 5U,
    script_southeast_asian = 1U << 6U,
    script_japanese_kana = 1U << 7U,
    script_han = 1U << 8U,
    script_hangul = 1U << 9U,
    script_other = 1U << 10U,
};

[[nodiscard]] u32 script_mask_for(u32 code_point) noexcept {
    if ((code_point >= 'A' && code_point <= 'Z') ||
        (code_point >= 'a' && code_point <= 'z') ||
        (code_point >= 0x00C0U && code_point <= 0x02AFU) ||
        (code_point >= 0x1E00U && code_point <= 0x1EFFU)) {
        return script_latin;
    }
    if ((code_point >= 0x0370U && code_point <= 0x03FFU) ||
        (code_point >= 0x1F00U && code_point <= 0x1FFFU)) {
        return script_greek;
    }
    if ((code_point >= 0x0400U && code_point <= 0x052FU) ||
        (code_point >= 0x2DE0U && code_point <= 0x2DFFU) ||
        (code_point >= 0xA640U && code_point <= 0xA69FU)) {
        return script_cyrillic;
    }
    if ((code_point >= 0x0590U && code_point <= 0x05FFU) ||
        (code_point >= 0xFB1DU && code_point <= 0xFB4FU)) {
        return script_hebrew;
    }
    if ((code_point >= 0x0600U && code_point <= 0x08FFU) ||
        (code_point >= 0xFB50U && code_point <= 0xFDFFU) ||
        (code_point >= 0xFE70U && code_point <= 0xFEFFU)) {
        return script_arabic;
    }
    if (code_point >= 0x0900U && code_point <= 0x0DFFU) {
        return script_indic;
    }
    if ((code_point >= 0x0E00U && code_point <= 0x0EFFU) ||
        (code_point >= 0x1780U && code_point <= 0x17FFU)) {
        return script_southeast_asian;
    }
    if ((code_point >= 0x3040U && code_point <= 0x30FFU) ||
        (code_point >= 0x31F0U && code_point <= 0x31FFU)) {
        return script_japanese_kana;
    }
    if ((code_point >= 0x3400U && code_point <= 0x4DBFU) ||
        (code_point >= 0x4E00U && code_point <= 0x9FFFU) ||
        (code_point >= 0xF900U && code_point <= 0xFAFFU) ||
        (code_point >= 0x2'0000U && code_point <= 0x2'FA1FU) ||
        (code_point >= 0x3'0000U && code_point <= 0x3'134FU)) {
        return script_han;
    }
    if ((code_point >= 0x1100U && code_point <= 0x11FFU) ||
        (code_point >= 0x3130U && code_point <= 0x318FU) ||
        (code_point >= 0xAC00U && code_point <= 0xD7AFU)) {
        return script_hangul;
    }
    return is_translatable_letter(code_point) ? script_other : 0U;
}

[[nodiscard]] u32 batch_script_mask(std::string_view text) {
    auto decoded = decode_utf8(text);
    if (!decoded) return 0U;
    u32 mask = 0U;
    for (const char32_t character : *decoded) {
        mask |= script_mask_for(static_cast<u32>(character));
    }
    return mask;
}

[[nodiscard]] bool ascii_equal_ignore_case(char32_t character,
                                           char expected) noexcept {
    u32 value = static_cast<u32>(character);
    if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
    return value == static_cast<u32>(expected);
}

[[nodiscard]] bool starts_with_ascii_ignore_case(
    std::span<const char32_t> text,
    usize begin,
    usize end,
    std::string_view prefix) noexcept {
    if (end - begin < prefix.size()) return false;
    for (usize index = 0U; index < prefix.size(); ++index) {
        if (!ascii_equal_ignore_case(text[begin + index], prefix[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_likely_machine_identifier(
    std::span<const char32_t> text) noexcept {
    usize begin = 0U;
    usize end = text.size();
    while (begin < end && text[begin] <= U' ') ++begin;
    while (end > begin && text[end - 1U] <= U' ') --end;
    if (begin == end) return false;

    constexpr std::array<std::string_view, 8> prefixes {{
        "http://", "https://", "ftp://", "www.",
        "mailto:", "file:", "socket://", "ssl://",
    }};
    for (const auto prefix : prefixes) {
        if (starts_with_ascii_ignore_case(text, begin, end, prefix)) {
            return true;
        }
    }

    bool contains_space = false;
    bool contains_at = false;
    for (usize index = begin; index < end; ++index) {
        contains_space = contains_space || text[index] <= U' ';
        contains_at = contains_at || text[index] == U'@';
    }
    return !contains_space && contains_at;
}

[[nodiscard]] bool should_translate(
    std::span<const char32_t> text,
    usize maximum_source_bytes) noexcept {
    if (text.empty() || text.size() > 1'024U ||
        is_likely_machine_identifier(text)) {
        return false;
    }
    usize estimated_bytes = 0U;
    bool has_letter = false;
    for (const char32_t character : text) {
        const u32 code_point = static_cast<u32>(character);
        if (code_point == 0U) return false;
        if (code_point <= 0x7FU) {
            estimated_bytes += 1U;
        } else if (code_point <= 0x7FFU) {
            estimated_bytes += 2U;
        } else if (code_point <= 0xFFFFU) {
            estimated_bytes += 3U;
        } else {
            estimated_bytes += 4U;
        }
        if (estimated_bytes > maximum_source_bytes) return false;
        has_letter = has_letter || is_translatable_letter(code_point);
    }
    return has_letter;
}

[[nodiscard]] bool should_translate_utf8(
    std::string_view text,
    usize maximum_source_bytes) {
    if (text.empty() || text.size() > maximum_source_bytes) return false;
    auto decoded = decode_utf8(text);
    return decoded && should_translate(*decoded, maximum_source_bytes);
}

[[nodiscard]] bool unreserved(u8 byte) noexcept {
    return (byte >= 'A' && byte <= 'Z') ||
           (byte >= 'a' && byte <= 'z') ||
           (byte >= '0' && byte <= '9') ||
           byte == '-' || byte == '_' || byte == '.' || byte == '~';
}

[[nodiscard]] std::string percent_encode(std::string_view input) {
    static constexpr std::array<char, 16> hex {{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    }};
    std::string output;
    output.reserve(input.size() * 3U);
    for (const char character : input) {
        const u8 byte = static_cast<u8>(character);
        if (unreserved(byte)) {
            output.push_back(character);
        } else {
            output.push_back('%');
            output.push_back(hex[byte >> 4U]);
            output.push_back(hex[byte & 0x0FU]);
        }
    }
    return output;
}

[[nodiscard]] std::string json_quote(std::string_view input) {
    static constexpr std::array<char, 16> hex {{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    }};
    std::string output;
    output.reserve(input.size() + 2U);
    output.push_back('"');
    for (const char character : input) {
        const u8 byte = static_cast<u8>(character);
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20U) {
                output += "\\u00";
                output.push_back(hex[byte >> 4U]);
                output.push_back(hex[byte & 0x0FU]);
            } else {
                output.push_back(character);
            }
            break;
        }
    }
    output.push_back('"');
    return output;
}

[[nodiscard]] bool valid_language_code(std::string_view value) noexcept {
    if (value.empty() || value.size() > 16U) return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-';
    });
}

[[nodiscard]] std::string bing_language_code(std::string_view value) {
    if (value == "auto") return "auto-detect";
    if (value == "zh-CN" || value == "zh-Hans") return "zh-Hans";
    if (value == "zh-TW" || value == "zh-Hant") return "zh-Hant";
    return std::string(value);
}

[[nodiscard]] std::optional<std::string> html_attribute(
    std::string_view html,
    std::string_view attribute,
    usize start = 0U) {
    std::string marker(attribute);
    marker += "=\"";
    const usize begin = html.find(marker, start);
    if (begin == std::string_view::npos) return std::nullopt;
    const usize value_begin = begin + marker.size();
    const usize value_end = html.find('"', value_begin);
    if (value_end == std::string_view::npos || value_end == value_begin) {
        return std::nullopt;
    }
    return std::string(html.substr(value_begin, value_end - value_begin));
}

struct BingTokens final {
    std::string host;
    std::string ig;
    std::string iid;
    std::string key;
    std::string token;
};

[[nodiscard]] Result<BingTokens> parse_bing_tokens(
    const network::HttpResponse& response) {
    const std::string_view html(
        reinterpret_cast<const char*>(response.body.data()),
        response.body.size());

    const std::string_view ig_marker = "IG:\"";
    const usize ig_begin = html.find(ig_marker);
    if (ig_begin == std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page did not contain IG");
    }
    const usize ig_value_begin = ig_begin + ig_marker.size();
    const usize ig_end = html.find('"', ig_value_begin);
    if (ig_end == std::string_view::npos || ig_end == ig_value_begin) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page contained an invalid IG");
    }

    const usize params_name = html.find("params_AbusePreventionHelper");
    const usize params_begin = params_name == std::string_view::npos
        ? std::string_view::npos
        : html.find('[', params_name);
    if (params_begin == std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page did not contain request tokens");
    }
    usize cursor = params_begin + 1U;
    while (cursor < html.size() &&
           (html[cursor] == ' ' || html[cursor] == '\t' ||
            html[cursor] == '\r' || html[cursor] == '\n')) {
        ++cursor;
    }
    const usize key_begin = cursor;
    while (cursor < html.size() &&
           std::isdigit(static_cast<unsigned char>(html[cursor])) != 0) {
        ++cursor;
    }
    if (cursor == key_begin) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page contained an invalid key");
    }
    const std::string key(html.substr(key_begin, cursor - key_begin));
    cursor = html.find('"', cursor);
    if (cursor == std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page contained an invalid token");
    }
    const usize token_begin = ++cursor;
    const usize token_end = html.find('"', token_begin);
    if (token_end == std::string_view::npos || token_end == token_begin) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page contained an empty token");
    }

    const usize rich_tta = html.find("id=\"rich_tta\"");
    auto iid = html_attribute(html, "data-iid", rich_tta ==
        std::string_view::npos ? 0U : rich_tta);
    if (!iid) {
        const usize iid_marker = html.find("translator.");
        if (iid_marker != std::string_view::npos) {
            usize iid_end = iid_marker;
            while (iid_end < html.size()) {
                const char character = html[iid_end];
                if (!(std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                      character == '.' || character == '_' ||
                      character == '-')) {
                    break;
                }
                ++iid_end;
            }
            if (iid_end > iid_marker) {
                iid = std::string(html.substr(iid_marker,
                                              iid_end - iid_marker));
            }
        }
    }
    if (!iid) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translator page did not contain IID");
    }

    return BingTokens {
        .host = response.final_url.host.empty()
            ? std::string("www.bing.com")
            : response.final_url.host,
        .ig = std::string(html.substr(ig_value_begin,
                                     ig_end - ig_value_begin)),
        .iid = std::move(*iid),
        .key = key,
        .token = std::string(html.substr(token_begin,
                                        token_end - token_begin)),
    };
}

class JsonCursor final {
public:
    explicit JsonCursor(std::span<const u8> bytes) : bytes_(bytes) {}

    void skip_whitespace() noexcept {
        while (cursor_ < bytes_.size()) {
            const char character = static_cast<char>(bytes_[cursor_]);
            if (character != ' ' && character != '\n' &&
                character != '\r' && character != '\t') {
                break;
            }
            ++cursor_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        skip_whitespace();
        if (cursor_ >= bytes_.size() ||
            static_cast<char>(bytes_[cursor_]) != expected) {
            return false;
        }
        ++cursor_;
        return true;
    }

    [[nodiscard]] bool peek(char expected) noexcept {
        skip_whitespace();
        return cursor_ < bytes_.size() &&
               static_cast<char>(bytes_[cursor_]) == expected;
    }

    [[nodiscard]] Result<std::string> parse_string() {
        if (!consume('"')) {
            return fail(ErrorCode::invalid_argument,
                        "translation response expected a JSON string");
        }
        std::string output;
        while (cursor_ < bytes_.size()) {
            const u8 byte = bytes_[cursor_++];
            if (byte == static_cast<u8>('"')) return output;
            if (byte < 0x20U) {
                return fail(ErrorCode::invalid_argument,
                            "translation response contains a control character");
            }
            if (byte != static_cast<u8>('\\')) {
                output.push_back(static_cast<char>(byte));
                continue;
            }
            if (cursor_ >= bytes_.size()) {
                return fail(ErrorCode::invalid_argument,
                            "translation response has a truncated escape");
            }
            const char escape = static_cast<char>(bytes_[cursor_++]);
            switch (escape) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                auto scalar = parse_hex_quad();
                if (!scalar) return std::unexpected(scalar.error());
                u32 code_point = *scalar;
                if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
                    const usize saved = cursor_;
                    if (cursor_ + 2U <= bytes_.size() &&
                        bytes_[cursor_] == static_cast<u8>('\\') &&
                        bytes_[cursor_ + 1U] == static_cast<u8>('u')) {
                        cursor_ += 2U;
                        auto low = parse_hex_quad();
                        if (low && *low >= 0xDC00U && *low <= 0xDFFFU) {
                            code_point = 0x1'0000U +
                                ((code_point - 0xD800U) << 10U) +
                                (*low - 0xDC00U);
                        } else {
                            cursor_ = saved;
                            code_point = 0xFFFDU;
                        }
                    } else {
                        code_point = 0xFFFDU;
                    }
                } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
                    code_point = 0xFFFDU;
                }
                append_utf8(output, code_point);
                break;
            }
            default:
                return fail(ErrorCode::invalid_argument,
                            "translation response contains an invalid escape");
            }
        }
        return fail(ErrorCode::invalid_argument,
                    "translation response has an unterminated string");
    }

    [[nodiscard]] Status skip_value(usize depth = 0U) {
        if (depth > kMaximumJsonDepth) {
            return fail(ErrorCode::invalid_argument,
                        "translation response exceeds the JSON depth limit");
        }
        skip_whitespace();
        if (cursor_ >= bytes_.size()) {
            return fail(ErrorCode::invalid_argument,
                        "translation response ended unexpectedly");
        }
        const char character = static_cast<char>(bytes_[cursor_]);
        if (character == '"') {
            auto ignored = parse_string();
            if (!ignored) return std::unexpected(ignored.error());
            return {};
        }
        if (character == '[') {
            ++cursor_;
            skip_whitespace();
            if (consume(']')) return {};
            for (;;) {
                auto skipped = skip_value(depth + 1U);
                if (!skipped) return skipped;
                if (consume(']')) return {};
                if (!consume(',')) {
                    return fail(ErrorCode::invalid_argument,
                                "translation response has a malformed array");
                }
            }
        }
        if (character == '{') {
            ++cursor_;
            skip_whitespace();
            if (consume('}')) return {};
            for (;;) {
                auto key = parse_string();
                if (!key) return std::unexpected(key.error());
                if (!consume(':')) {
                    return fail(ErrorCode::invalid_argument,
                                "translation response has a malformed object");
                }
                auto skipped = skip_value(depth + 1U);
                if (!skipped) return skipped;
                if (consume('}')) return {};
                if (!consume(',')) {
                    return fail(ErrorCode::invalid_argument,
                                "translation response has a malformed object");
                }
            }
        }
        const usize start = cursor_;
        while (cursor_ < bytes_.size()) {
            const char current = static_cast<char>(bytes_[cursor_]);
            if (current == ',' || current == ']' || current == '}' ||
                current == ' ' || current == '\n' ||
                current == '\r' || current == '\t') {
                break;
            }
            ++cursor_;
        }
        if (cursor_ == start) {
            return fail(ErrorCode::invalid_argument,
                        "translation response contains an invalid JSON value");
        }
        return {};
    }

private:
    [[nodiscard]] Result<u32> parse_hex_quad() {
        if (cursor_ + 4U > bytes_.size()) {
            return fail(ErrorCode::invalid_argument,
                        "translation response has a truncated Unicode escape");
        }
        u32 value = 0U;
        for (usize index = 0U; index < 4U; ++index) {
            const char character = static_cast<char>(bytes_[cursor_++]);
            u32 digit = 0U;
            if (character >= '0' && character <= '9') {
                digit = static_cast<u32>(character - '0');
            } else if (character >= 'A' && character <= 'F') {
                digit = static_cast<u32>(character - 'A' + 10);
            } else if (character >= 'a' && character <= 'f') {
                digit = static_cast<u32>(character - 'a' + 10);
            } else {
                return fail(ErrorCode::invalid_argument,
                            "translation response has an invalid Unicode escape");
            }
            value = (value << 4U) | digit;
        }
        return value;
    }

    std::span<const u8> bytes_;
    usize cursor_ {0U};
};

[[nodiscard]] Result<std::string> parse_google_batchexecute_response(
    std::span<const u8> body) {
    const auto is_space = [](char character) noexcept {
        return character == ' ' || character == '\t' ||
               character == '\r' || character == '\n';
    };
    const std::string_view response(
        reinterpret_cast<const char*>(body.data()), body.size());
    constexpr std::string_view rpc_marker = "\"MkEWBc\"";
    const usize marker = response.find(rpc_marker);
    if (marker == std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation response did not contain its RPC payload");
    }
    const usize separator = response.find(',', marker + rpc_marker.size());
    const usize payload_quote = separator == std::string_view::npos
        ? std::string_view::npos
        : response.find('"', separator + 1U);
    if (payload_quote == std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation response contained an invalid RPC payload");
    }

    JsonCursor envelope(body.subspan(payload_quote));
    auto decoded_payload = envelope.parse_string();
    if (!decoded_payload) return std::unexpected(decoded_payload.error());
    const auto* payload_bytes = reinterpret_cast<const u8*>(
        decoded_payload->data());
    JsonCursor cursor(std::span<const u8>(payload_bytes,
                                         decoded_payload->size()));

    // MkEWBc currently returns the translated sentence list at
    // root[1][0][0][5]. Keep navigation strict so unrelated strings in the
    // RPC metadata can never be rendered as a translation.
    if (!cursor.consume('[')) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation RPC payload has an unexpected shape");
    }
    auto skipped = cursor.skip_value();
    if (!skipped || !cursor.consume(',') || !cursor.consume('[') ||
        !cursor.consume('[') || !cursor.consume('[')) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation RPC payload has no translation block");
    }
    for (usize index = 0U; index < 5U; ++index) {
        skipped = cursor.skip_value();
        if (!skipped || !cursor.consume(',')) {
            return fail(ErrorCode::invalid_argument,
                        "Google translation RPC payload has a malformed translation block");
        }
    }
    if (!cursor.consume('[') || cursor.consume(']')) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation RPC payload has no translated segments");
    }

    std::string translated;
    for (;;) {
        if (!cursor.consume('[')) {
            return fail(ErrorCode::invalid_argument,
                        "Google translation RPC payload has a malformed segment");
        }

        std::string segment;
        if (cursor.peek('"')) {
            auto value = cursor.parse_string();
            if (!value) return std::unexpected(value.error());
            segment = std::move(*value);
        } else {
            skipped = cursor.skip_value();
            if (!skipped) return std::unexpected(skipped.error());
        }

        bool prepend_space = false;
        if (!cursor.consume(']')) {
            if (!cursor.consume(',')) {
                return fail(ErrorCode::invalid_argument,
                            "Google translation RPC segment is malformed");
            }
            skipped = cursor.skip_value();
            if (!skipped) return std::unexpected(skipped.error());
            if (!cursor.consume(']')) {
                if (!cursor.consume(',')) {
                    return fail(ErrorCode::invalid_argument,
                                "Google translation RPC segment is malformed");
                }
                prepend_space = cursor.peek('t');
                skipped = cursor.skip_value();
                if (!skipped) return std::unexpected(skipped.error());
                while (!cursor.consume(']')) {
                    if (!cursor.consume(',')) {
                        return fail(ErrorCode::invalid_argument,
                                    "Google translation RPC segment is malformed");
                    }
                    skipped = cursor.skip_value();
                    if (!skipped) return std::unexpected(skipped.error());
                }
            }
        }

        if (!segment.empty()) {
            if (!translated.empty() && prepend_space &&
                !is_space(translated.back()) &&
                !is_space(segment.front())) {
                translated.push_back(' ');
            }
            translated += segment;
        }
        if (cursor.consume(']')) break;
        if (!cursor.consume(',')) {
            return fail(ErrorCode::invalid_argument,
                        "Google translation RPC segments are malformed");
        }
    }

    if (translated.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation RPC produced empty text");
    }
    auto decoded = decode_utf8(translated);
    if (!decoded) return std::unexpected(decoded.error());
    return translated;
}

void write_u32(std::ofstream& stream, u32 value) {
    const std::array<char, 4> bytes {{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    }};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] bool read_u32(std::ifstream& stream, u32& value) {
    std::array<u8, 4> bytes {};
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return false;
    }
    value = static_cast<u32>(bytes[0]) |
            (static_cast<u32>(bytes[1]) << 8U) |
            (static_cast<u32>(bytes[2]) << 16U) |
            (static_cast<u32>(bytes[3]) << 24U);
    return true;
}

[[nodiscard]] std::shared_ptr<CachedTranslation> make_cached(
    std::string translated) {
    auto decoded = decode_utf8(translated);
    if (!decoded || decoded->empty()) return nullptr;
    return std::make_shared<CachedTranslation>(CachedTranslation {
        .utf8 = std::move(translated),
        .utf32 = std::make_shared<const std::vector<char32_t>>(
            std::move(*decoded)),
    });
}

[[nodiscard]] bool ascii_space(char character) noexcept {
    return character == ' ' || character == '\t' ||
           character == '\r' || character == '\n';
}

[[nodiscard]] std::string trimmed_batch_segment(std::string_view segment) {
    while (!segment.empty() && ascii_space(segment.front())) {
        segment.remove_prefix(1U);
    }
    while (!segment.empty() && ascii_space(segment.back())) {
        segment.remove_suffix(1U);
    }
    return std::string(segment);
}

[[nodiscard]] std::optional<std::vector<std::string>> split_batch_translation(
    std::string_view translated,
    usize expected_count) {
    std::vector<std::string> segments;
    segments.reserve(expected_count);
    usize offset = 0U;
    while (offset <= translated.size()) {
        const usize separator = translated.find(';', offset);
        const usize end = separator == std::string_view::npos
            ? translated.size()
            : separator;
        auto segment = trimmed_batch_segment(
            translated.substr(offset, end - offset));
        if (segment.empty()) return std::nullopt;
        segments.push_back(std::move(segment));
        if (separator == std::string_view::npos) break;
        offset = separator + 1U;
    }
    if (segments.size() != expected_count) return std::nullopt;
    return segments;
}

[[nodiscard]] std::string join_batch_sources(
    const std::vector<std::string>& sources) {
    usize size = sources.empty() ? 0U : sources.size() - 1U;
    for (const auto& source : sources) size += source.size();
    std::string joined;
    joined.reserve(size);
    for (usize index = 0U; index < sources.size(); ++index) {
        if (index != 0U) joined.push_back(';');
        joined += sources[index];
    }
    return joined;
}

} // namespace

struct TranslationService::State final {
    TranslationConfiguration configuration;
    std::shared_ptr<network::AsyncNetworkAdapter> adapter;
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<CachedTranslation>> cache;
    std::unordered_set<std::string> scheduled;
    std::unordered_set<std::string> prefetched;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        retry_after;
    std::unordered_map<std::string, std::vector<Utf8Completion>> waiters;
    std::deque<PendingSource> pending;
    std::chrono::steady_clock::time_point first_enqueue_at {};
    std::chrono::steady_clock::time_point last_enqueue_at {};
    std::chrono::steady_clock::time_point next_request_at {};
    ProviderHealth google_health;
    ProviderHealth bing_health;
    TranslationProvider preferred_automatic_provider {TranslationProvider::google};
    usize in_flight {0U};
    bool pump_scheduled {false};
    bool stopped {false};
    std::string bing_host;
    std::string bing_ig;
    std::string bing_iid;
    std::string bing_key;
    std::string bing_token;
    u64 bing_request_count {0U};
    std::atomic<u64> generation {0U};
};

namespace {

[[nodiscard]] constexpr TranslationProvider other_provider(
    TranslationProvider provider) noexcept {
    return provider == TranslationProvider::google
        ? TranslationProvider::bing
        : TranslationProvider::google;
}

template <typename StateType>
[[nodiscard]] ProviderHealth& provider_health(
    StateType& state,
    TranslationProvider provider) noexcept {
    return provider == TranslationProvider::bing
        ? state.bing_health
        : state.google_health;
}

template <typename StateType>
[[nodiscard]] const ProviderHealth& provider_health(
    const StateType& state,
    TranslationProvider provider) noexcept {
    return provider == TranslationProvider::bing
        ? state.bing_health
        : state.google_health;
}

template <typename StateType>
[[nodiscard]] bool provider_ready(
    const StateType& state,
    TranslationProvider provider,
    std::chrono::steady_clock::time_point now) noexcept {
    return now >= provider_health(state, provider).blocked_until;
}

template <typename StateType>
[[nodiscard]] std::optional<TranslationProvider> choose_provider(
    const StateType& state,
    std::chrono::steady_clock::time_point now) noexcept {
    if (state.configuration.provider != TranslationProvider::automatic) {
        return provider_ready(state, state.configuration.provider, now)
            ? std::optional<TranslationProvider>(state.configuration.provider)
            : std::nullopt;
    }
    const TranslationProvider preferred = state.preferred_automatic_provider;
    if (provider_ready(state, preferred, now)) return preferred;
    const TranslationProvider fallback = other_provider(preferred);
    if (provider_ready(state, fallback, now)) return fallback;
    return std::nullopt;
}

template <typename StateType>
[[nodiscard]] std::chrono::steady_clock::time_point next_provider_ready_at(
    const StateType& state) noexcept {
    if (state.configuration.provider != TranslationProvider::automatic) {
        return provider_health(state, state.configuration.provider).blocked_until;
    }
    return std::min(state.google_health.blocked_until,
                    state.bing_health.blocked_until);
}

[[nodiscard]] bool google_sorry_response(
    const network::HttpResponse& response) noexcept {
    return response.final_url.host == "www.google.com" &&
        response.final_url.path.starts_with("/sorry");
}

[[nodiscard]] bool rate_limited_response(
    TranslationProvider provider,
    const Result<network::HttpResponse>& response) noexcept {
    if (!response) return false;
    if (response->status_code == 302 || response->status_code == 403 ||
        response->status_code == 429) {
        return true;
    }
    return provider == TranslationProvider::google &&
        google_sorry_response(*response);
}

template <typename StateType>
void mark_provider_success(StateType& state,
                           TranslationProvider provider) noexcept {
    ProviderHealth& health = provider_health(state, provider);
    health.blocked_until = {};
    health.consecutive_failures = 0U;
    if (state.configuration.provider == TranslationProvider::automatic) {
        state.preferred_automatic_provider = provider;
    }
}

template <typename StateType>
void mark_provider_failure(
    StateType& state,
    TranslationProvider provider,
    const Result<network::HttpResponse>& response) noexcept {
    ProviderHealth& health = provider_health(state, provider);
    health.consecutive_failures = std::min<u32>(
        health.consecutive_failures + 1U, 8U);
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        rate_limited_response(provider, response)
            ? kProviderRateLimitBackoff
            : kProviderFailureBackoff);
    for (u32 attempt = 1U; attempt < health.consecutive_failures; ++attempt) {
        delay = std::min(
            delay * 2,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                kMaximumProviderBackoff));
    }
    health.blocked_until = std::chrono::steady_clock::now() + delay;
}

template <typename StateType>
void load_cache(const std::shared_ptr<StateType>& state) {
    if (state->configuration.cache_path.empty()) return;
    std::ifstream stream(state->configuration.cache_path, std::ios::binary);
    if (!stream) return;

    std::array<char, kCacheMagic.size()> magic {};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (stream.gcount() != static_cast<std::streamsize>(magic.size()) ||
        magic != kCacheMagic) {
        return;
    }

    for (;;) {
        u32 source_size = 0U;
        u32 translated_size = 0U;
        if (!read_u32(stream, source_size)) break;
        if (!read_u32(stream, translated_size)) break;
        if (source_size == 0U || translated_size == 0U ||
            source_size > kMaximumCacheRecordBytes ||
            translated_size > kMaximumCacheRecordBytes) {
            break;
        }
        std::string source(source_size, '\0');
        std::string translated(translated_size, '\0');
        stream.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (stream.gcount() != static_cast<std::streamsize>(source.size())) break;
        stream.read(translated.data(),
                    static_cast<std::streamsize>(translated.size()));
        if (stream.gcount() != static_cast<std::streamsize>(translated.size())) {
            break;
        }
        auto cached = make_cached(std::move(translated));
        if (cached) state->cache.insert_or_assign(std::move(source),
                                                  std::move(cached));
    }
}

template <typename StateType>
void append_cache_record(const StateType& state,
                         std::string_view source,
                         std::string_view translated) {
    if (state.configuration.cache_path.empty() || source.empty() ||
        translated.empty() || source.size() > kMaximumCacheRecordBytes ||
        translated.size() > kMaximumCacheRecordBytes ||
        source.size() > std::numeric_limits<u32>::max() ||
        translated.size() > std::numeric_limits<u32>::max()) {
        return;
    }

    bool write_magic = false;
    {
        std::ifstream existing(state.configuration.cache_path,
                               std::ios::binary | std::ios::ate);
        write_magic = !existing || existing.tellg() == 0;
    }
    std::ofstream stream(state.configuration.cache_path,
                         std::ios::binary | std::ios::app);
    if (!stream) return;
    if (write_magic) {
        stream.write(kCacheMagic.data(),
                     static_cast<std::streamsize>(kCacheMagic.size()));
    }
    write_u32(stream, static_cast<u32>(source.size()));
    write_u32(stream, static_cast<u32>(translated.size()));
    stream.write(source.data(), static_cast<std::streamsize>(source.size()));
    stream.write(translated.data(),
                 static_cast<std::streamsize>(translated.size()));
}

[[nodiscard]] Result<network::HttpRequest> make_google_request(
    const TranslationConfiguration& configuration,
    std::string_view source) {
    if (!valid_language_code(configuration.source_language) ||
        !valid_language_code(configuration.target_language)) {
        return fail(ErrorCode::invalid_argument,
                    "translation language code is invalid");
    }

    std::string url =
        "https://translate.google.com/_/TranslateWebserverUi/data/"
        "batchexecute?rpcids=MkEWBc&hl=";
    url += percent_encode(configuration.target_language);
    url += "&rt=c";
    auto parsed = network::Url::parse(url);
    if (!parsed) return std::unexpected(parsed.error());

    std::string parameters = "[[";
    parameters += json_quote(source);
    parameters.push_back(',');
    parameters += json_quote(configuration.source_language);
    parameters.push_back(',');
    parameters += json_quote(configuration.target_language);
    parameters += ",true],[null]]";

    std::string rpc = "[[[\"MkEWBc\",";
    rpc += json_quote(parameters);
    rpc += ",null,\"generic\"]]]";
    std::string body = "f.req=";
    body += percent_encode(rpc);

    return network::HttpRequest {
        .url = std::move(*parsed),
        .method = "POST",
        .headers = {
            {"Accept", "*/*"},
            {"Accept-Language", "vi-VN,vi;q=0.9"},
            {"Content-Type", "application/x-www-form-urlencoded;charset=UTF-8"},
            {"Origin", "https://translate.google.com"},
            {"Referer", "https://translate.google.com/"},
            {"User-Agent", "phoneME-iOS/translation"},
        },
        .body = std::vector<u8>(body.begin(), body.end()),
        .timeout_ms = 5'000,
        .redirect_limit = 1,
    };
}

[[nodiscard]] Result<network::HttpRequest> make_bing_home_request() {
    auto parsed = network::Url::parse("https://www.bing.com/translator");
    if (!parsed) return std::unexpected(parsed.error());
    return network::HttpRequest {
        .url = std::move(*parsed),
        .method = "GET",
        .headers = {
            {"Accept", "text/html,application/xhtml+xml"},
            {"Accept-Language", "vi-VN,vi;q=0.9,en;q=0.8"},
            {"User-Agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1"},
        },
        .body = {},
        .timeout_ms = 5'000,
        .redirect_limit = 3,
    };
}

[[nodiscard]] Result<network::HttpRequest> make_bing_request(
    const TranslationConfiguration& configuration,
    std::string_view host,
    std::string_view ig,
    std::string_view iid,
    std::string_view key,
    std::string_view token,
    u64 request_count,
    std::string_view source) {
    if (host.empty() || ig.empty() || iid.empty() || key.empty() ||
        token.empty() || !valid_language_code(configuration.source_language) ||
        !valid_language_code(configuration.target_language)) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translation request is invalid");
    }

    std::string url = "https://";
    url += host;
    url += "/ttranslatev3?isVertical=1&IG=";
    url += percent_encode(ig);
    url += "&IID=";
    url += percent_encode(iid);
    url.push_back('.');
    url += std::to_string(request_count);
    auto parsed = network::Url::parse(url);
    if (!parsed) return std::unexpected(parsed.error());

    std::string body = "fromLang=";
    body += percent_encode(bing_language_code(configuration.source_language));
    body += "&to=";
    body += percent_encode(bing_language_code(configuration.target_language));
    body += "&text=";
    body += percent_encode(source);
    body += "&token=";
    body += percent_encode(token);
    body += "&key=";
    body += percent_encode(key);

    std::string origin = "https://";
    origin += host;
    std::string referer = origin + "/translator";
    return network::HttpRequest {
        .url = std::move(*parsed),
        .method = "POST",
        .headers = {
            {"Accept", "application/json"},
            {"Accept-Language", "vi-VN,vi;q=0.9,en;q=0.8"},
            {"Content-Type", "application/x-www-form-urlencoded"},
            {"Origin", origin},
            {"Referer", referer},
            {"User-Agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1"},
        },
        .body = std::vector<u8>(body.begin(), body.end()),
        .timeout_ms = 5'000,
        .redirect_limit = 2,
    };
}

} // namespace

TranslationService::TranslationService(
    TranslationConfiguration configuration,
    std::shared_ptr<network::AsyncNetworkAdapter> adapter)
    : state_(std::make_shared<State>()) {
    state_->configuration = std::move(configuration);
    state_->adapter = std::move(adapter);
    state_->configuration.maximum_concurrent_requests = std::max<usize>(
        1U, state_->configuration.maximum_concurrent_requests);
    state_->configuration.maximum_pending_requests = std::max<usize>(
        state_->configuration.maximum_concurrent_requests,
        state_->configuration.maximum_pending_requests);
    state_->configuration.maximum_source_bytes = std::clamp<usize>(
        state_->configuration.maximum_source_bytes, 64U, 8U * 1'024U);
    state_->configuration.maximum_batch_items = std::clamp<usize>(
        state_->configuration.maximum_batch_items, 1U, 64U);
    state_->configuration.maximum_batch_source_bytes = std::clamp<usize>(
        state_->configuration.maximum_batch_source_bytes,
        state_->configuration.maximum_source_bytes,
        16U * 1'024U);
    state_->configuration.batch_coalescing_delay_ms = std::clamp<i32>(
        state_->configuration.batch_coalescing_delay_ms, 0, 250);
    state_->configuration.maximum_batch_wait_ms = std::clamp<i32>(
        state_->configuration.maximum_batch_wait_ms,
        state_->configuration.batch_coalescing_delay_ms,
        500);
    const bool valid_provider =
        state_->configuration.provider == TranslationProvider::google ||
        state_->configuration.provider == TranslationProvider::bing ||
        state_->configuration.provider == TranslationProvider::automatic;
    if (!state_->adapter || !valid_provider ||
        !valid_language_code(state_->configuration.source_language) ||
        !valid_language_code(state_->configuration.target_language)) {
        state_->configuration.enabled = false;
    }
    load_cache(state_);
}

TranslationService::~TranslationService() {
    std::scoped_lock lock(state_->mutex);
    state_->stopped = true;
    state_->pending.clear();
    state_->pump_scheduled = false;
    state_->scheduled.clear();
    state_->prefetched.clear();
    state_->waiters.clear();
}

bool TranslationService::enabled() const noexcept {
    return state_->configuration.enabled;
}

u64 TranslationService::generation() const noexcept {
    return state_->generation.load(std::memory_order_acquire);
}

std::shared_ptr<const std::vector<char32_t>>
TranslationService::lookup_or_request(std::span<const char32_t> text) {
    if (!enabled() ||
        !should_translate(text, state_->configuration.maximum_source_bytes)) {
        return nullptr;
    }
    const std::string source = encode_utf8(text);
    bool should_pump = false;
    {
        std::scoped_lock lock(state_->mutex);
        const auto cached = state_->cache.find(source);
        if (cached != state_->cache.end()) return cached->second->utf32;
        const auto retry = state_->retry_after.find(source);
        if (retry != state_->retry_after.end() &&
            std::chrono::steady_clock::now() < retry->second) {
            return nullptr;
        }
        if (state_->stopped) return nullptr;
        if (state_->scheduled.contains(source)) {
            state_->prefetched.erase(source);
            const auto pending = std::find_if(
                state_->pending.begin(), state_->pending.end(),
                [&](const PendingSource& value) {
                    return value.prefetch && value.source == source;
                });
            if (pending != state_->pending.end()) {
                PendingSource promoted = std::move(*pending);
                promoted.prefetch = false;
                state_->pending.erase(pending);
                state_->pending.push_front(std::move(promoted));
                should_pump = true;
            }
        } else if (state_->scheduled.size() <
                       state_->configuration.maximum_pending_requests) {
            state_->scheduled.insert(source);
            const auto now = std::chrono::steady_clock::now();
            if (state_->pending.empty()) state_->first_enqueue_at = now;
            state_->pending.push_back(PendingSource {
                .source = source,
            });
            state_->last_enqueue_at = now;
            should_pump = true;
        }
    }
    if (should_pump) schedule_pump(state_);
    return nullptr;
}

std::optional<std::string> TranslationService::lookup_or_request_utf8(
    std::string_view text,
    Utf8Completion completion) {
    if (!enabled() || !should_translate_utf8(
            text, state_->configuration.maximum_source_bytes)) {
        return std::nullopt;
    }
    const std::string source(text);
    bool should_pump = false;
    {
        std::scoped_lock lock(state_->mutex);
        const auto cached = state_->cache.find(source);
        if (cached != state_->cache.end()) return cached->second->utf8;
        if (completion) {
            state_->waiters[source].push_back(std::move(completion));
        }
        const auto retry = state_->retry_after.find(source);
        if (retry != state_->retry_after.end() &&
            std::chrono::steady_clock::now() < retry->second) {
            state_->waiters.erase(source);
            return std::nullopt;
        }
        if (state_->stopped) {
            state_->waiters.erase(source);
            return std::nullopt;
        }
        if (state_->scheduled.contains(source)) {
            state_->prefetched.erase(source);
            const auto pending = std::find_if(
                state_->pending.begin(), state_->pending.end(),
                [&](const PendingSource& value) {
                    return value.prefetch && value.source == source;
                });
            if (pending != state_->pending.end()) {
                PendingSource promoted = std::move(*pending);
                promoted.prefetch = false;
                state_->pending.erase(pending);
                state_->pending.push_front(std::move(promoted));
                should_pump = true;
            }
        } else if (state_->scheduled.size() <
                       state_->configuration.maximum_pending_requests) {
            state_->scheduled.insert(source);
            const auto now = std::chrono::steady_clock::now();
            if (state_->pending.empty()) state_->first_enqueue_at = now;
            state_->pending.push_back(PendingSource {
                .source = source,
            });
            state_->last_enqueue_at = now;
            should_pump = true;
        } else {
            state_->waiters.erase(source);
        }
    }
    if (should_pump) schedule_pump(state_);
    return std::nullopt;
}

void TranslationService::prefetch(std::span<const char32_t> text) {
    if (!enabled() ||
        !should_translate(text, state_->configuration.maximum_source_bytes)) {
        return;
    }
    prefetch_utf8(encode_utf8(text));
}

void TranslationService::prefetch_utf8(std::string_view text) {
    if (!enabled() || !should_translate_utf8(
            text, state_->configuration.maximum_source_bytes)) {
        return;
    }
    const std::string source(text);
    bool should_pump = false;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->stopped || state_->cache.contains(source) ||
            state_->scheduled.contains(source)) {
            return;
        }
        const auto retry = state_->retry_after.find(source);
        if (retry != state_->retry_after.end() &&
            std::chrono::steady_clock::now() < retry->second) {
            return;
        }
        const usize maximum_prefetch_pending = std::clamp<usize>(
            state_->configuration.maximum_pending_requests / 8U,
            4U, 24U);
        const usize prefetch_pending = static_cast<usize>(std::count_if(
            state_->pending.begin(), state_->pending.end(),
            [](const PendingSource& value) { return value.prefetch; }));
        if (state_->scheduled.size() >=
                state_->configuration.maximum_pending_requests ||
            prefetch_pending >= maximum_prefetch_pending) {
            return;
        }
        state_->scheduled.insert(source);
        state_->prefetched.insert(source);
        const auto now = std::chrono::steady_clock::now();
        if (state_->pending.empty()) state_->first_enqueue_at = now;
        state_->pending.push_back(PendingSource {
            .source = source,
            .prefetch = true,
        });
        state_->last_enqueue_at = now;
        should_pump = true;
    }
    if (should_pump) schedule_pump(state_);
}

bool TranslationService::contains_translatable_text(
    std::span<const char32_t> text) noexcept {
    return std::any_of(text.begin(), text.end(), [](char32_t character) {
        return is_translatable_letter(static_cast<u32>(character));
    });
}

Result<std::string> TranslationService::parse_google_response(
    std::span<const u8> body) {
    if (body.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "Google translation response is empty");
    }
    return parse_google_batchexecute_response(body);
}

Result<std::string> TranslationService::parse_bing_response(
    std::span<const u8> body) {
    if (body.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translation response is empty");
    }

    JsonCursor cursor(body);
    if (!cursor.consume('[') || !cursor.consume('{')) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translation response has an unexpected shape");
    }

    std::string translated;
    if (!cursor.consume('}')) {
        for (;;) {
            auto key = cursor.parse_string();
            if (!key || !cursor.consume(':')) {
                return fail(ErrorCode::invalid_argument,
                            "Bing translation response has a malformed object");
            }

            if (*key == "translations") {
                if (!cursor.consume('[') || cursor.consume(']') ||
                    !cursor.consume('{')) {
                    return fail(ErrorCode::invalid_argument,
                                "Bing translation response has no translations");
                }
                if (!cursor.consume('}')) {
                    for (;;) {
                        auto translation_key = cursor.parse_string();
                        if (!translation_key || !cursor.consume(':')) {
                            return fail(
                                ErrorCode::invalid_argument,
                                "Bing translation response has a malformed translation");
                        }
                        if (*translation_key == "text") {
                            auto value = cursor.parse_string();
                            if (!value) return std::unexpected(value.error());
                            translated = std::move(*value);
                        } else {
                            auto skipped = cursor.skip_value();
                            if (!skipped) return std::unexpected(skipped.error());
                        }
                        if (cursor.consume('}')) break;
                        if (!cursor.consume(',')) {
                            return fail(
                                ErrorCode::invalid_argument,
                                "Bing translation response has a malformed translation");
                        }
                    }
                }
                while (!cursor.consume(']')) {
                    if (!cursor.consume(',')) {
                        return fail(
                            ErrorCode::invalid_argument,
                            "Bing translation response has malformed translations");
                    }
                    auto skipped = cursor.skip_value();
                    if (!skipped) return std::unexpected(skipped.error());
                }
            } else {
                auto skipped = cursor.skip_value();
                if (!skipped) return std::unexpected(skipped.error());
            }

            if (cursor.consume('}')) break;
            if (!cursor.consume(',')) {
                return fail(ErrorCode::invalid_argument,
                            "Bing translation response has a malformed object");
            }
        }
    }

    if (translated.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "Bing translation response produced empty text");
    }
    auto decoded = decode_utf8(translated);
    if (!decoded) return std::unexpected(decoded.error());
    return translated;
}

void TranslationService::schedule_pump(
    const std::shared_ptr<State>& state) {
    {
        std::scoped_lock lock(state->mutex);
        if (state->stopped || state->pump_scheduled) return;
        state->pump_scheduled = true;
    }

    std::thread([state] {
        for (;;) {
            std::chrono::steady_clock::time_point deadline;
            {
                std::scoped_lock lock(state->mutex);
                if (state->stopped) {
                    state->pump_scheduled = false;
                    return;
                }
                const auto quiet_deadline = state->last_enqueue_at +
                    std::chrono::milliseconds(
                        state->configuration.batch_coalescing_delay_ms);
                const auto maximum_deadline = state->first_enqueue_at +
                    std::chrono::milliseconds(
                        state->configuration.maximum_batch_wait_ms);
                deadline = std::max(
                    std::min(quiet_deadline, maximum_deadline),
                    state->next_request_at);
            }

            const auto now = std::chrono::steady_clock::now();
            if (now < deadline) std::this_thread::sleep_until(deadline);

            {
                std::scoped_lock lock(state->mutex);
                if (state->stopped) {
                    state->pump_scheduled = false;
                    return;
                }
                const auto quiet_deadline = state->last_enqueue_at +
                    std::chrono::milliseconds(
                        state->configuration.batch_coalescing_delay_ms);
                const auto maximum_deadline = state->first_enqueue_at +
                    std::chrono::milliseconds(
                        state->configuration.maximum_batch_wait_ms);
                const auto refreshed_deadline = std::max(
                    std::min(quiet_deadline, maximum_deadline),
                    state->next_request_at);
                if (std::chrono::steady_clock::now() < refreshed_deadline) {
                    continue;
                }
            }
            pump_requests(state);
            return;
        }
    }).detach();
}

void TranslationService::pump_requests(const std::shared_ptr<State>& state) {
    for (;;) {
        std::vector<std::string> sources;
        TranslationProvider provider = TranslationProvider::bing;
        bool should_reschedule = false;
        bool allow_provider_fallback = false;
        {
            std::scoped_lock lock(state->mutex);
            state->pump_scheduled = false;
            if (state->stopped ||
                state->in_flight >=
                    state->configuration.maximum_concurrent_requests ||
                state->pending.empty()) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now < state->next_request_at) {
                should_reschedule = true;
            } else if (auto selected = choose_provider(*state, now)) {
                provider = *selected;
                allow_provider_fallback =
                    state->configuration.provider ==
                        TranslationProvider::automatic;
            } else {
                state->next_request_at = std::max(
                    state->next_request_at,
                    next_provider_ready_at(*state));
                should_reschedule = true;
            }

            if (!should_reschedule) {
                // Only one request may bootstrap the Bing token session.
                if (provider == TranslationProvider::bing &&
                    state->bing_token.empty() && state->in_flight != 0U) {
                    return;
                }

                auto first_iterator = std::find_if(
                    state->pending.begin(), state->pending.end(),
                    [](const PendingSource& value) {
                        return !value.prefetch;
                    });
                if (first_iterator == state->pending.end()) {
                    first_iterator = state->pending.begin();
                }
                PendingSource first = std::move(*first_iterator);
                state->pending.erase(first_iterator);
                usize joined_size = first.source.size();
                const bool may_batch = !first.force_single &&
                    first.source.find(';') == std::string::npos;
                const bool first_prefetch = first.prefetch;
                const u32 first_script_mask =
                    batch_script_mask(first.source);
                sources.push_back(std::move(first.source));

                if (may_batch) {
                    for (auto iterator = state->pending.begin();
                         iterator != state->pending.end() &&
                         sources.size() <
                             state->configuration.maximum_batch_items;) {
                        const bool compatible_script =
                            state->configuration.source_language != "auto" ||
                            batch_script_mask(iterator->source) ==
                                first_script_mask;
                        if (iterator->prefetch != first_prefetch ||
                            iterator->force_single ||
                            iterator->source.find(';') !=
                                std::string::npos ||
                            !compatible_script) {
                            ++iterator;
                            continue;
                        }
                        const usize next_size = joined_size + 1U +
                            iterator->source.size();
                        if (next_size >
                            state->configuration.maximum_batch_source_bytes) {
                            ++iterator;
                            continue;
                        }
                        joined_size = next_size;
                        sources.push_back(std::move(iterator->source));
                        iterator = state->pending.erase(iterator);
                    }
                }
                if (state->pending.empty()) {
                    state->first_enqueue_at = {};
                    state->last_enqueue_at = {};
                }
                ++state->in_flight;
                state->next_request_at = now + kMinimumRequestInterval;
            }
        }

        if (should_reschedule) {
            schedule_pump(state);
            return;
        }

        if (provider == TranslationProvider::bing) {
            start_bing_request(
                state, std::move(sources), false, true,
                allow_provider_fallback);
            continue;
        }

        perform_google_request(
            state, std::move(sources), allow_provider_fallback);
    }
}

void TranslationService::perform_google_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    bool allow_provider_fallback) {
    const std::string joined_source = join_batch_sources(sources);
    auto request = make_google_request(state->configuration, joined_source);
    if (!request) {
        complete_google_request(
            state, std::move(sources), allow_provider_fallback,
            std::unexpected(request.error()));
        return;
    }

    auto callback_sources = sources;
    auto operation = state->adapter->perform_http(
        std::move(*request),
        [state, sources = std::move(callback_sources),
         allow_provider_fallback](
            Result<network::HttpResponse> response) mutable {
            complete_google_request(
                state, std::move(sources), allow_provider_fallback,
                std::move(response));
        });
    if (!operation) {
        complete_google_request(
            state, std::move(sources), allow_provider_fallback,
            std::unexpected(operation.error()));
    }
}

void TranslationService::complete_google_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    bool allow_provider_fallback,
    Result<network::HttpResponse> response) {
    std::vector<std::string> translated_values;
    bool valid_translation_response = false;
    if (response && response->status_code >= 200 &&
        response->status_code < 300) {
        auto translated = parse_google_response(response->body);
        if (translated) {
            valid_translation_response = true;
            if (sources.size() == 1U) {
                translated_values.push_back(std::move(*translated));
            } else if (auto split = split_batch_translation(
                           *translated, sources.size())) {
                translated_values = std::move(*split);
            }
        }
    }

    bool use_fallback = false;
    {
        std::scoped_lock lock(state->mutex);
        if (valid_translation_response) {
            mark_provider_success(*state, TranslationProvider::google);
        } else {
            mark_provider_failure(
                *state, TranslationProvider::google, response);
            use_fallback = allow_provider_fallback &&
                provider_ready(
                    *state, TranslationProvider::bing,
                    std::chrono::steady_clock::now());
        }
    }

    if (use_fallback) {
        start_bing_request(
            state, std::move(sources), false, true, false);
        return;
    }
    finish_request(state, std::move(sources),
                   std::move(translated_values));
}

void TranslationService::start_bing_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    bool force_token_refresh,
    bool allow_token_retry,
    bool allow_provider_fallback) {
    bool needs_tokens = force_token_refresh;
    {
        std::scoped_lock lock(state->mutex);
        if (force_token_refresh) {
            state->bing_host.clear();
            state->bing_ig.clear();
            state->bing_iid.clear();
            state->bing_key.clear();
            state->bing_token.clear();
            state->bing_request_count = 0U;
        }
        needs_tokens = needs_tokens || state->bing_token.empty();
    }

    if (!needs_tokens) {
        perform_bing_translation(
            state, std::move(sources), allow_token_retry,
            allow_provider_fallback);
        return;
    }

    auto request = make_bing_home_request();
    if (!request) {
        complete_bing_home_request(
            state, std::move(sources), allow_token_retry,
            allow_provider_fallback, std::unexpected(request.error()));
        return;
    }
    auto callback_sources = sources;
    auto operation = state->adapter->perform_http(
        std::move(*request),
        [state, sources = std::move(callback_sources), allow_token_retry,
         allow_provider_fallback](
            Result<network::HttpResponse> response) mutable {
            complete_bing_home_request(
                state, std::move(sources), allow_token_retry,
                allow_provider_fallback, std::move(response));
        });
    if (!operation) {
        complete_bing_home_request(
            state, std::move(sources), allow_token_retry,
            allow_provider_fallback,
            std::unexpected(operation.error()));
    }
}

void TranslationService::complete_bing_home_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    bool allow_token_retry,
    bool allow_provider_fallback,
    Result<network::HttpResponse> response) {
    std::optional<BingTokens> tokens;
    if (response && response->status_code >= 200 &&
        response->status_code < 300) {
        auto parsed = parse_bing_tokens(*response);
        if (parsed) tokens = std::move(*parsed);
    }

    if (!tokens) {
        bool use_fallback = false;
        {
            std::scoped_lock lock(state->mutex);
            state->bing_host.clear();
            state->bing_ig.clear();
            state->bing_iid.clear();
            state->bing_key.clear();
            state->bing_token.clear();
            state->bing_request_count = 0U;
            mark_provider_failure(
                *state, TranslationProvider::bing, response);
            use_fallback = allow_provider_fallback &&
                provider_ready(
                    *state, TranslationProvider::google,
                    std::chrono::steady_clock::now());
        }
        if (use_fallback) {
            perform_google_request(state, std::move(sources), false);
        } else {
            finish_request(state, std::move(sources), {});
        }
        return;
    }

    {
        std::scoped_lock lock(state->mutex);
        state->bing_host = std::move(tokens->host);
        state->bing_ig = std::move(tokens->ig);
        state->bing_iid = std::move(tokens->iid);
        state->bing_key = std::move(tokens->key);
        state->bing_token = std::move(tokens->token);
        state->bing_request_count = 0U;
    }
    perform_bing_translation(
        state, std::move(sources), allow_token_retry,
        allow_provider_fallback);
}

void TranslationService::perform_bing_translation(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    bool allow_token_retry,
    bool allow_provider_fallback) {
    std::string host;
    std::string ig;
    std::string iid;
    std::string key;
    std::string token;
    u64 request_count = 0U;
    {
        std::scoped_lock lock(state->mutex);
        host = state->bing_host;
        ig = state->bing_ig;
        iid = state->bing_iid;
        key = state->bing_key;
        token = state->bing_token;
        request_count = ++state->bing_request_count;
    }

    const std::string joined_source = join_batch_sources(sources);
    auto request = make_bing_request(
        state->configuration, host, ig, iid, key, token,
        request_count, joined_source);
    if (!request) {
        complete_bing_request(
            state, std::move(sources), allow_token_retry,
            allow_provider_fallback, std::unexpected(request.error()));
        return;
    }
    auto callback_sources = sources;
    auto operation = state->adapter->perform_http(
        std::move(*request),
        [state, sources = std::move(callback_sources), allow_token_retry,
         allow_provider_fallback](
            Result<network::HttpResponse> response) mutable {
            complete_bing_request(
                state, std::move(sources), allow_token_retry,
                allow_provider_fallback, std::move(response));
        });
    if (!operation) {
        complete_bing_request(
            state, std::move(sources), allow_token_retry,
            allow_provider_fallback,
            std::unexpected(operation.error()));
    }
}

void TranslationService::complete_bing_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    bool allow_token_retry,
    bool allow_provider_fallback,
    Result<network::HttpResponse> response) {
    std::vector<std::string> translated_values;
    bool valid_translation_response = false;
    if (response && response->status_code >= 200 &&
        response->status_code < 300) {
        auto translated = parse_bing_response(response->body);
        if (translated) {
            valid_translation_response = true;
            if (sources.size() == 1U) {
                translated_values.push_back(std::move(*translated));
            } else if (auto split = split_batch_translation(
                           *translated, sources.size())) {
                translated_values = std::move(*split);
            }
        }
    }

    if (valid_translation_response) {
        {
            std::scoped_lock lock(state->mutex);
            mark_provider_success(*state, TranslationProvider::bing);
        }
        finish_request(state, std::move(sources),
                       std::move(translated_values));
        return;
    }

    const bool token_may_be_stale = response &&
        (response->status_code == 401 ||
         (response->status_code >= 200 && response->status_code < 300));
    if (allow_token_retry && token_may_be_stale) {
        start_bing_request(
            state, std::move(sources), true, false,
            allow_provider_fallback);
        return;
    }

    bool use_fallback = false;
    {
        std::scoped_lock lock(state->mutex);
        mark_provider_failure(*state, TranslationProvider::bing, response);
        use_fallback = allow_provider_fallback &&
            provider_ready(
                *state, TranslationProvider::google,
                std::chrono::steady_clock::now());
    }
    if (use_fallback) {
        perform_google_request(state, std::move(sources), false);
        return;
    }
    finish_request(state, std::move(sources), {});
}

void TranslationService::finish_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    std::vector<std::string> translated_values) {
    std::vector<CompletedTranslation> completed;
    if (translated_values.size() == sources.size()) {
        completed.reserve(sources.size());
        for (usize index = 0U; index < sources.size(); ++index) {
            auto cached = make_cached(translated_values[index]);
            if (!cached) {
                completed.clear();
                break;
            }
            completed.push_back(CompletedTranslation {
                .source = sources[index],
                .translated = std::move(translated_values[index]),
                .cached = std::move(cached),
            });
        }
    }

    if (completed.size() != sources.size() && sources.size() > 1U) {
        {
            std::scoped_lock lock(state->mutex);
            if (state->in_flight > 0U) --state->in_flight;
            if (!state->stopped) {
                for (auto iterator = sources.rbegin();
                     iterator != sources.rend(); ++iterator) {
                    state->pending.push_front(PendingSource {
                        .source = *iterator,
                        .force_single = true,
                        .prefetch = state->prefetched.contains(*iterator),
                    });
                }
            } else {
                for (const auto& source : sources) {
                    state->scheduled.erase(source);
                    state->prefetched.erase(source);
                    state->waiters.erase(source);
                }
            }
        }
        pump_requests(state);
        return;
    }

    bool stored = false;
    {
        std::scoped_lock lock(state->mutex);
        if (state->in_flight > 0U) --state->in_flight;
        if (!completed.empty() && !state->stopped) {
            usize foreground_count = 0U;
            for (auto& item : completed) {
                state->scheduled.erase(item.source);
                const bool was_prefetch = state->prefetched.erase(item.source) > 0U;
                state->retry_after.erase(item.source);
                state->cache.insert_or_assign(item.source, item.cached);
                if (const auto waiters = state->waiters.find(item.source);
                    waiters != state->waiters.end()) {
                    item.completions = std::move(waiters->second);
                    state->waiters.erase(waiters);
                }
                if (!was_prefetch) ++foreground_count;
            }
            if (foreground_count != 0U) {
                state->generation.fetch_add(
                    static_cast<u64>(foreground_count),
                    std::memory_order_release);
            }
            stored = true;
        } else {
            for (const auto& source : sources) {
                state->scheduled.erase(source);
                state->prefetched.erase(source);
                state->waiters.erase(source);
                state->retry_after.insert_or_assign(
                    source,
                    std::chrono::steady_clock::now() + kRetryDelay);
            }
        }
    }

    if (stored) {
        // Publish the in-memory result immediately. Disk persistence must not
        // add latency to LCDUI updates or Canvas repaint scheduling.
        for (auto& item : completed) {
            for (auto& completion : item.completions) {
                if (completion) completion(item.translated);
            }
        }
    }

    // Let the next ready batch start before doing synchronous cache I/O.
    schedule_pump(state);

    if (stored) {
        for (const auto& item : completed) {
            append_cache_record(*state, item.source, item.translated);
        }
    }
}

} // namespace phoneme::translation
