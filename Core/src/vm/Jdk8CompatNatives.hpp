#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_jdk8_compat_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
