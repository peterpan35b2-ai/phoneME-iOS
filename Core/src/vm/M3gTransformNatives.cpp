#include "M3gNativeModules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

constexpr const char* kVertexArray = "javax/microedition/m3g/VertexArray";

void register_transform(NativeMethodRegistry& registry) {
    add(registry, kTransform, "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.<init>");
            if (!object) return std::unexpected(object.error());
            auto stored = set_transform_matrix(machine, *object, identity_matrix());
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "<init>",
        "(Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.<init>");
            auto source = reference_argument(arguments, 1U,
                                             "Transform.<init>", false);
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            auto matrix = transform_matrix(machine, *source);
            if (!matrix) return std::unexpected(matrix.error());
            auto stored = set_transform_matrix(machine, *object, *matrix);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "setIdentity", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.setIdentity");
            if (!object) return std::unexpected(object.error());
            auto stored = set_transform_matrix(machine, *object, identity_matrix());
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "set", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.set");
            auto source = reference_argument(arguments, 1U,
                                             "Transform.set", false);
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            auto values = read_float_array(machine, *source, "Transform.set");
            if (!values) return std::unexpected(values.error());
            if (values->size() != 16U) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Transform.set requires 16 floats");
            }
            Matrix matrix {};
            std::copy(values->begin(), values->end(), matrix.begin());
            auto stored = set_transform_matrix(machine, *object, matrix);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "set",
        "(Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.set");
            auto source = reference_argument(arguments, 1U,
                                             "Transform.set", false);
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            auto matrix = transform_matrix(machine, *source);
            if (!matrix) return std::unexpected(matrix.error());
            auto stored = set_transform_matrix(machine, *object, *matrix);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "get", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.get");
            auto destination = reference_argument(arguments, 1U,
                                                  "Transform.get", false);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto matrix = transform_matrix(machine, *object);
            if (!matrix) return std::unexpected(matrix.error());
            auto stored = write_float_array(machine, *destination, *matrix);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "postMultiply",
        "(Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.postMultiply");
            auto other = reference_argument(arguments, 1U,
                                            "Transform.postMultiply", false);
            if (!object) return std::unexpected(object.error());
            if (!other) return std::unexpected(other.error());
            auto left = transform_matrix(machine, *object);
            auto right = transform_matrix(machine, *other);
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            auto stored = set_transform_matrix(machine, *object,
                                               multiply(*left, *right));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    const auto register_post = [&registry](const char* name,
                                           const char* descriptor,
                                           auto factory) {
        add(registry, kTransform, name, descriptor,
            [factory](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Transform operation");
                if (!object) return std::unexpected(object.error());
                auto operation = factory(arguments);
                if (!operation) return std::unexpected(operation.error());
                auto current = transform_matrix(machine, *object);
                if (!current) return std::unexpected(current.error());
                auto stored = set_transform_matrix(machine, *object,
                                                   multiply(*current, *operation));
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    register_post("postTranslate", "(FFF)V",
        [](std::span<const Value> arguments) -> Result<Matrix> {
            auto x = float_argument(arguments, 1U, "Transform.postTranslate");
            auto y = float_argument(arguments, 2U, "Transform.postTranslate");
            auto z = float_argument(arguments, 3U, "Transform.postTranslate");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            return translation_matrix(*x, *y, *z);
        });
    register_post("postScale", "(FFF)V",
        [](std::span<const Value> arguments) -> Result<Matrix> {
            auto x = float_argument(arguments, 1U, "Transform.postScale");
            auto y = float_argument(arguments, 2U, "Transform.postScale");
            auto z = float_argument(arguments, 3U, "Transform.postScale");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            return scale_matrix(*x, *y, *z);
        });
    register_post("postRotate", "(FFFF)V",
        [](std::span<const Value> arguments) -> Result<Matrix> {
            auto angle = float_argument(arguments, 1U, "Transform.postRotate");
            auto x = float_argument(arguments, 2U, "Transform.postRotate");
            auto y = float_argument(arguments, 3U, "Transform.postRotate");
            auto z = float_argument(arguments, 4U, "Transform.postRotate");
            if (!angle) return std::unexpected(angle.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            return rotation_matrix(*angle, *x, *y, *z);
        });
    register_post("postRotateQuat", "(FFFF)V",
        [](std::span<const Value> arguments) -> Result<Matrix> {
            auto x = float_argument(arguments, 1U, "Transform.postRotateQuat");
            auto y = float_argument(arguments, 2U, "Transform.postRotateQuat");
            auto z = float_argument(arguments, 3U, "Transform.postRotateQuat");
            auto w = float_argument(arguments, 4U, "Transform.postRotateQuat");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            if (!w) return std::unexpected(w.error());
            return quaternion_matrix(*x, *y, *z, *w);
        });
    add(registry, kTransform, "transpose", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.transpose");
            if (!object) return std::unexpected(object.error());
            auto matrix = transform_matrix(machine, *object);
            if (!matrix) return std::unexpected(matrix.error());
            for (usize row = 0; row < 4U; ++row) {
                for (usize column = row + 1U; column < 4U; ++column) {
                    std::swap((*matrix)[row * 4U + column],
                              (*matrix)[column * 4U + row]);
                }
            }
            auto stored = set_transform_matrix(machine, *object, *matrix);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "invert", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.invert");
            if (!object) return std::unexpected(object.error());
            auto matrix = transform_matrix(machine, *object);
            if (!matrix) return std::unexpected(matrix.error());
            auto inverse = inverse_matrix(*matrix);
            if (!inverse) return std::unexpected(inverse.error());
            auto stored = set_transform_matrix(machine, *object, *inverse);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "transform", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.transform");
            auto array = reference_argument(arguments, 1U,
                                            "Transform.transform", false);
            if (!object) return std::unexpected(object.error());
            if (!array) return std::unexpected(array.error());
            auto values = read_float_array(machine, *array,
                                           "Transform.transform");
            if (!values) return std::unexpected(values.error());
            if (values->size() % 4U != 0U) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Transform.transform requires groups of four floats");
            }
            if (values->empty()) return std::optional<Value> {};
            auto matrix = transform_matrix(machine, *object);
            if (!matrix) return std::unexpected(matrix.error());
            for (usize base = 0; base < values->size(); base += 4U) {
                const std::array<float, 4> input {
                    (*values)[base], (*values)[base + 1U],
                    (*values)[base + 2U], (*values)[base + 3U],
                };
                for (usize row = 0; row < 4U; ++row) {
                    (*values)[base + row] =
                        (*matrix)[row * 4U] * input[0] +
                        (*matrix)[row * 4U + 1U] * input[1] +
                        (*matrix)[row * 4U + 2U] * input[2] +
                        (*matrix)[row * 4U + 3U] * input[3];
                }
            }
            auto stored = write_float_array(machine, *array, *values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransform, "transform",
        "(Ljavax/microedition/m3g/VertexArray;[FZ)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transform.transform");
            auto input = reference_argument(arguments, 1U,
                                            "Transform.transform", false);
            auto output = reference_argument(arguments, 2U,
                                             "Transform.transform", false);
            auto use_w = int_argument(arguments, 3U,
                                      "Transform.transform");
            if (!object) return std::unexpected(object.error());
            if (!input) return std::unexpected(input.error());
            if (!output) return std::unexpected(output.error());
            if (!use_w) return std::unexpected(use_w.error());

            auto vertex_count = int_field(machine, *input, kVertexArray,
                                          "vertexCount");
            auto component_count = int_field(machine, *input, kVertexArray,
                                             "componentCount");
            auto component_size = int_field(machine, *input, kVertexArray,
                                            "componentSize");
            auto data = reference_field(machine, *input, kVertexArray,
                                        "data", "Ljava/lang/Object;");
            if (!vertex_count) return std::unexpected(vertex_count.error());
            if (!component_count) {
                return std::unexpected(component_count.error());
            }
            if (!component_size) {
                return std::unexpected(component_size.error());
            }
            if (!data) return std::unexpected(data.error());
            if (*vertex_count < 0 ||
                (*component_count != 2 && *component_count != 3) ||
                (*component_size != 1 && *component_size != 2) ||
                data->is_null()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Transform vertex array is invalid");
            }
            auto output_length = machine.heap().array_length(*output);
            auto data_length = machine.heap().array_length(*data);
            if (!output_length) return std::unexpected(output_length.error());
            if (!data_length) return std::unexpected(data_length.error());
            const usize vertices = static_cast<usize>(*vertex_count);
            const usize components = static_cast<usize>(*component_count);
            if (*output_length < vertices * 4U ||
                *data_length < vertices * components) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Transform output or vertex data is too short");
            }
            auto matrix = transform_matrix(machine, *object);
            if (!matrix) return std::unexpected(matrix.error());
            std::vector<float> transformed(vertices * 4U, 0.0F);
            for (usize vertex = 0U; vertex < vertices; ++vertex) {
                std::array<float, 4> input_value {
                    0.0F, 0.0F, 0.0F, *use_w != 0 ? 1.0F : 0.0F};
                for (usize component = 0U; component < components;
                     ++component) {
                    auto value = machine.heap().element(
                        *data, vertex * components + component);
                    if (!value) return std::unexpected(value.error());
                    auto integer = value->as_int();
                    if (!integer) return std::unexpected(integer.error());
                    input_value[component] = *component_size == 1
                        ? static_cast<float>(static_cast<std::int8_t>(
                            *integer & 0xFF))
                        : static_cast<float>(static_cast<std::int16_t>(
                            *integer & 0xFFFF));
                }
                for (usize row = 0U; row < 4U; ++row) {
                    transformed[vertex * 4U + row] =
                        (*matrix)[row * 4U] * input_value[0U] +
                        (*matrix)[row * 4U + 1U] * input_value[1U] +
                        (*matrix)[row * 4U + 2U] * input_value[2U] +
                        (*matrix)[row * 4U + 3U] * input_value[3U];
                }
            }
            for (usize index = 0U; index < transformed.size(); ++index) {
                auto stored = machine.heap().set_element(
                    *output, index, Value::from_float(transformed[index]));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
}

void register_transformable(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kTransformable, "()V",
                              initialize_transformable);
    add(registry, kTransformable, "setTransform",
        "(Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.setTransform");
            auto source = reference_argument(arguments, 1U,
                                             "Transformable.setTransform", true);
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            Matrix matrix = identity_matrix();
            if (!source->is_null()) {
                auto read = transform_matrix(machine, *source);
                if (!read) return std::unexpected(read.error());
                matrix = *read;
            }
            auto target = generic_transform(machine, *object);
            if (!target) return std::unexpected(target.error());
            auto stored = set_transform_matrix(machine, *target, matrix);
            if (!stored) return std::unexpected(stored.error());
            stored = rebuild_transformable_matrix(machine, *object);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    const auto register_get_transform = [&registry](const char* name,
                                                    bool composite) {
        add(registry, kTransformable, name,
            "(Ljavax/microedition/m3g/Transform;)V",
            [composite](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Transformable.getTransform");
                auto destination = reference_argument(
                    arguments, 1U, "Transformable.getTransform", false);
                if (!object) return std::unexpected(object.error());
                if (!destination) return std::unexpected(destination.error());
                auto source = composite
                    ? local_transform(machine, *object)
                    : generic_transform(machine, *object);
                if (!source) return std::unexpected(source.error());
                auto matrix = transform_matrix(machine, *source);
                if (!matrix) return std::unexpected(matrix.error());
                auto stored = set_transform_matrix(machine, *destination, *matrix);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    register_get_transform("getTransform", false);
    register_get_transform("getCompositeTransform", true);

    add(registry, kTransformable, "setTranslation", "(FFF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.setTranslation");
            auto x = float_argument(arguments, 1U, "Transformable.setTranslation");
            auto y = float_argument(arguments, 2U, "Transformable.setTranslation");
            auto z = float_argument(arguments, 3U, "Transformable.setTranslation");
            if (!object) return std::unexpected(object.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            const std::array<Status, 3> stored {
                set_float_field(machine, *object, kTransformable,
                                "translationX", *x),
                set_float_field(machine, *object, kTransformable,
                                "translationY", *y),
                set_float_field(machine, *object, kTransformable,
                                "translationZ", *z),
            };
            for (const Status& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            auto rebuilt = rebuild_transformable_matrix(machine, *object);
            if (!rebuilt) return std::unexpected(rebuilt.error());
            return std::optional<Value> {};
        });
    add(registry, kTransformable, "getTranslation", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.getTranslation");
            auto destination = reference_argument(arguments, 1U,
                                                  "Transformable.getTranslation", false);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto x = float_field(machine, *object, kTransformable, "translationX");
            auto y = float_field(machine, *object, kTransformable, "translationY");
            auto z = float_field(machine, *object, kTransformable, "translationZ");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            const std::array<float, 3> values {*x, *y, *z};
            auto stored = write_float_array(machine, *destination, values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry, kTransformable, "translate", "(FFF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.translate");
            auto x = float_argument(arguments, 1U, "Transformable.translate");
            auto y = float_argument(arguments, 2U, "Transformable.translate");
            auto z = float_argument(arguments, 3U, "Transformable.translate");
            if (!object) return std::unexpected(object.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            auto current_x = float_field(machine, *object, kTransformable,
                                         "translationX");
            auto current_y = float_field(machine, *object, kTransformable,
                                         "translationY");
            auto current_z = float_field(machine, *object, kTransformable,
                                         "translationZ");
            if (!current_x) return std::unexpected(current_x.error());
            if (!current_y) return std::unexpected(current_y.error());
            if (!current_z) return std::unexpected(current_z.error());
            const std::array<Status, 3> stored {
                set_float_field(machine, *object, kTransformable,
                                "translationX", *current_x + *x),
                set_float_field(machine, *object, kTransformable,
                                "translationY", *current_y + *y),
                set_float_field(machine, *object, kTransformable,
                                "translationZ", *current_z + *z),
            };
            for (const Status& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            auto rebuilt = rebuild_transformable_matrix(machine, *object);
            if (!rebuilt) return std::unexpected(rebuilt.error());
            return std::optional<Value> {};
        });

    add(registry, kTransformable, "scale", "(FFF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.scale");
            auto x = float_argument(arguments, 1U, "Transformable.scale");
            auto y = float_argument(arguments, 2U, "Transformable.scale");
            auto z = float_argument(arguments, 3U, "Transformable.scale");
            if (!object) return std::unexpected(object.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            auto current_x = float_field(machine, *object, kTransformable, "scaleX");
            auto current_y = float_field(machine, *object, kTransformable, "scaleY");
            auto current_z = float_field(machine, *object, kTransformable, "scaleZ");
            if (!current_x) return std::unexpected(current_x.error());
            if (!current_y) return std::unexpected(current_y.error());
            if (!current_z) return std::unexpected(current_z.error());
            const std::array<Status, 3> stored {
                set_float_field(machine, *object, kTransformable,
                                "scaleX", *current_x * *x),
                set_float_field(machine, *object, kTransformable,
                                "scaleY", *current_y * *y),
                set_float_field(machine, *object, kTransformable,
                                "scaleZ", *current_z * *z),
            };
            for (const Status& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            auto rebuilt = rebuild_transformable_matrix(machine, *object);
            if (!rebuilt) return std::unexpected(rebuilt.error());
            return std::optional<Value> {};
        });

    const auto rotation_factory = [](std::span<const Value> arguments)
        -> Result<Quaternion> {
        auto angle = float_argument(arguments, 1U, "Transformable.rotate");
        auto x = float_argument(arguments, 2U, "Transformable.rotate");
        auto y = float_argument(arguments, 3U, "Transformable.rotate");
        auto z = float_argument(arguments, 4U, "Transformable.rotate");
        if (!angle) return std::unexpected(angle.error());
        if (!x) return std::unexpected(x.error());
        if (!y) return std::unexpected(y.error());
        if (!z) return std::unexpected(z.error());
        return axis_angle_quaternion(*angle, *x, *y, *z);
    };
    const auto register_rotation = [&registry, rotation_factory](
        const char* name,
        bool pre) {
        add(registry, kTransformable, name, "(FFFF)V",
            [pre, rotation_factory](Machine& machine,
                                    std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Transformable.rotate");
                auto rotation = rotation_factory(arguments);
                if (!object) return std::unexpected(object.error());
                if (!rotation) return std::unexpected(rotation.error());
                auto current = transformable_quaternion(machine, *object);
                if (!current) return std::unexpected(current.error());
                auto combined = pre
                    ? multiply_quaternion(*rotation, *current)
                    : multiply_quaternion(*current, *rotation);
                if (!combined) return std::unexpected(combined.error());
                auto stored = set_transformable_quaternion(
                    machine, *object, *combined);
                if (!stored) return std::unexpected(stored.error());
                stored = rebuild_transformable_matrix(machine, *object);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    register_rotation("postRotate", false);
    register_rotation("preRotate", true);

    add(registry, kTransformable, "setScale", "(FFF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.setScale");
            auto x = float_argument(arguments, 1U, "Transformable.setScale");
            auto y = float_argument(arguments, 2U, "Transformable.setScale");
            auto z = float_argument(arguments, 3U, "Transformable.setScale");
            if (!object) return std::unexpected(object.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            const std::array<Status, 3> stored {
                set_float_field(machine, *object, kTransformable, "scaleX", *x),
                set_float_field(machine, *object, kTransformable, "scaleY", *y),
                set_float_field(machine, *object, kTransformable, "scaleZ", *z),
            };
            for (const Status& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            auto rebuilt = rebuild_transformable_matrix(machine, *object);
            if (!rebuilt) return std::unexpected(rebuilt.error());
            return std::optional<Value> {};
        });
    add(registry, kTransformable, "getScale", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.getScale");
            auto destination = reference_argument(arguments, 1U,
                                                  "Transformable.getScale", false);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto x = float_field(machine, *object, kTransformable, "scaleX");
            auto y = float_field(machine, *object, kTransformable, "scaleY");
            auto z = float_field(machine, *object, kTransformable, "scaleZ");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            const std::array<float, 3> values {*x, *y, *z};
            auto stored = write_float_array(machine, *destination, values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransformable, "setOrientation", "(FFFF)V",
        [rotation_factory](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.setOrientation");
            auto rotation = rotation_factory(arguments);
            if (!object) return std::unexpected(object.error());
            if (!rotation) return std::unexpected(rotation.error());
            auto stored = set_transformable_quaternion(
                machine, *object, *rotation);
            if (!stored) return std::unexpected(stored.error());
            stored = rebuild_transformable_matrix(machine, *object);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kTransformable, "getOrientation", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Transformable.getOrientation");
            auto destination = reference_argument(arguments, 1U,
                                                  "Transformable.getOrientation", false);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto orientation = transformable_quaternion(machine, *object);
            if (!orientation) return std::unexpected(orientation.error());
            const auto values = quaternion_angle_axis(*orientation);
            auto stored = write_float_array(machine, *destination, values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
}

} // namespace

void register_m3g_transform_natives(NativeMethodRegistry& registry) {
    register_transform(registry);
    register_transformable(registry);
}

} // namespace phoneme::vm
