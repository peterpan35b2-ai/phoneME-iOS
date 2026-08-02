#include "FileNatives.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/filesystem/FileSystem.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr usize kFileStreamHandleField = 0;
constexpr usize kFileStreamClosedField = 1;
constexpr usize kConnectionPathField = 0;
constexpr usize kConnectionModeField = 1;
constexpr usize kConnectionOpenField = 2;
constexpr usize kFilterStreamField = 0;
constexpr usize kArrayEnumerationValuesField = 0;
constexpr usize kArrayEnumerationIndexField = 1;
constexpr usize kArrayEnumerationSizeField = 2;
constexpr i32 kConnectorRead = 1;
constexpr i32 kConnectorWrite = 2;
constexpr i32 kConnectorReadWrite = 3;

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
                    "file native has no receiver");
    }
    auto object = arguments.front().as_reference();
    if (!object || object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "file native receiver is null");
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

[[nodiscard]] Status set_int_field(Machine& machine,
                                   ObjectRef object,
                                   usize index,
                                   i32 value) {
    return machine.heap().set_field(object, index, Value::from_int(value));
}

[[nodiscard]] Status set_reference_field(Machine& machine,
                                         ObjectRef object,
                                         usize index,
                                         ObjectRef value) {
    return machine.heap().set_field(object, index,
                                    Value::from_reference(value));
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

[[nodiscard]] Result<std::u16string> utf16_text(std::string_view text) {
    std::u16string result;
    result.reserve(text.size());
    for (usize index = 0; index < text.size();) {
        const u8 first = static_cast<u8>(text[index]);
        u32 code_point = 0;
        usize count = 0;
        if (first <= 0x7FU) {
            code_point = first;
            count = 1;
        } else if ((first & 0xE0U) == 0xC0U) {
            code_point = first & 0x1FU;
            count = 2;
        } else if ((first & 0xF0U) == 0xE0U) {
            code_point = first & 0x0FU;
            count = 3;
        } else if ((first & 0xF8U) == 0xF0U) {
            code_point = first & 0x07U;
            count = 4;
        } else {
            return fail(ErrorCode::invalid_argument,
                        "filesystem returned invalid UTF-8");
        }
        if (index + count > text.size()) {
            return fail(ErrorCode::invalid_argument,
                        "filesystem returned truncated UTF-8");
        }
        for (usize continuation = 1; continuation < count; ++continuation) {
            const u8 byte = static_cast<u8>(text[index + continuation]);
            if ((byte & 0xC0U) != 0x80U) {
                return fail(ErrorCode::invalid_argument,
                            "filesystem returned invalid UTF-8 continuation");
            }
            code_point = (code_point << 6U) | (byte & 0x3FU);
        }
        if (code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return fail(ErrorCode::invalid_argument,
                        "filesystem returned invalid Unicode scalar");
        }
        if (code_point <= 0xFFFFU) {
            result.push_back(static_cast<char16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            result.push_back(static_cast<char16_t>(
                0xD800U | (code_point >> 10U)));
            result.push_back(static_cast<char16_t>(
                0xDC00U | (code_point & 0x3FFU)));
        }
        index += count;
    }
    return result;
}

[[nodiscard]] Result<ObjectRef> create_string(Machine& machine,
                                              std::string_view text) {
    auto decoded = utf16_text(text);
    if (!decoded) return std::unexpected(decoded.error());
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(*decoded));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] std::unexpected<Error> file_error(Error error,
                                                 bool opening = false) {
    if (error.code == ErrorCode::invalid_argument) {
        return fail_java("java/lang/SecurityException",
                         std::move(error.message));
    }
    return fail_java(opening ? "java/io/FileNotFoundException"
                             : "java/io/IOException",
                     std::move(error.message));
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

[[nodiscard]] Status validate_range(usize size, i32 offset, i32 length) {
    if (offset < 0 || length < 0 ||
        static_cast<usize>(offset) > size ||
        static_cast<usize>(length) > size - static_cast<usize>(offset)) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "byte array range is invalid");
    }
    return {};
}

[[nodiscard]] Result<std::vector<u8>> read_byte_array(
    Machine& machine,
    ObjectRef array,
    i32 offset,
    i32 length) {
    auto size = byte_array_length(machine, array);
    if (!size) return std::unexpected(size.error());
    auto valid = validate_range(*size, offset, length);
    if (!valid) return std::unexpected(valid.error());
    std::vector<u8> bytes;
    bytes.reserve(static_cast<usize>(length));
    for (i32 index = 0; index < length; ++index) {
        auto value = machine.heap().element(
            array, static_cast<usize>(offset + index));
        if (!value) return std::unexpected(value.error());
        auto integer = value->as_int();
        if (!integer) return std::unexpected(integer.error());
        bytes.push_back(static_cast<u8>(static_cast<i8>(*integer)));
    }
    return bytes;
}

[[nodiscard]] Status write_byte_array(Machine& machine,
                                      ObjectRef array,
                                      i32 offset,
                                      std::span<const u8> bytes) {
    auto size = byte_array_length(machine, array);
    if (!size) return std::unexpected(size.error());
    if (bytes.size() > static_cast<usize>(std::numeric_limits<i32>::max())) {
        return fail_java("java/lang/OutOfMemoryError",
                         "native file read exceeds Java array range");
    }
    auto valid = validate_range(*size, offset,
                                static_cast<i32>(bytes.size()));
    if (!valid) return valid;
    for (usize index = 0; index < bytes.size(); ++index) {
        auto stored = machine.heap().set_element(
            array, static_cast<usize>(offset) + index,
            Value::from_int(static_cast<i32>(
                static_cast<i8>(bytes[index]))));
        if (!stored) return stored;
    }
    return {};
}

[[nodiscard]] Result<i32> stream_handle(Machine& machine,
                                        ObjectRef stream) {
    auto closed = int_field(machine, stream, kFileStreamClosedField);
    if (!closed) return std::unexpected(closed.error());
    if (*closed != 0) {
        return fail_java("java/io/IOException", "file stream is closed");
    }
    return int_field(machine, stream, kFileStreamHandleField);
}

[[nodiscard]] Status initialize_stream(Machine& machine,
                                       ObjectRef stream,
                                       i32 handle) {
    auto stored_handle = set_int_field(machine, stream,
                                       kFileStreamHandleField, handle);
    if (!stored_handle) return stored_handle;
    return set_int_field(machine, stream, kFileStreamClosedField, 0);
}

[[nodiscard]] Result<std::string> normalize_path_argument(
    Machine& machine,
    ObjectRef path) {
    auto text = utf8_text(machine, path);
    if (!text) return std::unexpected(text.error());
    if (text->starts_with("file:")) {
        return filesystem::path_from_file_url(*text);
    }
    return filesystem::normalize_virtual_path(*text);
}

[[nodiscard]] Result<ObjectRef> create_file_input_stream(
    Machine& machine,
    std::string_view path) {
    auto handle = machine.filesystem().open(path,
                                            filesystem::OpenMode::read,
                                            false, false);
    if (!handle) return file_error(handle.error(), true);
    auto stream = machine.class_states().allocate_instance(
        machine.heap(), "java/io/FileInputStream");
    if (!stream) {
        static_cast<void>(machine.filesystem().close(*handle));
        return std::unexpected(stream.error());
    }
    auto initialized = initialize_stream(machine, *stream, *handle);
    if (!initialized) {
        static_cast<void>(machine.filesystem().close(*handle));
        return std::unexpected(initialized.error());
    }
    return *stream;
}

[[nodiscard]] Result<ObjectRef> create_file_output_stream(
    Machine& machine,
    std::string_view path,
    bool append,
    std::optional<i64> offset = std::nullopt) {
    const auto mode = append ? filesystem::OpenMode::append
                             : filesystem::OpenMode::read_write;
    auto handle = machine.filesystem().open(path, mode, true,
                                            !append && !offset.has_value());
    if (!handle) return file_error(handle.error(), true);
    if (offset.has_value()) {
        auto size = machine.filesystem().size(*handle);
        if (!size || *offset < 0 || *offset > *size) {
            static_cast<void>(machine.filesystem().close(*handle));
            return fail_java("java/io/IOException",
                             "output stream offset is outside the file");
        }
        auto positioned = machine.filesystem().seek(
            *handle, *offset, filesystem::SeekOrigin::begin);
        if (!positioned) {
            static_cast<void>(machine.filesystem().close(*handle));
            return file_error(positioned.error());
        }
    }
    auto stream = machine.class_states().allocate_instance(
        machine.heap(), "java/io/FileOutputStream");
    if (!stream) {
        static_cast<void>(machine.filesystem().close(*handle));
        return std::unexpected(stream.error());
    }
    auto initialized = initialize_stream(machine, *stream, *handle);
    if (!initialized) {
        static_cast<void>(machine.filesystem().close(*handle));
        return std::unexpected(initialized.error());
    }
    return *stream;
}

[[nodiscard]] Result<ObjectRef> wrap_data_input(Machine& machine,
                                                ObjectRef input) {
    auto stream = machine.class_states().allocate_instance(
        machine.heap(), "java/io/DataInputStream");
    if (!stream) return std::unexpected(stream.error());
    auto stored = set_reference_field(machine, *stream,
                                      kFilterStreamField, input);
    if (!stored) return std::unexpected(stored.error());
    return *stream;
}

[[nodiscard]] Result<ObjectRef> wrap_data_output(Machine& machine,
                                                 ObjectRef output) {
    auto stream = machine.class_states().allocate_instance(
        machine.heap(), "java/io/DataOutputStream");
    if (!stream) return std::unexpected(stream.error());
    auto stored = set_reference_field(machine, *stream,
                                      kFilterStreamField, output);
    if (!stored) return std::unexpected(stored.error());
    return *stream;
}

struct ConnectionState final {
    ObjectRef object;
    std::string path;
    i32 mode {0};
};

[[nodiscard]] Result<ConnectionState> connection_state(
    Machine& machine,
    std::span<const Value> arguments,
    bool require_read = false,
    bool require_write = false) {
    auto object = receiver(arguments);
    if (!object) return std::unexpected(object.error());
    auto open = int_field(machine, *object, kConnectionOpenField);
    auto mode = int_field(machine, *object, kConnectionModeField);
    auto path_object = reference_field(machine, *object, kConnectionPathField);
    if (!open || !mode || !path_object) {
        return fail(ErrorCode::invalid_state,
                    "FileConnection state is invalid");
    }
    if (*open == 0) {
        return fail_java("java/io/IOException",
                         "FileConnection is closed");
    }
    if (require_read && *mode != kConnectorRead &&
        *mode != kConnectorReadWrite) {
        return fail_java("java/io/IOException",
                         "FileConnection is not open for reading");
    }
    if (require_write && *mode != kConnectorWrite &&
        *mode != kConnectorReadWrite) {
        return fail_java("java/io/IOException",
                         "FileConnection is not open for writing");
    }
    auto path = utf8_text(machine, *path_object);
    if (!path) return std::unexpected(path.error());
    return ConnectionState {
        .object = *object,
        .path = std::move(*path),
        .mode = *mode,
    };
}

[[nodiscard]] Result<ObjectRef> create_connection(Machine& machine,
                                                  ObjectRef url,
                                                  i32 mode) {
    if (mode != kConnectorRead && mode != kConnectorWrite &&
        mode != kConnectorReadWrite) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Connector mode must be READ, WRITE, or READ_WRITE");
    }
    auto text = utf8_text(machine, url);
    if (!text) return std::unexpected(text.error());
    auto path = filesystem::path_from_file_url(*text);
    if (!path) return file_error(path.error());
    auto path_string = create_string(machine, *path);
    if (!path_string) return std::unexpected(path_string.error());
    auto connection = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/io/file/FileConnectionImpl");
    if (!connection) return std::unexpected(connection.error());
    auto stored_path = set_reference_field(machine, *connection,
                                           kConnectionPathField,
                                           *path_string);
    auto stored_mode = set_int_field(machine, *connection,
                                     kConnectionModeField, mode);
    auto stored_open = set_int_field(machine, *connection,
                                     kConnectionOpenField, 1);
    if (!stored_path) return std::unexpected(stored_path.error());
    if (!stored_mode) return std::unexpected(stored_mode.error());
    if (!stored_open) return std::unexpected(stored_open.error());
    return *connection;
}

[[nodiscard]] bool wildcard_match(std::string_view pattern,
                                  std::string_view text) noexcept {
    usize pattern_index = 0;
    usize text_index = 0;
    usize star = std::string_view::npos;
    usize checkpoint = 0;
    while (text_index < text.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == '?' ||
             pattern[pattern_index] == text[text_index])) {
            ++pattern_index;
            ++text_index;
        } else if (pattern_index < pattern.size() &&
                   pattern[pattern_index] == '*') {
            star = pattern_index++;
            checkpoint = text_index;
        } else if (star != std::string_view::npos) {
            pattern_index = star + 1U;
            text_index = ++checkpoint;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

[[nodiscard]] Result<ObjectRef> create_enumeration(
    Machine& machine,
    const std::vector<std::string>& names) {
    auto values = machine.heap().allocate_array(
        "[Ljava/lang/Object;", names.size(), Value::from_reference({}));
    if (!values) return std::unexpected(values.error());
    for (usize index = 0; index < names.size(); ++index) {
        auto name = create_string(machine, names[index]);
        if (!name) return std::unexpected(name.error());
        auto stored = machine.heap().set_element(
            *values, index, Value::from_reference(*name));
        if (!stored) return std::unexpected(stored.error());
    }
    auto enumeration = machine.class_states().allocate_instance(
        machine.heap(), "java/util/ArrayEnumeration");
    if (!enumeration) return std::unexpected(enumeration.error());
    auto stored_values = set_reference_field(machine, *enumeration,
                                              kArrayEnumerationValuesField,
                                              *values);
    auto stored_index = set_int_field(machine, *enumeration,
                                      kArrayEnumerationIndexField, 0);
    auto stored_size = set_int_field(machine, *enumeration,
                                     kArrayEnumerationSizeField,
                                     static_cast<i32>(names.size()));
    if (!stored_values) return std::unexpected(stored_values.error());
    if (!stored_index) return std::unexpected(stored_index.error());
    if (!stored_size) return std::unexpected(stored_size.error());
    return *enumeration;
}

[[nodiscard]] std::string parent_path(std::string_view path) {
    const usize slash = path.rfind('/');
    return slash == std::string_view::npos
               ? std::string {}
               : std::string(path.substr(0, slash));
}

[[nodiscard]] std::string leaf_name(std::string_view path) {
    const usize slash = path.rfind('/');
    return std::string(path.substr(slash == std::string_view::npos
                                       ? 0U
                                       : slash + 1U));
}

void register_file_stream_natives(NativeMethodRegistry& registry) {
    add(registry, "java/io/FileInputStream", "<init>",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto path_object = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!path_object || path_object->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "file path is null");
            }
            auto path = normalize_path_argument(machine, *path_object);
            if (!path) return file_error(path.error());
            auto handle = machine.filesystem().open(
                *path, filesystem::OpenMode::read, false, false);
            if (!handle) return file_error(handle.error(), true);
            auto initialized = initialize_stream(machine, *object, *handle);
            if (!initialized) {
                static_cast<void>(machine.filesystem().close(*handle));
                return std::unexpected(initialized.error());
            }
            return std::optional<Value> {};
        });

    const auto output_constructor = [&registry](bool with_append) {
        add(registry, "java/io/FileOutputStream", "<init>",
            with_append ? "(Ljava/lang/String;Z)V"
                        : "(Ljava/lang/String;)V",
            [with_append](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto path_object = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!path_object || path_object->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "file path is null");
                }
                bool append = false;
                if (with_append) {
                    auto value = arguments[2].as_int();
                    if (!value) return std::unexpected(value.error());
                    append = *value != 0;
                }
                auto path = normalize_path_argument(machine, *path_object);
                if (!path) return file_error(path.error());
                auto handle = machine.filesystem().open(
                    *path,
                    append ? filesystem::OpenMode::append
                           : filesystem::OpenMode::write,
                    true, !append);
                if (!handle) return file_error(handle.error(), true);
                auto initialized = initialize_stream(machine, *object, *handle);
                if (!initialized) {
                    static_cast<void>(machine.filesystem().close(*handle));
                    return std::unexpected(initialized.error());
                }
                return std::optional<Value> {};
            });
    };
    output_constructor(false);
    output_constructor(true);

    add(registry, "java/io/FileInputStream", "read", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = file_input_read_one(machine, *object);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    const auto register_input_array = [&registry](bool ranged) {
        add(registry, "java/io/FileInputStream", "read",
            ranged ? "([BII)I" : "([B)I",
            [ranged](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto array = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!array) return std::unexpected(array.error());
                auto size = byte_array_length(machine, *array);
                if (!size) return std::unexpected(size.error());
                i32 offset = 0;
                i32 length = static_cast<i32>(*size);
                if (ranged) {
                    auto parsed_offset = arguments[2].as_int();
                    auto parsed_length = arguments[3].as_int();
                    if (!parsed_offset || !parsed_length) {
                        return fail(ErrorCode::invalid_argument,
                                    "file read range is invalid");
                    }
                    offset = *parsed_offset;
                    length = *parsed_length;
                }
                auto valid = validate_range(*size, offset, length);
                if (!valid) return std::unexpected(valid.error());
                if (length == 0) {
                    return std::optional<Value>(Value::from_int(0));
                }
                auto handle = stream_handle(machine, *object);
                if (!handle) return std::unexpected(handle.error());
                std::vector<u8> bytes(static_cast<usize>(length));
                auto count = machine.filesystem().read(*handle, bytes);
                if (!count) return file_error(count.error());
                if (*count == 0U) {
                    return std::optional<Value>(Value::from_int(-1));
                }
                bytes.resize(*count);
                auto stored = write_byte_array(machine, *array, offset, bytes);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_int(
                    static_cast<i32>(*count)));
            });
    };
    register_input_array(false);
    register_input_array(true);
    add(registry, "java/io/FileInputStream", "skip", "(J)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto requested = arguments[1].as_long();
            if (!object) return std::unexpected(object.error());
            if (!requested) return std::unexpected(requested.error());
            auto skipped = file_input_skip(machine, *object, *requested);
            if (!skipped) return std::unexpected(skipped.error());
            return std::optional<Value>(Value::from_long(*skipped));
        });
    add(registry, "java/io/FileInputStream", "available", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto available = file_input_available(machine, *object);
            if (!available) return std::unexpected(available.error());
            return std::optional<Value>(Value::from_int(*available));
        });
    add(registry, "java/io/FileInputStream", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = file_input_close(machine, *object);
            if (!closed) return std::unexpected(closed.error());
            return std::optional<Value> {};
        });

    add(registry, "java/io/FileOutputStream", "write", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto written = file_output_write_one(
                machine, *object, static_cast<u8>(*value));
            if (!written) return std::unexpected(written.error());
            return std::optional<Value> {};
        });
    const auto register_output_array = [&registry](bool ranged) {
        add(registry, "java/io/FileOutputStream", "write",
            ranged ? "([BII)V" : "([B)V",
            [ranged](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto array = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!array) return std::unexpected(array.error());
                auto size = byte_array_length(machine, *array);
                if (!size) return std::unexpected(size.error());
                i32 offset = 0;
                i32 length = static_cast<i32>(*size);
                if (ranged) {
                    auto parsed_offset = arguments[2].as_int();
                    auto parsed_length = arguments[3].as_int();
                    if (!parsed_offset || !parsed_length) {
                        return fail(ErrorCode::invalid_argument,
                                    "file write range is invalid");
                    }
                    offset = *parsed_offset;
                    length = *parsed_length;
                }
                auto bytes = read_byte_array(machine, *array, offset, length);
                if (!bytes) return std::unexpected(bytes.error());
                auto handle = stream_handle(machine, *object);
                if (!handle) return std::unexpected(handle.error());
                auto count = machine.filesystem().write(*handle, *bytes);
                if (!count) return file_error(count.error());
                return std::optional<Value> {};
            });
    };
    register_output_array(false);
    register_output_array(true);
    add(registry, "java/io/FileOutputStream", "flush", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto flushed = file_output_flush(machine, *object);
            if (!flushed) return std::unexpected(flushed.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/FileOutputStream", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = file_output_close(machine, *object);
            if (!closed) return std::unexpected(closed.error());
            return std::optional<Value> {};
        });
}

void register_file_connection_natives(NativeMethodRegistry& registry) {
    constexpr const char* owner =
        "javax/microedition/io/file/FileConnectionImpl";
    add(registry, owner, "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = set_int_field(machine, *object,
                                        kConnectionOpenField, 0);
            if (!closed) return std::unexpected(closed.error());
            return std::optional<Value> {};
        });

    const auto register_stat_boolean = [&registry](const char* name,
                                                   auto selector) {
        add(registry, owner, name, "()Z",
            [selector](Machine& machine,
                       std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto state = connection_state(machine, arguments);
                if (!state) return std::unexpected(state.error());
                auto info = machine.filesystem().stat(state->path);
                if (!info) return file_error(info.error());
                return std::optional<Value>(Value::from_int(
                    selector(*info) ? 1 : 0));
            });
    };
    register_stat_boolean("exists", [](const filesystem::FileInfo& info) {
        return info.exists;
    });
    register_stat_boolean("isDirectory", [](const filesystem::FileInfo& info) {
        return info.exists && info.directory;
    });
    register_stat_boolean("canRead", [](const filesystem::FileInfo& info) {
        return info.readable;
    });
    register_stat_boolean("canWrite", [](const filesystem::FileInfo& info) {
        return info.writable;
    });
    register_stat_boolean("isHidden", [](const filesystem::FileInfo& info) {
        return info.hidden;
    });

    add(registry, owner, "fileSize", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, true);
            if (!state) return std::unexpected(state.error());
            auto info = machine.filesystem().stat(state->path);
            if (!info) return file_error(info.error());
            if (!info->exists || info->directory) {
                return fail_java("java/io/IOException",
                                 "FileConnection does not name a file");
            }
            return std::optional<Value>(Value::from_long(
                static_cast<i64>(info->size)));
        });
    add(registry, owner, "lastModified", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments);
            if (!state) return std::unexpected(state.error());
            auto info = machine.filesystem().stat(state->path);
            if (!info) return file_error(info.error());
            return std::optional<Value>(Value::from_long(
                info->exists ? info->modified_seconds * 1000 : 0));
        });

    const auto register_string_property = [&registry](const char* name,
                                                       auto selector) {
        add(registry, owner, name, "()Ljava/lang/String;",
            [selector](Machine& machine,
                       std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto state = connection_state(machine, arguments);
                if (!state) return std::unexpected(state.error());
                auto text = create_string(machine, selector(state->path));
                if (!text) return std::unexpected(text.error());
                return std::optional<Value>(Value::from_reference(*text));
            });
    };
    register_string_property("getName", [](const std::string& path) {
        return leaf_name(path);
    });
    register_string_property("getPath", [](const std::string& path) {
        const std::string parent = parent_path(path);
        return parent.empty() ? std::string("/")
                              : "/" + parent + "/";
    });
    register_string_property("getURL", [](const std::string& path) {
        return "file:///" + path;
    });

    const auto register_mutation = [&registry](const char* name,
                                               auto operation) {
        add(registry, owner, name, "()V",
            [operation](Machine& machine,
                        std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto state = connection_state(machine, arguments,
                                              false, true);
                if (!state) return std::unexpected(state.error());
                auto status = operation(machine.filesystem(), state->path);
                if (!status) return file_error(status.error());
                return std::optional<Value> {};
            });
    };
    register_mutation("create", [](filesystem::FileSystem& files,
                                    const std::string& path) {
        return files.create_file(path);
    });
    register_mutation("mkdir", [](filesystem::FileSystem& files,
                                   const std::string& path) {
        return files.create_directory(path);
    });
    register_mutation("delete", [](filesystem::FileSystem& files,
                                    const std::string& path) {
        return files.remove(path, false);
    });

    add(registry, owner, "truncate", "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, false, true);
            auto length = arguments[1].as_long();
            if (!state) return std::unexpected(state.error());
            if (!length) return std::unexpected(length.error());
            if (*length < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "truncate length is negative");
            }
            auto status = machine.filesystem().truncate(
                state->path, static_cast<u64>(*length));
            if (!status) return file_error(status.error());
            return std::optional<Value> {};
        });
    add(registry, owner, "rename", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, false, true);
            auto name_object = arguments[1].as_reference();
            if (!state) return std::unexpected(state.error());
            if (!name_object || name_object->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "new file name is null");
            }
            auto name = utf8_text(machine, *name_object);
            if (!name) return std::unexpected(name.error());
            if (name->empty() || name->find('/') != std::string::npos ||
                name->find('\\') != std::string::npos || *name == "." ||
                *name == "..") {
                return fail_java("java/lang/IllegalArgumentException",
                                 "rename requires one file name");
            }
            std::string destination = parent_path(state->path);
            if (!destination.empty()) destination.push_back('/');
            destination.append(*name);
            auto renamed = machine.filesystem().rename(state->path,
                                                       destination);
            if (!renamed) return file_error(renamed.error());
            auto string = create_string(machine, destination);
            if (!string) return std::unexpected(string.error());
            auto stored = set_reference_field(machine, state->object,
                                              kConnectionPathField, *string);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    const auto register_list = [&registry](bool filtered) {
        add(registry, owner, "list",
            filtered ? "(Ljava/lang/String;Z)Ljava/util/Enumeration;"
                     : "()Ljava/util/Enumeration;",
            [filtered](Machine& machine,
                       std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto state = connection_state(machine, arguments, true);
                if (!state) return std::unexpected(state.error());
                std::string filter = "*";
                bool include_hidden = false;
                if (filtered) {
                    auto filter_object = arguments[1].as_reference();
                    auto hidden = arguments[2].as_int();
                    if (!filter_object || filter_object->is_null()) {
                        return fail_java("java/lang/NullPointerException",
                                         "file filter is null");
                    }
                    if (!hidden) return std::unexpected(hidden.error());
                    auto parsed = utf8_text(machine, *filter_object);
                    if (!parsed) return std::unexpected(parsed.error());
                    filter = std::move(*parsed);
                    include_hidden = *hidden != 0;
                }
                auto names = machine.filesystem().list(state->path);
                if (!names) return file_error(names.error());
                std::vector<std::string> selected;
                for (const std::string& name : *names) {
                    if (!include_hidden && !name.empty() &&
                        name.front() == '.') continue;
                    if (wildcard_match(filter, name)) selected.push_back(name);
                }
                auto enumeration = create_enumeration(machine, selected);
                if (!enumeration) return std::unexpected(enumeration.error());
                return std::optional<Value>(
                    Value::from_reference(*enumeration));
            });
    };
    register_list(false);
    register_list(true);

    add(registry, owner, "openInputStream", "()Ljava/io/InputStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, true);
            if (!state) return std::unexpected(state.error());
            auto stream = create_file_input_stream(machine, state->path);
            if (!stream) return std::unexpected(stream.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
    add(registry, owner, "openDataInputStream",
        "()Ljava/io/DataInputStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, true);
            if (!state) return std::unexpected(state.error());
            auto input = create_file_input_stream(machine, state->path);
            if (!input) return std::unexpected(input.error());
            auto stream = wrap_data_input(machine, *input);
            if (!stream) return std::unexpected(stream.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
    add(registry, owner, "openOutputStream", "()Ljava/io/OutputStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, false, true);
            if (!state) return std::unexpected(state.error());
            auto stream = create_file_output_stream(machine, state->path,
                                                    false);
            if (!stream) return std::unexpected(stream.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
    add(registry, owner, "openOutputStream", "(J)Ljava/io/OutputStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, false, true);
            auto offset = arguments[1].as_long();
            if (!state) return std::unexpected(state.error());
            if (!offset) return std::unexpected(offset.error());
            auto stream = create_file_output_stream(machine, state->path,
                                                    false, *offset);
            if (!stream) return std::unexpected(stream.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
    add(registry, owner, "openDataOutputStream",
        "()Ljava/io/DataOutputStream;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto state = connection_state(machine, arguments, false, true);
            if (!state) return std::unexpected(state.error());
            auto output = create_file_output_stream(machine, state->path,
                                                     false);
            if (!output) return std::unexpected(output.error());
            auto stream = wrap_data_output(machine, *output);
            if (!stream) return std::unexpected(stream.error());
            return std::optional<Value>(Value::from_reference(*stream));
        });
}

} // namespace

Result<std::optional<ObjectRef>> try_open_file_connection(
    Machine& machine,
    ObjectRef url,
    i32 mode) {
    auto text = utf8_text(machine, url);
    if (!text) return std::unexpected(text.error());
    if (!text->starts_with("file:")) return std::optional<ObjectRef> {};
    auto connection = create_connection(machine, url, mode);
    if (!connection) return std::unexpected(connection.error());
    return std::optional<ObjectRef>(*connection);
}

Result<std::optional<ObjectRef>> try_open_file_connection_stream(
    Machine& machine,
    ObjectRef connection,
    bool input,
    bool data) {
    auto is_file = machine.object_is_instance(
        connection, "javax/microedition/io/file/FileConnection");
    if (!is_file) return std::unexpected(is_file.error());
    if (!*is_file) return std::optional<ObjectRef> {};

    const std::array<Value, 1> arguments {
        Value::from_reference(connection),
    };
    auto state = connection_state(machine, arguments, input, !input);
    if (!state) return std::unexpected(state.error());
    Result<ObjectRef> stream = input
        ? create_file_input_stream(machine, state->path)
        : create_file_output_stream(machine, state->path, false);
    if (!stream) return std::unexpected(stream.error());
    if (data) {
        stream = input ? wrap_data_input(machine, *stream)
                       : wrap_data_output(machine, *stream);
        if (!stream) return std::unexpected(stream.error());
    }
    return std::optional<ObjectRef>(*stream);
}

Result<std::optional<bool>> try_close_file_connection(
    Machine& machine,
    ObjectRef connection) {
    auto is_file = machine.object_is_instance(
        connection, "javax/microedition/io/file/FileConnection");
    if (!is_file) return std::unexpected(is_file.error());
    if (!*is_file) return std::optional<bool> {};
    auto closed = set_int_field(machine, connection, kConnectionOpenField, 0);
    if (!closed) return std::unexpected(closed.error());
    return std::optional<bool>(true);
}

Result<i32> file_input_read_one(Machine& machine, ObjectRef stream) {
    auto handle = stream_handle(machine, stream);
    if (!handle) return std::unexpected(handle.error());
    std::array<u8, 1> byte {};
    auto count = machine.filesystem().read(*handle, byte);
    if (!count) return file_error(count.error());
    return *count == 0U ? -1 : static_cast<i32>(byte.front());
}

Result<i64> file_input_skip(Machine& machine,
                            ObjectRef stream,
                            i64 requested) {
    if (requested <= 0) return 0;
    auto handle = stream_handle(machine, stream);
    if (!handle) return std::unexpected(handle.error());
    auto available = machine.filesystem().available(*handle);
    if (!available) return file_error(available.error());
    const i64 amount = std::min(requested, *available);
    auto position = machine.filesystem().seek(
        *handle, amount, filesystem::SeekOrigin::current);
    if (!position) return file_error(position.error());
    return amount;
}

Result<i32> file_input_available(Machine& machine, ObjectRef stream) {
    auto handle = stream_handle(machine, stream);
    if (!handle) return std::unexpected(handle.error());
    auto available = machine.filesystem().available(*handle);
    if (!available) return file_error(available.error());
    return static_cast<i32>(std::min<i64>(
        *available, std::numeric_limits<i32>::max()));
}

Status file_input_close(Machine& machine, ObjectRef stream) {
    auto closed = int_field(machine, stream, kFileStreamClosedField);
    if (!closed) return std::unexpected(closed.error());
    if (*closed != 0) return {};
    auto handle = int_field(machine, stream, kFileStreamHandleField);
    if (!handle) return std::unexpected(handle.error());
    auto status = machine.filesystem().close(*handle);
    if (!status) return file_error(status.error());
    return set_int_field(machine, stream, kFileStreamClosedField, 1);
}

Status file_output_write_one(Machine& machine,
                             ObjectRef stream,
                             u8 byte) {
    auto handle = stream_handle(machine, stream);
    if (!handle) return std::unexpected(handle.error());
    const std::array<u8, 1> bytes {byte};
    auto count = machine.filesystem().write(*handle, bytes);
    if (!count) return file_error(count.error());
    return {};
}

Status file_output_flush(Machine& machine, ObjectRef stream) {
    auto handle = stream_handle(machine, stream);
    if (!handle) return std::unexpected(handle.error());
    auto status = machine.filesystem().flush(*handle);
    if (!status) return file_error(status.error());
    return {};
}

Status file_output_close(Machine& machine, ObjectRef stream) {
    auto closed = int_field(machine, stream, kFileStreamClosedField);
    if (!closed) return std::unexpected(closed.error());
    if (*closed != 0) return {};
    auto handle = int_field(machine, stream, kFileStreamHandleField);
    if (!handle) return std::unexpected(handle.error());
    auto flushed = machine.filesystem().flush(*handle);
    auto closed_status = machine.filesystem().close(*handle);
    auto marked = set_int_field(machine, stream, kFileStreamClosedField, 1);
    if (!flushed) return file_error(flushed.error());
    if (!closed_status) return file_error(closed_status.error());
    return marked;
}

void register_file_natives(NativeMethodRegistry& registry) {
    register_file_stream_natives(registry);
    register_file_connection_natives(registry);
}

} // namespace phoneme::vm
