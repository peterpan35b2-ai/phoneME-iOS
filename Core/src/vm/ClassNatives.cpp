#include "ClassNatives.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "phoneme/base/Types.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr u16 kAccInterface = 0x0200U;
constexpr usize kByteInputBufferField = 0;
constexpr usize kByteInputPositionField = 1;
constexpr usize kByteInputMarkField = 2;
constexpr usize kByteInputCountField = 3;

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
                    "class native has no receiver");
    }
    auto reference = arguments.front().as_reference();
    if (!reference || reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "class native receiver is null");
    }
    return *reference;
}

[[nodiscard]] Result<ObjectRef> create_string(Machine& machine,
                                              std::u16string text) {
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] std::u16string ascii_text(std::string_view text) {
    std::u16string result;
    result.reserve(text.size());
    for (const char character : text) {
        result.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

[[nodiscard]] Result<std::string> utf8_text(Machine& machine,
                                            ObjectRef string) {
    if (string.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "String argument is null");
    }
    auto text = machine.heap().string_value(string);
    if (!text) return std::unexpected(text.error());
    std::string result;
    result.reserve(text->size());
    for (usize index = 0; index < text->size(); ++index) {
        u32 code_point = static_cast<u16>((*text)[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < text->size()) {
            const u32 low = static_cast<u16>((*text)[index + 1U]);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U +
                    ((code_point - 0xD800U) << 10U) +
                    (low - 0xDC00U);
                ++index;
            }
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

[[nodiscard]] Result<ObjectRef> create_byte_input_stream(
    Machine& machine,
    std::span<const phoneme::u8> bytes) {
    auto array = machine.heap().allocate_array(
        "[B", bytes.size(), Value::from_int(0));
    if (!array && array.error().code == ErrorCode::overflow) {
        auto collected = machine.collect_garbage();
        if (!collected) return std::unexpected(collected.error());
        array = machine.heap().allocate_array(
            "[B", bytes.size(), Value::from_int(0));
    }
    if (!array) return std::unexpected(array.error());
    auto array_root = machine.pin_native_root(*array);
    if (!array_root) return std::unexpected(array_root.error());

    auto bytes_stored = machine.heap().write_byte_array(*array, 0U, bytes);
    if (!bytes_stored) return std::unexpected(bytes_stored.error());

    auto stream_root = machine.allocate_pinned_instance(
        "java/io/ByteArrayInputStream");
    if (!stream_root) return std::unexpected(stream_root.error());
    auto stream = stream_root->get();
    if (!stream) return std::unexpected(stream.error());
    auto buffer_stored = machine.heap().set_field(
        *stream, kByteInputBufferField, Value::from_reference(*array));
    auto position_stored = machine.heap().set_field(
        *stream, kByteInputPositionField, Value::from_int(0));
    auto mark_stored = machine.heap().set_field(
        *stream, kByteInputMarkField, Value::from_int(0));
    auto count_stored = machine.heap().set_field(
        *stream, kByteInputCountField,
        Value::from_int(static_cast<i32>(bytes.size())));
    if (!buffer_stored) return std::unexpected(buffer_stored.error());
    if (!position_stored) return std::unexpected(position_stored.error());
    if (!mark_stored) return std::unexpected(mark_stored.error());
    if (!count_stored) return std::unexpected(count_stored.error());
    return *stream;
}

[[nodiscard]] std::string normalized_class_name(std::string name) {
    std::replace(name.begin(), name.end(), '.', '/');
    return name;
}

[[nodiscard]] bool is_primitive_name(std::string_view name) noexcept {
    return name.size() == 1U &&
           std::string_view("ZBCSIJFDV").find(name.front()) !=
               std::string_view::npos;
}

[[nodiscard]] std::string display_class_name(std::string name) {
    if (name.size() == 1U) {
        switch (name.front()) {
        case 'Z': return "boolean";
        case 'B': return "byte";
        case 'C': return "char";
        case 'S': return "short";
        case 'I': return "int";
        case 'J': return "long";
        case 'F': return "float";
        case 'D': return "double";
        case 'V': return "void";
        default: break;
        }
    }
    std::replace(name.begin(), name.end(), '/', '.');
    return name;
}

[[nodiscard]] Result<std::string> array_component_name(
    std::string_view array_name) {
    if (array_name.empty() || array_name.front() != '[' ||
        array_name.size() < 2U) {
        return fail(ErrorCode::invalid_argument,
                    "class is not an array descriptor");
    }
    const std::string_view component = array_name.substr(1U);
    if (component.front() == '[') return std::string(component);
    if (component.front() == 'L' && component.size() >= 3U &&
        component.back() == ';') {
        return std::string(component.substr(1U, component.size() - 2U));
    }
    if (component.size() == 1U && is_primitive_name(component)) {
        return std::string(component);
    }
    return fail(ErrorCode::malformed_class,
                "array Class contains an invalid component descriptor");
}

} // namespace

void register_class_natives(NativeMethodRegistry& registry) {
    add(registry, "java/lang/Object", "getClass",
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

    add(registry, "java/lang/Class", "getName", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            auto string = create_string(
                machine, ascii_text(display_class_name(*class_name)));
            if (!string) return std::unexpected(string.error());
            return std::optional<Value>(Value::from_reference(*string));
        });
    add(registry, "java/lang/Class", "isArray", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            return std::optional<Value>(Value::from_int(
                !class_name->empty() && class_name->front() == '[' ? 1 : 0));
        });
    add(registry, "java/lang/Class", "isInterface", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            if ((!class_name->empty() && class_name->front() == '[') ||
                is_primitive_name(*class_name))
                return std::optional<Value>(Value::from_int(0));
            auto loaded = machine.classes().load(*class_name);
            if (!loaded) return std::unexpected(loaded.error());
            return std::optional<Value>(Value::from_int(
                ((*loaded)->access_flags() & kAccInterface) != 0U ? 1 : 0));
        });
    add(registry, "java/lang/Class", "isInstance",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            auto object = arguments[1].as_reference();
            if (!mirror) return std::unexpected(mirror.error());
            if (!object) return std::unexpected(object.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            if (is_primitive_name(*class_name))
                return std::optional<Value>(Value::from_int(0));
            auto instance = machine.object_is_instance(*object, *class_name);
            if (!instance) return std::unexpected(instance.error());
            return std::optional<Value>(Value::from_int(*instance ? 1 : 0));
        });
    add(registry, "java/lang/Class", "isAssignableFrom",
        "(Ljava/lang/Class;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto target_mirror = receiver(arguments);
            auto source_mirror = arguments[1].as_reference();
            if (!target_mirror) return std::unexpected(target_mirror.error());
            if (!source_mirror || source_mirror->is_null())
                return fail_java("java/lang/NullPointerException",
                                 "Class.isAssignableFrom source is null");
            auto target = machine.mirrored_class_name(*target_mirror);
            auto source = machine.mirrored_class_name(*source_mirror);
            if (!target) return std::unexpected(target.error());
            if (!source) return std::unexpected(source.error());
            if (is_primitive_name(*target) || is_primitive_name(*source)) {
                return std::optional<Value>(Value::from_int(
                    *target == *source ? 1 : 0));
            }
            auto assignable = machine.classes().is_assignable(*source, *target);
            if (!assignable) return std::unexpected(assignable.error());
            return std::optional<Value>(Value::from_int(*assignable ? 1 : 0));
        });
    add(registry, "java/lang/Class", "getSuperclass",
        "()Ljava/lang/Class;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            if (is_primitive_name(*class_name))
                return std::optional<Value>(Value::from_reference({}));
            std::string super_name;
            if (!class_name->empty() && class_name->front() == '[') {
                super_name = "java/lang/Object";
            } else {
                auto loaded = machine.classes().load(*class_name);
                if (!loaded) return std::unexpected(loaded.error());
                if (((*loaded)->access_flags() & kAccInterface) != 0U ||
                    (*loaded)->super_name().empty()) {
                    return std::optional<Value>(Value::from_reference({}));
                }
                super_name = (*loaded)->super_name();
            }
            auto super_mirror = machine.class_mirror(super_name);
            if (!super_mirror) return std::unexpected(super_mirror.error());
            return std::optional<Value>(Value::from_reference(*super_mirror));
        });
    add(registry, "java/lang/Class", "getComponentType",
        "()Ljava/lang/Class;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            if (class_name->empty() || class_name->front() != '[')
                return std::optional<Value>(Value::from_reference({}));
            auto component = array_component_name(*class_name);
            if (!component) return std::unexpected(component.error());
            auto component_mirror = machine.class_mirror(*component);
            if (!component_mirror)
                return std::unexpected(component_mirror.error());
            return std::optional<Value>(
                Value::from_reference(*component_mirror));
        });
    add(registry, "java/lang/Class", "desiredAssertionStatus", "()Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, "java/lang/Class", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            if (!mirror) return std::unexpected(mirror.error());
            auto class_name = machine.mirrored_class_name(*mirror);
            if (!class_name) return std::unexpected(class_name.error());
            std::string text;
            if (is_primitive_name(*class_name)) {
                text = display_class_name(*class_name);
            } else {
                bool is_interface = false;
                if (class_name->empty() || class_name->front() != '[') {
                    auto loaded = machine.classes().load(*class_name);
                    if (!loaded) return std::unexpected(loaded.error());
                    is_interface =
                        ((*loaded)->access_flags() & kAccInterface) != 0U;
                }
                text = is_interface ? "interface " : "class ";
                text.append(display_class_name(*class_name));
            }
            auto string = create_string(machine, ascii_text(text));
            if (!string) return std::unexpected(string.error());
            return std::optional<Value>(Value::from_reference(*string));
        });
    add(registry, "java/lang/Class", "forName",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto name = arguments[0].as_reference();
            if (!name || name->is_null())
                return fail_java("java/lang/NullPointerException",
                                 "Class.forName name is null");
            auto encoded = utf8_text(machine, *name);
            if (!encoded) return std::unexpected(encoded.error());
            const std::string normalized = normalized_class_name(*encoded);
            if (normalized.empty())
                return fail_java("java/lang/ClassNotFoundException",
                                 "Class.forName name is empty");
            if (normalized.front() != '[') {
                auto loaded = machine.classes().load(normalized);
                if (!loaded) {
                    if (loaded.error().code == ErrorCode::class_not_found)
                        return fail_java("java/lang/ClassNotFoundException",
                                         loaded.error().message);
                    return std::unexpected(loaded.error());
                }
            }
            auto mirror = machine.class_mirror(normalized);
            if (!mirror) return std::unexpected(mirror.error());
            return std::optional<Value>(Value::from_reference(*mirror));
        });
    add(registry, "java/lang/Class", "getResourceAsStream",
        "(Ljava/lang/String;)Ljava/io/InputStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto mirror = receiver(arguments);
            auto name = arguments[1].as_reference();
            if (!mirror) return std::unexpected(mirror.error());
            if (!name || name->is_null())
                return fail_java("java/lang/NullPointerException",
                                 "resource name is null");
            auto class_name = machine.mirrored_class_name(*mirror);
            auto resource = utf8_text(machine, *name);
            if (!class_name) return std::unexpected(class_name.error());
            if (!resource) return std::unexpected(resource.error());
            if (resource->empty()) {
                // Class.getResourceAsStream("") denotes no resource. Returning
                // null is required here; forwarding an empty archive path to
                // the secure normalizer turns a harmless probe into an
                // unexpected SecurityException in legacy game engines.
                return std::optional<Value>(Value::from_reference({}));
            }
            std::string path;
            if (!resource->empty() && resource->front() == '/') {
                path.assign(resource->begin() + 1, resource->end());
            } else {
                const usize slash = class_name->rfind('/');
                if (slash != std::string::npos &&
                    (class_name->empty() || class_name->front() != '[')) {
                    path.assign(class_name->begin(),
                                class_name->begin() +
                                    static_cast<std::ptrdiff_t>(slash + 1U));
                }
                path.append(*resource);
            }
            auto bytes = machine.classes().read_resource(path);
            if (!bytes) {
                if (bytes.error().code == ErrorCode::class_not_found) {
                    if (std::getenv("PHONEME_TRACE_RESOURCE_MISS") != nullptr) {
                        std::fprintf(stderr,
                                     "[resource-miss] class=%s path=%s\n",
                                     class_name->c_str(), path.c_str());
                    }
                    return std::optional<Value>(Value::from_reference({}));
                }
                if (bytes.error().code == ErrorCode::invalid_argument)
                    return fail_java("java/lang/SecurityException",
                                     bytes.error().message);
                return std::unexpected(bytes.error());
            }
            auto stream = create_byte_input_stream(machine, *bytes);
            if (!stream) return std::unexpected(stream.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
}

} // namespace phoneme::vm
