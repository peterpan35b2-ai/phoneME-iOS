#include "UtilNatives.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr usize kVectorDataField = 0;
constexpr usize kVectorCountField = 1;
constexpr usize kVectorIncrementField = 2;
constexpr usize kHashtableKeysField = 0;
constexpr usize kHashtableValuesField = 1;
constexpr usize kHashtableCountField = 2;
constexpr usize kEnumerationArrayField = 0;
constexpr usize kEnumerationIndexField = 1;
constexpr usize kEnumerationSizeField = 2;
constexpr usize kRandomSeedField = 0;
constexpr usize kDateMillisField = 0;
constexpr u64 kRandomMultiplier = 0x5DEECE66DULL;
constexpr u64 kRandomAddend = 0xBULL;
constexpr u64 kRandomMask = (1ULL << 48U) - 1ULL;

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

[[nodiscard]] Result<ObjectRef> receiver(
    std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "java.util method has no receiver");
    }
    auto reference = arguments.front().as_reference();
    if (!reference || reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "java.util receiver is null");
    }
    return *reference;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool nullable = false) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "java.util reference argument is missing");
    }
    auto reference = arguments[index].as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (!nullable && reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "java.util reference argument is null");
    }
    return *reference;
}

[[nodiscard]] Result<i32> int_argument(
    std::span<const Value> arguments,
    usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "java.util int argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<i64> long_argument(
    std::span<const Value> arguments,
    usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "java.util long argument is missing");
    }
    return arguments[index].as_long();
}

[[nodiscard]] i64 current_time_millis() noexcept {
    return static_cast<i64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

[[nodiscard]] Result<i64> delayed_time(i64 delay) {
    if (delay < 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Timer delay cannot be negative");
    }
    const i64 now = current_time_millis();
    if (delay > std::numeric_limits<i64>::max() - now) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Timer delay overflows absolute time");
    }
    return now + delay;
}

[[nodiscard]] Result<i32> int_field(Machine& machine,
                                    ObjectRef object,
                                    usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<i64> long_field(Machine& machine,
                                     ObjectRef object,
                                     usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_long();
}

[[nodiscard]] Result<ObjectRef> reference_field(Machine& machine,
                                                 ObjectRef object,
                                                 usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Status set_int_field(Machine& machine,
                                   ObjectRef object,
                                   usize index,
                                   i32 value) {
    return machine.heap().set_field(object, index, Value::from_int(value));
}

[[nodiscard]] Status set_long_field(Machine& machine,
                                    ObjectRef object,
                                    usize index,
                                    i64 value) {
    return machine.heap().set_field(object, index, Value::from_long(value));
}

[[nodiscard]] Status set_reference_field(Machine& machine,
                                         ObjectRef object,
                                         usize index,
                                         ObjectRef value) {
    return machine.heap().set_field(object, index,
                                    Value::from_reference(value));
}

[[nodiscard]] Result<bool> values_equal(Machine& machine,
                                        ObjectRef left,
                                        ObjectRef right) {
    if (left == right) return true;
    if (left.is_null() || right.is_null()) return false;
    auto left_class = machine.heap().class_name(left);
    auto right_class = machine.heap().class_name(right);
    if (!left_class || !right_class) {
        return fail(ErrorCode::invalid_argument,
                    "collection contains a stale reference");
    }
    if (*left_class == *right_class &&
        (*left_class == "java/lang/String" ||
         *left_class == "java/lang/StringBuilder" ||
         *left_class == "java/lang/StringBuffer")) {
        auto left_text = machine.heap().string_value(left);
        auto right_text = machine.heap().string_value(right);
        if (!left_text || !right_text) {
            return fail(ErrorCode::invalid_state,
                        "text object has no payload");
        }
        return *left_text == *right_text;
    }
    if (*left_class == *right_class &&
        (*left_class == "java/lang/Boolean" ||
         *left_class == "java/lang/Byte" ||
         *left_class == "java/lang/Short" ||
         *left_class == "java/lang/Integer" ||
         *left_class == "java/lang/Long" ||
         *left_class == "java/lang/Character" ||
         *left_class == "java/lang/Float" ||
         *left_class == "java/lang/Double")) {
        auto left_value = machine.heap().field(left, 0);
        auto right_value = machine.heap().field(right, 0);
        if (!left_value || !right_value ||
            left_value->kind() != right_value->kind()) {
            return false;
        }
        switch (left_value->kind()) {
        case ValueKind::int32: {
            auto left_number = left_value->as_int();
            auto right_number = right_value->as_int();
            return left_number && right_number &&
                   *left_number == *right_number;
        }
        case ValueKind::int64: {
            auto left_number = left_value->as_long();
            auto right_number = right_value->as_long();
            return left_number && right_number &&
                   *left_number == *right_number;
        }
        case ValueKind::float32: {
            auto left_number = left_value->as_float();
            auto right_number = right_value->as_float();
            return left_number && right_number &&
                   *left_number == *right_number;
        }
        case ValueKind::float64: {
            auto left_number = left_value->as_double();
            auto right_number = right_value->as_double();
            return left_number && right_number &&
                   *left_number == *right_number;
        }
        default:
            return false;
        }
    }
    const Value argument = Value::from_reference(right);
    auto equality = machine.invoke_instance(
        left,
        *left_class,
        "equals",
        "(Ljava/lang/Object;)Z",
        std::span<const Value>(&argument, 1U));
    if (!equality) return std::unexpected(equality.error());
    if (equality->throwable.has_value()) {
        auto throwable_class = machine.heap().class_name(*equality->throwable);
        if (!throwable_class) return std::unexpected(throwable_class.error());
        return fail_java(*throwable_class,
                         "collection element equals() threw an exception");
    }
    if (!equality->return_value.has_value()) {
        return fail(ErrorCode::internal_error,
                    "Object.equals returned without a boolean value");
    }
    auto equal = equality->return_value->as_int();
    if (!equal) return std::unexpected(equal.error());
    return *equal != 0;
}

[[nodiscard]] Result<ObjectRef> allocate_object_array(Machine& machine,
                                                       usize length) {
    return machine.heap().allocate_array(
        "[Ljava/lang/Object;", length, Value::from_reference({}));
}

[[nodiscard]] Result<ObjectRef> make_enumeration(
    Machine& machine,
    std::span<const ObjectRef> values) {
    auto array = allocate_object_array(machine, values.size());
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < values.size(); ++index) {
        auto stored = machine.heap().set_element(
            *array, index, Value::from_reference(values[index]));
        if (!stored) return std::unexpected(stored.error());
    }
    auto enumeration = machine.class_states().allocate_instance(
        machine.heap(), "java/util/ArrayEnumeration");
    if (!enumeration) return std::unexpected(enumeration.error());
    auto array_stored = set_reference_field(machine, *enumeration,
                                            kEnumerationArrayField, *array);
    auto index_stored = set_int_field(machine, *enumeration,
                                      kEnumerationIndexField, 0);
    auto size_stored = set_int_field(
        machine, *enumeration, kEnumerationSizeField,
        static_cast<i32>(values.size()));
    if (!array_stored) return std::unexpected(array_stored.error());
    if (!index_stored) return std::unexpected(index_stored.error());
    if (!size_stored) return std::unexpected(size_stored.error());
    return *enumeration;
}

[[nodiscard]] Status initialize_vector(Machine& machine,
                                       ObjectRef object,
                                       i32 capacity,
                                       i32 increment) {
    if (capacity < 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Vector capacity is negative");
    }
    auto data = allocate_object_array(machine,
                                      static_cast<usize>(capacity));
    if (!data) return std::unexpected(data.error());
    auto data_stored = set_reference_field(machine, object,
                                           kVectorDataField, *data);
    auto count_stored = set_int_field(machine, object,
                                      kVectorCountField, 0);
    auto increment_stored = set_int_field(machine, object,
                                          kVectorIncrementField,
                                          increment);
    if (!data_stored) return data_stored;
    if (!count_stored) return count_stored;
    return increment_stored;
}

[[nodiscard]] Result<ObjectRef> vector_data(Machine& machine,
                                            ObjectRef vector) {
    auto data = reference_field(machine, vector, kVectorDataField);
    if (!data) return std::unexpected(data.error());
    if (data->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "Vector has not been initialized");
    }
    return *data;
}

[[nodiscard]] Status ensure_vector_capacity(Machine& machine,
                                            ObjectRef vector,
                                            i32 minimum) {
    auto data = vector_data(machine, vector);
    if (!data) return std::unexpected(data.error());
    auto capacity = machine.heap().array_length(*data);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    auto increment = int_field(machine, vector, kVectorIncrementField);
    if (!increment) return std::unexpected(increment.error());
    usize new_capacity = *increment > 0
        ? *capacity + static_cast<usize>(*increment)
        : (*capacity == 0U ? 1U : *capacity * 2U);
    if (new_capacity < static_cast<usize>(minimum)) {
        new_capacity = static_cast<usize>(minimum);
    }
    auto replacement = allocate_object_array(machine, new_capacity);
    if (!replacement) return std::unexpected(replacement.error());
    auto count = int_field(machine, vector, kVectorCountField);
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*data,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(
            *replacement, static_cast<usize>(index), *value);
        if (!stored) return stored;
    }
    return set_reference_field(machine, vector,
                               kVectorDataField, *replacement);
}

[[nodiscard]] Result<i32> vector_index_of(Machine& machine,
                                          ObjectRef vector,
                                          ObjectRef target,
                                          i32 start,
                                          bool reverse) {
    auto count = int_field(machine, vector, kVectorCountField);
    auto data = vector_data(machine, vector);
    if (!count) return std::unexpected(count.error());
    if (!data) return std::unexpected(data.error());
    if (!reverse) {
        i32 index = start < 0 ? 0 : start;
        for (; index < *count; ++index) {
            auto value = machine.heap().element(*data,
                                                static_cast<usize>(index));
            if (!value) return std::unexpected(value.error());
            auto reference = value->as_reference();
            if (!reference) return std::unexpected(reference.error());
            auto equal = values_equal(machine, *reference, target);
            if (!equal) return std::unexpected(equal.error());
            if (*equal) return index;
        }
        return -1;
    }
    i32 index = std::min(start, *count - 1);
    for (; index >= 0; --index) {
        auto value = machine.heap().element(*data,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto reference = value->as_reference();
        if (!reference) return std::unexpected(reference.error());
        auto equal = values_equal(machine, *reference, target);
        if (!equal) return std::unexpected(equal.error());
        if (*equal) return index;
    }
    return -1;
}

[[nodiscard]] Status remove_vector_index(Machine& machine,
                                         ObjectRef vector,
                                         i32 index) {
    auto count = int_field(machine, vector, kVectorCountField);
    auto data = vector_data(machine, vector);
    if (!count) return std::unexpected(count.error());
    if (!data) return std::unexpected(data.error());
    if (index < 0 || index >= *count) {
        return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                         "Vector index is out of range");
    }
    for (i32 current = index; current + 1 < *count; ++current) {
        auto next = machine.heap().element(
            *data, static_cast<usize>(current + 1));
        if (!next) return std::unexpected(next.error());
        auto stored = machine.heap().set_element(
            *data, static_cast<usize>(current), *next);
        if (!stored) return stored;
    }
    auto cleared = machine.heap().set_element(
        *data, static_cast<usize>(*count - 1), Value::from_reference({}));
    if (!cleared) return cleared;
    return set_int_field(machine, vector, kVectorCountField, *count - 1);
}

void register_enumeration(NativeMethodRegistry& registry) {
    add(registry, "java/util/ArrayEnumeration", "hasMoreElements", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto index = int_field(machine, *object, kEnumerationIndexField);
            auto size = int_field(machine, *object, kEnumerationSizeField);
            if (!index || !size)
                return fail(ErrorCode::invalid_state,
                            "Enumeration state is invalid");
            return std::optional<Value>(Value::from_int(
                *index < *size ? 1 : 0));
        });
    add(registry, "java/util/ArrayEnumeration", "nextElement",
        "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto index = int_field(machine, *object, kEnumerationIndexField);
            auto size = int_field(machine, *object, kEnumerationSizeField);
            auto array = reference_field(machine, *object,
                                         kEnumerationArrayField);
            if (!index || !size || !array)
                return fail(ErrorCode::invalid_state,
                            "Enumeration state is invalid");
            if (*index >= *size) {
                return fail_java("java/util/NoSuchElementException",
                                 "Enumeration has no more elements");
            }
            auto value = machine.heap().element(
                *array, static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            auto updated = set_int_field(machine, *object,
                                         kEnumerationIndexField,
                                         *index + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value>(*value);
        });
}

void register_vector(NativeMethodRegistry& registry) {
    add(registry, "java/util/Vector", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto initialized = initialize_vector(machine, *object, 10, 0);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto capacity = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!capacity) return std::unexpected(capacity.error());
            auto initialized = initialize_vector(machine, *object,
                                                 *capacity, 0);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "<init>", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto capacity = arguments[1].as_int();
            auto increment = arguments[2].as_int();
            if (!object) return std::unexpected(object.error());
            if (!capacity || !increment)
                return fail(ErrorCode::invalid_argument,
                            "Vector constructor arguments are invalid");
            auto initialized = initialize_vector(machine, *object,
                                                 *capacity, *increment);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Stack", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto initialized = initialize_vector(machine, *object, 10, 0);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });

    add(registry, "java/util/Vector", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, "java/util/Vector", "capacity", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto data = vector_data(machine, *object);
            if (!data) return std::unexpected(data.error());
            auto length = machine.heap().array_length(*data);
            if (!length) return std::unexpected(length.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(*length)));
        });
    add(registry, "java/util/Vector", "ensureCapacity", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto minimum = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!minimum) return std::unexpected(minimum.error());
            if (*minimum > 0) {
                auto ensured = ensure_vector_capacity(
                    machine, *object, *minimum);
                if (!ensured) return std::unexpected(ensured.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "trimToSize", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kVectorCountField);
            auto data = vector_data(machine, *object);
            if (!count) return std::unexpected(count.error());
            if (!data) return std::unexpected(data.error());
            auto capacity = machine.heap().array_length(*data);
            if (!capacity) return std::unexpected(capacity.error());
            if (*capacity == static_cast<usize>(*count)) {
                return std::optional<Value> {};
            }
            auto replacement = allocate_object_array(
                machine, static_cast<usize>(*count));
            if (!replacement) return std::unexpected(replacement.error());
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(
                    *data, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(
                    *replacement, static_cast<usize>(index), *value);
                if (!stored) return std::unexpected(stored.error());
            }
            auto stored = set_reference_field(
                machine, *object, kVectorDataField, *replacement);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "setSize", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto requested = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!requested) return std::unexpected(requested.error());
            if (*requested < 0) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "Vector size is negative");
            }
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            if (*requested > *count) {
                auto ensured = ensure_vector_capacity(
                    machine, *object, *requested);
                if (!ensured) return std::unexpected(ensured.error());
            } else if (*requested < *count) {
                auto data = vector_data(machine, *object);
                if (!data) return std::unexpected(data.error());
                for (i32 index = *requested; index < *count; ++index) {
                    auto cleared = machine.heap().set_element(
                        *data, static_cast<usize>(index),
                        Value::from_reference({}));
                    if (!cleared) return std::unexpected(cleared.error());
                }
            }
            auto updated = set_int_field(
                machine, *object, kVectorCountField, *requested);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "isEmpty", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count == 0 ? 1 : 0));
        });
    add(registry, "java/util/Vector", "copyInto",
        "([Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (arguments.size() < 2U) {
                return fail(ErrorCode::invalid_argument,
                            "Vector.copyInto destination is missing");
            }
            auto destination = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!destination || destination->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Vector.copyInto destination is null");
            }
            auto destination_class = machine.heap().class_name(*destination);
            auto destination_length = machine.heap().array_length(*destination);
            auto count = int_field(machine, *object, kVectorCountField);
            auto data = vector_data(machine, *object);
            if (!destination_class || !destination_length || !count || !data) {
                return fail(ErrorCode::invalid_state,
                            "Vector.copyInto state is invalid");
            }
            if (!destination_class->starts_with("[L") &&
                !destination_class->starts_with("[[")) {
                return fail_java("java/lang/ArrayStoreException",
                                 "Vector.copyInto requires a reference array");
            }
            if (*destination_length < static_cast<usize>(*count)) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "Vector.copyInto destination is too small");
            }
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(
                    *data, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(
                    *destination, static_cast<usize>(index), *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    const auto add_index_search = [&registry](const char* name,
                                              bool reverse,
                                              bool with_start) {
        add(registry, "java/util/Vector", name,
            with_start ? "(Ljava/lang/Object;I)I"
                       : "(Ljava/lang/Object;)I",
            [reverse, with_start](Machine& machine,
                                  std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto target = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!target) return std::unexpected(target.error());
                i32 start = reverse ? std::numeric_limits<i32>::max() : 0;
                if (with_start) {
                    auto parsed = arguments[2].as_int();
                    if (!parsed) return std::unexpected(parsed.error());
                    start = *parsed;
                }
                auto index = vector_index_of(machine, *object, *target,
                                             start, reverse);
                if (!index) return std::unexpected(index.error());
                return std::optional<Value>(Value::from_int(*index));
            });
    };
    add_index_search("indexOf", false, false);
    add_index_search("indexOf", false, true);
    add_index_search("lastIndexOf", true, false);
    add_index_search("lastIndexOf", true, true);
    add(registry, "java/util/Vector", "contains",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto target = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!target) return std::unexpected(target.error());
            auto index = vector_index_of(machine, *object, *target, 0, false);
            if (!index) return std::unexpected(index.error());
            return std::optional<Value>(Value::from_int(*index >= 0 ? 1 : 0));
        });

    add(registry, "java/util/Vector", "elementAt",
        "(I)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto index = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            auto count = int_field(machine, *object, kVectorCountField);
            auto data = vector_data(machine, *object);
            if (!count || !data)
                return fail(ErrorCode::invalid_state,
                            "Vector state is invalid");
            if (*index < 0 || *index >= *count) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "Vector index is out of range");
            }
            auto value = machine.heap().element(
                *data, static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
    const auto add_edge = [&registry](const char* name, bool last) {
        add(registry, "java/util/Vector", name, "()Ljava/lang/Object;",
            [last](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto count = int_field(machine, *object, kVectorCountField);
                auto data = vector_data(machine, *object);
                if (!count || !data)
                    return fail(ErrorCode::invalid_state,
                                "Vector state is invalid");
                if (*count == 0) {
                    return fail_java("java/util/NoSuchElementException",
                                     "Vector is empty");
                }
                const usize index = last
                    ? static_cast<usize>(*count - 1) : 0U;
                auto value = machine.heap().element(*data, index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(*value);
            });
    };
    add_edge("firstElement", false);
    add_edge("lastElement", true);

    add(registry, "java/util/Vector", "setElementAt",
        "(Ljava/lang/Object;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = arguments[1].as_reference();
            auto index = arguments[2].as_int();
            if (!object || !value || !index)
                return fail(ErrorCode::invalid_argument,
                            "Vector.setElementAt arguments are invalid");
            auto count = int_field(machine, *object, kVectorCountField);
            auto data = vector_data(machine, *object);
            if (!count || !data)
                return fail(ErrorCode::invalid_state,
                            "Vector state is invalid");
            if (*index < 0 || *index >= *count) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "Vector index is out of range");
            }
            auto stored = machine.heap().set_element(
                *data, static_cast<usize>(*index),
                Value::from_reference(*value));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "removeElementAt", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto index = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            auto removed = remove_vector_index(machine, *object, *index);
            if (!removed) return std::unexpected(removed.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "insertElementAt",
        "(Ljava/lang/Object;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = arguments[1].as_reference();
            auto index = arguments[2].as_int();
            if (!object || !value || !index)
                return fail(ErrorCode::invalid_argument,
                            "Vector.insertElementAt arguments are invalid");
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            if (*index < 0 || *index > *count) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "Vector insertion index is out of range");
            }
            auto capacity = ensure_vector_capacity(machine, *object,
                                                   *count + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto data = vector_data(machine, *object);
            if (!data) return std::unexpected(data.error());
            for (i32 current = *count; current > *index; --current) {
                auto previous = machine.heap().element(
                    *data, static_cast<usize>(current - 1));
                if (!previous) return std::unexpected(previous.error());
                auto stored = machine.heap().set_element(
                    *data, static_cast<usize>(current), *previous);
                if (!stored) return std::unexpected(stored.error());
            }
            auto stored = machine.heap().set_element(
                *data, static_cast<usize>(*index),
                Value::from_reference(*value));
            if (!stored) return std::unexpected(stored.error());
            auto updated = set_int_field(machine, *object,
                                         kVectorCountField, *count + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "addElement",
        "(Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            auto capacity = ensure_vector_capacity(machine, *object,
                                                   *count + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto data = vector_data(machine, *object);
            if (!data) return std::unexpected(data.error());
            auto stored = machine.heap().set_element(
                *data, static_cast<usize>(*count),
                Value::from_reference(*value));
            if (!stored) return std::unexpected(stored.error());
            auto updated = set_int_field(machine, *object,
                                         kVectorCountField, *count + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "removeElement",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto target = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!target) return std::unexpected(target.error());
            auto index = vector_index_of(machine, *object, *target, 0, false);
            if (!index) return std::unexpected(index.error());
            if (*index < 0) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto removed = remove_vector_index(machine, *object, *index);
            if (!removed) return std::unexpected(removed.error());
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, "java/util/Vector", "removeAllElements", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kVectorCountField);
            auto data = vector_data(machine, *object);
            if (!count || !data)
                return fail(ErrorCode::invalid_state,
                            "Vector state is invalid");
            for (i32 index = 0; index < *count; ++index) {
                auto cleared = machine.heap().set_element(
                    *data, static_cast<usize>(index),
                    Value::from_reference({}));
                if (!cleared) return std::unexpected(cleared.error());
            }
            auto updated = set_int_field(machine, *object,
                                         kVectorCountField, 0);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Vector", "elements",
        "()Ljava/util/Enumeration;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kVectorCountField);
            auto data = vector_data(machine, *object);
            if (!count || !data)
                return fail(ErrorCode::invalid_state,
                            "Vector state is invalid");
            std::vector<ObjectRef> values;
            values.reserve(static_cast<usize>(*count));
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(
                    *data, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto reference = value->as_reference();
                if (!reference) return std::unexpected(reference.error());
                values.push_back(*reference);
            }
            auto enumeration = make_enumeration(machine, values);
            if (!enumeration) return std::unexpected(enumeration.error());
            return std::optional<Value>(Value::from_reference(*enumeration));
        });

    add(registry, "java/util/Stack", "push",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            auto capacity = ensure_vector_capacity(machine, *object,
                                                   *count + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto data = vector_data(machine, *object);
            if (!data) return std::unexpected(data.error());
            auto stored = machine.heap().set_element(
                *data, static_cast<usize>(*count),
                Value::from_reference(*value));
            if (!stored) return std::unexpected(stored.error());
            auto updated = set_int_field(machine, *object,
                                         kVectorCountField, *count + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
    const auto stack_edge = [&registry](const char* name, bool remove) {
        add(registry, "java/util/Stack", name, "()Ljava/lang/Object;",
            [remove](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto count = int_field(machine, *object, kVectorCountField);
                auto data = vector_data(machine, *object);
                if (!count || !data)
                    return fail(ErrorCode::invalid_state,
                                "Stack state is invalid");
                if (*count == 0) {
                    return fail_java("java/util/EmptyStackException",
                                     "Stack is empty");
                }
                const usize index = static_cast<usize>(*count - 1);
                auto value = machine.heap().element(*data, index);
                if (!value) return std::unexpected(value.error());
                if (remove) {
                    auto cleared = machine.heap().set_element(
                        *data, index, Value::from_reference({}));
                    auto updated = set_int_field(machine, *object,
                                                 kVectorCountField,
                                                 *count - 1);
                    if (!cleared) return std::unexpected(cleared.error());
                    if (!updated) return std::unexpected(updated.error());
                }
                return std::optional<Value>(*value);
            });
    };
    stack_edge("pop", true);
    stack_edge("peek", false);
    add(registry, "java/util/Stack", "empty", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count == 0 ? 1 : 0));
        });
    add(registry, "java/util/Stack", "search",
        "(Ljava/lang/Object;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto target = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!target) return std::unexpected(target.error());
            auto index = vector_index_of(machine, *object, *target,
                                         std::numeric_limits<i32>::max(),
                                         true);
            if (!index) return std::unexpected(index.error());
            if (*index < 0) return std::optional<Value>(Value::from_int(-1));
            auto count = int_field(machine, *object, kVectorCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count - *index));
        });
}

[[nodiscard]] Status initialize_hashtable(Machine& machine,
                                          ObjectRef object,
                                          i32 capacity) {
    if (capacity < 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Hashtable capacity is negative");
    }
    const usize actual = capacity == 0 ? 1U : static_cast<usize>(capacity);
    auto keys = allocate_object_array(machine, actual);
    auto values = allocate_object_array(machine, actual);
    if (!keys) return std::unexpected(keys.error());
    if (!values) return std::unexpected(values.error());
    auto keys_stored = set_reference_field(machine, object,
                                           kHashtableKeysField, *keys);
    auto values_stored = set_reference_field(machine, object,
                                             kHashtableValuesField, *values);
    auto count_stored = set_int_field(machine, object,
                                      kHashtableCountField, 0);
    if (!keys_stored) return keys_stored;
    if (!values_stored) return values_stored;
    return count_stored;
}

[[nodiscard]] Result<i32> hashtable_find(Machine& machine,
                                         ObjectRef table,
                                         ObjectRef key,
                                         bool search_values) {
    auto count = int_field(machine, table, kHashtableCountField);
    auto array = reference_field(machine, table,
        search_values ? kHashtableValuesField : kHashtableKeysField);
    if (!count || !array)
        return fail(ErrorCode::invalid_state,
                    "Hashtable state is invalid");
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*array,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto reference = value->as_reference();
        if (!reference) return std::unexpected(reference.error());
        auto equal = values_equal(machine, *reference, key);
        if (!equal) return std::unexpected(equal.error());
        if (*equal) return index;
    }
    return -1;
}

[[nodiscard]] Status ensure_hashtable_capacity(Machine& machine,
                                               ObjectRef table,
                                               i32 minimum) {
    auto keys = reference_field(machine, table, kHashtableKeysField);
    auto values = reference_field(machine, table, kHashtableValuesField);
    if (!keys || !values)
        return fail(ErrorCode::invalid_state,
                    "Hashtable arrays are missing");
    auto capacity = machine.heap().array_length(*keys);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    usize new_capacity = *capacity * 2U + 1U;
    if (new_capacity < static_cast<usize>(minimum))
        new_capacity = static_cast<usize>(minimum);
    auto new_keys = allocate_object_array(machine, new_capacity);
    auto new_values = allocate_object_array(machine, new_capacity);
    if (!new_keys) return std::unexpected(new_keys.error());
    if (!new_values) return std::unexpected(new_values.error());
    auto count = int_field(machine, table, kHashtableCountField);
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto key = machine.heap().element(*keys,
                                         static_cast<usize>(index));
        auto value = machine.heap().element(*values,
                                           static_cast<usize>(index));
        if (!key || !value)
            return fail(ErrorCode::invalid_state,
                        "Hashtable entry is invalid");
        auto key_stored = machine.heap().set_element(
            *new_keys, static_cast<usize>(index), *key);
        auto value_stored = machine.heap().set_element(
            *new_values, static_cast<usize>(index), *value);
        if (!key_stored) return key_stored;
        if (!value_stored) return value_stored;
    }
    auto keys_stored = set_reference_field(machine, table,
                                           kHashtableKeysField, *new_keys);
    if (!keys_stored) return keys_stored;
    return set_reference_field(machine, table,
                               kHashtableValuesField, *new_values);
}

void register_hashtable(NativeMethodRegistry& registry) {
    add(registry, "java/util/Hashtable", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto initialized = initialize_hashtable(machine, *object, 11);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Hashtable", "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto capacity = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!capacity) return std::unexpected(capacity.error());
            auto initialized = initialize_hashtable(machine, *object,
                                                    *capacity);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Hashtable", "<init>", "(IF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto capacity = arguments[1].as_int();
            auto load_factor = arguments[2].as_float();
            if (!object) return std::unexpected(object.error());
            if (!capacity || !load_factor)
                return fail(ErrorCode::invalid_argument,
                            "Hashtable constructor arguments are invalid");
            if (*load_factor <= 0.0F || std::isnan(*load_factor)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Hashtable load factor is invalid");
            }
            auto initialized = initialize_hashtable(machine, *object,
                                                    *capacity);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Hashtable", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kHashtableCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, "java/util/Hashtable", "isEmpty", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kHashtableCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count == 0 ? 1 : 0));
        });
    const auto contains = [&registry](const char* name, bool values) {
        add(registry, "java/util/Hashtable", name,
            "(Ljava/lang/Object;)Z",
            [values](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto target = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!target || target->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "Hashtable lookup key/value is null");
                }
                auto index = hashtable_find(machine, *object, *target, values);
                if (!index) return std::unexpected(index.error());
                return std::optional<Value>(Value::from_int(*index >= 0 ? 1 : 0));
            });
    };
    contains("containsKey", false);
    contains("contains", true);
    add(registry, "java/util/Hashtable", "get",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto key = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!key || key->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Hashtable key is null");
            }
            auto index = hashtable_find(machine, *object, *key, false);
            if (!index) return std::unexpected(index.error());
            if (*index < 0)
                return std::optional<Value>(Value::from_reference({}));
            auto values = reference_field(machine, *object,
                                          kHashtableValuesField);
            if (!values) return std::unexpected(values.error());
            auto value = machine.heap().element(
                *values, static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
    add(registry, "java/util/Hashtable", "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto key = arguments[1].as_reference();
            auto value = arguments[2].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!key || key->is_null() || !value || value->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Hashtable does not accept null keys or values");
            }
            auto existing = hashtable_find(machine, *object, *key, false);
            if (!existing) return std::unexpected(existing.error());
            auto values = reference_field(machine, *object,
                                          kHashtableValuesField);
            if (!values) return std::unexpected(values.error());
            if (*existing >= 0) {
                auto old = machine.heap().element(
                    *values, static_cast<usize>(*existing));
                if (!old) return std::unexpected(old.error());
                auto stored = machine.heap().set_element(
                    *values, static_cast<usize>(*existing),
                    Value::from_reference(*value));
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(*old);
            }
            auto count = int_field(machine, *object, kHashtableCountField);
            if (!count) return std::unexpected(count.error());
            auto capacity = ensure_hashtable_capacity(machine, *object,
                                                      *count + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto keys = reference_field(machine, *object,
                                        kHashtableKeysField);
            values = reference_field(machine, *object,
                                     kHashtableValuesField);
            if (!keys || !values)
                return fail(ErrorCode::invalid_state,
                            "Hashtable arrays are missing");
            auto key_stored = machine.heap().set_element(
                *keys, static_cast<usize>(*count),
                Value::from_reference(*key));
            auto value_stored = machine.heap().set_element(
                *values, static_cast<usize>(*count),
                Value::from_reference(*value));
            if (!key_stored) return std::unexpected(key_stored.error());
            if (!value_stored) return std::unexpected(value_stored.error());
            auto updated = set_int_field(machine, *object,
                                         kHashtableCountField, *count + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value>(Value::from_reference({}));
        });
    add(registry, "java/util/Hashtable", "remove",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto key = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!key || key->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Hashtable key is null");
            }
            auto index = hashtable_find(machine, *object, *key, false);
            if (!index) return std::unexpected(index.error());
            if (*index < 0)
                return std::optional<Value>(Value::from_reference({}));
            auto keys = reference_field(machine, *object,
                                        kHashtableKeysField);
            auto values = reference_field(machine, *object,
                                          kHashtableValuesField);
            auto count = int_field(machine, *object, kHashtableCountField);
            if (!keys || !values || !count)
                return fail(ErrorCode::invalid_state,
                            "Hashtable state is invalid");
            auto old = machine.heap().element(
                *values, static_cast<usize>(*index));
            if (!old) return std::unexpected(old.error());
            for (i32 current = *index; current + 1 < *count; ++current) {
                auto next_key = machine.heap().element(
                    *keys, static_cast<usize>(current + 1));
                auto next_value = machine.heap().element(
                    *values, static_cast<usize>(current + 1));
                if (!next_key || !next_value)
                    return fail(ErrorCode::invalid_state,
                                "Hashtable entry is invalid");
                auto key_stored = machine.heap().set_element(
                    *keys, static_cast<usize>(current), *next_key);
                auto value_stored = machine.heap().set_element(
                    *values, static_cast<usize>(current), *next_value);
                if (!key_stored) return std::unexpected(key_stored.error());
                if (!value_stored) return std::unexpected(value_stored.error());
            }
            auto key_cleared = machine.heap().set_element(
                *keys, static_cast<usize>(*count - 1),
                Value::from_reference({}));
            auto value_cleared = machine.heap().set_element(
                *values, static_cast<usize>(*count - 1),
                Value::from_reference({}));
            auto updated = set_int_field(machine, *object,
                                         kHashtableCountField, *count - 1);
            if (!key_cleared) return std::unexpected(key_cleared.error());
            if (!value_cleared) return std::unexpected(value_cleared.error());
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value>(*old);
        });
    add(registry, "java/util/Hashtable", "clear", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto keys = reference_field(machine, *object,
                                        kHashtableKeysField);
            auto values = reference_field(machine, *object,
                                          kHashtableValuesField);
            auto count = int_field(machine, *object, kHashtableCountField);
            if (!keys || !values || !count)
                return fail(ErrorCode::invalid_state,
                            "Hashtable state is invalid");
            for (i32 index = 0; index < *count; ++index) {
                auto key_cleared = machine.heap().set_element(
                    *keys, static_cast<usize>(index),
                    Value::from_reference({}));
                auto value_cleared = machine.heap().set_element(
                    *values, static_cast<usize>(index),
                    Value::from_reference({}));
                if (!key_cleared) return std::unexpected(key_cleared.error());
                if (!value_cleared) return std::unexpected(value_cleared.error());
            }
            auto updated = set_int_field(machine, *object,
                                         kHashtableCountField, 0);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    const auto enumeration = [&registry](const char* name, bool values) {
        add(registry, "java/util/Hashtable", name,
            "()Ljava/util/Enumeration;",
            [values](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto count = int_field(machine, *object,
                                       kHashtableCountField);
                auto array = reference_field(
                    machine, *object,
                    values ? kHashtableValuesField : kHashtableKeysField);
                if (!count || !array)
                    return fail(ErrorCode::invalid_state,
                                "Hashtable state is invalid");
                std::vector<ObjectRef> snapshot;
                snapshot.reserve(static_cast<usize>(*count));
                for (i32 index = 0; index < *count; ++index) {
                    auto value = machine.heap().element(
                        *array, static_cast<usize>(index));
                    if (!value) return std::unexpected(value.error());
                    auto reference = value->as_reference();
                    if (!reference) return std::unexpected(reference.error());
                    snapshot.push_back(*reference);
                }
                auto result = make_enumeration(machine, snapshot);
                if (!result) return std::unexpected(result.error());
                return std::optional<Value>(Value::from_reference(*result));
            });
    };
    enumeration("keys", false);
    enumeration("elements", true);
}

[[nodiscard]] Result<i32> random_next(Machine& machine,
                                      ObjectRef random,
                                      i32 bits) {
    if (bits < 1 || bits > 32) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Random.next bit count is out of range");
    }
    auto seed = long_field(machine, random, kRandomSeedField);
    if (!seed) return std::unexpected(seed.error());
    const u64 updated = (static_cast<u64>(*seed) * kRandomMultiplier +
                         kRandomAddend) & kRandomMask;
    auto stored = set_long_field(machine, random,
                                 kRandomSeedField,
                                 static_cast<i64>(updated));
    if (!stored) return std::unexpected(stored.error());
    return static_cast<i32>(static_cast<u32>(updated >> (48 - bits)));
}

void register_timer(NativeMethodRegistry& registry) {
    add(registry, "java/util/TimerTask", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            auto initialized = machine.timers().initialize_task(*task);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/TimerTask", "cancel", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            auto cancelled = machine.timers().cancel_task(*task);
            if (!cancelled) return std::unexpected(cancelled.error());
            return std::optional<Value>(Value::from_int(*cancelled ? 1 : 0));
        });
    add(registry, "java/util/TimerTask", "scheduledExecutionTime", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            auto scheduled = machine.timers().scheduled_execution_time(*task);
            if (!scheduled) return std::unexpected(scheduled.error());
            return std::optional<Value>(Value::from_long(*scheduled));
        });

    add(registry, "java/util/Timer", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto timer = receiver(arguments);
            if (!timer) return std::unexpected(timer.error());
            auto initialized = machine.timers().initialize_timer(*timer, false);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Timer", "<init>", "(Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto timer = receiver(arguments);
            auto daemon = int_argument(arguments, 1U);
            if (!timer) return std::unexpected(timer.error());
            if (!daemon) return std::unexpected(daemon.error());
            auto initialized =
                machine.timers().initialize_timer(*timer, *daemon != 0);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });

    const auto schedule_delay = [](bool repeating, bool fixed_rate) {
        return [repeating, fixed_rate](
            Machine& machine,
            std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto timer = receiver(arguments);
            auto task = reference_argument(arguments, 1U);
            auto delay = long_argument(arguments, 2U);
            if (!timer) return std::unexpected(timer.error());
            if (!task) return std::unexpected(task.error());
            if (!delay) return std::unexpected(delay.error());
            auto first = delayed_time(*delay);
            if (!first) return std::unexpected(first.error());
            i64 period = 0;
            if (repeating) {
                auto value = long_argument(arguments, 3U);
                if (!value) return std::unexpected(value.error());
                if (*value <= 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Timer period must be positive");
                }
                period = *value;
            }
            auto scheduled = machine.timers().schedule(
                *timer, *task, *first, period, fixed_rate);
            if (!scheduled) return std::unexpected(scheduled.error());
            return std::optional<Value> {};
        };
    };
    const auto schedule_date = [](bool repeating, bool fixed_rate) {
        return [repeating, fixed_rate](
            Machine& machine,
            std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto timer = receiver(arguments);
            auto task = reference_argument(arguments, 1U);
            auto date = reference_argument(arguments, 2U);
            if (!timer) return std::unexpected(timer.error());
            if (!task) return std::unexpected(task.error());
            if (!date) return std::unexpected(date.error());
            auto time = long_field(machine, *date, kDateMillisField);
            if (!time) return std::unexpected(time.error());
            if (*time < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Timer date cannot precede the epoch");
            }
            i64 period = 0;
            if (repeating) {
                auto value = long_argument(arguments, 3U);
                if (!value) return std::unexpected(value.error());
                if (*value <= 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Timer period must be positive");
                }
                period = *value;
            }
            const i64 first = std::max(*time, current_time_millis());
            auto scheduled = machine.timers().schedule(
                *timer, *task, first, period, fixed_rate);
            if (!scheduled) return std::unexpected(scheduled.error());
            return std::optional<Value> {};
        };
    };

    add(registry, "java/util/Timer", "schedule",
        "(Ljava/util/TimerTask;J)V", schedule_delay(false, false));
    add(registry, "java/util/Timer", "schedule",
        "(Ljava/util/TimerTask;JJ)V", schedule_delay(true, false));
    add(registry, "java/util/Timer", "schedule",
        "(Ljava/util/TimerTask;Ljava/util/Date;)V",
        schedule_date(false, false));
    add(registry, "java/util/Timer", "schedule",
        "(Ljava/util/TimerTask;Ljava/util/Date;J)V",
        schedule_date(true, false));
    add(registry, "java/util/Timer", "scheduleAtFixedRate",
        "(Ljava/util/TimerTask;JJ)V", schedule_delay(true, true));
    add(registry, "java/util/Timer", "scheduleAtFixedRate",
        "(Ljava/util/TimerTask;Ljava/util/Date;J)V",
        schedule_date(true, true));
    add(registry, "java/util/Timer", "cancel", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto timer = receiver(arguments);
            if (!timer) return std::unexpected(timer.error());
            auto cancelled = machine.timers().cancel_timer(*timer);
            if (!cancelled) return std::unexpected(cancelled.error());
            return std::optional<Value> {};
        });
}

void register_random(NativeMethodRegistry& registry) {
    const auto set_seed = [](Machine& machine,
                             ObjectRef object,
                             i64 seed) -> Status {
        const u64 scrambled = (static_cast<u64>(seed) ^
                               kRandomMultiplier) & kRandomMask;
        return set_long_field(machine, object, kRandomSeedField,
                              static_cast<i64>(scrambled));
    };
    add(registry, "java/util/Random", "<init>", "()V",
        [set_seed](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            const auto now = std::chrono::high_resolution_clock::now()
                                 .time_since_epoch().count();
            auto stored = set_seed(machine, *object,
                static_cast<i64>(now) ^ static_cast<i64>(object->bits));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Random", "<init>", "(J)V",
        [set_seed](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto seed = arguments[1].as_long();
            if (!object) return std::unexpected(object.error());
            if (!seed) return std::unexpected(seed.error());
            auto stored = set_seed(machine, *object, *seed);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Random", "setSeed", "(J)V",
        [set_seed](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto seed = arguments[1].as_long();
            if (!object) return std::unexpected(object.error());
            if (!seed) return std::unexpected(seed.error());
            auto stored = set_seed(machine, *object, *seed);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/Random", "next", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto bits = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!bits) return std::unexpected(bits.error());
            auto value = random_next(machine, *object, *bits);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/util/Random", "nextInt", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = random_next(machine, *object, 32);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/util/Random", "nextInt", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto bound = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!bound) return std::unexpected(bound.error());
            if (*bound <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Random.nextInt bound is not positive");
            }
            if ((*bound & -*bound) == *bound) {
                auto bits = random_next(machine, *object, 31);
                if (!bits) return std::unexpected(bits.error());
                const i64 result = (static_cast<i64>(*bound) *
                                    static_cast<i64>(*bits)) >> 31;
                return std::optional<Value>(
                    Value::from_int(static_cast<i32>(result)));
            }
            i32 bits = 0;
            i32 value = 0;
            do {
                auto generated = random_next(machine, *object, 31);
                if (!generated) return std::unexpected(generated.error());
                bits = *generated;
                value = bits % *bound;
            } while (bits - value + (*bound - 1) < 0);
            return std::optional<Value>(Value::from_int(value));
        });
    add(registry, "java/util/Random", "nextLong", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto high = random_next(machine, *object, 32);
            auto low = random_next(machine, *object, 32);
            if (!high || !low)
                return fail(ErrorCode::invalid_state,
                            "Random state update failed");
            const u64 bits = (static_cast<u64>(static_cast<u32>(*high))
                              << 32U) |
                             static_cast<u32>(*low);
            return std::optional<Value>(
                Value::from_long(static_cast<i64>(bits)));
        });
    add(registry, "java/util/Random", "nextBoolean", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = random_next(machine, *object, 1);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/util/Random", "nextFloat", "()F",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = random_next(machine, *object, 24);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_float(
                static_cast<float>(*value) / 16777216.0F));
        });
    add(registry, "java/util/Random", "nextDouble", "()D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto high = random_next(machine, *object, 26);
            auto low = random_next(machine, *object, 27);
            if (!high || !low)
                return fail(ErrorCode::invalid_state,
                            "Random state update failed");
            const u64 combined = (static_cast<u64>(static_cast<u32>(*high))
                                  << 27U) +
                                 static_cast<u32>(*low);
            return std::optional<Value>(Value::from_double(
                static_cast<double>(combined) /
                9007199254740992.0));
        });
}

} // namespace

void register_util_natives(NativeMethodRegistry& registry) {
    register_enumeration(registry);
    register_vector(registry);
    register_hashtable(registry);
    register_timer(registry);
    register_random(registry);
}

} // namespace phoneme::vm
