#include "ConsoleNatives.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr usize kPrintStreamOutputField = 0;
constexpr usize kPrintStreamErrorField = 1;
constexpr usize kPrintStreamAutoFlushField = 2;
constexpr usize kPrintStreamConsoleField = 3;
constexpr usize kByteOutputBufferField = 0;
constexpr usize kByteOutputCountField = 1;
constexpr usize kFilterOutputField = 0;
constexpr usize kMaximumOutputDepth = 64;

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
                    "PrintStream method has no receiver");
    }
    auto object = arguments.front().as_reference();
    if (!object || object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "PrintStream receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<i32> int_field(Machine& machine,
                                    ObjectRef object,
                                    usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<ObjectRef> reference_field(Machine& machine,
                                                 ObjectRef object,
                                                 usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Status set_error(Machine& machine,
                               ObjectRef stream) {
    return machine.heap().set_field(stream, kPrintStreamErrorField,
                                    Value::from_int(1));
}

[[nodiscard]] Result<bool> is_instance(Machine& machine,
                                       ObjectRef object,
                                       std::string_view target) {
    return machine.object_is_instance(object, target);
}

[[nodiscard]] Result<usize> byte_array_length(Machine& machine,
                                              ObjectRef array) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "byte array is null");
    }
    auto class_name = machine.heap().class_name(array);
    if (!class_name || *class_name != "[B") {
        return fail_java("java/lang/IllegalArgumentException",
                         "value is not byte[]");
    }
    return machine.heap().array_length(array);
}

[[nodiscard]] Result<u8> byte_value(Machine& machine,
                                    ObjectRef array,
                                    usize index) {
    auto value = machine.heap().element(array, index);
    if (!value) return std::unexpected(value.error());
    auto integer = value->as_int();
    if (!integer) return std::unexpected(integer.error());
    return static_cast<u8>(static_cast<i8>(*integer));
}

[[nodiscard]] Status set_byte_value(Machine& machine,
                                    ObjectRef array,
                                    usize index,
                                    u8 value) {
    return machine.heap().set_element(
        array, index,
        Value::from_int(static_cast<i32>(static_cast<i8>(value))));
}

[[nodiscard]] Status ensure_byte_output_capacity(Machine& machine,
                                                 ObjectRef output,
                                                 i32 minimum) {
    auto buffer = reference_field(machine, output, kByteOutputBufferField);
    if (!buffer) return std::unexpected(buffer.error());
    auto capacity = byte_array_length(machine, *buffer);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    usize new_capacity = *capacity == 0U ? 1U : *capacity * 2U;
    if (new_capacity < static_cast<usize>(minimum)) {
        new_capacity = static_cast<usize>(minimum);
    }
    auto replacement = machine.heap().allocate_array(
        "[B", new_capacity, Value::from_int(0));
    if (!replacement) return std::unexpected(replacement.error());
    auto count = int_field(machine, output, kByteOutputCountField);
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*buffer,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(
            *replacement, static_cast<usize>(index), *value);
        if (!stored) return stored;
    }
    return machine.heap().set_field(output, kByteOutputBufferField,
                                    Value::from_reference(*replacement));
}

[[nodiscard]] Status write_byte_array_output(Machine& machine,
                                             ObjectRef output,
                                             u8 byte) {
    auto count = int_field(machine, output, kByteOutputCountField);
    if (!count) return std::unexpected(count.error());
    if (*count == std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "ByteArrayOutputStream exceeds int capacity");
    }
    auto capacity = ensure_byte_output_capacity(machine, output, *count + 1);
    if (!capacity) return capacity;
    auto buffer = reference_field(machine, output, kByteOutputBufferField);
    if (!buffer) return std::unexpected(buffer.error());
    auto stored = set_byte_value(machine, *buffer,
                                 static_cast<usize>(*count), byte);
    if (!stored) return stored;
    return machine.heap().set_field(output, kByteOutputCountField,
                                    Value::from_int(*count + 1));
}

[[nodiscard]] Status write_output_byte(Machine& machine,
                                       ObjectRef output,
                                       u8 byte,
                                       usize depth) {
    if (output.is_null()) {
        return fail(ErrorCode::invalid_state,
                    "PrintStream underlying output is closed");
    }
    if (depth >= kMaximumOutputDepth) {
        return fail(ErrorCode::invalid_state,
                    "PrintStream output chain is too deep");
    }
    auto runtime_class = machine.heap().class_name(output);
    if (!runtime_class) return std::unexpected(runtime_class.error());
    auto write_method = machine.classes().resolve_method(
        *runtime_class, "write", "(I)V");
    if (write_method && write_method->owner != nullptr &&
        write_method->owner->name() != "java/io/OutputStream" &&
        write_method->owner->name() != "java/io/FilterOutputStream" &&
        write_method->owner->name() != "java/io/ByteArrayOutputStream" &&
        write_method->owner->name() != "java/io/PrintStream" &&
        write_method->owner->name() != "java/io/DataOutputStream") {
        const Value argument = Value::from_int(static_cast<i32>(byte));
        auto invoked = machine.invoke_instance(
            output, *runtime_class, "write", "(I)V",
            std::span<const Value>(&argument, 1U));
        if (!invoked) return std::unexpected(invoked.error());
        if (invoked->throwable.has_value()) {
            auto thrown = machine.heap().class_name(*invoked->throwable);
            if (!thrown) return std::unexpected(thrown.error());
            return fail_java(*thrown, "custom OutputStream.write threw");
        }
        return {};
    }
    auto byte_array = is_instance(machine, output,
                                  "java/io/ByteArrayOutputStream");
    if (!byte_array) return std::unexpected(byte_array.error());
    if (*byte_array) return write_byte_array_output(machine, output, byte);

    auto print_stream = is_instance(machine, output, "java/io/PrintStream");
    if (!print_stream) return std::unexpected(print_stream.error());
    if (*print_stream) {
        auto console = int_field(machine, output, kPrintStreamConsoleField);
        if (!console) return std::unexpected(console.error());
        if (*console != 0) {
            const char16_t character = static_cast<char16_t>(byte);
            machine.append_console(std::u16string_view(&character, 1));
            return {};
        }
        auto nested = reference_field(machine, output,
                                      kPrintStreamOutputField);
        if (!nested) return std::unexpected(nested.error());
        return write_output_byte(machine, *nested, byte, depth + 1U);
    }

    auto filter = is_instance(machine, output,
                              "java/io/FilterOutputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto nested = reference_field(machine, output, kFilterOutputField);
        if (!nested) return std::unexpected(nested.error());
        return write_output_byte(machine, *nested, byte, depth + 1U);
    }
    return fail(ErrorCode::unsupported_feature,
                "PrintStream output implementation is not connected");
}

[[nodiscard]] Status invoke_custom_output_void(Machine& machine,
                                               ObjectRef output,
                                               std::string_view method_name) {
    auto runtime_class = machine.heap().class_name(output);
    if (!runtime_class) return std::unexpected(runtime_class.error());
    auto method = machine.classes().resolve_method(
        *runtime_class, method_name, "()V");
    if (!method || method->owner == nullptr ||
        method->owner->name() == "java/io/OutputStream" ||
        method->owner->name() == "java/io/FilterOutputStream" ||
        method->owner->name() == "java/io/ByteArrayOutputStream" ||
        method->owner->name() == "java/io/PrintStream" ||
        method->owner->name() == "java/io/DataOutputStream") {
        return {};
    }
    auto invoked = machine.invoke_instance(output, *runtime_class,
                                           method_name, "()V");
    if (!invoked) return std::unexpected(invoked.error());
    if (invoked->throwable.has_value()) {
        auto thrown = machine.heap().class_name(*invoked->throwable);
        if (!thrown) return std::unexpected(thrown.error());
        return fail_java(*thrown,
                         std::string("custom OutputStream.") +
                             std::string(method_name) + " threw");
    }
    return {};
}

[[nodiscard]] Status flush_output(Machine& machine,
                                  ObjectRef output,
                                  usize depth) {
    if (output.is_null()) return {};
    if (depth >= kMaximumOutputDepth) {
        return fail(ErrorCode::invalid_state,
                    "PrintStream output chain is too deep");
    }
    auto custom = invoke_custom_output_void(machine, output, "flush");
    if (!custom) return custom;
    auto print_stream = is_instance(machine, output, "java/io/PrintStream");
    if (!print_stream) return std::unexpected(print_stream.error());
    if (*print_stream) {
        auto nested = reference_field(machine, output,
                                      kPrintStreamOutputField);
        if (!nested) return std::unexpected(nested.error());
        return flush_output(machine, *nested, depth + 1U);
    }
    auto filter = is_instance(machine, output,
                              "java/io/FilterOutputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto nested = reference_field(machine, output, kFilterOutputField);
        if (!nested) return std::unexpected(nested.error());
        return flush_output(machine, *nested, depth + 1U);
    }
    return {};
}

[[nodiscard]] Status close_output(Machine& machine,
                                  ObjectRef output,
                                  usize depth) {
    if (output.is_null()) return {};
    if (depth >= kMaximumOutputDepth) {
        return fail(ErrorCode::invalid_state,
                    "PrintStream output chain is too deep");
    }
    auto custom = invoke_custom_output_void(machine, output, "close");
    if (!custom) return custom;
    auto print_stream = is_instance(machine, output, "java/io/PrintStream");
    if (!print_stream) return std::unexpected(print_stream.error());
    if (*print_stream) {
        auto nested = reference_field(machine, output,
                                      kPrintStreamOutputField);
        if (!nested) return std::unexpected(nested.error());
        return close_output(machine, *nested, depth + 1U);
    }
    auto filter = is_instance(machine, output,
                              "java/io/FilterOutputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto nested = reference_field(machine, output, kFilterOutputField);
        if (!nested) return std::unexpected(nested.error());
        return close_output(machine, *nested, depth + 1U);
    }
    return {};
}

[[nodiscard]] Status write_utf8(Machine& machine,
                                ObjectRef stream,
                                std::u16string_view text) {
    auto console = int_field(machine, stream, kPrintStreamConsoleField);
    if (!console) return std::unexpected(console.error());
    if (*console != 0) {
        machine.append_console(text);
        return {};
    }
    auto output = reference_field(machine, stream, kPrintStreamOutputField);
    if (!output) return std::unexpected(output.error());
    for (usize index = 0; index < text.size(); ++index) {
        u32 code_point = static_cast<u16>(text[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < text.size()) {
            const u32 low = static_cast<u16>(text[index + 1U]);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U +
                    ((code_point - 0xD800U) << 10U) +
                    (low - 0xDC00U);
                ++index;
            }
        }
        std::array<u8, 4> bytes {};
        usize count = 0;
        if (code_point <= 0x7FU) {
            bytes[0] = static_cast<u8>(code_point);
            count = 1;
        } else if (code_point <= 0x7FFU) {
            bytes[0] = static_cast<u8>(0xC0U | (code_point >> 6U));
            bytes[1] = static_cast<u8>(0x80U | (code_point & 0x3FU));
            count = 2;
        } else if (code_point <= 0xFFFFU) {
            bytes[0] = static_cast<u8>(0xE0U | (code_point >> 12U));
            bytes[1] = static_cast<u8>(0x80U |
                                       ((code_point >> 6U) & 0x3FU));
            bytes[2] = static_cast<u8>(0x80U | (code_point & 0x3FU));
            count = 3;
        } else {
            bytes[0] = static_cast<u8>(0xF0U | (code_point >> 18U));
            bytes[1] = static_cast<u8>(0x80U |
                                       ((code_point >> 12U) & 0x3FU));
            bytes[2] = static_cast<u8>(0x80U |
                                       ((code_point >> 6U) & 0x3FU));
            bytes[3] = static_cast<u8>(0x80U | (code_point & 0x3FU));
            count = 4;
        }
        for (usize byte_index = 0; byte_index < count; ++byte_index) {
            auto written = write_output_byte(machine, *output,
                                             bytes[byte_index], 0);
            if (!written) return written;
        }
    }
    return {};
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
    std::array<char, 96> buffer {};
    auto converted = std::to_chars(buffer.data(),
                                   buffer.data() + buffer.size(),
                                   value);
    if (converted.ec != std::errc {}) {
        return fail(ErrorCode::internal_error,
                    "failed to format PrintStream integer");
    }
    return ascii_text(std::string_view(
        buffer.data(),
        static_cast<usize>(converted.ptr - buffer.data())));
}

[[nodiscard]] std::u16string float_text(float value) {
    std::array<char, 64> buffer {};
    const int count = std::snprintf(buffer.data(), buffer.size(),
                                    "%.9g", static_cast<double>(value));
    if (count <= 0) return u"0.0";
    std::string text(buffer.data(), static_cast<usize>(count));
    if (text.find_first_of(".eE") == std::string::npos &&
        text != "nan" && text != "inf" && text != "-inf") {
        text.append(".0");
    }
    return ascii_text(text);
}

[[nodiscard]] std::u16string double_text(double value) {
    std::array<char, 96> buffer {};
    const int count = std::snprintf(buffer.data(), buffer.size(),
                                    "%.17g", value);
    if (count <= 0) return u"0.0";
    std::string text(buffer.data(), static_cast<usize>(count));
    if (text.find_first_of(".eE") == std::string::npos &&
        text != "nan" && text != "inf" && text != "-inf") {
        text.append(".0");
    }
    return ascii_text(text);
}

[[nodiscard]] Result<std::u16string> object_text(Machine& machine,
                                                 ObjectRef object) {
    if (object.is_null()) return std::u16string(u"null");
    auto class_name = machine.heap().class_name(object);
    if (!class_name) return std::unexpected(class_name.error());
    if (*class_name == "java/lang/String" ||
        *class_name == "java/lang/StringBuilder" ||
        *class_name == "java/lang/StringBuffer") {
        return machine.heap().string_value(object);
    }
    if (*class_name == "java/lang/Boolean") {
        auto value = machine.heap().field(object, 0);
        if (!value) return std::unexpected(value.error());
        auto boolean = value->as_int();
        if (!boolean) return std::unexpected(boolean.error());
        return std::u16string(*boolean == 0 ? u"false" : u"true");
    }
    if (*class_name == "java/lang/Byte" ||
        *class_name == "java/lang/Short" ||
        *class_name == "java/lang/Integer" ||
        *class_name == "java/lang/Character") {
        auto value = machine.heap().field(object, 0);
        if (!value) return std::unexpected(value.error());
        auto integer = value->as_int();
        if (!integer) return std::unexpected(integer.error());
        if (*class_name == "java/lang/Character") {
            return std::u16string(1, static_cast<char16_t>(
                static_cast<u16>(*integer)));
        }
        return integral_text(*integer);
    }
    if (*class_name == "java/lang/Long") {
        auto value = machine.heap().field(object, 0);
        if (!value) return std::unexpected(value.error());
        auto integer = value->as_long();
        if (!integer) return std::unexpected(integer.error());
        return integral_text(*integer);
    }
    if (*class_name == "java/lang/Float") {
        auto value = machine.heap().field(object, 0);
        if (!value) return std::unexpected(value.error());
        auto number = value->as_float();
        if (!number) return std::unexpected(number.error());
        return float_text(*number);
    }
    if (*class_name == "java/lang/Double") {
        auto value = machine.heap().field(object, 0);
        if (!value) return std::unexpected(value.error());
        auto number = value->as_double();
        if (!number) return std::unexpected(number.error());
        return double_text(*number);
    }
    if (*class_name == "java/lang/Class") {
        auto mirrored = machine.mirrored_class_name(object);
        if (mirrored) {
            std::replace(mirrored->begin(), mirrored->end(), '/', '.');
            return ascii_text("class " + *mirrored);
        }
    }
    std::replace(class_name->begin(), class_name->end(), '/', '.');
    constexpr std::array<char, 16> digits {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    const u32 hash = static_cast<u32>(object.bits ^ (object.bits >> 32U));
    std::string text = *class_name + "@";
    bool started = false;
    for (i32 shift = 28; shift >= 0; shift -= 4) {
        const u32 digit = (hash >> static_cast<u32>(shift)) & 0xFU;
        if (digit != 0U || started || shift == 0) {
            text.push_back(digits[digit]);
            started = true;
        }
    }
    return ascii_text(text);
}

[[nodiscard]] Status print_text(Machine& machine,
                                ObjectRef stream,
                                std::u16string_view text,
                                bool newline) {
    auto written = write_utf8(machine, stream, text);
    if (!written) {
        auto error = set_error(machine, stream);
        if (!error) return error;
        return {};
    }
    if (newline) {
        const char16_t line_feed = u'\n';
        auto ended = write_utf8(machine, stream,
                                std::u16string_view(&line_feed, 1));
        if (!ended) {
            auto error = set_error(machine, stream);
            if (!error) return error;
        }
    }
    return {};
}

[[nodiscard]] Result<std::u16string> string_argument(Machine& machine,
                                                     ObjectRef value) {
    if (value.is_null()) return std::u16string(u"null");
    return machine.heap().string_value(value);
}

[[nodiscard]] Result<std::u16string> char_array_argument(Machine& machine,
                                                         ObjectRef array) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "PrintStream char array is null");
    }
    auto class_name = machine.heap().class_name(array);
    if (!class_name || *class_name != "[C") {
        return fail_java("java/lang/IllegalArgumentException",
                         "PrintStream value is not char[]");
    }
    auto length = machine.heap().array_length(array);
    if (!length) return std::unexpected(length.error());
    std::u16string text;
    text.reserve(*length);
    for (usize index = 0; index < *length; ++index) {
        auto value = machine.heap().element(array, index);
        if (!value) return std::unexpected(value.error());
        auto character = value->as_int();
        if (!character) return std::unexpected(character.error());
        text.push_back(static_cast<char16_t>(static_cast<u16>(*character)));
    }
    return text;
}

[[nodiscard]] Result<std::optional<std::u16string>> system_property(
    Machine& machine,
    ObjectRef key) {
    if (key.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "System property key is null");
    }
    auto text = machine.heap().string_value(key);
    if (!text) return std::unexpected(text.error());
    if (text->empty()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "System property key is empty");
    }
    if (auto configured = machine.configured_system_property(*text);
        configured.has_value()) {
        return configured;
    }
    struct Property final {
        std::u16string_view key;
        std::u16string_view value;
    };
    static constexpr std::array<Property, 10> properties {{
        // Match phoneME's generated MIDP system configuration. MIDlets use
        // this value as a protocol capability, not as the host OS identity.
        {u"microedition.platform", u"j2me"},
        {u"microedition.profiles", u"MIDP-2.1"},
        {u"microedition.configuration", u"CLDC-1.1"},
        {u"microedition.locale", u"en-US"},
        {u"microedition.encoding", u"ISO8859_1"},
        {u"microedition.jtwi.version", u"1.0"},
        {u"microedition.msa.version", u"1.1"},
        {u"file.separator", u"/"},
        {u"path.separator", u":"},
        {u"line.separator", u"\n"},
    }};
    for (const Property& property : properties) {
        if (*text == property.key) {
            return std::optional<std::u16string>(
                std::u16string(property.value));
        }
    }
    return std::optional<std::u16string> {};
}

} // namespace

void register_console_natives(NativeMethodRegistry& registry) {
    const auto constructor = [&registry](const char* descriptor,
                                         bool with_auto_flush) {
        add(registry, "java/io/PrintStream", "<init>", descriptor,
            [with_auto_flush](Machine& machine,
                              std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto stream = receiver(arguments);
                auto output = arguments[1].as_reference();
                if (!stream) return std::unexpected(stream.error());
                if (!output || output->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "PrintStream output is null");
                }
                i32 auto_flush = 0;
                if (with_auto_flush) {
                    auto parsed = arguments[2].as_int();
                    if (!parsed) return std::unexpected(parsed.error());
                    auto_flush = *parsed == 0 ? 0 : 1;
                }
                auto output_stored = machine.heap().set_field(
                    *stream, kPrintStreamOutputField,
                    Value::from_reference(*output));
                auto error_stored = machine.heap().set_field(
                    *stream, kPrintStreamErrorField, Value::from_int(0));
                auto flush_stored = machine.heap().set_field(
                    *stream, kPrintStreamAutoFlushField,
                    Value::from_int(auto_flush));
                auto console_stored = machine.heap().set_field(
                    *stream, kPrintStreamConsoleField, Value::from_int(0));
                if (!output_stored) return std::unexpected(output_stored.error());
                if (!error_stored) return std::unexpected(error_stored.error());
                if (!flush_stored) return std::unexpected(flush_stored.error());
                if (!console_stored) return std::unexpected(console_stored.error());
                return std::optional<Value> {};
            });
    };
    constructor("(Ljava/io/OutputStream;)V", false);
    constructor("(Ljava/io/OutputStream;Z)V", true);

    add(registry, "java/io/PrintStream", "write", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            auto value = arguments[1].as_int();
            if (!stream) return std::unexpected(stream.error());
            if (!value) return std::unexpected(value.error());
            auto written = write_output_byte(machine, *stream,
                                             static_cast<u8>(*value), 0);
            if (!written) {
                auto error = set_error(machine, *stream);
                if (!error) return std::unexpected(error.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/io/PrintStream", "write", "([BII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            auto array = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!stream || !array || !offset || !length) {
                return fail(ErrorCode::invalid_argument,
                            "PrintStream.write arguments are invalid");
            }
            auto array_length = byte_array_length(machine, *array);
            if (!array_length) return std::unexpected(array_length.error());
            if (*offset < 0 || *length < 0 ||
                static_cast<usize>(*offset) > *array_length ||
                static_cast<usize>(*length) >
                    *array_length - static_cast<usize>(*offset)) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "PrintStream byte range is invalid");
            }
            for (i32 index = 0; index < *length; ++index) {
                auto byte = byte_value(machine, *array,
                                       static_cast<usize>(*offset + index));
                if (!byte) return std::unexpected(byte.error());
                auto written = write_output_byte(machine, *stream, *byte, 0);
                if (!written) {
                    auto error = set_error(machine, *stream);
                    if (!error) return std::unexpected(error.error());
                    break;
                }
            }
            return std::optional<Value> {};
        });
    add(registry, "java/io/PrintStream", "flush", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            if (!stream) return std::unexpected(stream.error());
            auto output = reference_field(machine, *stream,
                                          kPrintStreamOutputField);
            if (!output) return std::unexpected(output.error());
            auto flushed = flush_output(machine, *output, 0U);
            if (!flushed) {
                auto error = set_error(machine, *stream);
                if (!error) return std::unexpected(error.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/io/PrintStream", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            if (!stream) return std::unexpected(stream.error());
            auto output = reference_field(machine, *stream,
                                          kPrintStreamOutputField);
            if (!output) return std::unexpected(output.error());
            auto flushed = flush_output(machine, *output, 0U);
            auto closed = flushed
                ? close_output(machine, *output, 0U)
                : flushed;
            if (!closed) {
                auto error = set_error(machine, *stream);
                if (!error) return std::unexpected(error.error());
            }
            auto output_cleared = machine.heap().set_field(
                *stream, kPrintStreamOutputField,
                Value::from_reference({}));
            auto console = machine.heap().set_field(
                *stream, kPrintStreamConsoleField, Value::from_int(0));
            if (!output_cleared) {
                return std::unexpected(output_cleared.error());
            }
            if (!console) return std::unexpected(console.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/PrintStream", "checkError", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            if (!stream) return std::unexpected(stream.error());
            auto error = machine.heap().field(*stream,
                                              kPrintStreamErrorField);
            if (!error) return std::unexpected(error.error());
            return std::optional<Value>(*error);
        });
    add(registry, "java/io/PrintStream", "setError", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            if (!stream) return std::unexpected(stream.error());
            auto error = set_error(machine, *stream);
            if (!error) return std::unexpected(error.error());
            return std::optional<Value> {};
        });

    const auto add_print = [&registry](const char* name,
                                       const char* descriptor,
                                       bool newline,
                                       auto format) {
        add(registry, "java/io/PrintStream", name, descriptor,
            [newline, format](Machine& machine,
                              std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto stream = receiver(arguments);
                if (!stream) return std::unexpected(stream.error());
                auto text = format(machine, arguments[1]);
                if (!text) return std::unexpected(text.error());
                auto printed = print_text(machine, *stream, *text, newline);
                if (!printed) return std::unexpected(printed.error());
                return std::optional<Value> {};
            });
    };
    const auto boolean_format = [](Machine&, const Value& value)
        -> Result<std::u16string> {
        auto boolean = value.as_int();
        if (!boolean) return std::unexpected(boolean.error());
        return std::u16string(*boolean == 0 ? u"false" : u"true");
    };
    const auto character_format = [](Machine&, const Value& value)
        -> Result<std::u16string> {
        auto character = value.as_int();
        if (!character) return std::unexpected(character.error());
        return std::u16string(1, static_cast<char16_t>(
            static_cast<u16>(*character)));
    };
    const auto integer_format = [](Machine&, const Value& value)
        -> Result<std::u16string> {
        auto integer = value.as_int();
        if (!integer) return std::unexpected(integer.error());
        return integral_text(*integer);
    };
    const auto long_format = [](Machine&, const Value& value)
        -> Result<std::u16string> {
        auto integer = value.as_long();
        if (!integer) return std::unexpected(integer.error());
        return integral_text(*integer);
    };
    const auto float_format = [](Machine&, const Value& value)
        -> Result<std::u16string> {
        auto number = value.as_float();
        if (!number) return std::unexpected(number.error());
        return float_text(*number);
    };
    const auto double_format = [](Machine&, const Value& value)
        -> Result<std::u16string> {
        auto number = value.as_double();
        if (!number) return std::unexpected(number.error());
        return double_text(*number);
    };
    const auto string_format = [](Machine& machine, const Value& value)
        -> Result<std::u16string> {
        auto string = value.as_reference();
        if (!string) return std::unexpected(string.error());
        return string_argument(machine, *string);
    };
    const auto object_format = [](Machine& machine, const Value& value)
        -> Result<std::u16string> {
        auto object = value.as_reference();
        if (!object) return std::unexpected(object.error());
        return object_text(machine, *object);
    };
    const auto chars_format = [](Machine& machine, const Value& value)
        -> Result<std::u16string> {
        auto array = value.as_reference();
        if (!array) return std::unexpected(array.error());
        return char_array_argument(machine, *array);
    };

    for (const bool newline : {false, true}) {
        const char* name = newline ? "println" : "print";
        add_print(name, "(Z)V", newline, boolean_format);
        add_print(name, "(C)V", newline, character_format);
        add_print(name, "(I)V", newline, integer_format);
        add_print(name, "(J)V", newline, long_format);
        add_print(name, "(F)V", newline, float_format);
        add_print(name, "(D)V", newline, double_format);
        add_print(name, "([C)V", newline, chars_format);
        add_print(name, "(Ljava/lang/String;)V", newline, string_format);
        add_print(name, "(Ljava/lang/Object;)V", newline, object_format);
    }
    add(registry, "java/io/PrintStream", "println", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = receiver(arguments);
            if (!stream) return std::unexpected(stream.error());
            auto printed = print_text(machine, *stream, {}, true);
            if (!printed) return std::unexpected(printed.error());
            return std::optional<Value> {};
        });

    add(registry, "java/lang/System", "nanoTime", "()J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "System.nanoTime expects no arguments");
            }
            const auto now = std::chrono::steady_clock::now()
                                 .time_since_epoch();
            const auto nanoseconds =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now)
                    .count();
            return std::optional<Value>(Value::from_long(
                static_cast<i64>(nanoseconds)));
        });
    const auto get_property = [&registry](bool with_default) {
        add(registry, "java/lang/System", "getProperty",
            with_default
                ? "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"
                : "(Ljava/lang/String;)Ljava/lang/String;",
            [with_default](Machine& machine,
                           std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto key = arguments[0].as_reference();
                if (!key) return std::unexpected(key.error());
                auto property = system_property(machine, *key);
                if (!property) return std::unexpected(property.error());
                if (!property->has_value()) {
                    if (!with_default) {
                        return std::optional<Value>(
                            Value::from_reference({}));
                    }
                    return std::optional<Value>(arguments[1]);
                }
                auto string = machine.class_states().allocate_instance(
                    machine.heap(), "java/lang/String");
                if (!string) return std::unexpected(string.error());
                auto attached = machine.heap().attach_string(
                    *string, std::move(**property));
                if (!attached) return std::unexpected(attached.error());
                return std::optional<Value>(Value::from_reference(*string));
            });
    };
    get_property(false);
    get_property(true);
    add(registry, "java/lang/System", "gc", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "System.gc expects no arguments");
            }
            auto collected = machine.collect_garbage();
            if (!collected) {
                return std::unexpected(collected.error());
            }
            return std::optional<Value> {};
        });
    const auto exit_runtime = [](Machine& machine,
                                 std::span<const Value> arguments,
                                 bool has_receiver)
        -> Result<std::optional<Value>> {
        const usize expected = has_receiver ? 2U : 1U;
        if (arguments.size() != expected) {
            return fail(ErrorCode::invalid_argument,
                        "runtime exit expects one status argument");
        }
        if (has_receiver) {
            auto receiver = arguments[0].as_reference();
            if (!receiver || receiver->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Runtime.exit receiver is null");
            }
        }
        auto status = arguments[has_receiver ? 1U : 0U].as_int();
        if (!status) return std::unexpected(status.error());
        machine.signal_midlet(MidletSignal::destroyed);
        return std::optional<Value> {};
    };
    add(registry, "java/lang/System", "exit", "(I)V",
        [exit_runtime](Machine& machine, std::span<const Value> arguments) {
            return exit_runtime(machine, arguments, false);
        });
    add(registry, "java/lang/Runtime", "exit", "(I)V",
        [exit_runtime](Machine& machine, std::span<const Value> arguments) {
            return exit_runtime(machine, arguments, true);
        });
}

} // namespace phoneme::vm
