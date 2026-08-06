#pragma once

namespace phoneme::vm {

class NativeMethodRegistry;

void register_jdk8_language_natives(NativeMethodRegistry& registry);
void register_jdk8_collection_natives(NativeMethodRegistry& registry);
void register_jdk8_map_natives(NativeMethodRegistry& registry);
void register_jdk8_set_list_natives(NativeMethodRegistry& registry);
void register_jdk8_utility_natives(NativeMethodRegistry& registry);
void register_jdk8_properties_natives(NativeMethodRegistry& registry);
void register_jdk8_binary_natives(NativeMethodRegistry& registry);
void register_jdk8_stream_natives(NativeMethodRegistry& registry);
void register_jdk8_zip_natives(NativeMethodRegistry& registry);
void register_jdk8_regex_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
