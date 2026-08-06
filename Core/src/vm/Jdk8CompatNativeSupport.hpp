#pragma once

#include <array>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm::jdk8compat {

inline void add(NativeMethodRegistry& registry,
                std::string owner,
                std::string name,
                std::string descriptor,
                NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) std::terminate();
}

inline void alias(NativeMethodRegistry& registry,
                  std::string_view source_owner,
                  std::string_view name,
                  std::string_view descriptor,
                  std::string target_owner) {
    auto registered = registry.register_alias(source_owner, name, descriptor,
                                              std::move(target_owner),
                                              std::string(name),
                                              std::string(descriptor));
    if (!registered) std::terminate();
}

[[nodiscard]] inline Result<ObjectRef> receiver(
    std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "JDK 8 native has no receiver");
    }
    auto value = arguments.front().as_reference();
    if (!value || value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "JDK 8 native receiver is null");
    }
    return *value;
}

[[nodiscard]] inline Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool nullable = false) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "JDK 8 reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!nullable && value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "JDK 8 reference argument is null");
    }
    return *value;
}

[[nodiscard]] inline Result<i32> int_argument(
    std::span<const Value> arguments,
    usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "JDK 8 int argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] inline Result<i64> long_argument(
    std::span<const Value> arguments,
    usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "JDK 8 long argument is missing");
    }
    return arguments[index].as_long();
}

[[nodiscard]] inline Result<ObjectRef> reference_field(
    Machine& machine,
    ObjectRef object,
    usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] inline Result<i32> int_field(Machine& machine,
                                           ObjectRef object,
                                           usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] inline Result<i64> long_field(Machine& machine,
                                            ObjectRef object,
                                            usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_long();
}

[[nodiscard]] inline Status set_reference_field(Machine& machine,
                                                ObjectRef object,
                                                usize index,
                                                ObjectRef value) {
    return machine.heap().set_field(object, index,
                                    Value::from_reference(value));
}

[[nodiscard]] inline Status set_int_field(Machine& machine,
                                          ObjectRef object,
                                          usize index,
                                          i32 value) {
    return machine.heap().set_field(object, index, Value::from_int(value));
}

[[nodiscard]] inline Status set_long_field(Machine& machine,
                                           ObjectRef object,
                                           usize index,
                                           i64 value) {
    return machine.heap().set_field(object, index, Value::from_long(value));
}

[[nodiscard]] inline Result<ObjectRef> create_string(Machine& machine,
                                                     std::u16string text) {
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] inline Result<std::u16string> string_value(
    Machine& machine,
    ObjectRef string) {
    if (string.is_null()) {
        return fail_java("java/lang/NullPointerException", "String is null");
    }
    return machine.heap().string_value(string);
}

[[nodiscard]] inline Result<std::optional<Value>> invoke_checked(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments = {}) {
    auto invoked = machine.invoke_instance(object, owner, name, descriptor,
                                           arguments);
    if (!invoked) return std::unexpected(invoked.error());
    if (invoked->throwable.has_value()) {
        auto throwable = machine.heap().class_name(*invoked->throwable);
        if (!throwable) return std::unexpected(throwable.error());
        return fail_java(*throwable,
                         invoked->exception_context.empty()
                             ? "invoked JDK 8 method threw"
                             : invoked->exception_context);
    }
    return invoked->return_value;
}

[[nodiscard]] inline Result<std::optional<Value>> invoke_native(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments) {
    return machine.natives().invoke(machine, owner, name, descriptor,
                                    arguments);
}

[[nodiscard]] inline Result<ObjectRef> new_instance(
    Machine& machine,
    std::string_view class_name) {
    return machine.class_states().allocate_instance(machine.heap(), class_name);
}

[[nodiscard]] inline Result<ObjectRef> allocate_object_array(
    Machine& machine,
    usize length) {
    return machine.heap().allocate_array(
        "[Ljava/lang/Object;", length, Value::from_reference({}));
}

[[nodiscard]] inline Result<ObjectRef> new_array_list(Machine& machine,
                                                       i32 capacity = 0) {
    auto list = new_instance(machine, "java/util/ArrayList");
    if (!list) return std::unexpected(list.error());
    auto root = machine.pin_native_root(*list);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> arguments {
        Value::from_reference(*list), Value::from_int(capacity),
    };
    auto initialized = invoke_native(machine, "java/util/ArrayList", "<init>",
                                     "(I)V", arguments);
    if (!initialized) return std::unexpected(initialized.error());
    return *list;
}

[[nodiscard]] inline Result<ObjectRef> new_hash_map(Machine& machine,
                                                     i32 capacity = 16) {
    auto map = new_instance(machine, "java/util/HashMap");
    if (!map) return std::unexpected(map.error());
    auto root = machine.pin_native_root(*map);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> arguments {
        Value::from_reference(*map), Value::from_int(capacity),
    };
    auto initialized = invoke_native(machine, "java/util/HashMap", "<init>",
                                     "(I)V", arguments);
    if (!initialized) return std::unexpected(initialized.error());
    return *map;
}

[[nodiscard]] inline Result<ObjectRef> new_hash_set(Machine& machine,
                                                     i32 capacity = 16) {
    auto set = new_instance(machine, "java/util/HashSet");
    if (!set) return std::unexpected(set.error());
    auto root = machine.pin_native_root(*set);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> arguments {
        Value::from_reference(*set), Value::from_int(capacity),
    };
    auto initialized = invoke_native(machine, "java/util/HashSet", "<init>",
                                     "(I)V", arguments);
    if (!initialized) return std::unexpected(initialized.error());
    return *set;
}

[[nodiscard]] inline Result<bool> object_equals(Machine& machine,
                                                ObjectRef left,
                                                ObjectRef right) {
    if (left == right) return true;
    if (left.is_null() || right.is_null()) return false;
    const Value argument = Value::from_reference(right);
    auto result = invoke_checked(machine, left, "java/lang/Object", "equals",
                                 "(Ljava/lang/Object;)Z",
                                 std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Object.equals returned no value");
    }
    auto equal = result->value().as_int();
    if (!equal) return std::unexpected(equal.error());
    return *equal != 0;
}

[[nodiscard]] inline Result<i32> object_hash(Machine& machine,
                                             ObjectRef object) {
    if (object.is_null()) return 0;
    auto result = invoke_checked(machine, object, "java/lang/Object",
                                 "hashCode", "()I");
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Object.hashCode returned no value");
    }
    return result->value().as_int();
}

[[nodiscard]] inline Result<i32> comparator_compare(Machine& machine,
                                                    ObjectRef comparator,
                                                    ObjectRef left,
                                                    ObjectRef right) {
    const std::array<Value, 2> arguments {
        Value::from_reference(left), Value::from_reference(right),
    };
    auto result = invoke_checked(machine, comparator, "java/util/Comparator",
                                 "compare",
                                 "(Ljava/lang/Object;Ljava/lang/Object;)I",
                                 arguments);
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Comparator.compare returned no value");
    }
    return result->value().as_int();
}

[[nodiscard]] inline Result<i32> natural_compare(Machine& machine,
                                                 ObjectRef left,
                                                 ObjectRef right) {
    if (left.is_null() || right.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "natural-order key is null");
    }
    const Value argument = Value::from_reference(right);
    auto result = invoke_checked(machine, left, "java/lang/Comparable",
                                 "compareTo", "(Ljava/lang/Object;)I",
                                 std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Comparable.compareTo returned no value");
    }
    return result->value().as_int();
}

} // namespace phoneme::vm::jdk8compat
