#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_push_natives(NativeMethodRegistry &registry);

} // namespace phoneme::vm
