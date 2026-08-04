#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_headless_compat_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
