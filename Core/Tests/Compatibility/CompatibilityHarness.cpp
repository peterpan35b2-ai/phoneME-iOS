#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/base/Types.hpp"
#include "phoneme/runtime/Runtime.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Options final {
    std::string jar;
    std::string main_class;
    std::string runtime_home;
    std::string result_path;
    std::string frame_path;
    phoneme::i32 width {320};
    phoneme::i32 height {240};
    phoneme::i32 observe_ms {0};
};

struct HarnessResult final {
    bool configured {false};
    bool installed {false};
    bool system_started {false};
    bool midlet_started {false};
    bool destroyed {false};
    std::string install {"failure"};
    std::string app_state {"unknown"};
    phoneme::i32 exit_code {1};
    phoneme::i64 startup_ms {0};
    phoneme::usize frames_produced {0};
    phoneme::usize nonzero_frame_bytes {0};
    phoneme::usize ui_event_count {0};
    phoneme::usize canvas_event_count {0};
    phoneme::usize lcdui_event_count {0};
    phoneme::i32 error_code {0};
    std::string error_message;
    std::string java_exception_class;
    std::vector<std::string> milestones;
    std::vector<std::string> network_actions;
    std::vector<std::string> media_actions;
};

[[nodiscard]] std::optional<phoneme::i32> parse_i32(std::string_view value) {
    phoneme::i32 parsed = 0;
    const auto result = std::from_chars(value.data(),
                                        value.data() + value.size(),
                                        parsed);
    if (result.ec != std::errc {} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] bool parse_options(int argc,
                                 char** argv,
                                 Options& options,
                                 std::string& error) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--help") {
            error = "usage: CompatibilityHarness --jar FILE --main CLASS "
                    "--runtime-home DIR --result FILE --frame FILE "
                    "[--width N --height N --observe-ms N]";
            return false;
        }
        if (index + 1 >= argc) {
            error = "missing value for " + std::string(argument);
            return false;
        }
        const std::string value {argv[++index]};
        if (argument == "--jar") {
            options.jar = value;
        } else if (argument == "--main") {
            options.main_class = value;
        } else if (argument == "--runtime-home") {
            options.runtime_home = value;
        } else if (argument == "--result") {
            options.result_path = value;
        } else if (argument == "--frame") {
            options.frame_path = value;
        } else if (argument == "--width") {
            const auto parsed = parse_i32(value);
            if (!parsed.has_value() || *parsed <= 0 || *parsed > 8192) {
                error = "invalid width: " + value;
                return false;
            }
            options.width = *parsed;
        } else if (argument == "--height") {
            const auto parsed = parse_i32(value);
            if (!parsed.has_value() || *parsed <= 0 || *parsed > 8192) {
                error = "invalid height: " + value;
                return false;
            }
            options.height = *parsed;
        } else if (argument == "--observe-ms") {
            const auto parsed = parse_i32(value);
            if (!parsed.has_value() || *parsed < 0 || *parsed > 120'000) {
                error = "invalid observe duration: " + value;
                return false;
            }
            options.observe_ms = *parsed;
        } else {
            error = "unknown argument: " + std::string(argument);
            return false;
        }
    }

    if (options.jar.empty() || options.main_class.empty() ||
        options.runtime_home.empty() || options.result_path.empty() ||
        options.frame_path.empty()) {
        error = "--jar, --main, --runtime-home, --result and --frame are required";
        return false;
    }
    return true;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8U);
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20U) {
                static constexpr char kHex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(kHex[(character >> 4U) & 0x0FU]);
                escaped.push_back(kHex[character & 0x0FU]);
            } else {
                escaped.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return escaped;
}

void add_milestone(HarnessResult& result, std::string milestone) {
    if (std::find(result.milestones.begin(),
                  result.milestones.end(),
                  milestone) == result.milestones.end()) {
        result.milestones.push_back(std::move(milestone));
    }
}

[[nodiscard]] std::string app_state_name(phoneme::runtime::AppState state) {
    switch (state) {
    case phoneme::runtime::AppState::none: return "none";
    case phoneme::runtime::AppState::active: return "active";
    case phoneme::runtime::AppState::paused: return "paused";
    case phoneme::runtime::AppState::destroyed: return "destroyed";
    case phoneme::runtime::AppState::error: return "error";
    }
    return "unknown";
}

void capture_error(HarnessResult& result, const phoneme::Error& error) {
    result.error_code = static_cast<phoneme::i32>(error.code);
    result.error_message = error.message;
    result.java_exception_class = error.java_exception_class;
}

void emit_string_array(std::ostream& output,
                       std::string_view name,
                       std::span<const std::string> values,
                       bool trailing_comma) {
    output << "  \"" << name << "\": [";
    for (phoneme::usize index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ", ";
        output << '"' << json_escape(values[index]) << '"';
    }
    output << ']';
    if (trailing_comma) output << ',';
    output << '\n';
}

[[nodiscard]] bool write_result(const Options& options,
                                const HarnessResult& result) {
    std::error_code directory_error;
    const auto parent = std::filesystem::path(options.result_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            std::cerr << "cannot create result directory: "
                      << directory_error.message() << '\n';
            return false;
        }
    }

    std::ofstream output(options.result_path,
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "cannot open result file: " << options.result_path << '\n';
        return false;
    }
    output << "{\n"
           << "  \"configured\": " << (result.configured ? "true" : "false") << ",\n"
           << "  \"installed\": " << (result.installed ? "true" : "false") << ",\n"
           << "  \"system_started\": " << (result.system_started ? "true" : "false") << ",\n"
           << "  \"midlet_started\": " << (result.midlet_started ? "true" : "false") << ",\n"
           << "  \"destroyed\": " << (result.destroyed ? "true" : "false") << ",\n"
           << "  \"install\": \"" << json_escape(result.install) << "\",\n"
           << "  \"app_state\": \"" << json_escape(result.app_state) << "\",\n"
           << "  \"exit_code\": " << result.exit_code << ",\n"
           << "  \"startup_ms\": " << result.startup_ms << ",\n"
           << "  \"frames_produced\": " << result.frames_produced << ",\n"
           << "  \"nonzero_frame_bytes\": " << result.nonzero_frame_bytes << ",\n"
           << "  \"ui_event_count\": " << result.ui_event_count << ",\n"
           << "  \"canvas_event_count\": " << result.canvas_event_count << ",\n"
           << "  \"lcdui_event_count\": " << result.lcdui_event_count << ",\n"
           << "  \"error_code\": " << result.error_code << ",\n"
           << "  \"error_message\": \"" << json_escape(result.error_message) << "\",\n"
           << "  \"java_exception_class\": \""
           << json_escape(result.java_exception_class) << "\",\n";
    emit_string_array(output, "milestones", result.milestones, true);
    emit_string_array(output, "network_actions", result.network_actions, true);
    emit_string_array(output, "media_actions", result.media_actions, false);
    output << "}\n";
    return static_cast<bool>(output);
}

[[nodiscard]] bool write_ppm(const Options& options,
                             const phoneme::runtime::FrameSnapshot& frame,
                             HarnessResult& result) {
    if (frame.dimensions.width <= 0 || frame.dimensions.height <= 0 ||
        frame.rgba.empty()) {
        return false;
    }
    const auto pixel_count =
        static_cast<phoneme::usize>(frame.dimensions.width) *
        static_cast<phoneme::usize>(frame.dimensions.height);
    if (frame.rgba.size() != pixel_count * 4U) {
        return false;
    }

    std::error_code directory_error;
    const auto parent = std::filesystem::path(options.frame_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) return false;
    }
    std::ofstream output(options.frame_path,
                         std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "P6\n" << frame.dimensions.width << ' '
           << frame.dimensions.height << "\n255\n";
    for (phoneme::usize offset = 0; offset < frame.rgba.size(); offset += 4U) {
        const char rgb[] {
            static_cast<char>(frame.rgba[offset]),
            static_cast<char>(frame.rgba[offset + 1U]),
            static_cast<char>(frame.rgba[offset + 2U]),
        };
        output.write(rgb, static_cast<std::streamsize>(sizeof(rgb)));
    }
    if (!output) return false;
    result.frames_produced = std::max<phoneme::usize>(
        result.frames_produced, 1U);
    result.nonzero_frame_bytes = static_cast<phoneme::usize>(std::count_if(
        frame.rgba.begin(), frame.rgba.end(), [](phoneme::u8 value) {
            return value != 0U;
        }));
    add_milestone(result, "frame-produced");
    if (result.nonzero_frame_bytes != 0U) {
        add_milestone(result, "frame-nonblank");
    }
    return true;
}

[[nodiscard]] std::string milestone_component(std::string_view value) {
    std::string component;
    component.reserve(std::min<phoneme::usize>(value.size(), 64U));
    bool pending_separator = false;
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isalnum(character) != 0) {
            if (pending_separator && !component.empty()) component.push_back('-');
            component.push_back(static_cast<char>(std::tolower(character)));
            pending_separator = false;
        } else if (!component.empty()) {
            pending_separator = true;
        }
        if (component.size() >= 64U) break;
    }
    return component;
}

void collect_ui_events(phoneme::runtime::Runtime& runtime,
                       HarnessResult& result) {
    while (auto event = runtime.poll_ui_event()) {
        ++result.ui_event_count;
        add_milestone(result, "ui-event");
        const auto text_component = milestone_component(event->text);
        if (!text_component.empty()) {
            add_milestone(result, "ui-text:" + text_component);
        }
        const auto detail_component = milestone_component(event->detail);
        if (!detail_component.empty()) {
            add_milestone(result, "ui-detail:" + detail_component);
        }
        if (event->component_type == 22) {
            ++result.canvas_event_count;
            add_milestone(result, "canvas-event");
            if (event->kind == 2) add_milestone(result, "canvas-created");
            if (event->kind == 4) add_milestone(result, "canvas-shown");
            if (event->kind == 3 && event->text.starts_with("paint:")) {
                add_milestone(result, "canvas-painted");
            }
        } else {
            ++result.lcdui_event_count;
            add_milestone(result, "lcdui-event");
            if (event->kind == 2) add_milestone(result, "lcdui-component-created");
            if (event->kind == 4) add_milestone(result, "lcdui-screen-shown");
        }
    }
}

[[nodiscard]] int run(const Options& options) {
    HarnessResult result;
    const auto started_at = Clock::now();
    phoneme::runtime::Runtime runtime;

    const auto finish = [&](int code) {
        result.exit_code = static_cast<phoneme::i32>(code);
        if (!write_result(options, result)) return 90;
        return code;
    };

    auto configured = runtime.configure(options.runtime_home);
    if (!configured.has_value()) {
        capture_error(result, configured.error());
        return finish(10);
    }
    result.configured = true;
    add_milestone(result, "runtime-configured");

    auto suite_id = runtime.install_jar(options.jar);
    if (!suite_id.has_value()) {
        capture_error(result, suite_id.error());
        return finish(11);
    }
    result.installed = true;
    result.install = "success";
    add_milestone(result, "jar-installed");

    // The iOS host treats user-imported JARs as trusted compatibility-domain
    // suites before launch. Mirror that production path here so smoke tests
    // reach the MIDlet instead of failing on the first declared network API.
    auto trusted = runtime.set_suite_trust(
        *suite_id, phoneme::security::SuiteTrust::trusted);
    if (!trusted.has_value()) {
        capture_error(result, trusted.error());
        return finish(15);
    }
    add_milestone(result, "suite-trusted");

    auto system_started = runtime.start_system();
    if (!system_started.has_value()) {
        capture_error(result, system_started.error());
        return finish(12);
    }
    result.system_started = true;
    add_milestone(result, "system-started");

    constexpr phoneme::AppId kAppId {17};
    auto midlet_started = runtime.start_midlet(
        *suite_id,
        options.main_class,
        kAppId,
        phoneme::Dimensions {options.width, options.height});
    result.startup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - started_at).count();
    if (!midlet_started.has_value()) {
        capture_error(result, midlet_started.error());
        result.app_state = app_state_name(runtime.app_state(kAppId));
        collect_ui_events(runtime, result);
        if (result.canvas_event_count != 0U) {
            (void)write_ppm(options, runtime.frame_snapshot(), result);
        }
        const auto console_output = runtime.app_console_output(kAppId);
        if (!console_output.empty()) {
            std::cout << console_output;
            std::cout.flush();
        }
        return finish(13);
    }
    result.midlet_started = true;
    add_milestone(result, "midlet-started");
    result.app_state = app_state_name(runtime.app_state(kAppId));
    if (result.app_state == "active") add_milestone(result, "app-active");
    if (result.app_state == "paused") add_milestone(result, "app-paused");
    if (result.app_state == "destroyed") add_milestone(result, "app-self-destroyed");
    // Persist a diagnostic checkpoint so an external timeout can distinguish
    // a launch hang from teardown or frame-collection deadlock.
    (void)write_result(options, result);

    collect_ui_events(runtime, result);
    auto latest_frame = runtime.frame_snapshot();
    if (result.canvas_event_count != 0U) {
        (void)write_ppm(options, latest_frame, result);
    }

    if (options.observe_ms > 0 &&
        runtime.app_state(kAppId) != phoneme::runtime::AppState::destroyed) {
        add_milestone(result, "observation-begin");
        const auto deadline = Clock::now() +
            std::chrono::milliseconds(options.observe_ms);
        phoneme::u64 last_generation = latest_frame.generation;
        while (Clock::now() < deadline &&
               runtime.app_state(kAppId) !=
                   phoneme::runtime::AppState::destroyed) {
            collect_ui_events(runtime, result);
            auto frame = runtime.frame_snapshot();
            if (frame.generation != last_generation) {
                last_generation = frame.generation;
                latest_frame = std::move(frame);
                ++result.frames_produced;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (result.canvas_event_count != 0U && !latest_frame.rgba.empty()) {
            (void)write_ppm(options, latest_frame, result);
        }
        add_milestone(result, "observation-end");
    }

    const auto console_output = runtime.app_console_output(kAppId);
    if (!console_output.empty()) {
        std::cout << console_output;
        std::cout.flush();
    }

    if (runtime.app_state(kAppId) != phoneme::runtime::AppState::destroyed) {
        add_milestone(result, "destroy-begin");
        (void)write_result(options, result);
        auto destroyed = runtime.destroy_midlet(kAppId);
        if (!destroyed.has_value()) {
            capture_error(result, destroyed.error());
            return finish(14);
        }
        result.destroyed = true;
        add_milestone(result, "midlet-destroyed");
    } else {
        result.destroyed = true;
    }
    runtime.stop();
    return finish(0);
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    return run(options);
}
