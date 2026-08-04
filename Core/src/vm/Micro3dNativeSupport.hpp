#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "GraphicsNativeSupport.hpp"
#include "M3gNativeSupport.hpp"

namespace phoneme::vm::micro3d {

constexpr const char* kVector3D = "com/mascotcapsule/micro3d/v3/Vector3D";
constexpr const char* kAffineTrans = "com/mascotcapsule/micro3d/v3/AffineTrans";
constexpr const char* kUtil3D = "com/mascotcapsule/micro3d/v3/Util3D";
constexpr const char* kLight = "com/mascotcapsule/micro3d/v3/Light";
constexpr const char* kTexture = "com/mascotcapsule/micro3d/v3/Texture";
constexpr const char* kEffect3D = "com/mascotcapsule/micro3d/v3/Effect3D";
constexpr const char* kFigureLayout = "com/mascotcapsule/micro3d/v3/FigureLayout";
constexpr const char* kActionTable = "com/mascotcapsule/micro3d/v3/ActionTable";
constexpr const char* kFigure = "com/mascotcapsule/micro3d/v3/Figure";
constexpr const char* kGraphics3D = "com/mascotcapsule/micro3d/v3/Graphics3D";

using NativeResult = Result<std::optional<Value>>;

[[nodiscard]] inline NativeResult void_result() {
    return std::optional<Value> {};
}

[[nodiscard]] inline NativeResult int_result(i32 value) {
    return std::optional<Value>(Value::from_int(value));
}

[[nodiscard]] inline NativeResult reference_result(ObjectRef value) {
    return std::optional<Value>(Value::from_reference(value));
}

[[nodiscard]] inline i32 wrap_i64(i64 value) noexcept {
    return std::bit_cast<i32>(static_cast<u32>(static_cast<u64>(value)));
}

[[nodiscard]] inline i64 arithmetic_shift_12(i64 value) noexcept {
    return value >= 0 ? value / 4096 : -(((-value) + 4095) / 4096);
}

[[nodiscard]] inline i32 fixed_round(i64 value) noexcept {
    return wrap_i64(arithmetic_shift_12(value + 2048));
}

[[nodiscard]] inline Result<i32> int_field(
    Machine& machine, ObjectRef object, std::string_view owner,
    std::string_view name, std::string_view descriptor = "I") {
    return m3g::int_field(machine, object, owner, name, descriptor);
}

[[nodiscard]] inline Status set_int_field(
    Machine& machine, ObjectRef object, std::string_view owner,
    std::string_view name, i32 value, std::string_view descriptor = "I") {
    return m3g::set_int_field(machine, object, owner, name, value, descriptor);
}

[[nodiscard]] inline Result<ObjectRef> reference_field(
    Machine& machine, ObjectRef object, std::string_view owner,
    std::string_view name, std::string_view descriptor) {
    return m3g::reference_field(machine, object, owner, name, descriptor);
}

[[nodiscard]] inline Status set_reference_field(
    Machine& machine, ObjectRef object, std::string_view owner,
    std::string_view name, std::string_view descriptor, ObjectRef value) {
    return m3g::set_reference_field(machine, object, owner, name, descriptor,
                                    value);
}

[[nodiscard]] inline Result<std::vector<i32>> read_int_array(
    Machine& machine, ObjectRef array, std::string_view operation) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " int[] is null");
    }
    auto class_name = machine.heap().class_name(array);
    auto length = machine.heap().array_length(array);
    if (!class_name) return std::unexpected(class_name.error());
    if (!length) return std::unexpected(length.error());
    if (*class_name != "[I") {
        return fail_java("java/lang/IllegalArgumentException",
                         std::string(operation) + " expects int[]");
    }
    std::vector<i32> result;
    result.reserve(*length);
    for (usize index = 0; index < *length; ++index) {
        auto value = machine.heap().element(array, index);
        if (!value) return std::unexpected(value.error());
        auto integer = value->as_int();
        if (!integer) return std::unexpected(integer.error());
        result.push_back(*integer);
    }
    return result;
}

[[nodiscard]] inline Status write_int_array(
    Machine& machine, ObjectRef array, std::span<const i32> values,
    usize offset, std::string_view operation) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " int[] is null");
    }
    auto class_name = machine.heap().class_name(array);
    auto length = machine.heap().array_length(array);
    if (!class_name) return std::unexpected(class_name.error());
    if (!length) return std::unexpected(length.error());
    if (*class_name != "[I" || offset > *length ||
        values.size() > *length - offset) {
        return fail_java("java/lang/IllegalArgumentException",
                         std::string(operation) + " int[] range is invalid");
    }
    for (usize index = 0; index < values.size(); ++index) {
        auto stored = machine.heap().set_element(
            array, offset + index, Value::from_int(values[index]));
        if (!stored) return stored;
    }
    return {};
}

[[nodiscard]] inline Result<ObjectRef> create_int_array(
    Machine& machine, std::span<const i32> values) {
    auto array = m3g::allocate_array(machine, "[I", values.size(),
                                     Value::from_int(0));
    if (!array) return std::unexpected(array.error());
    auto stored = write_int_array(machine, *array, values, 0U,
                                  "Micro3D array creation");
    if (!stored) return std::unexpected(stored.error());
    return *array;
}

struct VectorValue final {
    i32 x {0};
    i32 y {0};
    i32 z {0};
};

[[nodiscard]] inline Result<VectorValue> read_vector(
    Machine& machine, ObjectRef object, std::string_view operation) {
    if (object.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " vector is null");
    }
    auto x = int_field(machine, object, kVector3D, "x");
    auto y = int_field(machine, object, kVector3D, "y");
    auto z = int_field(machine, object, kVector3D, "z");
    if (!x) return std::unexpected(x.error());
    if (!y) return std::unexpected(y.error());
    if (!z) return std::unexpected(z.error());
    return VectorValue {.x = *x, .y = *y, .z = *z};
}

[[nodiscard]] inline Status write_vector(Machine& machine, ObjectRef object,
                                         VectorValue value) {
    auto x = set_int_field(machine, object, kVector3D, "x", value.x);
    auto y = set_int_field(machine, object, kVector3D, "y", value.y);
    auto z = set_int_field(machine, object, kVector3D, "z", value.z);
    if (!x) return x;
    if (!y) return y;
    return z;
}

[[nodiscard]] inline Result<ObjectRef> create_vector(Machine& machine,
                                                     VectorValue value) {
    auto object = m3g::allocate_instance(machine, kVector3D);
    if (!object) return std::unexpected(object.error());
    auto stored = write_vector(machine, *object, value);
    if (!stored) return std::unexpected(stored.error());
    return *object;
}

[[nodiscard]] inline i32 unsigned_sqrt(i32 value) noexcept {
    if (value == 0) return 0;
    if (value < 0 && value > -196606) return 65535;
    return static_cast<i32>(std::llround(
        std::sqrt(static_cast<double>(static_cast<u32>(value)))));
}

[[nodiscard]] inline i32 integer_sin(i32 angle) noexcept {
    const double radians = static_cast<double>(angle) * std::numbers::pi / 2048.0;
    return static_cast<i32>(std::llround(std::sin(radians) * 4096.0));
}

[[nodiscard]] inline i32 integer_cos(i32 angle) noexcept {
    return integer_sin(wrap_i64(static_cast<i64>(angle) + 1024));
}

struct AffineValue final {
    std::array<i32, 12> m {};
};

constexpr std::array<std::string_view, 12> kAffineFields {
    "m00", "m01", "m02", "m03", "m10", "m11",
    "m12", "m13", "m20", "m21", "m22", "m23",
};

[[nodiscard]] inline Result<AffineValue> read_affine(
    Machine& machine, ObjectRef object, std::string_view operation) {
    if (object.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " affine transform is null");
    }
    AffineValue value;
    for (usize index = 0; index < value.m.size(); ++index) {
        auto field = int_field(machine, object, kAffineTrans,
                               kAffineFields[index]);
        if (!field) return std::unexpected(field.error());
        value.m[index] = *field;
    }
    return value;
}

[[nodiscard]] inline Status write_affine(Machine& machine, ObjectRef object,
                                         const AffineValue& value) {
    for (usize index = 0; index < value.m.size(); ++index) {
        auto stored = set_int_field(machine, object, kAffineTrans,
                                    kAffineFields[index], value.m[index]);
        if (!stored) return stored;
    }
    return {};
}

[[nodiscard]] inline AffineValue identity_affine() noexcept {
    return AffineValue {.m = {4096, 0, 0, 0,
                              0, 4096, 0, 0,
                              0, 0, 4096, 0}};
}

[[nodiscard]] inline AffineValue multiply_affine(
    const AffineValue& left, const AffineValue& right) noexcept {
    AffineValue result;
    for (usize row = 0; row < 3U; ++row) {
        for (usize column = 0; column < 3U; ++column) {
            const i64 value =
                static_cast<i64>(left.m[row * 4U]) * right.m[column] +
                static_cast<i64>(left.m[row * 4U + 1U]) *
                    right.m[4U + column] +
                static_cast<i64>(left.m[row * 4U + 2U]) *
                    right.m[8U + column];
            result.m[row * 4U + column] = fixed_round(value);
        }
        const i64 translation =
            static_cast<i64>(left.m[row * 4U]) * right.m[3U] +
            static_cast<i64>(left.m[row * 4U + 1U]) * right.m[7U] +
            static_cast<i64>(left.m[row * 4U + 2U]) * right.m[11U];
        result.m[row * 4U + 3U] = wrap_i64(
            static_cast<i64>(fixed_round(translation)) +
            left.m[row * 4U + 3U]);
    }
    return result;
}

[[nodiscard]] inline AffineValue axis_rotation(VectorValue axis,
                                               i32 angle) noexcept {
    const i32 cosine = integer_cos(angle);
    const i32 sine = integer_sin(angle);
    const i32 xs = fixed_round(static_cast<i64>(axis.x) * sine);
    const i32 ys = fixed_round(static_cast<i64>(axis.y) * sine);
    const i32 zs = fixed_round(static_cast<i64>(axis.z) * sine);
    const i32 nc = 4096 - cosine;
    const i32 xy = fixed_round(static_cast<i64>(axis.x) * axis.y);
    const i32 yz = fixed_round(static_cast<i64>(axis.y) * axis.z);
    const i32 zx = fixed_round(static_cast<i64>(axis.z) * axis.x);
    const i32 xx = fixed_round(static_cast<i64>(axis.x) * axis.x);
    const i32 yy = fixed_round(static_cast<i64>(axis.y) * axis.y);
    const i32 zz = fixed_round(static_cast<i64>(axis.z) * axis.z);
    return AffineValue {.m = {
        wrap_i64(static_cast<i64>(cosine) +
                 fixed_round(static_cast<i64>(xx) * nc)),
        wrap_i64(static_cast<i64>(fixed_round(static_cast<i64>(xy) * nc)) - zs),
        wrap_i64(static_cast<i64>(fixed_round(static_cast<i64>(zx) * nc)) + ys), 0,
        wrap_i64(static_cast<i64>(zs) +
                 fixed_round(static_cast<i64>(xy) * nc)),
        wrap_i64(static_cast<i64>(cosine) +
                 fixed_round(static_cast<i64>(yy) * nc)),
        wrap_i64(static_cast<i64>(fixed_round(static_cast<i64>(yz) * nc)) - xs), 0,
        wrap_i64(static_cast<i64>(fixed_round(static_cast<i64>(zx) * nc)) - ys),
        wrap_i64(static_cast<i64>(xs) +
                 fixed_round(static_cast<i64>(yz) * nc)),
        wrap_i64(static_cast<i64>(cosine) +
                 fixed_round(static_cast<i64>(zz) * nc)), 0,
    }};
}

[[nodiscard]] inline Result<std::vector<u8>> byte_array(
    Machine& machine, ObjectRef array, std::string_view operation) {
    auto length = machine.heap().array_length(array);
    if (!length) return std::unexpected(length.error());
    if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
        return fail_java("java/lang/IllegalArgumentException",
                         std::string(operation) + " byte[] is too large");
    }
    return graphics_native::byte_array_slice(
        machine, array, 0, static_cast<i32>(*length), operation);
}

[[nodiscard]] inline u16 read_u16_le(std::span<const u8> bytes,
                                     usize offset) noexcept {
    return static_cast<u16>(static_cast<u16>(bytes[offset]) |
                            (static_cast<u16>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] inline u32 read_u32_le(std::span<const u8> bytes,
                                     usize offset) noexcept {
    return static_cast<u32>(bytes[offset]) |
           (static_cast<u32>(bytes[offset + 1U]) << 8U) |
           (static_cast<u32>(bytes[offset + 2U]) << 16U) |
           (static_cast<u32>(bytes[offset + 3U]) << 24U);
}

class ByteCursor final {
public:
    explicit ByteCursor(std::span<const u8> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool skip(usize count) noexcept {
        if (count > bytes_.size() - position_) return false;
        position_ += count;
        return true;
    }

    [[nodiscard]] bool byte(u8& value) noexcept {
        if (position_ >= bytes_.size()) return false;
        value = bytes_[position_++];
        return true;
    }

    [[nodiscard]] bool u16_value(u16& value) noexcept {
        if (bytes_.size() - position_ < 2U) return false;
        value = read_u16_le(bytes_, position_);
        position_ += 2U;
        return true;
    }

private:
    std::span<const u8> bytes_;
    usize position_ {0};
};

[[nodiscard]] inline bool skip_animation(ByteCursor& cursor,
                                         usize entry_size) noexcept {
    u16 count = 0;
    return cursor.u16_value(count) &&
           cursor.skip(static_cast<usize>(count) * entry_size);
}

[[nodiscard]] inline bool skip_bone_action(ByteCursor& cursor) noexcept {
    u8 type = 0;
    if (!cursor.byte(type)) return false;
    switch (type) {
    case 0: return cursor.skip(24U);
    case 1: return true;
    case 2:
        return skip_animation(cursor, 8U) && skip_animation(cursor, 8U) &&
               skip_animation(cursor, 8U) && skip_animation(cursor, 4U);
    case 3:
        return cursor.skip(6U) && skip_animation(cursor, 8U) &&
               cursor.skip(2U);
    case 4:
        return skip_animation(cursor, 8U) && skip_animation(cursor, 4U);
    case 5: return skip_animation(cursor, 8U);
    case 6:
        return skip_animation(cursor, 8U) && skip_animation(cursor, 8U) &&
               skip_animation(cursor, 4U);
    default: return false;
    }
}

[[nodiscard]] inline Result<std::vector<i32>> parse_mtra_frames(
    std::span<const u8> bytes) {
    if (bytes.size() < 28U || bytes[0] != 'M' || bytes[1] != 'T' ||
        bytes[3] != 0U || bytes[2] < 2U || bytes[2] > 5U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "invalid or unsupported MTRA data");
    }
    const u8 version = bytes[2];
    ByteCursor cursor(bytes);
    u16 actions = 0;
    u16 bones = 0;
    if (!cursor.skip(4U) || !cursor.u16_value(actions) ||
        !cursor.u16_value(bones) || !cursor.skip(20U)) {
        return fail_java("java/lang/IllegalArgumentException",
                         "truncated MTRA header");
    }
    std::vector<i32> frames;
    frames.reserve(actions);
    for (u16 action = 0; action < actions; ++action) {
        u16 keyframes = 0;
        if (!cursor.u16_value(keyframes)) {
            return fail_java("java/lang/IllegalArgumentException",
                             "truncated MTRA action table");
        }
        frames.push_back(static_cast<i32>(keyframes) << 16);
        for (u16 bone = 0; bone < bones; ++bone) {
            if (!skip_bone_action(cursor)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid MTRA bone animation");
            }
        }
        if (version == 5U) {
            u16 count = 0;
            if (!cursor.u16_value(count) ||
                !cursor.skip(static_cast<usize>(count) * 6U)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid MTRA dynamic polygon table");
            }
        }
    }
    return frames;
}

struct MbacMetadata final {
    i32 patterns {1};
    i32 textures {1};
};

[[nodiscard]] inline Result<MbacMetadata> parse_mbac_metadata(
    std::span<const u8> bytes) {
    if (bytes.size() < 12U || bytes[0] != 'M' || bytes[1] != 'B' ||
        bytes[3] != 0U || bytes[2] < 2U || bytes[2] > 5U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "invalid or unsupported MBAC data");
    }
    const u8 version = bytes[2];
    usize position = 4U;
    u8 polygon_format = 1U;
    if (version > 3U) {
        if (bytes.size() < 8U) {
            return fail_java("java/lang/IllegalArgumentException",
                             "truncated MBAC format header");
        }
        const u8 vertex_format = bytes[4];
        const u8 normal_format = bytes[5];
        polygon_format = bytes[6];
        const u8 bone_format = bytes[7];
        if ((vertex_format != 1U && vertex_format != 2U) ||
            normal_format > 2U || polygon_format < 1U ||
            polygon_format > 3U || bone_format != 1U) {
            return fail_java("java/lang/IllegalArgumentException",
                             "unsupported MBAC component format");
        }
        position = 8U;
    }
    if (bytes.size() - position < 8U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "truncated MBAC geometry header");
    }
    if (read_u16_le(bytes, position) > 21845U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "MBAC vertex count exceeds format limit");
    }
    position += 8U;
    MbacMetadata result;
    if (polygon_format >= 3U) {
        if (bytes.size() - position < 10U) {
            return fail_java("java/lang/IllegalArgumentException",
                             "truncated MBAC polygon header");
        }
        result.textures = static_cast<i32>(read_u16_le(bytes, position + 4U));
        result.patterns = static_cast<i32>(read_u16_le(bytes, position + 6U));
        const i32 colors = static_cast<i32>(
            read_u16_le(bytes, position + 8U));
        if (result.textures > 16 || result.patterns < 1 ||
            result.patterns > 33 || colors > 256) {
            return fail_java("java/lang/IllegalArgumentException",
                             "MBAC metadata exceeds format limits");
        }
    }
    return result;
}

[[nodiscard]] inline std::pair<i32, i32> parse_bmp_size(
    std::span<const u8> bytes) noexcept {
    if (bytes.size() < 26U || bytes[0] != 'B' || bytes[1] != 'M') {
        return {0, 0};
    }
    const u32 header_size = read_u32_le(bytes, 14U);
    if (header_size == 12U) {
        return {static_cast<i32>(read_u16_le(bytes, 18U)),
                static_cast<i32>(read_u16_le(bytes, 20U))};
    }
    if (header_size >= 40U) {
        const i32 width = std::bit_cast<i32>(read_u32_le(bytes, 18U));
        i32 height = std::bit_cast<i32>(read_u32_le(bytes, 22U));
        if (height < 0 && height != std::numeric_limits<i32>::min()) {
            height = -height;
        }
        return {width, height};
    }
    return {0, 0};
}

[[nodiscard]] inline Result<ObjectRef> resource_byte_array(
    Machine& machine, ObjectRef string, std::string_view operation) {
    auto name = graphics_native::utf8_text(machine, string, operation);
    if (!name) return std::unexpected(name.error());
    while (!name->empty() && name->front() == '/') name->erase(name->begin());
    if (name->empty()) {
        return fail_java("java/io/IOException", "resource name is empty");
    }
    auto bytes = machine.classes().read_resource(*name);
    if (!bytes) return fail_java("java/io/IOException", bytes.error().message);
    auto array = m3g::allocate_array(machine, "[B", bytes->size(),
                                     Value::from_int(0));
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < bytes->size(); ++index) {
        const i32 signed_byte = static_cast<i32>(
            static_cast<i8>((*bytes)[index]));
        auto stored = machine.heap().set_element(
            *array, index, Value::from_int(signed_byte));
        if (!stored) return std::unexpected(stored.error());
    }
    return *array;
}

[[nodiscard]] inline Status require_not_disposed(
    Machine& machine, ObjectRef object, std::string_view owner,
    std::string_view operation) {
    auto disposed = int_field(machine, object, owner, "disposed", "Z");
    if (!disposed) return std::unexpected(disposed.error());
    if (*disposed != 0) {
        return fail_java("java/lang/IllegalStateException",
                         std::string(operation) + " is disposed");
    }
    return {};
}

} // namespace phoneme::vm::micro3d
