#pragma once

#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;
class NativeMethodRegistry;

[[nodiscard]] Result<std::vector<u8>> read_input_stream_all(
    Machine& machine,
    ObjectRef stream,
    usize maximum_bytes);

void register_io_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
