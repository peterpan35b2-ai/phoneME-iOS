#include "Micro3dNativeModules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>

#include "Micro3dNativeSupport.hpp"

namespace phoneme::vm {
namespace micro3d {
namespace {

void register_vector(NativeMethodRegistry& registry) {
    m3g::add(registry, kVector3D, "<init>", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Vector3D.<init>");
            if (!self) return std::unexpected(self.error());
            auto stored = write_vector(machine, *self, {});
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    m3g::add(registry, kVector3D, "<init>", "(III)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Vector3D.<init>");
            auto x = m3g::int_argument(args, 1U, "Vector3D.<init>");
            auto y = m3g::int_argument(args, 2U, "Vector3D.<init>");
            auto z = m3g::int_argument(args, 3U, "Vector3D.<init>");
            if (!self) return std::unexpected(self.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            auto stored = write_vector(machine, *self, {*x, *y, *z});
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    const auto copy = [](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "Vector3D copy");
        auto source = m3g::reference_argument(args, 1U, "Vector3D copy", false);
        if (!self) return std::unexpected(self.error());
        if (!source) return std::unexpected(source.error());
        auto value = read_vector(machine, *source, "Vector3D copy");
        if (!value) return std::unexpected(value.error());
        auto stored = write_vector(machine, *self, *value);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    m3g::add(registry, kVector3D, "<init>",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V", copy);
    m3g::add(registry, kVector3D, "set",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V", copy);

    for (const auto& [method_name, field_name] :
         std::array<std::pair<const char*, const char*>, 3> {{
             {"getX", "x"}, {"getY", "y"}, {"getZ", "z"}}}) {
        m3g::add(registry, kVector3D, method_name, "()I",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Vector3D getter");
                if (!self) return std::unexpected(self.error());
                auto value = int_field(machine, *self, kVector3D, field_name);
                if (!value) return std::unexpected(value.error());
                return int_result(*value);
            });
    }

    m3g::add(registry, kVector3D, "innerProduct",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)I",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto left_ref = m3g::receiver(args, "Vector3D.innerProduct");
            auto right_ref = m3g::reference_argument(
                args, 1U, "Vector3D.innerProduct", false);
            if (!left_ref) return std::unexpected(left_ref.error());
            if (!right_ref) return std::unexpected(right_ref.error());
            auto left = read_vector(machine, *left_ref, "Vector3D.innerProduct");
            auto right = read_vector(machine, *right_ref, "Vector3D.innerProduct");
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            return int_result(wrap_i64(
                static_cast<i64>(left->x) * right->x +
                static_cast<i64>(left->y) * right->y +
                static_cast<i64>(left->z) * right->z));
        });
    m3g::add(registry, kVector3D, "innerProduct",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
             "Lcom/mascotcapsule/micro3d/v3/Vector3D;)I",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto left_ref = m3g::reference_argument(
                args, 0U, "Vector3D.innerProduct", false);
            auto right_ref = m3g::reference_argument(
                args, 1U, "Vector3D.innerProduct", false);
            if (!left_ref) return std::unexpected(left_ref.error());
            if (!right_ref) return std::unexpected(right_ref.error());
            auto left = read_vector(machine, *left_ref, "Vector3D.innerProduct");
            auto right = read_vector(machine, *right_ref, "Vector3D.innerProduct");
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            return int_result(wrap_i64(
                static_cast<i64>(left->x) * right->x +
                static_cast<i64>(left->y) * right->y +
                static_cast<i64>(left->z) * right->z));
        });

    const auto cross_product = [](VectorValue left, VectorValue right) {
        return VectorValue {
            .x = wrap_i64(static_cast<i64>(left.y) * right.z -
                          static_cast<i64>(left.z) * right.y),
            .y = wrap_i64(static_cast<i64>(left.z) * right.x -
                          static_cast<i64>(left.x) * right.z),
            .z = wrap_i64(static_cast<i64>(left.x) * right.y -
                          static_cast<i64>(left.y) * right.x),
        };
    };
    m3g::add(registry, kVector3D, "outerProduct",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V",
        [cross_product](Machine& machine,
                        std::span<const Value> args) -> NativeResult {
            auto left_ref = m3g::receiver(args, "Vector3D.outerProduct");
            auto right_ref = m3g::reference_argument(
                args, 1U, "Vector3D.outerProduct", false);
            if (!left_ref) return std::unexpected(left_ref.error());
            if (!right_ref) return std::unexpected(right_ref.error());
            auto left = read_vector(machine, *left_ref, "Vector3D.outerProduct");
            auto right = read_vector(machine, *right_ref, "Vector3D.outerProduct");
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            auto stored = write_vector(machine, *left_ref,
                                       cross_product(*left, *right));
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    m3g::add(registry, kVector3D, "outerProduct",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
             "Lcom/mascotcapsule/micro3d/v3/Vector3D;)"
             "Lcom/mascotcapsule/micro3d/v3/Vector3D;",
        [cross_product](Machine& machine,
                        std::span<const Value> args) -> NativeResult {
            auto left_ref = m3g::reference_argument(
                args, 0U, "Vector3D.outerProduct", false);
            auto right_ref = m3g::reference_argument(
                args, 1U, "Vector3D.outerProduct", false);
            if (!left_ref) return std::unexpected(left_ref.error());
            if (!right_ref) return std::unexpected(right_ref.error());
            auto left = read_vector(machine, *left_ref, "Vector3D.outerProduct");
            auto right = read_vector(machine, *right_ref, "Vector3D.outerProduct");
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            auto result = create_vector(machine, cross_product(*left, *right));
            if (!result) return std::unexpected(result.error());
            return reference_result(*result);
        });

    m3g::add(registry, kVector3D, "set", "(III)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Vector3D.set");
            auto x = m3g::int_argument(args, 1U, "Vector3D.set");
            auto y = m3g::int_argument(args, 2U, "Vector3D.set");
            auto z = m3g::int_argument(args, 3U, "Vector3D.set");
            if (!self) return std::unexpected(self.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!z) return std::unexpected(z.error());
            auto stored = write_vector(machine, *self, {*x, *y, *z});
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    for (const auto& [method_name, field_name] :
         std::array<std::pair<const char*, const char*>, 3> {{
             {"setX", "x"}, {"setY", "y"}, {"setZ", "z"}}}) {
        m3g::add(registry, kVector3D, method_name, "(I)V",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Vector3D setter");
                auto value = m3g::int_argument(args, 1U, "Vector3D setter");
                if (!self) return std::unexpected(self.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(
                    machine, *self, kVector3D, field_name, *value);
                if (!stored) return std::unexpected(stored.error());
                return void_result();
            });
    }
    m3g::add(registry, kVector3D, "unit", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Vector3D.unit");
            if (!self) return std::unexpected(self.error());
            auto value = read_vector(machine, *self, "Vector3D.unit");
            if (!value) return std::unexpected(value.error());
            const long double length = std::sqrt(
                static_cast<long double>(value->x) * value->x +
                static_cast<long double>(value->y) * value->y +
                static_cast<long double>(value->z) * value->z);
            VectorValue result {0, 0, 4096};
            if (length > 0.0L) {
                result.x = static_cast<i32>(std::llround(
                    static_cast<long double>(value->x) * 4096.0L / length));
                result.y = static_cast<i32>(std::llround(
                    static_cast<long double>(value->y) * 4096.0L / length));
                result.z = static_cast<i32>(std::llround(
                    static_cast<long double>(value->z) * 4096.0L / length));
            }
            auto stored = write_vector(machine, *self, result);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
}

void register_util(NativeMethodRegistry& registry) {
    m3g::add(registry, kUtil3D, "sqrt", "(I)I",
        [](Machine&, std::span<const Value> args) -> NativeResult {
            auto value = m3g::int_argument(args, 0U, "Util3D.sqrt");
            if (!value) return std::unexpected(value.error());
            return int_result(unsigned_sqrt(*value));
        });
    m3g::add(registry, kUtil3D, "sin", "(I)I",
        [](Machine&, std::span<const Value> args) -> NativeResult {
            auto value = m3g::int_argument(args, 0U, "Util3D.sin");
            if (!value) return std::unexpected(value.error());
            return int_result(integer_sin(*value));
        });
    m3g::add(registry, kUtil3D, "cos", "(I)I",
        [](Machine&, std::span<const Value> args) -> NativeResult {
            auto value = m3g::int_argument(args, 0U, "Util3D.cos");
            if (!value) return std::unexpected(value.error());
            return int_result(integer_cos(*value));
        });
}

[[nodiscard]] NativeResult set_affine_from_array(
    Machine& machine, std::span<const Value> args, usize offset_argument) {
    auto self = m3g::receiver(args, "AffineTrans.set");
    auto array = m3g::reference_argument(args, 1U, "AffineTrans.set", false);
    if (!self) return std::unexpected(self.error());
    if (!array) return std::unexpected(array.error());
    i32 offset = 0;
    if (offset_argument != 0U) {
        auto parsed = m3g::int_argument(args, offset_argument, "AffineTrans.set");
        if (!parsed) return std::unexpected(parsed.error());
        offset = *parsed;
    }
    auto values = read_int_array(machine, *array, "AffineTrans.set");
    if (!values) return std::unexpected(values.error());
    if (offset < 0 || static_cast<usize>(offset) > values->size() ||
        values->size() - static_cast<usize>(offset) < 12U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "AffineTrans requires 12 integers");
    }
    AffineValue affine;
    std::copy_n(values->begin() + offset, 12, affine.m.begin());
    auto stored = write_affine(machine, *self, affine);
    if (!stored) return std::unexpected(stored.error());
    return void_result();
}

void register_affine(NativeMethodRegistry& registry) {
    m3g::add(registry, kAffineTrans, "<init>", "()V",
        [](Machine&, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "AffineTrans.<init>");
            if (!self) return std::unexpected(self.error());
            return void_result();
        });
    const auto copy = [](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans copy");
        auto source = m3g::reference_argument(args, 1U, "AffineTrans copy", false);
        if (!self) return std::unexpected(self.error());
        if (!source) return std::unexpected(source.error());
        auto value = read_affine(machine, *source, "AffineTrans copy");
        if (!value) return std::unexpected(value.error());
        auto stored = write_affine(machine, *self, *value);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    m3g::add(registry, kAffineTrans, "<init>",
             "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V", copy);
    m3g::add(registry, kAffineTrans, "set",
             "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V", copy);
    m3g::add(registry, kAffineTrans, "<init>", "([I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            return set_affine_from_array(machine, args, 0U);
        });
    m3g::add(registry, kAffineTrans, "set", "([I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            return set_affine_from_array(machine, args, 0U);
        });
    m3g::add(registry, kAffineTrans, "<init>", "([II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            return set_affine_from_array(machine, args, 2U);
        });
    m3g::add(registry, kAffineTrans, "set", "([II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            return set_affine_from_array(machine, args, 2U);
        });

    const auto set_matrix = [](Machine& machine,
                               std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.set");
        auto outer = m3g::reference_argument(args, 1U, "AffineTrans.set", false);
        if (!self) return std::unexpected(self.error());
        if (!outer) return std::unexpected(outer.error());
        auto class_name = machine.heap().class_name(*outer);
        auto length = machine.heap().array_length(*outer);
        if (!class_name) return std::unexpected(class_name.error());
        if (!length) return std::unexpected(length.error());
        if (*class_name != "[[I" || *length < 3U) {
            return fail_java("java/lang/IllegalArgumentException",
                             "AffineTrans requires int[3][4]");
        }
        AffineValue affine;
        for (usize row = 0; row < 3U; ++row) {
            auto row_value = machine.heap().element(*outer, row);
            if (!row_value) return std::unexpected(row_value.error());
            auto row_ref = row_value->as_reference();
            if (!row_ref) return std::unexpected(row_ref.error());
            auto values = read_int_array(machine, *row_ref, "AffineTrans.set");
            if (!values) return std::unexpected(values.error());
            if (values->size() < 4U) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "AffineTrans row requires four integers");
            }
            std::copy_n(values->begin(), 4, affine.m.begin() + row * 4U);
        }
        auto stored = write_affine(machine, *self, affine);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    m3g::add(registry, kAffineTrans, "<init>", "([[I)V", set_matrix);
    m3g::add(registry, kAffineTrans, "set", "([[I)V", set_matrix);

    const auto set_scalars = [](Machine& machine,
                                std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.set");
        if (!self) return std::unexpected(self.error());
        AffineValue value;
        for (usize index = 0; index < value.m.size(); ++index) {
            auto item = m3g::int_argument(args, index + 1U, "AffineTrans.set");
            if (!item) return std::unexpected(item.error());
            value.m[index] = *item;
        }
        auto stored = write_affine(machine, *self, value);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    m3g::add(registry, kAffineTrans, "<init>", "(IIIIIIIIIIII)V", set_scalars);
    m3g::add(registry, kAffineTrans, "set", "(IIIIIIIIIIII)V", set_scalars);

    m3g::add(registry, kAffineTrans, "get", "([I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "AffineTrans.get");
            auto array = m3g::reference_argument(args, 1U, "AffineTrans.get", false);
            if (!self) return std::unexpected(self.error());
            if (!array) return std::unexpected(array.error());
            auto value = read_affine(machine, *self, "AffineTrans.get");
            if (!value) return std::unexpected(value.error());
            auto stored = write_int_array(
                machine, *array, value->m, 0U, "AffineTrans.get");
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    m3g::add(registry, kAffineTrans, "get", "([II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "AffineTrans.get");
            auto array = m3g::reference_argument(args, 1U, "AffineTrans.get", false);
            auto offset = m3g::int_argument(args, 2U, "AffineTrans.get");
            if (!self) return std::unexpected(self.error());
            if (!array) return std::unexpected(array.error());
            if (!offset) return std::unexpected(offset.error());
            if (*offset < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "AffineTrans destination offset is negative");
            }
            auto value = read_affine(machine, *self, "AffineTrans.get");
            if (!value) return std::unexpected(value.error());
            auto stored = write_int_array(machine, *array, value->m,
                                          static_cast<usize>(*offset),
                                          "AffineTrans.get");
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });

    m3g::add(registry, kAffineTrans, "setIdentity", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "AffineTrans.setIdentity");
            if (!self) return std::unexpected(self.error());
            auto stored = write_affine(machine, *self, identity_affine());
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });

    const auto axis = [](usize axis_index) {
        return [axis_index](Machine& machine,
                            std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "AffineTrans.rotation");
            auto angle = m3g::int_argument(args, 1U, "AffineTrans.rotation");
            if (!self) return std::unexpected(self.error());
            if (!angle) return std::unexpected(angle.error());
            const i32 cosine = integer_cos(*angle);
            const i32 sine = integer_sin(*angle);
            auto current = read_affine(machine, *self, "AffineTrans.rotation");
            if (!current) return std::unexpected(current.error());
            AffineValue value = *current;
            if (axis_index == 0U) {
                value.m[0] = 4096; value.m[1] = 0; value.m[2] = 0;
                value.m[4] = 0; value.m[5] = cosine; value.m[6] = -sine;
                value.m[8] = 0; value.m[9] = sine; value.m[10] = cosine;
            } else if (axis_index == 1U) {
                value.m[0] = cosine; value.m[1] = 0; value.m[2] = sine;
                value.m[4] = 0; value.m[5] = 4096; value.m[6] = 0;
                value.m[8] = -sine; value.m[9] = 0; value.m[10] = cosine;
            } else {
                value.m[0] = cosine; value.m[1] = -sine; value.m[2] = 0;
                value.m[4] = sine; value.m[5] = cosine; value.m[6] = 0;
                value.m[8] = 0; value.m[9] = 0; value.m[10] = 4096;
            }
            auto stored = write_affine(machine, *self, value);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        };
    };
    m3g::add(registry, kAffineTrans, "rotationX", "(I)V", axis(0U));
    m3g::add(registry, kAffineTrans, "setRotationX", "(I)V", axis(0U));
    m3g::add(registry, kAffineTrans, "rotationY", "(I)V", axis(1U));
    m3g::add(registry, kAffineTrans, "setRotationY", "(I)V", axis(1U));
    m3g::add(registry, kAffineTrans, "rotationZ", "(I)V", axis(2U));
    m3g::add(registry, kAffineTrans, "setRotationZ", "(I)V", axis(2U));

    const auto arbitrary_rotation = [](Machine& machine,
                                       std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.setRotation");
        auto vector_ref = m3g::reference_argument(
            args, 1U, "AffineTrans.setRotation", false);
        auto angle = m3g::int_argument(args, 2U, "AffineTrans.setRotation");
        if (!self) return std::unexpected(self.error());
        if (!vector_ref) return std::unexpected(vector_ref.error());
        if (!angle) return std::unexpected(angle.error());
        auto vector = read_vector(machine, *vector_ref, "AffineTrans.setRotation");
        auto current = read_affine(machine, *self, "AffineTrans.setRotation");
        if (!vector) return std::unexpected(vector.error());
        if (!current) return std::unexpected(current.error());
        auto rotation = axis_rotation(*vector, *angle);
        rotation.m[3] = current->m[3];
        rotation.m[7] = current->m[7];
        rotation.m[11] = current->m[11];
        auto stored = write_affine(machine, *self, rotation);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    m3g::add(registry, kAffineTrans, "rotationV",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;I)V",
             arbitrary_rotation);
    m3g::add(registry, kAffineTrans, "setRotation",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;I)V",
             arbitrary_rotation);

    const auto multiply_one = [](Machine& machine,
                                 std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.mul");
        auto right_ref = m3g::reference_argument(args, 1U, "AffineTrans.mul", false);
        if (!self) return std::unexpected(self.error());
        if (!right_ref) return std::unexpected(right_ref.error());
        auto left = read_affine(machine, *self, "AffineTrans.mul");
        auto right = read_affine(machine, *right_ref, "AffineTrans.mul");
        if (!left) return std::unexpected(left.error());
        if (!right) return std::unexpected(right.error());
        auto stored = write_affine(machine, *self,
                                   multiply_affine(*left, *right));
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    const auto multiply_two = [](Machine& machine,
                                 std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.mul");
        auto left_ref = m3g::reference_argument(args, 1U, "AffineTrans.mul", false);
        auto right_ref = m3g::reference_argument(args, 2U, "AffineTrans.mul", false);
        if (!self) return std::unexpected(self.error());
        if (!left_ref) return std::unexpected(left_ref.error());
        if (!right_ref) return std::unexpected(right_ref.error());
        auto left = read_affine(machine, *left_ref, "AffineTrans.mul");
        auto right = read_affine(machine, *right_ref, "AffineTrans.mul");
        if (!left) return std::unexpected(left.error());
        if (!right) return std::unexpected(right.error());
        auto stored = write_affine(machine, *self,
                                   multiply_affine(*left, *right));
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    for (const char* name : {"mul", "multiply"}) {
        m3g::add(registry, kAffineTrans, name,
                 "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V",
                 multiply_one);
        m3g::add(registry, kAffineTrans, name,
                 "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;"
                 "Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V",
                 multiply_two);
    }

    const auto transform = [](Machine& machine,
                              std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.transform");
        auto vector_ref = m3g::reference_argument(
            args, 1U, "AffineTrans.transform", false);
        if (!self) return std::unexpected(self.error());
        if (!vector_ref) return std::unexpected(vector_ref.error());
        auto affine = read_affine(machine, *self, "AffineTrans.transform");
        auto vector = read_vector(machine, *vector_ref, "AffineTrans.transform");
        if (!affine) return std::unexpected(affine.error());
        if (!vector) return std::unexpected(vector.error());
        VectorValue result;
        result.x = wrap_i64(static_cast<i64>(fixed_round(
            static_cast<i64>(vector->x) * affine->m[0] +
            static_cast<i64>(vector->y) * affine->m[1] +
            static_cast<i64>(vector->z) * affine->m[2])) + affine->m[3]);
        result.y = wrap_i64(static_cast<i64>(fixed_round(
            static_cast<i64>(vector->x) * affine->m[4] +
            static_cast<i64>(vector->y) * affine->m[5] +
            static_cast<i64>(vector->z) * affine->m[6])) + affine->m[7]);
        result.z = wrap_i64(static_cast<i64>(fixed_round(
            static_cast<i64>(vector->x) * affine->m[8] +
            static_cast<i64>(vector->y) * affine->m[9] +
            static_cast<i64>(vector->z) * affine->m[10])) + affine->m[11]);
        auto output = create_vector(machine, result);
        if (!output) return std::unexpected(output.error());
        return reference_result(*output);
    };
    m3g::add(registry, kAffineTrans, "transform",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)"
             "Lcom/mascotcapsule/micro3d/v3/Vector3D;", transform);
    m3g::add(registry, kAffineTrans, "transPoint",
             "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)"
             "Lcom/mascotcapsule/micro3d/v3/Vector3D;", transform);

    const auto look_at = [](Machine& machine,
                            std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "AffineTrans.lookAt");
        auto pos_ref = m3g::reference_argument(args, 1U, "AffineTrans.lookAt", false);
        auto look_ref = m3g::reference_argument(args, 2U, "AffineTrans.lookAt", false);
        auto up_ref = m3g::reference_argument(args, 3U, "AffineTrans.lookAt", false);
        if (!self) return std::unexpected(self.error());
        if (!pos_ref) return std::unexpected(pos_ref.error());
        if (!look_ref) return std::unexpected(look_ref.error());
        if (!up_ref) return std::unexpected(up_ref.error());
        auto pos = read_vector(machine, *pos_ref, "AffineTrans.lookAt");
        auto look = read_vector(machine, *look_ref, "AffineTrans.lookAt");
        auto up = read_vector(machine, *up_ref, "AffineTrans.lookAt");
        if (!pos) return std::unexpected(pos.error());
        if (!look) return std::unexpected(look.error());
        if (!up) return std::unexpected(up.error());
        const auto cross = [](VectorValue a, VectorValue b) {
            return VectorValue {
                wrap_i64(static_cast<i64>(a.y) * b.z - static_cast<i64>(a.z) * b.y),
                wrap_i64(static_cast<i64>(a.z) * b.x - static_cast<i64>(a.x) * b.z),
                wrap_i64(static_cast<i64>(a.x) * b.y - static_cast<i64>(a.y) * b.x),
            };
        };
        const auto normalize = [](VectorValue value) {
            const long double length = std::sqrt(
                static_cast<long double>(value.x) * value.x +
                static_cast<long double>(value.y) * value.y +
                static_cast<long double>(value.z) * value.z);
            if (length <= 0.0L) return VectorValue {0, 0, 4096};
            return VectorValue {
                static_cast<i32>(std::llround(value.x * 4096.0L / length)),
                static_cast<i32>(std::llround(value.y * 4096.0L / length)),
                static_cast<i32>(std::llround(value.z * 4096.0L / length)),
            };
        };
        const VectorValue row0 = normalize(cross(*look, *up));
        const VectorValue row1 = normalize(cross(*look, row0));
        const VectorValue row2 = normalize(*look);
        const i64 px = -static_cast<i64>(pos->x);
        const i64 py = -static_cast<i64>(pos->y);
        const i64 pz = -static_cast<i64>(pos->z);
        const AffineValue affine {.m = {
            row0.x, row0.y, row0.z,
            fixed_round(px * row0.x + py * row0.y + pz * row0.z),
            row1.x, row1.y, row1.z,
            fixed_round(px * row1.x + py * row1.y + pz * row1.z),
            row2.x, row2.y, row2.z,
            fixed_round(px * row2.x + py * row2.y + pz * row2.z),
        }};
        auto stored = write_affine(machine, *self, affine);
        if (!stored) return std::unexpected(stored.error());
        return void_result();
    };
    constexpr const char* descriptor =
        "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
        "Lcom/mascotcapsule/micro3d/v3/Vector3D;"
        "Lcom/mascotcapsule/micro3d/v3/Vector3D;)V";
    m3g::add(registry, kAffineTrans, "lookAt", descriptor, look_at);
    m3g::add(registry, kAffineTrans, "setViewTrans", descriptor, look_at);
}

} // namespace
} // namespace micro3d

void register_micro3d_math_natives(NativeMethodRegistry& registry) {
    micro3d::register_vector(registry);
    micro3d::register_util(registry);
    micro3d::register_affine(registry);
}

} // namespace phoneme::vm
