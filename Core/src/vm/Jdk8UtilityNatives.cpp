#include "Jdk8CompatNativesParts.hpp"

#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {

void register_jdk8_utility_natives(NativeMethodRegistry& registry) {
    register_jdk8_properties_natives(registry);
    register_jdk8_binary_natives(registry);
    register_jdk8_stream_natives(registry);
    register_jdk8_zip_natives(registry);
    register_jdk8_regex_natives(registry);
}

} // namespace phoneme::vm
