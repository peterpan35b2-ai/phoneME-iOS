#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/translation/TranslationService.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

std::span<const phoneme::u8> bytes(std::string_view text) {
    return {
        reinterpret_cast<const phoneme::u8*>(text.data()),
        text.size(),
    };
}

class CompletionCollector final {
public:
    phoneme::translation::TranslationService::Utf8Completion callback(
        std::string key) {
        return [this, key = std::move(key)](std::string translated) {
            {
                std::scoped_lock lock(mutex_);
                values_.insert_or_assign(key, std::move(translated));
            }
            condition_.notify_all();
        };
    }

    bool wait_for(phoneme::usize count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock,
            std::chrono::seconds(2),
            [this, count] { return values_.size() >= count; });
    }

    std::string value(std::string_view key) {
        std::scoped_lock lock(mutex_);
        const auto found = values_.find(std::string(key));
        return found == values_.end() ? std::string {} : found->second;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<std::string, std::string> values_;
};

class FakeTranslationAdapter final
    : public phoneme::network::AsyncNetworkAdapter {
public:
    enum class Mode {
        normal,
        malformed_batch,
    };

    explicit FakeTranslationAdapter(Mode mode = Mode::normal) : mode_(mode) {}

    std::atomic<int> request_count {0};
    std::atomic<int> batch_request_count {0};

    phoneme::Result<phoneme::network::OperationId> open_stream(
        const phoneme::network::Url&, phoneme::i32,
        phoneme::network::Completion<phoneme::network::NativeConnection>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> open_server(
        const phoneme::network::Url&, phoneme::i32,
        phoneme::network::Completion<phoneme::network::NativeConnection>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> accept(
        phoneme::network::NativeHandle, phoneme::i32,
        phoneme::network::Completion<phoneme::network::NativeConnection>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> open_datagram(
        const phoneme::network::Url&, phoneme::i32,
        phoneme::network::Completion<phoneme::network::NativeConnection>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> read(
        phoneme::network::NativeHandle, phoneme::usize, phoneme::i32,
        phoneme::network::Completion<std::vector<phoneme::u8>>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> write(
        phoneme::network::NativeHandle, std::vector<phoneme::u8>, phoneme::i32,
        phoneme::network::Completion<phoneme::usize>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> available(
        phoneme::network::NativeHandle,
        phoneme::network::Completion<phoneme::usize>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> send_datagram(
        phoneme::network::NativeHandle, phoneme::network::DatagramPacket,
        phoneme::i32, phoneme::network::Completion<phoneme::usize>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> receive_datagram(
        phoneme::network::NativeHandle, phoneme::usize, phoneme::i32,
        phoneme::network::Completion<phoneme::network::DatagramPacket>) override {
        return unsupported();
    }

    phoneme::Result<phoneme::network::OperationId> perform_http(
        phoneme::network::HttpRequest request,
        phoneme::network::Completion<phoneme::network::HttpResponse> completion) override {
        const int current_request = request_count.fetch_add(1) + 1;
        require(request.url.host == "translate.googleapis.com",
                "translation request targets Google Translate");

        const bool is_batch = request.url.query.find("%3B") !=
            std::string::npos;
        if (is_batch) batch_request_count.fetch_add(1);

        std::string translated;
        if (is_batch && mode_ == Mode::malformed_batch) {
            translated = "Đăng nhập vào trò chơi";
        } else if (is_batch) {
            translated = "Đăng nhập vào trò chơi; tự nhiên";
        } else if (request.url.query.find("%E8%87%AA%E7%84%B6") !=
                   std::string::npos) {
            translated = "tự nhiên";
        } else {
            translated = "Đăng nhập vào trò chơi";
        }

        const std::string payload =
            "[[[\"" + translated +
            "\",\"source\",null,null,3]],null,\"zh-CN\"]";
        auto final_url = phoneme::network::Url::parse(
            "https://translate.googleapis.com/");
        require(final_url.has_value(), "fake response URL parses");
        completion(phoneme::network::HttpResponse {
            .final_url = std::move(*final_url),
            .status_code = 200,
            .reason = "OK",
            .body = std::vector<phoneme::u8>(payload.begin(), payload.end()),
        });
        return phoneme::network::OperationId {
            static_cast<phoneme::u64>(current_request),
        };
    }

    phoneme::Result<phoneme::network::OperationId> set_socket_option(
        phoneme::network::NativeHandle, phoneme::network::SocketOption,
        phoneme::i32, phoneme::network::Completion<bool>) override {
        return unsupported();
    }
    phoneme::Result<phoneme::network::OperationId> get_socket_option(
        phoneme::network::NativeHandle, phoneme::network::SocketOption,
        phoneme::network::Completion<phoneme::i32>) override {
        return unsupported();
    }
    phoneme::Status shutdown_output(phoneme::network::NativeHandle) override {
        return {};
    }
    phoneme::Status close(phoneme::network::NativeHandle) override {
        return {};
    }
    phoneme::Status cancel(phoneme::network::OperationId) override {
        return {};
    }

private:
    static phoneme::Result<phoneme::network::OperationId> unsupported() {
        return phoneme::fail(
            phoneme::ErrorCode::unsupported_feature,
            "unused fake network operation");
    }

    Mode mode_;
};

phoneme::translation::TranslationConfiguration test_configuration() {
    return phoneme::translation::TranslationConfiguration {
        .enabled = true,
        .source_language = "auto",
        .target_language = "vi",
        .maximum_pending_requests = 8,
        .maximum_concurrent_requests = 1,
        .maximum_source_bytes = 2'048,
        .maximum_batch_items = 8,
        .maximum_batch_source_bytes = 2'048,
        .batch_coalescing_delay_ms = 10,
    };
}

} // namespace

int main() {
    using phoneme::translation::TranslationService;

    const std::string simple =
        R"([[ ["Xin chào","hello",null,null,10] ],null,"en"])";
    auto simple_result = TranslationService::parse_google_response(
        bytes(simple));
    require(simple_result && *simple_result == "Xin chào",
            "parse one Google Translate segment");

    const std::string multiple =
        R"([[ ["Xin chào ","hello "], ["thế giới","world"] ],null,"en"])";
    auto multiple_result = TranslationService::parse_google_response(
        bytes(multiple));
    require(multiple_result && *multiple_result == "Xin chào thế giới",
            "concatenate Google Translate response segments");

    const std::string escaped =
        R"([[ ["Vi\u1ec7t Nam \ud83c\uddfb\ud83c\uddf3","Vietnam"] ],null,"en"])";
    auto escaped_result = TranslationService::parse_google_response(
        bytes(escaped));
    require(escaped_result && *escaped_result == "Việt Nam 🇻🇳",
            "decode Unicode and surrogate-pair JSON escapes");

    const std::string current_shape =
        R"([[["Đăng nhập vào trò chơi","登录游戏",null,null,3,null,null,[[],[]],[[["af64405095a399ceb1e05c7abb7cda66","zh_en_2023q1.md"]],[["824257d7a249c58caeea06a2e64a25bd","en_vi_2023q1.md"]]]]],null,"zh-CN",null,null,null,1,[],[["zh-CN"],null,[1],["zh-CN"]]])";
    auto current_shape_result = TranslationService::parse_google_response(
        bytes(current_shape));
    require(current_shape_result &&
                *current_shape_result == "Đăng nhập vào trò chơi",
            "parse the current Google Translate response shape");

    const std::string malformed = R"({"translation":"Xin chào"})";
    require(!TranslationService::parse_google_response(bytes(malformed)),
            "reject an unexpected response shape");

    const std::array<char32_t, 4> chinese {{U'登', U'录', U'游', U'戏'}};
    const std::array<char32_t, 5> latin {{U'h', U'e', U'l', U'l', U'o'}};
    require(TranslationService::contains_han(chinese),
            "detect Han text eligible for translation");
    require(!TranslationService::contains_han(latin),
            "skip non-Han Canvas text");

    {
        auto adapter = std::make_shared<FakeTranslationAdapter>();
        TranslationService service(test_configuration(), adapter);
        CompletionCollector collector;

        require(!service.lookup_or_request_utf8(
                    "登录游戏", collector.callback("login")),
                "first bulk lookup renders original login text");
        require(!service.lookup_or_request_utf8(
                    "自然", collector.callback("natural")),
                "first bulk lookup renders original natural text");
        require(collector.wait_for(2),
                "bulk translation callbacks complete");
        require(collector.value("login") == "Đăng nhập vào trò chơi",
                "bulk response maps the first translated segment");
        require(collector.value("natural") == "tự nhiên",
                "bulk response trims and maps the second translated segment");
        require(adapter->request_count.load() == 1,
                "two queued strings use one HTTP request");
        require(adapter->batch_request_count.load() == 1,
                "bulk request joins sources with a semicolon");

        auto cached_login = service.lookup_or_request_utf8("登录游戏");
        auto cached_natural = service.lookup_or_request_utf8("自然");
        require(cached_login && *cached_login == "Đăng nhập vào trò chơi",
                "bulk result caches the first source independently");
        require(cached_natural && *cached_natural == "tự nhiên",
                "bulk result caches the second source independently");
    }

    {
        auto adapter = std::make_shared<FakeTranslationAdapter>(
            FakeTranslationAdapter::Mode::malformed_batch);
        TranslationService service(test_configuration(), adapter);
        CompletionCollector collector;

        (void)service.lookup_or_request_utf8(
            "登录游戏", collector.callback("login"));
        (void)service.lookup_or_request_utf8(
            "自然", collector.callback("natural"));
        require(collector.wait_for(2),
                "malformed bulk response falls back to single requests");
        require(collector.value("login") == "Đăng nhập vào trò chơi",
                "fallback preserves the first source mapping");
        require(collector.value("natural") == "tự nhiên",
                "fallback preserves the second source mapping");
        require(adapter->batch_request_count.load() == 1,
                "fallback attempts the bulk request once");
        require(adapter->request_count.load() == 3,
                "fallback retries each source individually");
    }

    std::cout << "Translation service tests passed\n";
    return 0;
}
