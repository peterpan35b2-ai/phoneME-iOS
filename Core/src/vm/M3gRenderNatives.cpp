#include "M3gNativeModules.hpp"

#include <array>
#include <span>
#include <string>
#include <vector>

#include "M3gLoader.hpp"
#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

[[nodiscard]] Status require_bound_target(Machine& machine,
                                          ObjectRef graphics3d) {
    auto target = reference_field(machine, graphics3d, kGraphics3D,
                                  "target", "Ljava/lang/Object;");
    if (!target) return std::unexpected(target.error());
    if (target->is_null()) {
        return fail_java("java/lang/IllegalStateException",
                         "Graphics3D has no bound target");
    }
    return {};
}

void register_graphics3d(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kGraphics3D, "()V");
    add(registry, kGraphics3D, "getInstance",
        "()Ljavax/microedition/m3g/Graphics3D;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto location = field_location(machine, kGraphics3D, "INSTANCE",
                                           "Ljavax/microedition/m3g/Graphics3D;",
                                           true);
            if (!location) return std::unexpected(location.error());
            auto current = machine.class_states().static_field(*location);
            if (!current) return std::unexpected(current.error());
            auto instance = current->as_reference();
            if (!instance) return std::unexpected(instance.error());
            if (instance->is_null()) {
                auto allocated = allocate_instance(machine, kGraphics3D);
                if (!allocated) return std::unexpected(allocated.error());
                const Dimensions dimensions = machine.canvas_bridge() != nullptr
                    ? machine.canvas_bridge()->display_dimensions()
                    : Dimensions {320, 240};
                auto width = set_int_field(machine, *allocated, kGraphics3D,
                                           "viewportWidth", dimensions.width);
                auto height = set_int_field(machine, *allocated, kGraphics3D,
                                            "viewportHeight", dimensions.height);
                auto near_value = set_float_field(machine, *allocated, kGraphics3D,
                                                  "depthNear", 0.0F);
                auto far_value = set_float_field(machine, *allocated, kGraphics3D,
                                                 "depthFar", 1.0F);
                if (!width) return std::unexpected(width.error());
                if (!height) return std::unexpected(height.error());
                if (!near_value) return std::unexpected(near_value.error());
                if (!far_value) return std::unexpected(far_value.error());
                auto stored = machine.class_states().set_static_field(
                    *location, Value::from_reference(*allocated));
                if (!stored) return std::unexpected(stored.error());
                instance = *allocated;
            }
            return std::optional<Value>(Value::from_reference(*instance));
        });
    add(registry, kGraphics3D, "getProperties", "()Ljava/util/Hashtable;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto table = allocate_instance(machine, "java/util/Hashtable");
            if (!table) return std::unexpected(table.error());
            auto initialized = machine.invoke_instance(*table,
                "java/util/Hashtable", "<init>", "()V");
            if (!initialized) return std::unexpected(initialized.error());
            if (!initialized->completed_normally()) {
                return fail(ErrorCode::java_exception,
                            "Graphics3D properties table initialization failed");
            }
            return std::optional<Value>(Value::from_reference(*table));
        });

    const auto register_bind = [&registry](const char* descriptor) {
        add(registry, kGraphics3D, "bindTarget", descriptor,
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Graphics3D.bindTarget");
                auto target = reference_argument(arguments, 1U,
                                                "Graphics3D.bindTarget", false);
                if (!object) return std::unexpected(object.error());
                if (!target) return std::unexpected(target.error());
                auto current = reference_field(machine, *object, kGraphics3D,
                                               "target", "Ljava/lang/Object;");
                if (!current) return std::unexpected(current.error());
                if (!current->is_null()) {
                    return fail_java("java/lang/IllegalStateException",
                                     "Graphics3D target is already bound");
                }
                auto stored = set_reference_field(machine, *object, kGraphics3D,
                                                  "target", "Ljava/lang/Object;",
                                                  *target);
                if (!stored) return std::unexpected(stored.error());
                if (arguments.size() >= 4U) {
                    auto depth = int_argument(arguments, 2U,
                                              "Graphics3D.bindTarget");
                    auto hints = int_argument(arguments, 3U,
                                              "Graphics3D.bindTarget");
                    if (!depth) return std::unexpected(depth.error());
                    if (!hints) return std::unexpected(hints.error());
                    auto depth_stored = set_int_field(machine, *object,
                        kGraphics3D, "depthBuffer", *depth, "Z");
                    auto hints_stored = set_int_field(machine, *object,
                        kGraphics3D, "hints", *hints);
                    if (!depth_stored) return std::unexpected(depth_stored.error());
                    if (!hints_stored) return std::unexpected(hints_stored.error());
                }
                return std::optional<Value> {};
            });
    };
    register_bind("(Ljava/lang/Object;)V");
    register_bind("(Ljava/lang/Object;ZI)V");
    add(registry, kGraphics3D, "releaseTarget", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.releaseTarget");
            if (!object) return std::unexpected(object.error());
            auto stored = set_reference_field(machine, *object, kGraphics3D,
                                              "target", "Ljava/lang/Object;", {});
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kGraphics3D, "getTarget", "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.getTarget");
            if (!object) return std::unexpected(object.error());
            auto target = reference_field(machine, *object, kGraphics3D,
                                          "target", "Ljava/lang/Object;");
            if (!target) return std::unexpected(target.error());
            return std::optional<Value>(Value::from_reference(*target));
        });
    add(registry, kGraphics3D, "clear",
        "(Ljavax/microedition/m3g/Background;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.clear");
            if (!object) return std::unexpected(object.error());
            auto bound = require_bound_target(machine, *object);
            if (!bound) return std::unexpected(bound.error());
            return std::optional<Value> {};
        });
    const auto register_render = [&registry](const char* descriptor) {
        add(registry, kGraphics3D, "render", descriptor,
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Graphics3D.render");
                if (!object) return std::unexpected(object.error());
                auto bound = require_bound_target(machine, *object);
                if (!bound) return std::unexpected(bound.error());
                return std::optional<Value> {};
            });
    };
    register_render("(Ljavax/microedition/m3g/World;)V");
    register_render(
        "(Ljavax/microedition/m3g/Node;"
        "Ljavax/microedition/m3g/Transform;)V");
    register_render(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;"
        "Ljavax/microedition/m3g/Transform;)V");
    register_render(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;"
        "Ljavax/microedition/m3g/Transform;I)V");

    add(registry, kGraphics3D, "setCamera",
        "(Ljavax/microedition/m3g/Camera;"
        "Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.setCamera");
            auto camera = reference_argument(arguments, 1U,
                                             "Graphics3D.setCamera");
            auto transform = reference_argument(arguments, 2U,
                                                "Graphics3D.setCamera");
            if (!object) return std::unexpected(object.error());
            if (!camera) return std::unexpected(camera.error());
            if (!transform) return std::unexpected(transform.error());
            auto camera_stored = set_reference_field(machine, *object,
                kGraphics3D, "camera",
                "Ljavax/microedition/m3g/Camera;", *camera);
            auto transform_stored = set_reference_field(machine, *object,
                kGraphics3D, "cameraTransform",
                "Ljavax/microedition/m3g/Transform;", *transform);
            if (!camera_stored) return std::unexpected(camera_stored.error());
            if (!transform_stored) {
                return std::unexpected(transform_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kGraphics3D, "getCamera",
        "(Ljavax/microedition/m3g/Transform;)"
        "Ljavax/microedition/m3g/Camera;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.getCamera");
            auto destination = reference_argument(arguments, 1U,
                                                  "Graphics3D.getCamera");
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto camera = reference_field(machine, *object, kGraphics3D,
                "camera", "Ljavax/microedition/m3g/Camera;");
            auto transform = reference_field(machine, *object, kGraphics3D,
                "cameraTransform", "Ljavax/microedition/m3g/Transform;");
            if (!camera) return std::unexpected(camera.error());
            if (!transform) return std::unexpected(transform.error());
            if (!destination->is_null() && !transform->is_null()) {
                auto matrix = transform_matrix(machine, *transform);
                if (!matrix) return std::unexpected(matrix.error());
                auto stored = set_transform_matrix(machine, *destination, *matrix);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*camera));
        });

    add(registry, kGraphics3D, "setViewport", "(IIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.setViewport");
            if (!object) return std::unexpected(object.error());
            const std::array<const char*, 4> fields {
                "viewportX", "viewportY", "viewportWidth", "viewportHeight",
            };
            for (usize index = 0; index < fields.size(); ++index) {
                auto value = int_argument(arguments, index + 1U,
                                          "Graphics3D.setViewport");
                if (!value) return std::unexpected(value.error());
                if (index >= 2U && *value <= 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Graphics3D viewport dimensions must be positive");
                }
                auto stored = set_int_field(machine, *object, kGraphics3D,
                                            fields[index], *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    const std::array<std::pair<const char*, const char*>, 4> viewport_getters {{
        {"viewportX", "getViewportX"},
        {"viewportY", "getViewportY"},
        {"viewportWidth", "getViewportWidth"},
        {"viewportHeight", "getViewportHeight"},
    }};
    for (const auto& [field, getter] : viewport_getters) {
        add(registry, kGraphics3D, getter, "()I",
            [field, getter](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, getter);
                if (!object) return std::unexpected(object.error());
                auto value = int_field(machine, *object, kGraphics3D, field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    }
    add(registry, kGraphics3D, "setDepthRange", "(FF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.setDepthRange");
            auto near_value = float_argument(arguments, 1U,
                                             "Graphics3D.setDepthRange");
            auto far_value = float_argument(arguments, 2U,
                                            "Graphics3D.setDepthRange");
            if (!object) return std::unexpected(object.error());
            if (!near_value) return std::unexpected(near_value.error());
            if (!far_value) return std::unexpected(far_value.error());
            if (*near_value < 0.0F || *far_value > 1.0F ||
                *near_value > *far_value) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Graphics3D depth range is invalid");
            }
            auto first = set_float_field(machine, *object, kGraphics3D,
                                         "depthNear", *near_value);
            auto second = set_float_field(machine, *object, kGraphics3D,
                                          "depthFar", *far_value);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    for (const auto& [field, getter] :
         std::array<std::pair<const char*, const char*>, 2> {{
             {"depthNear", "getDepthRangeNear"},
             {"depthFar", "getDepthRangeFar"},
         }}) {
        add(registry, kGraphics3D, getter, "()F",
            [field, getter](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, getter);
                if (!object) return std::unexpected(object.error());
                auto value = float_field(machine, *object, kGraphics3D, field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_float(*value));
            });
    }

    add(registry, kGraphics3D, "resetLights", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.resetLights");
            if (!object) return std::unexpected(object.error());
            auto stored = set_int_field(machine, *object, kGraphics3D,
                                        "lightCount", 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kGraphics3D, "addLight",
        "(Ljavax/microedition/m3g/Light;"
        "Ljavax/microedition/m3g/Transform;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.addLight");
            auto light = reference_argument(arguments, 1U,
                                            "Graphics3D.addLight", false);
            if (!object) return std::unexpected(object.error());
            if (!light) return std::unexpected(light.error());
            auto count = int_field(machine, *object, kGraphics3D, "lightCount");
            if (!count) return std::unexpected(count.error());
            auto stored = set_int_field(machine, *object, kGraphics3D,
                                        "lightCount", *count + 1);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, kGraphics3D, "setLight",
        "(ILjavax/microedition/m3g/Light;"
        "Ljavax/microedition/m3g/Transform;)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
    add(registry, kGraphics3D, "getLight",
        "(ILjavax/microedition/m3g/Transform;)"
        "Ljavax/microedition/m3g/Light;",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_reference({}));
        });
}

[[nodiscard]] Result<std::string> loader_resource_name(
    Machine& machine,
    ObjectRef string) {
    if (string.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Loader resource name is null");
    }
    auto value = machine.heap().string_value(string);
    if (!value) return std::unexpected(value.error());
    std::string result;
    result.reserve(value->size());
    for (char16_t character : *value) {
        if (static_cast<u16>(character) > 0x7FU) {
            return fail_java("java/io/IOException",
                             "M3G resource name is not ASCII");
        }
        result.push_back(static_cast<char>(character));
    }
    while (!result.empty() && result.front() == '/') {
        result.erase(result.begin());
    }
    if (result.empty()) {
        return fail_java("java/io/IOException",
                         "M3G resource name is empty");
    }
    return result;
}

[[nodiscard]] Result<std::vector<u8>> loader_byte_array(
    Machine& machine,
    ObjectRef array,
    i32 offset) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Loader byte array is null");
    }
    auto class_name = machine.heap().class_name(array);
    auto length = machine.heap().array_length(array);
    if (!class_name) return std::unexpected(class_name.error());
    if (!length) return std::unexpected(length.error());
    if (*class_name != "[B" || offset < 0 ||
        static_cast<usize>(offset) > *length) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "Loader byte array offset is invalid");
    }
    std::vector<u8> result;
    result.reserve(*length - static_cast<usize>(offset));
    for (usize index = static_cast<usize>(offset); index < *length; ++index) {
        auto value = machine.heap().element(array, index);
        if (!value) return std::unexpected(value.error());
        auto number = value->as_int();
        if (!number) return std::unexpected(number.error());
        result.push_back(static_cast<u8>(*number & 0xFF));
    }
    return result;
}

void register_loader(NativeMethodRegistry& registry) {
    add(registry, "javax/microedition/m3g/Loader", "load",
        "(Ljava/lang/String;)[Ljavax/microedition/m3g/Object3D;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto name = reference_argument(arguments, 0U,
                                            "Loader.load", false);
            if (!name) return std::unexpected(name.error());
            auto resource = loader_resource_name(machine, *name);
            if (!resource) return std::unexpected(resource.error());
            auto bytes = machine.classes().read_resource(*resource);
            if (!bytes) {
                return fail_java("java/io/IOException",
                                 bytes.error().message);
            }
            auto objects = load_m3g(machine, *bytes);
            if (!objects) return std::unexpected(objects.error());
            return std::optional<Value>(Value::from_reference(*objects));
        });
    add(registry, "javax/microedition/m3g/Loader", "load",
        "([BI)[Ljavax/microedition/m3g/Object3D;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto data = reference_argument(arguments, 0U,
                                            "Loader.load", false);
            auto offset = int_argument(arguments, 1U, "Loader.load");
            if (!data) return std::unexpected(data.error());
            if (!offset) return std::unexpected(offset.error());
            auto bytes = loader_byte_array(machine, *data, *offset);
            if (!bytes) return std::unexpected(bytes.error());
            auto objects = load_m3g(machine, *bytes);
            if (!objects) return std::unexpected(objects.error());
            return std::optional<Value>(Value::from_reference(*objects));
        });
}

} // namespace

void register_m3g_render_natives(NativeMethodRegistry& registry) {
    register_graphics3d(registry);
    register_loader(registry);
}

} // namespace phoneme::vm
