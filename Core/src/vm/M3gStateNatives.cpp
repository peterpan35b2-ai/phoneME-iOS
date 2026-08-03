#include "M3gNativeModules.hpp"

#include <array>

#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

void register_nullable_reference_property(
    NativeMethodRegistry& registry,
    const char* owner,
    const char* field_owner,
    const char* field_name,
    const char* descriptor,
    const char* setter,
    const char* getter) {
    add(registry, owner, setter, std::string("(") + descriptor + ")V",
        [field_owner, field_name, descriptor](
            Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            auto value = reference_argument(arguments, 1U, field_name, true);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored = set_reference_field(machine, *object, field_owner,
                                              field_name, descriptor, *value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, owner, getter, std::string("()") + descriptor,
        [field_owner, field_name, descriptor](
            Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, field_owner,
                                         field_name, descriptor);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
}

void register_background(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kBackground, "()V",
        [](Machine& machine, ObjectRef object) -> Status {
            auto initialized = initialize_object3d(machine, object);
            if (!initialized) return initialized;
            auto color = set_int_field(machine, object, kBackground,
                                       "color", static_cast<i32>(0xFF000000U));
            auto color_clear = set_int_field(machine, object, kBackground,
                                             "colorClear", 1, "Z");
            auto depth_clear = set_int_field(machine, object, kBackground,
                                             "depthClear", 1, "Z");
            if (!color) return color;
            if (!color_clear) return color_clear;
            return depth_clear;
        });
    register_int_property(registry, kBackground, kBackground, "color",
                          "setColor", "getColor");
    register_nullable_reference_property(registry, kBackground, kBackground,
        "image", "Ljavax/microedition/m3g/Image2D;", "setImage", "getImage");
    add(registry, kBackground, "setImageMode", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Background.setImageMode");
            auto x = int_argument(arguments, 1U, "Background.setImageMode");
            auto y = int_argument(arguments, 2U, "Background.setImageMode");
            if (!object) return std::unexpected(object.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            auto first = set_int_field(machine, *object, kBackground,
                                       "imageModeX", *x);
            auto second = set_int_field(machine, *object, kBackground,
                                        "imageModeY", *y);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    const auto int_getter = [&registry](const char* method,
                                        const char* field_name) {
        add(registry, kBackground, method, "()I",
            [field_name, method](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, method);
                if (!object) return std::unexpected(object.error());
                auto value = int_field(machine, *object, kBackground, field_name);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    int_getter("getImageModeX", "imageModeX");
    int_getter("getImageModeY", "imageModeY");
    add(registry, kBackground, "setCrop", "(IIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Background.setCrop");
            if (!object) return std::unexpected(object.error());
            const std::array<const char*, 4> names {
                "cropX", "cropY", "cropWidth", "cropHeight"};
            for (usize index = 0; index < names.size(); ++index) {
                auto value = int_argument(arguments, index + 1U,
                                          "Background.setCrop");
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(machine, *object, kBackground,
                                            names[index], *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    int_getter("getCropX", "cropX");
    int_getter("getCropY", "cropY");
    int_getter("getCropWidth", "cropWidth");
    int_getter("getCropHeight", "cropHeight");
    register_int_property(registry, kBackground, kBackground, "colorClear",
                          "setColorClearEnable", "isColorClearEnabled", "Z");
    register_int_property(registry, kBackground, kBackground, "depthClear",
                          "setDepthClearEnable", "isDepthClearEnabled", "Z");
}

void register_appearance(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kAppearance, "()V",
        [](Machine& machine, ObjectRef object) -> Status {
            auto initialized = initialize_object3d(machine, object);
            if (!initialized) return initialized;
            auto textures = allocate_array(machine,
                "[Ljavax/microedition/m3g/Texture2D;", 8U,
                Value::from_reference({}));
            if (!textures) return std::unexpected(textures.error());
            return set_reference_field(machine, object, kAppearance,
                "textures", "[Ljavax/microedition/m3g/Texture2D;", *textures);
        });
    register_int_property(registry, kAppearance, kAppearance, "layer",
                          "setLayer", "getLayer");
    register_nullable_reference_property(registry, kAppearance, kAppearance,
        "compositingMode", "Ljavax/microedition/m3g/CompositingMode;",
        "setCompositingMode", "getCompositingMode");
    register_nullable_reference_property(registry, kAppearance, kAppearance,
        "fog", "Ljavax/microedition/m3g/Fog;", "setFog", "getFog");
    register_nullable_reference_property(registry, kAppearance, kAppearance,
        "polygonMode", "Ljavax/microedition/m3g/PolygonMode;",
        "setPolygonMode", "getPolygonMode");
    register_nullable_reference_property(registry, kAppearance, kAppearance,
        "material", "Ljavax/microedition/m3g/Material;",
        "setMaterial", "getMaterial");
    add(registry, kAppearance, "setTexture",
        "(ILjavax/microedition/m3g/Texture2D;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Appearance.setTexture");
            auto index = int_argument(arguments, 1U, "Appearance.setTexture");
            auto texture = reference_argument(arguments, 2U,
                                              "Appearance.setTexture", true);
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            if (!texture) return std::unexpected(texture.error());
            auto textures = reference_field(machine, *object, kAppearance,
                "textures", "[Ljavax/microedition/m3g/Texture2D;");
            if (!textures) return std::unexpected(textures.error());
            auto length = machine.heap().array_length(*textures);
            if (!length || *index < 0 || static_cast<usize>(*index) >= *length) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Appearance texture index is out of range");
            }
            auto stored = machine.heap().set_element(
                *textures, static_cast<usize>(*index),
                Value::from_reference(*texture));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kAppearance, "getTexture",
        "(I)Ljavax/microedition/m3g/Texture2D;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Appearance.getTexture");
            auto index = int_argument(arguments, 1U, "Appearance.getTexture");
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            auto textures = reference_field(machine, *object, kAppearance,
                "textures", "[Ljavax/microedition/m3g/Texture2D;");
            if (!textures) return std::unexpected(textures.error());
            auto length = machine.heap().array_length(*textures);
            if (!length || *index < 0 || static_cast<usize>(*index) >= *length) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Appearance texture index is out of range");
            }
            auto value = machine.heap().element(*textures,
                                                static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
}

void register_compositing(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kCompositingMode, "()V",
        [](Machine& machine, ObjectRef object) -> Status {
            auto initialized = initialize_object3d(machine, object);
            if (!initialized) return initialized;
            for (const char* name : {"alphaWrite", "colorWrite",
                                     "depthWrite", "depthTest"}) {
                auto stored = set_int_field(machine, object, kCompositingMode,
                                            name, 1, "Z");
                if (!stored) return stored;
            }
            return {};
        });
    register_int_property(registry, kCompositingMode, kCompositingMode,
                          "blending", "setBlending", "getBlending");
    register_float_property(registry, kCompositingMode, kCompositingMode,
                            "alphaThreshold", "setAlphaThreshold",
                            "getAlphaThreshold");
    register_int_property(registry, kCompositingMode, kCompositingMode,
                          "alphaWrite", "setAlphaWriteEnable",
                          "isAlphaWriteEnabled", "Z");
    register_int_property(registry, kCompositingMode, kCompositingMode,
                          "colorWrite", "setColorWriteEnable",
                          "isColorWriteEnabled", "Z");
    register_int_property(registry, kCompositingMode, kCompositingMode,
                          "depthWrite", "setDepthWriteEnable",
                          "isDepthWriteEnabled", "Z");
    register_int_property(registry, kCompositingMode, kCompositingMode,
                          "depthTest", "setDepthTestEnable",
                          "isDepthTestEnabled", "Z");
    add(registry, kCompositingMode, "setDepthOffset", "(FF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "CompositingMode.setDepthOffset");
            auto factor = float_argument(arguments, 1U,
                                         "CompositingMode.setDepthOffset");
            auto units = float_argument(arguments, 2U,
                                        "CompositingMode.setDepthOffset");
            if (!object) return std::unexpected(object.error());
            if (!factor) return std::unexpected(factor.error());
            if (!units) return std::unexpected(units.error());
            auto first = set_float_field(machine, *object, kCompositingMode,
                                         "depthOffsetFactor", *factor);
            auto second = set_float_field(machine, *object, kCompositingMode,
                                          "depthOffsetUnits", *units);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    register_float_property(registry, kCompositingMode, kCompositingMode,
                            "depthOffsetFactor", "setDepthOffsetFactorInternal",
                            "getDepthOffsetFactor");
    register_float_property(registry, kCompositingMode, kCompositingMode,
                            "depthOffsetUnits", "setDepthOffsetUnitsInternal",
                            "getDepthOffsetUnits");
}

void register_polygon(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kPolygonMode, "()V",
        [](Machine& machine, ObjectRef object) -> Status {
            auto initialized = initialize_object3d(machine, object);
            if (!initialized) return initialized;
            auto culling = set_int_field(machine, object, kPolygonMode,
                                         "culling", 160);
            auto shading = set_int_field(machine, object, kPolygonMode,
                                         "shading", 165);
            auto winding = set_int_field(machine, object, kPolygonMode,
                                         "winding", 168);
            auto perspective = set_int_field(machine, object, kPolygonMode,
                                             "perspectiveCorrection", 1, "Z");
            if (!culling) return culling;
            if (!shading) return shading;
            if (!winding) return winding;
            return perspective;
        });
    register_int_property(registry, kPolygonMode, kPolygonMode, "culling",
                          "setCulling", "getCulling");
    register_int_property(registry, kPolygonMode, kPolygonMode, "shading",
                          "setShading", "getShading");
    register_int_property(registry, kPolygonMode, kPolygonMode, "winding",
                          "setWinding", "getWinding");
    register_int_property(registry, kPolygonMode, kPolygonMode, "twoSided",
                          "setTwoSidedLightingEnable",
                          "isTwoSidedLightingEnabled", "Z");
    register_int_property(registry, kPolygonMode, kPolygonMode,
                          "localCameraLighting", "setLocalCameraLightingEnable",
                          "isLocalCameraLightingEnabled", "Z");
    register_int_property(registry, kPolygonMode, kPolygonMode,
                          "perspectiveCorrection",
                          "setPerspectiveCorrectionEnable",
                          "isPerspectiveCorrectionEnabled", "Z");
}

[[maybe_unused]] void register_image2d(NativeMethodRegistry& registry) {
    const auto constructor = [&registry](const char* descriptor) {
        add(registry, kImage2D, "<init>", descriptor,
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Image2D.<init>");
                auto format = int_argument(arguments, 1U, "Image2D.<init>");
                if (!object) return std::unexpected(object.error());
                if (!format) return std::unexpected(format.error());
                i32 width = 0;
                i32 height = 0;
                bool mutable_image = false;
                ObjectRef source {};
                if (arguments.size() == 3U) {
                    auto image = reference_argument(arguments, 2U,
                                                    "Image2D.<init>");
                    if (!image) return std::unexpected(image.error());
                    source = *image;
                    auto payload = machine.graphics().image(image->bits);
                    if (payload) {
                        width = (*payload)->width();
                        height = (*payload)->height();
                    }
                } else {
                    auto parsed_width = int_argument(arguments, 2U,
                                                     "Image2D.<init>");
                    auto parsed_height = int_argument(arguments, 3U,
                                                      "Image2D.<init>");
                    if (!parsed_width) return std::unexpected(parsed_width.error());
                    if (!parsed_height) return std::unexpected(parsed_height.error());
                    width = *parsed_width;
                    height = *parsed_height;
                    mutable_image = arguments.size() == 4U;
                    if (arguments.size() >= 5U) {
                        auto bytes = reference_argument(arguments, 4U,
                                                        "Image2D.<init>");
                        if (!bytes) return std::unexpected(bytes.error());
                        source = *bytes;
                    }
                }
                if (width < 0 || height < 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Image2D dimensions are negative");
                }
                const std::array<Status, 5> stored {
                    set_int_field(machine, *object, kImage2D, "format", *format),
                    set_int_field(machine, *object, kImage2D, "width", width),
                    set_int_field(machine, *object, kImage2D, "height", height),
                    set_int_field(machine, *object, kImage2D, "mutable",
                                  mutable_image ? 1 : 0, "Z"),
                    set_reference_field(machine, *object, kImage2D, "source",
                                        "Ljava/lang/Object;", source),
                };
                for (const Status& status : stored) {
                    if (!status) return std::unexpected(status.error());
                }
                return std::optional<Value> {};
            });
    };
    constructor("(ILjava/lang/Object;)V");
    constructor("(III)V");
    constructor("(III[B)V");
    constructor("(III[B[B)V");
    register_int_property(registry, kImage2D, kImage2D, "format",
                          "setFormatInternal", "getFormat");
    register_int_property(registry, kImage2D, kImage2D, "width",
                          "setWidthInternal", "getWidth");
    register_int_property(registry, kImage2D, kImage2D, "height",
                          "setHeightInternal", "getHeight");
    register_int_property(registry, kImage2D, kImage2D, "mutable",
                          "setMutableInternal", "isMutable", "Z");
    add(registry, kImage2D, "set", "(III[B)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
}

[[maybe_unused]] void register_texture2d(NativeMethodRegistry& registry) {
    add(registry, kTexture2D, "<init>",
        "(Ljavax/microedition/m3g/Image2D;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Texture2D.<init>");
            auto image = reference_argument(arguments, 1U, "Texture2D.<init>");
            if (!object) return std::unexpected(object.error());
            if (!image) return std::unexpected(image.error());
            auto initialized = initialize_transformable(machine, *object);
            if (!initialized) return std::unexpected(initialized.error());
            auto stored = set_reference_field(machine, *object, kTexture2D,
                "image", "Ljavax/microedition/m3g/Image2D;", *image);
            if (!stored) return std::unexpected(stored.error());
            auto wrap_s = set_int_field(machine, *object, kTexture2D,
                                        "wrapS", 240);
            auto wrap_t = set_int_field(machine, *object, kTexture2D,
                                        "wrapT", 240);
            if (!wrap_s) return std::unexpected(wrap_s.error());
            if (!wrap_t) return std::unexpected(wrap_t.error());
            return std::optional<Value> {};
        });
    register_reference_property(registry, kTexture2D, kTexture2D, "image",
        "Ljavax/microedition/m3g/Image2D;", "setImage", "getImage");
    register_int_property(registry, kTexture2D, kTexture2D, "blendColor",
                          "setBlendColor", "getBlendColor");
    register_int_property(registry, kTexture2D, kTexture2D, "blending",
                          "setBlending", "getBlending");
    add(registry, kTexture2D, "setFiltering", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Texture2D.setFiltering");
            auto level = int_argument(arguments, 1U, "Texture2D.setFiltering");
            auto image = int_argument(arguments, 2U, "Texture2D.setFiltering");
            if (!object) return std::unexpected(object.error());
            if (!level) return std::unexpected(level.error());
            if (!image) return std::unexpected(image.error());
            auto first = set_int_field(machine, *object, kTexture2D,
                                       "levelFilter", *level);
            auto second = set_int_field(machine, *object, kTexture2D,
                                        "imageFilter", *image);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    register_int_property(registry, kTexture2D, kTexture2D, "levelFilter",
                          "setLevelFilterInternal", "getLevelFilter");
    register_int_property(registry, kTexture2D, kTexture2D, "imageFilter",
                          "setImageFilterInternal", "getImageFilter");
    add(registry, kTexture2D, "setWrapping", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Texture2D.setWrapping");
            auto s = int_argument(arguments, 1U, "Texture2D.setWrapping");
            auto t = int_argument(arguments, 2U, "Texture2D.setWrapping");
            if (!object) return std::unexpected(object.error());
            if (!s) return std::unexpected(s.error());
            if (!t) return std::unexpected(t.error());
            auto first = set_int_field(machine, *object, kTexture2D, "wrapS", *s);
            auto second = set_int_field(machine, *object, kTexture2D, "wrapT", *t);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    register_int_property(registry, kTexture2D, kTexture2D, "wrapS",
                          "setWrappingSInternal", "getWrappingS");
    register_int_property(registry, kTexture2D, kTexture2D, "wrapT",
                          "setWrappingTInternal", "getWrappingT");
}

void register_material(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kMaterial, "()V", initialize_object3d);
    add(registry, kMaterial, "setColor", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Material.setColor");
            auto target = int_argument(arguments, 1U, "Material.setColor");
            auto color = int_argument(arguments, 2U, "Material.setColor");
            if (!object) return std::unexpected(object.error());
            if (!target) return std::unexpected(target.error());
            if (!color) return std::unexpected(color.error());
            const char* field_name = (*target & 0x400) != 0 ? "diffuse"
                : (*target & 0x800) != 0 ? "emissive"
                : (*target & 0x1000) != 0 ? "specular" : "ambient";
            auto stored = set_int_field(machine, *object, kMaterial,
                                        field_name, *color);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kMaterial, "getColor", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Material.getColor");
            auto target = int_argument(arguments, 1U, "Material.getColor");
            if (!object) return std::unexpected(object.error());
            if (!target) return std::unexpected(target.error());
            const char* field_name = (*target & 0x400) != 0 ? "diffuse"
                : (*target & 0x800) != 0 ? "emissive"
                : (*target & 0x1000) != 0 ? "specular" : "ambient";
            auto value = int_field(machine, *object, kMaterial, field_name);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    register_float_property(registry, kMaterial, kMaterial, "shininess",
                            "setShininess", "getShininess");
    register_int_property(registry, kMaterial, kMaterial,
                          "vertexColorTracking",
                          "setVertexColorTrackingEnable",
                          "isVertexColorTrackingEnabled", "Z");
}

void register_fog(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kFog, "()V", initialize_object3d);
    register_int_property(registry, kFog, kFog, "mode",
                          "setMode", "getMode");
    register_int_property(registry, kFog, kFog, "color",
                          "setColor", "getColor");
    register_float_property(registry, kFog, kFog, "density",
                            "setDensity", "getDensity");
    add(registry, kFog, "setLinear", "(FF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Fog.setLinear");
            auto near_distance = float_argument(arguments, 1U, "Fog.setLinear");
            auto far_distance = float_argument(arguments, 2U, "Fog.setLinear");
            if (!object) return std::unexpected(object.error());
            if (!near_distance) return std::unexpected(near_distance.error());
            if (!far_distance) return std::unexpected(far_distance.error());
            auto first = set_float_field(machine, *object, kFog,
                                         "nearDistance", *near_distance);
            auto second = set_float_field(machine, *object, kFog,
                                          "farDistance", *far_distance);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    register_float_property(registry, kFog, kFog, "nearDistance",
                            "setNearDistanceInternal", "getNearDistance");
    register_float_property(registry, kFog, kFog, "farDistance",
                            "setFarDistanceInternal", "getFarDistance");
}

} // namespace

void register_m3g_state_natives(NativeMethodRegistry& registry) {
    register_background(registry);
    register_appearance(registry);
    register_compositing(registry);
    register_polygon(registry);
    register_material(registry);
    register_fog(registry);
}

} // namespace phoneme::vm
