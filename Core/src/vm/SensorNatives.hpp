#pragma once

#include <string_view>

#include "phoneme/base/Error.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;
class NativeMethodRegistry;

void register_sensor_natives(NativeMethodRegistry& registry);

[[nodiscard]] Result<ObjectRef> open_sensor_connection(
    Machine& machine,
    ObjectRef url,
    std::string_view text,
    i32 mode);

} // namespace phoneme::vm
