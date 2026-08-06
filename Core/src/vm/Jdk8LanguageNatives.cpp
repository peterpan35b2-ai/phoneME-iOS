#include "Jdk8CompatNativesParts.hpp"

#include <array>
#include <limits>
#include <string>
#include <string_view>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kEnumNameField = 0U;
constexpr usize kEnumOrdinalField = 1U;
constexpr usize kComparatorKindField = 0U;
constexpr usize kComparatorFirstField = 1U;
constexpr usize kComparatorSecondField = 2U;

[[nodiscard]] Result<ObjectRef> make_comparator(Machine& machine,
                                                i32 kind,
                                                ObjectRef first,
                                                ObjectRef second = {}) {
    auto comparator = new_instance(machine, "java/util/NativeComparator");
    if (!comparator) return std::unexpected(comparator.error());
    auto kind_stored = set_int_field(machine, *comparator,
                                     kComparatorKindField, kind);
    auto first_stored = set_reference_field(machine, *comparator,
                                             kComparatorFirstField, first);
    auto second_stored = set_reference_field(machine, *comparator,
                                              kComparatorSecondField, second);
    if (!kind_stored) return std::unexpected(kind_stored.error());
    if (!first_stored) return std::unexpected(first_stored.error());
    if (!second_stored) return std::unexpected(second_stored.error());
    return *comparator;
}

void register_enum(NativeMethodRegistry& registry) {
    add(registry, "java/lang/Enum", "<init>",
        "(Ljava/lang/String;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto name = reference_argument(arguments, 1U);
            auto ordinal = int_argument(arguments, 2U);
            if (!object) return std::unexpected(object.error());
            if (!name) return std::unexpected(name.error());
            if (!ordinal) return std::unexpected(ordinal.error());
            auto name_stored = set_reference_field(machine, *object,
                                                   kEnumNameField, *name);
            auto ordinal_stored = set_int_field(machine, *object,
                                                kEnumOrdinalField, *ordinal);
            if (!name_stored) return std::unexpected(name_stored.error());
            if (!ordinal_stored) return std::unexpected(ordinal_stored.error());
            return std::optional<Value> {};
        });
    const auto string_value_method = [&registry](const char* name) {
        add(registry, "java/lang/Enum", name, "()Ljava/lang/String;",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto name_value = reference_field(machine, *object,
                                                  kEnumNameField);
                if (!name_value) {
                    return std::unexpected(name_value.error());
                }
                return std::optional<Value>(
                    Value::from_reference(*name_value));
            });
    };
    string_value_method("name");
    string_value_method("toString");
    add(registry, "java/lang/Enum", "ordinal", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto ordinal = int_field(machine, *object, kEnumOrdinalField);
            if (!ordinal) return std::unexpected(ordinal.error());
            return std::optional<Value>(Value::from_int(*ordinal));
        });
    add(registry, "java/lang/Enum", "equals", "(Ljava/lang/Object;)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto other = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!other) return std::unexpected(other.error());
            return std::optional<Value>(
                Value::from_int(*object == *other ? 1 : 0));
        });
    add(registry, "java/lang/Enum", "hashCode", "()I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            const u64 mixed = object->bits ^ (object->bits >> 33U) ^
                              (object->bits << 11U);
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(static_cast<u32>(mixed ^ (mixed >> 32U)))));
        });
    const auto compare = [&registry](const char* descriptor) {
        add(registry, "java/lang/Enum", "compareTo", descriptor,
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto other = reference_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!other) return std::unexpected(other.error());
                auto left_class = machine.heap().class_name(*object);
                auto right_class = machine.heap().class_name(*other);
                if (!left_class) return std::unexpected(left_class.error());
                if (!right_class) return std::unexpected(right_class.error());
                if (*left_class != *right_class) {
                    return fail_java("java/lang/ClassCastException",
                                     "enum constants have different classes");
                }
                auto left = int_field(machine, *object, kEnumOrdinalField);
                auto right = int_field(machine, *other, kEnumOrdinalField);
                if (!left) return std::unexpected(left.error());
                if (!right) return std::unexpected(right.error());
                return std::optional<Value>(Value::from_int(*left - *right));
            });
    };
    compare("(Ljava/lang/Enum;)I");
    compare("(Ljava/lang/Object;)I");
    add(registry, "java/lang/Enum", "getDeclaringClass",
        "()Ljava/lang/Class;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto class_name = machine.heap().class_name(*object);
            if (!class_name) return std::unexpected(class_name.error());
            auto mirror = machine.class_mirror(*class_name);
            if (!mirror) return std::unexpected(mirror.error());
            return std::optional<Value>(Value::from_reference(*mirror));
        });
    add(registry, "java/lang/Enum", "valueOf",
        "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = reference_argument(arguments, 0U);
            auto name = reference_argument(arguments, 1U);
            if (!mirror) return std::unexpected(mirror.error());
            if (!name) return std::unexpected(name.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            auto requested = string_value(machine, *name);
            if (!class_name) return std::unexpected(class_name.error());
            if (!requested) return std::unexpected(requested.error());
            auto loaded = machine.classes().load(*class_name);
            if (!loaded) return std::unexpected(loaded.error());
            const std::string descriptor = "L" + *class_name + ";";
            for (const auto& candidate : (*loaded)->fields()) {
                if ((candidate.access_flags & 0x0008U) == 0U ||
                    candidate.descriptor != descriptor) {
                    continue;
                }
                std::u16string candidate_name;
                candidate_name.reserve(candidate.name.size());
                for (const char value : candidate.name) {
                    candidate_name.push_back(static_cast<char16_t>(
                        static_cast<unsigned char>(value)));
                }
                if (candidate_name != *requested) continue;
                auto location = machine.class_states().resolve_field(
                    *class_name, candidate.name, descriptor, true);
                if (!location) return std::unexpected(location.error());
                auto value = machine.class_states().static_field(*location);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(*value);
            }
            return fail_java("java/lang/IllegalArgumentException",
                             "No enum constant with requested name");
        });
}

void register_functional(NativeMethodRegistry& registry) {
    add(registry, "java/util/function/IntUnaryOperator", "identity",
        "()Ljava/util/function/IntUnaryOperator;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto identity = new_instance(
                machine, "java/util/function/NativeIntIdentity");
            if (!identity) return std::unexpected(identity.error());
            return std::optional<Value>(Value::from_reference(*identity));
        });
    add(registry, "java/util/function/NativeIntIdentity", "applyAsInt",
        "(I)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
}

void register_comparator(NativeMethodRegistry& registry) {
    add(registry, "java/util/Comparator", "reversed",
        "()Ljava/util/Comparator;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto source = receiver(arguments);
            if (!source) return std::unexpected(source.error());
            auto result = make_comparator(machine, 0, *source);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, "java/util/Comparator", "thenComparing",
        "(Ljava/util/Comparator;)Ljava/util/Comparator;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto source = receiver(arguments);
            auto next = reference_argument(arguments, 1U);
            if (!source) return std::unexpected(source.error());
            if (!next) return std::unexpected(next.error());
            auto result = make_comparator(machine, 1, *source, *next);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    const auto extracting = [&registry](const char* name,
                                        const char* descriptor,
                                        i32 kind) {
        add(registry, "java/util/Comparator", name, descriptor,
            [kind, name](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                const bool is_static = std::string_view(name) ==
                                       "comparingInt";
                auto source = is_static
                    ? Result<ObjectRef>(ObjectRef {}) : receiver(arguments);
                auto extractor = reference_argument(arguments,
                                                    is_static ? 0U : 1U);
                if (!source) return std::unexpected(source.error());
                if (!extractor) return std::unexpected(extractor.error());
                auto extracted = make_comparator(machine, kind, *extractor);
                if (!extracted) return std::unexpected(extracted.error());
                if (is_static) {
                    return std::optional<Value>(
                        Value::from_reference(*extracted));
                }
                auto root = machine.pin_native_root(*extracted);
                if (!root) return std::unexpected(root.error());
                auto result = make_comparator(machine, 1, *source,
                                              *extracted);
                if (!result) return std::unexpected(result.error());
                return std::optional<Value>(Value::from_reference(*result));
            });
    };
    extracting("thenComparing",
               "(Ljava/util/function/Function;)Ljava/util/Comparator;", 2);
    extracting("thenComparingInt",
               "(Ljava/util/function/ToIntFunction;)Ljava/util/Comparator;", 3);
    extracting("comparingInt",
               "(Ljava/util/function/ToIntFunction;)Ljava/util/Comparator;", 3);
    add(registry, "java/util/NativeComparator", "compare",
        "(Ljava/lang/Object;Ljava/lang/Object;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto left = reference_argument(arguments, 1U, true);
            auto right = reference_argument(arguments, 2U, true);
            if (!object) return std::unexpected(object.error());
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            auto kind = int_field(machine, *object, kComparatorKindField);
            auto first = reference_field(machine, *object,
                                         kComparatorFirstField);
            auto second = reference_field(machine, *object,
                                          kComparatorSecondField);
            if (!kind) return std::unexpected(kind.error());
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            i32 comparison = 0;
            if (*kind == 0) {
                auto result = comparator_compare(machine, *first,
                                                 *right, *left);
                if (!result) return std::unexpected(result.error());
                comparison = *result;
            } else if (*kind == 1) {
                auto result = comparator_compare(machine, *first,
                                                 *left, *right);
                if (!result) return std::unexpected(result.error());
                comparison = *result;
                if (comparison == 0) {
                    auto next = comparator_compare(machine, *second,
                                                   *left, *right);
                    if (!next) return std::unexpected(next.error());
                    comparison = *next;
                }
            } else if (*kind == 2) {
                const Value left_argument = Value::from_reference(*left);
                const Value right_argument = Value::from_reference(*right);
                auto left_key = invoke_checked(
                    machine, *first, "java/util/function/Function", "apply",
                    "(Ljava/lang/Object;)Ljava/lang/Object;",
                    std::span<const Value>(&left_argument, 1U));
                auto right_key = invoke_checked(
                    machine, *first, "java/util/function/Function", "apply",
                    "(Ljava/lang/Object;)Ljava/lang/Object;",
                    std::span<const Value>(&right_argument, 1U));
                if (!left_key) return std::unexpected(left_key.error());
                if (!right_key) return std::unexpected(right_key.error());
                if (!left_key->has_value() || !right_key->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Function.apply returned no value");
                }
                auto left_ref = left_key->value().as_reference();
                auto right_ref = right_key->value().as_reference();
                if (!left_ref) return std::unexpected(left_ref.error());
                if (!right_ref) return std::unexpected(right_ref.error());
                auto result = natural_compare(machine, *left_ref, *right_ref);
                if (!result) return std::unexpected(result.error());
                comparison = *result;
            } else if (*kind == 3) {
                const Value left_argument = Value::from_reference(*left);
                const Value right_argument = Value::from_reference(*right);
                auto left_key = invoke_checked(
                    machine, *first, "java/util/function/ToIntFunction",
                    "applyAsInt", "(Ljava/lang/Object;)I",
                    std::span<const Value>(&left_argument, 1U));
                auto right_key = invoke_checked(
                    machine, *first, "java/util/function/ToIntFunction",
                    "applyAsInt", "(Ljava/lang/Object;)I",
                    std::span<const Value>(&right_argument, 1U));
                if (!left_key) return std::unexpected(left_key.error());
                if (!right_key) return std::unexpected(right_key.error());
                if (!left_key->has_value() || !right_key->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "ToIntFunction returned no value");
                }
                auto left_int = left_key->value().as_int();
                auto right_int = right_key->value().as_int();
                if (!left_int) return std::unexpected(left_int.error());
                if (!right_int) return std::unexpected(right_int.error());
                comparison = *left_int < *right_int
                    ? -1 : (*left_int > *right_int ? 1 : 0);
            } else {
                return fail(ErrorCode::invalid_state,
                            "NativeComparator kind is invalid");
            }
            return std::optional<Value>(Value::from_int(comparison));
        });
}

void register_small_methods(NativeMethodRegistry& registry) {
    add(registry, "java/lang/Integer", "compare", "(II)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto left = int_argument(arguments, 0U);
            auto right = int_argument(arguments, 1U);
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            return std::optional<Value>(Value::from_int(
                *left < *right ? -1 : (*left > *right ? 1 : 0)));
        });
    add(registry, "java/lang/Integer", "sum", "(II)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto left = int_argument(arguments, 0U);
            auto right = int_argument(arguments, 1U);
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(static_cast<u32>(*left) +
                                 static_cast<u32>(*right))));
        });
    add(registry, "java/lang/Long", "sum", "(JJ)J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto left = long_argument(arguments, 0U);
            auto right = long_argument(arguments, 1U);
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            return std::optional<Value>(Value::from_long(
                static_cast<i64>(static_cast<u64>(*left) +
                                 static_cast<u64>(*right))));
        });
    add(registry, "java/lang/Long", "remainderUnsigned", "(JJ)J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto dividend = long_argument(arguments, 0U);
            auto divisor = long_argument(arguments, 1U);
            if (!dividend) return std::unexpected(dividend.error());
            if (!divisor) return std::unexpected(divisor.error());
            if (*divisor == 0) {
                return fail_java("java/lang/ArithmeticException", "/ by zero");
            }
            return std::optional<Value>(Value::from_long(
                static_cast<i64>(static_cast<u64>(*dividend) %
                                 static_cast<u64>(*divisor))));
        });
    add(registry, "java/util/Objects", "hash", "([Ljava/lang/Object;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto values = reference_argument(arguments, 0U);
            if (!values) return std::unexpected(values.error());
            auto length = machine.heap().array_length(*values);
            if (!length) return std::unexpected(length.error());
            u32 result = 1U;
            for (usize index = 0U; index < *length; ++index) {
                auto value = machine.heap().element(*values, index);
                if (!value) return std::unexpected(value.error());
                auto reference = value->as_reference();
                if (!reference) return std::unexpected(reference.error());
                auto hash = object_hash(machine, *reference);
                if (!hash) return std::unexpected(hash.error());
                result = result * 31U + static_cast<u32>(*hash);
            }
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(result)));
        });
}

} // namespace

void register_jdk8_language_natives(NativeMethodRegistry& registry) {
    register_enum(registry);
    register_functional(registry);
    register_comparator(registry);
    register_small_methods(registry);
}

} // namespace phoneme::vm
