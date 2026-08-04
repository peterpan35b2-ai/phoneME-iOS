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

struct CachedTranslation final {
    std::string utf8;
    std::shared_ptr<const std::vector<char32_t>> utf32;
};

struct PendingSource final {
    std::string source;
    bool force_single {false};
};

struct CompletedTranslation final {
    std::string source;
    std::string translated;
    std::shared_ptr<CachedTranslation> cached;
    std::vector<TranslationService::Utf8Completion> completions;
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

[[nodiscard]] bool is_han(u32 code_point) noexcept {
    return (code_point >= 0x3400U && code_point <= 0x4DBFU) ||
           (code_point >= 0x4E00U && code_point <= 0x9FFFU) ||
           (code_point >= 0xF900U && code_point <= 0xFAFFU) ||
           (code_point >= 0x2'0000U && code_point <= 0x2'FA1FU) ||
           (code_point >= 0x3'0000U && code_point <= 0x3'134FU);
}

[[nodiscard]] bool should_translate(
    std::span<const char32_t> text,
    usize maximum_source_bytes) noexcept {
    if (text.empty() || text.size() > 1'024U) return false;
    usize estimated_bytes = 0U;
    bool has_han = false;
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
        has_han = has_han || is_han(code_point);
    }
    return has_han;
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

[[nodiscard]] bool valid_language_code(std::string_view value) noexcept {
    if (value.empty() || value.size() > 16U) return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-';
    });
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
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        retry_after;
    std::unordered_map<std::string, std::vector<Utf8Completion>> waiters;
    std::deque<PendingSource> pending;
    usize in_flight {0U};
    bool pump_scheduled {false};
    bool stopped {false};
    std::atomic<u64> generation {0U};
};

namespace {

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

[[nodiscard]] Result<network::HttpRequest> make_request(
    const TranslationConfiguration& configuration,
    std::string_view source) {
    if (!valid_language_code(configuration.source_language) ||
        !valid_language_code(configuration.target_language)) {
        return fail(ErrorCode::invalid_argument,
                    "translation language code is invalid");
    }
    std::string url =
        "https://translate.googleapis.com/translate_a/single?client=gtx&sl=";
    url += configuration.source_language;
    url += "&tl=";
    url += configuration.target_language;
    url += "&dt=t&q=";
    url += percent_encode(source);
    auto parsed = network::Url::parse(url);
    if (!parsed) return std::unexpected(parsed.error());
    return network::HttpRequest {
        .url = std::move(*parsed),
        .method = "GET",
        .headers = {
            {"Accept", "application/json"},
            {"Accept-Language", "vi-VN,vi;q=0.9"},
            {"User-Agent", "phoneME-iOS/translation"},
        },
        .body = {},
        .timeout_ms = 8'000,
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
        state_->configuration.batch_coalescing_delay_ms, 0, 50);
    if (!state_->adapter ||
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
        if (state_->stopped || state_->scheduled.contains(source) ||
            state_->scheduled.size() >=
                state_->configuration.maximum_pending_requests) {
            return nullptr;
        }
        state_->scheduled.insert(source);
        state_->pending.push_back(PendingSource {
            .source = source,
        });
        should_pump = true;
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
        if (state_->stopped || state_->scheduled.contains(source) ||
            state_->scheduled.size() >=
                state_->configuration.maximum_pending_requests) {
            if (!state_->scheduled.contains(source)) {
                state_->waiters.erase(source);
            }
            return std::nullopt;
        }
        state_->scheduled.insert(source);
        state_->pending.push_back(PendingSource {
            .source = source,
        });
        should_pump = true;
    }
    if (should_pump) schedule_pump(state_);
    return std::nullopt;
}

bool TranslationService::contains_han(
    std::span<const char32_t> text) noexcept {
    return std::any_of(text.begin(), text.end(), [](char32_t character) {
        return is_han(static_cast<u32>(character));
    });
}

Result<std::string> TranslationService::parse_google_response(
    std::span<const u8> body) {
    if (body.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "translation response is empty");
    }
    JsonCursor cursor(body);
    if (!cursor.consume('[') || !cursor.consume('[')) {
        return fail(ErrorCode::invalid_argument,
                    "translation response has an unexpected shape");
    }

    std::string translated;
    if (cursor.consume(']')) {
        return fail(ErrorCode::invalid_argument,
                    "translation response has no translated segments");
    }
    for (;;) {
        if (!cursor.consume('[')) {
            return fail(ErrorCode::invalid_argument,
                        "translation response has a malformed segment");
        }
        if (cursor.peek('"')) {
            auto segment = cursor.parse_string();
            if (!segment) return std::unexpected(segment.error());
            translated += *segment;
        } else {
            auto skipped = cursor.skip_value();
            if (!skipped) return std::unexpected(skipped.error());
        }
        while (!cursor.consume(']')) {
            if (!cursor.consume(',')) {
                return fail(ErrorCode::invalid_argument,
                            "translation response has a malformed segment");
            }
            auto skipped = cursor.skip_value();
            if (!skipped) return std::unexpected(skipped.error());
        }
        if (cursor.consume(']')) break;
        if (!cursor.consume(',')) {
            return fail(ErrorCode::invalid_argument,
                        "translation response has malformed segments");
        }
    }
    if (translated.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "translation response produced empty text");
    }
    auto decoded = decode_utf8(translated);
    if (!decoded) return std::unexpected(decoded.error());
    return translated;
}

void TranslationService::schedule_pump(
    const std::shared_ptr<State>& state) {
    i32 delay_ms = 0;
    {
        std::scoped_lock lock(state->mutex);
        if (state->stopped || state->pump_scheduled) return;
        state->pump_scheduled = true;
        delay_ms = state->configuration.batch_coalescing_delay_ms;
    }

    std::thread([state, delay_ms] {
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        pump_requests(state);
    }).detach();
}

void TranslationService::pump_requests(const std::shared_ptr<State>& state) {
    for (;;) {
        std::vector<std::string> sources;
        {
            std::scoped_lock lock(state->mutex);
            state->pump_scheduled = false;
            if (state->stopped ||
                state->in_flight >=
                    state->configuration.maximum_concurrent_requests ||
                state->pending.empty()) {
                return;
            }

            PendingSource first = std::move(state->pending.front());
            state->pending.pop_front();
            usize joined_size = first.source.size();
            const bool may_batch = !first.force_single &&
                first.source.find(';') == std::string::npos;
            sources.push_back(std::move(first.source));

            while (may_batch &&
                   sources.size() <
                       state->configuration.maximum_batch_items &&
                   !state->pending.empty()) {
                const PendingSource& candidate = state->pending.front();
                if (candidate.force_single ||
                    candidate.source.find(';') != std::string::npos) {
                    break;
                }
                const usize next_size = joined_size + 1U +
                    candidate.source.size();
                if (next_size >
                    state->configuration.maximum_batch_source_bytes) {
                    break;
                }
                joined_size = next_size;
                sources.push_back(std::move(state->pending.front().source));
                state->pending.pop_front();
            }
            ++state->in_flight;
        }

        const std::string joined_source = join_batch_sources(sources);
        auto request = make_request(state->configuration, joined_source);
        if (!request) {
            complete_request(state, std::move(sources),
                             std::unexpected(request.error()));
            continue;
        }

        auto callback_sources = sources;
        auto operation = state->adapter->perform_http(
            std::move(*request),
            [state, sources = std::move(callback_sources)](
                Result<network::HttpResponse> response) mutable {
                complete_request(state, std::move(sources),
                                 std::move(response));
            });
        if (!operation) {
            complete_request(state, std::move(sources),
                             std::unexpected(operation.error()));
        }
    }
}

void TranslationService::complete_request(
    const std::shared_ptr<State>& state,
    std::vector<std::string> sources,
    Result<network::HttpResponse> response) {
    std::vector<std::string> translated_values;
    if (response && response->status_code >= 200 &&
        response->status_code < 300) {
        auto translated = parse_google_response(response->body);
        if (translated) {
            if (sources.size() == 1U) {
                translated_values.push_back(std::move(*translated));
            } else if (auto split = split_batch_translation(
                           *translated, sources.size())) {
                translated_values = std::move(*split);
            }
        }
    }

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
                    });
                }
            } else {
                for (const auto& source : sources) {
                    state->scheduled.erase(source);
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
            for (auto& item : completed) {
                state->scheduled.erase(item.source);
                state->retry_after.erase(item.source);
                state->cache.insert_or_assign(item.source, item.cached);
                if (const auto waiters = state->waiters.find(item.source);
                    waiters != state->waiters.end()) {
                    item.completions = std::move(waiters->second);
                    state->waiters.erase(waiters);
                }
            }
            state->generation.fetch_add(
                static_cast<u64>(completed.size()),
                std::memory_order_release);
            stored = true;
        } else {
            for (const auto& source : sources) {
                state->scheduled.erase(source);
                state->waiters.erase(source);
                state->retry_after.insert_or_assign(
                    source,
                    std::chrono::steady_clock::now() + kRetryDelay);
            }
        }
    }

    if (stored) {
        for (auto& item : completed) {
            append_cache_record(*state, item.source, item.translated);
            for (auto& completion : item.completions) {
                if (completion) completion(item.translated);
            }
        }
    }
    pump_requests(state);
}

} // namespace phoneme::translation
