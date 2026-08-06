#include "Jdk8CompatNativesParts.hpp"

#include <array>
#include <limits>
#include <string_view>
#include <vector>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
#include <stdlib.h>
#endif

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kBufferDataField = 0U;
constexpr usize kBufferLimitField = 1U;
constexpr usize kBufferPositionField = 2U;
constexpr usize kTimeUnitNanosField = 2U;

[[nodiscard]] Result<ObjectRef> create_buffer(Machine& machine,
                                               ObjectRef data,
                                               i32 position,
                                               i32 limit) {
    auto buffer = new_instance(machine, "java/nio/ByteBuffer");
    if (!buffer) return std::unexpected(buffer.error());
    auto data_stored = set_reference_field(machine, *buffer,
                                           kBufferDataField, data);
    auto limit_stored = set_int_field(machine, *buffer,
                                      kBufferLimitField, limit);
    auto position_stored = set_int_field(machine, *buffer,
                                         kBufferPositionField, position);
    if (!data_stored) return std::unexpected(data_stored.error());
    if (!limit_stored) return std::unexpected(limit_stored.error());
    if (!position_stored) return std::unexpected(position_stored.error());
    return *buffer;
}

struct BufferState final {
    ObjectRef data;
    i32 position;
};

[[nodiscard]] Result<BufferState> require_buffer(Machine& machine,
                                                  ObjectRef buffer,
                                                  i32 count) {
    auto data = reference_field(machine, buffer, kBufferDataField);
    auto limit = int_field(machine, buffer, kBufferLimitField);
    auto position = int_field(machine, buffer, kBufferPositionField);
    if (!data) return std::unexpected(data.error());
    if (!limit) return std::unexpected(limit.error());
    if (!position) return std::unexpected(position.error());
    if (count < 0 || *position < 0 || *position > *limit - count) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "ByteBuffer underflow or overflow");
    }
    return BufferState {.data = *data, .position = *position};
}

void register_byte_buffer(NativeMethodRegistry& registry) {
    add(registry, "java/nio/ByteBuffer", "allocate",
        "(I)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto capacity = int_argument(arguments, 0U);
            if (!capacity) return std::unexpected(capacity.error());
            if (*capacity < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "ByteBuffer capacity is negative");
            }
            auto data = machine.heap().allocate_array(
                "[B", static_cast<usize>(*capacity), Value::from_int(0));
            if (!data) return std::unexpected(data.error());
            auto root = machine.pin_native_root(*data);
            if (!root) return std::unexpected(root.error());
            auto buffer = create_buffer(machine, *data, 0, *capacity);
            if (!buffer) return std::unexpected(buffer.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    const auto wrap = [&registry](const char* descriptor, bool ranged) {
        add(registry, "java/nio/ByteBuffer", "wrap", descriptor,
            [ranged](Machine& machine,
                     std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto data = reference_argument(arguments, 0U);
                if (!data) return std::unexpected(data.error());
                auto length = machine.heap().array_length(*data);
                if (!length) return std::unexpected(length.error());
                if (*length > static_cast<usize>(
                        std::numeric_limits<i32>::max())) {
                    return fail(ErrorCode::overflow,
                                "ByteBuffer array is too large");
                }
                i32 offset = 0;
                i32 count = static_cast<i32>(*length);
                if (ranged) {
                    auto requested_offset = int_argument(arguments, 1U);
                    auto requested_count = int_argument(arguments, 2U);
                    if (!requested_offset) {
                        return std::unexpected(requested_offset.error());
                    }
                    if (!requested_count) {
                        return std::unexpected(requested_count.error());
                    }
                    offset = *requested_offset;
                    count = *requested_count;
                }
                if (offset < 0 || count < 0 ||
                    static_cast<usize>(offset) > *length ||
                    static_cast<usize>(count) >
                        *length - static_cast<usize>(offset)) {
                    return fail_java("java/lang/IndexOutOfBoundsException",
                                     "ByteBuffer wrap range is invalid");
                }
                auto buffer = create_buffer(machine, *data, offset,
                                            offset + count);
                if (!buffer) return std::unexpected(buffer.error());
                return std::optional<Value>(Value::from_reference(*buffer));
            });
    };
    wrap("([B)Ljava/nio/ByteBuffer;", false);
    wrap("([BII)Ljava/nio/ByteBuffer;", true);
    add(registry, "java/nio/ByteBuffer", "put",
        "(B)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto value = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!value) return std::unexpected(value.error());
            auto state = require_buffer(machine, *buffer, 1);
            if (!state) return std::unexpected(state.error());
            auto stored = machine.heap().set_element(
                state->data, static_cast<usize>(state->position),
                Value::from_int(static_cast<i32>(static_cast<i8>(*value))));
            if (!stored) return std::unexpected(stored.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 1);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    const auto transfer = [&registry](const char* name, bool write) {
        add(registry, "java/nio/ByteBuffer", name,
            "([B)Ljava/nio/ByteBuffer;",
            [write](Machine& machine,
                    std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                auto array = reference_argument(arguments, 1U);
                if (!buffer) return std::unexpected(buffer.error());
                if (!array) return std::unexpected(array.error());
                auto length = machine.heap().array_length(*array);
                if (!length) return std::unexpected(length.error());
                if (*length > static_cast<usize>(
                        std::numeric_limits<i32>::max())) {
                    return fail(ErrorCode::overflow,
                                "ByteBuffer transfer is too large");
                }
                auto state = require_buffer(
                    machine, *buffer, static_cast<i32>(*length));
                if (!state) return std::unexpected(state.error());
                for (usize index = 0U; index < *length; ++index) {
                    const usize buffer_index =
                        static_cast<usize>(state->position) + index;
                    if (write) {
                        auto value = machine.heap().element(*array, index);
                        if (!value) return std::unexpected(value.error());
                        auto stored = machine.heap().set_element(
                            state->data, buffer_index, *value);
                        if (!stored) return std::unexpected(stored.error());
                    } else {
                        auto value = machine.heap().element(state->data,
                                                            buffer_index);
                        if (!value) return std::unexpected(value.error());
                        auto stored = machine.heap().set_element(*array, index,
                                                                 *value);
                        if (!stored) return std::unexpected(stored.error());
                    }
                }
                auto advanced = set_int_field(
                    machine, *buffer, kBufferPositionField,
                    state->position + static_cast<i32>(*length));
                if (!advanced) return std::unexpected(advanced.error());
                return std::optional<Value>(Value::from_reference(*buffer));
            });
    };
    transfer("put", true);
    transfer("get", false);
    add(registry, "java/nio/ByteBuffer", "putInt",
        "(I)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto value = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!value) return std::unexpected(value.error());
            auto state = require_buffer(machine, *buffer, 4);
            if (!state) return std::unexpected(state.error());
            const u32 bits = static_cast<u32>(*value);
            for (i32 offset = 0; offset < 4; ++offset) {
                const u32 shift = static_cast<u32>((3 - offset) * 8);
                const i32 byte = static_cast<i32>(
                    static_cast<i8>((bits >> shift) & 0xFFU));
                auto stored = machine.heap().set_element(
                    state->data,
                    static_cast<usize>(state->position + offset),
                    Value::from_int(byte));
                if (!stored) return std::unexpected(stored.error());
            }
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 4);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "get", "()B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto state = require_buffer(machine, *buffer, 1);
            if (!state) return std::unexpected(state.error());
            auto value = machine.heap().element(
                state->data, static_cast<usize>(state->position));
            if (!value) return std::unexpected(value.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 1);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(*value);
        });
    add(registry, "java/nio/ByteBuffer", "getInt", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto state = require_buffer(machine, *buffer, 4);
            if (!state) return std::unexpected(state.error());
            u32 result = 0U;
            for (i32 offset = 0; offset < 4; ++offset) {
                auto value = machine.heap().element(
                    state->data,
                    static_cast<usize>(state->position + offset));
                if (!value) return std::unexpected(value.error());
                auto byte = value->as_int();
                if (!byte) return std::unexpected(byte.error());
                result = (result << 8U) |
                         (static_cast<u32>(*byte) & 0xFFU);
            }
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 4);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(result)));
        });
    add(registry, "java/nio/ByteBuffer", "array", "()[B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto data = reference_field(machine, *buffer, kBufferDataField);
            if (!data) return std::unexpected(data.error());
            return std::optional<Value>(Value::from_reference(*data));
        });
    add(registry, "java/nio/ByteBuffer", "position", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto position = int_field(machine, *buffer,
                                      kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            return std::optional<Value>(Value::from_int(*position));
        });
    add(registry, "java/nio/ByteBuffer", "position",
        "(I)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto position = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!position) return std::unexpected(position.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            if (!limit) return std::unexpected(limit.error());
            if (*position < 0 || *position > *limit) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "ByteBuffer position is outside limit");
            }
            auto stored = set_int_field(machine, *buffer,
                                        kBufferPositionField, *position);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "remaining", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            auto position = int_field(machine, *buffer,
                                      kBufferPositionField);
            if (!limit) return std::unexpected(limit.error());
            if (!position) return std::unexpected(position.error());
            return std::optional<Value>(
                Value::from_int(*limit - *position));
        });
}

[[nodiscard]] bool platform_secure_bytes(std::span<u8> output) noexcept {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
    if (!output.empty()) arc4random_buf(output.data(), output.size());
    return true;
#else
    (void)output;
    return false;
#endif
}

void register_secure_random(NativeMethodRegistry& registry) {
    alias(registry, "java/util/Random", "<init>", "()V",
          "java/security/SecureRandom");
    add(registry, "java/security/SecureRandom", "nextLong", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto random = receiver(arguments);
            if (!random) return std::unexpected(random.error());
            u64 bits = 0U;
            auto bytes = std::span<u8>(reinterpret_cast<u8*>(&bits),
                                       sizeof(bits));
            if (platform_secure_bytes(bytes)) {
                return std::optional<Value>(
                    Value::from_long(static_cast<i64>(bits)));
            }
            const Value random_value = Value::from_reference(*random);
            return invoke_native(machine, "java/util/Random", "nextLong",
                                 "()J",
                                 std::span<const Value>(&random_value, 1U));
        });
    add(registry, "java/security/SecureRandom", "nextBytes", "([B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto random = receiver(arguments);
            auto output = reference_argument(arguments, 1U);
            if (!random) return std::unexpected(random.error());
            if (!output) return std::unexpected(output.error());
            auto length = machine.heap().array_length(*output);
            if (!length) return std::unexpected(length.error());
            std::vector<u8> secure(*length);
            if (platform_secure_bytes(secure)) {
                auto written = machine.heap().write_byte_array(
                    *output, 0U, secure);
                if (!written) return std::unexpected(written.error());
                return std::optional<Value> {};
            }
            u64 bits = 0U;
            for (usize index = 0U; index < *length; ++index) {
                if ((index & 7U) == 0U) {
                    const Value random_value = Value::from_reference(*random);
                    auto next = invoke_native(
                        machine, "java/util/Random", "nextLong", "()J",
                        std::span<const Value>(&random_value, 1U));
                    if (!next) return std::unexpected(next.error());
                    if (!next->has_value()) {
                        return fail(ErrorCode::internal_error,
                                    "SecureRandom nextLong returned no value");
                    }
                    auto value = next->value().as_long();
                    if (!value) return std::unexpected(value.error());
                    bits = static_cast<u64>(*value);
                }
                auto stored = machine.heap().set_element(
                    *output, index,
                    Value::from_int(static_cast<i32>(
                        static_cast<i8>(bits & 0xFFU))));
                if (!stored) return std::unexpected(stored.error());
                bits >>= 8U;
            }
            return std::optional<Value> {};
        });
}

[[nodiscard]] i64 saturated_multiply(i64 value, i64 multiplier) {
    if (value > 0 && multiplier > 0 &&
        value > std::numeric_limits<i64>::max() / multiplier) {
        return std::numeric_limits<i64>::max();
    }
    if (value < 0 && multiplier > 0 &&
        value < std::numeric_limits<i64>::min() / multiplier) {
        return std::numeric_limits<i64>::min();
    }
    return value * multiplier;
}

void register_time_unit(NativeMethodRegistry& registry) {
    add(registry, "java/util/concurrent/TimeUnit", "<init>",
        "(Ljava/lang/String;IJ)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto unit = receiver(arguments);
            auto name = reference_argument(arguments, 1U);
            auto ordinal = int_argument(arguments, 2U);
            auto nanos = long_argument(arguments, 3U);
            if (!unit) return std::unexpected(unit.error());
            if (!name) return std::unexpected(name.error());
            if (!ordinal) return std::unexpected(ordinal.error());
            if (!nanos) return std::unexpected(nanos.error());
            auto name_stored = set_reference_field(machine, *unit, 0U, *name);
            auto ordinal_stored = set_int_field(machine, *unit, 1U, *ordinal);
            auto nanos_stored = set_long_field(machine, *unit,
                                               kTimeUnitNanosField, *nanos);
            if (!name_stored) return std::unexpected(name_stored.error());
            if (!ordinal_stored) return std::unexpected(ordinal_stored.error());
            if (!nanos_stored) return std::unexpected(nanos_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/concurrent/TimeUnit", "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            struct Unit final { const char* name; i64 nanos; };
            static constexpr std::array<Unit, 7> units {{
                {"NANOSECONDS", 1LL}, {"MICROSECONDS", 1'000LL},
                {"MILLISECONDS", 1'000'000LL},
                {"SECONDS", 1'000'000'000LL},
                {"MINUTES", 60'000'000'000LL},
                {"HOURS", 3'600'000'000'000LL},
                {"DAYS", 86'400'000'000'000LL},
            }};
            for (usize index = 0U; index < units.size(); ++index) {
                std::u16string name;
                for (const char value : std::string_view(units[index].name)) {
                    name.push_back(static_cast<char16_t>(
                        static_cast<unsigned char>(value)));
                }
                auto name_string = create_string(machine, std::move(name));
                if (!name_string) return std::unexpected(name_string.error());
                auto name_root = machine.pin_native_root(*name_string);
                if (!name_root) return std::unexpected(name_root.error());
                auto unit = new_instance(machine,
                                         "java/util/concurrent/TimeUnit");
                if (!unit) return std::unexpected(unit.error());
                auto unit_root = machine.pin_native_root(*unit);
                if (!unit_root) return std::unexpected(unit_root.error());
                const std::array<Value, 4> arguments {
                    Value::from_reference(*unit),
                    Value::from_reference(*name_string),
                    Value::from_int(static_cast<i32>(index)),
                    Value::from_long(units[index].nanos),
                };
                auto initialized = invoke_native(
                    machine, "java/util/concurrent/TimeUnit", "<init>",
                    "(Ljava/lang/String;IJ)V", arguments);
                if (!initialized) return std::unexpected(initialized.error());
                auto location = machine.class_states().resolve_field(
                    "java/util/concurrent/TimeUnit", units[index].name,
                    "Ljava/util/concurrent/TimeUnit;", true);
                if (!location) return std::unexpected(location.error());
                auto stored = machine.class_states().set_static_field(
                    *location, Value::from_reference(*unit));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    const auto convert_to = [&registry](const char* name, i64 target_nanos) {
        add(registry, "java/util/concurrent/TimeUnit", name, "(J)J",
            [target_nanos](Machine& machine,
                           std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto unit = receiver(arguments);
                auto duration = long_argument(arguments, 1U);
                if (!unit) return std::unexpected(unit.error());
                if (!duration) return std::unexpected(duration.error());
                auto source_nanos = long_field(machine, *unit,
                                               kTimeUnitNanosField);
                if (!source_nanos) {
                    return std::unexpected(source_nanos.error());
                }
                const i64 converted = *source_nanos >= target_nanos
                    ? saturated_multiply(*duration,
                                         *source_nanos / target_nanos)
                    : *duration / (target_nanos / *source_nanos);
                return std::optional<Value>(Value::from_long(converted));
            });
    };
    convert_to("toNanos", 1LL);
    convert_to("toMicros", 1'000LL);
    convert_to("toMillis", 1'000'000LL);
    convert_to("toSeconds", 1'000'000'000LL);
    add(registry, "java/util/concurrent/TimeUnit", "convert",
        "(JLjava/util/concurrent/TimeUnit;)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto target = receiver(arguments);
            auto duration = long_argument(arguments, 1U);
            auto source = reference_argument(arguments, 2U);
            if (!target) return std::unexpected(target.error());
            if (!duration) return std::unexpected(duration.error());
            if (!source) return std::unexpected(source.error());
            auto source_nanos = long_field(machine, *source,
                                           kTimeUnitNanosField);
            auto target_nanos = long_field(machine, *target,
                                           kTimeUnitNanosField);
            if (!source_nanos) return std::unexpected(source_nanos.error());
            if (!target_nanos) return std::unexpected(target_nanos.error());
            const i64 converted = *source_nanos >= *target_nanos
                ? saturated_multiply(*duration,
                                     *source_nanos / *target_nanos)
                : *duration / (*target_nanos / *source_nanos);
            return std::optional<Value>(Value::from_long(converted));
        });
}

} // namespace

void register_jdk8_binary_natives(NativeMethodRegistry& registry) {
    register_byte_buffer(registry);
    register_secure_random(registry);
    register_time_unit(registry);
}

} // namespace phoneme::vm
