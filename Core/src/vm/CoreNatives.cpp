#include "phoneme/vm/NativeMethodRegistry.hpp"
#include "phoneme/vm/Machine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "BluetoothNatives.hpp"
#include "CanvasNatives.hpp"
#include "ChoiceNatives.hpp"
#include "ClassNatives.hpp"
#include "ConnectionNatives.hpp"
#include "ConsoleNatives.hpp"
#include "FileNatives.hpp"
#include "GameCanvasNatives.hpp"
#include "GameApiNatives.hpp"
#include "GraphicsNatives.hpp"
#include "ImageNatives.hpp"
#include "IONatives.hpp"
#include "LcduiNatives.hpp"
#include "MathNatives.hpp"
#include "M3gNatives.hpp"
#include "MediaNatives.hpp"
#include "PushNatives.hpp"
#include "ReferenceNatives.hpp"
#include "RmsNatives.hpp"
#include "SecurityNatives.hpp"
#include "StringEncodingNatives.hpp"
#include "TimeNatives.hpp"
#include "UtilNatives.hpp"
#include "WrapperNatives.hpp"

namespace phoneme::vm {
namespace {

[[nodiscard]] Result<ObjectRef> require_receiver(
    std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "native instance method is missing its receiver");
    }
    auto reference = arguments.front().as_reference();
    if (!reference || reference->is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "native instance method receiver is null");
    }
    return *reference;
}

[[nodiscard]] i32 stable_identity_hash(ObjectRef reference) noexcept {
    const u64 mixed = reference.bits ^ (reference.bits >> 33U) ^
                      (reference.bits << 11U);
    return static_cast<i32>(static_cast<u32>(mixed ^ (mixed >> 32U)));
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

template <typename Number>
[[nodiscard]] Result<std::u16string> integral_text(Number value) {
    std::array<char, 64> buffer {};
    const auto converted = std::to_chars(buffer.data(),
                                         buffer.data() + buffer.size(),
                                         value);
    if (converted.ec != std::errc {}) {
        return fail(ErrorCode::internal_error,
                    "failed to format integral Java value");
    }
    return ascii_text(std::string_view(buffer.data(),
                                       static_cast<usize>(converted.ptr -
                                                          buffer.data())));
}

template <typename Number>
[[nodiscard]] Result<std::u16string> floating_text(Number value) {
    if (std::isnan(value)) {
        return std::u16string(u"NaN");
    }
    if (std::isinf(value)) {
        return std::signbit(value) ? std::u16string(u"-Infinity")
                                   : std::u16string(u"Infinity");
    }
    if (value == static_cast<Number>(0)) {
        return std::signbit(value) ? std::u16string(u"-0.0")
                                   : std::u16string(u"0.0");
    }

    std::array<char, 128> buffer {};
    const int length = std::snprintf(
        buffer.data(),
        buffer.size(),
        "%.*g",
        std::numeric_limits<Number>::max_digits10,
        static_cast<double>(value));
    if (length < 0 || static_cast<usize>(length) >= buffer.size()) {
        return fail(ErrorCode::internal_error,
                    "failed to format floating Java value");
    }
    std::string text(buffer.data(), static_cast<usize>(length));
    if (text.find_first_of(".eE") == std::string::npos) {
        text.append(".0");
    }
    return ascii_text(text);
}

[[nodiscard]] Result<ObjectRef> create_java_string(Machine& machine,
                                                   std::u16string text) {
    auto reference = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!reference) {
        return std::unexpected(reference.error());
    }
    auto attached = machine.heap().attach_string(*reference, std::move(text));
    if (!attached) {
        return std::unexpected(attached.error());
    }
    return *reference;
}

[[nodiscard]] Result<std::string> array_component_name(
    std::string_view descriptor) {
    if (descriptor.size() < 2U || descriptor.front() != '[') {
        return fail(ErrorCode::invalid_argument,
                    "object is not a Java array");
    }
    const std::string_view component = descriptor.substr(1);
    if (component.front() == '[') {
        return std::string(component);
    }
    if (component.front() == 'L' && component.size() >= 3U &&
        component.back() == ';') {
        return std::string(component.substr(1, component.size() - 2U));
    }
    if (component.size() == 1U &&
        std::string_view("ZCBSIFJD").find(component.front()) !=
            std::string_view::npos) {
        return std::string(component);
    }
    return fail(ErrorCode::malformed_class,
                "array contains an invalid component descriptor");
}

[[nodiscard]] Result<std::u16string> object_text(Machine& machine,
                                                 ObjectRef reference) {
    if (reference.is_null()) {
        return std::u16string(u"null");
    }
    auto class_name = machine.heap().class_name(reference);
    if (!class_name) {
        return std::unexpected(class_name.error());
    }
    if (*class_name == "java/lang/String" ||
        *class_name == "java/lang/StringBuilder" ||
        *class_name == "java/lang/StringBuffer") {
        return machine.heap().string_value(reference);
    }

    std::string printable = *class_name;
    std::replace(printable.begin(), printable.end(), '/', '.');
    printable.push_back('@');
    std::array<char, 16> hash_buffer {};
    const auto hash = static_cast<u32>(stable_identity_hash(reference));
    const auto converted = std::to_chars(hash_buffer.data(),
                                         hash_buffer.data() + hash_buffer.size(),
                                         hash,
                                         16);
    if (converted.ec != std::errc {}) {
        return fail(ErrorCode::internal_error,
                    "failed to format Java identity hash");
    }
    printable.append(hash_buffer.data(), converted.ptr);
    return ascii_text(printable);
}

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    const auto registered = registry.register_method(std::move(owner),
                                                     std::move(name),
                                                     std::move(descriptor),
                                                     std::move(method));
    if (!registered) {
        std::terminate();
    }
}

[[nodiscard]] Result<std::optional<Value>> append_builder_text(
    Machine& machine,
    std::span<const Value> arguments,
    std::u16string suffix) {
    auto receiver = require_receiver(arguments);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    auto text = machine.heap().string_value(*receiver);
    if (!text) {
        return std::unexpected(text.error());
    }
    if (suffix.size() > text->max_size() - text->size()) {
        return fail(ErrorCode::overflow,
                    "Java string builder exceeds its maximum size");
    }
    text->append(suffix);
    auto stored = machine.heap().attach_string(*receiver, std::move(*text));
    if (!stored) {
        return std::unexpected(stored.error());
    }
    return std::optional<Value>(Value::from_reference(*receiver));
}

[[nodiscard]] Result<std::u16string> char_array_slice(
    Machine& machine,
    ObjectRef array,
    i32 offset,
    i32 count);

void register_text_builder(NativeMethodRegistry& registry,
                           std::string class_name) {
    const std::string return_descriptor = "L" + class_name + ";";

    add(registry, class_name, "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto attached = machine.heap().attach_string(*receiver, {});
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });
    add(registry, class_name, "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder capacity constructor expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto capacity = arguments[1].as_int();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!capacity) {
                return std::unexpected(capacity.error());
            }
            if (*capacity < 0) {
                return fail_java("java/lang/NegativeArraySizeException",
                                 "string builder capacity is negative");
            }
            auto attached = machine.heap().attach_string(*receiver, {});
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });
    add(registry, class_name, "<init>", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder String constructor expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto source = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!source || source->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "string builder String constructor received null");
            }
            auto text = machine.heap().string_value(*source);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto attached = machine.heap().attach_string(*receiver,
                                                         std::move(*text));
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });

    add(registry, class_name, "append",
        "(Ljava/lang/String;)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(String) expects one argument");
            }
            auto source = arguments[1].as_reference();
            if (!source) {
                return std::unexpected(source.error());
            }
            auto text = object_text(machine, *source);
            if (!text) {
                return std::unexpected(text.error());
            }
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append",
        "(Ljava/lang/Object;)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(Object) expects one argument");
            }
            auto source = arguments[1].as_reference();
            if (!source) {
                return std::unexpected(source.error());
            }
            auto text = object_text(machine, *source);
            if (!text) {
                return std::unexpected(text.error());
            }
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append", "(Z)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(boolean) expects one argument");
            }
            auto value = arguments[1].as_int();
            if (!value) {
                return std::unexpected(value.error());
            }
            return append_builder_text(machine, arguments,
                                       *value == 0 ? std::u16string(u"false")
                                                   : std::u16string(u"true"));
        });
    add(registry, class_name, "append", "(C)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(char) expects one argument");
            }
            auto value = arguments[1].as_int();
            if (!value) {
                return std::unexpected(value.error());
            }
            return append_builder_text(
                machine, arguments,
                std::u16string(1, static_cast<char16_t>(
                    static_cast<u16>(*value))));
        });
    add(registry, class_name, "append", "(I)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(int) expects one argument");
            }
            auto value = arguments[1].as_int();
            if (!value) {
                return std::unexpected(value.error());
            }
            auto text = integral_text(*value);
            if (!text) {
                return std::unexpected(text.error());
            }
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append", "(J)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(long) expects one argument");
            }
            auto value = arguments[1].as_long();
            if (!value) {
                return std::unexpected(value.error());
            }
            auto text = integral_text(*value);
            if (!text) {
                return std::unexpected(text.error());
            }
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append", "(F)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(float) expects one argument");
            }
            auto value = arguments[1].as_float();
            if (!value) {
                return std::unexpected(value.error());
            }
            auto text = floating_text(*value);
            if (!text) {
                return std::unexpected(text.error());
            }
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append", "(D)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(double) expects one argument");
            }
            auto value = arguments[1].as_double();
            if (!value) {
                return std::unexpected(value.error());
            }
            auto text = floating_text(*value);
            if (!text) {
                return std::unexpected(text.error());
            }
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append", "([C)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(char[]) expects one argument");
            }
            auto array = arguments[1].as_reference();
            if (!array) return std::unexpected(array.error());
            auto length = machine.heap().array_length(*array);
            if (!length) return std::unexpected(length.error());
            if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "string builder char[] exceeds int range");
            }
            auto text = char_array_slice(
                machine, *array, 0, static_cast<i32>(*length));
            if (!text) return std::unexpected(text.error());
            return append_builder_text(machine, arguments, std::move(*text));
        });
    add(registry, class_name, "append", "([CII)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 4U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder append(char[],int,int) arguments are invalid");
            }
            auto array = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!array) return std::unexpected(array.error());
            if (!offset) return std::unexpected(offset.error());
            if (!length) return std::unexpected(length.error());
            auto text = char_array_slice(
                machine, *array, *offset, *length);
            if (!text) return std::unexpected(text.error());
            return append_builder_text(machine, arguments, std::move(*text));
        });

    add(registry, class_name, "length", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            if (text->size() > static_cast<usize>(
                                   std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "string builder length exceeds int range");
            }
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(text->size())));
        });
    add(registry, class_name, "charAt", "(I)C",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder charAt expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto index = arguments[1].as_int();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!index) {
                return std::unexpected(index.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            if (*index < 0 || static_cast<usize>(*index) >= text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder charAt index is out of range");
            }
            return std::optional<Value>(Value::from_int(static_cast<i32>(
                static_cast<u16>((*text)[static_cast<usize>(*index)]))));
        });
    add(registry, class_name, "setCharAt", "(IC)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder setCharAt arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto index = arguments[1].as_int();
            auto character = arguments[2].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!index) return std::unexpected(index.error());
            if (!character) return std::unexpected(character.error());
            auto text = machine.heap().string_value(*receiver);
            if (!text) return std::unexpected(text.error());
            if (*index < 0 || static_cast<usize>(*index) >= text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder setCharAt index is out of range");
            }
            (*text)[static_cast<usize>(*index)] = static_cast<char16_t>(
                static_cast<u16>(*character));
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, class_name, "getChars", "(II[CI)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 5U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder getChars arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto source_begin = arguments[1].as_int();
            auto source_end = arguments[2].as_int();
            auto destination = arguments[3].as_reference();
            auto destination_begin = arguments[4].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!source_begin) return std::unexpected(source_begin.error());
            if (!source_end) return std::unexpected(source_end.error());
            if (!destination || destination->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "string builder getChars destination is null");
            }
            if (!destination_begin) {
                return std::unexpected(destination_begin.error());
            }
            auto text = machine.heap().string_value(*receiver);
            auto destination_class = machine.heap().class_name(*destination);
            auto destination_length = machine.heap().array_length(*destination);
            if (!text) return std::unexpected(text.error());
            if (!destination_class || *destination_class != "[C" ||
                !destination_length) {
                return fail_java("java/lang/ArrayStoreException",
                                 "string builder getChars expects char[]");
            }
            if (*source_begin < 0 || *source_end < *source_begin ||
                static_cast<usize>(*source_end) > text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder getChars source range is invalid");
            }
            const i32 count = *source_end - *source_begin;
            if (*destination_begin < 0 ||
                static_cast<usize>(*destination_begin) > *destination_length ||
                static_cast<usize>(count) >
                    *destination_length - static_cast<usize>(*destination_begin)) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "string builder getChars destination range is invalid");
            }
            for (i32 index = 0; index < count; ++index) {
                const char16_t character = (*text)[static_cast<usize>(
                    *source_begin + index)];
                auto stored = machine.heap().set_element(
                    *destination,
                    static_cast<usize>(*destination_begin + index),
                    Value::from_int(static_cast<i32>(
                        static_cast<u16>(character))));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, class_name, "delete", "(II)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder delete arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto start = arguments[1].as_int();
            auto end = arguments[2].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!start) return std::unexpected(start.error());
            if (!end) return std::unexpected(end.error());
            auto text = machine.heap().string_value(*receiver);
            if (!text) return std::unexpected(text.error());
            if (*start < 0 || *start > *end ||
                static_cast<usize>(*start) > text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder delete range is invalid");
            }
            const usize first = static_cast<usize>(*start);
            const usize last = std::min(
                text->size(), static_cast<usize>(std::max(*end, 0)));
            text->erase(first, last - first);
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*receiver));
        });
    add(registry, class_name, "deleteCharAt", "(I)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder deleteCharAt expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto index = arguments[1].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!index) return std::unexpected(index.error());
            auto text = machine.heap().string_value(*receiver);
            if (!text) return std::unexpected(text.error());
            if (*index < 0 || static_cast<usize>(*index) >= text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder deleteCharAt index is out of range");
            }
            text->erase(static_cast<usize>(*index), 1U);
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*receiver));
        });
    add(registry, class_name, "insert",
        "(ILjava/lang/String;)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder insert(String) arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto offset = arguments[1].as_int();
            auto source = arguments[2].as_reference();
            if (!receiver) return std::unexpected(receiver.error());
            if (!offset) return std::unexpected(offset.error());
            if (!source) return std::unexpected(source.error());
            auto text = machine.heap().string_value(*receiver);
            auto inserted = object_text(machine, *source);
            if (!text) return std::unexpected(text.error());
            if (!inserted) return std::unexpected(inserted.error());
            if (*offset < 0 || static_cast<usize>(*offset) > text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder insert offset is out of range");
            }
            text->insert(static_cast<usize>(*offset), *inserted);
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*receiver));
        });
    add(registry, class_name, "insert", "(IC)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder insert(char) arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto offset = arguments[1].as_int();
            auto character = arguments[2].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!offset) return std::unexpected(offset.error());
            if (!character) return std::unexpected(character.error());
            auto text = machine.heap().string_value(*receiver);
            if (!text) return std::unexpected(text.error());
            if (*offset < 0 || static_cast<usize>(*offset) > text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder insert offset is out of range");
            }
            text->insert(text->begin() + *offset,
                         static_cast<char16_t>(static_cast<u16>(*character)));
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*receiver));
        });
    add(registry, class_name, "insert", "(II)" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder insert(int) arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto offset = arguments[1].as_int();
            auto value = arguments[2].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!offset) return std::unexpected(offset.error());
            if (!value) return std::unexpected(value.error());
            auto text = machine.heap().string_value(*receiver);
            auto inserted = integral_text(*value);
            if (!text) return std::unexpected(text.error());
            if (!inserted) return std::unexpected(inserted.error());
            if (*offset < 0 || static_cast<usize>(*offset) > text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder insert offset is out of range");
            }
            text->insert(static_cast<usize>(*offset), *inserted);
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*receiver));
        });
    add(registry, class_name, "reverse", "()" + return_descriptor,
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            auto text = machine.heap().string_value(*receiver);
            if (!text) return std::unexpected(text.error());
            std::reverse(text->begin(), text->end());
            for (usize index = 0; index + 1U < text->size(); ++index) {
                const u16 first = static_cast<u16>((*text)[index]);
                const u16 second = static_cast<u16>((*text)[index + 1U]);
                if (first >= 0xDC00U && first <= 0xDFFFU &&
                    second >= 0xD800U && second <= 0xDBFFU) {
                    std::swap((*text)[index], (*text)[index + 1U]);
                    ++index;
                }
            }
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*receiver));
        });
    add(registry, class_name, "ensureCapacity", "(I)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) return std::unexpected(receiver.error());
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder ensureCapacity expects one argument");
            }
            auto minimum = arguments[1].as_int();
            if (!minimum) return std::unexpected(minimum.error());
            // The VM stores text in a dynamically sized UTF-16 payload, so
            // reserve capacity is an implementation hint with no observable
            // effect. Validation and call compatibility are still preserved.
            return std::optional<Value> {};
        });
    add(registry, class_name, "setLength", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "string builder setLength expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto length = arguments[1].as_int();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!length) {
                return std::unexpected(length.error());
            }
            if (*length < 0) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "string builder length is negative");
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            text->resize(static_cast<usize>(*length), u'\0');
            auto stored = machine.heap().attach_string(*receiver,
                                                       std::move(*text));
            if (!stored) {
                return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, class_name, "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto string = create_java_string(machine, std::move(*text));
            if (!string) {
                return std::unexpected(string.error());
            }
            return std::optional<Value>(Value::from_reference(*string));
        });
}

[[nodiscard]] Result<std::u16string> char_array_slice(
    Machine& machine,
    ObjectRef array,
    i32 offset,
    i32 count) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "character array is null");
    }
    auto class_name = machine.heap().class_name(array);
    if (!class_name || *class_name != "[C") {
        return fail_java("java/lang/IllegalArgumentException",
                         "value is not a char array");
    }
    auto length = machine.heap().array_length(array);
    if (!length) {
        return std::unexpected(length.error());
    }
    if (offset < 0 || count < 0 ||
        static_cast<usize>(offset) > *length ||
        static_cast<usize>(count) > *length - static_cast<usize>(offset)) {
        return fail_java("java/lang/StringIndexOutOfBoundsException",
                         "character array slice is out of range");
    }
    std::u16string result;
    result.reserve(static_cast<usize>(count));
    for (i32 index = 0; index < count; ++index) {
        auto value = machine.heap().element(
            array, static_cast<usize>(offset + index));
        if (!value) {
            return std::unexpected(value.error());
        }
        auto character = value->as_int();
        if (!character) {
            return std::unexpected(character.error());
        }
        result.push_back(static_cast<char16_t>(
            static_cast<u16>(*character)));
    }
    return result;
}

[[nodiscard]] char16_t simple_case_fold(char16_t value) noexcept {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

[[nodiscard]] char16_t simple_case_upper(char16_t value) noexcept {
    if (value >= u'a' && value <= u'z') {
        return static_cast<char16_t>(value - (u'a' - u'A'));
    }
    return value;
}

enum class SplitAtomKind : u8 {
    literal,
    any,
    whitespace,
    digit,
    word,
    character_class,
};

struct SplitPattern final {
    SplitAtomKind kind {SplitAtomKind::literal};
    std::u16string literal;
    std::vector<std::pair<char16_t, char16_t>> ranges;
    bool negate_class {false};
    bool repeat {false};
};

[[nodiscard]] bool split_whitespace(char16_t value) noexcept {
    return value == u' ' || value == u'\t' || value == u'\n' ||
           value == u'\v' || value == u'\f' || value == u'\r';
}

[[nodiscard]] bool split_word(char16_t value) noexcept {
    return (value >= u'a' && value <= u'z') ||
           (value >= u'A' && value <= u'Z') ||
           (value >= u'0' && value <= u'9') || value == u'_';
}

[[nodiscard]] SplitPattern parse_split_pattern(std::u16string regex) {
    if (regex.size() >= 4U && regex.starts_with(u"\\Q") &&
        regex.ends_with(u"\\E")) {
        return SplitPattern {
            .kind = SplitAtomKind::literal,
            .literal = regex.substr(2U, regex.size() - 4U),
        };
    }

    bool repeat = false;
    if (regex.size() > 1U &&
        (regex.back() == u'+' || regex.back() == u'*')) {
        repeat = true;
        regex.pop_back();
    }
    if (regex == u".") {
        return SplitPattern {.kind = SplitAtomKind::any, .repeat = repeat};
    }
    if (regex == u"\\s") {
        return SplitPattern {
            .kind = SplitAtomKind::whitespace,
            .repeat = repeat,
        };
    }
    if (regex == u"\\d") {
        return SplitPattern {.kind = SplitAtomKind::digit, .repeat = repeat};
    }
    if (regex == u"\\w") {
        return SplitPattern {.kind = SplitAtomKind::word, .repeat = repeat};
    }
    if (regex.size() >= 2U && regex.front() == u'[' &&
        regex.back() == u']') {
        SplitPattern result {
            .kind = SplitAtomKind::character_class,
            .repeat = repeat,
        };
        usize index = 1U;
        if (index + 1U < regex.size() && regex[index] == u'^') {
            result.negate_class = true;
            ++index;
        }
        const usize end = regex.size() - 1U;
        while (index < end) {
            char16_t first = regex[index++];
            if (first == u'\\' && index < end) {
                first = regex[index++];
            }
            char16_t last = first;
            if (index + 1U < end && regex[index] == u'-') {
                ++index;
                last = regex[index++];
                if (last == u'\\' && index < end) {
                    last = regex[index++];
                }
                if (last < first) std::swap(first, last);
            }
            result.ranges.emplace_back(first, last);
        }
        if (!result.ranges.empty()) return result;
    }

    std::u16string literal;
    literal.reserve(regex.size());
    for (usize index = 0; index < regex.size(); ++index) {
        if (regex[index] == u'\\' && index + 1U < regex.size()) {
            literal.push_back(regex[++index]);
        } else {
            literal.push_back(regex[index]);
        }
    }
    return SplitPattern {
        .kind = SplitAtomKind::literal,
        .literal = std::move(literal),
    };
}

[[nodiscard]] bool split_atom_matches(const SplitPattern& pattern,
                                      char16_t value) noexcept {
    switch (pattern.kind) {
      case SplitAtomKind::any:
        return true;
      case SplitAtomKind::whitespace:
        return split_whitespace(value);
      case SplitAtomKind::digit:
        return value >= u'0' && value <= u'9';
      case SplitAtomKind::word:
        return split_word(value);
      case SplitAtomKind::character_class: {
        bool matches = false;
        for (const auto [first, last] : pattern.ranges) {
            if (value >= first && value <= last) {
                matches = true;
                break;
            }
        }
        return pattern.negate_class ? !matches : matches;
      }
      case SplitAtomKind::literal:
        return false;
    }
    return false;
}

[[nodiscard]] std::optional<std::pair<usize, usize>> next_split_match(
    std::u16string_view text,
    const SplitPattern& pattern,
    usize search_from) noexcept {
    if (pattern.kind == SplitAtomKind::literal) {
        if (pattern.literal.empty()) {
            if (search_from < text.size()) return {{search_from, 0U}};
            return std::nullopt;
        }
        const usize position = text.find(pattern.literal, search_from);
        if (position == std::u16string_view::npos) return std::nullopt;
        return {{position, pattern.literal.size()}};
    }

    for (usize position = search_from; position < text.size(); ++position) {
        if (!split_atom_matches(pattern, text[position])) continue;
        usize length = 1U;
        if (pattern.repeat) {
            while (position + length < text.size() &&
                   split_atom_matches(pattern, text[position + length])) {
                ++length;
            }
        }
        return {{position, length}};
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::u16string> split_java_text(
    std::u16string_view text,
    const SplitPattern& pattern) {
    if (pattern.kind == SplitAtomKind::literal && pattern.literal.empty()) {
        std::vector<std::u16string> characters;
        characters.reserve(text.size());
        for (const char16_t character : text) {
            characters.emplace_back(1U, character);
        }
        if (characters.empty()) characters.emplace_back();
        return characters;
    }

    std::vector<std::u16string> parts;
    usize cursor = 0U;
    usize search_from = 0U;
    bool matched = false;
    while (auto match = next_split_match(text, pattern, search_from)) {
        matched = true;
        parts.emplace_back(text.substr(cursor, match->first - cursor));
        cursor = match->first + match->second;
        search_from = cursor;
        if (search_from > text.size()) break;
    }
    if (!matched) {
        parts.emplace_back(text);
        return parts;
    }
    parts.emplace_back(text.substr(cursor));
    while (!parts.empty() && parts.back().empty()) parts.pop_back();
    return parts;
}

void register_string_extensions(NativeMethodRegistry& registry) {
    add(registry, "java/lang/String", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto attached = machine.heap().attach_string(*receiver, {});
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/lang/String", "<init>",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String copy constructor expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto source = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!source || source->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String copy source is null");
            }
            auto text = machine.heap().string_value(*source);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto attached = machine.heap().attach_string(*receiver,
                                                         std::move(*text));
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/lang/String", "<init>",
        "(Ljava/lang/StringBuffer;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String buffer constructor expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto source = arguments[1].as_reference();
            if (!receiver) return std::unexpected(receiver.error());
            if (!source || source->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String buffer source is null");
            }
            auto source_class = machine.heap().class_name(*source);
            if (!source_class) return std::unexpected(source_class.error());
            if (*source_class != "java/lang/StringBuffer") {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String buffer source has the wrong class");
            }
            auto text = machine.heap().string_value(*source);
            if (!text) return std::unexpected(text.error());
            auto attached = machine.heap().attach_string(*receiver,
                                                         std::move(*text));
            if (!attached) return std::unexpected(attached.error());
            return std::optional<Value> {};
        });
    add(registry, "java/lang/String", "<init>", "([C)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String char-array constructor expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto array = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!array) {
                return std::unexpected(array.error());
            }
            auto length = machine.heap().array_length(*array);
            if (!length) {
                return std::unexpected(length.error());
            }
            if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail_java("java/lang/OutOfMemoryError",
                                 "String char array is too large");
            }
            auto text = char_array_slice(machine, *array, 0,
                                         static_cast<i32>(*length));
            if (!text) {
                return std::unexpected(text.error());
            }
            auto attached = machine.heap().attach_string(*receiver,
                                                         std::move(*text));
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/lang/String", "<init>", "([CII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 4U) {
                return fail(ErrorCode::invalid_argument,
                            "String char-array slice constructor expects three arguments");
            }
            auto receiver = require_receiver(arguments);
            auto array = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto count = arguments[3].as_int();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!array || !offset || !count) {
                return fail(ErrorCode::invalid_argument,
                            "String char-array slice arguments are invalid");
            }
            auto text = char_array_slice(machine, *array, *offset, *count);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto attached = machine.heap().attach_string(*receiver,
                                                         std::move(*text));
            if (!attached) {
                return std::unexpected(attached.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "java/lang/String", "getChars", "(II[CI)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 5U) {
                return fail(ErrorCode::invalid_argument,
                            "String.getChars expects four arguments");
            }
            auto receiver = require_receiver(arguments);
            auto begin = arguments[1].as_int();
            auto end = arguments[2].as_int();
            auto destination = arguments[3].as_reference();
            auto destination_begin = arguments[4].as_int();
            if (!receiver || !begin || !end || !destination ||
                !destination_begin) {
                return fail(ErrorCode::invalid_argument,
                            "String.getChars arguments are invalid");
            }
            if (destination->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String.getChars destination is null");
            }
            auto text = machine.heap().string_value(*receiver);
            auto destination_class = machine.heap().class_name(*destination);
            auto destination_length = machine.heap().array_length(*destination);
            if (!text) {
                return std::unexpected(text.error());
            }
            if (!destination_class || *destination_class != "[C" ||
                !destination_length) {
                return fail_java("java/lang/ArrayStoreException",
                                 "String.getChars destination is not char[]");
            }
            if (*begin < 0 || *end < *begin ||
                static_cast<usize>(*end) > text->size() ||
                *destination_begin < 0) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "String.getChars source range is invalid");
            }
            const usize count = static_cast<usize>(*end - *begin);
            const usize destination_offset =
                static_cast<usize>(*destination_begin);
            if (destination_offset > *destination_length ||
                count > *destination_length - destination_offset) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "String.getChars destination range is invalid");
            }
            for (usize index = 0; index < count; ++index) {
                auto stored = machine.heap().set_element(
                    *destination,
                    destination_offset + index,
                    Value::from_int(static_cast<i32>(static_cast<u16>(
                        (*text)[static_cast<usize>(*begin) + index]))));
                if (!stored) {
                    return std::unexpected(stored.error());
                }
            }
            return std::optional<Value> {};
        });
    add(registry, "java/lang/String", "toCharArray", "()[C",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto array = machine.heap().allocate_array(
                "[C", text->size(), Value::from_int(0));
            if (!array) {
                return std::unexpected(array.error());
            }
            for (usize index = 0; index < text->size(); ++index) {
                auto stored = machine.heap().set_element(
                    *array, index,
                    Value::from_int(static_cast<i32>(
                        static_cast<u16>((*text)[index]))));
                if (!stored) {
                    return std::unexpected(stored.error());
                }
            }
            return std::optional<Value>(Value::from_reference(*array));
        });

    add(registry, "java/lang/String", "equalsIgnoreCase",
        "(Ljava/lang/String;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String.equalsIgnoreCase expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto other = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!other || other->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto left = machine.heap().string_value(*receiver);
            auto right = machine.heap().string_value(*other);
            if (!left || !right) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String.equalsIgnoreCase argument is not String");
            }
            bool equal = left->size() == right->size();
            for (usize index = 0; equal && index < left->size(); ++index) {
                equal = simple_case_fold((*left)[index]) ==
                        simple_case_fold((*right)[index]);
            }
            return std::optional<Value>(Value::from_int(equal ? 1 : 0));
        });
    add(registry, "java/lang/String", "regionMatches",
        "(ZILjava/lang/String;II)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 6U) {
                return fail(ErrorCode::invalid_argument,
                            "String.regionMatches arguments are invalid");
            }
            auto receiver = require_receiver(arguments);
            auto ignore_case = arguments[1].as_int();
            auto receiver_offset = arguments[2].as_int();
            auto other = arguments[3].as_reference();
            auto other_offset = arguments[4].as_int();
            auto length = arguments[5].as_int();
            if (!receiver) return std::unexpected(receiver.error());
            if (!ignore_case) return std::unexpected(ignore_case.error());
            if (!receiver_offset) {
                return std::unexpected(receiver_offset.error());
            }
            if (!other || other->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String.regionMatches argument is null");
            }
            if (!other_offset) return std::unexpected(other_offset.error());
            if (!length) return std::unexpected(length.error());
            auto left = machine.heap().string_value(*receiver);
            auto right = machine.heap().string_value(*other);
            if (!left || !right) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String.regionMatches argument is not String");
            }
            if (*receiver_offset < 0 || *other_offset < 0 || *length < 0 ||
                static_cast<usize>(*receiver_offset) > left->size() ||
                static_cast<usize>(*other_offset) > right->size() ||
                static_cast<usize>(*length) >
                    left->size() - static_cast<usize>(*receiver_offset) ||
                static_cast<usize>(*length) >
                    right->size() - static_cast<usize>(*other_offset)) {
                return std::optional<Value>(Value::from_int(0));
            }
            bool equal = true;
            for (i32 index = 0; equal && index < *length; ++index) {
                char16_t left_character = (*left)[static_cast<usize>(
                    *receiver_offset + index)];
                char16_t right_character = (*right)[static_cast<usize>(
                    *other_offset + index)];
                if (*ignore_case != 0) {
                    left_character = simple_case_fold(left_character);
                    right_character = simple_case_fold(right_character);
                }
                equal = left_character == right_character;
            }
            return std::optional<Value>(Value::from_int(equal ? 1 : 0));
        });
    add(registry, "java/lang/String", "compareTo",
        "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String.compareTo expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto other = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!other || other->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String.compareTo argument is null");
            }
            auto left = machine.heap().string_value(*receiver);
            auto right = machine.heap().string_value(*other);
            if (!left || !right) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String.compareTo argument is not String");
            }
            const usize common = std::min(left->size(), right->size());
            for (usize index = 0; index < common; ++index) {
                if ((*left)[index] != (*right)[index]) {
                    return std::optional<Value>(Value::from_int(
                        static_cast<i32>(static_cast<u16>((*left)[index])) -
                        static_cast<i32>(static_cast<u16>((*right)[index]))));
                }
            }
            const auto difference = static_cast<i64>(left->size()) -
                                    static_cast<i64>(right->size());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(difference)));
        });

    const auto register_prefix = [&registry](bool with_offset) {
        add(registry, "java/lang/String", "startsWith",
            with_offset ? "(Ljava/lang/String;I)Z"
                        : "(Ljava/lang/String;)Z",
            [with_offset](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                const usize expected = with_offset ? 3U : 2U;
                if (arguments.size() != expected) {
                    return fail(ErrorCode::invalid_argument,
                                "String.startsWith argument count is invalid");
                }
                auto receiver = require_receiver(arguments);
                auto prefix = arguments[1].as_reference();
                i32 offset = 0;
                if (with_offset) {
                    auto parsed_offset = arguments[2].as_int();
                    if (!parsed_offset) {
                        return std::unexpected(parsed_offset.error());
                    }
                    offset = *parsed_offset;
                }
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                if (!prefix || prefix->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "String.startsWith prefix is null");
                }
                auto text = machine.heap().string_value(*receiver);
                auto prefix_text = machine.heap().string_value(*prefix);
                if (!text || !prefix_text) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "String.startsWith prefix is not String");
                }
                const bool matches = offset >= 0 &&
                    static_cast<usize>(offset) <= text->size() &&
                    prefix_text->size() <=
                        text->size() - static_cast<usize>(offset) &&
                    std::equal(prefix_text->begin(), prefix_text->end(),
                               text->begin() + offset);
                return std::optional<Value>(Value::from_int(matches ? 1 : 0));
            });
    };
    register_prefix(false);
    register_prefix(true);
    add(registry, "java/lang/String", "endsWith",
        "(Ljava/lang/String;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String.endsWith expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto suffix = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!suffix || suffix->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String.endsWith suffix is null");
            }
            auto text = machine.heap().string_value(*receiver);
            auto suffix_text = machine.heap().string_value(*suffix);
            if (!text || !suffix_text) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String.endsWith suffix is not String");
            }
            const bool matches = suffix_text->size() <= text->size() &&
                std::equal(suffix_text->begin(), suffix_text->end(),
                           text->end() -
                               static_cast<std::ptrdiff_t>(suffix_text->size()));
            return std::optional<Value>(Value::from_int(matches ? 1 : 0));
        });

    const auto register_char_search = [&registry](bool reverse,
                                                  bool with_offset) {
        const std::string name = reverse ? "lastIndexOf" : "indexOf";
        add(registry, "java/lang/String", name,
            with_offset ? "(II)I" : "(I)I",
            [reverse, with_offset](Machine& machine,
                                   std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto receiver = require_receiver(arguments);
                auto character = arguments[1].as_int();
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                if (!character) {
                    return std::unexpected(character.error());
                }
                auto text = machine.heap().string_value(*receiver);
                if (!text) {
                    return std::unexpected(text.error());
                }
                i32 from = reverse
                    ? static_cast<i32>(std::min<usize>(
                          text->size(),
                          static_cast<usize>(
                              std::numeric_limits<i32>::max()))) - 1
                    : 0;
                if (with_offset) {
                    auto parsed = arguments[2].as_int();
                    if (!parsed) {
                        return std::unexpected(parsed.error());
                    }
                    from = *parsed;
                }
                const char16_t target = static_cast<char16_t>(
                    static_cast<u16>(*character));
                usize position = std::u16string::npos;
                if (reverse) {
                    if (from >= 0 && !text->empty()) {
                        const usize start = std::min(
                            static_cast<usize>(from), text->size() - 1U);
                        position = text->rfind(target, start);
                    }
                } else {
                    const usize start = from <= 0 ? 0U
                        : static_cast<usize>(from);
                    if (start <= text->size()) {
                        position = text->find(target, start);
                    }
                }
                return std::optional<Value>(Value::from_int(
                    position == std::u16string::npos
                        ? -1
                        : static_cast<i32>(position)));
            });
    };
    register_char_search(false, false);
    register_char_search(false, true);
    register_char_search(true, false);
    register_char_search(true, true);

    const auto register_string_search = [&registry](bool reverse,
                                                    bool with_offset) {
        const std::string name = reverse ? "lastIndexOf" : "indexOf";
        add(registry, "java/lang/String", name,
            with_offset ? "(Ljava/lang/String;I)I"
                        : "(Ljava/lang/String;)I",
            [reverse, with_offset](Machine& machine,
                                   std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto receiver = require_receiver(arguments);
                auto needle = arguments[1].as_reference();
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                if (!needle || needle->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "String search argument is null");
                }
                auto text = machine.heap().string_value(*receiver);
                auto needle_text = machine.heap().string_value(*needle);
                if (!text || !needle_text) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "String search argument is not String");
                }
                i32 from = reverse
                    ? static_cast<i32>(std::min<usize>(
                          text->size(),
                          static_cast<usize>(
                              std::numeric_limits<i32>::max())))
                    : 0;
                if (with_offset) {
                    auto parsed = arguments[2].as_int();
                    if (!parsed) {
                        return std::unexpected(parsed.error());
                    }
                    from = *parsed;
                }
                usize position = std::u16string::npos;
                if (reverse) {
                    if (from >= 0) {
                        position = text->rfind(
                            *needle_text,
                            std::min(static_cast<usize>(from), text->size()));
                    }
                } else {
                    const usize start = from <= 0 ? 0U
                        : static_cast<usize>(from);
                    if (start <= text->size()) {
                        position = text->find(*needle_text, start);
                    }
                }
                return std::optional<Value>(Value::from_int(
                    position == std::u16string::npos
                        ? -1
                        : static_cast<i32>(position)));
            });
    };
    register_string_search(false, false);
    register_string_search(false, true);
    register_string_search(true, false);
    register_string_search(true, true);

    const auto register_substring = [&registry](bool with_end) {
        add(registry, "java/lang/String", "substring",
            with_end ? "(II)Ljava/lang/String;"
                     : "(I)Ljava/lang/String;",
            [with_end](Machine& machine,
                       std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto receiver = require_receiver(arguments);
                auto begin = arguments[1].as_int();
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                if (!begin) {
                    return std::unexpected(begin.error());
                }
                auto text = machine.heap().string_value(*receiver);
                if (!text) {
                    return std::unexpected(text.error());
                }
                i32 end = static_cast<i32>(text->size());
                if (with_end) {
                    auto parsed_end = arguments[2].as_int();
                    if (!parsed_end) {
                        return std::unexpected(parsed_end.error());
                    }
                    end = *parsed_end;
                }
                if (*begin < 0 || end < *begin ||
                    static_cast<usize>(end) > text->size()) {
                    return fail_java(
                        "java/lang/StringIndexOutOfBoundsException",
                        "String.substring range is invalid: begin=" +
                            std::to_string(*begin) + " end=" +
                            std::to_string(end) + " length=" +
                            std::to_string(text->size()));
                }
                auto result = create_java_string(
                    machine,
                    text->substr(static_cast<usize>(*begin),
                                 static_cast<usize>(end - *begin)));
                if (!result) {
                    return std::unexpected(result.error());
                }
                return std::optional<Value>(Value::from_reference(*result));
            });
    };
    register_substring(false);
    register_substring(true);

    add(registry, "java/lang/String", "concat",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            auto other = arguments[1].as_reference();
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (!other || other->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String.concat argument is null");
            }
            auto left = machine.heap().string_value(*receiver);
            auto right = machine.heap().string_value(*other);
            if (!left || !right) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String.concat argument is not String");
            }
            left->append(*right);
            auto result = create_java_string(machine, std::move(*left));
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, "java/lang/String", "replace",
        "(CC)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            auto old_character = arguments[1].as_int();
            auto new_character = arguments[2].as_int();
            if (!receiver || !old_character || !new_character) {
                return fail(ErrorCode::invalid_argument,
                            "String.replace arguments are invalid");
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            std::replace(text->begin(), text->end(),
                         static_cast<char16_t>(static_cast<u16>(*old_character)),
                         static_cast<char16_t>(static_cast<u16>(*new_character)));
            auto result = create_java_string(machine, std::move(*text));
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, "java/lang/String", "trim", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            usize begin = 0;
            usize end = text->size();
            while (begin < end && (*text)[begin] <= u' ') {
                ++begin;
            }
            while (end > begin && (*text)[end - 1U] <= u' ') {
                --end;
            }
            auto result = create_java_string(
                machine, text->substr(begin, end - begin));
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });

    const auto register_string_case = [&registry](
        const char* name,
        char16_t (*convert)(char16_t) noexcept) {
        add(registry, "java/lang/String", name,
            "()Ljava/lang/String;",
            [convert](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto receiver = require_receiver(arguments);
                if (!receiver) return std::unexpected(receiver.error());
                auto text = machine.heap().string_value(*receiver);
                if (!text) return std::unexpected(text.error());

                bool changed = false;
                for (char16_t& character : *text) {
                    const char16_t converted = convert(character);
                    changed = changed || converted != character;
                    character = converted;
                }
                if (!changed) {
                    return std::optional<Value>(
                        Value::from_reference(*receiver));
                }
                auto result = create_java_string(machine, std::move(*text));
                if (!result) return std::unexpected(result.error());
                return std::optional<Value>(Value::from_reference(*result));
            });
    };
    register_string_case("toLowerCase", simple_case_fold);
    register_string_case("toUpperCase", simple_case_upper);

    add(registry, "java/lang/String", "split",
        "(Ljava/lang/String;)[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "String.split expects one argument");
            }
            auto receiver = require_receiver(arguments);
            auto regex = arguments[1].as_reference();
            if (!receiver) return std::unexpected(receiver.error());
            if (!regex || regex->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "String.split pattern is null");
            }
            auto text = machine.heap().string_value(*receiver);
            auto regex_text = machine.heap().string_value(*regex);
            if (!text || !regex_text) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "String.split pattern is not String");
            }

            auto parts = split_java_text(*text,
                                         parse_split_pattern(*regex_text));
            auto array = machine.heap().allocate_array(
                "[Ljava/lang/String;", parts.size(),
                Value::from_reference({}));
            if (!array) return std::unexpected(array.error());
            auto array_root = machine.pin_native_root(*array);
            if (!array_root) return std::unexpected(array_root.error());
            for (usize index = 0; index < parts.size(); ++index) {
                auto value = create_java_string(machine,
                                                std::move(parts[index]));
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(
                    *array, index, Value::from_reference(*value));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*array));
        });

    add(registry, "java/lang/String", "valueOf",
        "(Ljava/lang/Object;)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto reference = arguments[0].as_reference();
            if (!reference) {
                return std::unexpected(reference.error());
            }
            auto text = object_text(machine, *reference);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto result = create_java_string(machine, std::move(*text));
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, "java/lang/String", "valueOf", "([C)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto array = arguments[0].as_reference();
            if (!array) {
                return std::unexpected(array.error());
            }
            auto length = machine.heap().array_length(*array);
            if (!length) {
                return std::unexpected(length.error());
            }
            auto text = char_array_slice(machine, *array, 0,
                                         static_cast<i32>(*length));
            if (!text) {
                return std::unexpected(text.error());
            }
            auto result = create_java_string(machine, std::move(*text));
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, "java/lang/String", "valueOf", "([CII)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto array = arguments[0].as_reference();
            auto offset = arguments[1].as_int();
            auto count = arguments[2].as_int();
            if (!array || !offset || !count) {
                return fail(ErrorCode::invalid_argument,
                            "String.valueOf char slice arguments are invalid");
            }
            auto text = char_array_slice(machine, *array, *offset, *count);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto result = create_java_string(machine, std::move(*text));
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::optional<Value>(Value::from_reference(*result));
        });

    const auto register_value_of = [&registry](std::string descriptor,
                                               NativeMethod converter) {
        add(registry, "java/lang/String", "valueOf",
            std::move(descriptor), std::move(converter));
    };
    register_value_of("(Z)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_int();
            if (!value) return std::unexpected(value.error());
            auto result = create_java_string(
                machine, *value == 0 ? std::u16string(u"false")
                                     : std::u16string(u"true"));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    register_value_of("(C)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_int();
            if (!value) return std::unexpected(value.error());
            auto result = create_java_string(
                machine, std::u16string(1, static_cast<char16_t>(
                    static_cast<u16>(*value))));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    register_value_of("(I)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_int();
            if (!value) return std::unexpected(value.error());
            auto text = integral_text(*value);
            if (!text) return std::unexpected(text.error());
            auto result = create_java_string(machine, std::move(*text));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    register_value_of("(J)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_long();
            if (!value) return std::unexpected(value.error());
            auto text = integral_text(*value);
            if (!text) return std::unexpected(text.error());
            auto result = create_java_string(machine, std::move(*text));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    register_value_of("(F)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_float();
            if (!value) return std::unexpected(value.error());
            auto text = floating_text(*value);
            if (!text) return std::unexpected(text.error());
            auto result = create_java_string(machine, std::move(*text));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    register_value_of("(D)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_double();
            if (!value) return std::unexpected(value.error());
            auto text = floating_text(*value);
            if (!text) return std::unexpected(text.error());
            auto result = create_java_string(machine, std::move(*text));
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
}

} // namespace

void register_core_natives(NativeMethodRegistry& registry) {
    register_bluetooth_natives(registry);
    register_canvas_natives(registry);
    register_class_natives(registry);
    register_choice_natives(registry);
    register_connection_natives(registry);
    register_console_natives(registry);
    register_file_natives(registry);
    register_game_canvas_natives(registry);
    register_game_api_natives(registry);
    register_graphics_natives(registry);
    register_image_natives(registry);
    register_io_natives(registry);
    register_lcdui_natives(registry);
    register_math_natives(registry);
    register_m3g_natives(registry);
    register_media_natives(registry);
    register_push_natives(registry);
    register_reference_natives(registry);
    register_rms_natives(registry);
    register_security_natives(registry);
    register_string_encoding_natives(registry);
    register_time_natives(registry);
    register_util_natives(registry);
    register_wrapper_natives(registry);

    add(registry,
        "java/lang/Object",
        "<init>",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Object",
        "hashCode",
        "()I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            return std::optional<Value>(
                Value::from_int(stable_identity_hash(*receiver)));
        });

    add(registry,
        "java/lang/Object",
        "equals",
        "(Ljava/lang/Object;)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (arguments.size() != 2) {
                return fail(ErrorCode::invalid_argument,
                            "Object.equals expects one argument");
            }
            auto other = arguments[1].as_reference();
            if (!other) {
                return std::unexpected(other.error());
            }
            return std::optional<Value>(
                Value::from_int(*receiver == *other ? 1 : 0));
        });

    add(registry,
        "java/lang/Object",
        "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = object_text(machine, *receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            auto string = create_java_string(machine, std::move(*text));
            if (!string) {
                return std::unexpected(string.error());
            }
            return std::optional<Value>(Value::from_reference(*string));
        });

    add(registry,
        "java/lang/Object",
        "wait",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto waited = machine.wait_on_object(*receiver, std::nullopt);
            if (!waited) {
                return std::unexpected(waited.error());
            }
            if (*waited == MonitorWaitResult::interrupted) {
                return fail_java("java/lang/InterruptedException",
                                 "Object.wait was interrupted");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Object",
        "wait",
        "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "Object.wait(long) expects one timeout");
            }
            auto receiver = require_receiver(arguments);
            auto timeout = arguments[1].as_long();
            if (!receiver || !timeout) {
                return fail(ErrorCode::invalid_argument,
                            "Object.wait(long) arguments are invalid");
            }
            auto waited = machine.wait_on_object(*receiver, *timeout);
            if (!waited) {
                return std::unexpected(waited.error());
            }
            if (*waited == MonitorWaitResult::interrupted) {
                return fail_java("java/lang/InterruptedException",
                                 "Object.wait was interrupted");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Object",
        "notify",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto notified = machine.notify_object(*receiver, false);
            if (!notified) {
                return std::unexpected(notified.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Object",
        "notifyAll",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto notified = machine.notify_object(*receiver, true);
            if (!notified) {
                return std::unexpected(notified.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/String",
        "length",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            if (text->size() > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "Java String length exceeds int range");
            }
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(text->size())));
        });

    add(registry,
        "java/lang/String",
        "isEmpty",
        "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            return std::optional<Value>(Value::from_int(text->empty() ? 1 : 0));
        });

    add(registry,
        "java/lang/String",
        "charAt",
        "(I)C",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2) {
                return fail(ErrorCode::invalid_argument,
                            "String.charAt expects receiver and index");
            }
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto index = arguments[1].as_int();
            if (!index) {
                return std::unexpected(index.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            if (*index < 0 || static_cast<usize>(*index) >= text->size()) {
                return fail_java("java/lang/StringIndexOutOfBoundsException",
                                 "String.charAt index is out of range");
            }
            return std::optional<Value>(Value::from_int(static_cast<i32>(
                static_cast<u16>((*text)[static_cast<usize>(*index)]))));
        });

    add(registry,
        "java/lang/String",
        "equals",
        "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2) {
                return fail(ErrorCode::invalid_argument,
                            "String.equals expects receiver and argument");
            }
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto other = arguments[1].as_reference();
            if (!other) {
                return std::unexpected(other.error());
            }
            if (other->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto other_class = machine.heap().class_name(*other);
            if (!other_class || *other_class != "java/lang/String") {
                return std::optional<Value>(Value::from_int(0));
            }
            auto left = machine.heap().string_value(*receiver);
            auto right = machine.heap().string_value(*other);
            if (!left) {
                return std::unexpected(left.error());
            }
            if (!right) {
                return std::unexpected(right.error());
            }
            return std::optional<Value>(Value::from_int(*left == *right ? 1 : 0));
        });

    add(registry,
        "java/lang/String",
        "hashCode",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto text = machine.heap().string_value(*receiver);
            if (!text) {
                return std::unexpected(text.error());
            }
            u32 hash = 0;
            for (const char16_t character : *text) {
                hash = hash * 31U + static_cast<u16>(character);
            }
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(hash)));
        });

    add(registry,
        "java/lang/String",
        "intern",
        "()Ljava/lang/String;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            return std::optional<Value>(Value::from_reference(*receiver));
        });

    add(registry,
        "java/lang/String",
        "toString",
        "()Ljava/lang/String;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            return std::optional<Value>(Value::from_reference(*receiver));
        });

    register_string_extensions(registry);
    register_text_builder(registry, "java/lang/StringBuilder");
    register_text_builder(registry, "java/lang/StringBuffer");

    add(registry,
        "java/lang/System",
        "identityHashCode",
        "(Ljava/lang/Object;)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1) {
                return fail(ErrorCode::invalid_argument,
                            "identityHashCode expects one argument");
            }
            auto reference = arguments[0].as_reference();
            if (!reference) {
                return std::unexpected(reference.error());
            }
            return std::optional<Value>(Value::from_int(
                reference->is_null() ? 0 : stable_identity_hash(*reference)));
        });

    add(registry,
        "java/lang/System",
        "arraycopy",
        "(Ljava/lang/Object;ILjava/lang/Object;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 5U) {
                return fail(ErrorCode::invalid_argument,
                            "System.arraycopy expects five arguments");
            }
            auto source = arguments[0].as_reference();
            auto source_position = arguments[1].as_int();
            auto destination = arguments[2].as_reference();
            auto destination_position = arguments[3].as_int();
            auto length = arguments[4].as_int();
            if (!source || !source_position || !destination ||
                !destination_position || !length) {
                return fail(ErrorCode::invalid_argument,
                            "System.arraycopy argument kinds are invalid");
            }
            if (source->is_null() || destination->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "System.arraycopy received a null array");
            }
            auto source_class = machine.heap().class_name(*source);
            auto destination_class = machine.heap().class_name(*destination);
            if (!source_class || !destination_class) {
                return fail_java("java/lang/ArrayStoreException",
                                 "System.arraycopy requires array objects");
            }
            if (!source_class->starts_with('[') ||
                !destination_class->starts_with('[')) {
                return fail_java("java/lang/ArrayStoreException",
                                 "System.arraycopy requires array objects");
            }
            auto source_length = machine.heap().array_length(*source);
            auto destination_length = machine.heap().array_length(*destination);
            if (!source_length || !destination_length) {
                return fail_java("java/lang/ArrayStoreException",
                                 "System.arraycopy requires array objects");
            }
            if (*source_position < 0 || *destination_position < 0 ||
                *length < 0) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "System.arraycopy range is negative");
            }
            const usize source_start = static_cast<usize>(*source_position);
            const usize destination_start =
                static_cast<usize>(*destination_position);
            const usize count = static_cast<usize>(*length);
            if (source_start > *source_length ||
                count > *source_length - source_start ||
                destination_start > *destination_length ||
                count > *destination_length - destination_start) {
                return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                                 "System.arraycopy range exceeds array bounds");
            }

            auto source_component = array_component_name(*source_class);
            auto destination_component = array_component_name(*destination_class);
            if (!source_component || !destination_component) {
                return fail_java("java/lang/ArrayStoreException",
                                 "System.arraycopy array descriptor is invalid");
            }
            const bool source_primitive = source_component->size() == 1U;
            const bool destination_primitive =
                destination_component->size() == 1U;
            if (source_primitive || destination_primitive) {
                if (*source_component != *destination_component) {
                    return fail_java("java/lang/ArrayStoreException",
                                     "primitive array types do not match");
                }
            }

            std::vector<Value> copied;
            copied.reserve(count);
            for (usize index = 0; index < count; ++index) {
                auto value = machine.heap().element(*source,
                                                    source_start + index);
                if (!value) {
                    return std::unexpected(value.error());
                }
                copied.push_back(*value);
            }
            if (!destination_primitive) {
                for (const Value& value : copied) {
                    auto reference = value.as_reference();
                    if (!reference) {
                        return fail_java("java/lang/ArrayStoreException",
                                         "reference array contains a primitive value");
                    }
                    if (reference->is_null()) {
                        continue;
                    }
                    auto value_class = machine.heap().class_name(*reference);
                    if (!value_class) {
                        return std::unexpected(value_class.error());
                    }
                    auto assignable = machine.classes().is_assignable(
                        *value_class,
                        *destination_component);
                    if (!assignable) {
                        return std::unexpected(assignable.error());
                    }
                    if (!*assignable) {
                        return fail_java("java/lang/ArrayStoreException",
                                         "array element is not assignable to destination");
                    }
                }
            }
            for (usize index = 0; index < count; ++index) {
                auto stored = machine.heap().set_element(
                    *destination,
                    destination_start + index,
                    copied[index]);
                if (!stored) {
                    return std::unexpected(stored.error());
                }
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/System",
        "currentTimeMillis",
        "()J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "currentTimeMillis expects no arguments");
            }
            const auto now = std::chrono::system_clock::now()
                                 .time_since_epoch();
            const auto milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(now)
                    .count();
            return std::optional<Value>(
                Value::from_long(static_cast<i64>(milliseconds)));
        });

    add(registry,
        "java/lang/Float",
        "floatToIntBits",
        "(F)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1) {
                return fail(ErrorCode::invalid_argument,
                            "floatToIntBits expects one argument");
            }
            auto value = arguments[0].as_float();
            if (!value) {
                return std::unexpected(value.error());
            }
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(std::bit_cast<u32>(*value))));
        });

    add(registry,
        "java/lang/Float",
        "isNaN",
        "(F)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "Float.isNaN expects one argument");
            }
            auto value = arguments[0].as_float();
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(
                std::isnan(*value) ? 1 : 0));
        });

    add(registry,
        "java/lang/Float",
        "intBitsToFloat",
        "(I)F",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1) {
                return fail(ErrorCode::invalid_argument,
                            "intBitsToFloat expects one argument");
            }
            auto value = arguments[0].as_int();
            if (!value) {
                return std::unexpected(value.error());
            }
            return std::optional<Value>(Value::from_float(
                std::bit_cast<float>(static_cast<u32>(*value))));
        });

    add(registry,
        "java/lang/Double",
        "doubleToLongBits",
        "(D)J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1) {
                return fail(ErrorCode::invalid_argument,
                            "doubleToLongBits expects one argument");
            }
            auto value = arguments[0].as_double();
            if (!value) {
                return std::unexpected(value.error());
            }
            return std::optional<Value>(Value::from_long(
                static_cast<i64>(std::bit_cast<u64>(*value))));
        });

    add(registry,
        "java/lang/Double",
        "longBitsToDouble",
        "(J)D",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1) {
                return fail(ErrorCode::invalid_argument,
                            "longBitsToDouble expects one argument");
            }
            auto value = arguments[0].as_long();
            if (!value) {
                return std::unexpected(value.error());
            }
            return std::optional<Value>(Value::from_double(
                std::bit_cast<double>(static_cast<u64>(*value))));
        });

    add(registry,
        "java/lang/Runtime",
        "getRuntime",
        "()Ljava/lang/Runtime;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "Runtime.getRuntime expects no arguments");
            }
            auto field = machine.class_states().resolve_field(
                "java/lang/Runtime",
                "currentRuntime",
                "Ljava/lang/Runtime;",
                true);
            if (!field) return std::unexpected(field.error());
            auto current = machine.class_states().static_field(*field);
            if (!current) return std::unexpected(current.error());
            auto reference = current->as_reference();
            if (!reference) return std::unexpected(reference.error());
            if (reference->is_null()) {
                auto allocated = machine.class_states().allocate_instance(
                    machine.heap(), "java/lang/Runtime");
                if (!allocated) return std::unexpected(allocated.error());
                auto stored = machine.class_states().set_static_field(
                    *field, Value::from_reference(*allocated));
                if (!stored) return std::unexpected(stored.error());
                reference = *allocated;
            }
            return std::optional<Value>(Value::from_reference(*reference));
        });

    add(registry,
        "java/lang/Runtime",
        "totalMemory",
        "()J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            return std::optional<Value>(Value::from_long(64LL * 1024LL * 1024LL));
        });

    add(registry,
        "java/lang/Runtime",
        "freeMemory",
        "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            constexpr usize capacity = 64U * 1024U * 1024U;
            const usize used = machine.heap().stats().estimated_bytes;
            const usize free = used >= capacity ? 0 : capacity - used;
            return std::optional<Value>(
                Value::from_long(static_cast<i64>(free)));
        });

    add(registry,
        "java/lang/Runtime",
        "gc",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            // Collection is requested at a VM safepoint by the scheduler. The
            // synchronous native call cannot collect until all frame roots are
            // published, so this method intentionally only records success.
            return std::optional<Value> {};
        });

    add(registry,
        "java/util/Objects",
        "requireNonNull",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "Objects.requireNonNull expects one argument");
            }
            auto reference = arguments.front().as_reference();
            if (!reference || reference->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Objects.requireNonNull received null");
            }
            return std::optional<Value>(arguments.front());
        });

    add(registry,
        "java/lang/Thread",
        "<init>",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto initialized = machine.initialize_java_thread(*receiver,
                                                              ObjectRef {});
            if (!initialized) {
                return std::unexpected(initialized.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "<init>",
        "(Ljava/lang/Runnable;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "Thread(Runnable) expects one target");
            }
            auto receiver = require_receiver(arguments);
            auto target = arguments[1].as_reference();
            if (!receiver || !target) {
                return fail(ErrorCode::invalid_argument,
                            "Thread(Runnable) arguments are invalid");
            }
            auto initialized = machine.initialize_java_thread(*receiver,
                                                              *target);
            if (!initialized) {
                return std::unexpected(initialized.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "<init>",
        "(Ljava/lang/Runnable;Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "Thread(Runnable,String) expects target and name");
            }
            auto receiver = require_receiver(arguments);
            auto target = arguments[1].as_reference();
            auto name = arguments[2].as_reference();
            if (!receiver || !target || !name) {
                return fail(ErrorCode::invalid_argument,
                            "Thread(Runnable,String) arguments are invalid");
            }
            if (name->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Thread name is null");
            }
            auto initialized = machine.initialize_java_thread(*receiver,
                                                              *target);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "start",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto started = machine.start_java_thread(*receiver);
            if (!started) {
                return std::unexpected(started.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "run",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            return machine.run_java_thread_target(*receiver);
        });

    add(registry,
        "java/lang/Thread",
        "currentThread",
        "()Ljava/lang/Thread;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.currentThread expects no arguments");
            }
            auto current = machine.current_java_thread();
            if (!current) {
                return std::unexpected(current.error());
            }
            return std::optional<Value>(Value::from_reference(*current));
        });

    add(registry,
        "java/lang/Thread",
        "activeCount",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.activeCount expects no arguments");
            }
            const auto snapshot = machine.scheduler().snapshot();
            const usize count = static_cast<usize>(std::count_if(
                snapshot.threads.begin(),
                snapshot.threads.end(),
                [](const JavaThreadSnapshot& thread) {
                    return thread.alive;
                }));
            return std::optional<Value>(Value::from_int(
                count > static_cast<usize>(std::numeric_limits<i32>::max())
                    ? std::numeric_limits<i32>::max()
                    : static_cast<i32>(count)));
        });

    add(registry,
        "java/lang/Thread",
        "yield",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.yield expects no arguments");
            }
            machine.cooperative_yield();
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "sleep",
        "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.sleep expects one timeout");
            }
            auto timeout = arguments[0].as_long();
            if (!timeout) {
                return std::unexpected(timeout.error());
            }
            auto slept = machine.sleep_current_thread(*timeout);
            if (!slept) {
                return std::unexpected(slept.error());
            }
            if (*slept == SchedulerWaitResult::interrupted) {
                return fail_java("java/lang/InterruptedException",
                                 "Thread.sleep was interrupted");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "join",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto joined = machine.join_java_thread(*receiver, std::nullopt);
            if (!joined) {
                return std::unexpected(joined.error());
            }
            if (*joined == SchedulerWaitResult::interrupted) {
                return fail_java("java/lang/InterruptedException",
                                 "Thread.join was interrupted");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "join",
        "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.join(long) expects one timeout");
            }
            auto receiver = require_receiver(arguments);
            auto timeout = arguments[1].as_long();
            if (!receiver || !timeout) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.join(long) arguments are invalid");
            }
            auto joined = machine.join_java_thread(*receiver, *timeout);
            if (!joined) {
                return std::unexpected(joined.error());
            }
            if (*joined == SchedulerWaitResult::interrupted) {
                return fail_java("java/lang/InterruptedException",
                                 "Thread.join was interrupted");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "isAlive",
        "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto alive = machine.scheduler().is_alive(*receiver);
            if (!alive) {
                return std::unexpected(alive.error());
            }
            return std::optional<Value>(Value::from_int(*alive ? 1 : 0));
        });

    add(registry,
        "java/lang/Thread",
        "interrupt",
        "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto interrupted = machine.interrupt_java_thread(*receiver);
            if (!interrupted) {
                return std::unexpected(interrupted.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "interrupted",
        "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.interrupted expects no arguments");
            }
            return std::optional<Value>(Value::from_int(
                machine.scheduler().consume_current_interrupt() ? 1 : 0));
        });

    add(registry,
        "java/lang/Thread",
        "isInterrupted",
        "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto interrupted = machine.scheduler().is_interrupted(*receiver);
            if (!interrupted) {
                return std::unexpected(interrupted.error());
            }
            return std::optional<Value>(Value::from_int(
                *interrupted ? 1 : 0));
        });

    add(registry,
        "java/lang/Thread",
        "setPriority",
        "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.setPriority expects one value");
            }
            auto receiver = require_receiver(arguments);
            auto priority = arguments[1].as_int();
            if (!receiver || !priority) {
                return fail(ErrorCode::invalid_argument,
                            "Thread.setPriority arguments are invalid");
            }
            auto updated = machine.scheduler().set_priority(*receiver,
                                                            *priority);
            if (!updated) {
                return std::unexpected(updated.error());
            }
            return std::optional<Value> {};
        });

    add(registry,
        "java/lang/Thread",
        "getPriority",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            auto priority = machine.scheduler().priority(*receiver);
            if (!priority) {
                return std::unexpected(priority.error());
            }
            return std::optional<Value>(Value::from_int(*priority));
        });

    const auto add_midlet_signal = [&registry](const char* name,
                                               MidletSignal signal) {
        add(registry,
            "javax/microedition/midlet/MIDlet",
            name,
            "()V",
            [signal](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto receiver = require_receiver(arguments);
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                machine.signal_midlet(signal);
                return std::optional<Value> {};
            });
    };
    add_midlet_signal("notifyDestroyed", MidletSignal::destroyed);
    add_midlet_signal("notifyPaused", MidletSignal::paused);
    add_midlet_signal("resumeRequest", MidletSignal::resume_requested);

    add(registry,
        "javax/microedition/midlet/MIDlet",
        "getAppProperty",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (arguments.size() != 2) {
                return fail(ErrorCode::invalid_argument,
                            "MIDlet.getAppProperty expects one key");
            }
            auto key = arguments[1].as_reference();
            if (!key) {
                return std::unexpected(key.error());
            }
            auto property = machine.app_property(*key);
            if (!property) {
                return std::unexpected(property.error());
            }
            if (!property->has_value()) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto string = create_java_string(machine,
                                             std::move(**property));
            if (!string) {
                return std::unexpected(string.error());
            }
            return std::optional<Value>(Value::from_reference(*string));
        });

    add(registry,
        "javax/microedition/midlet/MIDlet",
        "platformRequest",
        "(Ljava/lang/String;)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (arguments.size() != 2) {
                return fail(ErrorCode::invalid_argument,
                            "MIDlet.platformRequest expects one URL");
            }
            return std::optional<Value>(Value::from_int(0));
        });
}

} // namespace phoneme::vm
