#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/network/AsyncNetworkAdapter.hpp"

namespace phoneme::translation {

enum class TranslationProvider : i32 {
    google = 0,
    bing = 1,
    automatic = 2,
};

struct TranslationConfiguration final {
    bool enabled {false};
    TranslationProvider provider {TranslationProvider::automatic};
    std::string source_language {"auto"};
    std::string target_language {"vi"};
    std::string cache_path;
    usize maximum_pending_requests {256U};
    usize maximum_concurrent_requests {2U};
    usize maximum_source_bytes {2'048U};
    usize maximum_batch_items {16U};
    usize maximum_batch_source_bytes {2'048U};
    i32 batch_coalescing_delay_ms {12};
    i32 maximum_batch_wait_ms {32};
};

class TranslationService final {
public:
    using Utf8Completion = std::function<void(std::string)>;

    explicit TranslationService(
        TranslationConfiguration configuration,
        std::shared_ptr<network::AsyncNetworkAdapter> adapter =
            network::make_posix_network_adapter());
    ~TranslationService();

    TranslationService(const TranslationService&) = delete;
    TranslationService& operator=(const TranslationService&) = delete;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] u64 generation() const noexcept;

    // Returns a shared immutable translation when it is already cached. A
    // cache miss schedules one asynchronous request and returns nullptr so the
    // caller can render the original text without blocking the VM thread.
    [[nodiscard]] std::shared_ptr<const std::vector<char32_t>>
    lookup_or_request(std::span<const char32_t> text);

    // UTF-8 counterpart used by native LCDUI bridges and tests.
    [[nodiscard]] std::optional<std::string> lookup_or_request_utf8(
        std::string_view text,
        Utf8Completion completion = {});

    // Queues likely-future text at low priority. Prefetch requests never block
    // or displace text that is already being rendered; a later foreground
    // lookup promotes the same pending source instead of sending it twice.
    void prefetch(std::span<const char32_t> text);
    void prefetch_utf8(std::string_view text);

    [[nodiscard]] static bool contains_translatable_text(
        std::span<const char32_t> text) noexcept;
    [[nodiscard]] static Result<std::string> parse_google_response(
        std::span<const u8> body);
    [[nodiscard]] static Result<std::string> parse_bing_response(
        std::span<const u8> body);

private:
    struct State;

    static void schedule_pump(const std::shared_ptr<State>& state);
    static void pump_requests(const std::shared_ptr<State>& state);
    static void perform_google_request(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        bool allow_provider_fallback);
    static void complete_google_request(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        bool allow_provider_fallback,
        Result<network::HttpResponse> response);
    static void start_bing_request(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        bool force_token_refresh,
        bool allow_token_retry,
        bool allow_provider_fallback);
    static void complete_bing_home_request(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        bool allow_token_retry,
        bool allow_provider_fallback,
        Result<network::HttpResponse> response);
    static void perform_bing_translation(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        bool allow_token_retry,
        bool allow_provider_fallback);
    static void complete_bing_request(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        bool allow_token_retry,
        bool allow_provider_fallback,
        Result<network::HttpResponse> response);
    static void finish_request(
        const std::shared_ptr<State>& state,
        std::vector<std::string> sources,
        std::vector<std::string> translated_values);

    std::shared_ptr<State> state_;
};

} // namespace phoneme::translation
