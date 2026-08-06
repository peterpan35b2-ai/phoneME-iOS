#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::vm {

class BuiltinClassRegistry final {
public:
    using ClassPtr = std::shared_ptr<const classfile::ClassFile>;
    using Factory = ClassPtr (*)(std::string_view internal_name);

    void add_factory(Factory factory);

    [[nodiscard]] ClassPtr find(std::string_view internal_name) const;

private:
    std::vector<Factory> factories_;
};

void register_lang_classes(BuiltinClassRegistry& registry);
void register_io_classes(BuiltinClassRegistry& registry);
void register_file_classes(BuiltinClassRegistry& registry);
void register_connection_classes(BuiltinClassRegistry& registry);
void register_bluetooth_classes(BuiltinClassRegistry& registry);
void register_util_classes(BuiltinClassRegistry& registry);
void register_headless_compat_classes(BuiltinClassRegistry& registry);
void register_jdk8_compat_classes(BuiltinClassRegistry& registry);
void register_lcdui_classes(BuiltinClassRegistry& registry);
void register_game_classes(BuiltinClassRegistry& registry);
void register_m3g_classes(BuiltinClassRegistry& registry);
void register_micro3d_classes(BuiltinClassRegistry& registry);
void register_rms_classes(BuiltinClassRegistry& registry);
void register_security_classes(BuiltinClassRegistry& registry);
void register_midlet_classes(BuiltinClassRegistry& registry);
void register_media_classes(BuiltinClassRegistry& registry);
void register_push_classes(BuiltinClassRegistry& registry);
void register_xml_classes(BuiltinClassRegistry& registry);
void register_vendor_classes(BuiltinClassRegistry& registry);
void register_sensor_classes(BuiltinClassRegistry& registry);
void register_pim_classes(BuiltinClassRegistry& registry);
void register_amms_classes(BuiltinClassRegistry& registry);

} // namespace phoneme::vm
