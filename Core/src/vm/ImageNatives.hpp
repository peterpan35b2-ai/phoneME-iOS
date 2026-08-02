#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_image_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
