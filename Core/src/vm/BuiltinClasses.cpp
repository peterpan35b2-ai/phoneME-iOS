#include "phoneme/vm/BuiltinClasses.hpp"

#include <string>

#include "phoneme/vm/BuiltinClassRegistry.hpp"

namespace phoneme::vm {
namespace {

[[nodiscard]] const BuiltinClassRegistry& builtin_registry() {
    static const BuiltinClassRegistry registry = [] {
        BuiltinClassRegistry result;
        register_lang_classes(result);
        register_io_classes(result);
        register_file_classes(result);
        register_connection_classes(result);
        register_bluetooth_classes(result);
        register_util_classes(result);
        register_headless_compat_classes(result);
        register_jdk8_compat_classes(result);
        register_midlet_classes(result);
        register_media_classes(result);
        register_push_classes(result);
        register_rms_classes(result);
        register_security_classes(result);
        register_lcdui_classes(result);
        register_game_classes(result);
        register_m3g_classes(result);
        register_micro3d_classes(result);
        register_xml_classes(result);
        register_vendor_classes(result);
        register_sensor_classes(result);
        register_pim_classes(result);
        register_amms_classes(result);
        return result;
    }();
    return registry;
}

} // namespace

bool is_builtin_class(std::string_view internal_name) noexcept {
    return builtin_registry().find(internal_name) != nullptr;
}

Result<std::shared_ptr<const classfile::ClassFile>> load_builtin_class(
    std::string_view internal_name) {
    auto built = builtin_registry().find(internal_name);
    if (built == nullptr) {
        return fail(ErrorCode::class_not_found,
                    "class is not implemented by the standalone C++ runtime: " +
                        std::string(internal_name));
    }
    return built;
}

} // namespace phoneme::vm
