#include "M3gNatives.hpp"

#include "M3gNativeModules.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

void register_m3g_natives(NativeMethodRegistry& registry) {
    register_m3g_transform_natives(registry);
    register_m3g_scene_natives(registry);
    register_m3g_state_natives(registry);
    register_m3g_geometry_natives(registry);
    register_m3g_render_natives(registry);
}

} // namespace phoneme::vm
