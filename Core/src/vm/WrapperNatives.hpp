#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_wrapper_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
