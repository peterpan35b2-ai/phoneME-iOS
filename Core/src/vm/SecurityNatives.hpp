#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_security_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
