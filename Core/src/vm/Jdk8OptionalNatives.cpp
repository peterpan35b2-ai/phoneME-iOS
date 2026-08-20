#include "Jdk8CompatNativesParts.hpp"

#include <bit>
#include <cmath>
#include <string>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kOptionalPrimitiveValueField = 0U;
constexpr usize kOptionalPrimitivePresentField = 1U;

[[nodiscard]] Result<ObjectRef> create_optional_int(Machine& machine,
                                                    i32 value,
                                                    bool present) {
    auto optional = new_instance(machine, "java/util/OptionalInt");
    if (!optional) return std::unexpected(optional.error());
    auto value_stored = set_int_field(machine, *optional,
                                      kOptionalPrimitiveValueField, value);
    auto present_stored = set_int_field(machine, *optional,
                                        kOptionalPrimitivePresentField,
                                        present ? 1 : 0);
    if (!value_stored) return std::unexpected(value_stored.error());
    if (!present_stored) return std::unexpected(present_stored.error());
    return *optional;
}

[[nodiscard]] Result<ObjectRef> create_optional_long(Machine& machine,
                                                     i64 value,
                                                     bool present) {
    auto optional = new_instance(machine, "java/util/OptionalLong");
    if (!optional) return std::unexpected(optional.error());
    auto value_stored = set_long_field(machine, *optional,
                                       kOptionalPrimitiveValueField, value);
    auto present_stored = set_int_field(machine, *optional,
                                        kOptionalPrimitivePresentField,
                                        present ? 1 : 0);
    if (!value_stored) return std::unexpected(value_stored.error());
    if (!present_stored) return std::unexpected(present_stored.error());
    return *optional;
}

[[nodiscard]] Result<ObjectRef> create_optional_double(Machine& machine,
                                                       double value,
                                                       bool present) {
    auto optional = new_instance(machine, "java/util/OptionalDouble");
    if (!optional) return std::unexpected(optional.error());
    auto value_stored = set_double_field(machine, *optional,
                                         kOptionalPrimitiveValueField, value);
    auto present_stored = set_int_field(machine, *optional,
                                        kOptionalPrimitivePresentField,
                                        present ? 1 : 0);
    if (!value_stored) return std::unexpected(value_stored.error());
    if (!present_stored) return std::unexpected(present_stored.error());
    return *optional;
}

[[nodiscard]] Result<bool> optional_present(Machine& machine,
                                            ObjectRef optional) {
    auto present = int_field(machine, optional, kOptionalPrimitivePresentField);
    if (!present) return std::unexpected(present.error());
    return *present != 0;
}

[[nodiscard]] Result<std::optional<Value>> throw_from_supplier(
    Machine& machine,
    ObjectRef supplier) {
    auto supplied = invoke_checked(machine, supplier, "java/util/function/Supplier",
                                   "get", "()Ljava/lang/Object;");
    if (!supplied) return std::unexpected(supplied.error());
    if (!supplied->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Optional exception supplier returned no value");
    }
    auto throwable = supplied->value().as_reference();
    if (!throwable) return std::unexpected(throwable.error());
    if (throwable->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Optional exception supplier returned null");
    }
    auto is_throwable = machine.object_is_instance(*throwable,
                                                    "java/lang/Throwable");
    if (!is_throwable) return std::unexpected(is_throwable.error());
    if (!*is_throwable) {
        return fail_java("java/lang/ClassCastException",
                         "Optional exception supplier did not return Throwable");
    }
    auto class_name = machine.heap().class_name(*throwable);
    if (!class_name) return std::unexpected(class_name.error());
    return fail_java(*class_name, "Optional.orElseThrow supplied exception");
}

[[nodiscard]] Result<ObjectRef> wrapped_number_text(Machine& machine,
                                                    std::string_view owner,
                                                    std::string_view descriptor,
                                                    Value value) {
    const Value argument = value;
    auto text = invoke_native(machine, owner, "toString", descriptor,
                              std::span<const Value>(&argument, 1U));
    if (!text) return std::unexpected(text.error());
    if (!text->has_value()) {
        return fail(ErrorCode::internal_error,
                    "numeric wrapper toString returned no value");
    }
    return text->value().as_reference();
}

[[nodiscard]] Result<ObjectRef> optional_text(Machine& machine,
                                              std::u16string_view type,
                                              std::optional<ObjectRef> value) {
    std::u16string text(type);
    if (!value.has_value()) {
        text.append(u".empty");
        return create_string(machine, std::move(text));
    }
    auto value_text = string_value(machine, *value);
    if (!value_text) return std::unexpected(value_text.error());
    text.push_back(u'[');
    text.append(*value_text);
    text.push_back(u']');
    return create_string(machine, std::move(text));
}

void register_optional_int(NativeMethodRegistry& registry) {
    add(registry, "java/util/OptionalInt", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = set_int_field(machine, *object,
                                       kOptionalPrimitiveValueField, 0);
            auto present = set_int_field(machine, *object,
                                         kOptionalPrimitivePresentField, 0);
            if (!value) return std::unexpected(value.error());
            if (!present) return std::unexpected(present.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalInt", "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto initial = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!initial) return std::unexpected(initial.error());
            auto value = set_int_field(machine, *object,
                                       kOptionalPrimitiveValueField, *initial);
            auto present = set_int_field(machine, *object,
                                         kOptionalPrimitivePresentField, 1);
            if (!value) return std::unexpected(value.error());
            if (!present) return std::unexpected(present.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalInt", "empty", "()Ljava/util/OptionalInt;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto optional = create_optional_int(machine, 0, false);
            if (!optional) return std::unexpected(optional.error());
            return std::optional<Value>(Value::from_reference(*optional));
        });
    add(registry, "java/util/OptionalInt", "of", "(I)Ljava/util/OptionalInt;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = int_argument(arguments, 0U);
            if (!value) return std::unexpected(value.error());
            auto optional = create_optional_int(machine, *value, true);
            if (!optional) return std::unexpected(optional.error());
            return std::optional<Value>(Value::from_reference(*optional));
        });
    add(registry, "java/util/OptionalInt", "isPresent", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            return std::optional<Value>(Value::from_int(*present ? 1 : 0));
        });
    add(registry, "java/util/OptionalInt", "getAsInt", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) {
                return fail_java("java/util/NoSuchElementException",
                                 "No value present");
            }
            auto value = int_field(machine, *object,
                                   kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/util/OptionalInt", "ifPresent",
        "(Ljava/util/function/IntConsumer;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto consumer = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!consumer) return std::unexpected(consumer.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value> {};
            auto value = int_field(machine, *object,
                                   kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            const Value callback = Value::from_int(*value);
            auto accepted = invoke_checked(machine, *consumer,
                                           "java/util/function/IntConsumer",
                                           "accept", "(I)V",
                                           std::span<const Value>(&callback, 1U));
            if (!accepted) return std::unexpected(accepted.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalInt", "orElse", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto fallback = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!fallback) return std::unexpected(fallback.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value>(Value::from_int(*fallback));
            auto value = int_field(machine, *object,
                                   kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/util/OptionalInt", "orElseGet",
        "(Ljava/util/function/IntSupplier;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto supplier = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!supplier) return std::unexpected(supplier.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (*present) {
                auto value = int_field(machine, *object,
                                       kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            }
            if (supplier->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "OptionalInt supplier is null");
            }
            auto value = invoke_checked(machine, *supplier,
                                        "java/util/function/IntSupplier",
                                        "getAsInt", "()I");
            if (!value) return std::unexpected(value.error());
            if (!value->has_value()) {
                return fail(ErrorCode::internal_error,
                            "IntSupplier.getAsInt returned no value");
            }
            return value->value();
        });
    add(registry, "java/util/OptionalInt", "orElseThrow",
        "(Ljava/util/function/Supplier;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto supplier = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!supplier) return std::unexpected(supplier.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (*present) {
                auto value = int_field(machine, *object,
                                       kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            }
            return throw_from_supplier(machine, *supplier);
        });
    add(registry, "java/util/OptionalInt", "equals", "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto other = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!other) return std::unexpected(other.error());
            if (*object == *other) return std::optional<Value>(Value::from_int(1));
            if (other->is_null()) return std::optional<Value>(Value::from_int(0));
            auto compatible = machine.object_is_instance(*other,
                                                         "java/util/OptionalInt");
            if (!compatible) return std::unexpected(compatible.error());
            if (!*compatible) return std::optional<Value>(Value::from_int(0));
            auto present = optional_present(machine, *object);
            auto other_present = optional_present(machine, *other);
            if (!present) return std::unexpected(present.error());
            if (!other_present) return std::unexpected(other_present.error());
            if (*present != *other_present) {
                return std::optional<Value>(Value::from_int(0));
            }
            if (!*present) return std::optional<Value>(Value::from_int(1));
            auto value = int_field(machine, *object, kOptionalPrimitiveValueField);
            auto other_value = int_field(machine, *other,
                                         kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            if (!other_value) return std::unexpected(other_value.error());
            return std::optional<Value>(Value::from_int(
                *value == *other_value ? 1 : 0));
        });
    add(registry, "java/util/OptionalInt", "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value>(Value::from_int(0));
            auto value = int_field(machine, *object, kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/util/OptionalInt", "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            std::optional<ObjectRef> value_text;
            if (*present) {
                auto value = int_field(machine, *object,
                                       kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                auto text = wrapped_number_text(machine, "java/lang/Integer",
                                                "(I)Ljava/lang/String;",
                                                Value::from_int(*value));
                if (!text) return std::unexpected(text.error());
                value_text = *text;
            }
            auto text = optional_text(machine, u"OptionalInt", value_text);
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

void register_optional_long(NativeMethodRegistry& registry) {
    add(registry, "java/util/OptionalLong", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = set_long_field(machine, *object,
                                        kOptionalPrimitiveValueField, 0);
            auto present = set_int_field(machine, *object,
                                         kOptionalPrimitivePresentField, 0);
            if (!value) return std::unexpected(value.error());
            if (!present) return std::unexpected(present.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalLong", "<init>", "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto initial = long_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!initial) return std::unexpected(initial.error());
            auto value = set_long_field(machine, *object,
                                        kOptionalPrimitiveValueField, *initial);
            auto present = set_int_field(machine, *object,
                                         kOptionalPrimitivePresentField, 1);
            if (!value) return std::unexpected(value.error());
            if (!present) return std::unexpected(present.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalLong", "empty", "()Ljava/util/OptionalLong;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto optional = create_optional_long(machine, 0, false);
            if (!optional) return std::unexpected(optional.error());
            return std::optional<Value>(Value::from_reference(*optional));
        });
    add(registry, "java/util/OptionalLong", "of", "(J)Ljava/util/OptionalLong;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = long_argument(arguments, 0U);
            if (!value) return std::unexpected(value.error());
            auto optional = create_optional_long(machine, *value, true);
            if (!optional) return std::unexpected(optional.error());
            return std::optional<Value>(Value::from_reference(*optional));
        });
    add(registry, "java/util/OptionalLong", "isPresent", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            return std::optional<Value>(Value::from_int(*present ? 1 : 0));
        });
    add(registry, "java/util/OptionalLong", "getAsLong", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) {
                return fail_java("java/util/NoSuchElementException",
                                 "No value present");
            }
            auto value = long_field(machine, *object,
                                    kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_long(*value));
        });
    add(registry, "java/util/OptionalLong", "ifPresent",
        "(Ljava/util/function/LongConsumer;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto consumer = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!consumer) return std::unexpected(consumer.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value> {};
            auto value = long_field(machine, *object,
                                    kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            const Value callback = Value::from_long(*value);
            auto accepted = invoke_checked(machine, *consumer,
                                           "java/util/function/LongConsumer",
                                           "accept", "(J)V",
                                           std::span<const Value>(&callback, 1U));
            if (!accepted) return std::unexpected(accepted.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalLong", "orElse", "(J)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto fallback = long_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!fallback) return std::unexpected(fallback.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value>(Value::from_long(*fallback));
            auto value = long_field(machine, *object,
                                    kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_long(*value));
        });
    add(registry, "java/util/OptionalLong", "orElseGet",
        "(Ljava/util/function/LongSupplier;)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto supplier = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!supplier) return std::unexpected(supplier.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (*present) {
                auto value = long_field(machine, *object,
                                        kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_long(*value));
            }
            if (supplier->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "OptionalLong supplier is null");
            }
            auto value = invoke_checked(machine, *supplier,
                                        "java/util/function/LongSupplier",
                                        "getAsLong", "()J");
            if (!value) return std::unexpected(value.error());
            if (!value->has_value()) {
                return fail(ErrorCode::internal_error,
                            "LongSupplier.getAsLong returned no value");
            }
            return value->value();
        });
    add(registry, "java/util/OptionalLong", "orElseThrow",
        "(Ljava/util/function/Supplier;)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto supplier = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!supplier) return std::unexpected(supplier.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (*present) {
                auto value = long_field(machine, *object,
                                        kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_long(*value));
            }
            return throw_from_supplier(machine, *supplier);
        });
    add(registry, "java/util/OptionalLong", "equals", "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto other = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!other) return std::unexpected(other.error());
            if (*object == *other) return std::optional<Value>(Value::from_int(1));
            if (other->is_null()) return std::optional<Value>(Value::from_int(0));
            auto compatible = machine.object_is_instance(*other,
                                                         "java/util/OptionalLong");
            if (!compatible) return std::unexpected(compatible.error());
            if (!*compatible) return std::optional<Value>(Value::from_int(0));
            auto present = optional_present(machine, *object);
            auto other_present = optional_present(machine, *other);
            if (!present) return std::unexpected(present.error());
            if (!other_present) return std::unexpected(other_present.error());
            if (*present != *other_present) {
                return std::optional<Value>(Value::from_int(0));
            }
            if (!*present) return std::optional<Value>(Value::from_int(1));
            auto value = long_field(machine, *object, kOptionalPrimitiveValueField);
            auto other_value = long_field(machine, *other,
                                          kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            if (!other_value) return std::unexpected(other_value.error());
            return std::optional<Value>(Value::from_int(
                *value == *other_value ? 1 : 0));
        });
    add(registry, "java/util/OptionalLong", "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value>(Value::from_int(0));
            auto value = long_field(machine, *object, kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            const u64 bits = static_cast<u64>(*value);
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(static_cast<u32>(bits ^ (bits >> 32U)))));
        });
    add(registry, "java/util/OptionalLong", "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            std::optional<ObjectRef> value_text;
            if (*present) {
                auto value = long_field(machine, *object,
                                        kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                auto text = wrapped_number_text(machine, "java/lang/Long",
                                                "(J)Ljava/lang/String;",
                                                Value::from_long(*value));
                if (!text) return std::unexpected(text.error());
                value_text = *text;
            }
            auto text = optional_text(machine, u"OptionalLong", value_text);
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

[[nodiscard]] u64 java_double_bits(double value) noexcept {
    if (std::isnan(value)) return 0x7ff8'0000'0000'0000ULL;
    return std::bit_cast<u64>(value);
}

void register_optional_double(NativeMethodRegistry& registry) {
    add(registry, "java/util/OptionalDouble", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = set_double_field(machine, *object,
                                          kOptionalPrimitiveValueField, 0.0);
            auto present = set_int_field(machine, *object,
                                         kOptionalPrimitivePresentField, 0);
            if (!value) return std::unexpected(value.error());
            if (!present) return std::unexpected(present.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalDouble", "<init>", "(D)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto initial = double_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!initial) return std::unexpected(initial.error());
            auto value = set_double_field(machine, *object,
                                          kOptionalPrimitiveValueField, *initial);
            auto present = set_int_field(machine, *object,
                                         kOptionalPrimitivePresentField, 1);
            if (!value) return std::unexpected(value.error());
            if (!present) return std::unexpected(present.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalDouble", "empty",
        "()Ljava/util/OptionalDouble;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto optional = create_optional_double(machine, 0.0, false);
            if (!optional) return std::unexpected(optional.error());
            return std::optional<Value>(Value::from_reference(*optional));
        });
    add(registry, "java/util/OptionalDouble", "of",
        "(D)Ljava/util/OptionalDouble;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = double_argument(arguments, 0U);
            if (!value) return std::unexpected(value.error());
            auto optional = create_optional_double(machine, *value, true);
            if (!optional) return std::unexpected(optional.error());
            return std::optional<Value>(Value::from_reference(*optional));
        });
    add(registry, "java/util/OptionalDouble", "isPresent", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            return std::optional<Value>(Value::from_int(*present ? 1 : 0));
        });
    add(registry, "java/util/OptionalDouble", "getAsDouble", "()D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) {
                return fail_java("java/util/NoSuchElementException",
                                 "No value present");
            }
            auto value = double_field(machine, *object,
                                      kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_double(*value));
        });
    add(registry, "java/util/OptionalDouble", "ifPresent",
        "(Ljava/util/function/DoubleConsumer;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto consumer = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!consumer) return std::unexpected(consumer.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value> {};
            auto value = double_field(machine, *object,
                                      kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            const Value callback = Value::from_double(*value);
            auto accepted = invoke_checked(machine, *consumer,
                                           "java/util/function/DoubleConsumer",
                                           "accept", "(D)V",
                                           std::span<const Value>(&callback, 1U));
            if (!accepted) return std::unexpected(accepted.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/OptionalDouble", "orElse", "(D)D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto fallback = double_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!fallback) return std::unexpected(fallback.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) {
                return std::optional<Value>(Value::from_double(*fallback));
            }
            auto value = double_field(machine, *object,
                                      kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_double(*value));
        });
    add(registry, "java/util/OptionalDouble", "orElseGet",
        "(Ljava/util/function/DoubleSupplier;)D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto supplier = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!supplier) return std::unexpected(supplier.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (*present) {
                auto value = double_field(machine, *object,
                                          kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_double(*value));
            }
            if (supplier->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "OptionalDouble supplier is null");
            }
            auto value = invoke_checked(machine, *supplier,
                                        "java/util/function/DoubleSupplier",
                                        "getAsDouble", "()D");
            if (!value) return std::unexpected(value.error());
            if (!value->has_value()) {
                return fail(ErrorCode::internal_error,
                            "DoubleSupplier.getAsDouble returned no value");
            }
            return value->value();
        });
    add(registry, "java/util/OptionalDouble", "orElseThrow",
        "(Ljava/util/function/Supplier;)D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto supplier = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!supplier) return std::unexpected(supplier.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (*present) {
                auto value = double_field(machine, *object,
                                          kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_double(*value));
            }
            return throw_from_supplier(machine, *supplier);
        });
    add(registry, "java/util/OptionalDouble", "equals", "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto other = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!other) return std::unexpected(other.error());
            if (*object == *other) return std::optional<Value>(Value::from_int(1));
            if (other->is_null()) return std::optional<Value>(Value::from_int(0));
            auto compatible = machine.object_is_instance(*other,
                                                         "java/util/OptionalDouble");
            if (!compatible) return std::unexpected(compatible.error());
            if (!*compatible) return std::optional<Value>(Value::from_int(0));
            auto present = optional_present(machine, *object);
            auto other_present = optional_present(machine, *other);
            if (!present) return std::unexpected(present.error());
            if (!other_present) return std::unexpected(other_present.error());
            if (*present != *other_present) {
                return std::optional<Value>(Value::from_int(0));
            }
            if (!*present) return std::optional<Value>(Value::from_int(1));
            auto value = double_field(machine, *object,
                                      kOptionalPrimitiveValueField);
            auto other_value = double_field(machine, *other,
                                            kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            if (!other_value) return std::unexpected(other_value.error());
            return std::optional<Value>(Value::from_int(
                java_double_bits(*value) == java_double_bits(*other_value)
                    ? 1 : 0));
        });
    add(registry, "java/util/OptionalDouble", "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            if (!*present) return std::optional<Value>(Value::from_int(0));
            auto value = double_field(machine, *object,
                                      kOptionalPrimitiveValueField);
            if (!value) return std::unexpected(value.error());
            const u64 bits = java_double_bits(*value);
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(static_cast<u32>(bits ^ (bits >> 32U)))));
        });
    add(registry, "java/util/OptionalDouble", "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto present = optional_present(machine, *object);
            if (!present) return std::unexpected(present.error());
            std::optional<ObjectRef> value_text;
            if (*present) {
                auto value = double_field(machine, *object,
                                          kOptionalPrimitiveValueField);
                if (!value) return std::unexpected(value.error());
                auto text = wrapped_number_text(machine, "java/lang/Double",
                                                "(D)Ljava/lang/String;",
                                                Value::from_double(*value));
                if (!text) return std::unexpected(text.error());
                value_text = *text;
            }
            auto text = optional_text(machine, u"OptionalDouble", value_text);
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

} // namespace

void register_jdk8_optional_natives(NativeMethodRegistry& registry) {
    register_optional_int(registry);
    register_optional_long(registry);
    register_optional_double(registry);
}

} // namespace phoneme::vm
