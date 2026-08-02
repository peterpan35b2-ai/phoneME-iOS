#pragma once

#include <optional>

#include "phoneme/base/Error.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;
class NativeMethodRegistry;

void register_file_natives(NativeMethodRegistry& registry);

[[nodiscard]] Result<std::optional<ObjectRef>> try_open_file_connection(
    Machine& machine,
    ObjectRef url,
    i32 mode);
[[nodiscard]] Result<std::optional<ObjectRef>> try_open_file_connection_stream(
    Machine& machine,
    ObjectRef connection,
    bool input,
    bool data);
[[nodiscard]] Result<std::optional<bool>> try_close_file_connection(
    Machine& machine,
    ObjectRef connection);

[[nodiscard]] Result<i32> file_input_read_one(Machine& machine,
                                              ObjectRef stream);
[[nodiscard]] Result<i64> file_input_skip(Machine& machine,
                                          ObjectRef stream,
                                          i64 requested);
[[nodiscard]] Result<i32> file_input_available(Machine& machine,
                                               ObjectRef stream);
[[nodiscard]] Status file_input_close(Machine& machine,
                                      ObjectRef stream);
[[nodiscard]] Status file_output_write_one(Machine& machine,
                                           ObjectRef stream,
                                           u8 byte);
[[nodiscard]] Status file_output_flush(Machine& machine,
                                       ObjectRef stream);
[[nodiscard]] Status file_output_close(Machine& machine,
                                       ObjectRef stream);

} // namespace phoneme::vm
