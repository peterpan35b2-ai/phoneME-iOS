#include "Jdk8CompatNativesParts.hpp"

#include <array>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kArrayListDataField = 0U;
constexpr usize kArrayListSizeField = 1U;
constexpr usize kStreamValuesField = 0U;
constexpr usize kIntStreamValuesField = 0U;

[[nodiscard]] Result<ObjectRef> allocate_int_array(Machine& machine,
                                                    usize length) {
    return machine.heap().allocate_array("[I", length, Value::from_int(0));
}

void register_stream_constructors(NativeMethodRegistry& registry) {
    add(registry, "java/util/stream/Stream", "<init>",
        "([Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            auto values = reference_argument(arguments, 1U);
            if (!stream) return std::unexpected(stream.error());
            if (!values) return std::unexpected(values.error());
            auto stored = set_reference_field(machine, *stream,
                                              kStreamValuesField, *values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/stream/IntStream", "<init>", "([I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            auto values = reference_argument(arguments, 1U);
            if (!stream) return std::unexpected(stream.error());
            if (!values) return std::unexpected(values.error());
            auto stored = set_reference_field(machine, *stream,
                                              kIntStreamValuesField, *values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
}

void register_collection_stream(NativeMethodRegistry& registry) {
    add(registry, "java/util/Collection", "stream",
        "()Ljava/util/stream/Stream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto collection = receiver(arguments);
            if (!collection) return std::unexpected(collection.error());
            auto array_result = invoke_checked(
                machine, *collection, "java/util/Collection", "toArray",
                "()[Ljava/lang/Object;");
            if (!array_result) return std::unexpected(array_result.error());
            if (!array_result->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Collection.toArray returned no value");
            }
            auto values = array_result->value().as_reference();
            if (!values) return std::unexpected(values.error());
            if (values->is_null()) {
                return fail(ErrorCode::invalid_state,
                            "Collection.toArray returned null");
            }
            auto values_root = machine.pin_native_root(*values);
            if (!values_root) return std::unexpected(values_root.error());
            auto stream = new_instance(machine, "java/util/stream/Stream");
            if (!stream) return std::unexpected(stream.error());
            auto stored = set_reference_field(machine, *stream,
                                              kStreamValuesField, *values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
}

void register_array_list_stream(NativeMethodRegistry& registry) {
    add(registry, "java/util/ArrayList", "stream",
        "()Ljava/util/stream/Stream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            auto data = reference_field(machine, *list,
                                        kArrayListDataField);
            auto size = int_field(machine, *list, kArrayListSizeField);
            if (!data) return std::unexpected(data.error());
            if (!size) return std::unexpected(size.error());
            if (data->is_null() || *size < 0) {
                return fail(ErrorCode::invalid_state,
                            "ArrayList storage is invalid");
            }
            auto values = allocate_object_array(machine,
                                                static_cast<usize>(*size));
            if (!values) return std::unexpected(values.error());
            auto values_root = machine.pin_native_root(*values);
            if (!values_root) return std::unexpected(values_root.error());
            for (i32 index = 0; index < *size; ++index) {
                auto value = machine.heap().element(
                    *data, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(
                    *values, static_cast<usize>(index), *value);
                if (!stored) return std::unexpected(stored.error());
            }
            auto stream = new_instance(machine, "java/util/stream/Stream");
            if (!stream) return std::unexpected(stream.error());
            auto stored = set_reference_field(machine, *stream,
                                              kStreamValuesField, *values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
}

void register_map_to_int(NativeMethodRegistry& registry) {
    add(registry, "java/util/stream/Stream", "mapToInt",
        "(Ljava/util/function/ToIntFunction;)Ljava/util/stream/IntStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            auto mapper = reference_argument(arguments, 1U);
            if (!stream) return std::unexpected(stream.error());
            if (!mapper) return std::unexpected(mapper.error());
            auto values = reference_field(machine, *stream,
                                          kStreamValuesField);
            if (!values) return std::unexpected(values.error());
            if (values->is_null()) {
                return fail(ErrorCode::invalid_state,
                            "Stream values are not initialized");
            }
            auto length = machine.heap().array_length(*values);
            if (!length) return std::unexpected(length.error());
            auto mapped_values = allocate_int_array(machine, *length);
            if (!mapped_values) {
                return std::unexpected(mapped_values.error());
            }
            auto mapped_root = machine.pin_native_root(*mapped_values);
            if (!mapped_root) return std::unexpected(mapped_root.error());
            for (usize index = 0U; index < *length; ++index) {
                auto value = machine.heap().element(*values, index);
                if (!value) return std::unexpected(value.error());
                auto reference = value->as_reference();
                if (!reference) return std::unexpected(reference.error());
                const Value callback_argument =
                    Value::from_reference(*reference);
                auto mapped = invoke_checked(
                    machine, *mapper, "java/util/function/ToIntFunction",
                    "applyAsInt", "(Ljava/lang/Object;)I",
                    std::span<const Value>(&callback_argument, 1U));
                if (!mapped) return std::unexpected(mapped.error());
                if (!mapped->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "ToIntFunction.applyAsInt returned no value");
                }
                auto integer = mapped->value().as_int();
                if (!integer) return std::unexpected(integer.error());
                auto stored = machine.heap().set_element(
                    *mapped_values, index, Value::from_int(*integer));
                if (!stored) return std::unexpected(stored.error());
            }
            auto int_stream = new_instance(machine,
                                           "java/util/stream/IntStream");
            if (!int_stream) return std::unexpected(int_stream.error());
            auto stored = set_reference_field(machine, *int_stream,
                                              kIntStreamValuesField,
                                              *mapped_values);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*int_stream));
        });
}

void register_int_stream_to_array(NativeMethodRegistry& registry) {
    add(registry, "java/util/stream/IntStream", "toArray", "()[I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            if (!stream) return std::unexpected(stream.error());
            auto values = reference_field(machine, *stream,
                                          kIntStreamValuesField);
            if (!values) return std::unexpected(values.error());
            if (values->is_null()) {
                return fail(ErrorCode::invalid_state,
                            "IntStream values are not initialized");
            }
            auto length = machine.heap().array_length(*values);
            if (!length) return std::unexpected(length.error());
            auto copy = allocate_int_array(machine, *length);
            if (!copy) return std::unexpected(copy.error());
            for (usize index = 0U; index < *length; ++index) {
                auto value = machine.heap().element(*values, index);
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(*copy, index, *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*copy));
        });
}

} // namespace

void register_jdk8_stream_natives(NativeMethodRegistry& registry) {
    register_stream_constructors(registry);
    register_collection_stream(registry);
    register_array_list_stream(registry);
    register_map_to_int(registry);
    register_int_stream_to_array(registry);
}

} // namespace phoneme::vm
