#include "StringEncodingNatives.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {
namespace {

enum class Charset : u8 {
    utf8,
    latin1,
    ascii,
    utf16be,
    utf16le,
    utf16,
};

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
        return fail(ErrorCode::invalid_argument,
                    "String native constructor has no receiver");
    }
    auto object = arguments.front().as_reference();
    if (!object || object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "String receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<std::string> charset_name(Machine& machine,
                                               ObjectRef string) {
    if (string.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "charset name is null");
    }
    auto class_name = machine.heap().class_name(string);
    if (!class_name) return std::unexpected(class_name.error());
    ObjectRef name_reference = string;
    if (*class_name == "java/nio/charset/Charset") {
        auto field = machine.heap().field(string, 0U);
        if (!field) return std::unexpected(field.error());
        auto reference = field->as_reference();
        if (!reference || reference->is_null()) {
            return fail(ErrorCode::invalid_state,
                        "Charset canonical name is missing");
        }
        name_reference = *reference;
    }
    auto value = machine.heap().string_value(name_reference);
    if (!value) return std::unexpected(value.error());
    std::string normalized;
    normalized.reserve(value->size());
    for (const char16_t character : *value) {
        if (character > 0x7FU) {
            return fail_java("java/io/UnsupportedEncodingException",
                             "charset name is not ASCII");
        }
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '-' || byte == '_' || std::isspace(byte) != 0) continue;
        normalized.push_back(static_cast<char>(std::toupper(byte)));
    }
    return normalized;
}

[[nodiscard]] Result<Charset> resolve_charset(std::string_view name) {
    if (name == "UTF8") return Charset::utf8;
    if (name == "ISO88591" || name == "LATIN1") return Charset::latin1;
    if (name == "USASCII" || name == "ASCII") return Charset::ascii;
    if (name == "UTF16BE" || name == "UNICODEBIGUNMARKED") {
        return Charset::utf16be;
    }
    if (name == "UTF16LE" || name == "UNICODELITTLEUNMARKED") {
        return Charset::utf16le;
    }
    if (name == "UTF16" || name == "UNICODE") return Charset::utf16;
    return fail_java("java/io/UnsupportedEncodingException",
                     "unsupported String charset");
}

[[nodiscard]] Result<std::vector<u8>> byte_slice(Machine& machine,
                                                 ObjectRef array,
                                                 i32 offset,
                                                 i32 length) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "String byte array is null");
    }
    auto class_name = machine.heap().class_name(array);
    auto array_length = machine.heap().array_length(array);
    if (!class_name || !array_length || *class_name != "[B") {
        return fail_java("java/lang/IllegalArgumentException",
                         "String constructor requires byte[]");
    }
    if (offset < 0 || length < 0 ||
        static_cast<usize>(offset) > *array_length ||
        static_cast<usize>(length) >
            *array_length - static_cast<usize>(offset)) {
        return fail_java("java/lang/StringIndexOutOfBoundsException",
                         "String byte slice is outside array bounds");
    }
    std::vector<u8> bytes;
    bytes.reserve(static_cast<usize>(length));
    for (i32 index = 0; index < length; ++index) {
        auto element = machine.heap().element(
            array, static_cast<usize>(offset + index));
        if (!element) return std::unexpected(element.error());
        auto value = element->as_int();
        if (!value) return std::unexpected(value.error());
        bytes.push_back(static_cast<u8>(static_cast<u32>(*value)));
    }
    return bytes;
}

void append_replacement(std::u16string& output) {
    output.push_back(static_cast<char16_t>(0xFFFDU));
}

[[nodiscard]] std::u16string decode_utf8(std::span<const u8> bytes) {
    std::u16string output;
    output.reserve(bytes.size());
    usize index = 0;
    while (index < bytes.size()) {
        const u8 first = bytes[index];
        if (first <= 0x7FU) {
            output.push_back(static_cast<char16_t>(first));
            ++index;
            continue;
        }

        u32 code_point = 0;
        usize count = 0;
        u32 minimum = 0;
        if ((first & 0xE0U) == 0xC0U) {
            code_point = first & 0x1FU;
            count = 2U;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            code_point = first & 0x0FU;
            count = 3U;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            code_point = first & 0x07U;
            count = 4U;
            minimum = 0x10000U;
        } else {
            append_replacement(output);
            ++index;
            continue;
        }

        if (count > bytes.size() - index) {
            append_replacement(output);
            ++index;
            continue;
        }
        bool valid = true;
        for (usize continuation = 1U; continuation < count; ++continuation) {
            const u8 byte = bytes[index + continuation];
            if ((byte & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            code_point = (code_point << 6U) | (byte & 0x3FU);
        }
        if (!valid || code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            append_replacement(output);
            ++index;
            continue;
        }
        if (code_point <= 0xFFFFU) {
            output.push_back(static_cast<char16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            output.push_back(static_cast<char16_t>(
                0xD800U | (code_point >> 10U)));
            output.push_back(static_cast<char16_t>(
                0xDC00U | (code_point & 0x3FFU)));
        }
        index += count;
    }
    return output;
}

[[nodiscard]] std::u16string decode_bytes(std::span<const u8> bytes,
                                          Charset charset) {
    if (charset == Charset::utf8) return decode_utf8(bytes);
    std::u16string output;
    if (charset == Charset::latin1 || charset == Charset::ascii) {
        output.reserve(bytes.size());
        for (const u8 byte : bytes) {
            if (charset == Charset::ascii && byte > 0x7FU) {
                append_replacement(output);
            } else {
                output.push_back(static_cast<char16_t>(byte));
            }
        }
        return output;
    }

    bool little_endian = charset == Charset::utf16le;
    usize index = 0;
    if (charset == Charset::utf16 && bytes.size() >= 2U) {
        if (bytes[0] == 0xFEU && bytes[1] == 0xFFU) {
            little_endian = false;
            index = 2U;
        } else if (bytes[0] == 0xFFU && bytes[1] == 0xFEU) {
            little_endian = true;
            index = 2U;
        }
    }
    output.reserve((bytes.size() - index + 1U) / 2U);
    while (index + 1U < bytes.size()) {
        const u16 value = little_endian
            ? static_cast<u16>(static_cast<u16>(bytes[index]) |
                               (static_cast<u16>(bytes[index + 1U]) << 8U))
            : static_cast<u16>((static_cast<u16>(bytes[index]) << 8U) |
                               static_cast<u16>(bytes[index + 1U]));
        output.push_back(static_cast<char16_t>(value));
        index += 2U;
    }
    if (index < bytes.size()) append_replacement(output);
    return output;
}

void append_utf8(std::vector<u8>& output, u32 code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<u8>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<u8>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<u8>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<u8>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<u8>(0x80U |
                                         ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<u8>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<u8>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<u8>(0x80U |
                                         ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<u8>(0x80U |
                                         ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<u8>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] std::vector<u8> encode_text(std::u16string_view text,
                                          Charset charset) {
    std::vector<u8> output;
    if (charset == Charset::latin1 || charset == Charset::ascii) {
        output.reserve(text.size());
        const u16 maximum = charset == Charset::latin1 ? 0x00FFU : 0x007FU;
        for (const char16_t character : text) {
            const u16 value = static_cast<u16>(character);
            output.push_back(value <= maximum ? static_cast<u8>(value)
                                              : static_cast<u8>('?'));
        }
        return output;
    }
    if (charset == Charset::utf16be || charset == Charset::utf16le ||
        charset == Charset::utf16) {
        const bool little_endian = charset == Charset::utf16le;
        output.reserve(text.size() * 2U + (charset == Charset::utf16 ? 2U : 0U));
        if (charset == Charset::utf16) {
            output.push_back(0xFEU);
            output.push_back(0xFFU);
        }
        for (const char16_t character : text) {
            const u16 value = static_cast<u16>(character);
            if (little_endian) {
                output.push_back(static_cast<u8>(value & 0xFFU));
                output.push_back(static_cast<u8>(value >> 8U));
            } else {
                output.push_back(static_cast<u8>(value >> 8U));
                output.push_back(static_cast<u8>(value & 0xFFU));
            }
        }
        return output;
    }

    output.reserve(text.size() * 3U);
    for (usize index = 0; index < text.size(); ++index) {
        u32 code_point = static_cast<u16>(text[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < text.size()) {
            const u32 low = static_cast<u16>(text[index + 1U]);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                             (low - 0xDC00U);
                ++index;
            } else {
                code_point = 0xFFFDU;
            }
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            code_point = 0xFFFDU;
        }
        append_utf8(output, code_point);
    }
    return output;
}

[[nodiscard]] Result<ObjectRef> create_byte_array(Machine& machine,
                                                  std::span<const u8> bytes) {
    auto array = machine.heap().allocate_array(
        "[B", bytes.size(), Value::from_int(0));
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < bytes.size(); ++index) {
        auto stored = machine.heap().set_element(
            *array, index,
            Value::from_int(static_cast<i32>(static_cast<i8>(bytes[index]))));
        if (!stored) return std::unexpected(stored.error());
    }
    return *array;
}

[[nodiscard]] Result<Charset> argument_charset(Machine& machine,
                                               ObjectRef name) {
    auto normalized = charset_name(machine, name);
    if (!normalized) return std::unexpected(normalized.error());
    return resolve_charset(*normalized);
}

void register_constructor(NativeMethodRegistry& registry,
                          std::string descriptor,
                          bool has_range,
                          bool has_charset) {
    add(registry, "java/lang/String", "<init>", std::move(descriptor),
        [has_range, has_charset](Machine& machine,
                                 std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto string = receiver(arguments);
            if (!string) return std::unexpected(string.error());
            auto bytes_reference = arguments[1].as_reference();
            if (!bytes_reference) return std::unexpected(bytes_reference.error());
            i32 offset = 0;
            i32 length = 0;
            usize cursor = 2U;
            if (has_range) {
                auto parsed_offset = arguments[cursor++].as_int();
                auto parsed_length = arguments[cursor++].as_int();
                if (!parsed_offset) return std::unexpected(parsed_offset.error());
                if (!parsed_length) return std::unexpected(parsed_length.error());
                offset = *parsed_offset;
                length = *parsed_length;
            } else {
                if (bytes_reference->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "String byte array is null");
                }
                auto array_length = machine.heap().array_length(*bytes_reference);
                if (!array_length) return std::unexpected(array_length.error());
                if (*array_length > static_cast<usize>(
                        std::numeric_limits<i32>::max())) {
                    return fail(ErrorCode::overflow,
                                "String byte array exceeds int range");
                }
                length = static_cast<i32>(*array_length);
            }
            // CLDC String(byte[]) follows microedition.encoding. phoneME's
            // platform default is ISO-8859-1, not UTF-8.
            Charset charset = Charset::latin1;
            if (has_charset) {
                auto name = arguments[cursor].as_reference();
                if (!name) return std::unexpected(name.error());
                auto resolved = argument_charset(machine, *name);
                if (!resolved) return std::unexpected(resolved.error());
                charset = *resolved;
            }
            auto bytes = byte_slice(machine, *bytes_reference, offset, length);
            if (!bytes) return std::unexpected(bytes.error());
            auto text = decode_bytes(*bytes, charset);
            auto attached = machine.heap().attach_string(*string, std::move(text));
            if (!attached) return std::unexpected(attached.error());
            return std::optional<Value> {};
        });
}

} // namespace

void register_string_encoding_natives(NativeMethodRegistry& registry) {
    register_constructor(registry, "([B)V", false, false);
    register_constructor(registry, "([BII)V", true, false);
    register_constructor(registry, "([BLjava/lang/String;)V", false, true);
    register_constructor(registry, "([BIILjava/lang/String;)V", true, true);
    register_constructor(registry, "([BLjava/nio/charset/Charset;)V", false, true);
    register_constructor(registry, "([BIILjava/nio/charset/Charset;)V", true, true);

    const auto get_bytes = [&registry](std::string descriptor,
                                       bool has_charset) {
        add(registry, "java/lang/String", "getBytes", std::move(descriptor),
            [has_charset](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto string = receiver(arguments);
                if (!string) return std::unexpected(string.error());
                // CLDC String.getBytes() follows microedition.encoding.
                // Keep this aligned with the exposed ISO8859_1 property and
                // the original phoneME i18n Helper fallback.
                Charset charset = Charset::latin1;
                if (has_charset) {
                    auto name = arguments[1].as_reference();
                    if (!name) return std::unexpected(name.error());
                    auto resolved = argument_charset(machine, *name);
                    if (!resolved) return std::unexpected(resolved.error());
                    charset = *resolved;
                }
                auto text = machine.heap().string_value(*string);
                if (!text) return std::unexpected(text.error());
                const std::vector<u8> bytes = encode_text(*text, charset);
                auto array = create_byte_array(machine, bytes);
                if (!array) return std::unexpected(array.error());
                return std::optional<Value>(Value::from_reference(*array));
            });
    };
    get_bytes("()[B", false);
    get_bytes("(Ljava/lang/String;)[B", true);
    get_bytes("(Ljava/nio/charset/Charset;)[B", true);
}

} // namespace phoneme::vm
