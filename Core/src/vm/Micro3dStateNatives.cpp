#include "Micro3dNativeModules.hpp"

#include <array>
#include <span>
#include <utility>

#include "Micro3dNativeSupport.hpp"

namespace phoneme::vm {
namespace micro3d {
namespace {

void register_light(NativeMethodRegistry& registry) {
    m3g::add(registry, kLight, "<init>", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Light.<init>");
            if (!self) return std::unexpected(self.error());
            auto direction = create_vector(machine, {0, 0, 4096});
            if (!direction) return std::unexpected(direction.error());
            auto stored_direction = set_reference_field(
                machine, *self, kLight, "direction",
                "Lcom/mascotcapsule/micro3d/v3/Vector3D;", *direction);
            auto stored_directional = set_int_field(
                machine, *self, kLight, "dirIntensity", 4096);
            auto stored_ambient = set_int_field(
                machine, *self, kLight, "ambIntensity", 0);
            if (!stored_direction) {
                return std::unexpected(stored_direction.error());
            }
            if (!stored_directional) {
                return std::unexpected(stored_directional.error());
            }
            if (!stored_ambient) {
                return std::unexpected(stored_ambient.error());
            }
            return void_result();
        });
    m3g::add(registry, kLight, "<init>",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Light.<init>");
            auto direction = m3g::reference_argument(
                args, 1U, "Light.<init>", false);
            auto directional = m3g::int_argument(args, 2U, "Light.<init>");
            auto ambient = m3g::int_argument(args, 3U, "Light.<init>");
            if (!self) return std::unexpected(self.error());
            if (!direction) return std::unexpected(direction.error());
            if (!directional) return std::unexpected(directional.error());
            if (!ambient) return std::unexpected(ambient.error());
            auto stored_direction = set_reference_field(
                machine, *self, kLight, "direction",
                "Lcom/mascotcapsule/micro3d/v3/Vector3D;", *direction);
            auto stored_directional = set_int_field(
                machine, *self, kLight, "dirIntensity", *directional);
            auto stored_ambient = set_int_field(
                machine, *self, kLight, "ambIntensity", *ambient);
            if (!stored_direction) {
                return std::unexpected(stored_direction.error());
            }
            if (!stored_directional) {
                return std::unexpected(stored_directional.error());
            }
            if (!stored_ambient) {
                return std::unexpected(stored_ambient.error());
            }
            return void_result();
        });

    constexpr std::array<std::pair<const char*, const char*>, 4> getters {{
        {"getAmbientIntensity", "ambIntensity"},
        {"getAmbIntensity", "ambIntensity"},
        {"getDirIntensity", "dirIntensity"},
        {"getParallelLightIntensity", "dirIntensity"},
    }};
    for (const auto& [method_name, field_name] : getters) {
        m3g::add(registry, kLight, method_name, "()I",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Light getter");
                if (!self) return std::unexpected(self.error());
                auto value = int_field(machine, *self, kLight, field_name);
                if (!value) return std::unexpected(value.error());
                return int_result(*value);
            });
    }
    for (const char* method_name :
         {"getDirection", "getParallelLightDirection"}) {
        m3g::add(registry, kLight, method_name,
                 "()Lcom/mascotcapsule/micro3d/v3/Vector3D;",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Light direction getter");
                if (!self) return std::unexpected(self.error());
                auto value = reference_field(
                    machine, *self, kLight, "direction",
                    "Lcom/mascotcapsule/micro3d/v3/Vector3D;");
                if (!value) return std::unexpected(value.error());
                return reference_result(*value);
            });
    }

    constexpr std::array<std::pair<const char*, const char*>, 4> setters {{
        {"setAmbientIntensity", "ambIntensity"},
        {"setAmbIntensity", "ambIntensity"},
        {"setDirIntensity", "dirIntensity"},
        {"setParallelLightIntensity", "dirIntensity"},
    }};
    for (const auto& [method_name, field_name] : setters) {
        m3g::add(registry, kLight, method_name, "(I)V",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Light setter");
                auto value = m3g::int_argument(args, 1U, "Light setter");
                if (!self) return std::unexpected(self.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(
                    machine, *self, kLight, field_name, *value);
                if (!stored) return std::unexpected(stored.error());
                return void_result();
            });
    }
    for (const char* method_name :
         {"setDirection", "setParallelLightDirection"}) {
        m3g::add(registry, kLight, method_name,
                 "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Light direction setter");
                auto value = m3g::reference_argument(
                    args, 1U, "Light direction setter", false);
                if (!self) return std::unexpected(self.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_reference_field(
                    machine, *self, kLight, "direction",
                    "Lcom/mascotcapsule/micro3d/v3/Vector3D;", *value);
                if (!stored) return std::unexpected(stored.error());
                return void_result();
            });
    }
}

[[nodiscard]] Status validate_sphere_texture(
    Machine& machine, ObjectRef texture) {
    if (texture.is_null()) return {};
    auto for_model = int_field(
        machine, texture, kTexture, "isForModel", "Z");
    if (!for_model) return std::unexpected(for_model.error());
    if (*for_model != 0) {
        return fail_java(
            "java/lang/IllegalArgumentException",
            "model texture cannot be used as a sphere texture");
    }
    return {};
}

void register_effect(NativeMethodRegistry& registry) {
    m3g::add(registry, kEffect3D, "<init>", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Effect3D.<init>");
            if (!self) return std::unexpected(self.error());
            auto shading = set_int_field(
                machine, *self, kEffect3D, "shading", 0);
            auto transparency = set_int_field(
                machine, *self, kEffect3D, "isTransparency", 1, "Z");
            if (!shading) return std::unexpected(shading.error());
            if (!transparency) {
                return std::unexpected(transparency.error());
            }
            return void_result();
        });
    m3g::add(registry, kEffect3D, "<init>",
             "(Lcom/mascotcapsule/micro3d/v3/Light;IZ"
             "Lcom/mascotcapsule/micro3d/v3/Texture;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Effect3D.<init>");
            auto light = m3g::reference_argument(
                args, 1U, "Effect3D.<init>", true);
            auto shading = m3g::int_argument(args, 2U, "Effect3D.<init>");
            auto transparency = m3g::int_argument(
                args, 3U, "Effect3D.<init>");
            auto texture = m3g::reference_argument(
                args, 4U, "Effect3D.<init>", true);
            if (!self) return std::unexpected(self.error());
            if (!light) return std::unexpected(light.error());
            if (!shading) return std::unexpected(shading.error());
            if (!transparency) {
                return std::unexpected(transparency.error());
            }
            if (!texture) return std::unexpected(texture.error());
            if ((*shading & ~1) != 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid Micro3D shading type");
            }
            auto valid_texture = validate_sphere_texture(machine, *texture);
            if (!valid_texture) {
                return std::unexpected(valid_texture.error());
            }
            auto stored_light = set_reference_field(
                machine, *self, kEffect3D, "light",
                "Lcom/mascotcapsule/micro3d/v3/Light;", *light);
            auto stored_texture = set_reference_field(
                machine, *self, kEffect3D, "texture",
                "Lcom/mascotcapsule/micro3d/v3/Texture;", *texture);
            auto stored_shading = set_int_field(
                machine, *self, kEffect3D, "shading", *shading);
            auto stored_transparency = set_int_field(
                machine, *self, kEffect3D, "isTransparency",
                *transparency != 0 ? 1 : 0, "Z");
            if (!stored_light) return std::unexpected(stored_light.error());
            if (!stored_texture) {
                return std::unexpected(stored_texture.error());
            }
            if (!stored_shading) {
                return std::unexpected(stored_shading.error());
            }
            if (!stored_transparency) {
                return std::unexpected(stored_transparency.error());
            }
            return void_result();
        });

    m3g::add(registry, kEffect3D, "getLight",
             "()Lcom/mascotcapsule/micro3d/v3/Light;",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Effect3D.getLight");
            if (!self) return std::unexpected(self.error());
            auto value = reference_field(
                machine, *self, kEffect3D, "light",
                "Lcom/mascotcapsule/micro3d/v3/Light;");
            if (!value) return std::unexpected(value.error());
            return reference_result(*value);
        });
    for (const char* method_name : {"getSphereMap", "getSphereTexture"}) {
        m3g::add(registry, kEffect3D, method_name,
                 "()Lcom/mascotcapsule/micro3d/v3/Texture;",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D texture getter");
                if (!self) return std::unexpected(self.error());
                auto value = reference_field(
                    machine, *self, kEffect3D, "texture",
                    "Lcom/mascotcapsule/micro3d/v3/Texture;");
                if (!value) return std::unexpected(value.error());
                return reference_result(*value);
            });
    }

    constexpr std::array<std::pair<const char*, const char*>, 8> getters {{
        {"getShading", "shading"},
        {"getShadingType", "shading"},
        {"getThreshold", "toonThreshold"},
        {"getToonThreshold", "toonThreshold"},
        {"getThresholdHigh", "toonHigh"},
        {"getToonHigh", "toonHigh"},
        {"getThresholdLow", "toonLow"},
        {"getToonLow", "toonLow"},
    }};
    for (const auto& [method_name, field_name] : getters) {
        m3g::add(registry, kEffect3D, method_name, "()I",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D getter");
                if (!self) return std::unexpected(self.error());
                auto value = int_field(
                    machine, *self, kEffect3D, field_name);
                if (!value) return std::unexpected(value.error());
                return int_result(*value);
            });
    }
    for (const char* method_name :
         {"isSemiTransparentEnabled", "isTransparency"}) {
        m3g::add(registry, kEffect3D, method_name, "()Z",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D transparency getter");
                if (!self) return std::unexpected(self.error());
                auto value = int_field(
                    machine, *self, kEffect3D, "isTransparency", "Z");
                if (!value) return std::unexpected(value.error());
                return int_result(*value != 0 ? 1 : 0);
            });
    }

    m3g::add(registry, kEffect3D, "setLight",
             "(Lcom/mascotcapsule/micro3d/v3/Light;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Effect3D.setLight");
            auto light = m3g::reference_argument(
                args, 1U, "Effect3D.setLight", true);
            if (!self) return std::unexpected(self.error());
            if (!light) return std::unexpected(light.error());
            auto stored = set_reference_field(
                machine, *self, kEffect3D, "light",
                "Lcom/mascotcapsule/micro3d/v3/Light;", *light);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    for (const char* method_name : {"setShading", "setShadingType"}) {
        m3g::add(registry, kEffect3D, method_name, "(I)V",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D shading setter");
                auto value = m3g::int_argument(
                    args, 1U, "Effect3D shading setter");
                if (!self) return std::unexpected(self.error());
                if (!value) return std::unexpected(value.error());
                if ((*value & ~1) != 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "invalid Micro3D shading type");
                }
                auto stored = set_int_field(
                    machine, *self, kEffect3D, "shading", *value);
                if (!stored) return std::unexpected(stored.error());
                return void_result();
            });
    }
    for (const char* method_name : {"setSphereMap", "setSphereTexture"}) {
        m3g::add(registry, kEffect3D, method_name,
                 "(Lcom/mascotcapsule/micro3d/v3/Texture;)V",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D texture setter");
                auto texture = m3g::reference_argument(
                    args, 1U, "Effect3D texture setter", true);
                if (!self) return std::unexpected(self.error());
                if (!texture) return std::unexpected(texture.error());
                auto valid = validate_sphere_texture(machine, *texture);
                if (!valid) return std::unexpected(valid.error());
                auto stored = set_reference_field(
                    machine, *self, kEffect3D, "texture",
                    "Lcom/mascotcapsule/micro3d/v3/Texture;", *texture);
                if (!stored) return std::unexpected(stored.error());
                return void_result();
            });
    }
    for (const char* method_name :
         {"setSemiTransparentEnabled", "setTransparency"}) {
        m3g::add(registry, kEffect3D, method_name, "(Z)V",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D transparency setter");
                auto value = m3g::int_argument(
                    args, 1U, "Effect3D transparency setter");
                if (!self) return std::unexpected(self.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(
                    machine, *self, kEffect3D, "isTransparency",
                    *value != 0 ? 1 : 0, "Z");
                if (!stored) return std::unexpected(stored.error());
                return void_result();
            });
    }
    for (const char* method_name : {"setThreshold", "setToonParams"}) {
        m3g::add(registry, kEffect3D, method_name, "(III)V",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Effect3D.setToonParams");
                auto threshold = m3g::int_argument(
                    args, 1U, "Effect3D.setToonParams");
                auto high = m3g::int_argument(
                    args, 2U, "Effect3D.setToonParams");
                auto low = m3g::int_argument(
                    args, 3U, "Effect3D.setToonParams");
                if (!self) return std::unexpected(self.error());
                if (!threshold) return std::unexpected(threshold.error());
                if (!high) return std::unexpected(high.error());
                if (!low) return std::unexpected(low.error());
                if (((*threshold & ~0xff) | (*high & ~0xff) |
                     (*low & ~0xff)) != 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "toon parameters must be in range 0...255");
                }
                auto stored_threshold = set_int_field(
                    machine, *self, kEffect3D, "toonThreshold", *threshold);
                auto stored_high = set_int_field(
                    machine, *self, kEffect3D, "toonHigh", *high);
                auto stored_low = set_int_field(
                    machine, *self, kEffect3D, "toonLow", *low);
                if (!stored_threshold) {
                    return std::unexpected(stored_threshold.error());
                }
                if (!stored_high) return std::unexpected(stored_high.error());
                if (!stored_low) return std::unexpected(stored_low.error());
                return void_result();
            });
    }
}

[[nodiscard]] Status initialize_layout(
    Machine& machine, ObjectRef self, ObjectRef affine,
    i32 scale_x, i32 scale_y, i32 center_x, i32 center_y) {
    if (affine.is_null()) {
        auto created = m3g::allocate_instance(machine, kAffineTrans);
        if (!created) return std::unexpected(created.error());
        auto initialized = write_affine(machine, *created, identity_affine());
        if (!initialized) return initialized;
        affine = *created;
    }
    auto array = m3g::allocate_array(
        machine, "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;", 1U,
        Value::from_reference(ObjectRef {}));
    if (!array) return std::unexpected(array.error());
    auto element = machine.heap().set_element(
        *array, 0U, Value::from_reference(affine));
    if (!element) return element;
    auto stored_array = set_reference_field(
        machine, self, kFigureLayout, "affineArray",
        "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;", *array);
    auto stored_affine = set_reference_field(
        machine, self, kFigureLayout, "affine",
        "Lcom/mascotcapsule/micro3d/v3/AffineTrans;", affine);
    auto stored_x = set_int_field(
        machine, self, kFigureLayout, "scaleX", scale_x);
    auto stored_y = set_int_field(
        machine, self, kFigureLayout, "scaleY", scale_y);
    auto stored_center_x = set_int_field(
        machine, self, kFigureLayout, "centerX", center_x);
    auto stored_center_y = set_int_field(
        machine, self, kFigureLayout, "centerY", center_y);
    auto stored_projection = set_int_field(
        machine, self, kFigureLayout, "projection",
        std::bit_cast<i32>(0x90000000U));
    if (!stored_array) return stored_array;
    if (!stored_affine) return stored_affine;
    if (!stored_x) return stored_x;
    if (!stored_y) return stored_y;
    if (!stored_center_x) return stored_center_x;
    if (!stored_center_y) return stored_center_y;
    return stored_projection;
}

void register_layout(NativeMethodRegistry& registry) {
    m3g::add(registry, kFigureLayout, "<init>", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.<init>");
            if (!self) return std::unexpected(self.error());
            auto stored = initialize_layout(
                machine, *self, {}, 512, 512, 0, 0);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    m3g::add(registry, kFigureLayout, "<init>",
             "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;IIII)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.<init>");
            auto affine = m3g::reference_argument(
                args, 1U, "FigureLayout.<init>", true);
            auto scale_x = m3g::int_argument(args, 2U, "FigureLayout.<init>");
            auto scale_y = m3g::int_argument(args, 3U, "FigureLayout.<init>");
            auto center_x = m3g::int_argument(args, 4U, "FigureLayout.<init>");
            auto center_y = m3g::int_argument(args, 5U, "FigureLayout.<init>");
            if (!self) return std::unexpected(self.error());
            if (!affine) return std::unexpected(affine.error());
            if (!scale_x) return std::unexpected(scale_x.error());
            if (!scale_y) return std::unexpected(scale_y.error());
            if (!center_x) return std::unexpected(center_x.error());
            if (!center_y) return std::unexpected(center_y.error());
            auto stored = initialize_layout(
                machine, *self, *affine, *scale_x, *scale_y,
                *center_x, *center_y);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });

    m3g::add(registry, kFigureLayout, "getAffineTrans",
             "()Lcom/mascotcapsule/micro3d/v3/AffineTrans;",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.getAffineTrans");
            if (!self) return std::unexpected(self.error());
            auto value = reference_field(
                machine, *self, kFigureLayout, "affine",
                "Lcom/mascotcapsule/micro3d/v3/AffineTrans;");
            if (!value) return std::unexpected(value.error());
            return reference_result(*value);
        });
    constexpr std::array<std::pair<const char*, const char*>, 6> getters {{
        {"getCenterX", "centerX"},
        {"getCenterY", "centerY"},
        {"getParallelHeight", "parallelHeight"},
        {"getParallelWidth", "parallelWidth"},
        {"getScaleX", "scaleX"},
        {"getScaleY", "scaleY"},
    }};
    for (const auto& [method_name, field_name] : getters) {
        m3g::add(registry, kFigureLayout, method_name, "()I",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "FigureLayout getter");
                if (!self) return std::unexpected(self.error());
                auto value = int_field(
                    machine, *self, kFigureLayout, field_name);
                if (!value) return std::unexpected(value.error());
                return int_result(*value);
            });
    }

    m3g::add(registry, kFigureLayout, "setAffineTrans",
             "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.setAffineTrans");
            auto affine = m3g::reference_argument(
                args, 1U, "FigureLayout.setAffineTrans", true);
            if (!self) return std::unexpected(self.error());
            if (!affine) return std::unexpected(affine.error());
            ObjectRef value = *affine;
            if (value.is_null()) {
                auto created = m3g::allocate_instance(machine, kAffineTrans);
                if (!created) return std::unexpected(created.error());
                auto identity = write_affine(
                    machine, *created, identity_affine());
                if (!identity) return std::unexpected(identity.error());
                value = *created;
            }
            auto array = reference_field(
                machine, *self, kFigureLayout, "affineArray",
                "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;");
            if (!array) return std::unexpected(array.error());
            if (array->is_null()) {
                auto created_array = m3g::allocate_array(
                    machine,
                    "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;", 1U,
                    Value::from_reference(ObjectRef {}));
                if (!created_array) {
                    return std::unexpected(created_array.error());
                }
                auto element = machine.heap().set_element(
                    *created_array, 0U, Value::from_reference(value));
                if (!element) return std::unexpected(element.error());
                auto stored_array = set_reference_field(
                    machine, *self, kFigureLayout, "affineArray",
                    "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;",
                    *created_array);
                if (!stored_array) {
                    return std::unexpected(stored_array.error());
                }
            }
            auto stored = set_reference_field(
                machine, *self, kFigureLayout, "affine",
                "Lcom/mascotcapsule/micro3d/v3/AffineTrans;", value);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });

    const auto set_array = [](Machine& machine,
                              std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "FigureLayout.setAffineTrans");
        auto array = m3g::reference_argument(
            args, 1U, "FigureLayout.setAffineTrans", false);
        if (!self) return std::unexpected(self.error());
        if (!array) return std::unexpected(array.error());
        auto class_name = machine.heap().class_name(*array);
        auto length = machine.heap().array_length(*array);
        if (!class_name) return std::unexpected(class_name.error());
        if (!length) return std::unexpected(length.error());
        if (*class_name !=
            "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;") {
            return fail_java("java/lang/IllegalArgumentException",
                             "FigureLayout expects AffineTrans[]");
        }
        for (usize index = 0; index < *length; ++index) {
            auto element = machine.heap().element(*array, index);
            if (!element) return std::unexpected(element.error());
            auto reference = element->as_reference();
            if (!reference) return std::unexpected(reference.error());
            if (reference->is_null()) {
                return fail_java(
                    "java/lang/NullPointerException",
                    "FigureLayout AffineTrans[] contains null");
            }
        }
        auto stored = set_reference_field(
            machine, *self, kFigureLayout, "affineArray",
            "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;", *array);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    m3g::add(registry, kFigureLayout, "setAffineTrans",
             "([Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V", set_array);
    m3g::add(registry, kFigureLayout, "setAffineTransArray",
             "([Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V", set_array);

    m3g::add(registry, kFigureLayout, "selectAffineTrans", "(I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.selectAffineTrans");
            auto index = m3g::int_argument(
                args, 1U, "FigureLayout.selectAffineTrans");
            if (!self) return std::unexpected(self.error());
            if (!index) return std::unexpected(index.error());
            auto array = reference_field(
                machine, *self, kFigureLayout, "affineArray",
                "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;");
            if (!array) return std::unexpected(array.error());
            if (array->is_null() || *index < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid AffineTrans selection");
            }
            auto length = machine.heap().array_length(*array);
            if (!length) return std::unexpected(length.error());
            if (static_cast<usize>(*index) >= *length) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid AffineTrans selection");
            }
            auto element = machine.heap().element(
                *array, static_cast<usize>(*index));
            if (!element) return std::unexpected(element.error());
            auto value = element->as_reference();
            if (!value) return std::unexpected(value.error());
            auto stored = set_reference_field(
                machine, *self, kFigureLayout, "affine",
                "Lcom/mascotcapsule/micro3d/v3/AffineTrans;", *value);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });

    m3g::add(registry, kFigureLayout, "setCenter", "(II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.setCenter");
            auto x = m3g::int_argument(args, 1U, "FigureLayout.setCenter");
            auto y = m3g::int_argument(args, 2U, "FigureLayout.setCenter");
            if (!self) return std::unexpected(self.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            auto stored_x = set_int_field(
                machine, *self, kFigureLayout, "centerX", *x);
            auto stored_y = set_int_field(
                machine, *self, kFigureLayout, "centerY", *y);
            if (!stored_x) return std::unexpected(stored_x.error());
            if (!stored_y) return std::unexpected(stored_y.error());
            return void_result();
        });
    m3g::add(registry, kFigureLayout, "setScale", "(II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.setScale");
            auto x = m3g::int_argument(args, 1U, "FigureLayout.setScale");
            auto y = m3g::int_argument(args, 2U, "FigureLayout.setScale");
            if (!self) return std::unexpected(self.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            auto stored_x = set_int_field(
                machine, *self, kFigureLayout, "scaleX", *x);
            auto stored_y = set_int_field(
                machine, *self, kFigureLayout, "scaleY", *y);
            auto projection = set_int_field(
                machine, *self, kFigureLayout, "projection",
                std::bit_cast<i32>(0x90000000U));
            if (!stored_x) return std::unexpected(stored_x.error());
            if (!stored_y) return std::unexpected(stored_y.error());
            if (!projection) return std::unexpected(projection.error());
            return void_result();
        });
    m3g::add(registry, kFigureLayout, "setParallelSize", "(II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.setParallelSize");
            auto width = m3g::int_argument(
                args, 1U, "FigureLayout.setParallelSize");
            auto height = m3g::int_argument(
                args, 2U, "FigureLayout.setParallelSize");
            if (!self) return std::unexpected(self.error());
            if (!width) return std::unexpected(width.error());
            if (!height) return std::unexpected(height.error());
            if (*width < 0 || *height < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "parallel size cannot be negative");
            }
            auto stored_width = set_int_field(
                machine, *self, kFigureLayout, "parallelWidth", *width);
            auto stored_height = set_int_field(
                machine, *self, kFigureLayout, "parallelHeight", *height);
            auto projection = set_int_field(
                machine, *self, kFigureLayout, "projection",
                std::bit_cast<i32>(0x91000000U));
            if (!stored_width) {
                return std::unexpected(stored_width.error());
            }
            if (!stored_height) {
                return std::unexpected(stored_height.error());
            }
            if (!projection) return std::unexpected(projection.error());
            return void_result();
        });
    m3g::add(registry, kFigureLayout, "setPerspective", "(III)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.setPerspective");
            auto near_value = m3g::int_argument(
                args, 1U, "FigureLayout.setPerspective");
            auto far_value = m3g::int_argument(
                args, 2U, "FigureLayout.setPerspective");
            auto angle = m3g::int_argument(
                args, 3U, "FigureLayout.setPerspective");
            if (!self) return std::unexpected(self.error());
            if (!near_value) return std::unexpected(near_value.error());
            if (!far_value) return std::unexpected(far_value.error());
            if (!angle) return std::unexpected(angle.error());
            if (*near_value >= *far_value || *near_value < 1 ||
                *far_value > 32767 || *angle < 1 || *angle > 2047) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid perspective parameters");
            }
            auto stored_near = set_int_field(
                machine, *self, kFigureLayout, "near", *near_value);
            auto stored_far = set_int_field(
                machine, *self, kFigureLayout, "far", *far_value);
            auto stored_angle = set_int_field(
                machine, *self, kFigureLayout, "angle", *angle);
            auto projection = set_int_field(
                machine, *self, kFigureLayout, "projection",
                std::bit_cast<i32>(0x92000000U));
            if (!stored_near) return std::unexpected(stored_near.error());
            if (!stored_far) return std::unexpected(stored_far.error());
            if (!stored_angle) return std::unexpected(stored_angle.error());
            if (!projection) return std::unexpected(projection.error());
            return void_result();
        });
    m3g::add(registry, kFigureLayout, "setPerspective", "(IIII)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "FigureLayout.setPerspective");
            auto near_value = m3g::int_argument(
                args, 1U, "FigureLayout.setPerspective");
            auto far_value = m3g::int_argument(
                args, 2U, "FigureLayout.setPerspective");
            auto width = m3g::int_argument(
                args, 3U, "FigureLayout.setPerspective");
            auto height = m3g::int_argument(
                args, 4U, "FigureLayout.setPerspective");
            if (!self) return std::unexpected(self.error());
            if (!near_value) return std::unexpected(near_value.error());
            if (!far_value) return std::unexpected(far_value.error());
            if (!width) return std::unexpected(width.error());
            if (!height) return std::unexpected(height.error());
            if (*near_value >= *far_value || *near_value < 1 ||
                *far_value > 32767 || *width < 0 || *height < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid perspective parameters");
            }
            auto stored_near = set_int_field(
                machine, *self, kFigureLayout, "near", *near_value);
            auto stored_far = set_int_field(
                machine, *self, kFigureLayout, "far", *far_value);
            auto stored_width = set_int_field(
                machine, *self, kFigureLayout, "perspectiveWidth", *width);
            auto stored_height = set_int_field(
                machine, *self, kFigureLayout, "perspectiveHeight", *height);
            auto projection = set_int_field(
                machine, *self, kFigureLayout, "projection",
                std::bit_cast<i32>(0x93000000U));
            if (!stored_near) return std::unexpected(stored_near.error());
            if (!stored_far) return std::unexpected(stored_far.error());
            if (!stored_width) return std::unexpected(stored_width.error());
            if (!stored_height) {
                return std::unexpected(stored_height.error());
            }
            if (!projection) return std::unexpected(projection.error());
            return void_result();
        });
}

} // namespace
} // namespace micro3d

void register_micro3d_state_natives(NativeMethodRegistry& registry) {
    micro3d::register_light(registry);
    micro3d::register_effect(registry);
    micro3d::register_layout(registry);
}

} // namespace phoneme::vm
