#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_micro3d_math_natives(NativeMethodRegistry& registry);
void register_micro3d_state_natives(NativeMethodRegistry& registry);
void register_micro3d_resource_natives(NativeMethodRegistry& registry);
void register_micro3d_render_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
