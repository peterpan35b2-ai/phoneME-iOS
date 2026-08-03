#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_m3g_transform_natives(NativeMethodRegistry& registry);
void register_m3g_scene_natives(NativeMethodRegistry& registry);
void register_m3g_state_natives(NativeMethodRegistry& registry);
void register_m3g_geometry_natives(NativeMethodRegistry& registry);
void register_m3g_render_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
