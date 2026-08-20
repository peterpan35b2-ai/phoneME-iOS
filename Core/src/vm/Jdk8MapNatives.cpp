#include "Jdk8CompatNativesParts.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <vector>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kMapKeysField = 0U;
constexpr usize kMapSizeField = 2U;
constexpr usize kEnumMapTypeField = 4U;
constexpr usize kEntryOwnerField = 0U;
constexpr usize kEntryKeyField = 1U;

[[nodiscard]] Result<std::optional<Value>> invoke_hash_map_native(
    Machine& machine,
    ObjectRef map,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments = {}) {
    std::vector<Value> forwarded;
    forwarded.reserve(arguments.size() + 1U);
    forwarded.push_back(Value::from_reference(map));
    forwarded.insert(forwarded.end(), arguments.begin(), arguments.end());
    return invoke_native(machine, "java/util/HashMap", name, descriptor,
                         forwarded);
}

[[nodiscard]] Result<ObjectRef> create_concurrent_key_set(
    Machine& machine,
    std::optional<ObjectRef> map,
    i32 requested_capacity = 0) {
    i32 capacity = requested_capacity;
    if (map.has_value()) {
        auto size = int_field(machine, *map, kMapSizeField);
        if (!size) return std::unexpected(size.error());
        capacity = std::max(capacity, *size * 2);
    }
    auto set = new_instance(
        machine, "java/util/concurrent/ConcurrentHashMap$KeySetView");
    if (!set) return std::unexpected(set.error());
    auto root = machine.pin_native_root(*set);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> init_arguments {
        Value::from_reference(*set), Value::from_int(capacity),
    };
    auto initialized = invoke_native(machine, "java/util/HashSet", "<init>",
                                     "(I)V", init_arguments);
    if (!initialized) return std::unexpected(initialized.error());
    if (!map.has_value()) return *set;

    auto size = int_field(machine, *map, kMapSizeField);
    auto keys = reference_field(machine, *map, kMapKeysField);
    if (!size) return std::unexpected(size.error());
    if (!keys) return std::unexpected(keys.error());
    for (i32 index = 0; index < *size; ++index) {
        auto key = machine.heap().element(*keys, static_cast<usize>(index));
        if (!key) return std::unexpected(key.error());
        const std::array<Value, 2> add_arguments {
            Value::from_reference(*set), *key,
        };
        auto added = invoke_native(machine, "java/util/HashSet", "add",
                                   "(Ljava/lang/Object;)Z", add_arguments);
        if (!added) return std::unexpected(added.error());
    }
    return *set;
}

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

void register_concurrent_hash_map(NativeMethodRegistry& registry) {
    constexpr std::string_view kOwner =
        "java/util/concurrent/ConcurrentHashMap";

    const auto initialize = [&registry, kOwner](const char* descriptor,
                                        bool has_capacity) {
        add(registry, std::string(kOwner), "<init>", descriptor,
            [has_capacity](Machine& machine,
                           std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto map = receiver(arguments);
                if (!map) return std::unexpected(map.error());
                i32 capacity = 16;
                if (has_capacity) {
                    auto requested = int_argument(arguments, 1U);
                    if (!requested) return std::unexpected(requested.error());
                    if (*requested < 0) {
                        return fail_java("java/lang/IllegalArgumentException",
                                         "negative ConcurrentHashMap capacity");
                    }
                    capacity = *requested;
                }
                const Value capacity_value = Value::from_int(capacity);
                auto initialized = invoke_hash_map_native(
                    machine, *map, "<init>", "(I)V",
                    std::span<const Value>(&capacity_value, 1U));
                if (!initialized) return std::unexpected(initialized.error());
                return std::optional<Value> {};
            });
    };
    initialize("()V", false);
    initialize("(I)V", true);

    add(registry, std::string(kOwner), "<init>", "(IF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto capacity = int_argument(arguments, 1U);
            auto load_factor = float_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!capacity) return std::unexpected(capacity.error());
            if (!load_factor) return std::unexpected(load_factor.error());
            if (*capacity < 0 || !(*load_factor > 0.0F)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid ConcurrentHashMap sizing arguments");
            }
            const Value capacity_value = Value::from_int(*capacity);
            auto initialized = invoke_hash_map_native(
                machine, *map, "<init>", "(I)V",
                std::span<const Value>(&capacity_value, 1U));
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "<init>", "(IFI)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto capacity = int_argument(arguments, 1U);
            auto load_factor = float_argument(arguments, 2U);
            auto concurrency = int_argument(arguments, 3U);
            if (!map) return std::unexpected(map.error());
            if (!capacity) return std::unexpected(capacity.error());
            if (!load_factor) return std::unexpected(load_factor.error());
            if (!concurrency) return std::unexpected(concurrency.error());
            if (*capacity < 0 || !(*load_factor > 0.0F) || *concurrency <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid ConcurrentHashMap sizing arguments");
            }
            const i32 requested = std::max(*capacity, *concurrency);
            const Value capacity_value = Value::from_int(requested);
            auto initialized = invoke_hash_map_native(
                machine, *map, "<init>", "(I)V",
                std::span<const Value>(&capacity_value, 1U));
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });

    const auto invoke_one_reference = [&registry, kOwner](const char* name,
                                                  const char* descriptor,
                                                  bool nullable,
                                                  const char* source_name = nullptr) {
        add(registry, std::string(kOwner), name, descriptor,
            [name = std::string(name), descriptor = std::string(descriptor),
             source = std::string(source_name == nullptr ? name : source_name),
             nullable](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto map = receiver(arguments);
                auto value = reference_argument(arguments, 1U, nullable);
                if (!map) return std::unexpected(map.error());
                if (!value) return std::unexpected(value.error());
                const Value forwarded = Value::from_reference(*value);
                return invoke_hash_map_native(
                    machine, *map, source, descriptor,
                    std::span<const Value>(&forwarded, 1U));
            });
    };
    invoke_one_reference("get", "(Ljava/lang/Object;)Ljava/lang/Object;", false);
    invoke_one_reference("containsKey", "(Ljava/lang/Object;)Z", false);
    invoke_one_reference("containsValue", "(Ljava/lang/Object;)Z", false);
    invoke_one_reference("remove", "(Ljava/lang/Object;)Ljava/lang/Object;", false);
    invoke_one_reference("contains", "(Ljava/lang/Object;)Z", false,
                         "containsValue");

    const auto invoke_two_references = [&registry, kOwner](const char* name,
                                                   const char* descriptor,
                                                   bool second_nullable,
                                                   const char* source_name = nullptr) {
        add(registry, std::string(kOwner), name, descriptor,
            [name = std::string(name), descriptor = std::string(descriptor),
             source = std::string(source_name == nullptr ? name : source_name),
             second_nullable](Machine& machine,
                              std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto map = receiver(arguments);
                auto first = reference_argument(arguments, 1U);
                auto second = reference_argument(arguments, 2U,
                                                 second_nullable);
                if (!map) return std::unexpected(map.error());
                if (!first) return std::unexpected(first.error());
                if (!second) return std::unexpected(second.error());
                const std::array<Value, 2> forwarded {
                    Value::from_reference(*first),
                    Value::from_reference(*second),
                };
                return invoke_hash_map_native(machine, *map, source,
                                              descriptor, forwarded);
            });
    };
    invoke_two_references("put",
                          "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                          false);
    invoke_two_references("putIfAbsent",
                          "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                          false);
    invoke_two_references("getOrDefault",
                          "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                          true);

    const std::array<std::pair<std::string_view, std::string_view>, 6>
        simple_aliases {{
            {"size", "()I"}, {"isEmpty", "()Z"}, {"clear", "()V"},
            {"keySet", "()Ljava/util/Set;"},
            {"values", "()Ljava/util/Collection;"},
            {"toString", "()Ljava/lang/String;"},
        }};
    for (const auto& [name, descriptor] : simple_aliases) {
        alias(registry, "java/util/HashMap", name, descriptor,
              std::string(kOwner));
    }

    add(registry, std::string(kOwner), "remove",
        "(Ljava/lang/Object;Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto expected = reference_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!expected) return std::unexpected(expected.error());
            const Value key_argument = Value::from_reference(*key);
            auto current = invoke_hash_map_native(
                machine, *map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_argument, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "ConcurrentHashMap get returned no value");
            }
            auto current_ref = current->value().as_reference();
            if (!current_ref) return std::unexpected(current_ref.error());
            if (current_ref->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto equal = object_equals(machine, *current_ref, *expected);
            if (!equal) return std::unexpected(equal.error());
            if (!*equal) return std::optional<Value>(Value::from_int(0));
            auto removed = invoke_hash_map_native(
                machine, *map, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_argument, 1U));
            if (!removed) return std::unexpected(removed.error());
            return std::optional<Value>(Value::from_int(1));
        });

    add(registry, std::string(kOwner), "replace",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto value = reference_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            const Value key_argument = Value::from_reference(*key);
            auto current = invoke_hash_map_native(
                machine, *map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_argument, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "ConcurrentHashMap get returned no value");
            }
            auto current_ref = current->value().as_reference();
            if (!current_ref) return std::unexpected(current_ref.error());
            if (current_ref->is_null()) return current->value();
            const std::array<Value, 2> put_arguments {
                Value::from_reference(*key), Value::from_reference(*value),
            };
            return invoke_hash_map_native(
                machine, *map, "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                put_arguments);
        });

    add(registry, std::string(kOwner), "replace",
        "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto expected = reference_argument(arguments, 2U);
            auto desired = reference_argument(arguments, 3U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!expected) return std::unexpected(expected.error());
            if (!desired) return std::unexpected(desired.error());
            const Value key_argument = Value::from_reference(*key);
            auto current = invoke_hash_map_native(
                machine, *map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_argument, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "ConcurrentHashMap get returned no value");
            }
            auto current_ref = current->value().as_reference();
            if (!current_ref) return std::unexpected(current_ref.error());
            if (current_ref->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto equal = object_equals(machine, *current_ref, *expected);
            if (!equal) return std::unexpected(equal.error());
            if (!*equal) return std::optional<Value>(Value::from_int(0));
            const std::array<Value, 2> put_arguments {
                Value::from_reference(*key), Value::from_reference(*desired),
            };
            auto stored = invoke_hash_map_native(
                machine, *map, "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                put_arguments);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(1));
        });

    add(registry, std::string(kOwner), "computeIfAbsent",
        "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto function = reference_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!function) return std::unexpected(function.error());
            const std::array<Value, 2> forwarded {
                Value::from_reference(*key), Value::from_reference(*function),
            };
            return invoke_hash_map_native(
                machine, *map, "computeIfAbsent",
                "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;",
                forwarded);
        });
    add(registry, std::string(kOwner), "merge",
        "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto value = reference_argument(arguments, 2U);
            auto function = reference_argument(arguments, 3U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            if (!function) return std::unexpected(function.error());
            const std::array<Value, 3> forwarded {
                Value::from_reference(*key), Value::from_reference(*value),
                Value::from_reference(*function),
            };
            return invoke_hash_map_native(
                machine, *map, "merge",
                "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;",
                forwarded);
        });

    add(registry, std::string(kOwner), "computeIfPresent",
        "(Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto function = reference_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!function) return std::unexpected(function.error());
            const Value key_argument = Value::from_reference(*key);
            auto current = invoke_hash_map_native(
                machine, *map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_argument, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "ConcurrentHashMap get returned no value");
            }
            auto current_ref = current->value().as_reference();
            if (!current_ref) return std::unexpected(current_ref.error());
            if (current_ref->is_null()) return current->value();
            const std::array<Value, 2> callback_arguments {
                Value::from_reference(*key),
                Value::from_reference(*current_ref),
            };
            auto computed = invoke_checked(
                machine, *function, "java/util/function/BiFunction", "apply",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                callback_arguments);
            if (!computed) return std::unexpected(computed.error());
            if (!computed->has_value()) {
                return fail(ErrorCode::internal_error,
                            "BiFunction.apply returned no value");
            }
            auto computed_ref = computed->value().as_reference();
            if (!computed_ref) return std::unexpected(computed_ref.error());
            if (computed_ref->is_null()) {
                auto removed = invoke_hash_map_native(
                    machine, *map, "remove",
                    "(Ljava/lang/Object;)Ljava/lang/Object;",
                    std::span<const Value>(&key_argument, 1U));
                if (!removed) return std::unexpected(removed.error());
            } else {
                const std::array<Value, 2> put_arguments {
                    Value::from_reference(*key),
                    Value::from_reference(*computed_ref),
                };
                auto stored = invoke_hash_map_native(
                    machine, *map, "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    put_arguments);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*computed_ref));
        });

    add(registry, std::string(kOwner), "compute",
        "(Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto key = reference_argument(arguments, 1U);
            auto function = reference_argument(arguments, 2U);
            if (!map) return std::unexpected(map.error());
            if (!key) return std::unexpected(key.error());
            if (!function) return std::unexpected(function.error());
            const Value key_argument = Value::from_reference(*key);
            auto current = invoke_hash_map_native(
                machine, *map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                std::span<const Value>(&key_argument, 1U));
            if (!current) return std::unexpected(current.error());
            if (!current->has_value()) {
                return fail(ErrorCode::internal_error,
                            "ConcurrentHashMap get returned no value");
            }
            auto current_ref = current->value().as_reference();
            if (!current_ref) return std::unexpected(current_ref.error());
            const std::array<Value, 2> callback_arguments {
                Value::from_reference(*key),
                Value::from_reference(*current_ref),
            };
            auto computed = invoke_checked(
                machine, *function, "java/util/function/BiFunction", "apply",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                callback_arguments);
            if (!computed) return std::unexpected(computed.error());
            if (!computed->has_value()) {
                return fail(ErrorCode::internal_error,
                            "BiFunction.apply returned no value");
            }
            auto computed_ref = computed->value().as_reference();
            if (!computed_ref) return std::unexpected(computed_ref.error());
            if (computed_ref->is_null()) {
                auto removed = invoke_hash_map_native(
                    machine, *map, "remove",
                    "(Ljava/lang/Object;)Ljava/lang/Object;",
                    std::span<const Value>(&key_argument, 1U));
                if (!removed) return std::unexpected(removed.error());
            } else {
                const std::array<Value, 2> put_arguments {
                    Value::from_reference(*key),
                    Value::from_reference(*computed_ref),
                };
                auto stored = invoke_hash_map_native(
                    machine, *map, "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    put_arguments);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*computed_ref));
        });

    add(registry, std::string(kOwner), "forEach",
        "(Ljava/util/function/BiConsumer;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto consumer = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!consumer) return std::unexpected(consumer.error());
            auto size = int_field(machine, *map, kMapSizeField);
            auto keys = reference_field(machine, *map, kMapKeysField);
            auto values = reference_field(machine, *map, 1U);
            if (!size || !keys || !values) {
                return fail(ErrorCode::invalid_state,
                            "ConcurrentHashMap storage is invalid");
            }
            for (i32 index = 0; index < *size; ++index) {
                auto key = machine.heap().element(*keys,
                                                  static_cast<usize>(index));
                auto value = machine.heap().element(*values,
                                                    static_cast<usize>(index));
                if (!key) return std::unexpected(key.error());
                if (!value) return std::unexpected(value.error());
                const std::array<Value, 2> callback {*key, *value};
                auto accepted = invoke_checked(
                    machine, *consumer, "java/util/function/BiConsumer",
                    "accept", "(Ljava/lang/Object;Ljava/lang/Object;)V",
                    callback);
                if (!accepted) return std::unexpected(accepted.error());
            }
            return std::optional<Value> {};
        });

    add(registry, std::string(kOwner), "replaceAll",
        "(Ljava/util/function/BiFunction;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto function = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!function) return std::unexpected(function.error());
            auto size = int_field(machine, *map, kMapSizeField);
            auto keys = reference_field(machine, *map, kMapKeysField);
            auto values = reference_field(machine, *map, 1U);
            if (!size || !keys || !values) {
                return fail(ErrorCode::invalid_state,
                            "ConcurrentHashMap storage is invalid");
            }
            for (i32 index = 0; index < *size; ++index) {
                auto key = machine.heap().element(*keys,
                                                  static_cast<usize>(index));
                auto value = machine.heap().element(*values,
                                                    static_cast<usize>(index));
                if (!key) return std::unexpected(key.error());
                if (!value) return std::unexpected(value.error());
                const std::array<Value, 2> callback {*key, *value};
                auto replaced = invoke_checked(
                    machine, *function, "java/util/function/BiFunction",
                    "apply",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    callback);
                if (!replaced) return std::unexpected(replaced.error());
                if (!replaced->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "BiFunction.apply returned no value");
                }
                auto replacement = replaced->value().as_reference();
                if (!replacement) return std::unexpected(replacement.error());
                if (replacement->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "ConcurrentHashMap replacement is null");
                }
                auto stored = machine.heap().set_element(
                    *values, static_cast<usize>(index),
                    Value::from_reference(*replacement));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, std::string(kOwner), "mappingCount", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            if (!map) return std::unexpected(map.error());
            auto size = int_field(machine, *map, kMapSizeField);
            if (!size) return std::unexpected(size.error());
            return std::optional<Value>(Value::from_long(*size));
        });

    add(registry, std::string(kOwner), "keySet",
        "()Ljava/util/concurrent/ConcurrentHashMap$KeySetView;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            if (!map) return std::unexpected(map.error());
            auto set = create_concurrent_key_set(machine, *map);
            if (!set) return std::unexpected(set.error());
            return std::optional<Value>(Value::from_reference(*set));
        });
    add(registry, std::string(kOwner), "keySet",
        "(Ljava/lang/Object;)Ljava/util/concurrent/ConcurrentHashMap$KeySetView;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto mapped_value = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!mapped_value) return std::unexpected(mapped_value.error());
            auto set = create_concurrent_key_set(machine, *map);
            if (!set) return std::unexpected(set.error());
            return std::optional<Value>(Value::from_reference(*set));
        });
    add(registry, std::string(kOwner), "newKeySet",
        "()Ljava/util/concurrent/ConcurrentHashMap$KeySetView;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto set = create_concurrent_key_set(machine, std::nullopt, 16);
            if (!set) return std::unexpected(set.error());
            return std::optional<Value>(Value::from_reference(*set));
        });
    add(registry, std::string(kOwner), "newKeySet",
        "(I)Ljava/util/concurrent/ConcurrentHashMap$KeySetView;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto capacity = int_argument(arguments, 0U);
            if (!capacity) return std::unexpected(capacity.error());
            if (*capacity < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "negative key set capacity");
            }
            auto set = create_concurrent_key_set(machine, std::nullopt,
                                                 *capacity);
            if (!set) return std::unexpected(set.error());
            return std::optional<Value>(Value::from_reference(*set));
        });
    add(registry, "java/util/concurrent/ConcurrentHashMap$KeySetView",
        "getMappedValue", "()Ljava/lang/Object;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto set = receiver(arguments);
            if (!set) return std::unexpected(set.error());
            return std::optional<Value>(Value::from_reference({}));
        });

    add(registry, std::string(kOwner), "putAll", "(Ljava/util/Map;)V",
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
                if (key_ref->is_null() || value_ref->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "ConcurrentHashMap does not allow nulls");
                }
                const std::array<Value, 2> put_arguments {
                    Value::from_reference(*key_ref),
                    Value::from_reference(*value_ref),
                };
                auto stored = invoke_hash_map_native(
                    machine, *map, "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                    put_arguments);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, std::string(kOwner), "<init>", "(Ljava/util/Map;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto map = receiver(arguments);
            auto source = reference_argument(arguments, 1U);
            if (!map) return std::unexpected(map.error());
            if (!source) return std::unexpected(source.error());
            const Value capacity = Value::from_int(16);
            auto initialized = invoke_hash_map_native(
                machine, *map, "<init>", "(I)V",
                std::span<const Value>(&capacity, 1U));
            if (!initialized) return std::unexpected(initialized.error());
            const Value source_argument = Value::from_reference(*source);
            auto copied = invoke_checked(
                machine, *map, "java/util/concurrent/ConcurrentHashMap",
                "putAll", "(Ljava/util/Map;)V",
                std::span<const Value>(&source_argument, 1U));
            if (!copied) return std::unexpected(copied.error());
            return std::optional<Value> {};
        });

    const auto enumeration = [&registry, kOwner](const char* name,
                                         const char* collection_method,
                                         const char* collection_owner) {
        add(registry, std::string(kOwner), name, "()Ljava/util/Enumeration;",
            [collection_method = std::string(collection_method),
             collection_owner = std::string(collection_owner)](
                 Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto map = receiver(arguments);
                if (!map) return std::unexpected(map.error());
                auto collection = invoke_hash_map_native(
                    machine, *map, collection_method,
                    collection_method == "keySet"
                        ? "()Ljava/util/Set;"
                        : "()Ljava/util/Collection;");
                if (!collection) return std::unexpected(collection.error());
                if (!collection->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "map collection accessor returned no value");
                }
                auto collection_ref = collection->value().as_reference();
                if (!collection_ref) return std::unexpected(collection_ref.error());
                auto array = invoke_checked(machine, *collection_ref,
                                            collection_owner, "toArray",
                                            "()[Ljava/lang/Object;");
                if (!array) return std::unexpected(array.error());
                if (!array->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Collection.toArray returned no value");
                }
                auto values = array->value().as_reference();
                if (!values) return std::unexpected(values.error());
                auto size = machine.heap().array_length(*values);
                if (!size) return std::unexpected(size.error());
                if (*size > static_cast<usize>(std::numeric_limits<i32>::max())) {
                    return fail(ErrorCode::overflow,
                                "ConcurrentHashMap enumeration is too large");
                }
                auto result = new_instance(machine, "java/util/ArrayEnumeration");
                if (!result) return std::unexpected(result.error());
                auto stored_values = set_reference_field(machine, *result, 0U,
                                                         *values);
                auto stored_index = set_int_field(machine, *result, 1U, 0);
                auto stored_size = set_int_field(
                    machine, *result, 2U, static_cast<i32>(*size));
                if (!stored_values) return std::unexpected(stored_values.error());
                if (!stored_index) return std::unexpected(stored_index.error());
                if (!stored_size) return std::unexpected(stored_size.error());
                return std::optional<Value>(Value::from_reference(*result));
            });
    };
    enumeration("keys", "keySet", "java/util/Set");
    enumeration("elements", "values", "java/util/Collection");
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
    const std::array<std::string_view, 6> owners {
        "java/util/HashMap", "java/util/LinkedHashMap",
        "java/util/IdentityHashMap", "java/util/WeakHashMap",
        "java/util/EnumMap", "java/util/concurrent/ConcurrentHashMap",
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
    register_concurrent_hash_map(registry);
    register_identity_map(registry);
    alias_hash_map(registry, "java/util/WeakHashMap");
    register_enum_map(registry);
    register_entry_set(registry);
}

} // namespace phoneme::vm
