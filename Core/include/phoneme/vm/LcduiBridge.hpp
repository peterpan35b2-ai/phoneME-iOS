#pragma once

#include <string>

#include "phoneme/base/Error.hpp"

namespace phoneme::vm {

class Machine;

enum class LcduiActionKind : i32 {
    select_command = 100,
    focus_item = 101,
    activate_item = 102,
    set_text = 103,
    set_choice = 104,
    set_gauge = 105,
    set_date = 106,
    set_scroll_position = 107,
    custom_item_key = 108,
    select_list_item_command = 109,
};

enum class CustomItemKeyPhase : i64 {
    pressed = 0,
    released = 1,
    repeated = 2,
};

[[nodiscard]] Status handle_lcdui_action(Machine& machine,
                                         i32 kind,
                                         i32 component_id,
                                         i32 first,
                                         i64 value64,
                                         std::string text);
[[nodiscard]] Status pump_lcdui_alert_timeouts(Machine& machine);
[[nodiscard]] Status replay_current_lcdui(Machine& machine);

} // namespace phoneme::vm
