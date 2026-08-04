#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_vendor_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
