#include "M3gNativeModules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

[[nodiscard]] Result<Matrix> composite_matrix(Machine& machine,
                                              ObjectRef object) {
    std::vector<ObjectRef> chain;
    ObjectRef current = object;
    for (usize depth = 0; depth < 1'024U && !current.is_null(); ++depth) {
        chain.push_back(current);
        auto parent = reference_field(machine, current, kNode,
                                      "parent", "Ljavax/microedition/m3g/Node;");
        if (!parent) break;
        current = *parent;
    }
    std::reverse(chain.begin(), chain.end());
    Matrix result = identity_matrix();
    for (ObjectRef item : chain) {
        auto local = local_transform(machine, item);
        if (!local) return std::unexpected(local.error());
        auto matrix = transform_matrix(machine, *local);
        if (!matrix) return std::unexpected(matrix.error());
        result = multiply(result, *matrix);
    }
    return result;
}

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
            if (values->empty() || values->size() % 4U != 0U) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Transform.transform requires groups of four floats");
            }
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
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
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
                                             "Transformable.setTransform");
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            Matrix matrix = identity_matrix();
            if (!source->is_null()) {
                auto read = transform_matrix(machine, *source);
                if (!read) return std::unexpected(read.error());
                matrix = *read;
            }
            auto target = local_transform(machine, *object);
            if (!target) return std::unexpected(target.error());
            auto stored = set_transform_matrix(machine, *target, matrix);
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
                Result<Matrix> matrix = composite
                    ? composite_matrix(machine, *object)
                    : [&]() -> Result<Matrix> {
                        auto local = local_transform(machine, *object);
                        if (!local) return std::unexpected(local.error());
                        return transform_matrix(machine, *local);
                    }();
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
            auto local = local_transform(machine, *object);
            if (!local) return std::unexpected(local.error());
            auto matrix = transform_matrix(machine, *local);
            if (!matrix) return std::unexpected(matrix.error());
            (*matrix)[3] = *x;
            (*matrix)[7] = *y;
            (*matrix)[11] = *z;
            auto stored = set_transform_matrix(machine, *local, *matrix);
            if (!stored) return std::unexpected(stored.error());
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
            auto local = local_transform(machine, *object);
            if (!local) return std::unexpected(local.error());
            auto matrix = transform_matrix(machine, *local);
            if (!matrix) return std::unexpected(matrix.error());
            const std::array<float, 3> values {
                (*matrix)[3], (*matrix)[7], (*matrix)[11],
            };
            auto stored = write_float_array(machine, *destination, values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    const auto register_local_operation = [&registry](
        const char* name,
        const char* descriptor,
        bool pre,
        auto factory) {
        add(registry, kTransformable, name, descriptor,
            [pre, factory](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Transformable operation");
                if (!object) return std::unexpected(object.error());
                auto operation = factory(arguments);
                if (!operation) return std::unexpected(operation.error());
                auto local = local_transform(machine, *object);
                if (!local) return std::unexpected(local.error());
                auto current = transform_matrix(machine, *local);
                if (!current) return std::unexpected(current.error());
                auto stored = set_transform_matrix(
                    machine, *local,
                    pre ? multiply(*operation, *current)
                        : multiply(*current, *operation));
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    register_local_operation("translate", "(FFF)V", false,
        [](std::span<const Value> arguments) -> Result<Matrix> {
            auto x = float_argument(arguments, 1U, "Transformable.translate");
            auto y = float_argument(arguments, 2U, "Transformable.translate");
            auto z = float_argument(arguments, 3U, "Transformable.translate");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            return translation_matrix(*x, *y, *z);
        });
    register_local_operation("scale", "(FFF)V", false,
        [](std::span<const Value> arguments) -> Result<Matrix> {
            auto x = float_argument(arguments, 1U, "Transformable.scale");
            auto y = float_argument(arguments, 2U, "Transformable.scale");
            auto z = float_argument(arguments, 3U, "Transformable.scale");
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            return scale_matrix(*x, *y, *z);
        });
    const auto rotation_factory = [](std::span<const Value> arguments)
        -> Result<Matrix> {
        auto angle = float_argument(arguments, 1U, "Transformable.rotate");
        auto x = float_argument(arguments, 2U, "Transformable.rotate");
        auto y = float_argument(arguments, 3U, "Transformable.rotate");
        auto z = float_argument(arguments, 4U, "Transformable.rotate");
        if (!angle) return std::unexpected(angle.error());
        if (!x) return std::unexpected(x.error());
        if (!y) return std::unexpected(y.error());
        if (!z) return std::unexpected(z.error());
        return rotation_matrix(*angle, *x, *y, *z);
    };
    register_local_operation("postRotate", "(FFFF)V", false, rotation_factory);
    register_local_operation("preRotate", "(FFFF)V", true, rotation_factory);

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
            auto local = local_transform(machine, *object);
            if (!local) return std::unexpected(local.error());
            auto current = transform_matrix(machine, *local);
            if (!current) return std::unexpected(current.error());
            Matrix matrix = scale_matrix(*x, *y, *z);
            matrix[3] = (*current)[3];
            matrix[7] = (*current)[7];
            matrix[11] = (*current)[11];
            auto stored = set_transform_matrix(machine, *local, matrix);
            if (!stored) return std::unexpected(stored.error());
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
            auto local = local_transform(machine, *object);
            if (!local) return std::unexpected(local.error());
            auto matrix = transform_matrix(machine, *local);
            if (!matrix) return std::unexpected(matrix.error());
            const std::array<float, 3> values {
                std::sqrt((*matrix)[0] * (*matrix)[0] +
                          (*matrix)[4] * (*matrix)[4] +
                          (*matrix)[8] * (*matrix)[8]),
                std::sqrt((*matrix)[1] * (*matrix)[1] +
                          (*matrix)[5] * (*matrix)[5] +
                          (*matrix)[9] * (*matrix)[9]),
                std::sqrt((*matrix)[2] * (*matrix)[2] +
                          (*matrix)[6] * (*matrix)[6] +
                          (*matrix)[10] * (*matrix)[10]),
            };
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
            auto local = local_transform(machine, *object);
            if (!local) return std::unexpected(local.error());
            auto current = transform_matrix(machine, *local);
            if (!current) return std::unexpected(current.error());
            (*rotation)[3] = (*current)[3];
            (*rotation)[7] = (*current)[7];
            (*rotation)[11] = (*current)[11];
            auto stored = set_transform_matrix(machine, *local, *rotation);
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
            auto local = local_transform(machine, *object);
            if (!local) return std::unexpected(local.error());
            auto matrix = transform_matrix(machine, *local);
            if (!matrix) return std::unexpected(matrix.error());
            const float cosine = std::clamp(
                (((*matrix)[0] + (*matrix)[5] + (*matrix)[10]) - 1.0F) * 0.5F,
                -1.0F, 1.0F);
            const float radians = std::acos(cosine);
            const float sine = std::sin(radians);
            std::array<float, 4> values {
                radians * (180.0F / std::numbers::pi_v<float>),
                0.0F, 0.0F, 1.0F,
            };
            if (std::abs(sine) > 1.0e-5F) {
                values[1] = ((*matrix)[9] - (*matrix)[6]) / (2.0F * sine);
                values[2] = ((*matrix)[2] - (*matrix)[8]) / (2.0F * sine);
                values[3] = ((*matrix)[4] - (*matrix)[1]) / (2.0F * sine);
            }
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
