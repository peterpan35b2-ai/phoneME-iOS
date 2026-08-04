#include "PimNatives.hpp"

#include <array>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr std::string_view kPim = "javax/microedition/pim/PIM";
constexpr std::string_view kPimImpl = "phoneme/pim/PIMImpl";
constexpr std::string_view kEmptyList = "phoneme/pim/EmptyPIMList";

void add(NativeMethodRegistry& registry,
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

[[nodiscard]] Result<ObjectRef> receiver(std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument, "PIM receiver is missing");
    }
    auto object = arguments.front().as_reference();
    if (!object) return std::unexpected(object.error());
    if (object->is_null()) {
        return fail_java("java/lang/NullPointerException", "PIM receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<i32> int_argument(std::span<const Value> arguments,
                                       usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument, "PIM integer argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<ObjectRef> allocate_instance(Machine& machine,
                                                  std::string_view class_name) {
    auto object = machine.class_states().allocate_instance(machine.heap(),
                                                           class_name);
    if (object || object.error().code != ErrorCode::overflow) return object;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.class_states().allocate_instance(machine.heap(), class_name);
}

[[nodiscard]] Result<ObjectRef> allocate_array(Machine& machine,
                                               std::string descriptor,
                                               usize length,
                                               Value initial) {
    auto array = machine.heap().allocate_array(descriptor, length, initial);
    if (array || array.error().code != ErrorCode::overflow) return array;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.heap().allocate_array(descriptor, length, initial);
}

[[nodiscard]] Result<ObjectRef> make_string(Machine& machine,
                                            std::u16string text) {
    auto object = allocate_instance(machine, "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] Result<FieldLocation> field_location(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    return machine.class_states().resolve_field(owner, name, descriptor, false);
}

[[nodiscard]] Status set_field(Machine& machine,
                               ObjectRef object,
                               std::string_view owner,
                               std::string_view name,
                               std::string_view descriptor,
                               Value value) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index, value);
}

[[nodiscard]] Result<ObjectRef> reference_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    auto value = machine.heap().field(object, location->index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Result<ObjectRef> empty_reference_array(
    Machine& machine,
    std::string descriptor) {
    return allocate_array(machine, std::move(descriptor), 0U,
                          Value::from_reference({}));
}

[[nodiscard]] Result<ObjectRef> empty_int_array(Machine& machine) {
    return allocate_array(machine, "[I", 0U, Value::from_int(0));
}

[[nodiscard]] Result<ObjectRef> create_empty_list(Machine& machine,
                                                  std::u16string name) {
    auto list = allocate_instance(machine, kEmptyList);
    if (!list) return std::unexpected(list.error());
    auto root = machine.pin_native_root(*list);
    if (!root) return std::unexpected(root.error());
    auto name_string = make_string(machine, std::move(name));
    if (!name_string) return std::unexpected(name_string.error());
    auto stored_closed = set_field(machine, *list, kEmptyList, "closed", "I",
                                   Value::from_int(0));
    auto stored_name = set_field(machine, *list, kEmptyList, "name",
                                 "Ljava/lang/String;",
                                 Value::from_reference(*name_string));
    if (!stored_closed) return std::unexpected(stored_closed.error());
    if (!stored_name) return std::unexpected(stored_name.error());
    return *list;
}

void register_pim_factory(NativeMethodRegistry& registry) {
    add(registry, std::string(kPim), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kPimImpl), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kPim), "getInstance",
        "()Ljavax/microedition/pim/PIM;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto pim = allocate_instance(machine, kPimImpl);
            if (!pim) return std::unexpected(pim.error());
            return std::optional<Value>(Value::from_reference(*pim));
        });
    const auto open = [](Machine& machine, std::span<const Value> arguments)
        -> Result<std::optional<Value>> {
        auto object = receiver(arguments);
        auto list_type = int_argument(arguments, 1U);
        auto mode = int_argument(arguments, 2U);
        if (!object || !list_type || !mode) {
            if (!object) return std::unexpected(object.error());
            if (!list_type) return std::unexpected(list_type.error());
            return std::unexpected(mode.error());
        }
        if (*mode < 1 || *mode > 3) {
            return fail_java("java/lang/IllegalArgumentException",
                             "invalid PIM access mode");
        }
        std::u16string name;
        if (*list_type == 1) name = u"Contacts";
        else if (*list_type == 2) name = u"Events";
        else if (*list_type == 3) name = u"ToDo";
        else {
            return fail_java("java/lang/IllegalArgumentException",
                             "unknown PIM list type");
        }
        auto list = create_empty_list(machine, std::move(name));
        if (!list) return std::unexpected(list.error());
        return std::optional<Value>(Value::from_reference(*list));
    };
    add(registry, std::string(kPimImpl), "openPIMList",
        "(II)Ljavax/microedition/pim/PIMList;", open);
    add(registry, std::string(kPimImpl), "openPIMList",
        "(IILjava/lang/String;)Ljavax/microedition/pim/PIMList;", open);

    add(registry, std::string(kPimImpl), "listPIMLists",
        "(I)[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto list_type = int_argument(arguments, 1U);
            if (!object || !list_type) {
                if (!object) return std::unexpected(object.error());
                return std::unexpected(list_type.error());
            }
            std::u16string name;
            if (*list_type == 1) name = u"Contacts";
            else if (*list_type == 2) name = u"Events";
            else if (*list_type == 3) name = u"ToDo";
            else {
                auto empty = empty_reference_array(machine,
                                                    "[Ljava/lang/String;");
                if (!empty) return std::unexpected(empty.error());
                return std::optional<Value>(Value::from_reference(*empty));
            }
            auto text = make_string(machine, std::move(name));
            if (!text) return std::unexpected(text.error());
            auto root = machine.pin_native_root(*text);
            if (!root) return std::unexpected(root.error());
            auto result = allocate_array(machine, "[Ljava/lang/String;", 1U,
                                         Value::from_reference({}));
            if (!result) return std::unexpected(result.error());
            auto stored = machine.heap().set_element(
                *result, 0U, Value::from_reference(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, std::string(kPimImpl), "supportedSerialFormats",
        "(I)[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto result = empty_reference_array(machine, "[Ljava/lang/String;");
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, std::string(kPimImpl), "fromSerialFormat",
        "(Ljava/io/InputStream;Ljava/lang/String;)[Ljavax/microedition/pim/PIMItem;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto result = empty_reference_array(
                machine, "[Ljavax/microedition/pim/PIMItem;");
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, std::string(kPimImpl), "toSerialFormat",
        "(Ljavax/microedition/pim/PIMItem;Ljava/io/OutputStream;Ljava/lang/String;Ljava/lang/String;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return fail_java("javax/microedition/pim/PIMException",
                             "PIM serialization requires a native Contacts bridge");
        });
}

void register_empty_list(NativeMethodRegistry& registry) {
    add(registry, std::string(kEmptyList), "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            auto closed = set_field(machine, *list, kEmptyList, "closed", "I",
                                    Value::from_int(1));
            if (!closed) return std::unexpected(closed.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kEmptyList), "getName",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            auto name = reference_field(machine, *list, kEmptyList, "name",
                                        "Ljava/lang/String;");
            if (!name) return std::unexpected(name.error());
            return std::optional<Value>(Value::from_reference(*name));
        });
    for (const auto& [name, descriptor] : {
             std::pair<std::string_view, std::string_view>{
                 "items", "()Ljava/util/Enumeration;"},
             {"items", "(Ljavax/microedition/pim/PIMItem;)Ljava/util/Enumeration;"},
             {"items", "(Ljava/lang/String;)Ljava/util/Enumeration;"},
             {"itemsByCategory", "(Ljava/lang/String;)Ljava/util/Enumeration;"}}) {
        add(registry, std::string(kEmptyList), std::string(name),
            std::string(descriptor),
            [](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto list = receiver(arguments);
                if (!list) return std::unexpected(list.error());
                return std::optional<Value>(Value::from_reference(*list));
            });
    }
    add(registry, std::string(kEmptyList), "hasMoreElements", "()Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, std::string(kEmptyList), "nextElement",
        "()Ljava/lang/Object;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            return fail_java("java/util/NoSuchElementException",
                             "empty PIM list");
        });
    for (const std::string_view method : {"getCategories"}) {
        add(registry, std::string(kEmptyList), std::string(method),
            "()[Ljava/lang/String;",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto list = receiver(arguments);
                if (!list) return std::unexpected(list.error());
                auto result = empty_reference_array(machine,
                                                    "[Ljava/lang/String;");
                if (!result) return std::unexpected(result.error());
                return std::optional<Value>(Value::from_reference(*result));
            });
    }
    add(registry, std::string(kEmptyList), "getSupportedFields", "()[I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            auto result = empty_int_array(machine);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, std::string(kEmptyList), "maxCategories", "()I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto list = receiver(arguments);
            if (!list) return std::unexpected(list.error());
            return std::optional<Value>(Value::from_int(0));
        });
}

} // namespace

void register_pim_natives(NativeMethodRegistry& registry) {
    register_pim_factory(registry);
    register_empty_list(registry);
}

} // namespace phoneme::vm
