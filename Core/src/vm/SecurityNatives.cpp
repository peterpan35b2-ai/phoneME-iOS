#include "SecurityNatives.hpp"

#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/security/PermissionSemantics.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

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

    register_permission_natives(registry);
}

} // namespace phoneme::vm
