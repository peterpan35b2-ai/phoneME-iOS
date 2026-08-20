#include "Jdk8CompatNativesParts.hpp"

#include <array>
#include <bit>
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
constexpr usize kBufferMarkField = 3U;
constexpr usize kBufferOrderField = 4U;
constexpr i32 kBigEndian = 0;
constexpr i32 kLittleEndian = 1;
constexpr usize kByteOrderNameField = 0U;
constexpr usize kByteOrderKindField = 1U;
constexpr usize kTimeUnitNanosField = 2U;

[[nodiscard]] Result<ObjectRef> create_buffer(Machine& machine,
                                               ObjectRef data,
                                               i32 position,
                                               i32 limit) {
    auto buffer = new_instance(machine, "java/nio/HeapByteBuffer");
    if (!buffer) return std::unexpected(buffer.error());
    auto data_stored = set_reference_field(machine, *buffer,
                                           kBufferDataField, data);
    auto limit_stored = set_int_field(machine, *buffer,
                                      kBufferLimitField, limit);
    auto position_stored = set_int_field(machine, *buffer,
                                         kBufferPositionField, position);
    auto mark_stored = set_int_field(machine, *buffer, kBufferMarkField, -1);
    auto order_stored = set_int_field(machine, *buffer, kBufferOrderField,
                                      kBigEndian);
    if (!data_stored) return std::unexpected(data_stored.error());
    if (!limit_stored) return std::unexpected(limit_stored.error());
    if (!position_stored) return std::unexpected(position_stored.error());
    if (!mark_stored) return std::unexpected(mark_stored.error());
    if (!order_stored) return std::unexpected(order_stored.error());
    return *buffer;
}

struct BufferState final {
    ObjectRef data;
    i32 position;
};

[[nodiscard]] Result<BufferState> require_buffer(Machine& machine,
                                                  ObjectRef buffer,
                                                  i32 count,
                                                  bool writing) {
    auto data = reference_field(machine, buffer, kBufferDataField);
    auto limit = int_field(machine, buffer, kBufferLimitField);
    auto position = int_field(machine, buffer, kBufferPositionField);
    if (!data) return std::unexpected(data.error());
    if (!limit) return std::unexpected(limit.error());
    if (!position) return std::unexpected(position.error());
    if (count < 0 || *position < 0 || *position > *limit - count) {
        return fail_java(writing ? "java/nio/BufferOverflowException"
                                 : "java/nio/BufferUnderflowException",
                         writing ? "ByteBuffer overflow"
                                 : "ByteBuffer underflow");
    }
    return BufferState {.data = *data, .position = *position};
}

[[nodiscard]] Result<ObjectRef> require_buffer_range(Machine& machine,
                                                      ObjectRef buffer,
                                                      i32 index,
                                                      i32 count) {
    auto data = reference_field(machine, buffer, kBufferDataField);
    auto limit = int_field(machine, buffer, kBufferLimitField);
    if (!data) return std::unexpected(data.error());
    if (!limit) return std::unexpected(limit.error());
    if (index < 0 || count < 0 || index > *limit - count) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "ByteBuffer index is outside limit");
    }
    return *data;
}

[[nodiscard]] Result<u64> read_buffer_bits(Machine& machine,
                                           ObjectRef data,
                                           i32 index,
                                           i32 count,
                                           bool little_endian) {
    u64 result = 0U;
    for (i32 offset = 0; offset < count; ++offset) {
        auto value = machine.heap().element(
            data, static_cast<usize>(index + offset));
        if (!value) return std::unexpected(value.error());
        auto byte = value->as_int();
        if (!byte) return std::unexpected(byte.error());
        const u32 shift = little_endian
            ? static_cast<u32>(offset * 8)
            : static_cast<u32>((count - 1 - offset) * 8);
        result |= (static_cast<u64>(*byte) & 0xFFU) << shift;
    }
    return result;
}

[[nodiscard]] Status write_buffer_bits(Machine& machine,
                                       ObjectRef data,
                                       i32 index,
                                       i32 count,
                                       u64 bits,
                                       bool little_endian) {
    for (i32 offset = 0; offset < count; ++offset) {
        const u32 shift = little_endian
            ? static_cast<u32>(offset * 8)
            : static_cast<u32>((count - 1 - offset) * 8);
        const i32 byte = static_cast<i32>(static_cast<i8>(
            (bits >> shift) & 0xFFU));
        auto stored = machine.heap().set_element(
            data, static_cast<usize>(index + offset), Value::from_int(byte));
        if (!stored) return stored;
    }
    return {};
}

[[nodiscard]] Result<bool> buffer_little_endian(Machine& machine,
                                                 ObjectRef buffer) {
    auto order = int_field(machine, buffer, kBufferOrderField);
    if (!order) return std::unexpected(order.error());
    return *order == kLittleEndian;
}

[[nodiscard]] Result<i32> buffer_capacity(Machine& machine, ObjectRef buffer) {
    auto data = reference_field(machine, buffer, kBufferDataField);
    if (!data) return std::unexpected(data.error());
    auto length = machine.heap().array_length(*data);
    if (!length) return std::unexpected(length.error());
    if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
        return fail(ErrorCode::overflow, "Buffer capacity is too large");
    }
    return static_cast<i32>(*length);
}

[[nodiscard]] Status discard_buffer_mark(Machine& machine, ObjectRef buffer) {
    return set_int_field(machine, buffer, kBufferMarkField, -1);
}

[[nodiscard]] Result<ObjectRef> byte_order_constant(Machine& machine,
                                                    std::string_view name) {
    auto location = machine.class_states().resolve_field(
        "java/nio/ByteOrder", name, "Ljava/nio/ByteOrder;", true);
    if (!location) return std::unexpected(location.error());
    auto value = machine.class_states().static_field(*location);
    if (!value) return std::unexpected(value.error());
    auto reference = value->as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (!reference->is_null()) return *reference;

    auto initialized = invoke_native(machine, "java/nio/ByteOrder", "<clinit>",
                                     "()V", {});
    if (!initialized) return std::unexpected(initialized.error());
    value = machine.class_states().static_field(*location);
    if (!value) return std::unexpected(value.error());
    reference = value->as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (reference->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "ByteOrder static initialization produced null constant");
    }
    return *reference;
}

void register_byte_order(NativeMethodRegistry& registry) {
    add(registry, "java/nio/ByteOrder", "<init>",
        "(Ljava/lang/String;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto name = reference_argument(arguments, 1U);
            auto kind = int_argument(arguments, 2U);
            if (!object) return std::unexpected(object.error());
            if (!name) return std::unexpected(name.error());
            if (!kind) return std::unexpected(kind.error());
            auto name_stored = set_reference_field(machine, *object,
                                                   kByteOrderNameField, *name);
            auto kind_stored = set_int_field(machine, *object,
                                             kByteOrderKindField, *kind);
            if (!name_stored) return std::unexpected(name_stored.error());
            if (!kind_stored) return std::unexpected(kind_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/nio/ByteOrder", "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            struct Order final {
                const char* field;
                const char16_t* text;
                i32 kind;
            };
            static constexpr std::array<Order, 2> orders {{
                {"BIG_ENDIAN", u"BIG_ENDIAN", kBigEndian},
                {"LITTLE_ENDIAN", u"LITTLE_ENDIAN", kLittleEndian},
            }};
            for (const auto& order : orders) {
                auto location = machine.class_states().resolve_field(
                    "java/nio/ByteOrder", order.field,
                    "Ljava/nio/ByteOrder;", true);
                if (!location) return std::unexpected(location.error());
                auto current = machine.class_states().static_field(*location);
                if (!current) return std::unexpected(current.error());
                auto current_reference = current->as_reference();
                if (!current_reference) {
                    return std::unexpected(current_reference.error());
                }
                if (!current_reference->is_null()) continue;

                auto name = create_string(machine, std::u16string(order.text));
                if (!name) return std::unexpected(name.error());
                auto name_root = machine.pin_native_root(*name);
                if (!name_root) return std::unexpected(name_root.error());
                auto object = new_instance(machine, "java/nio/ByteOrder");
                if (!object) return std::unexpected(object.error());
                auto object_root = machine.pin_native_root(*object);
                if (!object_root) return std::unexpected(object_root.error());
                const std::array<Value, 3> init_arguments {
                    Value::from_reference(*object),
                    Value::from_reference(*name), Value::from_int(order.kind),
                };
                auto initialized = invoke_native(
                    machine, "java/nio/ByteOrder", "<init>",
                    "(Ljava/lang/String;I)V", init_arguments);
                if (!initialized) return std::unexpected(initialized.error());
                auto stored = machine.class_states().set_static_field(
                    *location, Value::from_reference(*object));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/nio/ByteOrder", "nativeOrder",
        "()Ljava/nio/ByteOrder;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            constexpr bool native_little =
                std::endian::native == std::endian::little;
            auto order = byte_order_constant(
                machine, native_little ? "LITTLE_ENDIAN" : "BIG_ENDIAN");
            if (!order) return std::unexpected(order.error());
            return std::optional<Value>(Value::from_reference(*order));
        });
    add(registry, "java/nio/ByteOrder", "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto name = reference_field(machine, *object, kByteOrderNameField);
            if (!name) return std::unexpected(name.error());
            return std::optional<Value>(Value::from_reference(*name));
        });
}

void register_buffer_base(NativeMethodRegistry& registry) {
    add(registry, "java/nio/Buffer", "capacity", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto capacity = buffer_capacity(machine, *buffer);
            if (!capacity) return std::unexpected(capacity.error());
            return std::optional<Value>(Value::from_int(*capacity));
        });
    add(registry, "java/nio/Buffer", "position", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto position = int_field(machine, *buffer, kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            return std::optional<Value>(Value::from_int(*position));
        });
    add(registry, "java/nio/Buffer", "position", "(I)Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto requested = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!requested) return std::unexpected(requested.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            if (!limit) return std::unexpected(limit.error());
            if (*requested < 0 || *requested > *limit) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Buffer position is outside limit");
            }
            auto stored = set_int_field(machine, *buffer,
                                        kBufferPositionField, *requested);
            if (!stored) return std::unexpected(stored.error());
            auto mark = int_field(machine, *buffer, kBufferMarkField);
            if (!mark) return std::unexpected(mark.error());
            if (*mark > *requested) {
                auto discarded = discard_buffer_mark(machine, *buffer);
                if (!discarded) return std::unexpected(discarded.error());
            }
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "limit", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            if (!limit) return std::unexpected(limit.error());
            return std::optional<Value>(Value::from_int(*limit));
        });
    add(registry, "java/nio/Buffer", "limit", "(I)Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto requested = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!requested) return std::unexpected(requested.error());
            auto capacity = buffer_capacity(machine, *buffer);
            if (!capacity) return std::unexpected(capacity.error());
            if (*requested < 0 || *requested > *capacity) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Buffer limit is outside capacity");
            }
            auto stored = set_int_field(machine, *buffer,
                                        kBufferLimitField, *requested);
            if (!stored) return std::unexpected(stored.error());
            auto position = int_field(machine, *buffer, kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            if (*position > *requested) {
                auto moved = set_int_field(machine, *buffer,
                                           kBufferPositionField, *requested);
                if (!moved) return std::unexpected(moved.error());
            }
            auto mark = int_field(machine, *buffer, kBufferMarkField);
            if (!mark) return std::unexpected(mark.error());
            if (*mark > *requested) {
                auto discarded = discard_buffer_mark(machine, *buffer);
                if (!discarded) return std::unexpected(discarded.error());
            }
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "mark", "()Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto position = int_field(machine, *buffer, kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            auto stored = set_int_field(machine, *buffer, kBufferMarkField,
                                        *position);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "reset", "()Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto mark = int_field(machine, *buffer, kBufferMarkField);
            if (!mark) return std::unexpected(mark.error());
            if (*mark < 0) {
                return fail_java("java/nio/InvalidMarkException",
                                 "buffer mark is not set");
            }
            auto stored = set_int_field(machine, *buffer,
                                        kBufferPositionField, *mark);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "clear", "()Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto capacity = buffer_capacity(machine, *buffer);
            if (!capacity) return std::unexpected(capacity.error());
            auto limit = set_int_field(machine, *buffer, kBufferLimitField,
                                       *capacity);
            auto position = set_int_field(machine, *buffer,
                                          kBufferPositionField, 0);
            auto mark = discard_buffer_mark(machine, *buffer);
            if (!limit) return std::unexpected(limit.error());
            if (!position) return std::unexpected(position.error());
            if (!mark) return std::unexpected(mark.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "flip", "()Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto position = int_field(machine, *buffer, kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            auto limit = set_int_field(machine, *buffer, kBufferLimitField,
                                       *position);
            auto reset = set_int_field(machine, *buffer,
                                       kBufferPositionField, 0);
            auto mark = discard_buffer_mark(machine, *buffer);
            if (!limit) return std::unexpected(limit.error());
            if (!reset) return std::unexpected(reset.error());
            if (!mark) return std::unexpected(mark.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "rewind", "()Ljava/nio/Buffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto reset = set_int_field(machine, *buffer,
                                       kBufferPositionField, 0);
            auto mark = discard_buffer_mark(machine, *buffer);
            if (!reset) return std::unexpected(reset.error());
            if (!mark) return std::unexpected(mark.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/Buffer", "remaining", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            auto position = int_field(machine, *buffer, kBufferPositionField);
            if (!limit) return std::unexpected(limit.error());
            if (!position) return std::unexpected(position.error());
            return std::optional<Value>(Value::from_int(*limit - *position));
        });
    add(registry, "java/nio/Buffer", "hasRemaining", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            auto position = int_field(machine, *buffer, kBufferPositionField);
            if (!limit) return std::unexpected(limit.error());
            if (!position) return std::unexpected(position.error());
            return std::optional<Value>(Value::from_int(
                *position < *limit ? 1 : 0));
        });
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
            auto state = require_buffer(machine, *buffer, 1, true);
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
    add(registry, "java/nio/ByteBuffer", "put",
        "(IB)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            auto value = int_argument(arguments, 2U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            if (!value) return std::unexpected(value.error());
            auto data = require_buffer_range(machine, *buffer, *index, 1);
            if (!data) return std::unexpected(data.error());
            auto stored = machine.heap().set_element(
                *data, static_cast<usize>(*index),
                Value::from_int(static_cast<i32>(static_cast<i8>(*value))));
            if (!stored) return std::unexpected(stored.error());
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
                    machine, *buffer, static_cast<i32>(*length), write);
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
    const auto ranged_transfer = [&registry](const char* name, bool write) {
        add(registry, "java/nio/ByteBuffer", name,
            "([BII)Ljava/nio/ByteBuffer;",
            [write](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                auto array = reference_argument(arguments, 1U);
                auto offset = int_argument(arguments, 2U);
                auto count = int_argument(arguments, 3U);
                if (!buffer) return std::unexpected(buffer.error());
                if (!array) return std::unexpected(array.error());
                if (!offset) return std::unexpected(offset.error());
                if (!count) return std::unexpected(count.error());
                auto array_length = machine.heap().array_length(*array);
                if (!array_length) return std::unexpected(array_length.error());
                if (*offset < 0 || *count < 0 ||
                    static_cast<usize>(*offset) > *array_length ||
                    static_cast<usize>(*count) >
                        *array_length - static_cast<usize>(*offset)) {
                    return fail_java("java/lang/IndexOutOfBoundsException",
                                     "ByteBuffer array range is invalid");
                }
                auto state = require_buffer(machine, *buffer, *count, write);
                if (!state) return std::unexpected(state.error());
                for (i32 relative = 0; relative < *count; ++relative) {
                    const usize array_index =
                        static_cast<usize>(*offset + relative);
                    const usize buffer_index =
                        static_cast<usize>(state->position + relative);
                    if (write) {
                        auto value = machine.heap().element(*array, array_index);
                        if (!value) return std::unexpected(value.error());
                        auto stored = machine.heap().set_element(
                            state->data, buffer_index, *value);
                        if (!stored) return std::unexpected(stored.error());
                    } else {
                        auto value = machine.heap().element(state->data,
                                                            buffer_index);
                        if (!value) return std::unexpected(value.error());
                        auto stored = machine.heap().set_element(
                            *array, array_index, *value);
                        if (!stored) return std::unexpected(stored.error());
                    }
                }
                auto advanced = set_int_field(
                    machine, *buffer, kBufferPositionField,
                    state->position + *count);
                if (!advanced) return std::unexpected(advanced.error());
                return std::optional<Value>(Value::from_reference(*buffer));
            });
    };
    ranged_transfer("put", true);
    ranged_transfer("get", false);
    add(registry, "java/nio/ByteBuffer", "putShort",
        "(S)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto value = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!value) return std::unexpected(value.error());
            auto state = require_buffer(machine, *buffer, 2, true);
            if (!state) return std::unexpected(state.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto stored = write_buffer_bits(
                machine, state->data, state->position, 2,
                static_cast<u16>(static_cast<i16>(*value)), *little);
            if (!stored) return std::unexpected(stored.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 2);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "putShort",
        "(IS)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            auto value = int_argument(arguments, 2U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            if (!value) return std::unexpected(value.error());
            auto data = require_buffer_range(machine, *buffer, *index, 2);
            if (!data) return std::unexpected(data.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto stored = write_buffer_bits(
                machine, *data, *index, 2,
                static_cast<u16>(static_cast<i16>(*value)), *little);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "putInt",
        "(I)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto value = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!value) return std::unexpected(value.error());
            auto state = require_buffer(machine, *buffer, 4, true);
            if (!state) return std::unexpected(state.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto stored = write_buffer_bits(
                machine, state->data, state->position, 4,
                static_cast<u32>(*value), *little);
            if (!stored) return std::unexpected(stored.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 4);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "putInt",
        "(II)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            auto value = int_argument(arguments, 2U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            if (!value) return std::unexpected(value.error());
            auto data = require_buffer_range(machine, *buffer, *index, 4);
            if (!data) return std::unexpected(data.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto stored = write_buffer_bits(machine, *data, *index, 4,
                                            static_cast<u32>(*value), *little);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "putLong",
        "(J)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto value = long_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!value) return std::unexpected(value.error());
            auto state = require_buffer(machine, *buffer, 8, true);
            if (!state) return std::unexpected(state.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto stored = write_buffer_bits(
                machine, state->data, state->position, 8,
                static_cast<u64>(*value), *little);
            if (!stored) return std::unexpected(stored.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 8);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "putLong",
        "(IJ)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            auto value = long_argument(arguments, 2U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            if (!value) return std::unexpected(value.error());
            auto data = require_buffer_range(machine, *buffer, *index, 8);
            if (!data) return std::unexpected(data.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto stored = write_buffer_bits(machine, *data, *index, 8,
                                            static_cast<u64>(*value), *little);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "get", "()B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto state = require_buffer(machine, *buffer, 1, false);
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
    add(registry, "java/nio/ByteBuffer", "get", "(I)B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            auto data = require_buffer_range(machine, *buffer, *index, 1);
            if (!data) return std::unexpected(data.error());
            auto value = machine.heap().element(*data,
                                                static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
    add(registry, "java/nio/ByteBuffer", "getShort", "()S",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto state = require_buffer(machine, *buffer, 2, false);
            if (!state) return std::unexpected(state.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto bits = read_buffer_bits(machine, state->data,
                                         state->position, 2, *little);
            if (!bits) return std::unexpected(bits.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 2);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(static_cast<i16>(*bits))));
        });
    add(registry, "java/nio/ByteBuffer", "getShort", "(I)S",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            auto data = require_buffer_range(machine, *buffer, *index, 2);
            if (!data) return std::unexpected(data.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto bits = read_buffer_bits(machine, *data, *index, 2, *little);
            if (!bits) return std::unexpected(bits.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(static_cast<i16>(*bits))));
        });
    add(registry, "java/nio/ByteBuffer", "getInt", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto state = require_buffer(machine, *buffer, 4, false);
            if (!state) return std::unexpected(state.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto result = read_buffer_bits(machine, state->data,
                                           state->position, 4, *little);
            if (!result) return std::unexpected(result.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 4);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(static_cast<u32>(*result))));
        });
    add(registry, "java/nio/ByteBuffer", "getInt", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            auto data = require_buffer_range(machine, *buffer, *index, 4);
            if (!data) return std::unexpected(data.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto bits = read_buffer_bits(machine, *data, *index, 4, *little);
            if (!bits) return std::unexpected(bits.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(static_cast<u32>(*bits))));
        });
    add(registry, "java/nio/ByteBuffer", "getLong", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto state = require_buffer(machine, *buffer, 8, false);
            if (!state) return std::unexpected(state.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto bits = read_buffer_bits(machine, state->data,
                                         state->position, 8, *little);
            if (!bits) return std::unexpected(bits.error());
            auto advanced = set_int_field(machine, *buffer,
                                          kBufferPositionField,
                                          state->position + 8);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(
                Value::from_long(static_cast<i64>(*bits)));
        });
    add(registry, "java/nio/ByteBuffer", "getLong", "(I)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!index) return std::unexpected(index.error());
            auto data = require_buffer_range(machine, *buffer, *index, 8);
            if (!data) return std::unexpected(data.error());
            auto little = buffer_little_endian(machine, *buffer);
            if (!little) return std::unexpected(little.error());
            auto bits = read_buffer_bits(machine, *data, *index, 8, *little);
            if (!bits) return std::unexpected(bits.error());
            return std::optional<Value>(
                Value::from_long(static_cast<i64>(*bits)));
        });
    const auto put_int_bits = [&registry](const char* name,
                                          const char* relative_descriptor,
                                          const char* absolute_descriptor,
                                          i32 byte_count,
                                          auto encode) {
        add(registry, "java/nio/ByteBuffer", name, relative_descriptor,
            [byte_count, encode](Machine& machine,
                                 std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                if (!buffer) return std::unexpected(buffer.error());
                auto bits = encode(arguments, 1U);
                if (!bits) return std::unexpected(bits.error());
                auto state = require_buffer(machine, *buffer, byte_count, true);
                if (!state) return std::unexpected(state.error());
                auto little = buffer_little_endian(machine, *buffer);
                if (!little) return std::unexpected(little.error());
                auto stored = write_buffer_bits(machine, state->data,
                                                state->position, byte_count,
                                                *bits, *little);
                if (!stored) return std::unexpected(stored.error());
                auto advanced = set_int_field(machine, *buffer,
                                              kBufferPositionField,
                                              state->position + byte_count);
                if (!advanced) return std::unexpected(advanced.error());
                return std::optional<Value>(Value::from_reference(*buffer));
            });
        add(registry, "java/nio/ByteBuffer", name, absolute_descriptor,
            [byte_count, encode](Machine& machine,
                                 std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                auto index = int_argument(arguments, 1U);
                if (!buffer) return std::unexpected(buffer.error());
                if (!index) return std::unexpected(index.error());
                auto bits = encode(arguments, 2U);
                if (!bits) return std::unexpected(bits.error());
                auto data = require_buffer_range(machine, *buffer, *index,
                                                 byte_count);
                if (!data) return std::unexpected(data.error());
                auto little = buffer_little_endian(machine, *buffer);
                if (!little) return std::unexpected(little.error());
                auto stored = write_buffer_bits(machine, *data, *index,
                                                byte_count, *bits, *little);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_reference(*buffer));
            });
    };
    put_int_bits("putChar", "(C)Ljava/nio/ByteBuffer;",
                 "(IC)Ljava/nio/ByteBuffer;", 2,
                 [](std::span<const Value> arguments, usize index)
                    -> Result<u64> {
                    auto value = int_argument(arguments, index);
                    if (!value) return std::unexpected(value.error());
                    return static_cast<u16>(*value);
                 });
    put_int_bits("putFloat", "(F)Ljava/nio/ByteBuffer;",
                 "(IF)Ljava/nio/ByteBuffer;", 4,
                 [](std::span<const Value> arguments, usize index)
                    -> Result<u64> {
                    auto value = float_argument(arguments, index);
                    if (!value) return std::unexpected(value.error());
                    return static_cast<u64>(std::bit_cast<u32>(*value));
                 });
    put_int_bits("putDouble", "(D)Ljava/nio/ByteBuffer;",
                 "(ID)Ljava/nio/ByteBuffer;", 8,
                 [](std::span<const Value> arguments, usize index)
                    -> Result<u64> {
                    auto value = double_argument(arguments, index);
                    if (!value) return std::unexpected(value.error());
                    return std::bit_cast<u64>(*value);
                 });

    const auto get_bits = [&registry](const char* name,
                                      const char* relative_descriptor,
                                      const char* absolute_descriptor,
                                      i32 byte_count,
                                      auto decode) {
        add(registry, "java/nio/ByteBuffer", name, relative_descriptor,
            [byte_count, decode](Machine& machine,
                                 std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                if (!buffer) return std::unexpected(buffer.error());
                auto state = require_buffer(machine, *buffer, byte_count, false);
                if (!state) return std::unexpected(state.error());
                auto little = buffer_little_endian(machine, *buffer);
                if (!little) return std::unexpected(little.error());
                auto bits = read_buffer_bits(machine, state->data,
                                             state->position, byte_count,
                                             *little);
                if (!bits) return std::unexpected(bits.error());
                auto advanced = set_int_field(machine, *buffer,
                                              kBufferPositionField,
                                              state->position + byte_count);
                if (!advanced) return std::unexpected(advanced.error());
                return std::optional<Value>(decode(*bits));
            });
        add(registry, "java/nio/ByteBuffer", name, absolute_descriptor,
            [byte_count, decode](Machine& machine,
                                 std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                auto index = int_argument(arguments, 1U);
                if (!buffer) return std::unexpected(buffer.error());
                if (!index) return std::unexpected(index.error());
                auto data = require_buffer_range(machine, *buffer, *index,
                                                 byte_count);
                if (!data) return std::unexpected(data.error());
                auto little = buffer_little_endian(machine, *buffer);
                if (!little) return std::unexpected(little.error());
                auto bits = read_buffer_bits(machine, *data, *index,
                                             byte_count, *little);
                if (!bits) return std::unexpected(bits.error());
                return std::optional<Value>(decode(*bits));
            });
    };
    get_bits("getChar", "()C", "(I)C", 2,
             [](u64 bits) { return Value::from_int(
                 static_cast<i32>(static_cast<u16>(bits))); });
    get_bits("getFloat", "()F", "(I)F", 4,
             [](u64 bits) { return Value::from_float(
                 std::bit_cast<float>(static_cast<u32>(bits))); });
    get_bits("getDouble", "()D", "(I)D", 8,
             [](u64 bits) { return Value::from_double(std::bit_cast<double>(bits)); });

    add(registry, "java/nio/ByteBuffer", "order", "()Ljava/nio/ByteOrder;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto order = int_field(machine, *buffer, kBufferOrderField);
            if (!order) return std::unexpected(order.error());
            auto value = byte_order_constant(
                machine, *order == kLittleEndian ? "LITTLE_ENDIAN" : "BIG_ENDIAN");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
    add(registry, "java/nio/ByteBuffer", "order",
        "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto order = reference_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!order) return std::unexpected(order.error());
            auto kind = int_field(machine, *order, kByteOrderKindField);
            if (!kind) return std::unexpected(kind.error());
            if (*kind != kBigEndian && *kind != kLittleEndian) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "unknown ByteOrder value");
            }
            auto stored = set_int_field(machine, *buffer, kBufferOrderField,
                                        *kind);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*buffer));
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
    add(registry, "java/nio/ByteBuffer", "array", "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto data = reference_field(machine, *buffer, kBufferDataField);
            if (!data) return std::unexpected(data.error());
            return std::optional<Value>(Value::from_reference(*data));
        });
    add(registry, "java/nio/ByteBuffer", "hasArray", "()Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, "java/nio/ByteBuffer", "arrayOffset", "()I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            return std::optional<Value>(Value::from_int(0));
        });
    const auto register_false_property = [&registry](const char* name) {
        add(registry, "java/nio/ByteBuffer", name, "()Z",
            [](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto buffer = receiver(arguments);
                if (!buffer) return std::unexpected(buffer.error());
                return std::optional<Value>(Value::from_int(0));
            });
    };
    register_false_property("isDirect");
    register_false_property("isReadOnly");
    add(registry, "java/nio/ByteBuffer", "capacity", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto data = reference_field(machine, *buffer, kBufferDataField);
            if (!data) return std::unexpected(data.error());
            auto length = machine.heap().array_length(*data);
            if (!length) return std::unexpected(length.error());
            if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "ByteBuffer capacity is too large");
            }
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(*length)));
        });
    add(registry, "java/nio/ByteBuffer", "limit", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            if (!limit) return std::unexpected(limit.error());
            return std::optional<Value>(Value::from_int(*limit));
        });
    add(registry, "java/nio/ByteBuffer", "limit",
        "(I)Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            auto requested = int_argument(arguments, 1U);
            if (!buffer) return std::unexpected(buffer.error());
            if (!requested) return std::unexpected(requested.error());
            auto data = reference_field(machine, *buffer, kBufferDataField);
            if (!data) return std::unexpected(data.error());
            auto length = machine.heap().array_length(*data);
            if (!length) return std::unexpected(length.error());
            if (*requested < 0 ||
                static_cast<usize>(*requested) > *length) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "ByteBuffer limit is outside capacity");
            }
            auto stored = set_int_field(machine, *buffer,
                                        kBufferLimitField, *requested);
            if (!stored) return std::unexpected(stored.error());
            auto position = int_field(machine, *buffer,
                                      kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            if (*position > *requested) {
                auto moved = set_int_field(machine, *buffer,
                                           kBufferPositionField, *requested);
                if (!moved) return std::unexpected(moved.error());
            }
            auto mark = int_field(machine, *buffer, kBufferMarkField);
            if (!mark) return std::unexpected(mark.error());
            if (*mark > *requested) {
                auto discarded = discard_buffer_mark(machine, *buffer);
                if (!discarded) return std::unexpected(discarded.error());
            }
            return std::optional<Value>(Value::from_reference(*buffer));
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
            auto mark = int_field(machine, *buffer, kBufferMarkField);
            if (!mark) return std::unexpected(mark.error());
            if (*mark > *position) {
                auto discarded = discard_buffer_mark(machine, *buffer);
                if (!discarded) return std::unexpected(discarded.error());
            }
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
    add(registry, "java/nio/ByteBuffer", "hasRemaining", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto limit = int_field(machine, *buffer, kBufferLimitField);
            auto position = int_field(machine, *buffer,
                                      kBufferPositionField);
            if (!limit) return std::unexpected(limit.error());
            if (!position) return std::unexpected(position.error());
            return std::optional<Value>(Value::from_int(
                *position < *limit ? 1 : 0));
        });
    add(registry, "java/nio/ByteBuffer", "clear", "()Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto data = reference_field(machine, *buffer, kBufferDataField);
            if (!data) return std::unexpected(data.error());
            auto length = machine.heap().array_length(*data);
            if (!length) return std::unexpected(length.error());
            if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "ByteBuffer capacity is too large");
            }
            auto limit = set_int_field(machine, *buffer, kBufferLimitField,
                                       static_cast<i32>(*length));
            auto position = set_int_field(machine, *buffer,
                                          kBufferPositionField, 0);
            auto mark = discard_buffer_mark(machine, *buffer);
            if (!limit) return std::unexpected(limit.error());
            if (!position) return std::unexpected(position.error());
            if (!mark) return std::unexpected(mark.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "flip", "()Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto position = int_field(machine, *buffer,
                                      kBufferPositionField);
            if (!position) return std::unexpected(position.error());
            auto limit = set_int_field(machine, *buffer, kBufferLimitField,
                                       *position);
            auto reset = set_int_field(machine, *buffer,
                                       kBufferPositionField, 0);
            auto mark = discard_buffer_mark(machine, *buffer);
            if (!limit) return std::unexpected(limit.error());
            if (!reset) return std::unexpected(reset.error());
            if (!mark) return std::unexpected(mark.error());
            return std::optional<Value>(Value::from_reference(*buffer));
        });
    add(registry, "java/nio/ByteBuffer", "rewind", "()Ljava/nio/ByteBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto buffer = receiver(arguments);
            if (!buffer) return std::unexpected(buffer.error());
            auto reset = set_int_field(machine, *buffer,
                                       kBufferPositionField, 0);
            auto mark = discard_buffer_mark(machine, *buffer);
            if (!reset) return std::unexpected(reset.error());
            if (!mark) return std::unexpected(mark.error());
            return std::optional<Value>(Value::from_reference(*buffer));
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
    register_byte_order(registry);
    register_buffer_base(registry);
    register_byte_buffer(registry);
    register_secure_random(registry);
    register_time_unit(registry);
}

} // namespace phoneme::vm
