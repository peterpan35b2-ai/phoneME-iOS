#include "M3gNativeModules.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "M3gLoader.hpp"
#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

[[nodiscard]] Result<ObjectRef> graphics3d_reference_array(
    Machine& machine,
    ObjectRef graphics3d,
    std::string_view field,
    std::string_view descriptor) {
    auto array = reference_field(machine, graphics3d, kGraphics3D,
                                 field, descriptor);
    if (!array) return std::unexpected(array.error());
    if (!array->is_null()) return *array;
    auto created = allocate_array(machine, std::string(descriptor), 4U,
                                  Value::from_reference({}));
    if (!created) return std::unexpected(created.error());
    auto stored = set_reference_field(machine, graphics3d, kGraphics3D,
                                      field, descriptor, *created);
    if (!stored) return std::unexpected(stored.error());
    return *created;
}

[[nodiscard]] Status ensure_graphics3d_reference_capacity(
    Machine& machine,
    ObjectRef graphics3d,
    std::string_view field,
    std::string_view descriptor,
    usize minimum) {
    auto array = graphics3d_reference_array(
        machine, graphics3d, field, descriptor);
    if (!array) return std::unexpected(array.error());
    auto length = machine.heap().array_length(*array);
    if (!length) return std::unexpected(length.error());
    if (*length >= minimum) return {};
    const usize capacity = std::max(
        minimum, *length == 0U ? 4U : *length * 2U);
    auto replacement = allocate_array(
        machine, std::string(descriptor), capacity,
                                      Value::from_reference({}));
    if (!replacement) return std::unexpected(replacement.error());
    auto replacement_root = machine.pin_native_root(*replacement);
    if (!replacement_root) return std::unexpected(replacement_root.error());
    for (usize index = 0U; index < *length; ++index) {
        auto value = machine.heap().element(*array, index);
        if (!value) return std::unexpected(value.error());
        auto copied = machine.heap().set_element(*replacement, index, *value);
        if (!copied) return copied;
    }
    return set_reference_field(machine, graphics3d, kGraphics3D,
                               field, descriptor, *replacement);
}

[[nodiscard]] Status ensure_light_capacity(Machine& machine,
                                           ObjectRef graphics3d,
                                           usize minimum) {
    auto lights = ensure_graphics3d_reference_capacity(
        machine, graphics3d, "lights",
        "[Ljavax/microedition/m3g/Light;", minimum);
    if (!lights) return lights;
    return ensure_graphics3d_reference_capacity(
        machine, graphics3d, "lightTransforms",
        "[Ljavax/microedition/m3g/Transform;", minimum);
}

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
            auto count = int_field(machine, *object, kGraphics3D, "lightCount");
            if (!count) return std::unexpected(count.error());
            if (*count > 0) {
                auto lights = graphics3d_reference_array(
                    machine, *object, "lights",
                    "[Ljavax/microedition/m3g/Light;");
                auto transforms = graphics3d_reference_array(
                    machine, *object, "lightTransforms",
                    "[Ljavax/microedition/m3g/Transform;");
                if (!lights) return std::unexpected(lights.error());
                if (!transforms) return std::unexpected(transforms.error());
                for (i32 index = 0; index < *count; ++index) {
                    auto cleared = machine.heap().set_element(
                        *lights, static_cast<usize>(index),
                        Value::from_reference({}));
                    if (!cleared) return std::unexpected(cleared.error());
                    cleared = machine.heap().set_element(
                        *transforms, static_cast<usize>(index),
                        Value::from_reference({}));
                    if (!cleared) return std::unexpected(cleared.error());
                }
            }
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
            auto transform = reference_argument(arguments, 2U,
                                                "Graphics3D.addLight", true);
            if (!object) return std::unexpected(object.error());
            if (!light) return std::unexpected(light.error());
            if (!transform) return std::unexpected(transform.error());
            auto count = int_field(machine, *object, kGraphics3D, "lightCount");
            if (!count) return std::unexpected(count.error());
            if (*count < 0 || *count == std::numeric_limits<i32>::max()) {
                return fail_java("java/lang/IllegalStateException",
                                 "Graphics3D light count is invalid");
            }
            auto capacity = ensure_light_capacity(
                machine, *object, static_cast<usize>(*count + 1));
            if (!capacity) return std::unexpected(capacity.error());
            auto lights = graphics3d_reference_array(
                machine, *object, "lights",
                "[Ljavax/microedition/m3g/Light;");
            auto transforms = graphics3d_reference_array(
                machine, *object, "lightTransforms",
                "[Ljavax/microedition/m3g/Transform;");
            if (!lights) return std::unexpected(lights.error());
            if (!transforms) return std::unexpected(transforms.error());
            auto stored = machine.heap().set_element(
                *lights, static_cast<usize>(*count),
                Value::from_reference(*light));
            if (!stored) return std::unexpected(stored.error());
            stored = machine.heap().set_element(
                *transforms, static_cast<usize>(*count),
                Value::from_reference(*transform));
            if (!stored) return std::unexpected(stored.error());
            stored = set_int_field(machine, *object, kGraphics3D,
                                   "lightCount", *count + 1);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, kGraphics3D, "setLight",
        "(ILjavax/microedition/m3g/Light;"
        "Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.setLight");
            auto index = int_argument(arguments, 1U, "Graphics3D.setLight");
            auto light = reference_argument(arguments, 2U,
                                            "Graphics3D.setLight", false);
            auto transform = reference_argument(arguments, 3U,
                                                "Graphics3D.setLight", true);
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            if (!light) return std::unexpected(light.error());
            if (!transform) return std::unexpected(transform.error());
            auto count = int_field(machine, *object, kGraphics3D, "lightCount");
            if (!count) return std::unexpected(count.error());
            if (*index < 0 || *index >= *count) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Graphics3D light index is out of range");
            }
            auto lights = graphics3d_reference_array(
                machine, *object, "lights",
                "[Ljavax/microedition/m3g/Light;");
            auto transforms = graphics3d_reference_array(
                machine, *object, "lightTransforms",
                "[Ljavax/microedition/m3g/Transform;");
            if (!lights) return std::unexpected(lights.error());
            if (!transforms) return std::unexpected(transforms.error());
            auto stored = machine.heap().set_element(
                *lights, static_cast<usize>(*index),
                Value::from_reference(*light));
            if (!stored) return std::unexpected(stored.error());
            stored = machine.heap().set_element(
                *transforms, static_cast<usize>(*index),
                Value::from_reference(*transform));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kGraphics3D, "getLight",
        "(ILjavax/microedition/m3g/Transform;)"
        "Ljavax/microedition/m3g/Light;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Graphics3D.getLight");
            auto index = int_argument(arguments, 1U, "Graphics3D.getLight");
            auto destination = reference_argument(
                arguments, 2U, "Graphics3D.getLight", true);
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            if (!destination) return std::unexpected(destination.error());
            auto count = int_field(machine, *object, kGraphics3D, "lightCount");
            if (!count) return std::unexpected(count.error());
            if (*index < 0 || *index >= *count) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Graphics3D light index is out of range");
            }
            auto lights = graphics3d_reference_array(
                machine, *object, "lights",
                "[Ljavax/microedition/m3g/Light;");
            auto transforms = graphics3d_reference_array(
                machine, *object, "lightTransforms",
                "[Ljavax/microedition/m3g/Transform;");
            if (!lights) return std::unexpected(lights.error());
            if (!transforms) return std::unexpected(transforms.error());
            auto light_value = machine.heap().element(
                *lights, static_cast<usize>(*index));
            auto transform_value = machine.heap().element(
                *transforms, static_cast<usize>(*index));
            if (!light_value) return std::unexpected(light_value.error());
            if (!transform_value) {
                return std::unexpected(transform_value.error());
            }
            auto light = light_value->as_reference();
            auto transform = transform_value->as_reference();
            if (!light) return std::unexpected(light.error());
            if (!transform) return std::unexpected(transform.error());
            if (!destination->is_null()) {
                Matrix matrix = identity_matrix();
                if (!transform->is_null()) {
                    auto loaded = transform_matrix(machine, *transform);
                    if (!loaded) return std::unexpected(loaded.error());
                    matrix = *loaded;
                }
                auto stored = set_transform_matrix(
                    machine, *destination, matrix);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*light));
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

[[nodiscard]] Result<std::string> external_resource_name(
    std::string_view owner_resource,
    std::string_view external_uri) {
    if (external_uri.empty()) {
        return fail_java("java/io/IOException",
                         "M3G external reference URI is empty");
    }
    if (external_uri.find(':') != std::string_view::npos) {
        return fail_java("java/io/IOException",
                         "M3G external network URI is not supported");
    }

    std::vector<std::string> components;
    const bool absolute = external_uri.front() == '/';
    if (!absolute) {
        const usize slash = owner_resource.rfind('/');
        std::string_view base = slash == std::string_view::npos
            ? std::string_view {}
            : owner_resource.substr(0U, slash);
        usize start = 0U;
        while (start < base.size()) {
            const usize end = base.find('/', start);
            const auto component = base.substr(
                start, end == std::string_view::npos
                    ? base.size() - start : end - start);
            if (!component.empty()) components.emplace_back(component);
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
    }

    usize start = absolute ? 1U : 0U;
    while (start <= external_uri.size()) {
        const usize end = external_uri.find('/', start);
        const auto component = external_uri.substr(
            start, end == std::string_view::npos
                ? external_uri.size() - start : end - start);
        if (component.empty() || component == ".") {
            // Ignore duplicate separators and current-directory segments.
        } else if (component == "..") {
            if (components.empty()) {
                return fail_java("java/io/IOException",
                                 "M3G external reference escapes the JAR root");
            }
            components.pop_back();
        } else {
            if (component.find('\\') != std::string_view::npos ||
                component.find('\0') != std::string_view::npos) {
                return fail_java("java/io/IOException",
                                 "M3G external reference path is invalid");
            }
            components.emplace_back(component);
        }
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    if (components.empty()) {
        return fail_java("java/io/IOException",
                         "M3G external reference has no resource name");
    }

    std::string result;
    for (const auto& component : components) {
        if (!result.empty()) result.push_back('/');
        result += component;
    }
    return result;
}

[[nodiscard]] Result<ObjectRef> execution_reference(
    Machine& machine,
    const ExecutionResult& result,
    std::string_view operation) {
    if (!result.completed_normally()) {
        if (!result.throwable.has_value()) {
            return fail(ErrorCode::internal_error,
                        std::string(operation) +
                            " failed without a Java throwable");
        }
        auto class_name = machine.heap().class_name(*result.throwable);
        if (!class_name) return std::unexpected(class_name.error());
        return fail_java(*class_name, std::string(operation) + " failed");
    }
    if (!result.return_value.has_value()) {
        return fail(ErrorCode::invalid_state,
                    std::string(operation) + " returned no object");
    }
    auto reference = result.return_value->as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (reference->is_null()) {
        return fail_java("java/io/IOException",
                         std::string(operation) + " returned null");
    }
    return *reference;
}

[[nodiscard]] Result<ObjectRef> create_ascii_string(
    Machine& machine,
    std::string_view text) {
    auto string = allocate_instance(machine, "java/lang/String");
    if (!string) return std::unexpected(string.error());
    std::u16string utf16;
    utf16.reserve(text.size());
    for (const char character : text) {
        utf16.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
    }
    auto attached = machine.heap().attach_string(*string, std::move(utf16));
    if (!attached) return std::unexpected(attached.error());
    return *string;
}

[[nodiscard]] Result<ObjectRef> load_external_image2d(
    Machine& machine,
    std::string_view resource_name) {
    std::string java_name {"/"};
    java_name.append(resource_name);
    auto name = create_ascii_string(machine, java_name);
    if (!name) return std::unexpected(name.error());
    const std::array<Value, 1> image_arguments {
        Value::from_reference(*name),
    };
    auto created = machine.invoke_static(
        "javax/microedition/lcdui/Image", "createImage",
        "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;",
        image_arguments);
    if (!created) return std::unexpected(created.error());
    auto image = execution_reference(machine, *created,
                                     "M3G external image load");
    if (!image) return std::unexpected(image.error());
    auto image_root = machine.pin_native_root(*image);
    if (!image_root) return std::unexpected(image_root.error());

    auto width = int_field(machine, *image,
                           "javax/microedition/lcdui/Image", "width");
    auto height = int_field(machine, *image,
                            "javax/microedition/lcdui/Image", "height");
    if (!width) return std::unexpected(width.error());
    if (!height) return std::unexpected(height.error());

    auto image2d = allocate_instance(machine, kImage2D);
    if (!image2d) return std::unexpected(image2d.error());
    auto image2d_root = machine.pin_native_root(*image2d);
    if (!image2d_root) return std::unexpected(image2d_root.error());
    auto initialized = initialize_object3d(machine, *image2d);
    if (!initialized) return std::unexpected(initialized.error());
    i32 image_format = 100;
    auto decoded = machine.graphics().image(image->bits);
    if (decoded) {
        const bool has_alpha = std::any_of(
            (*decoded)->pixels().begin(), (*decoded)->pixels().end(),
            [](graphics::Pixel pixel) {
                return graphics::alpha(pixel) != 255U;
            });
        image_format = has_alpha ? 100 : 99;
    }
    auto format = set_int_field(machine, *image2d, kImage2D,
                                "format", image_format);
    auto width_stored = set_int_field(machine, *image2d, kImage2D,
                                      "width", *width);
    auto height_stored = set_int_field(machine, *image2d, kImage2D,
                                       "height", *height);
    auto immutable = set_int_field(machine, *image2d, kImage2D,
                                   "mutable", 0, "Z");
    auto source = set_reference_field(machine, *image2d, kImage2D,
                                      "source", "Ljava/lang/Object;", *image);
    if (!format) return std::unexpected(format.error());
    if (!width_stored) return std::unexpected(width_stored.error());
    if (!height_stored) return std::unexpected(height_stored.error());
    if (!immutable) return std::unexpected(immutable.error());
    if (!source) return std::unexpected(source.error());
    return *image2d;
}

[[nodiscard]] ExternalReferenceResolver external_resolver(
    Machine& machine,
    std::string owner_resource) {
    auto cache = std::make_shared<std::unordered_map<std::string, ObjectRef>>();
    return [&machine, owner_resource = std::move(owner_resource),
            cache = std::move(cache)](
        std::string_view uri) -> Result<ObjectRef> {
        auto resource = external_resource_name(owner_resource, uri);
        if (!resource) return std::unexpected(resource.error());
        const auto found = cache->find(*resource);
        if (found != cache->end()) return found->second;
        auto loaded = load_external_image2d(machine, *resource);
        if (!loaded) return std::unexpected(loaded.error());
        cache->emplace(*resource, *loaded);
        return *loaded;
    };
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
            auto objects = load_m3g(
                machine, *bytes, external_resolver(machine, *resource));
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
            auto objects = load_m3g(
                machine, *bytes, external_resolver(machine, {}));
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
