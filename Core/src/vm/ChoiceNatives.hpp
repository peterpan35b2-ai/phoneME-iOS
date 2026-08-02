#pragma once

#include <optional>

#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

void register_choice_natives(NativeMethodRegistry& registry);
[[nodiscard]] Status emit_choice_elements(Machine& machine, ObjectRef choice);
[[nodiscard]] Status handle_choice_action(Machine& machine,
                                          ObjectRef choice,
                                          i32 element_index,
                                          bool selected);
[[nodiscard]] Result<std::optional<ObjectRef>> implicit_choice_command(
    Machine& machine,
    ObjectRef choice);

} // namespace phoneme::vm
