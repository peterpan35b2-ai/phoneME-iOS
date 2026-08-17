#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ConnectionNatives.hpp"
#include "GameApiNatives.hpp"
#include "MediaNatives.hpp"
#include "phoneme/media/MediaService.hpp"
#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/runtime/Runtime.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

// Canvas/graphics integration tests do not exercise networking or MMAPI.
// Keep their registrations linkable while the dedicated modules are omitted.
void register_connection_natives(NativeMethodRegistry&) {}
void register_game_api_natives(NativeMethodRegistry&) {}
void register_media_natives(NativeMethodRegistry&) {}

Status dispatch_media_event(Machine&,
                            ObjectRef,
                            const media::MediaEvent&) {
    return {};
}

Result<std::optional<i32>> connection_stream_read_one(Machine&, ObjectRef) {
    return std::optional<i32> {};
}

Result<std::optional<i32>> connection_stream_read_range(
    Machine&,
    ObjectRef,
    ObjectRef,
    i32,
    i32) {
    return 0;
}

Result<std::optional<usize>> connection_stream_available(Machine&, ObjectRef) {
    return std::optional<usize> {};
}

Result<std::optional<bool>> connection_stream_write_one(Machine&,
                                                        ObjectRef,
                                                        u8) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_write_bytes(
    Machine&,
    ObjectRef,
    std::span<const u8>) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_flush(Machine&, ObjectRef) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_close_input(Machine&, ObjectRef) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_close_output(Machine&, ObjectRef) {
    return std::optional<bool> {};
}

} // namespace phoneme::vm

namespace phoneme::network {

std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return {};
}

} // namespace phoneme::network

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: CanvasGraphicsRuntimeTests <fixture.jar>");

    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(argv[1]).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime for Canvas graphics test");
    require(runtime.configure_keymap({-59, -60, -61, -62,
                                      -20, -21, -22}).has_value(),
            "configure Canvas keymap");
    auto suite_id = runtime.install_jar(argv[1]);
    if (!suite_id) {
        std::cerr << "Canvas fixture install error: "
                  << suite_id.error().message << '\n';
    }
    require(suite_id.has_value(), "install Canvas graphics fixture");
    require(runtime.start_system().has_value(), "start Canvas graphics runtime");

    constexpr phoneme::AppId app_id {431};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasOps",
                                 app_id,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start Canvas graphics MIDlet");

    bool painted = false;
    bool service_repaints_synchronous = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 3 && event->component_type == 22 &&
            event->text == "paint:1") {
            painted = true;
        }
        if (event->kind == 3 && event->component_type == 22 &&
            event->text == "service:1") {
            service_repaints_synchronous = true;
        }
    }
    require(painted, "Canvas.paint(Graphics) executes once after repaint coalescing");
    require(service_repaints_synchronous,
            "Canvas.serviceRepaints blocks until paint completes on the game thread");

    const auto frame = runtime.frame_snapshot();
    require(frame.dimensions.width == 320 && frame.dimensions.height == 240,
            "Canvas framebuffer preserves dimensions");
    require(frame.rgba.size() == 320U * 240U * 4U,
            "Canvas framebuffer publishes exact RGBA byte count");
    // phoneME renders opaque LCDUI colors through the target device's
    // RGB565 display model. 0x123456 therefore expands back to 0x103452.
    if (frame.rgba[0] != 0x10U || frame.rgba[1] != 0x34U ||
        frame.rgba[2] != 0x52U || frame.rgba[3] != 0xFFU) {
        std::cerr << "Canvas first pixel RGBA="
                  << static_cast<int>(frame.rgba[0]) << ','
                  << static_cast<int>(frame.rgba[1]) << ','
                  << static_cast<int>(frame.rgba[2]) << ','
                  << static_cast<int>(frame.rgba[3]) << '\n';
    }
    require(frame.rgba[0] == 0x10U && frame.rgba[1] == 0x34U &&
                frame.rgba[2] == 0x52U && frame.rgba[3] == 0xFFU,
            "Canvas paint uses phoneME-compatible RGB565 rendering");

    runtime.send_pointer(7, 9, 0);
    runtime.send_pointer(8, 10, 1);
    runtime.send_pointer(9, 11, 2);
    bool pointer_up = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 3 && event->component_type == 22 &&
            event->text == "pointerUp:9:11") {
            pointer_up = true;
        }
    }
    require(pointer_up, "Canvas input callbacks remain functional after rendering");

    runtime.send_key(-59, true);
    while (runtime.poll_ui_event()) { }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    (void)runtime.frame_snapshot();
    bool repeated = false;
    while (auto event = runtime.poll_ui_event()) {
        repeated = repeated ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text.rfind("repeat:-59:", 0) == 0);
    }
    require(repeated, "held Canvas key produces keyRepeated callback");
    runtime.send_key(-59, false);
    while (runtime.poll_ui_event()) { }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    (void)runtime.frame_snapshot();
    bool repeated_after_release = false;
    while (auto event = runtime.poll_ui_event()) {
        repeated_after_release = repeated_after_release ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text.rfind("repeat:-59:", 0) == 0);
    }
    require(!repeated_after_release,
            "released Canvas key cancels scheduled repeats");

    require(runtime.pause_midlet(app_id).has_value(),
            "pause Canvas graphics MIDlet");
    while (runtime.poll_ui_event()) { }
    runtime.send_pointer(21, 22, 1);
    bool paused_input_delivered = false;
    while (auto event = runtime.poll_ui_event()) {
        paused_input_delivered = paused_input_delivered ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "pointerDown:21:22");
    }
    require(!paused_input_delivered,
            "paused foreground MIDlet does not receive pointer input");
    require(runtime.resume_midlet(app_id).has_value(),
            "resume Canvas graphics MIDlet");
    require(runtime.app_state(app_id) == phoneme::runtime::AppState::active,
            "resumed Canvas MIDlet returns to active state");
    require(runtime.foreground_app_id() == app_id,
            "resumed Canvas MIDlet remains foreground");
    bool resumed_visible = false;
    while (auto event = runtime.poll_ui_event()) {
        resumed_visible = resumed_visible ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "show");
    }
    require(resumed_visible,
            "resumed Canvas receives showNotify before new input");
    runtime.send_pointer(23, 24, 1);
    bool resumed_input_delivered = false;
    while (auto event = runtime.poll_ui_event()) {
        resumed_input_delivered = resumed_input_delivered ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "pointerDown:23:24");
    }
    require(resumed_input_delivered,
            "resumed foreground MIDlet receives pointer input again");

    require(runtime.destroy_midlet(app_id).has_value(),
            "destroy Canvas graphics MIDlet");

    constexpr phoneme::AppId copy_app_id {432};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasCopyAreaOps",
                                 copy_app_id,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start display copyArea fixture MIDlet");
    (void)runtime.frame_snapshot();
    bool copy_blocked = false;
    std::vector<std::string> copy_events;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 3 && event->component_type == 22) {
            copy_events.push_back(event->text);
            if (event->text == "copyBlocked") {
                copy_blocked = true;
            }
        }
    }
    if (!copy_blocked) {
        std::cerr << "copyArea fixture events:";
        for (const auto& text : copy_events) std::cerr << ' ' << text;
        std::cerr << '\n';
    }
    require(copy_blocked,
            "copyArea throws IllegalStateException on display Graphics");
    const auto copy_frame = runtime.frame_snapshot();
    require(copy_frame.rgba.size() == 320U * 240U * 4U &&
                copy_frame.rgba[0] == 0x00U &&
                copy_frame.rgba[1] == 0xCFU &&
                copy_frame.rgba[2] == 0x63U &&
                copy_frame.rgba[3] == 0xFFU,
            "display Graphics remains usable after rejected copyArea with RGB565 color");
    require(runtime.destroy_midlet(copy_app_id).has_value(),
            "destroy display copyArea fixture MIDlet");

    constexpr phoneme::AppId event_app_id {433};
    auto event_started = runtime.start_midlet(
        *suite_id, "corefixture.CanvasEventOps", event_app_id,
        phoneme::Dimensions {320, 240});
    if (!event_started) {
        std::cerr << "Canvas event start error: "
                  << event_started.error().message << '\n';
    }
    require(event_started.has_value(),
            "start deterministic Canvas event fixture");
    std::vector<std::string> event_titles;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 3 && event->component_type == 22) {
            event_titles.push_back(event->text);
        }
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        (void)runtime.frame_snapshot();
        while (auto event = runtime.poll_ui_event()) {
            if (event->kind == 3 && event->component_type == 22) {
                event_titles.push_back(event->text);
            }
        }
    }
    const auto paint_one = std::find(event_titles.begin(), event_titles.end(),
                                     "eventPaint:1");
    const auto paint_two = std::find(event_titles.begin(), event_titles.end(),
                                     "eventPaint:2");
    require(paint_one != event_titles.end() && paint_two != event_titles.end() &&
                paint_one < paint_two,
            "repaint requested during paint is preserved without recursion deadlock");

    require(runtime.configure_input_capabilities(true, true, false).has_value(),
            "disable repeats while testing duplicate key edges");
    runtime.send_key(-59, true);
    runtime.send_key(-59, true);
    int key_down_count = 0;
    bool early_repeat = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind != 3 || event->component_type != 22) continue;
        key_down_count += event->text == "eventDown:-59" ? 1 : 0;
        early_repeat = early_repeat || event->text == "eventRepeat:-59";
    }
    require(key_down_count == 2 && !early_repeat,
            "duplicate host key-down recovers as a fresh press edge");
    runtime.send_key(-59, false);
    bool released_once = false;
    while (auto event = runtime.poll_ui_event()) {
        released_once = released_once ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventUp:-59");
    }
    require(released_once, "key release edge is delivered once");
    require(runtime.configure_input_capabilities(true, true, true).has_value(),
            "restore repeat capability after duplicate key test");

    runtime.send_pointer(-100, 900, 1);
    runtime.send_pointer(900, -100, 3);
    bool pointer_clipped_down = false;
    bool pointer_clipped_drag = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind != 3 || event->component_type != 22) continue;
        pointer_clipped_down = pointer_clipped_down ||
            event->text == "eventPointerDown:0:239";
        pointer_clipped_drag = pointer_clipped_drag ||
            event->text == "eventPointerDrag:319:0";
    }
    require(pointer_clipped_down && pointer_clipped_drag,
            "pointer coordinates are clipped to the active Canvas bounds");

    require(runtime.set_foreground(event_app_id,
                                   phoneme::Dimensions {240, 320}).has_value(),
            "resize active Canvas deterministically");
    bool resized_event = false;
    while (auto event = runtime.poll_ui_event()) {
        resized_event = resized_event ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventSize:240:320");
    }
    require(resized_event, "active Canvas receives sizeChanged after resize");

    runtime.send_key(-59, true);
    bool pressed_before_suspend = false;
    while (auto event = runtime.poll_ui_event()) {
        pressed_before_suspend = pressed_before_suspend ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventDown:-59");
    }
    require(pressed_before_suspend,
            "Canvas key is held before host presentation suspension");

    runtime.suspend();
    require(runtime.is_suspended(),
            "host presentation enters suspended state");
    bool released_on_suspend = false;
    while (auto event = runtime.poll_ui_event()) {
        released_on_suspend = released_on_suspend ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventUp:-59");
    }
    require(released_on_suspend,
            "host presentation suspension synthesizes keyReleased for held keys");
    require(runtime.app_state(event_app_id) ==
                phoneme::runtime::AppState::active,
            "host presentation suspension keeps MIDlet active");
    runtime.resume();
    require(!runtime.is_suspended(),
            "host presentation resumes without MIDlet lifecycle change");
    bool host_suspend_hide = false;
    bool host_resume_show = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind != 3 || event->component_type != 22) continue;
        host_suspend_hide = host_suspend_hide || event->text == "eventHide";
        host_resume_show = host_resume_show || event->text == "eventShow";
    }
    require(!host_suspend_hide && !host_resume_show,
            "host presentation suspension emits no hide/show lifecycle edge");

    require(runtime.pause_midlet(event_app_id).has_value(),
            "pause deterministic Canvas event fixture");
    bool hidden_event = false;
    while (auto event = runtime.poll_ui_event()) {
        hidden_event = hidden_event ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventHide");
    }
    require(hidden_event, "pause produces one hideNotify edge");
    require(runtime.resume_midlet(event_app_id).has_value(),
            "resume deterministic Canvas event fixture");
    bool shown_event = false;
    while (auto event = runtime.poll_ui_event()) {
        shown_event = shown_event ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventShow");
    }
    require(shown_event, "resume produces one showNotify edge");
    require(runtime.destroy_midlet(event_app_id).has_value(),
            "destroy deterministic Canvas event fixture");

    require(runtime.configure_input_capabilities(false, false, false).has_value(),
            "configure host without pointer/repeat capabilities");
    constexpr phoneme::AppId capability_app_id {434};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasEventOps",
                                 capability_app_id,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start Canvas capability fixture");
    while (runtime.poll_ui_event()) { }
    runtime.send_pointer(5, 6, 1);
    runtime.send_key(-59, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    (void)runtime.frame_snapshot();
    bool pointer_when_disabled = false;
    bool repeat_when_disabled = false;
    while (auto event = runtime.poll_ui_event()) {
        pointer_when_disabled = pointer_when_disabled ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text.rfind("eventPointer", 0) == 0);
        repeat_when_disabled = repeat_when_disabled ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "eventRepeat:-59");
    }
    require(!pointer_when_disabled && !repeat_when_disabled,
            "host capability flags suppress unsupported pointer and repeat events");
    runtime.send_key(-59, false);
    while (runtime.poll_ui_event()) { }
    require(runtime.destroy_midlet(capability_app_id).has_value(),
            "destroy Canvas capability fixture");
    require(runtime.configure_input_capabilities(true, true, true).has_value(),
            "restore default Canvas input capabilities");

    constexpr phoneme::AppId suppress_app_id {436};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasSuppressOps",
                                 suppress_app_id,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start suppressing GameCanvas fixture");
    while (runtime.poll_ui_event()) { }
    runtime.send_key(-1, true);
    runtime.send_key(-1, false);
    runtime.send_key(-6, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    (void)runtime.frame_snapshot();
    runtime.send_key(-6, false);
    runtime.send_key(-7, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    (void)runtime.frame_snapshot();
    runtime.send_key(-7, false);
    bool suppressed_game_callback = false;
    bool soft_left_down = false;
    bool soft_left_repeat = false;
    bool soft_left_up = false;
    bool soft_right_down = false;
    bool soft_right_repeat = false;
    bool soft_right_up = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind != 3 || event->component_type != 22) continue;
        suppressed_game_callback = suppressed_game_callback ||
            event->text.find(":-59") != std::string::npos;
        soft_left_down = soft_left_down ||
            event->text == "suppressDown:-21";
        soft_left_repeat = soft_left_repeat ||
            event->text == "suppressRepeat:-21";
        soft_left_up = soft_left_up ||
            event->text == "suppressUp:-21";
        soft_right_down = soft_right_down ||
            event->text == "suppressDown:-22";
        soft_right_repeat = soft_right_repeat ||
            event->text == "suppressRepeat:-22";
        soft_right_up = soft_right_up ||
            event->text == "suppressUp:-22";
    }
    require(!suppressed_game_callback,
            "GameCanvas suppression blocks mapped game-action callbacks");
    require(soft_left_down && soft_left_repeat && soft_left_up &&
                soft_right_down && soft_right_repeat && soft_right_up,
            "GameCanvas suppression preserves mapped L/R soft-key callbacks");
    require(runtime.destroy_midlet(suppress_app_id).has_value(),
            "destroy suppressing GameCanvas fixture");

    constexpr phoneme::AppId race_app_id {434};
    auto race_started = runtime.start_midlet(
        *suite_id, "corefixture.CanvasRaceOps", race_app_id,
        phoneme::Dimensions {320, 240});
    if (!race_started) {
        std::cerr << "Canvas race start error: "
                  << race_started.error().message << '\n';
    }
    require(race_started.has_value(),
            "start concurrent Canvas repaint fixture");
    for (int iteration = 0; iteration < 2'000; ++iteration) {
        (void)runtime.frame_snapshot();
        if ((iteration & 31) == 0) {
            std::this_thread::yield();
        }
    }
    require(runtime.app_state(race_app_id) ==
                phoneme::runtime::AppState::active,
            "Java repaint thread and host frame pump remain synchronized");
    require(runtime.destroy_midlet(race_app_id).has_value(),
            "destroy concurrent Canvas repaint fixture");

    constexpr phoneme::AppId throw_app_id {435};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasThrowOps",
                                 throw_app_id,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start throwing Canvas callback fixture");
    while (runtime.poll_ui_event()) { }
    require(runtime.set_foreground(phoneme::AppId {},
                                   phoneme::Dimensions {1, 1}).has_value(),
            "host render detach ignores MIDlet hideNotify");
    require(runtime.app_state(throw_app_id) ==
                phoneme::runtime::AppState::active,
            "host render detach leaves the MIDlet active");
    require(runtime.set_foreground(throw_app_id,
                                   phoneme::Dimensions {320, 240}).has_value(),
            "restore a MIDlet after presentation-only detach");
    require(runtime.app_state(throw_app_id) ==
                phoneme::runtime::AppState::active,
            "restored MIDlet remains active after hide and reopen");
    runtime.send_pointer(1, 2, 1);
    require(runtime.app_state(throw_app_id) ==
                phoneme::runtime::AppState::active,
            "uncaught input callback exception is reported without killing "
            "the MIDlet");
    require(runtime.destroy_midlet(throw_app_id).has_value(),
            "forced destroy ignores hideNotify exceptions");
    require(runtime.app_state(throw_app_id) ==
                phoneme::runtime::AppState::destroyed,
            "forced destroy releases a MIDlet with callback failures");

    std::cout << "Canvas graphics runtime tests passed\n";
    return 0;
}
