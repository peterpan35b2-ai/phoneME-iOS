#include "Jdk8CompatNatives.hpp"

#include "Jdk8CompatNativesParts.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

void register_jdk8_compat_natives(NativeMethodRegistry& registry) {
    register_jdk8_language_natives(registry);
    register_jdk8_collection_natives(registry);
    register_jdk8_utility_natives(registry);
}

} // namespace phoneme::vm
