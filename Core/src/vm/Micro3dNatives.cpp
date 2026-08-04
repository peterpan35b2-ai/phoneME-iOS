#include "Micro3dNatives.hpp"

#include "Micro3dNativeModules.hpp"

namespace phoneme::vm {

void register_micro3d_natives(NativeMethodRegistry& registry) {
    register_micro3d_math_natives(registry);
    register_micro3d_state_natives(registry);
    register_micro3d_resource_natives(registry);
    register_micro3d_render_natives(registry);
}

} // namespace phoneme::vm
