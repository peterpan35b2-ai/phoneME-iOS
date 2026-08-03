#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm::m3g {

constexpr const char* kObject3D = "javax/microedition/m3g/Object3D";
constexpr const char* kTransform = "javax/microedition/m3g/Transform";
constexpr const char* kTransformable = "javax/microedition/m3g/Transformable";
constexpr const char* kNode = "javax/microedition/m3g/Node";
constexpr const char* kGroup = "javax/microedition/m3g/Group";
constexpr const char* kWorld = "javax/microedition/m3g/World";
constexpr const char* kGraphics3D = "javax/microedition/m3g/Graphics3D";
constexpr const char* kCamera = "javax/microedition/m3g/Camera";
constexpr const char* kLight = "javax/microedition/m3g/Light";
constexpr const char* kBackground = "javax/microedition/m3g/Background";
constexpr const char* kAppearance = "javax/microedition/m3g/Appearance";
constexpr const char* kCompositingMode = "javax/microedition/m3g/CompositingMode";
constexpr const char* kPolygonMode = "javax/microedition/m3g/PolygonMode";
constexpr const char* kImage2D = "javax/microedition/m3g/Image2D";
constexpr const char* kTexture2D = "javax/microedition/m3g/Texture2D";
constexpr const char* kVertexArray = "javax/microedition/m3g/VertexArray";
constexpr const char* kVertexBuffer = "javax/microedition/m3g/VertexBuffer";
constexpr const char* kIndexBuffer = "javax/microedition/m3g/IndexBuffer";
constexpr const char* kTriangleStripArray = "javax/microedition/m3g/TriangleStripArray";
constexpr const char* kMesh = "javax/microedition/m3g/Mesh";
constexpr const char* kSprite3D = "javax/microedition/m3g/Sprite3D";
constexpr const char* kMaterial = "javax/microedition/m3g/Material";
constexpr const char* kFog = "javax/microedition/m3g/Fog";
constexpr const char* kAnimationController = "javax/microedition/m3g/AnimationController";
constexpr const char* kAnimationTrack = "javax/microedition/m3g/AnimationTrack";
constexpr const char* kKeyframeSequence = "javax/microedition/m3g/KeyframeSequence";
constexpr const char* kRayIntersection = "javax/microedition/m3g/RayIntersection";
constexpr const char* kLoader = "javax/microedition/m3g/Loader";

using Matrix = std::array<float, 16>;
using Initializer = Status (*)(Machine&, ObjectRef);

inline void add(NativeMethodRegistry& registry,
                std::string owner,
                std::string name,
                std::string descriptor,
                NativeMethod implementation) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(implementation));
    if (!registered) std::abort();
}

[[nodiscard]] inline Result<ObjectRef> receiver(
    std::span<const Value> arguments,
    std::string_view operation) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " receiver is missing");
    }
    auto object = arguments.front().as_reference();
    if (!object) return std::unexpected(object.error());
    if (object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " receiver is null");
    }
    return *object;
}

[[nodiscard]] inline Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation,
    bool nullable = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!nullable && value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " argument is null");
    }
    return *value;
}

[[nodiscard]] inline Result<i32> int_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " int argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] inline Result<float> float_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " float argument is missing");
    }
    return arguments[index].as_float();
}

[[nodiscard]] inline Result<FieldLocation> field_location(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    bool is_static = false) {
    return machine.class_states().resolve_field(owner, name, descriptor, is_static);
}

[[nodiscard]] inline Result<Value> object_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().field(object, location->index);
}

[[nodiscard]] inline Status set_object_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    Value value) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index, value);
}

[[nodiscard]] inline Result<ObjectRef> reference_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto value = object_field(machine, object, owner, name, descriptor);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] inline Result<i32> int_field(Machine& machine,
                                           ObjectRef object,
                                           std::string_view owner,
                                           std::string_view name,
                                           std::string_view descriptor = "I") {
    auto value = object_field(machine, object, owner, name, descriptor);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] inline Result<float> float_field(Machine& machine,
                                               ObjectRef object,
                                               std::string_view owner,
                                               std::string_view name) {
    auto value = object_field(machine, object, owner, name, "F");
    if (!value) return std::unexpected(value.error());
    return value->as_float();
}

[[nodiscard]] inline Status set_reference_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    ObjectRef value) {
    return set_object_field(machine, object, owner, name, descriptor,
                            Value::from_reference(value));
}

[[nodiscard]] inline Status set_int_field(Machine& machine,
                                          ObjectRef object,
                                          std::string_view owner,
                                          std::string_view name,
                                          i32 value,
                                          std::string_view descriptor = "I") {
    return set_object_field(machine, object, owner, name, descriptor,
                            Value::from_int(value));
}

[[nodiscard]] inline Status set_float_field(Machine& machine,
                                            ObjectRef object,
                                            std::string_view owner,
                                            std::string_view name,
                                            float value) {
    return set_object_field(machine, object, owner, name, "F",
                            Value::from_float(value));
}

[[nodiscard]] inline Result<ObjectRef> allocate_array(
    Machine& machine,
    std::string class_name,
    usize length,
    Value initial) {
    auto array = machine.heap().allocate_array(class_name, length, initial);
    if (array || array.error().code != ErrorCode::overflow) return array;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.heap().allocate_array(std::move(class_name), length, initial);
}

[[nodiscard]] inline Result<ObjectRef> allocate_instance(
    Machine& machine,
    std::string_view class_name) {
    auto object = machine.class_states().allocate_instance(machine.heap(), class_name);
    if (object || object.error().code != ErrorCode::overflow) return object;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.class_states().allocate_instance(machine.heap(), class_name);
}

[[nodiscard]] inline Matrix identity_matrix() noexcept {
    return Matrix {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] inline Matrix multiply(const Matrix& left,
                                     const Matrix& right) noexcept {
    Matrix result {};
    for (usize row = 0; row < 4U; ++row) {
        for (usize column = 0; column < 4U; ++column) {
            for (usize inner = 0; inner < 4U; ++inner) {
                result[row * 4U + column] +=
                    left[row * 4U + inner] * right[inner * 4U + column];
            }
        }
    }
    return result;
}

[[nodiscard]] inline Matrix translation_matrix(float x, float y, float z) noexcept {
    Matrix result = identity_matrix();
    result[3] = x;
    result[7] = y;
    result[11] = z;
    return result;
}

[[nodiscard]] inline Matrix scale_matrix(float x, float y, float z) noexcept {
    Matrix result = identity_matrix();
    result[0] = x;
    result[5] = y;
    result[10] = z;
    return result;
}

[[nodiscard]] inline Result<Matrix> rotation_matrix(float angle,
                                                    float x,
                                                    float y,
                                                    float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(angle) || !std::isfinite(length) || length <= 1.0e-7F) {
        return fail_java("java/lang/IllegalArgumentException",
                         "M3G rotation axis is invalid");
    }
    x /= length;
    y /= length;
    z /= length;
    const float radians = angle * (std::numbers::pi_v<float> / 180.0F);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0F - c;
    Matrix result = identity_matrix();
    result[0] = x * x * t + c;
    result[1] = x * y * t - z * s;
    result[2] = x * z * t + y * s;
    result[4] = y * x * t + z * s;
    result[5] = y * y * t + c;
    result[6] = y * z * t - x * s;
    result[8] = z * x * t - y * s;
    result[9] = z * y * t + x * s;
    result[10] = z * z * t + c;
    return result;
}

[[nodiscard]] inline Result<Matrix> quaternion_matrix(float x,
                                                      float y,
                                                      float z,
                                                      float w) {
    const float length = std::sqrt(x * x + y * y + z * z + w * w);
    if (!std::isfinite(length) || length <= 1.0e-7F) {
        return fail_java("java/lang/IllegalArgumentException",
                         "M3G quaternion is invalid");
    }
    x /= length;
    y /= length;
    z /= length;
    w /= length;
    Matrix result = identity_matrix();
    result[0] = 1.0F - 2.0F * (y * y + z * z);
    result[1] = 2.0F * (x * y - z * w);
    result[2] = 2.0F * (x * z + y * w);
    result[4] = 2.0F * (x * y + z * w);
    result[5] = 1.0F - 2.0F * (x * x + z * z);
    result[6] = 2.0F * (y * z - x * w);
    result[8] = 2.0F * (x * z - y * w);
    result[9] = 2.0F * (y * z + x * w);
    result[10] = 1.0F - 2.0F * (x * x + y * y);
    return result;
}

[[nodiscard]] inline Result<Matrix> inverse_matrix(const Matrix& input) {
    std::array<std::array<double, 8>, 4> work {};
    for (usize row = 0; row < 4U; ++row) {
        for (usize column = 0; column < 4U; ++column) {
            work[row][column] = input[row * 4U + column];
            work[row][column + 4U] = row == column ? 1.0 : 0.0;
        }
    }
    for (usize pivot = 0; pivot < 4U; ++pivot) {
        usize best = pivot;
        for (usize row = pivot + 1U; row < 4U; ++row) {
            if (std::abs(work[row][pivot]) > std::abs(work[best][pivot])) best = row;
        }
        if (std::abs(work[best][pivot]) < 1.0e-10) {
            return fail_java("java/lang/ArithmeticException",
                             "M3G transform is singular");
        }
        if (best != pivot) std::swap(work[best], work[pivot]);
        const double divisor = work[pivot][pivot];
        for (double& value : work[pivot]) value /= divisor;
        for (usize row = 0; row < 4U; ++row) {
            if (row == pivot) continue;
            const double factor = work[row][pivot];
            for (usize column = 0; column < 8U; ++column) {
                work[row][column] -= factor * work[pivot][column];
            }
        }
    }
    Matrix result {};
    for (usize row = 0; row < 4U; ++row) {
        for (usize column = 0; column < 4U; ++column) {
            result[row * 4U + column] =
                static_cast<float>(work[row][column + 4U]);
        }
    }
    return result;
}

[[nodiscard]] inline Result<ObjectRef> float_array(
    Machine& machine,
    std::span<const float> values) {
    auto array = allocate_array(machine, "[F", values.size(), Value::from_float(0.0F));
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < values.size(); ++index) {
        auto stored = machine.heap().set_element(*array, index,
                                                 Value::from_float(values[index]));
        if (!stored) return std::unexpected(stored.error());
    }
    return *array;
}

[[nodiscard]] inline Result<std::vector<float>> read_float_array(
    Machine& machine,
    ObjectRef array,
    std::string_view operation) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " array is null");
    }
    auto class_name = machine.heap().class_name(array);
    auto length = machine.heap().array_length(array);
    if (!class_name) return std::unexpected(class_name.error());
    if (!length) return std::unexpected(length.error());
    if (*class_name != "[F") {
        return fail_java("java/lang/IllegalArgumentException",
                         std::string(operation) + " requires float[]");
    }
    std::vector<float> result(*length);
    for (usize index = 0; index < *length; ++index) {
        auto value = machine.heap().element(array, index);
        if (!value) return std::unexpected(value.error());
        auto number = value->as_float();
        if (!number) return std::unexpected(number.error());
        result[index] = *number;
    }
    return result;
}

[[nodiscard]] inline Status write_float_array(
    Machine& machine,
    ObjectRef array,
    std::span<const float> values,
    usize offset = 0U) {
    auto class_name = machine.heap().class_name(array);
    auto length = machine.heap().array_length(array);
    if (!class_name) return std::unexpected(class_name.error());
    if (!length) return std::unexpected(length.error());
    if (*class_name != "[F" || offset > *length || values.size() > *length - offset) {
        return fail_java("java/lang/IllegalArgumentException",
                         "M3G float[] destination is invalid");
    }
    for (usize index = 0; index < values.size(); ++index) {
        auto stored = machine.heap().set_element(
            array, offset + index, Value::from_float(values[index]));
        if (!stored) return stored;
    }
    return {};
}

[[nodiscard]] inline Result<ObjectRef> ensure_matrix_array(
    Machine& machine,
    ObjectRef transform) {
    auto matrix = reference_field(machine, transform, kTransform, "matrix", "[F");
    if (!matrix) return std::unexpected(matrix.error());
    if (!matrix->is_null()) return *matrix;
    const Matrix identity = identity_matrix();
    auto allocated = float_array(machine, identity);
    if (!allocated) return std::unexpected(allocated.error());
    auto stored = set_reference_field(machine, transform, kTransform,
                                      "matrix", "[F", *allocated);
    if (!stored) return std::unexpected(stored.error());
    return *allocated;
}

[[nodiscard]] inline Result<Matrix> transform_matrix(
    Machine& machine,
    ObjectRef transform) {
    auto array = ensure_matrix_array(machine, transform);
    if (!array) return std::unexpected(array.error());
    auto values = read_float_array(machine, *array, "Transform");
    if (!values) return std::unexpected(values.error());
    if (values->size() != 16U) {
        return fail(ErrorCode::invalid_state,
                    "M3G Transform matrix does not have 16 elements");
    }
    Matrix result {};
    std::copy(values->begin(), values->end(), result.begin());
    return result;
}

[[nodiscard]] inline Status set_transform_matrix(
    Machine& machine,
    ObjectRef transform,
    const Matrix& matrix) {
    auto array = ensure_matrix_array(machine, transform);
    if (!array) return std::unexpected(array.error());
    return write_float_array(machine, *array, matrix);
}

[[nodiscard]] inline Result<ObjectRef> new_transform(
    Machine& machine,
    const Matrix& matrix = identity_matrix()) {
    auto transform = allocate_instance(machine, kTransform);
    if (!transform) return std::unexpected(transform.error());
    auto stored = set_transform_matrix(machine, *transform, matrix);
    if (!stored) return std::unexpected(stored.error());
    return *transform;
}

[[nodiscard]] inline Status initialize_object3d(Machine&, ObjectRef) {
    return {};
}

[[nodiscard]] inline Status initialize_transformable(
    Machine& machine,
    ObjectRef object) {
    auto transform = new_transform(machine);
    if (!transform) return std::unexpected(transform.error());
    return set_reference_field(machine, object, kTransformable,
                               "localTransform",
                               "Ljavax/microedition/m3g/Transform;",
                               *transform);
}

[[nodiscard]] inline Status initialize_node(Machine& machine,
                                            ObjectRef object) {
    auto initialized = initialize_transformable(machine, object);
    if (!initialized) return initialized;
    auto rendering = set_int_field(machine, object, kNode,
                                   "renderingEnabled", 1, "Z");
    auto picking = set_int_field(machine, object, kNode,
                                 "pickingEnabled", 1, "Z");
    auto alpha = set_float_field(machine, object, kNode, "alphaFactor", 1.0F);
    auto scope = set_int_field(machine, object, kNode, "scope", -1);
    if (!rendering) return rendering;
    if (!picking) return picking;
    if (!alpha) return alpha;
    return scope;
}

[[nodiscard]] inline Result<ObjectRef> local_transform(
    Machine& machine,
    ObjectRef object) {
    auto transform = reference_field(machine, object, kTransformable,
                                     "localTransform",
                                     "Ljavax/microedition/m3g/Transform;");
    if (!transform) return std::unexpected(transform.error());
    if (!transform->is_null()) return *transform;
    auto replacement = new_transform(machine);
    if (!replacement) return std::unexpected(replacement.error());
    auto stored = set_reference_field(machine, object, kTransformable,
                                      "localTransform",
                                      "Ljavax/microedition/m3g/Transform;",
                                      *replacement);
    if (!stored) return std::unexpected(stored.error());
    return *replacement;
}

[[nodiscard]] inline Status initialize_group(Machine& machine,
                                             ObjectRef object) {
    auto initialized = initialize_node(machine, object);
    if (!initialized) return initialized;
    auto children = allocate_array(machine, "[Ljavax/microedition/m3g/Node;", 4U,
                                   Value::from_reference({}));
    if (!children) return std::unexpected(children.error());
    return set_reference_field(machine, object, kGroup, "children",
                               "[Ljavax/microedition/m3g/Node;", *children);
}

inline void register_noop_constructor(NativeMethodRegistry& registry,
                                      std::string owner,
                                      std::string descriptor,
                                      Initializer initializer = initialize_object3d) {
    add(registry, std::move(owner), "<init>", std::move(descriptor),
        [initializer](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "M3G constructor");
            if (!object) return std::unexpected(object.error());
            auto initialized = initializer(machine, *object);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
}

inline void register_int_property(NativeMethodRegistry& registry,
                                  const char* owner,
                                  const char* field_owner,
                                  const char* field_name,
                                  const char* setter,
                                  const char* getter,
                                  const char* descriptor = "I") {
    add(registry, owner, setter, std::string("(") + descriptor + ")V",
        [field_owner, field_name, descriptor](Machine& machine,
                                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            auto value = int_argument(arguments, 1U, field_name);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored = set_int_field(machine, *object, field_owner,
                                        field_name, *value, descriptor);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, owner, getter, std::string("()") + descriptor,
        [field_owner, field_name, descriptor](Machine& machine,
                                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            if (!object) return std::unexpected(object.error());
            auto value = object_field(machine, *object, field_owner,
                                      field_name, descriptor);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
}

inline void register_float_property(NativeMethodRegistry& registry,
                                    const char* owner,
                                    const char* field_owner,
                                    const char* field_name,
                                    const char* setter,
                                    const char* getter) {
    add(registry, owner, setter, "(F)V",
        [field_owner, field_name](Machine& machine,
                                  std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            auto value = float_argument(arguments, 1U, field_name);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored = set_float_field(machine, *object, field_owner,
                                          field_name, *value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, owner, getter, "()F",
        [field_owner, field_name](Machine& machine,
                                  std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            if (!object) return std::unexpected(object.error());
            auto value = float_field(machine, *object, field_owner, field_name);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_float(*value));
        });
}

inline void register_reference_property(NativeMethodRegistry& registry,
                                        const char* owner,
                                        const char* field_owner,
                                        const char* field_name,
                                        const char* descriptor,
                                        const char* setter,
                                        const char* getter) {
    add(registry, owner, setter, std::string("(") + descriptor + ")V",
        [field_owner, field_name, descriptor](Machine& machine,
                                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            auto value = reference_argument(arguments, 1U, field_name);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored = set_reference_field(machine, *object, field_owner,
                                              field_name, descriptor, *value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, owner, getter, std::string("()") + descriptor,
        [field_owner, field_name, descriptor](Machine& machine,
                                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, field_name);
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, field_owner,
                                         field_name, descriptor);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
}

} // namespace phoneme::vm::m3g
