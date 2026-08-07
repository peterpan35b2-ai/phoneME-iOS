#include "Jdk8CompatNativesParts.hpp"

#include <array>
#include <string_view>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kMapKeysField = 0U;
constexpr usize kMapSizeField = 2U;
constexpr usize kEnumMapTypeField = 4U;
constexpr usize kEntryOwnerField = 0U;
constexpr usize kEntryKeyField = 1U;

void register_map_defaults(NativeMethodRegistry& registry) {
    add(registry, "java/util/Map", "getOrDefault",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            auto fallback = reference_argument(arguments, 2U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!fallback) return std::unexpected(fallback.error());
            const Value key_value = Value::from_reference(*key);
            auto value = invoke_checked(machine, *map, "java/util/Map", "get",
                                        "(Ljava/lang/Object;)Ljava/lang/Object;",
                                        std::span<const Value>(&key_value, 1U));
            if (!value) return std::unexpected(value.error());
            if (!value->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.get returned no value");
            }
            auto reference = value->value().as_reference();
            if (!reference) return std::unexpected(reference.error());
            if (!reference->is_null()) return value;
            auto contains = invoke_checked(machine, *map, "java/util/Map",
                                           "containsKey",
                                           "(Ljava/lang/Object;)Z",
                                           std::span<const Value>(&key_value,
                                                                  1U));
            if (!contains) return std::unexpected(contains.error());
            if (!contains->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.containsKey returned no value");
            }
            auto present = contains->value().as_int();
            if (!present) return std::unexpected(present.error());
            return std::optional<Value>(Value::from_reference(
                *present != 0 ? *reference : *fallback));
        });
    add(registry, "java/util/Map", "computeIfAbsent",
        "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            auto function = reference_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!function) return std::unexpected(function.error());
            const Value key_value = Value::from_reference(*key);
            auto current = invoke_checked(
                machine, *map, "java/util/Map", "get",
                "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_value, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.get returned no value");
            }
            auto current_reference = current->value().as_reference();
            if (!current_reference) {
                return std::unexpected(current_reference.error());
            }
            if (!current_reference->is_null()) return current;
            auto computed = invoke_checked(
                machine, *function, "java/util/function/Function", "apply",
                "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_value, 1U));
            if (!computed) return std::unexpected(computed.error());
            if (!computed->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Function.apply returned no value");
            }
            auto computed_reference = computed->value().as_reference();
            if (!computed_reference) {
                return std::unexpected(computed_reference.error());
            }
            if (computed_reference->is_null()) return computed;
            const std::array<Value, 2> put_arguments {
                Value::from_reference(*key),
                Value::from_reference(*computed_reference),
            };
            auto stored = invoke_checked(
                machine, *map, "java/util/Map", "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                put_arguments);
            if (!stored) return std::unexpected(stored.error());
            return computed;
        });
    add(registry, "java/util/Map", "merge",
        "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            auto value = reference_argument(arguments, 2U);
            auto function = reference_argument(arguments, 3U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            if (!function) return std::unexpected(function.error());
            const Value key_value = Value::from_reference(*key);
            auto current = invoke_checked(
                machine, *map, "java/util/Map", "get",
                "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_value, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.get returned no value");
            }
            auto current_reference = current->value().as_reference();
            if (!current_reference) {
                return std::unexpected(current_reference.error());
            }
            ObjectRef merged = *value;
            if (!current_reference->is_null()) {
                const std::array<Value, 2> callback_arguments {
                    Value::from_reference(*current_reference),
                    Value::from_reference(*value),
                };
                auto combined = invoke_checked(
                    machine, *function, "java/util/function/BiFunction",
                    "apply",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    callback_arguments);
                if (!combined) return std::unexpected(combined.error());
                if (!combined->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "BiFunction.apply returned no value");
                }
                auto reference = combined->value().as_reference();
                if (!reference) return std::unexpected(reference.error());
                merged = *reference;
            }
            if (merged.is_null()) {
                auto removed = invoke_checked(
                    machine, *map, "java/util/Map", "remove",
                    "(Ljava/lang/Object;)Ljava/lang/Object;",
                    std::span<const Value>(&key_value, 1U));
                if (!removed) return std::unexpected(removed.error());
            } else {
                const std::array<Value, 2> put_arguments {
                    Value::from_reference(*key),
                    Value::from_reference(merged),
                };
                auto stored = invoke_checked(
                    machine, *map, "java/util/Map", "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    put_arguments);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(merged));
        });
}

void alias_hash_map(NativeMethodRegistry& registry,
                    std::string_view target,
                    bool constructors = true) {
    if (constructors) {
        alias(registry, "java/util/HashMap", "<init>", "()V",
              std::string(target));
        alias(registry, "java/util/HashMap", "<init>", "(I)V",
              std::string(target));
        alias(registry, "java/util/HashMap", "<init>",
              "(Ljava/util/Map;)V", std::string(target));
    }
    const std::array<std::pair<std::string_view, std::string_view>, 16> methods {{
        {"size", "()I"}, {"isEmpty", "()Z"},
        {"containsKey", "(Ljava/lang/Object;)Z"},
        {"containsValue", "(Ljava/lang/Object;)Z"},
        {"get", "(Ljava/lang/Object;)Ljava/lang/Object;"},
        {"getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"},
        {"putIfAbsent", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"},
        {"computeIfAbsent", "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;"},
        {"merge", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;"},
        {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"},
        {"remove", "(Ljava/lang/Object;)Ljava/lang/Object;"},
        {"putAll", "(Ljava/util/Map;)V"}, {"clear", "()V"},
        {"keySet", "()Ljava/util/Set;"},
        {"values", "()Ljava/util/Collection;"},
        {"toString", "()Ljava/lang/String;"},
    }};
    for (const auto& [name, descriptor] : methods) {
        alias(registry, "java/util/HashMap", name, descriptor,
              std::string(target));
    }
}

[[nodiscard]] Result<ObjectRef> make_entry(Machine& machine,
                                            ObjectRef owner,
                                            ObjectRef key) {
    auto entry = new_instance(machine, "java/util/NativeMapEntry");
    if (!entry) return std::unexpected(entry.error());
    auto owner_stored = set_reference_field(machine, *entry,
                                            kEntryOwnerField, owner);
    auto key_stored = set_reference_field(machine, *entry,
                                          kEntryKeyField, key);
    if (!owner_stored) return std::unexpected(owner_stored.error());
    if (!key_stored) return std::unexpected(key_stored.error());
    return *entry;
}

void register_entry_set(NativeMethodRegistry& registry) {
    const std::array<std::string_view, 5> owners {
        "java/util/HashMap", "java/util/LinkedHashMap",
        "java/util/IdentityHashMap", "java/util/WeakHashMap",
        "java/util/EnumMap",
    };
    for (const auto owner : owners) {
        add(registry, std::string(owner), "entrySet", "()Ljava/util/Set;",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto map = receiver(arguments);
                if (!map) return std::unexpected(map.error());
                auto size = int_field(machine, *map, kMapSizeField);
                auto keys = reference_field(machine, *map, kMapKeysField);
                if (!size || !keys || keys->is_null()) {
                    return fail(ErrorCode::invalid_state,
                                "Map entry storage is invalid");
                }
                auto set = new_hash_set(machine, *size * 2);
                if (!set) return std::unexpected(set.error());
                auto root = machine.pin_native_root(*set);
                if (!root) return std::unexpected(root.error());
                for (i32 index = 0; index < *size; ++index) {
                    auto key_value = machine.heap().element(
                        *keys, static_cast<usize>(index));
                    if (!key_value) return std::unexpected(key_value.error());
                    auto key = key_value->as_reference();
                    if (!key) return std::unexpected(key.error());
                    auto entry = make_entry(machine, *map, *key);
                    if (!entry) return std::unexpected(entry.error());
                    const Value argument = Value::from_reference(*entry);
                    auto added = invoke_checked(
                        machine, *set, "java/util/Set", "add",
                        "(Ljava/lang/Object;)Z",
                        std::span<const Value>(&argument, 1U));
                    if (!added) return std::unexpected(added.error());
                }
                return std::optional<Value>(Value::from_reference(*set));
            });
    }
    add(registry, "java/util/NativeMapEntry", "getKey",
        "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto entry = receiver(arguments);
            if (!entry) return std::unexpected(entry.error());
            auto key = reference_field(machine, *entry, kEntryKeyField);
            if (!key) return std::unexpected(key.error());
            return std::optional<Value>(Value::from_reference(*key));
        });
    add(registry, "java/util/NativeMapEntry", "getValue",
        "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto entry = receiver(arguments);
            if (!entry) return std::unexpected(entry.error());
            auto owner = reference_field(machine, *entry, kEntryOwnerField);
            auto key = reference_field(machine, *entry, kEntryKeyField);
            if (!owner) return std::unexpected(owner.error());
            if (!key) return std::unexpected(key.error());
            const Value argument = Value::from_reference(*key);
            return invoke_checked(machine, *owner, "java/util/Map", "get",
                                  "(Ljava/lang/Object;)Ljava/lang/Object;",
                                  std::span<const Value>(&argument, 1U));
        });
    add(registry, "java/util/NativeMapEntry", "setValue",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto entry = receiver(arguments);
            auto value = reference_argument(arguments, 1U, true);
            if (!entry) return std::unexpected(entry.error());
            if (!value) return std::unexpected(value.error());
            auto owner = reference_field(machine, *entry, kEntryOwnerField);
            auto key = reference_field(machine, *entry, kEntryKeyField);
            if (!owner) return std::unexpected(owner.error());
            if (!key) return std::unexpected(key.error());
            const std::array<Value, 2> forwarded {
                Value::from_reference(*key), Value::from_reference(*value),
            };
            return invoke_checked(
                machine, *owner, "java/util/Map", "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                forwarded);
        });
    add(registry, "java/util/NativeMapEntry", "equals",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto entry = receiver(arguments);
            auto other = reference_argument(arguments, 1U, true);
            if (!entry) return std::unexpected(entry.error());
            if (!other) return std::unexpected(other.error());
            if (other->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto is_entry = machine.object_is_instance(*other,
                                                       "java/util/Map$Entry");
            if (!is_entry) return std::unexpected(is_entry.error());
            if (!*is_entry) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto key = reference_field(machine, *entry, kEntryKeyField);
            if (!key) return std::unexpected(key.error());
            auto own_value = invoke_checked(machine, *entry,
                                            "java/util/Map$Entry", "getValue",
                                            "()Ljava/lang/Object;");
            auto other_key = invoke_checked(machine, *other,
                                            "java/util/Map$Entry", "getKey",
                                            "()Ljava/lang/Object;");
            auto other_value = invoke_checked(machine, *other,
                                              "java/util/Map$Entry", "getValue",
                                              "()Ljava/lang/Object;");
            if (!own_value) return std::unexpected(own_value.error());
            if (!other_key) return std::unexpected(other_key.error());
            if (!other_value) return std::unexpected(other_value.error());
            if (!own_value->has_value() || !other_key->has_value() ||
                !other_value->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.Entry accessor returned no value");
            }
            auto own_ref = own_value->value().as_reference();
            auto other_key_ref = other_key->value().as_reference();
            auto other_ref = other_value->value().as_reference();
            if (!own_ref) return std::unexpected(own_ref.error());
            if (!other_key_ref) return std::unexpected(other_key_ref.error());
            if (!other_ref) return std::unexpected(other_ref.error());
            auto keys_equal = object_equals(machine, *key, *other_key_ref);
            auto values_equal = object_equals(machine, *own_ref, *other_ref);
            if (!keys_equal) return std::unexpected(keys_equal.error());
            if (!values_equal) return std::unexpected(values_equal.error());
            return std::optional<Value>(Value::from_int(
                *keys_equal && *values_equal ? 1 : 0));
        });
    add(registry, "java/util/NativeMapEntry", "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto entry = receiver(arguments);
            if (!entry) return std::unexpected(entry.error());
            auto key = reference_field(machine, *entry, kEntryKeyField);
            if (!key) return std::unexpected(key.error());
            auto value = invoke_checked(machine, *entry, "java/util/Map$Entry",
                                        "getValue", "()Ljava/lang/Object;");
            if (!value) return std::unexpected(value.error());
            if (!value->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.Entry.getValue returned no value");
            }
            auto value_ref = value->value().as_reference();
            if (!value_ref) return std::unexpected(value_ref.error());
            auto key_hash = object_hash(machine, *key);
            auto value_hash = object_hash(machine, *value_ref);
            if (!key_hash) return std::unexpected(key_hash.error());
            if (!value_hash) return std::unexpected(value_hash.error());
            return std::optional<Value>(
                Value::from_int(*key_hash ^ *value_hash));
        });
}

[[nodiscard]] Result<i32> identity_index(Machine& machine,
                                         ObjectRef map,
                                         ObjectRef key) {
    auto size = int_field(machine, map, kMapSizeField);
    auto keys = reference_field(machine, map, kMapKeysField);
    if (!size) return std::unexpected(size.error());
    if (!keys) return std::unexpected(keys.error());
    if (keys->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "IdentityHashMap keys are not initialized");
    }
    for (i32 index = 0; index < *size; ++index) {
        auto value = machine.heap().element(*keys,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto reference = value->as_reference();
        if (!reference) return std::unexpected(reference.error());
        if (*reference == key) return index;
    }
    return -1;
}

[[nodiscard]] Status ensure_identity_capacity(Machine& machine,
                                              ObjectRef map,
                                              i32 minimum) {
    auto keys = reference_field(machine, map, kMapKeysField);
    auto values = reference_field(machine, map, 1U);
    if (!keys) return std::unexpected(keys.error());
    if (!values) return std::unexpected(values.error());
    if (keys->is_null() || values->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "IdentityHashMap storage is not initialized");
    }
    auto capacity = machine.heap().array_length(*keys);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    usize next_capacity = *capacity == 0U ? 1U : *capacity * 2U;
    if (next_capacity < static_cast<usize>(minimum)) {
        next_capacity = static_cast<usize>(minimum);
    }
    auto new_keys = allocate_object_array(machine, next_capacity);
    if (!new_keys) return std::unexpected(new_keys.error());
    auto keys_root = machine.pin_native_root(*new_keys);
    if (!keys_root) return std::unexpected(keys_root.error());
    auto new_values = allocate_object_array(machine, next_capacity);
    if (!new_values) return std::unexpected(new_values.error());
    auto size = int_field(machine, map, kMapSizeField);
    if (!size) return std::unexpected(size.error());
    for (i32 index = 0; index < *size; ++index) {
        auto key = machine.heap().element(*keys, static_cast<usize>(index));
        auto value = machine.heap().element(*values, static_cast<usize>(index));
        if (!key) return std::unexpected(key.error());
        if (!value) return std::unexpected(value.error());
        auto key_stored = machine.heap().set_element(
            *new_keys, static_cast<usize>(index), *key);
        auto value_stored = machine.heap().set_element(
            *new_values, static_cast<usize>(index), *value);
        if (!key_stored) return key_stored;
        if (!value_stored) return value_stored;
    }
    auto keys_stored = set_reference_field(machine, map, kMapKeysField,
                                           *new_keys);
    auto values_stored = set_reference_field(machine, map, 1U, *new_values);
    if (!keys_stored) return keys_stored;
    return values_stored;
}

void register_identity_map(NativeMethodRegistry& registry) {
    alias(registry, "java/util/HashMap", "<init>", "()V",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "<init>", "(I)V",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "size", "()I",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "isEmpty", "()Z",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "clear", "()V",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "keySet", "()Ljava/util/Set;",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "values",
          "()Ljava/util/Collection;", "java/util/IdentityHashMap");
    alias(registry, "java/util/HashMap", "toString",
          "()Ljava/lang/String;", "java/util/IdentityHashMap");
    alias(registry, "java/util/Map", "getOrDefault",
          "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/Map", "computeIfAbsent",
          "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;",
          "java/util/IdentityHashMap");
    alias(registry, "java/util/Map", "merge",
          "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;",
          "java/util/IdentityHashMap");

    add(registry, "java/util/IdentityHashMap", "containsKey",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            auto index = identity_index(machine, *map, *key);
            if (!index) return std::unexpected(index.error());
            return std::optional<Value>(Value::from_int(*index >= 0 ? 1 : 0));
        });
    add(registry, "java/util/IdentityHashMap", "containsValue",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto target = reference_argument(arguments, 1U, true);
            if (!map) return std::unexpected(map.error());
            if (!target) return std::unexpected(target.error());
            auto size = int_field(machine, *map, kMapSizeField);
            auto values = reference_field(machine, *map, 1U);
            if (!size) return std::unexpected(size.error());
            if (!values) return std::unexpected(values.error());
            for (i32 index = 0; index < *size; ++index) {
                auto value = machine.heap().element(
                    *values, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto reference = value->as_reference();
                if (!reference) return std::unexpected(reference.error());
                if (*reference == *target) {
                    return std::optional<Value>(Value::from_int(1));
                }
            }
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, "java/util/IdentityHashMap", "get",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            auto index = identity_index(machine, *map, *key);
            if (!index) return std::unexpected(index.error());
            if (*index < 0) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto values = reference_field(machine, *map, 1U);
            if (!values) return std::unexpected(values.error());
            auto value = machine.heap().element(
                *values, static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
    add(registry, "java/util/IdentityHashMap", "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            auto value = reference_argument(arguments, 2U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            auto index = identity_index(machine, *map, *key);
            if (!index) return std::unexpected(index.error());
            auto values = reference_field(machine, *map, 1U);
            if (!values) return std::unexpected(values.error());
            if (*index >= 0) {
                auto previous = machine.heap().element(
                    *values, static_cast<usize>(*index));
                if (!previous) return std::unexpected(previous.error());
                auto stored = machine.heap().set_element(
                    *values, static_cast<usize>(*index),
                    Value::from_reference(*value));
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(*previous);
            }
            auto size = int_field(machine, *map, kMapSizeField);
            if (!size) return std::unexpected(size.error());
            auto capacity = ensure_identity_capacity(machine, *map, *size + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto keys = reference_field(machine, *map, kMapKeysField);
            values = reference_field(machine, *map, 1U);
            if (!keys) return std::unexpected(keys.error());
            if (!values) return std::unexpected(values.error());
            auto key_stored = machine.heap().set_element(
                *keys, static_cast<usize>(*size), Value::from_reference(*key));
            auto value_stored = machine.heap().set_element(
                *values, static_cast<usize>(*size),
                Value::from_reference(*value));
            auto size_stored = set_int_field(machine, *map, kMapSizeField,
                                             *size + 1);
            if (!key_stored) return std::unexpected(key_stored.error());
            if (!value_stored) return std::unexpected(value_stored.error());
            if (!size_stored) return std::unexpected(size_stored.error());
            return std::optional<Value>(Value::from_reference({}));
        });
    add(registry, "java/util/IdentityHashMap", "remove",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            auto index = identity_index(machine, *map, *key);
            if (!index) return std::unexpected(index.error());
            if (*index < 0) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto size = int_field(machine, *map, kMapSizeField);
            auto keys = reference_field(machine, *map, kMapKeysField);
            auto values = reference_field(machine, *map, 1U);
            if (!size) return std::unexpected(size.error());
            if (!keys) return std::unexpected(keys.error());
            if (!values) return std::unexpected(values.error());
            auto previous = machine.heap().element(
                *values, static_cast<usize>(*index));
            if (!previous) return std::unexpected(previous.error());
            for (i32 cursor = *index; cursor + 1 < *size; ++cursor) {
                auto next_key = machine.heap().element(
                    *keys, static_cast<usize>(cursor + 1));
                auto next_value = machine.heap().element(
                    *values, static_cast<usize>(cursor + 1));
                if (!next_key) return std::unexpected(next_key.error());
                if (!next_value) return std::unexpected(next_value.error());
                auto key_stored = machine.heap().set_element(
                    *keys, static_cast<usize>(cursor), *next_key);
                auto value_stored = machine.heap().set_element(
                    *values, static_cast<usize>(cursor), *next_value);
                if (!key_stored) return std::unexpected(key_stored.error());
                if (!value_stored) return std::unexpected(value_stored.error());
            }
            auto cleared_key = machine.heap().set_element(
                *keys, static_cast<usize>(*size - 1),
                Value::from_reference({}));
            auto cleared_value = machine.heap().set_element(
                *values, static_cast<usize>(*size - 1),
                Value::from_reference({}));
            auto size_stored = set_int_field(machine, *map, kMapSizeField,
                                             *size - 1);
            if (!cleared_key) return std::unexpected(cleared_key.error());
            if (!cleared_value) return std::unexpected(cleared_value.error());
            if (!size_stored) return std::unexpected(size_stored.error());
            return std::optional<Value>(*previous);
        });
    add(registry, "java/util/IdentityHashMap", "putIfAbsent",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U, true);
            auto value = reference_argument(arguments, 2U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            auto index = identity_index(machine, *map, *key);
            if (!index) return std::unexpected(index.error());
            if (*index >= 0) {
                auto values = reference_field(machine, *map, 1U);
                if (!values) return std::unexpected(values.error());
                auto current = machine.heap().element(
                    *values, static_cast<usize>(*index));
                if (!current) return std::unexpected(current.error());
                auto reference = current->as_reference();
                if (!reference) return std::unexpected(reference.error());
                if (!reference->is_null()) return std::optional<Value>(*current);
            }
            const std::array<Value, 2> forwarded {
                Value::from_reference(*key), Value::from_reference(*value),
            };
            return invoke_checked(
                machine, *map, "java/util/IdentityHashMap", "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                forwarded);
        });
    add(registry, "java/util/IdentityHashMap", "putAll",
        "(Ljava/util/Map;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto source = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!source) return std::unexpected(source.error());
            auto entries = invoke_checked(machine, *source, "java/util/Map",
                                          "entrySet", "()Ljava/util/Set;");
            if (!entries) return std::unexpected(entries.error());
            if (!entries->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.entrySet returned no value");
            }
            auto set = entries->value().as_reference();
            if (!set) return std::unexpected(set.error());
            auto iterator = invoke_checked(machine, *set, "java/util/Set",
                                           "iterator",
                                           "()Ljava/util/Iterator;");
            if (!iterator) return std::unexpected(iterator.error());
            if (!iterator->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Set.iterator returned no value");
            }
            auto cursor = iterator->value().as_reference();
            if (!cursor) return std::unexpected(cursor.error());
            while (true) {
                auto has_next = invoke_checked(machine, *cursor,
                                               "java/util/Iterator", "hasNext",
                                               "()Z");
                if (!has_next) return std::unexpected(has_next.error());
                if (!has_next->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Iterator.hasNext returned no value");
                }
                auto present = has_next->value().as_int();
                if (!present) return std::unexpected(present.error());
                if (*present == 0) break;
                auto next = invoke_checked(machine, *cursor,
                                           "java/util/Iterator", "next",
                                           "()Ljava/lang/Object;");
                if (!next) return std::unexpected(next.error());
                if (!next->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Iterator.next returned no value");
                }
                auto entry = next->value().as_reference();
                if (!entry) return std::unexpected(entry.error());
                auto key = invoke_checked(machine, *entry,
                                          "java/util/Map$Entry", "getKey",
                                          "()Ljava/lang/Object;");
                auto value = invoke_checked(machine, *entry,
                                            "java/util/Map$Entry", "getValue",
                                            "()Ljava/lang/Object;");
                if (!key) return std::unexpected(key.error());
                if (!value) return std::unexpected(value.error());
                if (!key->has_value() || !value->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Map.Entry accessor returned no value");
                }
                auto key_ref = key->value().as_reference();
                auto value_ref = value->value().as_reference();
                if (!key_ref) return std::unexpected(key_ref.error());
                if (!value_ref) return std::unexpected(value_ref.error());
                const std::array<Value, 2> put_arguments {
                    Value::from_reference(*key_ref),
                    Value::from_reference(*value_ref),
                };
                auto stored = invoke_checked(
                    machine, *map, "java/util/IdentityHashMap", "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    put_arguments);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/IdentityHashMap", "<init>",
        "(Ljava/util/Map;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto source = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!source) return std::unexpected(source.error());
            const std::array<Value, 2> initialized_arguments {
                Value::from_reference(*map), Value::from_int(16),
            };
            auto initialized = invoke_native(
                machine, "java/util/HashMap", "<init>", "(I)V",
                initialized_arguments);
            if (!initialized) return std::unexpected(initialized.error());
            const Value source_argument = Value::from_reference(*source);
            auto copied = invoke_checked(
                machine, *map, "java/util/IdentityHashMap", "putAll",
                "(Ljava/util/Map;)V",
                std::span<const Value>(&source_argument, 1U));
            if (!copied) return std::unexpected(copied.error());
            return std::optional<Value> {};
        });
}

void register_enum_map(NativeMethodRegistry& registry) {
    const std::array<std::pair<std::string_view, std::string_view>, 11> methods {{
        {"size", "()I"}, {"isEmpty", "()Z"},
        {"containsKey", "(Ljava/lang/Object;)Z"},
        {"containsValue", "(Ljava/lang/Object;)Z"},
        {"get", "(Ljava/lang/Object;)Ljava/lang/Object;"},
        {"getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"},
        {"remove", "(Ljava/lang/Object;)Ljava/lang/Object;"},
        {"clear", "()V"}, {"keySet", "()Ljava/util/Set;"},
        {"values", "()Ljava/util/Collection;"},
        {"toString", "()Ljava/lang/String;"},
    }};
    for (const auto& [name, descriptor] : methods) {
        alias(registry, "java/util/HashMap", name, descriptor,
              "java/util/EnumMap");
    }
    add(registry, "java/util/EnumMap", "<init>", "(Ljava/lang/Class;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto type = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!type) return std::unexpected(type.error());
            const std::array<Value, 2> initialize {
                Value::from_reference(*map), Value::from_int(16),
            };
            auto initialized = invoke_native(
                machine, "java/util/HashMap", "<init>", "(I)V", initialize);
            if (!initialized) return std::unexpected(initialized.error());
            auto stored = set_reference_field(machine, *map,
                                              kEnumMapTypeField, *type);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/EnumMap", "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto value = reference_argument(arguments, 2U, true);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            auto type = reference_field(machine, *map, kEnumMapTypeField);
            if (!type) return std::unexpected(type.error());
            auto type_name = machine.mirrored_class_name(*type);
            if (!type_name) return std::unexpected(type_name.error());
            auto valid = machine.object_is_instance(*key, *type_name);
            if (!valid) return std::unexpected(valid.error());
            if (!*valid) {
                return fail_java("java/lang/ClassCastException",
                                 "EnumMap key has wrong enum type");
            }
            const std::array<Value, 3> forwarded {
                Value::from_reference(*map), Value::from_reference(*key),
                Value::from_reference(*value),
            };
            return invoke_native(
                machine, "java/util/HashMap", "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                forwarded);
        });
    auto put_bridge = registry.register_alias(
        "java/util/EnumMap", "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        "java/util/EnumMap", "put",
        "(Ljava/lang/Enum;Ljava/lang/Object;)Ljava/lang/Object;");
    if (!put_bridge) std::terminate();
    add(registry, "java/util/EnumMap", "putAll", "(Ljava/util/Map;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto source = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!source) return std::unexpected(source.error());
            auto entries = invoke_checked(machine, *source, "java/util/Map",
                                          "entrySet", "()Ljava/util/Set;");
            if (!entries) return std::unexpected(entries.error());
            if (!entries->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Map.entrySet returned no value");
            }
            auto set = entries->value().as_reference();
            if (!set) return std::unexpected(set.error());
            auto iterator = invoke_checked(machine, *set, "java/util/Set",
                                           "iterator",
                                           "()Ljava/util/Iterator;");
            if (!iterator) return std::unexpected(iterator.error());
            if (!iterator->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Set.iterator returned no value");
            }
            auto cursor = iterator->value().as_reference();
            if (!cursor) return std::unexpected(cursor.error());
            while (true) {
                auto has_next = invoke_checked(machine, *cursor,
                                               "java/util/Iterator", "hasNext",
                                               "()Z");
                if (!has_next) return std::unexpected(has_next.error());
                if (!has_next->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Iterator.hasNext returned no value");
                }
                auto present = has_next->value().as_int();
                if (!present) return std::unexpected(present.error());
                if (*present == 0) break;
                auto next = invoke_checked(machine, *cursor,
                                           "java/util/Iterator", "next",
                                           "()Ljava/lang/Object;");
                if (!next) return std::unexpected(next.error());
                if (!next->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Iterator.next returned no value");
                }
                auto entry = next->value().as_reference();
                if (!entry) return std::unexpected(entry.error());
                auto key = invoke_checked(machine, *entry,
                                          "java/util/Map$Entry", "getKey",
                                          "()Ljava/lang/Object;");
                auto value = invoke_checked(machine, *entry,
                                            "java/util/Map$Entry", "getValue",
                                            "()Ljava/lang/Object;");
                if (!key) return std::unexpected(key.error());
                if (!value) return std::unexpected(value.error());
                if (!key->has_value() || !value->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Map.Entry accessor returned no value");
                }
                auto key_ref = key->value().as_reference();
                auto value_ref = value->value().as_reference();
                if (!key_ref) return std::unexpected(key_ref.error());
                if (!value_ref) return std::unexpected(value_ref.error());
                const std::array<Value, 2> put_arguments {
                    Value::from_reference(*key_ref),
                    Value::from_reference(*value_ref),
                };
                auto stored = invoke_checked(
                    machine, *map, "java/util/EnumMap", "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    put_arguments);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
}

} // namespace

void register_jdk8_map_natives(NativeMethodRegistry& registry) {
    register_map_defaults(registry);
    alias_hash_map(registry, "java/util/LinkedHashMap");
    register_identity_map(registry);
    alias_hash_map(registry, "java/util/WeakHashMap");
    register_enum_map(registry);
    register_entry_set(registry);
}

} // namespace phoneme::vm
