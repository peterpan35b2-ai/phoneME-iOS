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
#include <thread>
#include <unordered_map>
#include <vector>

#include "phoneme/translation/TranslationService.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"

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
        } else if (is_batch && request.url.query.find(
                       "%E5%95%86%E5%BA%97") != std::string::npos) {
            translated = "Đăng nhập vào trò chơi; tự nhiên; cửa hàng";
        } else if (is_batch) {
            translated = "Đăng nhập vào trò chơi; tự nhiên";
        } else if (request.url.query.find("%E5%95%86%E5%BA%97") !=
                   std::string::npos) {
            translated = "cửa hàng";
        } else if (request.url.query.find("%E8%87%AA%E7%84%B6") !=
                   std::string::npos) {
            translated = "tự nhiên";
        } else if (request.url.query.find("q=hello") != std::string::npos) {
            translated = "xin chào";
        } else if (request.url.query.find(
                       "%E3%82%B2%E3%83%BC%E3%83%A0%E9%96%8B%E5%A7%8B") !=
                   std::string::npos) {
            translated = "Bắt đầu trò chơi";
        } else {
            translated = "Đăng nhập vào trò chơi";
        }

        const std::string payload =
            "[[[\"" + translated +
            "\",\"source\",null,null,3]],null,\"zh-CN\"]";
        auto final_url = phoneme::network::Url::parse(
            "https://translate.googleapis.com/");
        require(final_url.has_value(), "fake response URL parses");
        phoneme::network::HttpResponse response {
            .final_url = std::move(*final_url),
            .status_code = 200,
            .reason = "OK",
            .body = std::vector<phoneme::u8>(payload.begin(), payload.end()),
        };
        std::thread worker([
            completion = std::move(completion),
            response = std::move(response)
        ]() mutable {
            completion(std::move(response));
        });
        worker.join();
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
        .maximum_batch_wait_ms = 30,
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
    const std::array<char32_t, 4> japanese {{U'ゲ', U'ー', U'ム', U'開'}};
    const std::array<char32_t, 4> numeric {{U'1', U'0', U'0', U'%'}};
    require(TranslationService::contains_translatable_text(chinese),
            "detect Chinese text eligible for auto translation");
    require(TranslationService::contains_translatable_text(latin),
            "detect Latin text eligible for auto translation");
    require(TranslationService::contains_translatable_text(japanese),
            "detect Japanese text eligible for auto translation");
    require(!TranslationService::contains_translatable_text(numeric),
            "skip numeric and symbol-only Canvas text");

    {
        auto adapter = std::make_shared<FakeTranslationAdapter>();
        TranslationService service(test_configuration(), adapter);
        CompletionCollector collector;

        require(!service.lookup_or_request_utf8("https://example.com/login"),
                "skip URL text during automatic translation");
        require(adapter->request_count.load() == 0,
                "URL filtering avoids a translation request");

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
        auto adapter = std::make_shared<FakeTranslationAdapter>();
        auto configuration = test_configuration();
        configuration.batch_coalescing_delay_ms = 40;
        configuration.maximum_batch_wait_ms = 120;
        TranslationService service(configuration, adapter);
        CompletionCollector collector;

        (void)service.lookup_or_request_utf8(
            "登录游戏", collector.callback("login"));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        (void)service.lookup_or_request_utf8(
            "自然", collector.callback("natural"));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        (void)service.lookup_or_request_utf8(
            "商店", collector.callback("store"));
        require(collector.wait_for(3),
                "screen debounce waits for the final text burst");
        require(collector.value("store") == "cửa hàng",
                "debounced screen batch maps the final source");
        require(adapter->request_count.load() == 1,
                "one stable screen uses one translation request");
        require(adapter->batch_request_count.load() == 1,
                "screen debounce keeps all same-script text in one batch");
    }

    {
        auto adapter = std::make_shared<FakeTranslationAdapter>();
        TranslationService service(test_configuration(), adapter);
        CompletionCollector collector;

        (void)service.lookup_or_request_utf8(
            "登录游戏", collector.callback("login"));
        (void)service.lookup_or_request_utf8(
            "hello", collector.callback("english"));
        (void)service.lookup_or_request_utf8(
            "自然", collector.callback("natural"));
        require(collector.wait_for(3),
                "interleaved scripts complete without losing mappings");
        require(adapter->request_count.load() == 2,
                "interleaved same-script text is regrouped into two requests");
        require(adapter->batch_request_count.load() == 1,
                "Chinese text is bulked across an interleaved Latin source");
    }

    {
        auto adapter = std::make_shared<FakeTranslationAdapter>();
        TranslationService service(test_configuration(), adapter);
        CompletionCollector collector;

        (void)service.lookup_or_request_utf8(
            "hello", collector.callback("english"));
        (void)service.lookup_or_request_utf8(
            "ゲーム開始", collector.callback("japanese"));
        require(collector.wait_for(2),
                "automatic translation handles multiple source scripts");
        require(collector.value("english") == "xin chào",
                "automatic translation handles Latin text");
        require(collector.value("japanese") == "Bắt đầu trò chơi",
                "automatic translation handles Japanese text");
        require(adapter->request_count.load() == 2,
                "different scripts use separate auto-detection requests");
        require(adapter->batch_request_count.load() == 0,
                "different scripts are never mixed into one bulk request");
    }

    {
        auto adapter = std::make_shared<FakeTranslationAdapter>();
        auto service = std::make_shared<TranslationService>(
            test_configuration(), adapter);
        phoneme::vm::ClassRepository classes;
        phoneme::vm::Machine machine(classes);
        machine.configure_translation_service(service);

        machine.begin_character_translation_frame();
        const auto first_original = machine.translate_draw_character(
            7U, U'登', 10, 20, 0, 0, 0, 8);
        const auto second_original = machine.translate_draw_character(
            7U, U'录', 18, 20, 0, 0, 0, 8);
        require(first_original.action ==
                    phoneme::vm::Machine::CharacterTranslationDecision::Action::
                        draw_original &&
                second_original.action ==
                    phoneme::vm::Machine::CharacterTranslationDecision::Action::
                        draw_original,
                "fragmented characters render original text on first frame");
        machine.end_character_translation_frame();

        const std::array<char32_t, 2> fragmented_source {{U'登', U'录'}};
        std::shared_ptr<const std::vector<char32_t>> cached;
        for (int attempt = 0; attempt < 100 && !cached; ++attempt) {
            cached = service->lookup_or_request(fragmented_source);
            if (!cached) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        require(cached != nullptr,
                "fragmented character run is translated as one phrase");

        machine.begin_character_translation_frame();
        const auto first_translated = machine.translate_draw_character(
            7U, U'登', 12, 22, 0, 0, 0, 8);
        const auto second_suppressed = machine.translate_draw_character(
            7U, U'录', 20, 22, 0, 0, 0, 8);
        require(first_translated.action ==
                    phoneme::vm::Machine::CharacterTranslationDecision::Action::
                        draw_translation &&
                first_translated.translated != nullptr,
                "next frame draws the complete translated phrase once");
        require(second_suppressed.action ==
                    phoneme::vm::Machine::CharacterTranslationDecision::Action::
                        suppress,
                "remaining source characters are suppressed after phrase draw");
        machine.end_character_translation_frame();

        constexpr std::u32string_view first_line =
            U"Đăng nhập vào trò chơi bằng tài khoản của bạn";
        constexpr std::u32string_view second_line =
            U"Tiếp tục cuộc hội thoại dài phía dưới";
        const auto capture_layout = [&](std::u32string_view text,
                                        phoneme::i32 y) {
            return machine.plan_translated_text(
                7U,
                std::span<const char32_t>(text.data(), text.size()),
                6,
                y,
                0,
                0,
                0,
                8,
                0,
                0,
                80,
                60,
                0,
                0);
        };

        machine.begin_character_translation_frame();
        require(!capture_layout(first_line, 8).planned &&
                    !capture_layout(second_line, 22).planned,
                "first translated paragraph frame captures its text blocks");
        machine.end_character_translation_frame();

        machine.begin_character_translation_frame();
        const auto first_layout = capture_layout(first_line, 8);
        const auto second_layout = capture_layout(second_line, 22);
        require(first_layout.planned && second_layout.planned,
                "next frame replays translated paragraph positions");
        require(first_layout.y + first_layout.height < second_layout.y,
                "translated text blocks are moved without overlap");
        require(first_layout.y != 8 || second_layout.y != 22,
                "overlapping translated text is shifted vertically");
        machine.end_character_translation_frame();

        constexpr std::u32string_view threshold_text = U"Một\nhai";
        const auto above_quarter = machine.plan_translated_text(
            7U,
            std::span<const char32_t>(threshold_text.data(),
                                     threshold_text.size()),
            6,
            29,
            0,
            0,
            0,
            8,
            0,
            0,
            120,
            120,
            0,
            0);
        const auto at_quarter = machine.plan_translated_text(
            7U,
            std::span<const char32_t>(threshold_text.data(),
                                     threshold_text.size()),
            6,
            30,
            0,
            0,
            0,
            8,
            0,
            0,
            120,
            120,
            0,
            0);
        require(above_quarter.y == 29 && at_quarter.y == 30,
                "middle text keeps its source position without quarter zones");
        require(above_quarter.height == at_quarter.height,
                "layout does not change at a fractional screen threshold");

        constexpr std::u32string_view lower_first_line =
            U"Đăng nhập\nvào trò chơi ngay";
        constexpr std::u32string_view lower_second_line = U"Tiếp tục";
        const auto capture_lower_layout = [&](std::u32string_view text,
                                              phoneme::i32 y) {
            return machine.plan_translated_text(
                7U,
                std::span<const char32_t>(text.data(), text.size()),
                6,
                y,
                0,
                0,
                0,
                8,
                0,
                0,
                120,
                110,
                0,
                0);
        };

        machine.begin_character_translation_frame();
        require(!capture_lower_layout(lower_first_line, 60).planned &&
                    !capture_lower_layout(lower_second_line, 74).planned,
                "first lower dialogue frame captures text with room below");
        machine.end_character_translation_frame();

        machine.begin_character_translation_frame();
        const auto lower_first = capture_lower_layout(lower_first_line, 60);
        const auto lower_second = capture_lower_layout(lower_second_line, 74);
        require(lower_first.planned && lower_second.planned,
                "lower dialogue layout replays on the next frame");
        require(lower_first.y == 60,
                "first lower text block keeps its source position");
        require(lower_second.y > 74,
                "overlapping lower text is pushed downward");
        require(lower_first.y + lower_first.height < lower_second.y,
                "lower text blocks remain independent and do not overlap");
        require(lower_second.y + lower_second.height <= 110,
                "downward-shifted text remains inside the clip");
        machine.end_character_translation_frame();

        constexpr std::u32string_view overflowing_lower_dialogue =
            U"Đây là một đoạn hội thoại dài cần nhiều dòng để hiển thị "
            U"nhưng vẫn phải tận dụng toàn bộ khoảng trống phía dưới";
        const auto capture_overflowing_lower_layout = [&] {
            return machine.plan_translated_text(
                7U,
                std::span<const char32_t>(overflowing_lower_dialogue.data(),
                                          overflowing_lower_dialogue.size()),
                6,
                82,
                0,
                0,
                0,
                8,
                0,
                0,
                120,
                110,
                0,
                0);
        };

        machine.begin_character_translation_frame();
        require(!capture_overflowing_lower_layout().planned,
                "first overflowing lower dialogue frame captures its layout");
        machine.end_character_translation_frame();

        machine.begin_character_translation_frame();
        const auto overflowing_lower = capture_overflowing_lower_layout();
        require(overflowing_lower.planned && overflowing_lower.y < 82,
                "overflowing lower dialogue moves upward only when required");
        require(overflowing_lower.y + overflowing_lower.height == 110,
                "upward-shifted text reuses the full clip height");
        machine.end_character_translation_frame();
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
