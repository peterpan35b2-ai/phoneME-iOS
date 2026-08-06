#include "Jdk8CompatNativesParts.hpp"

#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

void register_jdk8_collection_natives(NativeMethodRegistry& registry) {
    register_jdk8_map_natives(registry);
    register_jdk8_set_list_natives(registry);
}

} // namespace phoneme::vm
