#pragma once

#include <string>

#include "phoneme/base/Error.hpp"

namespace phoneme::vm {

class Machine;

[[nodiscard]] Status handle_lcdui_action(Machine& machine,
                                         i32 kind,
                                         i32 component_id,
                                         i32 first,
                                         i64 value64,
                                         std::string text);

} // namespace phoneme::vm
