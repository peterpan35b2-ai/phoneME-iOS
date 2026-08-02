#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_io_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
