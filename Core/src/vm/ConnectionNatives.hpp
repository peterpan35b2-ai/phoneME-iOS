#pragma once

#include <optional>

#include "phoneme/base/Error.hpp"
#include "phoneme/vm/Heap.hpp"

namespace phoneme::vm {

class Machine;
class NativeMethodRegistry;

void register_connection_natives(NativeMethodRegistry& registry);

[[nodiscard]] Result<std::optional<i32>>
connection_stream_read_one(Machine& machine, ObjectRef stream);
[[nodiscard]] Result<std::optional<usize>>
connection_stream_available(Machine& machine, ObjectRef stream);
[[nodiscard]] Result<std::optional<bool>>
connection_stream_write_one(Machine& machine, ObjectRef stream, u8 byte);
[[nodiscard]] Result<std::optional<bool>>
connection_stream_flush(Machine& machine, ObjectRef stream);
[[nodiscard]] Result<std::optional<bool>>
connection_stream_close_input(Machine& machine, ObjectRef stream);
[[nodiscard]] Result<std::optional<bool>>
connection_stream_close_output(Machine& machine, ObjectRef stream);

} // namespace phoneme::vm
