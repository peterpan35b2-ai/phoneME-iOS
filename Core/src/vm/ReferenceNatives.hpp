#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_reference_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
