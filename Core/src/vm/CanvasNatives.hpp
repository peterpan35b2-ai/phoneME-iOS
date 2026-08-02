#pragma once

#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

[[nodiscard]] Status initialize_canvas_object(Machine& machine,
                                              ObjectRef canvas,
                                              bool game_canvas,
                                              bool suppress_key_events);
void register_canvas_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
