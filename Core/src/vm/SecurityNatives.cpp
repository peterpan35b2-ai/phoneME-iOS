#include "SecurityNatives.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/security/PermissionSemantics.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

#include "Sha256Support.hpp"

namespace phoneme::vm {
namespace {

using security::PermissionEntry;

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) {
        std::terminate();
    }
}

[[nodiscard]] Result<ObjectRef> require_receiver(
    std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "security method has no receiver");
    }
    auto receiver = arguments.front().as_reference();
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    if (receiver->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "security receiver is null");
    }
    return *receiver;
}

[[nodiscard]] Result<std::string> utf8_string(Machine& machine,
                                              ObjectRef reference,
                                              bool allow_null) {
    if (reference.is_null()) {
        if (allow_null) {
            return std::string {};
        }
        return fail_java("java/lang/NullPointerException",
                         "permission name is null");
    }
    auto text = machine.heap().string_value(reference);
    if (!text) {
        return std::unexpected(text.error());
    }

    std::string result;
    result.reserve(text->size());
    for (usize index = 0; index < text->size(); ++index) {
        u32 code_point = static_cast<u16>((*text)[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (index + 1U < text->size()) {
                const u32 low = static_cast<u16>((*text)[index + 1U]);
                if (low >= 0xDC00U && low <= 0xDFFFU) {
                    code_point = 0x10000U +
                        ((code_point - 0xD800U) << 10U) +
                        (low - 0xDC00U);
                    ++index;
                } else {
                    code_point = 0xFFFDU;
                }
            } else {
                code_point = 0xFFFDU;
            }
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            code_point = 0xFFFDU;
        }
        if (code_point <= 0x7FU) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            result.push_back(static_cast<char>(0x80U |
                                               ((code_point >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            result.push_back(static_cast<char>(0x80U |
                                               ((code_point >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U |
                                               ((code_point >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
    }
    return result;
}

[[nodiscard]] Result<std::string> string_argument(
    Machine& machine,
    std::span<const Value> arguments,
    usize index,
    bool allow_null) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "security method is missing a string argument");
    }
    auto reference = arguments[index].as_reference();
    if (!reference) {
        return std::unexpected(reference.error());
    }
    return utf8_string(machine, *reference, allow_null);
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "security method is missing a reference argument");
    }
    auto reference = arguments[index].as_reference();
    if (!reference) {
        return std::unexpected(reference.error());
    }
    if (!allow_null && reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "security argument is null");
    }
    return *reference;
}

[[nodiscard]] i32 java_decision(
    security::PermissionDecision decision) noexcept {
    return static_cast<i32>(to_underlying(decision));
}

[[nodiscard]] Result<Value> instance_field(Machine& machine,
                                           ObjectRef object,
                                           std::string_view owner,
                                           std::string_view name,
                                           std::string_view descriptor) {
    auto location = machine.class_states().resolve_field(
        owner, name, descriptor, false);
    if (!location) {
        return std::unexpected(location.error());
    }
    return machine.heap().field(object, location->index);
}

[[nodiscard]] Status set_instance_field(Machine& machine,
                                        ObjectRef object,
                                        std::string_view owner,
                                        std::string_view name,
                                        std::string_view descriptor,
                                        Value value) {
    auto location = machine.class_states().resolve_field(
        owner, name, descriptor, false);
    if (!location) {
        return std::unexpected(location.error());
    }
    return machine.heap().set_field(object, location->index, value);
}

[[nodiscard]] Result<ObjectRef> string_field(Machine& machine,
                                             ObjectRef object,
                                             std::string_view owner,
                                             std::string_view name) {
    auto value = instance_field(machine, object, owner, name,
                                "Ljava/lang/String;");
    if (!value) {
        return std::unexpected(value.error());
    }
    return value->as_reference();
}

[[nodiscard]] Result<std::string> permission_name(Machine& machine,
                                                  ObjectRef permission) {
    auto name_reference = string_field(machine, permission,
                                       "java/security/Permission", "name");
    if (!name_reference) {
        return std::unexpected(name_reference.error());
    }
    return utf8_string(machine, *name_reference, false);
}

[[nodiscard]] Result<i32> property_mask(Machine& machine,
                                        ObjectRef permission) {
    auto value = instance_field(machine, permission,
                                "java/util/PropertyPermission", "mask", "I");
    if (!value) {
        return std::unexpected(value.error());
    }
    return value->as_int();
}

[[nodiscard]] Status init_basic_permission(Machine& machine,
                                           ObjectRef receiver,
                                           ObjectRef name_reference) {
    if (name_reference.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "name can't be null");
    }
    auto payload = machine.heap().string_value(name_reference);
    if (!payload) {
        return std::unexpected(payload.error());
    }
    if (payload->empty()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "name can't be empty");
    }
    return set_instance_field(machine, receiver, "java/security/Permission",
                              "name", "Ljava/lang/String;",
                              Value::from_reference(name_reference));
}

[[nodiscard]] Result<ObjectRef> make_string(Machine& machine,
                                            std::string_view utf8) {
    std::u16string text;
    text.reserve(utf8.size());
    for (usize index {0}; index < utf8.size();) {
        const u32 lead = static_cast<u8>(utf8[index]);
        u32 code_point {0xFFFDU};
        usize length {1U};
        if (lead < 0x80U) {
            code_point = lead;
            length = 1U;
        } else if ((lead & 0xE0U) == 0xC0U) {
            code_point = lead & 0x1FU;
            length = 2U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            code_point = lead & 0x0FU;
            length = 3U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            code_point = lead & 0x07U;
            length = 4U;
        }
        bool valid {true};
        if (index + length > utf8.size()) {
            valid = false;
        } else {
            for (usize offset {1U}; offset < length; ++offset) {
                const u32 trail = static_cast<u8>(utf8[index + offset]);
                if ((trail & 0xC0U) != 0x80U) {
                    valid = false;
                    length = offset;
                    break;
                }
                code_point = (code_point << 6U) | (trail & 0x3FU);
            }
            if (code_point > 0x10FFFFU) {
                valid = false;
            }
        }
        if (!valid) {
            code_point = 0xFFFDU;
            length = 1U;
        }
        if (code_point <= 0xFFFFU) {
            text.push_back(static_cast<char16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            text.push_back(static_cast<char16_t>(0xD800U + (code_point >> 10U)));
            text.push_back(static_cast<char16_t>(0xDC00U + (code_point & 0x3FFU)));
        }
        index += length;
    }
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!object) {
        return std::unexpected(object.error());
    }
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) {
        return std::unexpected(attached.error());
    }
    return *object;
}

[[nodiscard]] constexpr u8 aes_gf_multiply(u8 left, u8 right) noexcept {
    u8 result = 0U;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        if ((right & 1U) != 0U) result = static_cast<u8>(result ^ left);
        const bool high = (left & 0x80U) != 0U;
        left = static_cast<u8>(left << 1U);
        if (high) left = static_cast<u8>(left ^ 0x1BU);
        right = static_cast<u8>(right >> 1U);
    }
    return result;
}

[[nodiscard]] constexpr u8 aes_rotate_left(u8 value,
                                           unsigned amount) noexcept {
    return static_cast<u8>((static_cast<unsigned>(value) << amount) |
                           (static_cast<unsigned>(value) >> (8U - amount)));
}

[[nodiscard]] constexpr u8 aes_inverse(u8 value) noexcept {
    if (value == 0U) return 0U;
    u8 result = 1U;
    u8 base = value;
    unsigned exponent = 254U;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = aes_gf_multiply(result, base);
        }
        base = aes_gf_multiply(base, base);
        exponent >>= 1U;
    }
    return result;
}

[[nodiscard]] constexpr u8 aes_sbox_value(u8 value) noexcept {
    const u8 inverse = aes_inverse(value);
    return static_cast<u8>(inverse ^ aes_rotate_left(inverse, 1U) ^
                           aes_rotate_left(inverse, 2U) ^
                           aes_rotate_left(inverse, 3U) ^
                           aes_rotate_left(inverse, 4U) ^ 0x63U);
}

[[nodiscard]] constexpr std::array<u8, 256U> make_aes_sbox() noexcept {
    std::array<u8, 256U> table {};
    for (usize index = 0U; index < table.size(); ++index) {
        table[index] = aes_sbox_value(static_cast<u8>(index));
    }
    return table;
}

[[nodiscard]] constexpr std::array<u8, 256U> make_aes_inverse_sbox() noexcept {
    std::array<u8, 256U> table {};
    const auto sbox = make_aes_sbox();
    for (usize index = 0U; index < sbox.size(); ++index) {
        table[sbox[index]] = static_cast<u8>(index);
    }
    return table;
}

[[nodiscard]] constexpr std::array<u8, 256U> make_aes_multiply_table(
    u8 multiplier) noexcept {
    std::array<u8, 256U> table {};
    for (usize index = 0U; index < table.size(); ++index) {
        table[index] = aes_gf_multiply(static_cast<u8>(index), multiplier);
    }
    return table;
}

constexpr auto kAesSbox = make_aes_sbox();
constexpr auto kAesInverseSbox = make_aes_inverse_sbox();
constexpr auto kAesMul2 = make_aes_multiply_table(2U);
constexpr auto kAesMul3 = make_aes_multiply_table(3U);
constexpr auto kAesMul9 = make_aes_multiply_table(9U);
constexpr auto kAesMul11 = make_aes_multiply_table(11U);
constexpr auto kAesMul13 = make_aes_multiply_table(13U);
constexpr auto kAesMul14 = make_aes_multiply_table(14U);

[[nodiscard]] std::array<u8, 176U> aes128_expand_key(
    std::span<const u8, 16U> key) {
    std::array<u8, 176U> round_keys {};
    std::copy(key.begin(), key.end(), round_keys.begin());
    usize generated = 16U;
    u8 rcon = 1U;
    std::array<u8, 4U> temp {};
    while (generated < round_keys.size()) {
        std::copy_n(round_keys.begin() +
                        static_cast<std::ptrdiff_t>(generated - 4U),
                    4U, temp.begin());
        if ((generated % 16U) == 0U) {
            const u8 first = temp[0];
            temp[0] = kAesSbox[temp[1]];
            temp[1] = kAesSbox[temp[2]];
            temp[2] = kAesSbox[temp[3]];
            temp[3] = kAesSbox[first];
            temp[0] = static_cast<u8>(temp[0] ^ rcon);
            rcon = aes_gf_multiply(rcon, 2U);
        }
        for (usize index = 0U; index < 4U; ++index) {
            round_keys[generated] = static_cast<u8>(
                round_keys[generated - 16U] ^ temp[index]);
            ++generated;
        }
    }
    return round_keys;
}

void aes_add_round_key(std::array<u8, 16U>& state,
                       const std::array<u8, 176U>& round_keys,
                       usize round) noexcept {
    const usize offset = round * 16U;
    for (usize index = 0U; index < state.size(); ++index) {
        state[index] = static_cast<u8>(state[index] ^
                                       round_keys[offset + index]);
    }
}

void aes_shift_rows(std::array<u8, 16U>& state, bool inverse) noexcept {
    const auto original = state;
    for (usize row = 1U; row < 4U; ++row) {
        for (usize column = 0U; column < 4U; ++column) {
            const usize source_column = inverse
                ? (column + 4U - row) % 4U
                : (column + row) % 4U;
            state[row + 4U * column] =
                original[row + 4U * source_column];
        }
    }
}

void aes_mix_columns(std::array<u8, 16U>& state, bool inverse) noexcept {
    for (usize column = 0U; column < 4U; ++column) {
        const usize offset = column * 4U;
        const u8 a = state[offset];
        const u8 b = state[offset + 1U];
        const u8 c = state[offset + 2U];
        const u8 d = state[offset + 3U];
        if (inverse) {
            state[offset] = static_cast<u8>(
                kAesMul14[a] ^ kAesMul11[b] ^ kAesMul13[c] ^ kAesMul9[d]);
            state[offset + 1U] = static_cast<u8>(
                kAesMul9[a] ^ kAesMul14[b] ^ kAesMul11[c] ^ kAesMul13[d]);
            state[offset + 2U] = static_cast<u8>(
                kAesMul13[a] ^ kAesMul9[b] ^ kAesMul14[c] ^ kAesMul11[d]);
            state[offset + 3U] = static_cast<u8>(
                kAesMul11[a] ^ kAesMul13[b] ^ kAesMul9[c] ^ kAesMul14[d]);
        } else {
            state[offset] = static_cast<u8>(
                kAesMul2[a] ^ kAesMul3[b] ^ c ^ d);
            state[offset + 1U] = static_cast<u8>(
                a ^ kAesMul2[b] ^ kAesMul3[c] ^ d);
            state[offset + 2U] = static_cast<u8>(
                a ^ b ^ kAesMul2[c] ^ kAesMul3[d]);
            state[offset + 3U] = static_cast<u8>(
                kAesMul3[a] ^ b ^ c ^ kAesMul2[d]);
        }
    }
}

void aes128_encrypt_block(std::span<const u8, 16U> input,
                          std::span<u8, 16U> output,
                          const std::array<u8, 176U>& round_keys) noexcept {
    std::array<u8, 16U> state {};
    std::copy(input.begin(), input.end(), state.begin());
    aes_add_round_key(state, round_keys, 0U);
    for (usize round = 1U; round < 10U; ++round) {
        for (u8& value : state) value = kAesSbox[value];
        aes_shift_rows(state, false);
        aes_mix_columns(state, false);
        aes_add_round_key(state, round_keys, round);
    }
    for (u8& value : state) value = kAesSbox[value];
    aes_shift_rows(state, false);
    aes_add_round_key(state, round_keys, 10U);
    std::copy(state.begin(), state.end(), output.begin());
}

void aes128_decrypt_block(std::span<const u8, 16U> input,
                          std::span<u8, 16U> output,
                          const std::array<u8, 176U>& round_keys) noexcept {
    std::array<u8, 16U> state {};
    std::copy(input.begin(), input.end(), state.begin());
    aes_add_round_key(state, round_keys, 10U);
    for (usize round = 9U; round > 0U; --round) {
        aes_shift_rows(state, true);
        for (u8& value : state) value = kAesInverseSbox[value];
        aes_add_round_key(state, round_keys, round);
        aes_mix_columns(state, true);
    }
    aes_shift_rows(state, true);
    for (u8& value : state) value = kAesInverseSbox[value];
    aes_add_round_key(state, round_keys, 0U);
    std::copy(state.begin(), state.end(), output.begin());
}

[[nodiscard]] Result<ObjectRef> copy_byte_array(Machine& machine,
                                                ObjectRef source) {
    auto length = machine.heap().array_length(source);
    if (!length) return std::unexpected(length.error());
    auto bytes = machine.heap().read_byte_array(source, 0U, *length);
    if (!bytes) return std::unexpected(bytes.error());
    auto copy = machine.heap().allocate_array(
        "[B", bytes->size(), Value::from_int(0));
    if (!copy) return std::unexpected(copy.error());
    auto written = machine.heap().write_byte_array(*copy, 0U, *bytes);
    if (!written) return std::unexpected(written.error());
    return *copy;
}

[[nodiscard]] Result<ObjectRef> allocate_collection_instance(
    Machine& machine,
    std::string_view class_name) {
    auto object = machine.class_states().allocate_instance(
        machine.heap(), class_name);
    if (object || object.error().code != ErrorCode::overflow) {
        return object;
    }
    auto collected = machine.collect_garbage();
    if (!collected) {
        return std::unexpected(collected.error());
    }
    return machine.class_states().allocate_instance(machine.heap(), class_name);
}

constexpr usize kEnumerationArrayField {0U};
constexpr usize kEnumerationIndexField {1U};
constexpr usize kEnumerationSizeField {2U};

[[nodiscard]] Result<ObjectRef> make_enumeration(
    Machine& machine,
    const std::vector<ObjectRef>& values) {
    auto array = machine.heap().allocate_array(
        "[Ljava/lang/Object;", values.size(), Value::from_reference({}));
    if (!array) {
        return std::unexpected(array.error());
    }
    for (usize index {0U}; index < values.size(); ++index) {
        auto stored = machine.heap().set_element(
            *array, index, Value::from_reference(values[index]));
        if (!stored) {
            return std::unexpected(stored.error());
        }
    }
    auto enumeration = machine.class_states().allocate_instance(
        machine.heap(), "java/util/ArrayEnumeration");
    if (!enumeration) {
        return std::unexpected(enumeration.error());
    }
    auto array_stored = machine.heap().set_field(
        *enumeration, kEnumerationArrayField, Value::from_reference(*array));
    auto index_stored = machine.heap().set_field(
        *enumeration, kEnumerationIndexField, Value::from_int(0));
    auto size_stored = machine.heap().set_field(
        *enumeration, kEnumerationSizeField,
        Value::from_int(static_cast<i32>(values.size())));
    if (!array_stored) {
        return std::unexpected(array_stored.error());
    }
    if (!index_stored) {
        return std::unexpected(index_stored.error());
    }
    if (!size_stored) {
        return std::unexpected(size_stored.error());
    }
    return *enumeration;
}

[[nodiscard]] Result<i32> collection_count(Machine& machine,
                                           ObjectRef collection,
                                           std::string_view owner) {
    auto value = instance_field(machine, collection, owner, "count", "I");
    if (!value) {
        return std::unexpected(value.error());
    }
    return value->as_int();
}

[[nodiscard]] Status append_permission(Machine& machine,
                                       ObjectRef collection,
                                       ObjectRef permission,
                                       std::string_view owner,
                                       std::string_view entries_descriptor) {
    auto read_only = instance_field(machine, collection,
                                    "java/security/PermissionCollection",
                                    "readOnly", "Z");
    if (!read_only) {
        return std::unexpected(read_only.error());
    }
    auto read_only_value = read_only->as_int();
    if (!read_only_value) {
        return std::unexpected(read_only_value.error());
    }
    if (*read_only_value != 0) {
        return fail_java("java/lang/SecurityException",
                         "attempt to add a Permission to a readonly "
                         "PermissionCollection");
    }
    auto count = collection_count(machine, collection, owner);
    if (!count) {
        return std::unexpected(count.error());
    }
    auto entries_value = instance_field(machine, collection, owner,
                                        "entries", entries_descriptor);
    if (!entries_value) {
        return std::unexpected(entries_value.error());
    }
    auto entries = entries_value->as_reference();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const usize new_length = static_cast<usize>(*count) + 1U;
    auto grown = machine.heap().allocate_array(
        std::string(entries_descriptor), new_length,
        Value::from_reference({}));
    if (!grown) {
        return std::unexpected(grown.error());
    }
    for (usize index {0U}; index < static_cast<usize>(*count); ++index) {
        auto element = machine.heap().element(*entries, index);
        if (!element) {
            return std::unexpected(element.error());
        }
        auto stored = machine.heap().set_element(*grown, index, *element);
        if (!stored) {
            return std::unexpected(stored.error());
        }
    }
    auto appended = machine.heap().set_element(
        *grown, static_cast<usize>(*count), Value::from_reference(permission));
    if (!appended) {
        return std::unexpected(appended.error());
    }
    auto entries_stored = set_instance_field(
        machine, collection, owner, "entries", entries_descriptor,
        Value::from_reference(*grown));
    if (!entries_stored) {
        return std::unexpected(entries_stored.error());
    }
    return set_instance_field(machine, collection, owner, "count", "I",
                              Value::from_int(*count + 1));
}

[[nodiscard]] Result<std::vector<ObjectRef>> live_permissions(
    Machine& machine,
    ObjectRef collection,
    std::string_view owner,
    std::string_view entries_descriptor) {
    auto count = collection_count(machine, collection, owner);
    if (!count) {
        return std::unexpected(count.error());
    }
    auto entries_value = instance_field(machine, collection, owner,
                                        "entries", entries_descriptor);
    if (!entries_value) {
        return std::unexpected(entries_value.error());
    }
    auto entries = entries_value->as_reference();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    std::vector<ObjectRef> permissions;
    permissions.reserve(static_cast<usize>(*count));
    for (usize index {0U}; index < static_cast<usize>(*count); ++index) {
        auto element = machine.heap().element(*entries, index);
        if (!element) {
            return std::unexpected(element.error());
        }
        auto reference = element->as_reference();
        if (!reference) {
            return std::unexpected(reference.error());
        }
        permissions.push_back(*reference);
    }
    return permissions;
}

[[nodiscard]] Result<std::string> class_of(Machine& machine,
                                           ObjectRef object) {
    return machine.heap().class_name(object);
}

[[nodiscard]] Result<bool> same_class(Machine& machine,
                                      ObjectRef left,
                                      ObjectRef right) {
    auto left_class = class_of(machine, left);
    if (!left_class) {
        return std::unexpected(left_class.error());
    }
    auto right_class = class_of(machine, right);
    if (!right_class) {
        return std::unexpected(right_class.error());
    }
    return *left_class == *right_class;
}

[[nodiscard]] Result<bool> names_equal(Machine& machine,
                                       ObjectRef left,
                                       ObjectRef right) {
    auto left_name = string_field(machine, left,
                                  "java/security/Permission", "name");
    if (!left_name) {
        return std::unexpected(left_name.error());
    }
    auto right_name = string_field(machine, right,
                                   "java/security/Permission", "name");
    if (!right_name) {
        return std::unexpected(right_name.error());
    }
    if (left_name->is_null() || right_name->is_null()) {
        return *left_name == *right_name;
    }
    auto left_text = machine.heap().string_value(*left_name);
    if (!left_text) {
        return std::unexpected(left_text.error());
    }
    auto right_text = machine.heap().string_value(*right_name);
    if (!right_text) {
        return std::unexpected(right_text.error());
    }
    return *left_text == *right_text;
}

[[nodiscard]] Result<i32> name_hashcode(Machine& machine,
                                        ObjectRef permission) {
    auto name = string_field(machine, permission,
                             "java/security/Permission", "name");
    if (!name) {
        return std::unexpected(name.error());
    }
    if (name->is_null()) {
        return 0;
    }
    auto text = machine.heap().string_value(*name);
    if (!text) {
        return std::unexpected(text.error());
    }
    return security::java_string_hashcode(*text);
}

[[nodiscard]] std::optional<Value> boolean(bool value) {
    return Value::from_int(value ? 1 : 0);
}

[[nodiscard]] Result<std::optional<Value>> permission_name_init(
    Machine& machine,
    std::span<const Value> arguments) {
    auto receiver = require_receiver(arguments);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    if (arguments.size() < 2U) {
        return fail(ErrorCode::invalid_argument,
                    "Permission constructor is missing a name argument");
    }
    auto name = arguments[1].as_reference();
    if (!name) {
        return std::unexpected(name.error());
    }
    auto stored = set_instance_field(machine, *receiver,
                                     "java/security/Permission", "name",
                                     "Ljava/lang/String;",
                                     Value::from_reference(*name));
    if (!stored) {
        return std::unexpected(stored.error());
    }
    return std::optional<Value> {};
}

[[nodiscard]] Result<std::optional<Value>> basic_name_init(
    Machine& machine,
    std::span<const Value> arguments) {
    auto receiver = require_receiver(arguments);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    if (arguments.size() < 2U) {
        return fail(ErrorCode::invalid_argument,
                    "BasicPermission constructor is missing a name argument");
    }
    auto name = arguments[1].as_reference();
    if (!name) {
        return std::unexpected(name.error());
    }
    auto initialized = init_basic_permission(machine, *receiver, *name);
    if (!initialized) {
        return std::unexpected(initialized.error());
    }
    return std::optional<Value> {};
}

constexpr std::string_view kMessageDigestClass =
    "java/security/MessageDigest";

[[nodiscard]] Result<ObjectRef> byte_array_argument(
    Machine& machine,
    std::span<const Value> arguments,
    usize index,
    bool allow_null = false) {
    auto reference = reference_argument(arguments, index, allow_null);
    if (!reference) return std::unexpected(reference.error());
    if (reference->is_null()) return *reference;
    auto class_name = machine.heap().class_name(*reference);
    if (!class_name) return std::unexpected(class_name.error());
    if (*class_name != "[B") {
        return fail_java("java/lang/IllegalArgumentException",
                         "MessageDigest argument is not byte[]");
    }
    return *reference;
}

[[nodiscard]] Result<std::vector<u8>> read_byte_range(
    Machine& machine,
    ObjectRef array,
    i32 offset,
    i32 length) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "MessageDigest byte array is null");
    }
    auto array_length = machine.heap().array_length(array);
    if (!array_length) return std::unexpected(array_length.error());
    if (offset < 0 || length < 0 ||
        static_cast<usize>(offset) > *array_length ||
        static_cast<usize>(length) >
            *array_length - static_cast<usize>(offset)) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "MessageDigest byte range is invalid");
    }
    return machine.heap().read_byte_array(
        array, static_cast<usize>(offset), static_cast<usize>(length));
}

[[nodiscard]] Result<i32> digest_count(Machine& machine,
                                       ObjectRef digest) {
    auto value = instance_field(machine, digest, kMessageDigestClass,
                                "count", "I");
    if (!value) return std::unexpected(value.error());
    auto count = value->as_int();
    if (!count) return std::unexpected(count.error());
    if (*count < 0) {
        return fail(ErrorCode::invalid_state,
                    "MessageDigest count is negative");
    }
    return *count;
}

[[nodiscard]] Result<ObjectRef> ensure_digest_capacity(
    Machine& machine,
    ObjectRef digest,
    usize count,
    usize required) {
    auto value = instance_field(machine, digest, kMessageDigestClass,
                                "buffer", "[B");
    if (!value) return std::unexpected(value.error());
    auto buffer = value->as_reference();
    if (!buffer) return std::unexpected(buffer.error());

    usize capacity = 0U;
    if (!buffer->is_null()) {
        auto length = machine.heap().array_length(*buffer);
        if (!length) return std::unexpected(length.error());
        capacity = *length;
    }
    if (capacity >= required) return *buffer;

    usize grown_capacity = std::max<usize>(64U, capacity);
    while (grown_capacity < required) {
        if (grown_capacity > std::numeric_limits<usize>::max() / 2U) {
            grown_capacity = required;
            break;
        }
        grown_capacity *= 2U;
    }
    auto grown = machine.heap().allocate_array(
        "[B", grown_capacity, Value::from_int(0));
    if (!grown) return std::unexpected(grown.error());
    if (count != 0U && !buffer->is_null()) {
        auto existing = machine.heap().read_byte_array(*buffer, 0U, count);
        if (!existing) return std::unexpected(existing.error());
        auto copied = machine.heap().write_byte_array(*grown, 0U, *existing);
        if (!copied) return std::unexpected(copied.error());
    }
    auto stored = set_instance_field(
        machine, digest, kMessageDigestClass, "buffer", "[B",
        Value::from_reference(*grown));
    if (!stored) return std::unexpected(stored.error());
    return *grown;
}

[[nodiscard]] Status append_digest_bytes(Machine& machine,
                                         ObjectRef digest,
                                         std::span<const u8> bytes) {
    if (bytes.empty()) return {};
    auto count_value = digest_count(machine, digest);
    if (!count_value) return std::unexpected(count_value.error());
    const usize count = static_cast<usize>(*count_value);
    if (bytes.size() > static_cast<usize>(
                           std::numeric_limits<i32>::max()) - count) {
        return fail_java("java/lang/OutOfMemoryError",
                         "MessageDigest input is too large");
    }
    const usize required = count + bytes.size();
    auto buffer = ensure_digest_capacity(machine, digest, count, required);
    if (!buffer) return std::unexpected(buffer.error());
    auto written = machine.heap().write_byte_array(*buffer, count, bytes);
    if (!written) return std::unexpected(written.error());
    return set_instance_field(
        machine, digest, kMessageDigestClass, "count", "I",
        Value::from_int(static_cast<i32>(required)));
}

[[nodiscard]] Result<ObjectRef> make_byte_array(
    Machine& machine,
    std::span<const u8> bytes) {
    auto array = machine.heap().allocate_array(
        "[B", bytes.size(), Value::from_int(0));
    if (!array) return std::unexpected(array.error());
    auto written = machine.heap().write_byte_array(*array, 0U, bytes);
    if (!written) return std::unexpected(written.error());
    return *array;
}

[[nodiscard]] Result<ObjectRef> finish_digest(Machine& machine,
                                              ObjectRef digest) {
    auto count_value = digest_count(machine, digest);
    if (!count_value) return std::unexpected(count_value.error());
    const usize count = static_cast<usize>(*count_value);
    std::vector<u8> bytes;
    if (count != 0U) {
        auto value = instance_field(machine, digest, kMessageDigestClass,
                                    "buffer", "[B");
        if (!value) return std::unexpected(value.error());
        auto buffer = value->as_reference();
        if (!buffer) return std::unexpected(buffer.error());
        if (buffer->is_null()) {
            return fail(ErrorCode::invalid_state,
                        "MessageDigest buffer is missing");
        }
        auto read = machine.heap().read_byte_array(*buffer, 0U, count);
        if (!read) return std::unexpected(read.error());
        bytes = std::move(*read);
    }
    const auto result = crypto::sha256(bytes);
    auto reset = set_instance_field(
        machine, digest, kMessageDigestClass, "count", "I",
        Value::from_int(0));
    if (!reset) return std::unexpected(reset.error());
    return make_byte_array(machine, result);
}

[[nodiscard]] Result<std::string> normalize_digest_algorithm(
    Machine& machine,
    std::span<const Value> arguments) {
    auto name = string_argument(machine, arguments, 0U, false);
    if (!name) return std::unexpected(name.error());
    std::string normalized;
    normalized.reserve(name->size());
    for (const char raw_character : *name) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (character == '-' || character == '_' ||
            std::isspace(character) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::toupper(character)));
    }
    return normalized;
}

void register_message_digest_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kMessageDigestClass), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kMessageDigestClass), "getInstance",
        "(Ljava/lang/String;)Ljava/security/MessageDigest;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.getInstance expects one argument");
            }
            auto algorithm = normalize_digest_algorithm(machine, arguments);
            if (!algorithm) return std::unexpected(algorithm.error());
            if (*algorithm != "SHA256") {
                return fail_java("java/security/NoSuchAlgorithmException",
                                 "unsupported message digest algorithm");
            }
            auto digest = machine.class_states().allocate_instance(
                machine.heap(), kMessageDigestClass);
            if (!digest) return std::unexpected(digest.error());
            return std::optional<Value>(Value::from_reference(*digest));
        });

    add(registry, std::string(kMessageDigestClass), "getAlgorithm",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto algorithm = make_string(machine, "SHA-256");
            if (!algorithm) return std::unexpected(algorithm.error());
            return std::optional<Value>(Value::from_reference(*algorithm));
        });

    add(registry, std::string(kMessageDigestClass), "getDigestLength", "()I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.getDigestLength has no receiver");
            }
            return std::optional<Value>(Value::from_int(32));
        });

    add(registry, std::string(kMessageDigestClass), "update", "(B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.update(byte) expects one argument");
            }
            auto value = arguments[1].as_int();
            if (!value) return std::unexpected(value.error());
            const std::array<u8, 1> byte {
                static_cast<u8>(static_cast<i8>(*value)),
            };
            auto appended = append_digest_bytes(machine, *receiver, byte);
            if (!appended) return std::unexpected(appended.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kMessageDigestClass), "update", "([B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto array = byte_array_argument(machine, arguments, 1U);
            if (!array) return std::unexpected(array.error());
            auto length = machine.heap().array_length(*array);
            if (!length) return std::unexpected(length.error());
            auto bytes = machine.heap().read_byte_array(*array, 0U, *length);
            if (!bytes) return std::unexpected(bytes.error());
            auto appended = append_digest_bytes(machine, *receiver, *bytes);
            if (!appended) return std::unexpected(appended.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kMessageDigestClass), "update", "([BII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto array = byte_array_argument(machine, arguments, 1U);
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!array || !offset || !length) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.update range arguments are invalid");
            }
            auto bytes = read_byte_range(
                machine, *array, *offset, *length);
            if (!bytes) return std::unexpected(bytes.error());
            auto appended = append_digest_bytes(machine, *receiver, *bytes);
            if (!appended) return std::unexpected(appended.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kMessageDigestClass), "digest", "()[B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto result = finish_digest(machine, *receiver);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry, std::string(kMessageDigestClass), "digest", "([B)[B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto array = byte_array_argument(machine, arguments, 1U);
            if (!array) return std::unexpected(array.error());
            auto length = machine.heap().array_length(*array);
            if (!length) return std::unexpected(length.error());
            auto bytes = machine.heap().read_byte_array(*array, 0U, *length);
            if (!bytes) return std::unexpected(bytes.error());
            auto appended = append_digest_bytes(machine, *receiver, *bytes);
            if (!appended) return std::unexpected(appended.error());
            auto result = finish_digest(machine, *receiver);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry, std::string(kMessageDigestClass), "reset", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto reset = set_instance_field(
                machine, *receiver, kMessageDigestClass, "count", "I",
                Value::from_int(0));
            if (!reset) return std::unexpected(reset.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kMessageDigestClass), "isEqual", "([B[B)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.isEqual expects two arguments");
            }
            auto left = byte_array_argument(machine, arguments, 0U, true);
            auto right = byte_array_argument(machine, arguments, 1U, true);
            if (!left || !right) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.isEqual arguments are invalid");
            }
            if (*left == *right) return boolean(true);
            if (left->is_null() || right->is_null()) return boolean(false);
            auto left_length = machine.heap().array_length(*left);
            auto right_length = machine.heap().array_length(*right);
            if (!left_length || !right_length) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.isEqual arrays are invalid");
            }
            auto left_bytes = machine.heap().read_byte_array(
                *left, 0U, *left_length);
            auto right_bytes = machine.heap().read_byte_array(
                *right, 0U, *right_length);
            if (!left_bytes || !right_bytes) {
                return fail(ErrorCode::invalid_argument,
                            "MessageDigest.isEqual cannot read arrays");
            }
            usize difference = *left_length ^ *right_length;
            const usize maximum = std::max(*left_length, *right_length);
            for (usize index = 0U; index < maximum; ++index) {
                const u8 left_byte = index < *left_length
                    ? (*left_bytes)[index]
                    : 0U;
                const u8 right_byte = index < *right_length
                    ? (*right_bytes)[index]
                    : 0U;
                difference |= static_cast<usize>(left_byte ^ right_byte);
            }
            return boolean(difference == 0U);
        });
}

void register_permission_natives(NativeMethodRegistry& registry) {
    add(registry, "java/security/Permission", "<init>",
        "(Ljava/lang/String;)V", permission_name_init);

    add(registry, "java/security/Permission", "getName",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto name = string_field(machine, *receiver,
                                     "java/security/Permission", "name");
            if (!name) {
                return std::unexpected(name.error());
            }
            return std::optional<Value>(Value::from_reference(*name));
        });

    add(registry, "java/security/Permission", "newPermissionCollection",
        "()Ljava/security/PermissionCollection;",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_reference({}));
        });

    add(registry, "java/security/Permission", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto class_name = class_of(machine, *receiver);
            if (!class_name) {
                return std::unexpected(class_name.error());
            }
            std::string dotted;
            dotted.reserve(class_name->size());
            for (char character : *class_name) {
                dotted.push_back(character == '/' ? '.' : character);
            }
            auto name = permission_name(machine, *receiver);
            if (!name) {
                return std::unexpected(name.error());
            }
            std::string actions;
            if (*class_name == "java/util/PropertyPermission") {
                auto mask = property_mask(machine, *receiver);
                if (!mask) {
                    return std::unexpected(mask.error());
                }
                actions = security::format_actions(*mask);
            }
            std::string text = "(" + dotted + " " + *name;
            if (!actions.empty()) {
                text += " " + actions;
            }
            text += ")";
            auto result = make_string(machine, text);
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry, "java/security/PermissionCollection", "setReadOnly",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto stored = set_instance_field(
                machine, *receiver, "java/security/PermissionCollection",
                "readOnly", "Z", Value::from_int(1));
            if (!stored) {
                return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "java/security/PermissionCollection", "isReadOnly",
        "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto value = instance_field(machine, *receiver,
                                        "java/security/PermissionCollection",
                                        "readOnly", "Z");
            if (!value) {
                return std::unexpected(value.error());
            }
            auto read_only = value->as_int();
            if (!read_only) {
                return std::unexpected(read_only.error());
            }
            return boolean(*read_only != 0);
        });

    add(registry, "java/security/BasicPermission", "<init>",
        "(Ljava/lang/String;)V", basic_name_init);
    add(registry, "java/security/BasicPermission", "<init>",
        "(Ljava/lang/String;Ljava/lang/String;)V", basic_name_init);

    add(registry, "java/security/BasicPermission", "implies",
        "(Ljava/security/Permission;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto permission = reference_argument(arguments, 1U, true);
            if (!permission) {
                return std::unexpected(permission.error());
            }
            if (permission->is_null()) {
                return boolean(false);
            }
            auto same = same_class(machine, *receiver, *permission);
            if (!same) {
                return std::unexpected(same.error());
            }
            if (!*same) {
                return boolean(false);
            }
            auto holder = permission_name(machine, *receiver);
            if (!holder) {
                return std::unexpected(holder.error());
            }
            auto requested = permission_name(machine, *permission);
            if (!requested) {
                return std::unexpected(requested.error());
            }
            return boolean(security::basic_implies_name(*holder, *requested));
        });

    add(registry, "java/security/BasicPermission", "equals",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto other = reference_argument(arguments, 1U, true);
            if (!other) {
                return std::unexpected(other.error());
            }
            if (other->is_null()) {
                return boolean(false);
            }
            if (*receiver == *other) {
                return boolean(true);
            }
            auto same = same_class(machine, *receiver, *other);
            if (!same) {
                return std::unexpected(same.error());
            }
            if (!*same) {
                return boolean(false);
            }
            auto equal = names_equal(machine, *receiver, *other);
            if (!equal) {
                return std::unexpected(equal.error());
            }
            return boolean(*equal);
        });

    add(registry, "java/security/BasicPermission", "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto hash = name_hashcode(machine, *receiver);
            if (!hash) {
                return std::unexpected(hash.error());
            }
            return std::optional<Value>(Value::from_int(*hash));
        });

    add(registry, "java/security/BasicPermission", "getActions",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto result = make_string(machine, std::string_view {});
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry, "java/security/BasicPermission",
        "newPermissionCollection",
        "()Ljava/security/PermissionCollection;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto collection = allocate_collection_instance(
                machine, "java/security/BasicPermissionCollection");
            if (!collection) {
                return std::unexpected(collection.error());
            }
            return std::optional<Value>(Value::from_reference(*collection));
        });

    add(registry, "java/security/BasicPermissionCollection", "add",
        "(Ljava/security/Permission;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto permission = reference_argument(arguments, 1U, false);
            if (!permission) {
                return std::unexpected(permission.error());
            }
            auto stored = append_permission(
                machine, *receiver, *permission,
                "java/security/BasicPermissionCollection",
                "[Ljava/security/Permission;");
            if (!stored) {
                return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "java/security/BasicPermissionCollection", "implies",
        "(Ljava/security/Permission;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto permission = reference_argument(arguments, 1U, true);
            if (!permission) {
                return std::unexpected(permission.error());
            }
            if (permission->is_null()) {
                return boolean(false);
            }
            auto permissions = live_permissions(
                machine, *receiver, "java/security/BasicPermissionCollection",
                "[Ljava/security/Permission;");
            if (!permissions) {
                return std::unexpected(permissions.error());
            }
            if (permissions->empty()) {
                return boolean(false);
            }
            auto same = same_class(machine, permissions->front(), *permission);
            if (!same) {
                return std::unexpected(same.error());
            }
            if (!*same) {
                return boolean(false);
            }
            auto requested = permission_name(machine, *permission);
            if (!requested) {
                return std::unexpected(requested.error());
            }
            std::vector<PermissionEntry> entries;
            entries.reserve(permissions->size());
            for (const auto& entry : *permissions) {
                auto name = permission_name(machine, entry);
                if (!name) {
                    return std::unexpected(name.error());
                }
                entries.push_back(PermissionEntry {.name = std::move(*name)});
            }
            return boolean(security::basic_collection_implies(
                entries, *requested));
        });

    add(registry, "java/security/BasicPermissionCollection", "elements",
        "()Ljava/util/Enumeration;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto permissions = live_permissions(
                machine, *receiver, "java/security/BasicPermissionCollection",
                "[Ljava/security/Permission;");
            if (!permissions) {
                return std::unexpected(permissions.error());
            }
            auto enumeration = make_enumeration(machine, *permissions);
            if (!enumeration) {
                return std::unexpected(enumeration.error());
            }
            return std::optional<Value>(Value::from_reference(*enumeration));
        });

    add(registry, "java/lang/RuntimePermission", "<init>",
        "(Ljava/lang/String;)V", basic_name_init);
    add(registry, "java/lang/RuntimePermission", "<init>",
        "(Ljava/lang/String;Ljava/lang/String;)V", basic_name_init);

    add(registry, "java/util/PropertyPermission", "<init>",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (arguments.size() < 3U) {
                return fail(ErrorCode::invalid_argument,
                            "PropertyPermission constructor is missing arguments");
            }
            auto name = arguments[1].as_reference();
            if (!name) {
                return std::unexpected(name.error());
            }
            auto initialized = init_basic_permission(machine, *receiver, *name);
            if (!initialized) {
                return std::unexpected(initialized.error());
            }
            auto actions = string_argument(machine, arguments, 2U, true);
            if (!actions) {
                return std::unexpected(actions.error());
            }
            const int mask = security::parse_actions(*actions);
            if (mask <= security::kActionNone) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid actions mask");
            }
            auto mask_stored = set_instance_field(
                machine, *receiver, "java/util/PropertyPermission", "mask", "I",
                Value::from_int(mask));
            if (!mask_stored) {
                return std::unexpected(mask_stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "java/util/PropertyPermission", "implies",
        "(Ljava/security/Permission;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto permission = reference_argument(arguments, 1U, true);
            if (!permission) {
                return std::unexpected(permission.error());
            }
            if (permission->is_null()) {
                return boolean(false);
            }
            auto other_class = class_of(machine, *permission);
            if (!other_class) {
                return std::unexpected(other_class.error());
            }
            if (*other_class != "java/util/PropertyPermission") {
                return boolean(false);
            }
            auto this_mask = property_mask(machine, *receiver);
            auto that_mask = property_mask(machine, *permission);
            auto this_name = permission_name(machine, *receiver);
            auto that_name = permission_name(machine, *permission);
            if (!this_mask) return std::unexpected(this_mask.error());
            if (!that_mask) return std::unexpected(that_mask.error());
            if (!this_name) return std::unexpected(this_name.error());
            if (!that_name) return std::unexpected(that_name.error());
            return boolean(security::property_implies(
                *this_name, *this_mask, *that_name, *that_mask));
        });

    add(registry, "java/util/PropertyPermission", "equals",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto other = reference_argument(arguments, 1U, true);
            if (!other) return std::unexpected(other.error());
            if (other->is_null()) return boolean(false);
            if (*receiver == *other) return boolean(true);
            auto other_class = class_of(machine, *other);
            if (!other_class) return std::unexpected(other_class.error());
            if (*other_class != "java/util/PropertyPermission") {
                return boolean(false);
            }
            auto this_mask = property_mask(machine, *receiver);
            auto that_mask = property_mask(machine, *other);
            if (!this_mask) return std::unexpected(this_mask.error());
            if (!that_mask) return std::unexpected(that_mask.error());
            if (*this_mask != *that_mask) return boolean(false);
            auto equal = names_equal(machine, *receiver, *other);
            if (!equal) return std::unexpected(equal.error());
            return boolean(*equal);
        });

    add(registry, "java/util/PropertyPermission", "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto hash = name_hashcode(machine, *receiver);
            if (!hash) return std::unexpected(hash.error());
            return std::optional<Value>(Value::from_int(*hash));
        });

    add(registry, "java/util/PropertyPermission", "getActions",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto mask = property_mask(machine, *receiver);
            if (!mask) return std::unexpected(mask.error());
            auto result = make_string(machine, security::format_actions(*mask));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry, "java/util/PropertyPermission", "getMask", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto mask = property_mask(machine, *receiver);
            if (!mask) return std::unexpected(mask.error());
            return std::optional<Value>(Value::from_int(*mask));
        });

    add(registry, "java/util/PropertyPermission", "newPermissionCollection",
        "()Ljava/security/PermissionCollection;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto collection = allocate_collection_instance(
                machine, "java/util/PropertyPermissionCollection");
            if (!collection) return std::unexpected(collection.error());
            return std::optional<Value>(Value::from_reference(*collection));
        });

    add(registry, "java/util/PropertyPermissionCollection", "add",
        "(Ljava/security/Permission;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto permission = reference_argument(arguments, 1U, false);
            if (!permission) return std::unexpected(permission.error());
            auto stored = append_permission(
                machine, *receiver, *permission,
                "java/util/PropertyPermissionCollection",
                "[Ljava/util/PropertyPermission;");
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry, "java/util/PropertyPermissionCollection", "implies",
        "(Ljava/security/Permission;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto permission = reference_argument(arguments, 1U, true);
            if (!permission) return std::unexpected(permission.error());
            if (permission->is_null()) return boolean(false);
            auto other_class = class_of(machine, *permission);
            if (!other_class) return std::unexpected(other_class.error());
            if (*other_class != "java/util/PropertyPermission") {
                return boolean(false);
            }
            auto requested_mask = property_mask(machine, *permission);
            auto requested_name = permission_name(machine, *permission);
            if (!requested_mask) return std::unexpected(requested_mask.error());
            if (!requested_name) return std::unexpected(requested_name.error());
            auto permissions = live_permissions(
                machine, *receiver, "java/util/PropertyPermissionCollection",
                "[Ljava/util/PropertyPermission;");
            if (!permissions) return std::unexpected(permissions.error());
            std::vector<PermissionEntry> entries;
            entries.reserve(permissions->size());
            for (const auto& entry : *permissions) {
                auto name = permission_name(machine, entry);
                auto mask = property_mask(machine, entry);
                if (!name) return std::unexpected(name.error());
                if (!mask) return std::unexpected(mask.error());
                entries.push_back(PermissionEntry {
                    .name = std::move(*name), .mask = *mask});
            }
            return boolean(security::property_collection_implies(
                entries, *requested_name, *requested_mask));
        });

    add(registry, "java/util/PropertyPermissionCollection", "elements",
        "()Ljava/util/Enumeration;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto permissions = live_permissions(
                machine, *receiver, "java/util/PropertyPermissionCollection",
                "[Ljava/util/PropertyPermission;");
            if (!permissions) return std::unexpected(permissions.error());
            auto enumeration = make_enumeration(machine, *permissions);
            if (!enumeration) return std::unexpected(enumeration.error());
            return std::optional<Value>(Value::from_reference(*enumeration));
        });

    add(registry, "java/security/AccessControlException", "<init>",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            if (arguments.size() < 2U) {
                return fail(ErrorCode::invalid_argument,
                            "AccessControlException constructor is missing a message");
            }
            auto message = arguments[1].as_reference();
            if (!message) return std::unexpected(message.error());
            auto stored = set_instance_field(
                machine, *receiver, "java/lang/Throwable", "detailMessage",
                "Ljava/lang/String;", Value::from_reference(*message));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry, "java/security/AccessControlException", "<init>",
        "(Ljava/lang/String;Ljava/security/Permission;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            if (arguments.size() < 3U) {
                return fail(ErrorCode::invalid_argument,
                            "AccessControlException constructor is missing arguments");
            }
            auto message = arguments[1].as_reference();
            auto permission = arguments[2].as_reference();
            if (!message || !permission) {
                return fail(ErrorCode::invalid_argument,
                            "AccessControlException constructor arguments are invalid");
            }
            auto message_stored = set_instance_field(
                machine, *receiver, "java/lang/Throwable", "detailMessage",
                "Ljava/lang/String;", Value::from_reference(*message));
            auto permission_stored = set_instance_field(
                machine, *receiver, "java/security/AccessControlException",
                "perm", "Ljava/security/Permission;",
                Value::from_reference(*permission));
            if (!message_stored) return std::unexpected(message_stored.error());
            if (!permission_stored) return std::unexpected(permission_stored.error());
            return std::optional<Value> {};
        });

    add(registry, "java/security/AccessControlException", "getPermission",
        "()Ljava/security/Permission;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto value = instance_field(
                machine, *receiver, "java/security/AccessControlException",
                "perm", "Ljava/security/Permission;");
            if (!value) return std::unexpected(value.error());
            auto permission = value->as_reference();
            if (!permission) return std::unexpected(permission.error());
            return std::optional<Value>(Value::from_reference(*permission));
        });
}

void register_crypto_natives(NativeMethodRegistry& registry) {
    constexpr std::string_view kSecretKeySpecClass =
        "javax/crypto/spec/SecretKeySpec";
    constexpr std::string_view kCipherClass = "javax/crypto/Cipher";

    add(registry, std::string(kSecretKeySpecClass), "<init>",
        "([BLjava/lang/String;)V",
        [kSecretKeySpecClass](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            auto key = reference_argument(arguments, 1U, false);
            auto algorithm = reference_argument(arguments, 2U, false);
            if (!receiver) return std::unexpected(receiver.error());
            if (!key) return std::unexpected(key.error());
            if (!algorithm) return std::unexpected(algorithm.error());
            auto key_copy = copy_byte_array(machine, *key);
            if (!key_copy) return std::unexpected(key_copy.error());
            auto key_root = machine.pin_native_root(*key_copy);
            if (!key_root) return std::unexpected(key_root.error());
            auto key_stored = set_instance_field(
                machine, *receiver, kSecretKeySpecClass, "key", "[B",
                Value::from_reference(*key_copy));
            auto algorithm_stored = set_instance_field(
                machine, *receiver, kSecretKeySpecClass, "algorithm",
                "Ljava/lang/String;", Value::from_reference(*algorithm));
            if (!key_stored) return std::unexpected(key_stored.error());
            if (!algorithm_stored) {
                return std::unexpected(algorithm_stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, std::string(kSecretKeySpecClass), "getAlgorithm",
        "()Ljava/lang/String;",
        [kSecretKeySpecClass](Machine& machine,
                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto value = instance_field(
                machine, *receiver, kSecretKeySpecClass, "algorithm",
                "Ljava/lang/String;");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });

    add(registry, std::string(kSecretKeySpecClass), "getFormat",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto text = make_string(machine, "RAW");
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });

    add(registry, std::string(kSecretKeySpecClass), "getEncoded", "()[B",
        [kSecretKeySpecClass](Machine& machine,
                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto value = instance_field(
                machine, *receiver, kSecretKeySpecClass, "key", "[B");
            if (!value) return std::unexpected(value.error());
            auto key = value->as_reference();
            if (!key) return std::unexpected(key.error());
            if (key->is_null()) {
                return fail(ErrorCode::invalid_state,
                            "SecretKeySpec key is not initialized");
            }
            auto copy = copy_byte_array(machine, *key);
            if (!copy) return std::unexpected(copy.error());
            return std::optional<Value>(Value::from_reference(*copy));
        });

    add(registry, std::string(kCipherClass), "getInstance",
        "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
        [kCipherClass](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto transformation = reference_argument(arguments, 0U, false);
            if (!transformation) return std::unexpected(transformation.error());
            auto text = utf8_string(machine, *transformation, false);
            if (!text) return std::unexpected(text.error());
            std::string normalized = *text;
            std::transform(normalized.begin(), normalized.end(),
                           normalized.begin(), [](char character) {
                               return static_cast<char>(std::toupper(
                                   static_cast<unsigned char>(character)));
                           });
            if (normalized != "AES/ECB/PKCS5PADDING" &&
                normalized != "AES/ECB/PKCS7PADDING") {
                return fail_java("java/security/NoSuchAlgorithmException",
                                 "unsupported Cipher transformation: " + *text);
            }
            auto cipher = machine.class_states().allocate_instance(
                machine.heap(), kCipherClass);
            if (!cipher) return std::unexpected(cipher.error());
            auto stored = set_instance_field(
                machine, *cipher, kCipherClass, "transformation",
                "Ljava/lang/String;", Value::from_reference(*transformation));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*cipher));
        });

    add(registry, std::string(kCipherClass), "init",
        "(ILjava/security/Key;)V",
        [kCipherClass](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto cipher = require_receiver(arguments);
            if (!cipher) return std::unexpected(cipher.error());
            if (arguments.size() < 3U) {
                return fail(ErrorCode::invalid_argument,
                            "Cipher.init arguments are missing");
            }
            auto mode = arguments[1].as_int();
            auto key = arguments[2].as_reference();
            if (!mode) return std::unexpected(mode.error());
            if (!key || key->is_null()) {
                return fail_java("java/security/InvalidKeyException",
                                 "Cipher key is null");
            }
            if (*mode != 1 && *mode != 2) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Cipher mode must be ENCRYPT_MODE or DECRYPT_MODE");
            }
            auto encoded = machine.invoke_instance(
                *key, "java/security/Key", "getEncoded", "()[B");
            if (!encoded) return std::unexpected(encoded.error());
            if (encoded->throwable.has_value() ||
                !encoded->return_value.has_value()) {
                return fail_java("java/security/InvalidKeyException",
                                 "Cipher key cannot be encoded");
            }
            auto encoded_ref = encoded->return_value->as_reference();
            if (!encoded_ref || encoded_ref->is_null()) {
                return fail_java("java/security/InvalidKeyException",
                                 "Cipher key encoding is null");
            }
            auto key_length = machine.heap().array_length(*encoded_ref);
            if (!key_length) return std::unexpected(key_length.error());
            if (*key_length != 16U) {
                return fail_java("java/security/InvalidKeyException",
                                 "AES-128 requires a 16-byte key");
            }
            auto key_copy = copy_byte_array(machine, *encoded_ref);
            if (!key_copy) return std::unexpected(key_copy.error());
            auto key_root = machine.pin_native_root(*key_copy);
            if (!key_root) return std::unexpected(key_root.error());
            auto mode_stored = set_instance_field(
                machine, *cipher, kCipherClass, "mode", "I",
                Value::from_int(*mode));
            auto key_stored = set_instance_field(
                machine, *cipher, kCipherClass, "key", "[B",
                Value::from_reference(*key_copy));
            if (!mode_stored) return std::unexpected(mode_stored.error());
            if (!key_stored) return std::unexpected(key_stored.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kCipherClass), "doFinal", "([B)[B",
        [kCipherClass](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto cipher = require_receiver(arguments);
            auto input = reference_argument(arguments, 1U, false);
            if (!cipher) return std::unexpected(cipher.error());
            if (!input) return std::unexpected(input.error());
            auto mode_value = instance_field(
                machine, *cipher, kCipherClass, "mode", "I");
            auto key_value = instance_field(
                machine, *cipher, kCipherClass, "key", "[B");
            if (!mode_value) return std::unexpected(mode_value.error());
            if (!key_value) return std::unexpected(key_value.error());
            auto mode = mode_value->as_int();
            auto key_ref = key_value->as_reference();
            if (!mode) return std::unexpected(mode.error());
            if (!key_ref || key_ref->is_null()) {
                return fail_java("java/lang/IllegalStateException",
                                 "Cipher is not initialized");
            }
            auto key_bytes = machine.heap().read_byte_array(*key_ref, 0U, 16U);
            if (!key_bytes) return std::unexpected(key_bytes.error());
            std::array<u8, 16U> key {};
            std::copy_n(key_bytes->begin(), 16U, key.begin());
            const auto round_keys = aes128_expand_key(key);

            auto input_length = machine.heap().array_length(*input);
            if (!input_length) return std::unexpected(input_length.error());
            auto source = machine.heap().read_byte_array(*input, 0U, *input_length);
            if (!source) return std::unexpected(source.error());
            std::vector<u8> output;
            if (*mode == 2) {
                if (source->empty() || (source->size() % 16U) != 0U) {
                    return fail_java("javax/crypto/IllegalBlockSizeException",
                                     "AES/ECB ciphertext length is invalid");
                }
                output.resize(source->size());
                for (usize offset = 0U; offset < source->size(); offset += 16U) {
                    std::span<const u8, 16U> block(
                        source->data() + offset, 16U);
                    std::span<u8, 16U> result(output.data() + offset, 16U);
                    aes128_decrypt_block(block, result, round_keys);
                }
                const u8 padding = output.back();
                if (padding == 0U || padding > 16U ||
                    static_cast<usize>(padding) > output.size()) {
                    return fail_java("javax/crypto/BadPaddingException",
                                     "AES padding is invalid");
                }
                for (usize index = output.size() - padding;
                     index < output.size(); ++index) {
                    if (output[index] != padding) {
                        return fail_java("javax/crypto/BadPaddingException",
                                         "AES padding is invalid");
                    }
                }
                output.resize(output.size() - padding);
            } else if (*mode == 1) {
                const usize padding = 16U - (source->size() % 16U);
                output = *source;
                output.insert(output.end(), padding,
                              static_cast<u8>(padding));
                for (usize offset = 0U; offset < output.size(); offset += 16U) {
                    std::array<u8, 16U> plain {};
                    std::copy_n(output.begin() +
                                    static_cast<std::ptrdiff_t>(offset),
                                16U, plain.begin());
                    std::span<u8, 16U> result(output.data() + offset, 16U);
                    aes128_encrypt_block(plain, result, round_keys);
                }
            } else {
                return fail_java("java/lang/IllegalStateException",
                                 "Cipher is not initialized");
            }

            auto result = machine.heap().allocate_array(
                "[B", output.size(), Value::from_int(0));
            if (!result) return std::unexpected(result.error());
            auto written = machine.heap().write_byte_array(*result, 0U, output);
            if (!written) return std::unexpected(written.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
}

} // namespace

void register_security_natives(NativeMethodRegistry& registry) {
    add(registry,
        "java/security/AccessController",
        "checkPermission",
        "(Ljava/security/Permission;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "AccessController.checkPermission expects one argument");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/midlet/MIDlet",
        "checkPermission",
        "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "MIDlet.checkPermission expects one argument");
            }
            auto permission = string_argument(machine, arguments, 1U, false);
            if (!permission) return std::unexpected(permission.error());
            return std::optional<Value>(Value::from_int(java_decision(
                machine.permission_policy().check(*permission))));
        });

    add(registry,
        "com/sun/midp/security/PermissionGate",
        "checkPermission",
        "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "PermissionGate.checkPermission expects one argument");
            }
            auto permission = string_argument(machine, arguments, 0U, false);
            if (!permission) return std::unexpected(permission.error());
            return std::optional<Value>(Value::from_int(java_decision(
                machine.permission_policy().check(*permission))));
        });

    add(registry,
        "com/sun/midp/security/PermissionGate",
        "requestPermission",
        "(Ljava/lang/String;Ljava/lang/String;Z)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "PermissionGate.requestPermission expects three arguments");
            }
            auto permission = string_argument(machine, arguments, 0U, false);
            auto resource = string_argument(machine, arguments, 1U, true);
            auto user_initiated = arguments[2].as_int();
            if (!permission) return std::unexpected(permission.error());
            if (!resource) return std::unexpected(resource.error());
            if (!user_initiated) return std::unexpected(user_initiated.error());
            auto response = machine.permission_policy().request(
                *permission, std::move(*resource), *user_initiated != 0);
            if (!response) return std::unexpected(response.error());
            return std::optional<Value>(Value::from_int(
                java_decision(response->decision)));
        });

    add(registry,
        "com/sun/midp/security/PermissionGate",
        "requirePermission",
        "(Ljava/lang/String;Ljava/lang/String;Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "PermissionGate.requirePermission expects three arguments");
            }
            auto permission = string_argument(machine, arguments, 0U, false);
            auto resource = string_argument(machine, arguments, 1U, true);
            auto user_initiated = arguments[2].as_int();
            if (!permission) return std::unexpected(permission.error());
            if (!resource) return std::unexpected(resource.error());
            if (!user_initiated) return std::unexpected(user_initiated.error());
            auto allowed = machine.permission_policy().require(
                *permission, std::move(*resource), *user_initiated != 0);
            if (!allowed) return std::unexpected(allowed.error());
            return std::optional<Value> {};
        });

    register_message_digest_natives(registry);
    register_permission_natives(registry);
    register_crypto_natives(registry);
}

} // namespace phoneme::vm
