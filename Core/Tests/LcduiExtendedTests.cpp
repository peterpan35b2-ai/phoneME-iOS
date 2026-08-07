#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "LcduiNatives.hpp"
#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/vm/LcduiBridge.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MediaEventDispatch.hpp"

namespace phoneme::vm {

// The focused LCDUI test links CoreNatives.cpp, but omits unrelated platform
// modules. These registrations keep the core native table complete without
// pulling network/MMAPI/RMS/filesystem behavior into this test binary.
void register_canvas_natives(NativeMethodRegistry&) {}
void register_connection_natives(NativeMethodRegistry&) {}
void register_file_natives(NativeMethodRegistry&) {}
void register_game_canvas_natives(NativeMethodRegistry&) {}
void register_game_api_natives(NativeMethodRegistry&) {}
void register_media_natives(NativeMethodRegistry&) {}
void register_push_natives(NativeMethodRegistry&) {}
void register_rms_natives(NativeMethodRegistry&) {}
void register_security_natives(NativeMethodRegistry&) {}

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

Status dispatch_media_event(Machine&,
                            ObjectRef,
                            const media::MediaEvent&) {
    return {};
}

Result<i32> file_input_read_one(Machine&, ObjectRef) { return -1; }
Result<i64> file_input_skip(Machine&, ObjectRef, i64) { return 0; }
Result<i32> file_input_available(Machine&, ObjectRef) { return 0; }
Status file_input_close(Machine&, ObjectRef) { return {}; }
Status file_output_write_one(Machine&, ObjectRef, u8) { return {}; }
Status file_output_flush(Machine&, ObjectRef) { return {}; }
Status file_output_close(Machine&, ObjectRef) { return {}; }

} // namespace phoneme::vm

namespace phoneme::network {
std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return {};
}
} // namespace phoneme::network

namespace {

using phoneme::i32;
using phoneme::i64;
using phoneme::vm::Machine;
using phoneme::vm::UiBridgeEvent;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

int invoke_int(Machine& machine,
               const char* name,
               const char* descriptor = "()I") {
    auto result = machine.invoke_static("corefixture/LcduiExtendedOps",
                                        name,
                                        descriptor,
                                        {},
                                        80'000'000);
    if (!result) {
        std::cerr << "VM invoke error for " << name << ": "
                  << result.error().message << '\n';
    }
    require(result.has_value(), "invoke LCDUI fixture method");
    require(result->completed_normally(), "LCDUI fixture completes normally");
    require(result->return_value.has_value(), "LCDUI fixture returns int");
    auto value = result->return_value->as_int();
    require(value.has_value(), "LCDUI fixture return value is int");
    return *value;
}

void invoke_void(Machine& machine, const char* name) {
    auto result = machine.invoke_static("corefixture/LcduiExtendedOps",
                                        name,
                                        "()V",
                                        {},
                                        80'000'000);
    if (!result) {
        std::cerr << "VM invoke error for " << name << ": "
                  << result.error().message << '\n';
    }
    require(result.has_value(), "invoke LCDUI fixture void method");
    require(result->completed_normally(),
            "LCDUI fixture void method completes normally");
}

void perform(Machine& machine,
             i32 kind,
             i32 component_id,
             i32 first = 0,
             i64 value64 = 0,
             std::string text = {}) {
    auto status = phoneme::vm::handle_lcdui_action(
        machine, kind, component_id, first, value64, std::move(text));
    if (!status) {
        std::cerr << "LCDUI action " << kind << " failed: "
                  << status.error().message << '\n';
    }
    require(status.has_value(), "perform LCDUI reverse action");
}

void perform_rejected(Machine& machine,
                      i32 kind,
                      i32 component_id,
                      i32 first,
                      i64 value64,
                      std::string text) {
    auto status = phoneme::vm::handle_lcdui_action(
        machine, kind, component_id, first, value64, std::move(text));
    require(!status.has_value(), "invalid LCDUI reverse action is rejected");
}

i32 item_id(const std::vector<UiBridgeEvent>& events,
            i32 component_type,
            const char* label) {
    for (const auto& event : events) {
        if ((event.kind == 7 || event.kind == 9) &&
            event.component_type == component_type && event.text == label) {
            return event.component_id;
        }
    }
    return 0;
}

i32 latest_item_index(const std::vector<UiBridgeEvent>& events,
                      i32 component_id) {
    for (auto iterator = events.rbegin(); iterator != events.rend(); ++iterator) {
        if ((iterator->kind == 7 || iterator->kind == 8 ||
             iterator->kind == 9) &&
            iterator->component_id == component_id && iterator->index >= 0) {
            return iterator->index;
        }
    }
    return -1;
}

i32 command_id(const std::vector<UiBridgeEvent>& events,
               const char* label) {
    for (auto iterator = events.rbegin(); iterator != events.rend(); ++iterator) {
        if (iterator->kind == 15 && iterator->text == label) {
            return iterator->component_id;
        }
    }
    return 0;
}

i32 dismiss_command_id(const std::vector<UiBridgeEvent>& events) {
    for (auto iterator = events.rbegin(); iterator != events.rend(); ++iterator) {
        if (iterator->kind == 15 && iterator->arguments[0] == 4 &&
            iterator->text.empty()) {
            return iterator->component_id;
        }
    }
    return 0;
}

i32 choice_image_key(const std::vector<UiBridgeEvent>& events,
                     i32 component_id,
                     i32 index) {
    for (const auto& event : events) {
        if (event.kind == 12 && event.component_id == component_id &&
            event.index == index) {
            return event.arguments[3];
        }
    }
    return 0;
}

bool has_image_metadata(const std::vector<UiBridgeEvent>& events,
                        i32 component_id,
                        i32 width,
                        i32 height) {
    for (const auto& event : events) {
        if ((event.kind == 7 || event.kind == 8 || event.kind == 9) &&
            event.component_id == component_id &&
            event.arguments[0] == width && event.arguments[1] == height &&
            event.arguments[2] > 0 && event.arguments[3] == -1004) {
            return true;
        }
    }
    return false;
}

bool has_item_style(const std::vector<UiBridgeEvent>& events,
                    i32 component_id,
                    i32 appearance) {
    for (const auto& event : events) {
        if ((event.kind == 7 || event.kind == 8 || event.kind == 9) &&
            event.component_id == component_id &&
            event.arguments[1] == appearance &&
            event.arguments[3] == -1005) {
            return true;
        }
    }
    return false;
}

bool has_date_metadata(const std::vector<UiBridgeEvent>& events,
                       i32 component_id,
                       i32 bridge_mode,
                       const char* timezone = nullptr) {
    for (const auto& event : events) {
        if ((event.kind == 7 || event.kind == 8 || event.kind == 9) &&
            event.component_id == component_id &&
            event.arguments[1] == bridge_mode &&
            event.arguments[3] == -1003 &&
            !event.detail.empty() &&
            (timezone == nullptr || event.detail == timezone)) {
            return true;
        }
    }
    return false;
}

bool has_alert_metadata(const std::vector<UiBridgeEvent>& events,
                        i32 timeout,
                        i32 component_type,
                        i32 image_width = 0,
                        i32 image_height = 0) {
    for (const auto& event : events) {
        const i32 width = static_cast<i32>(
            static_cast<phoneme::u64>(event.value64) >> 32U);
        const i32 height = static_cast<i32>(
            static_cast<phoneme::u64>(event.value64) & 0xFFFF'FFFFULL);
        if ((event.kind == 2 || event.kind == 3 || event.kind == 4) &&
            event.component_type == component_type &&
            event.arguments[0] == timeout && event.arguments[2] == 3 &&
            event.arguments[3] == -1009 && event.index > 0 &&
            width == image_width &&
            height == image_height) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: LcduiExtendedTests <fixture.jar>");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(argv[1]).has_value(),
            "add LCDUI extended fixture archive");
    Machine machine(classes);
    std::vector<UiBridgeEvent> events;
    machine.configure_ui_bridge(13, [&events](UiBridgeEvent event) {
        events.push_back(std::move(event));
    });

    require(invoke_int(machine, "setup") == 0, "setup LCDUI fixture");
    const i32 text = item_id(events, 15, "Text");
    const i32 numeric = item_id(events, 15, "Numeric");
    const i32 styled = item_id(events, 12, "Styled");
    const i32 gauge = item_id(events, 7, "Gauge");
    const i32 choice = item_id(events, 1, "Choice");
    const i32 date_time = item_id(events, 5, "DateTime");
    const i32 date_only = item_id(events, 5, "Date");
    const i32 time_only = item_id(events, 5, "Time");
    const i32 image = item_id(events, 10, "Image");
    const i32 custom = item_id(events, 4, "Custom");
    const i32 ephemeral = item_id(events, 12, "Ephemeral");
    const i32 stale_command = command_id(events, "Stale");
    const i32 dismiss = dismiss_command_id(events);
    require(text != 0 && numeric != 0 && styled != 0 && gauge != 0 &&
                choice != 0 && date_time != 0 && date_only != 0 &&
                time_only != 0 && image != 0 && custom != 0 &&
                ephemeral != 0,
            "discover all extended item component IDs");
    require(latest_item_index(events, text) == 0 &&
                latest_item_index(events, numeric) == 1 &&
                latest_item_index(events, styled) == 2 &&
                latest_item_index(events, gauge) == 3,
            "Form item events preserve Java insertion order");

    require(has_date_metadata(events, date_time, 3, "GMT+07:00"),
            "DATE_TIME bridge preserves explicit timezone");
    require(has_date_metadata(events, date_only, 2),
            "DATE bridge supplies default timezone");
    require(has_date_metadata(events, time_only, 1),
            "TIME bridge supplies default timezone");
    const i32 choice_image = choice_image_key(events, choice, 0);
    require(choice_image < 0 &&
                ((-static_cast<i64>(choice_image)) >> 8U) == choice,
            "Choice bridge publishes a stable per-element image key");
    require(has_image_metadata(events, image, 2, 3),
            "ImageItem bridge publishes dimensions and image generation");
    require(has_item_style(events, image, 2),
            "ImageItem bridge publishes button appearance separately");
    if (!has_image_metadata(events, custom, 11, 12)) {
        for (const auto& event : events) {
            if (event.component_id == custom) {
                std::cerr << "custom event kind=" << event.kind
                          << " args=" << event.arguments[0] << ','
                          << event.arguments[1] << ','
                          << event.arguments[2] << ','
                          << event.arguments[3] << "\n";
            }
        }
    }
    require(has_image_metadata(events, custom, 11, 12),
            "CustomItem paint bridge publishes rendered image generation");
    require(stale_command != 0, "discover removable screen command ID");
    if (dismiss == 0) {
        for (const auto& event : events) {
            if (event.kind == 15) {
                std::cerr << "command id=" << event.component_id
                          << " type=" << event.arguments[0]
                          << " priority=" << event.arguments[1]
                          << " scope=" << event.arguments[2]
                          << " owner=" << event.arguments[3]
                          << " label='" << event.text << "'\n";
            }
        }
    }
    require(dismiss != 0, "discover Alert.DISMISS_COMMAND ID");
    require(has_alert_metadata(events, -2, 17, 2, 3),
            "FOREVER Alert publishes timeout and image metadata");

    events.clear();
    invoke_void(machine, "insertOrderProbe");
    const i32 order_probe = item_id(events, 12, "OrderProbe");
    require(order_probe != 0 && latest_item_index(events, order_probe) == 1,
            "Form.insert publishes the inserted item index");
    require(latest_item_index(events, numeric) == 2,
            "Form.insert reindexes following items");

    events.clear();
    invoke_void(machine, "deleteOrderProbe");
    require(latest_item_index(events, numeric) == 1,
            "Form.delete restores following item indices");

    perform(machine, 100, dismiss);
    invoke_void(machine, "removeStaleCommand");
    perform_rejected(machine, 100, stale_command, 0, 0, {});
    perform_rejected(machine, 103, numeric - 100'000, 2, 0, "99");
    perform(machine, 103, text, 3, 0, "native-text");
    perform_rejected(machine, 103, numeric, 2, 0, "1x");
    perform(machine, 103, numeric, 3, 0, "-42");
    perform(machine, 105, gauge, 7);
    perform(machine, 104, choice, 1, 1);
    perform(machine, 106, date_time, 0, 1'700'000'000LL);
    perform(machine, 106, date_only, 0, 1'700'086'400LL);
    perform(machine, 106, time_only, 0, 1'700'172'800LL);
    invoke_void(machine, "deleteEphemeral");
    perform_rejected(machine, 101, ephemeral, 0, 0, {});

    events.clear();
    perform(machine, 101, custom);
    std::vector<i32> command_types;
    std::vector<i32> command_scopes;
    for (const auto& event : events) {
        if (event.kind == 15) {
            command_types.push_back(event.arguments[0]);
            command_scopes.push_back(event.arguments[2]);
        }
    }
    require(command_types.size() == 4U,
            "focused CustomItem exposes item and screen commands");
    require(command_types[0] == 8 && command_scopes[0] == 1 &&
                command_types[1] == 4 && command_types[2] == 5 &&
                command_types[3] == 2,
            "MIDP command weights order ITEM, OK, HELP, BACK");
    perform(machine, 102, custom);
    perform(machine, 108, custom, -5, 0);
    perform(machine, 108, custom, -5, 2);
    perform(machine, 108, custom, -5, 1);

    events.clear();
    perform(machine, 107, 0, 42);
    bool scroll_persisted = false;
    for (const auto& event : events) {
        if (event.kind == 3 && event.arguments[2] == 42) {
            scroll_persisted = true;
        }
    }
    require(scroll_persisted, "scroll action stores and republishes position");

    events.clear();
    invoke_void(machine, "updateTicker");
    bool ticker_updated = false;
    for (const auto& event : events) {
        if (event.kind == 3 && event.arguments[3] == -1008 &&
            event.detail == "ticker-two") {
            ticker_updated = true;
        }
    }
    require(ticker_updated, "Ticker update reaches the active screen bridge");

    events.clear();
    invoke_void(machine, "showTimedAlert");
    require(has_alert_metadata(events, 250, 18),
            "finite Alert publishes timeout metadata for host auto-dismiss");
    require(dismiss_command_id(events) == dismiss,
            "Alert.DISMISS_COMMAND remains stable across alerts");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto timed = phoneme::vm::pump_lcdui_alert_timeouts(machine);
    require(timed.has_value(), "finite Alert timeout is pumped successfully");
    bool timed_hidden = false;
    bool form_restored = false;
    for (const auto& event : events) {
        timed_hidden = timed_hidden ||
            (event.kind == 5 && event.text == "Timed");
        form_restored = form_restored ||
            (event.kind == 4 && event.text == "Extended");
    }
    require(timed_hidden && form_restored,
            "finite Alert timeout invokes its sole command and restores Form");

    events.clear();
    invoke_void(machine, "showLoadingThenData");
    bool loading_shown = false;
    bool indicator_shown = false;
    for (const auto& event : events) {
        loading_shown = loading_shown ||
            (event.kind == 4 && event.text == "Loading");
        indicator_shown = indicator_shown ||
            (event.kind == 9 && event.component_type == 6);
    }
    require(loading_shown && indicator_shown,
            "FOREVER loading Alert shows its Gauge indicator");
    auto data_callback = machine.pump_serial_callbacks();
    require(data_callback.has_value(),
            "loading data callback runs through Display.callSerially");
    const auto serial_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (machine.pending_serial_callbacks() != 0U &&
           std::chrono::steady_clock::now() < serial_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto serial_completion = machine.pump_serial_callbacks();
    require(serial_completion.has_value() &&
                machine.pending_serial_callbacks() == 0U,
            "loading data callback finishes before bridge verification");
    bool loading_hidden = false;
    bool indicator_hidden = false;
    bool data_form_restored = false;
    for (const auto& event : events) {
        loading_hidden = loading_hidden ||
            (event.kind == 5 && event.text == "Loading");
        indicator_hidden = indicator_hidden ||
            (event.kind == 10 && event.component_type == 6);
        data_form_restored = data_form_restored ||
            (event.kind == 4 && event.text == "Extended");
    }
    require(loading_hidden && indicator_hidden && data_form_restored,
            "data-ready setCurrent hides loading Alert and indicator");

    const int verification = invoke_int(machine, "verify");
    if (verification != 0) {
        std::cerr << "LCDUI Java verification code: " << verification << '\n';
    }
    require(verification == 0,
            "extended LCDUI Java state and callbacks round-trip correctly");

    std::cout << "LCDUI extended VM tests passed\n";
    return 0;
}
