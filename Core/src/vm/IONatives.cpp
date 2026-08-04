#include "IONatives.hpp"

#include "ConnectionNatives.hpp"
#include "FileNatives.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/ModifiedUtf8.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr usize kFilterStreamField = 0;
constexpr usize kByteInputBufferField = 0;
constexpr usize kByteInputPositionField = 1;
constexpr usize kByteInputMarkField = 2;
constexpr usize kByteInputCountField = 3;
constexpr usize kByteOutputBufferField = 0;
constexpr usize kByteOutputCountField = 1;
constexpr usize kDataOutputWrittenField = 1;
constexpr usize kReaderLockField = 0;
constexpr usize kReaderInputField = 1;
constexpr usize kReaderCharsetField = 2;
constexpr usize kReaderPendingField = 3;
constexpr usize kReaderClosedField = 4;
constexpr usize kWriterLockField = 0;
constexpr usize kWriterOutputField = 1;
constexpr usize kWriterCharsetField = 2;
constexpr usize kWriterPendingField = 3;
constexpr usize kWriterClosedField = 4;
constexpr usize kMaximumStreamDepth = 64;

enum class StreamCharset : i32 {
    utf8 = 1,
    latin1 = 2,
    ascii = 3,
    utf16be = 4,
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
    if (!registered) {
        std::terminate();
    }
}

[[nodiscard]] Result<ObjectRef> receiver(
    std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "java.io method has no receiver");
    }
    auto reference = arguments.front().as_reference();
    if (!reference || reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "java.io receiver is null");
    }
    return *reference;
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

[[nodiscard]] Result<bool> is_instance(Machine& machine,
                                       ObjectRef object,
                                       std::string_view class_name) {
    auto source = machine.heap().class_name(object);
    if (!source) return std::unexpected(source.error());
    return machine.classes().is_assignable(*source, class_name);
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

[[nodiscard]] Result<u8> byte_array_value(Machine& machine,
                                          ObjectRef array,
                                          usize index) {
    auto value = machine.heap().element(array, index);
    if (!value) return std::unexpected(value.error());
    auto integer = value->as_int();
    if (!integer) return std::unexpected(integer.error());
    return static_cast<u8>(static_cast<i8>(*integer));
}

[[nodiscard]] Status set_byte_array_value(Machine& machine,
                                          ObjectRef array,
                                          usize index,
                                          u8 value) {
    return machine.heap().set_element(
        array, index,
        Value::from_int(static_cast<i32>(static_cast<i8>(value))));
}

[[nodiscard]] Status validate_range(usize array_length,
                                    i32 offset,
                                    i32 length) {
    if (offset < 0 || length < 0 ||
        static_cast<usize>(offset) > array_length ||
        static_cast<usize>(length) >
            array_length - static_cast<usize>(offset)) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "byte array range is invalid");
    }
    return {};
}

[[nodiscard]] Result<ObjectRef> allocate_byte_array(Machine& machine,
                                                     usize length) {
    return machine.heap().allocate_array(
        "[B", length, Value::from_int(0));
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

[[nodiscard]] Result<usize> char_array_length(Machine& machine,
                                              ObjectRef array) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "char array is null");
    }
    auto class_name = machine.heap().class_name(array);
    if (!class_name || *class_name != "[C") {
        return fail_java("java/lang/IllegalArgumentException",
                         "value is not char[]");
    }
    return machine.heap().array_length(array);
}

[[nodiscard]] Result<char16_t> char_array_value(Machine& machine,
                                                ObjectRef array,
                                                usize index) {
    auto value = machine.heap().element(array, index);
    if (!value) return std::unexpected(value.error());
    auto integer = value->as_int();
    if (!integer) return std::unexpected(integer.error());
    return static_cast<char16_t>(static_cast<u16>(*integer));
}

[[nodiscard]] Status set_char_array_value(Machine& machine,
                                          ObjectRef array,
                                          usize index,
                                          char16_t value) {
    return machine.heap().set_element(
        array, index, Value::from_int(static_cast<i32>(value)));
}

[[nodiscard]] Status validate_char_range(usize array_length,
                                         i32 offset,
                                         i32 length) {
    if (offset < 0 || length < 0 ||
        static_cast<usize>(offset) > array_length ||
        static_cast<usize>(length) >
            array_length - static_cast<usize>(offset)) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "char array range is invalid");
    }
    return {};
}

[[nodiscard]] Result<StreamCharset> resolve_stream_charset(
    Machine& machine,
    ObjectRef name) {
    if (name.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "charset name is null");
    }
    auto text = machine.heap().string_value(name);
    if (!text) return std::unexpected(text.error());
    std::string normalized;
    normalized.reserve(text->size());
    for (const char16_t character : *text) {
        if (character > 0x7FU) {
            return fail_java("java/io/UnsupportedEncodingException",
                             "charset name is not ASCII");
        }
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '-' || byte == '_' || std::isspace(byte) != 0) continue;
        normalized.push_back(static_cast<char>(std::toupper(byte)));
    }
    if (normalized == "UTF8") return StreamCharset::utf8;
    if (normalized == "ISO88591" || normalized == "LATIN1") {
        return StreamCharset::latin1;
    }
    if (normalized == "USASCII" || normalized == "ASCII") {
        return StreamCharset::ascii;
    }
    if (normalized == "UTF16BE" ||
        normalized == "UNICODEBIGUNMARKED") {
        return StreamCharset::utf16be;
    }
    return fail_java("java/io/UnsupportedEncodingException",
                     "unsupported reader/writer charset");
}

[[nodiscard]] std::u16string charset_display_name(StreamCharset charset) {
    switch (charset) {
    case StreamCharset::utf8:
        return u"UTF-8";
    case StreamCharset::latin1:
        return u"ISO-8859-1";
    case StreamCharset::ascii:
        return u"US-ASCII";
    case StreamCharset::utf16be:
        return u"UTF-16BE";
    }
    return u"ISO-8859-1";
}

[[nodiscard]] Result<i32> stream_read_one(Machine& machine,
                                          ObjectRef stream,
                                          usize depth);
[[nodiscard]] Status stream_write_one(Machine& machine,
                                      ObjectRef stream,
                                      u8 byte,
                                      usize depth);

[[nodiscard]] Result<i32> byte_input_read_one(Machine& machine,
                                               ObjectRef stream) {
    auto position = int_field(machine, stream, kByteInputPositionField);
    auto count = int_field(machine, stream, kByteInputCountField);
    auto buffer = reference_field(machine, stream, kByteInputBufferField);
    if (!position || !count || !buffer) {
        return fail(ErrorCode::invalid_state,
                    "ByteArrayInputStream state is invalid");
    }
    if (*position >= *count) return -1;
    auto byte = byte_array_value(machine, *buffer,
                                 static_cast<usize>(*position));
    if (!byte) return std::unexpected(byte.error());
    auto updated = set_int_field(machine, stream,
                                 kByteInputPositionField,
                                 *position + 1);
    if (!updated) return std::unexpected(updated.error());
    return static_cast<i32>(*byte);
}

[[nodiscard]] Result<i32> stream_read_one(Machine& machine,
                                          ObjectRef stream,
                                          usize depth) {
    if (stream.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "input stream is null");
    }
    if (depth >= kMaximumStreamDepth) {
        return fail_java("java/io/IOException",
                         "input stream filter chain is too deep");
    }
    auto runtime_class = machine.heap().class_name(stream);
    if (!runtime_class) return std::unexpected(runtime_class.error());
    auto read_method = machine.classes().resolve_method(
        *runtime_class, "read", "()I");
    if (read_method && read_method->owner != nullptr &&
        read_method->owner->name() != "java/io/InputStream" &&
        read_method->owner->name() != "java/io/FilterInputStream" &&
        read_method->owner->name() != "java/io/DataInputStream" &&
        read_method->owner->name() != "java/io/ByteArrayInputStream" &&
        read_method->owner->name() != "java/io/FileInputStream") {
        auto invoked = machine.invoke_instance(stream, *runtime_class,
                                               "read", "()I");
        if (!invoked) return std::unexpected(invoked.error());
        if (invoked->throwable.has_value()) {
            auto thrown = machine.heap().class_name(*invoked->throwable);
            if (!thrown) return std::unexpected(thrown.error());
            return fail_java(*thrown, "custom InputStream.read threw");
        }
        if (!invoked->return_value.has_value()) {
            return fail(ErrorCode::internal_error,
                        "custom InputStream.read returned no value");
        }
        return invoked->return_value->as_int();
    }
    auto byte_array = is_instance(machine, stream,
                                  "java/io/ByteArrayInputStream");
    if (!byte_array) return std::unexpected(byte_array.error());
    if (*byte_array) return byte_input_read_one(machine, stream);

    auto file_input = is_instance(machine, stream,
                                  "java/io/FileInputStream");
    if (!file_input) return std::unexpected(file_input.error());
    if (*file_input) return file_input_read_one(machine, stream);

    auto network_input = connection_stream_read_one(machine, stream);
    if (!network_input) return std::unexpected(network_input.error());
    if (network_input->has_value()) return **network_input;

    auto data_input = is_instance(machine, stream,
                                  "java/io/DataInputStream");
    if (!data_input) return std::unexpected(data_input.error());
    if (*data_input) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_read_one(machine, *input, depth + 1U);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterInputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_read_one(machine, *input, depth + 1U);
    }
    return fail_java("java/io/IOException",
                     "input stream implementation is not connected");
}

[[nodiscard]] Result<i32> stream_read_bytes(Machine& machine,
                                            ObjectRef stream,
                                            ObjectRef destination,
                                            i32 offset,
                                            i32 length) {
    auto destination_length = byte_array_length(machine, destination);
    if (!destination_length) return std::unexpected(destination_length.error());
    auto range = validate_range(*destination_length, offset, length);
    if (!range) return std::unexpected(range.error());
    if (length == 0) return 0;

    // FilterInputStream/DataInputStream must preserve InputStream's partial
    // read contract. Reading one network byte at a time until the caller's
    // whole buffer is full can block forever when a server sends a short
    // packet and waits for the client's response. Delegate one bulk recv to
    // the native stream and return as soon as any bytes are available.
    auto network_read = connection_stream_read_range(
        machine, stream, destination, offset, length);
    if (!network_read) return std::unexpected(network_read.error());
    if (network_read->has_value()) return **network_read;

    auto data_input = is_instance(machine, stream,
                                  "java/io/DataInputStream");
    if (!data_input) return std::unexpected(data_input.error());
    if (*data_input) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_read_bytes(machine, *input, destination, offset, length);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterInputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_read_bytes(machine,
                                 *input,
                                 destination,
                                 offset,
                                 length);
    }

    i32 count = 0;
    while (count < length) {
        auto value = stream_read_one(machine, stream, 0);
        if (!value) return std::unexpected(value.error());
        if (*value < 0) break;
        auto stored = set_byte_array_value(
            machine, destination,
            static_cast<usize>(offset + count),
            static_cast<u8>(*value));
        if (!stored) return std::unexpected(stored.error());
        ++count;
    }
    return count == 0 ? -1 : count;
}

[[nodiscard]] Result<i64> stream_skip(Machine& machine,
                                      ObjectRef stream,
                                      i64 requested,
                                      usize depth) {
    if (requested <= 0) return 0;
    if (stream.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "input stream is null");
    }
    if (depth >= kMaximumStreamDepth) {
        return fail_java("java/io/IOException",
                         "input stream filter chain is too deep");
    }
    auto byte_array = is_instance(machine, stream,
                                  "java/io/ByteArrayInputStream");
    if (!byte_array) return std::unexpected(byte_array.error());
    if (*byte_array) {
        auto position = int_field(machine, stream, kByteInputPositionField);
        auto count = int_field(machine, stream, kByteInputCountField);
        if (!position || !count)
            return fail(ErrorCode::invalid_state,
                        "ByteArrayInputStream state is invalid");
        const i64 available = static_cast<i64>(*count - *position);
        const i64 skipped = std::min(requested, available);
        auto updated = set_int_field(machine, stream,
                                     kByteInputPositionField,
                                     *position + static_cast<i32>(skipped));
        if (!updated) return std::unexpected(updated.error());
        return skipped;
    }
    auto file_input = is_instance(machine, stream,
                                  "java/io/FileInputStream");
    if (!file_input) return std::unexpected(file_input.error());
    if (*file_input) return file_input_skip(machine, stream, requested);

    auto data_input = is_instance(machine, stream,
                                  "java/io/DataInputStream");
    if (!data_input) return std::unexpected(data_input.error());
    if (*data_input) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_skip(machine, *input, requested, depth + 1U);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterInputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_skip(machine, *input, requested, depth + 1U);
    }

    i64 skipped = 0;
    while (skipped < requested) {
        auto value = stream_read_one(machine, stream, depth + 1U);
        if (!value) return std::unexpected(value.error());
        if (*value < 0) break;
        ++skipped;
    }
    return skipped;
}

[[nodiscard]] Result<i32> stream_available(Machine& machine,
                                           ObjectRef stream,
                                           usize depth) {
    if (stream.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "input stream is null");
    }
    if (depth >= kMaximumStreamDepth) {
        return fail_java("java/io/IOException",
                         "input stream filter chain is too deep");
    }
    auto byte_array = is_instance(machine, stream,
                                  "java/io/ByteArrayInputStream");
    if (!byte_array) return std::unexpected(byte_array.error());
    if (*byte_array) {
        auto position = int_field(machine, stream, kByteInputPositionField);
        auto count = int_field(machine, stream, kByteInputCountField);
        if (!position || !count)
            return fail(ErrorCode::invalid_state,
                        "ByteArrayInputStream state is invalid");
        return *count - *position;
    }
    auto file_input = is_instance(machine, stream,
                                  "java/io/FileInputStream");
    if (!file_input) return std::unexpected(file_input.error());
    if (*file_input) return file_input_available(machine, stream);

    auto network_input = connection_stream_available(machine, stream);
    if (!network_input) return std::unexpected(network_input.error());
    if (network_input->has_value()) {
        return static_cast<i32>(std::min(
            **network_input,
            static_cast<usize>(std::numeric_limits<i32>::max())));
    }

    auto data_input = is_instance(machine, stream,
                                  "java/io/DataInputStream");
    if (!data_input) return std::unexpected(data_input.error());
    if (*data_input) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_available(machine, *input, depth + 1U);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterInputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_available(machine, *input, depth + 1U);
    }
    return 0;
}

[[nodiscard]] Status byte_output_ensure_capacity(Machine& machine,
                                                 ObjectRef stream,
                                                 i32 minimum) {
    auto buffer = reference_field(machine, stream, kByteOutputBufferField);
    if (!buffer) return std::unexpected(buffer.error());
    auto capacity = byte_array_length(machine, *buffer);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    usize new_capacity = *capacity == 0U ? 1U : *capacity * 2U;
    if (new_capacity < static_cast<usize>(minimum))
        new_capacity = static_cast<usize>(minimum);
    auto replacement = allocate_byte_array(machine, new_capacity);
    if (!replacement) return std::unexpected(replacement.error());
    auto count = int_field(machine, stream, kByteOutputCountField);
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto value = byte_array_value(machine, *buffer,
                                      static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto stored = set_byte_array_value(machine, *replacement,
                                           static_cast<usize>(index),
                                           *value);
        if (!stored) return stored;
    }
    return set_reference_field(machine, stream,
                               kByteOutputBufferField, *replacement);
}

[[nodiscard]] Status byte_output_write_one(Machine& machine,
                                            ObjectRef stream,
                                            u8 byte) {
    auto count = int_field(machine, stream, kByteOutputCountField);
    if (!count) return std::unexpected(count.error());
    if (*count == std::numeric_limits<i32>::max()) {
        return fail_java("java/lang/OutOfMemoryError",
                         "ByteArrayOutputStream exceeds int capacity");
    }
    auto capacity = byte_output_ensure_capacity(machine, stream, *count + 1);
    if (!capacity) return capacity;
    auto buffer = reference_field(machine, stream, kByteOutputBufferField);
    if (!buffer) return std::unexpected(buffer.error());
    auto stored = set_byte_array_value(machine, *buffer,
                                       static_cast<usize>(*count), byte);
    if (!stored) return stored;
    return set_int_field(machine, stream,
                         kByteOutputCountField, *count + 1);
}

[[nodiscard]] Status increment_data_written(Machine& machine,
                                            ObjectRef stream,
                                            i32 amount) {
    auto data_output = is_instance(machine, stream,
                                   "java/io/DataOutputStream");
    if (!data_output) return std::unexpected(data_output.error());
    if (!*data_output) return {};
    auto written = int_field(machine, stream, kDataOutputWrittenField);
    if (!written) return std::unexpected(written.error());
    const u32 updated = static_cast<u32>(*written) +
                        static_cast<u32>(amount);
    return set_int_field(machine, stream,
                         kDataOutputWrittenField,
                         static_cast<i32>(updated));
}

[[nodiscard]] Status stream_write_one(Machine& machine,
                                      ObjectRef stream,
                                      u8 byte,
                                      usize depth) {
    if (stream.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "output stream is null");
    }
    if (depth >= kMaximumStreamDepth) {
        return fail_java("java/io/IOException",
                         "output stream filter chain is too deep");
    }
    auto runtime_class = machine.heap().class_name(stream);
    if (!runtime_class) return std::unexpected(runtime_class.error());
    auto write_method = machine.classes().resolve_method(
        *runtime_class, "write", "(I)V");
    if (write_method && write_method->owner != nullptr &&
        write_method->owner->name() != "java/io/OutputStream" &&
        write_method->owner->name() != "java/io/FilterOutputStream" &&
        write_method->owner->name() != "java/io/ByteArrayOutputStream" &&
        write_method->owner->name() != "java/io/DataOutputStream" &&
        write_method->owner->name() != "java/io/PrintStream" &&
        write_method->owner->name() != "java/io/FileOutputStream") {
        const Value argument = Value::from_int(static_cast<i32>(byte));
        auto invoked = machine.invoke_instance(
            stream, *runtime_class, "write", "(I)V",
            std::span<const Value>(&argument, 1U));
        if (!invoked) return std::unexpected(invoked.error());
        if (invoked->throwable.has_value()) {
            auto thrown = machine.heap().class_name(*invoked->throwable);
            if (!thrown) return std::unexpected(thrown.error());
            return fail_java(*thrown, "custom OutputStream.write threw");
        }
        return {};
    }
    auto byte_array = is_instance(machine, stream,
                                  "java/io/ByteArrayOutputStream");
    if (!byte_array) return std::unexpected(byte_array.error());
    if (*byte_array) return byte_output_write_one(machine, stream, byte);

    auto file_output = is_instance(machine, stream,
                                   "java/io/FileOutputStream");
    if (!file_output) return std::unexpected(file_output.error());
    if (*file_output) return file_output_write_one(machine, stream, byte);

    auto network_output = connection_stream_write_one(machine, stream, byte);
    if (!network_output) return std::unexpected(network_output.error());
    if (network_output->has_value()) return {};

    auto data_output = is_instance(machine, stream,
                                   "java/io/DataOutputStream");
    if (!data_output) return std::unexpected(data_output.error());
    if (*data_output) {
        auto output = reference_field(machine, stream, kFilterStreamField);
        if (!output) return std::unexpected(output.error());
        auto written = stream_write_one(machine, *output, byte, depth + 1U);
        if (!written) return written;
        return increment_data_written(machine, stream, 1);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterOutputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto output = reference_field(machine, stream, kFilterStreamField);
        if (!output) return std::unexpected(output.error());
        auto written = stream_write_one(machine, *output, byte, depth + 1U);
        if (!written) return written;
        return increment_data_written(machine, stream, 1);
    }
    return fail_java("java/io/IOException",
                     "output stream implementation is not connected");
}

[[nodiscard]] Status stream_write_bytes(Machine& machine,
                                        ObjectRef stream,
                                        ObjectRef source,
                                        i32 offset,
                                        i32 length) {
    auto source_length = byte_array_length(machine, source);
    if (!source_length) return std::unexpected(source_length.error());
    auto range = validate_range(*source_length, offset, length);
    if (!range) return range;
    for (i32 index = 0; index < length; ++index) {
        auto byte = byte_array_value(machine, source,
                                     static_cast<usize>(offset + index));
        if (!byte) return std::unexpected(byte.error());
        auto written = stream_write_one(machine, stream, *byte, 0);
        if (!written) return written;
    }
    return {};
}

[[nodiscard]] Status stream_flush(Machine& machine,
                                  ObjectRef stream,
                                  usize depth) {
    if (stream.is_null()) return {};
    if (depth >= kMaximumStreamDepth)
        return fail_java("java/io/IOException",
                         "output stream filter chain is too deep");
    auto file_output = is_instance(machine, stream,
                                   "java/io/FileOutputStream");
    if (!file_output) return std::unexpected(file_output.error());
    if (*file_output) return file_output_flush(machine, stream);

    auto network_output = connection_stream_flush(machine, stream);
    if (!network_output) return std::unexpected(network_output.error());
    if (network_output->has_value()) return {};

    auto data_output = is_instance(machine, stream,
                                   "java/io/DataOutputStream");
    if (!data_output) return std::unexpected(data_output.error());
    if (*data_output) {
        auto output = reference_field(machine, stream, kFilterStreamField);
        if (!output) return std::unexpected(output.error());
        return stream_flush(machine, *output, depth + 1U);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterOutputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto output = reference_field(machine, stream, kFilterStreamField);
        if (!output) return std::unexpected(output.error());
        return stream_flush(machine, *output, depth + 1U);
    }
    return {};
}

[[nodiscard]] Status stream_close_input(Machine& machine,
                                        ObjectRef stream,
                                        usize depth) {
    if (stream.is_null()) return {};
    if (depth >= kMaximumStreamDepth)
        return fail_java("java/io/IOException",
                         "input stream filter chain is too deep");
    auto file_input = is_instance(machine, stream,
                                  "java/io/FileInputStream");
    if (!file_input) return std::unexpected(file_input.error());
    if (*file_input) return file_input_close(machine, stream);

    auto network_input = connection_stream_close_input(machine, stream);
    if (!network_input) return std::unexpected(network_input.error());
    if (network_input->has_value()) return {};

    auto data_input = is_instance(machine, stream,
                                  "java/io/DataInputStream");
    if (!data_input) return std::unexpected(data_input.error());
    if (*data_input) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_close_input(machine, *input, depth + 1U);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterInputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto input = reference_field(machine, stream, kFilterStreamField);
        if (!input) return std::unexpected(input.error());
        return stream_close_input(machine, *input, depth + 1U);
    }
    return {};
}

[[nodiscard]] Status stream_close_output(Machine& machine,
                                         ObjectRef stream,
                                         usize depth) {
    if (stream.is_null()) return {};
    if (depth >= kMaximumStreamDepth)
        return fail_java("java/io/IOException",
                         "output stream filter chain is too deep");
    auto file_output = is_instance(machine, stream,
                                   "java/io/FileOutputStream");
    if (!file_output) return std::unexpected(file_output.error());
    if (*file_output) return file_output_close(machine, stream);

    auto network_output = connection_stream_close_output(machine, stream);
    if (!network_output) return std::unexpected(network_output.error());
    if (network_output->has_value()) return {};

    auto data_output = is_instance(machine, stream,
                                   "java/io/DataOutputStream");
    if (!data_output) return std::unexpected(data_output.error());
    if (*data_output) {
        auto output = reference_field(machine, stream, kFilterStreamField);
        if (!output) return std::unexpected(output.error());
        auto flushed = stream_flush(machine, *output, depth + 1U);
        if (!flushed) return flushed;
        return stream_close_output(machine, *output, depth + 1U);
    }
    auto filter = is_instance(machine, stream,
                              "java/io/FilterOutputStream");
    if (!filter) return std::unexpected(filter.error());
    if (*filter) {
        auto output = reference_field(machine, stream, kFilterStreamField);
        if (!output) return std::unexpected(output.error());
        auto flushed = stream_flush(machine, *output, depth + 1U);
        if (!flushed) return flushed;
        return stream_close_output(machine, *output, depth + 1U);
    }
    return {};
}

[[nodiscard]] Result<std::vector<u8>> read_required(Machine& machine,
                                                    ObjectRef input,
                                                    usize count) {
    std::vector<u8> bytes;
    bytes.reserve(count);
    for (usize index = 0; index < count; ++index) {
        auto value = stream_read_one(machine, input, 0);
        if (!value) return std::unexpected(value.error());
        if (*value < 0) {
            return fail_java("java/io/EOFException",
                             "data stream reached end of input");
        }
        bytes.push_back(static_cast<u8>(*value));
    }
    return bytes;
}

[[nodiscard]] Status write_bytes(Machine& machine,
                                 ObjectRef output,
                                 std::span<const u8> bytes) {
    for (const u8 byte : bytes) {
        auto written = stream_write_one(machine, output, byte, 0);
        if (!written) return written;
    }
    return {};
}

[[nodiscard]] Result<ObjectRef> data_input_reference(
    std::span<const Value> arguments,
    bool is_static) {
    ObjectRef input;
    if (is_static) {
        if (arguments.empty())
            return fail(ErrorCode::invalid_argument,
                        "DataInput.readUTF has no input");
        auto reference = arguments.front().as_reference();
        if (!reference || reference->is_null())
            return fail_java("java/lang/NullPointerException",
                             "DataInput is null");
        input = *reference;
    } else {
        auto object = receiver(arguments);
        if (!object) return std::unexpected(object.error());
        input = *object;
    }
    return input;
}

void register_base_streams(NativeMethodRegistry& registry) {
    const auto input_read_array = [&registry](std::string owner,
                                              bool with_range) {
        add(registry, std::move(owner), "read",
            with_range ? "([BII)I" : "([B)I",
            [with_range](Machine& machine,
                         std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                auto destination = arguments[1].as_reference();
                if (!input) return std::unexpected(input.error());
                if (!destination)
                    return std::unexpected(destination.error());
                i32 offset = 0;
                i32 length = 0;
                auto array_length = byte_array_length(machine, *destination);
                if (!array_length)
                    return std::unexpected(array_length.error());
                if (*array_length > static_cast<usize>(
                                        std::numeric_limits<i32>::max()))
                    return fail_java("java/lang/OutOfMemoryError",
                                     "byte array exceeds int length");
                length = static_cast<i32>(*array_length);
                if (with_range) {
                    auto parsed_offset = arguments[2].as_int();
                    auto parsed_length = arguments[3].as_int();
                    if (!parsed_offset || !parsed_length)
                        return fail(ErrorCode::invalid_argument,
                                    "InputStream.read range is invalid");
                    offset = *parsed_offset;
                    length = *parsed_length;
                }
                auto read = stream_read_bytes(machine, *input,
                                              *destination,
                                              offset, length);
                if (!read) return std::unexpected(read.error());
                return std::optional<Value>(Value::from_int(*read));
            });
    };
    input_read_array("java/io/InputStream", false);
    input_read_array("java/io/InputStream", true);
    input_read_array("java/io/FilterInputStream", false);
    input_read_array("java/io/FilterInputStream", true);
    input_read_array("java/io/DataInputStream", false);
    input_read_array("java/io/DataInputStream", true);
    input_read_array("java/io/ByteArrayInputStream", true);

    const auto input_read_one = [&registry](std::string owner) {
        add(registry, std::move(owner), "read", "()I",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                if (!input) return std::unexpected(input.error());
                auto value = stream_read_one(machine, *input, 0);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    input_read_one("java/io/FilterInputStream");
    input_read_one("java/io/DataInputStream");
    input_read_one("java/io/ByteArrayInputStream");

    const auto input_skip = [&registry](std::string owner) {
        add(registry, std::move(owner), "skip", "(J)J",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                auto requested = arguments[1].as_long();
                if (!input) return std::unexpected(input.error());
                if (!requested) return std::unexpected(requested.error());
                auto skipped = stream_skip(machine, *input, *requested, 0);
                if (!skipped) return std::unexpected(skipped.error());
                return std::optional<Value>(Value::from_long(*skipped));
            });
    };
    input_skip("java/io/InputStream");
    input_skip("java/io/FilterInputStream");
    input_skip("java/io/DataInputStream");
    input_skip("java/io/ByteArrayInputStream");

    const auto input_available = [&registry](std::string owner) {
        add(registry, std::move(owner), "available", "()I",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                if (!input) return std::unexpected(input.error());
                auto available = stream_available(machine, *input, 0);
                if (!available) return std::unexpected(available.error());
                return std::optional<Value>(Value::from_int(*available));
            });
    };
    input_available("java/io/InputStream");
    input_available("java/io/FilterInputStream");
    input_available("java/io/DataInputStream");
    input_available("java/io/ByteArrayInputStream");

    const auto input_close = [&registry](std::string owner) {
        add(registry, std::move(owner), "close", "()V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                if (!input) return std::unexpected(input.error());
                auto closed = stream_close_input(machine, *input, 0);
                if (!closed) return std::unexpected(closed.error());
                return std::optional<Value> {};
            });
    };
    input_close("java/io/InputStream");
    input_close("java/io/FilterInputStream");
    input_close("java/io/DataInputStream");
    input_close("java/io/ByteArrayInputStream");

    add(registry, "java/io/InputStream", "mark", "(I)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
    add(registry, "java/io/InputStream", "reset", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return fail_java("java/io/IOException",
                             "mark/reset is not supported");
        });
    add(registry, "java/io/InputStream", "markSupported", "()Z",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });

    add(registry, "java/io/FilterInputStream", "mark", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            auto target = reference_field(machine, *input, kFilterStreamField);
            if (!target) return std::unexpected(target.error());
            auto byte_array = is_instance(machine, *target,
                                          "java/io/ByteArrayInputStream");
            if (!byte_array) return std::unexpected(byte_array.error());
            if (*byte_array) {
                auto position = int_field(machine, *target,
                                          kByteInputPositionField);
                if (!position) return std::unexpected(position.error());
                auto stored = set_int_field(machine, *target,
                                            kByteInputMarkField,
                                            *position);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/io/FilterInputStream", "reset", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            auto target = reference_field(machine, *input, kFilterStreamField);
            if (!target) return std::unexpected(target.error());
            auto byte_array = is_instance(machine, *target,
                                          "java/io/ByteArrayInputStream");
            if (!byte_array) return std::unexpected(byte_array.error());
            if (!*byte_array)
                return fail_java("java/io/IOException",
                                 "mark/reset is not supported");
            auto mark = int_field(machine, *target, kByteInputMarkField);
            if (!mark) return std::unexpected(mark.error());
            auto stored = set_int_field(machine, *target,
                                        kByteInputPositionField, *mark);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/FilterInputStream", "markSupported", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            auto target = reference_field(machine, *input, kFilterStreamField);
            if (!target) return std::unexpected(target.error());
            auto byte_array = is_instance(machine, *target,
                                          "java/io/ByteArrayInputStream");
            if (!byte_array) return std::unexpected(byte_array.error());
            return std::optional<Value>(Value::from_int(*byte_array ? 1 : 0));
        });

    add(registry, "java/io/DataInputStream", "mark", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            auto target = reference_field(machine, *input, kFilterStreamField);
            if (!target) return std::unexpected(target.error());
            auto byte_array = is_instance(machine, *target,
                                          "java/io/ByteArrayInputStream");
            if (!byte_array) return std::unexpected(byte_array.error());
            if (*byte_array) {
                auto position = int_field(machine, *target,
                                          kByteInputPositionField);
                if (!position) return std::unexpected(position.error());
                auto stored = set_int_field(machine, *target,
                                            kByteInputMarkField,
                                            *position);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/io/DataInputStream", "reset", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            auto target = reference_field(machine, *input, kFilterStreamField);
            if (!target) return std::unexpected(target.error());
            auto byte_array = is_instance(machine, *target,
                                          "java/io/ByteArrayInputStream");
            if (!byte_array) return std::unexpected(byte_array.error());
            if (!*byte_array)
                return fail_java("java/io/IOException",
                                 "mark/reset is not supported");
            auto mark = int_field(machine, *target, kByteInputMarkField);
            if (!mark) return std::unexpected(mark.error());
            auto stored = set_int_field(machine, *target,
                                        kByteInputPositionField, *mark);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/DataInputStream", "markSupported", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            auto target = reference_field(machine, *input, kFilterStreamField);
            if (!target) return std::unexpected(target.error());
            auto byte_array = is_instance(machine, *target,
                                          "java/io/ByteArrayInputStream");
            if (!byte_array) return std::unexpected(byte_array.error());
            return std::optional<Value>(Value::from_int(*byte_array ? 1 : 0));
        });

    const auto output_write_array = [&registry](std::string owner) {
        add(registry, std::move(owner), "write", "([B)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                if (arguments.size() < 2U) {
                    return fail(ErrorCode::invalid_argument,
                                "OutputStream.write byte[] is missing");
                }
                auto source = arguments[1].as_reference();
                if (!output) return std::unexpected(output.error());
                if (!source) return std::unexpected(source.error());
                auto length = byte_array_length(machine, *source);
                if (!length) return std::unexpected(length.error());
                auto written = stream_write_bytes(
                    machine, *output, *source, 0,
                    static_cast<i32>(*length));
                if (!written) return std::unexpected(written.error());
                return std::optional<Value> {};
            });
    };
    output_write_array("java/io/OutputStream");
    output_write_array("java/io/FilterOutputStream");
    output_write_array("java/io/DataOutputStream");
    output_write_array("java/io/ByteArrayOutputStream");
    const auto output_write_range = [&registry](std::string owner) {
        add(registry, std::move(owner), "write", "([BII)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                auto source = arguments[1].as_reference();
                auto offset = arguments[2].as_int();
                auto length = arguments[3].as_int();
                if (!output || !source || !offset || !length)
                    return fail(ErrorCode::invalid_argument,
                                "OutputStream.write range is invalid");
                auto written = stream_write_bytes(machine, *output,
                                                  *source, *offset, *length);
                if (!written) return std::unexpected(written.error());
                return std::optional<Value> {};
            });
    };
    output_write_range("java/io/OutputStream");
    output_write_range("java/io/FilterOutputStream");
    output_write_range("java/io/DataOutputStream");
    output_write_range("java/io/ByteArrayOutputStream");

    const auto output_write_one = [&registry](std::string owner) {
        add(registry, std::move(owner), "write", "(I)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                auto value = arguments[1].as_int();
                if (!output) return std::unexpected(output.error());
                if (!value) return std::unexpected(value.error());
                auto written = stream_write_one(
                    machine, *output, static_cast<u8>(*value), 0);
                if (!written) return std::unexpected(written.error());
                return std::optional<Value> {};
            });
    };
    output_write_one("java/io/FilterOutputStream");
    output_write_one("java/io/DataOutputStream");
    output_write_one("java/io/ByteArrayOutputStream");

    const auto output_flush = [&registry](std::string owner) {
        add(registry, std::move(owner), "flush", "()V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                if (!output) return std::unexpected(output.error());
                auto flushed = stream_flush(machine, *output, 0);
                if (!flushed) return std::unexpected(flushed.error());
                return std::optional<Value> {};
            });
    };
    output_flush("java/io/OutputStream");
    output_flush("java/io/FilterOutputStream");
    output_flush("java/io/DataOutputStream");

    const auto output_close = [&registry](std::string owner) {
        add(registry, std::move(owner), "close", "()V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                if (!output) return std::unexpected(output.error());
                auto closed = stream_close_output(machine, *output, 0);
                if (!closed) return std::unexpected(closed.error());
                return std::optional<Value> {};
            });
    };
    output_close("java/io/OutputStream");
    output_close("java/io/FilterOutputStream");
    output_close("java/io/DataOutputStream");
    output_close("java/io/ByteArrayOutputStream");
}

void register_byte_array_streams(NativeMethodRegistry& registry) {
    add(registry, "java/io/ByteArrayInputStream", "<init>", "([B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto buffer = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!buffer) return std::unexpected(buffer.error());
            auto length = byte_array_length(machine, *buffer);
            if (!length) return std::unexpected(length.error());
            auto buffer_stored = set_reference_field(
                machine, *object, kByteInputBufferField, *buffer);
            auto position_stored = set_int_field(
                machine, *object, kByteInputPositionField, 0);
            auto mark_stored = set_int_field(
                machine, *object, kByteInputMarkField, 0);
            auto count_stored = set_int_field(
                machine, *object, kByteInputCountField,
                static_cast<i32>(*length));
            if (!buffer_stored) return std::unexpected(buffer_stored.error());
            if (!position_stored) return std::unexpected(position_stored.error());
            if (!mark_stored) return std::unexpected(mark_stored.error());
            if (!count_stored) return std::unexpected(count_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayInputStream", "<init>", "([BII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto buffer = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!object || !buffer || !offset || !length)
                return fail(ErrorCode::invalid_argument,
                            "ByteArrayInputStream arguments are invalid");
            auto array_length = byte_array_length(machine, *buffer);
            if (!array_length) return std::unexpected(array_length.error());
            if (*offset < 0 || *length < 0 ||
                static_cast<usize>(*offset) > *array_length)
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "ByteArrayInputStream range is invalid");
            const usize end = std::min(
                *array_length,
                static_cast<usize>(*offset) + static_cast<usize>(*length));
            auto buffer_stored = set_reference_field(
                machine, *object, kByteInputBufferField, *buffer);
            auto position_stored = set_int_field(
                machine, *object, kByteInputPositionField, *offset);
            auto mark_stored = set_int_field(
                machine, *object, kByteInputMarkField, *offset);
            auto count_stored = set_int_field(
                machine, *object, kByteInputCountField,
                static_cast<i32>(end));
            if (!buffer_stored) return std::unexpected(buffer_stored.error());
            if (!position_stored) return std::unexpected(position_stored.error());
            if (!mark_stored) return std::unexpected(mark_stored.error());
            if (!count_stored) return std::unexpected(count_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayInputStream", "mark", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto position = int_field(machine, *object,
                                      kByteInputPositionField);
            if (!position) return std::unexpected(position.error());
            auto stored = set_int_field(machine, *object,
                                        kByteInputMarkField,
                                        *position);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayInputStream", "reset", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto mark = int_field(machine, *object, kByteInputMarkField);
            if (!mark) return std::unexpected(mark.error());
            auto stored = set_int_field(machine, *object,
                                        kByteInputPositionField, *mark);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayInputStream", "markSupported", "()Z",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(1));
        });

    const auto initialize_output = [](Machine& machine,
                                      ObjectRef object,
                                      i32 capacity) -> Status {
        if (capacity < 0)
            return fail_java("java/lang/IllegalArgumentException",
                             "ByteArrayOutputStream capacity is negative");
        auto buffer = allocate_byte_array(machine,
                                          static_cast<usize>(capacity));
        if (!buffer) return std::unexpected(buffer.error());
        auto buffer_stored = set_reference_field(
            machine, object, kByteOutputBufferField, *buffer);
        auto count_stored = set_int_field(
            machine, object, kByteOutputCountField, 0);
        if (!buffer_stored) return buffer_stored;
        return count_stored;
    };
    add(registry, "java/io/ByteArrayOutputStream", "<init>", "()V",
        [initialize_output](Machine& machine,
                            std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto initialized = initialize_output(machine, *object, 32);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayOutputStream", "<init>", "(I)V",
        [initialize_output](Machine& machine,
                            std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto capacity = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!capacity) return std::unexpected(capacity.error());
            auto initialized = initialize_output(machine, *object, *capacity);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayOutputStream", "reset", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto stored = set_int_field(machine, *object,
                                        kByteOutputCountField, 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayOutputStream", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kByteOutputCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, "java/io/ByteArrayOutputStream", "toByteArray", "()[B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kByteOutputCountField);
            auto buffer = reference_field(machine, *object,
                                          kByteOutputBufferField);
            if (!count || !buffer)
                return fail(ErrorCode::invalid_state,
                            "ByteArrayOutputStream state is invalid");
            auto copy = allocate_byte_array(machine,
                                            static_cast<usize>(*count));
            if (!copy) return std::unexpected(copy.error());
            for (i32 index = 0; index < *count; ++index) {
                auto value = byte_array_value(machine, *buffer,
                                              static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto stored = set_byte_array_value(machine, *copy,
                                                   static_cast<usize>(index),
                                                   *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*copy));
        });
    add(registry, "java/io/ByteArrayOutputStream", "writeTo",
        "(Ljava/io/OutputStream;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto target = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!target || target->is_null())
                return fail_java("java/lang/NullPointerException",
                                 "ByteArrayOutputStream target is null");
            auto count = int_field(machine, *object, kByteOutputCountField);
            auto buffer = reference_field(machine, *object,
                                          kByteOutputBufferField);
            if (!count || !buffer)
                return fail(ErrorCode::invalid_state,
                            "ByteArrayOutputStream state is invalid");
            auto written = stream_write_bytes(machine, *target, *buffer,
                                              0, *count);
            if (!written) return std::unexpected(written.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/ByteArrayOutputStream", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto count = int_field(machine, *object, kByteOutputCountField);
            auto buffer = reference_field(machine, *object,
                                          kByteOutputBufferField);
            if (!count || !buffer)
                return fail(ErrorCode::invalid_state,
                            "ByteArrayOutputStream state is invalid");
            std::u16string text;
            text.reserve(static_cast<usize>(*count));
            for (i32 index = 0; index < *count; ++index) {
                auto value = byte_array_value(machine, *buffer,
                                              static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                text.push_back(static_cast<char16_t>(*value));
            }
            auto string = create_string(machine, std::move(text));
            if (!string) return std::unexpected(string.error());
            return std::optional<Value>(Value::from_reference(*string));
        });
}

void register_filter_streams(NativeMethodRegistry& registry) {
    const auto input_constructor = [&registry](std::string owner) {
        add(registry, std::move(owner), "<init>",
            "(Ljava/io/InputStream;)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto input = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!input || input->is_null())
                    return fail_java("java/lang/NullPointerException",
                                     "filter input is null");
                auto stored = set_reference_field(machine, *object,
                                                  kFilterStreamField,
                                                  *input);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    input_constructor("java/io/FilterInputStream");
    input_constructor("java/io/DataInputStream");

    const auto output_constructor = [&registry](std::string owner,
                                                bool data) {
        add(registry, std::move(owner), "<init>",
            "(Ljava/io/OutputStream;)V",
            [data](Machine& machine,
                   std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto output = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!output || output->is_null())
                    return fail_java("java/lang/NullPointerException",
                                     "filter output is null");
                auto stored = set_reference_field(machine, *object,
                                                  kFilterStreamField,
                                                  *output);
                if (!stored) return std::unexpected(stored.error());
                if (data) {
                    auto count = set_int_field(machine, *object,
                                               kDataOutputWrittenField, 0);
                    if (!count) return std::unexpected(count.error());
                }
                return std::optional<Value> {};
            });
    };
    output_constructor("java/io/FilterOutputStream", false);
    output_constructor("java/io/DataOutputStream", true);
}

void register_data_input(NativeMethodRegistry& registry) {
    const auto read_fully = [&registry](bool with_range) {
        add(registry, "java/io/DataInputStream", "readFully",
            with_range ? "([BII)V" : "([B)V",
            [with_range](Machine& machine,
                         std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                auto destination = arguments[1].as_reference();
                if (!input || !destination)
                    return fail(ErrorCode::invalid_argument,
                                "DataInputStream.readFully arguments are invalid");
                auto array_length = byte_array_length(machine, *destination);
                if (!array_length)
                    return std::unexpected(array_length.error());
                i32 offset = 0;
                i32 length = static_cast<i32>(*array_length);
                if (with_range) {
                    auto parsed_offset = arguments[2].as_int();
                    auto parsed_length = arguments[3].as_int();
                    if (!parsed_offset || !parsed_length)
                        return fail(ErrorCode::invalid_argument,
                                    "DataInputStream.readFully range is invalid");
                    offset = *parsed_offset;
                    length = *parsed_length;
                }
                auto range = validate_range(*array_length, offset, length);
                if (!range) return std::unexpected(range.error());
                for (i32 index = 0; index < length; ++index) {
                    auto value = stream_read_one(machine, *input, 0);
                    if (!value) return std::unexpected(value.error());
                    if (*value < 0)
                        return fail_java("java/io/EOFException",
                                         "readFully reached end of input");
                    auto stored = set_byte_array_value(
                        machine, *destination,
                        static_cast<usize>(offset + index),
                        static_cast<u8>(*value));
                    if (!stored) return std::unexpected(stored.error());
                }
                return std::optional<Value> {};
            });
    };
    read_fully(false);
    read_fully(true);
    add(registry, "java/io/DataInputStream", "skipBytes", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            auto count = arguments[1].as_int();
            if (!input) return std::unexpected(input.error());
            if (!count) return std::unexpected(count.error());
            auto skipped = stream_skip(machine, *input,
                                       std::max(*count, 0), 0);
            if (!skipped) return std::unexpected(skipped.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(*skipped)));
        });

    const auto add_integer_read = [&registry](const char* name,
                                              const char* descriptor,
                                              usize byte_count,
                                              auto convert) {
        add(registry, "java/io/DataInputStream", name, descriptor,
            [byte_count, convert](Machine& machine,
                                  std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto input = receiver(arguments);
                if (!input) return std::unexpected(input.error());
                auto bytes = read_required(machine, *input, byte_count);
                if (!bytes) return std::unexpected(bytes.error());
                u64 bits = 0;
                for (const u8 byte : *bytes)
                    bits = (bits << 8U) | byte;
                return std::optional<Value>(convert(bits));
            });
    };
    add_integer_read("readBoolean", "()Z", 1,
        [](u64 bits) { return Value::from_int(bits == 0 ? 0 : 1); });
    add_integer_read("readByte", "()B", 1,
        [](u64 bits) { return Value::from_int(static_cast<i8>(bits)); });
    add_integer_read("readUnsignedByte", "()I", 1,
        [](u64 bits) { return Value::from_int(static_cast<i32>(bits)); });
    add_integer_read("readShort", "()S", 2,
        [](u64 bits) { return Value::from_int(static_cast<i16>(bits)); });
    add_integer_read("readUnsignedShort", "()I", 2,
        [](u64 bits) { return Value::from_int(static_cast<u16>(bits)); });
    add_integer_read("readChar", "()C", 2,
        [](u64 bits) { return Value::from_int(static_cast<u16>(bits)); });
    add_integer_read("readInt", "()I", 4,
        [](u64 bits) { return Value::from_int(static_cast<i32>(
            static_cast<u32>(bits))); });
    add_integer_read("readLong", "()J", 8,
        [](u64 bits) { return Value::from_long(static_cast<i64>(bits)); });
    add_integer_read("readFloat", "()F", 4,
        [](u64 bits) { return Value::from_float(std::bit_cast<float>(
            static_cast<u32>(bits))); });
    add_integer_read("readDouble", "()D", 8,
        [](u64 bits) { return Value::from_double(std::bit_cast<double>(bits)); });

    const auto read_utf = [](Machine& machine,
                             ObjectRef input)
        -> Result<std::optional<Value>> {
        auto length_bytes = read_required(machine, input, 2);
        if (!length_bytes) return std::unexpected(length_bytes.error());
        const usize length = (static_cast<usize>((*length_bytes)[0]) << 8U) |
                             static_cast<usize>((*length_bytes)[1]);
        auto bytes = read_required(machine, input, length);
        if (!bytes) return std::unexpected(bytes.error());
        std::string encoded;
        encoded.reserve(length);
        for (const u8 byte : *bytes)
            encoded.push_back(static_cast<char>(byte));
        auto decoded = decode_modified_utf8(
            encoded, ModifiedUtf8Mode::allow_raw_nul);
        if (!decoded)
            return fail_java("java/io/UTFDataFormatException",
                             decoded.error().message);
        auto string = create_string(machine, std::move(*decoded));
        if (!string) return std::unexpected(string.error());
        return std::optional<Value>(Value::from_reference(*string));
    };
    add(registry, "java/io/DataInputStream", "readUTF",
        "()Ljava/lang/String;",
        [read_utf](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = receiver(arguments);
            if (!input) return std::unexpected(input.error());
            return read_utf(machine, *input);
        });
    add(registry, "java/io/DataInputStream", "readUTF",
        "(Ljava/io/DataInput;)Ljava/lang/String;",
        [read_utf](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto input = data_input_reference(arguments, true);
            if (!input) return std::unexpected(input.error());
            return read_utf(machine, *input);
        });
}

[[nodiscard]] Result<StreamCharset> object_charset(Machine& machine,
                                                   ObjectRef object,
                                                   usize field) {
    auto value = int_field(machine, object, field);
    if (!value) return std::unexpected(value.error());
    switch (static_cast<StreamCharset>(*value)) {
    case StreamCharset::utf8:
    case StreamCharset::latin1:
    case StreamCharset::ascii:
    case StreamCharset::utf16be:
        return static_cast<StreamCharset>(*value);
    }
    return fail(ErrorCode::invalid_state,
                "reader/writer charset state is invalid");
}

[[nodiscard]] Status require_open(Machine& machine,
                                  ObjectRef object,
                                  usize closed_field,
                                  std::string_view kind) {
    auto closed = int_field(machine, object, closed_field);
    if (!closed) return std::unexpected(closed.error());
    if (*closed != 0) {
        return fail_java("java/io/IOException",
                         std::string(kind) + " is closed");
    }
    return {};
}

[[nodiscard]] Result<i32> reader_read_character(Machine& machine,
                                                ObjectRef reader) {
    auto opened = require_open(machine, reader, kReaderClosedField,
                               "InputStreamReader");
    if (!opened) return std::unexpected(opened.error());
    auto pending = int_field(machine, reader, kReaderPendingField);
    if (!pending) return std::unexpected(pending.error());
    if (*pending >= 0) {
        auto cleared = set_int_field(machine, reader,
                                     kReaderPendingField, -1);
        if (!cleared) return std::unexpected(cleared.error());
        return *pending;
    }
    auto input = reference_field(machine, reader, kReaderInputField);
    auto charset = object_charset(machine, reader, kReaderCharsetField);
    if (!input) return std::unexpected(input.error());
    if (!charset) return std::unexpected(charset.error());

    auto first_value = stream_read_one(machine, *input, 0U);
    if (!first_value) return std::unexpected(first_value.error());
    if (*first_value < 0) return -1;
    const u8 first = static_cast<u8>(*first_value);
    if (*charset == StreamCharset::latin1) return static_cast<i32>(first);
    if (*charset == StreamCharset::ascii) {
        return first <= 0x7FU ? static_cast<i32>(first)
                              : static_cast<i32>(0xFFFDU);
    }
    if (*charset == StreamCharset::utf16be) {
        auto second = stream_read_one(machine, *input, 0U);
        if (!second) return std::unexpected(second.error());
        if (*second < 0) return 0xFFFDU;
        return static_cast<i32>((static_cast<u16>(first) << 8U) |
                                static_cast<u16>(static_cast<u8>(*second)));
    }
    if (first <= 0x7FU) return static_cast<i32>(first);

    u32 code_point = 0U;
    usize continuation_count = 0U;
    u32 minimum = 0U;
    if ((first & 0xE0U) == 0xC0U) {
        code_point = first & 0x1FU;
        continuation_count = 1U;
        minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
        code_point = first & 0x0FU;
        continuation_count = 2U;
        minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
        code_point = first & 0x07U;
        continuation_count = 3U;
        minimum = 0x10000U;
    } else {
        return 0xFFFDU;
    }
    for (usize index = 0U; index < continuation_count; ++index) {
        auto next = stream_read_one(machine, *input, 0U);
        if (!next) return std::unexpected(next.error());
        if (*next < 0 || (static_cast<u8>(*next) & 0xC0U) != 0x80U) {
            return 0xFFFDU;
        }
        code_point = (code_point << 6U) |
                     (static_cast<u8>(*next) & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
        return 0xFFFDU;
    }
    if (code_point <= 0xFFFFU) return static_cast<i32>(code_point);
    code_point -= 0x10000U;
    const i32 high = static_cast<i32>(0xD800U | (code_point >> 10U));
    const i32 low = static_cast<i32>(0xDC00U | (code_point & 0x3FFU));
    auto stored = set_int_field(machine, reader, kReaderPendingField, low);
    if (!stored) return std::unexpected(stored.error());
    return high;
}

[[nodiscard]] Status write_utf8_code_point(Machine& machine,
                                           ObjectRef output,
                                           u32 code_point) {
    u8 bytes[4] {};
    usize count = 0U;
    if (code_point <= 0x7FU) {
        bytes[0] = static_cast<u8>(code_point);
        count = 1U;
    } else if (code_point <= 0x7FFU) {
        bytes[0] = static_cast<u8>(0xC0U | (code_point >> 6U));
        bytes[1] = static_cast<u8>(0x80U | (code_point & 0x3FU));
        count = 2U;
    } else if (code_point <= 0xFFFFU) {
        bytes[0] = static_cast<u8>(0xE0U | (code_point >> 12U));
        bytes[1] = static_cast<u8>(0x80U | ((code_point >> 6U) & 0x3FU));
        bytes[2] = static_cast<u8>(0x80U | (code_point & 0x3FU));
        count = 3U;
    } else {
        bytes[0] = static_cast<u8>(0xF0U | (code_point >> 18U));
        bytes[1] = static_cast<u8>(0x80U | ((code_point >> 12U) & 0x3FU));
        bytes[2] = static_cast<u8>(0x80U | ((code_point >> 6U) & 0x3FU));
        bytes[3] = static_cast<u8>(0x80U | (code_point & 0x3FU));
        count = 4U;
    }
    for (usize index = 0U; index < count; ++index) {
        auto written = stream_write_one(machine, output, bytes[index], 0U);
        if (!written) return written;
    }
    return {};
}

[[nodiscard]] Status writer_write_character(Machine& machine,
                                            ObjectRef writer,
                                            char16_t character) {
    auto opened = require_open(machine, writer, kWriterClosedField,
                               "OutputStreamWriter");
    if (!opened) return opened;
    auto output = reference_field(machine, writer, kWriterOutputField);
    auto charset = object_charset(machine, writer, kWriterCharsetField);
    if (!output) return std::unexpected(output.error());
    if (!charset) return std::unexpected(charset.error());
    const u32 value = static_cast<u16>(character);
    if (*charset == StreamCharset::latin1) {
        return stream_write_one(machine, *output,
                                value <= 0xFFU ? static_cast<u8>(value)
                                               : static_cast<u8>('?'),
                                0U);
    }
    if (*charset == StreamCharset::ascii) {
        return stream_write_one(machine, *output,
                                value <= 0x7FU ? static_cast<u8>(value)
                                               : static_cast<u8>('?'),
                                0U);
    }
    if (*charset == StreamCharset::utf16be) {
        auto high = stream_write_one(machine, *output,
                                     static_cast<u8>(value >> 8U), 0U);
        if (!high) return high;
        return stream_write_one(machine, *output,
                                static_cast<u8>(value), 0U);
    }

    auto pending = int_field(machine, writer, kWriterPendingField);
    if (!pending) return std::unexpected(pending.error());
    if (*pending >= 0) {
        const u32 high = static_cast<u32>(*pending);
        auto cleared = set_int_field(machine, writer,
                                     kWriterPendingField, -1);
        if (!cleared) return cleared;
        if (value >= 0xDC00U && value <= 0xDFFFU) {
            const u32 code_point = 0x10000U +
                ((high - 0xD800U) << 10U) + (value - 0xDC00U);
            return write_utf8_code_point(machine, *output, code_point);
        }
        auto replacement = write_utf8_code_point(machine, *output, 0xFFFDU);
        if (!replacement) return replacement;
    }
    if (value >= 0xD800U && value <= 0xDBFFU) {
        return set_int_field(machine, writer, kWriterPendingField,
                             static_cast<i32>(value));
    }
    if (value >= 0xDC00U && value <= 0xDFFFU) {
        return write_utf8_code_point(machine, *output, 0xFFFDU);
    }
    return write_utf8_code_point(machine, *output, value);
}

[[nodiscard]] Status writer_flush_pending(Machine& machine,
                                          ObjectRef writer) {
    auto charset = object_charset(machine, writer, kWriterCharsetField);
    auto pending = int_field(machine, writer, kWriterPendingField);
    auto output = reference_field(machine, writer, kWriterOutputField);
    if (!charset) return std::unexpected(charset.error());
    if (!pending) return std::unexpected(pending.error());
    if (!output) return std::unexpected(output.error());
    if (*charset == StreamCharset::utf8 && *pending >= 0) {
        auto cleared = set_int_field(machine, writer,
                                     kWriterPendingField, -1);
        if (!cleared) return cleared;
        return write_utf8_code_point(machine, *output, 0xFFFDU);
    }
    return {};
}

[[nodiscard]] Status writer_write_text(Machine& machine,
                                       ObjectRef writer,
                                       std::u16string_view text,
                                       usize offset,
                                       usize length) {
    for (usize index = 0U; index < length; ++index) {
        auto written = writer_write_character(machine, writer,
                                              text[offset + index]);
        if (!written) return written;
    }
    return {};
}

void register_reader_writer(NativeMethodRegistry& registry) {
    const auto initialize_lock = [](Machine& machine,
                                    ObjectRef object,
                                    ObjectRef lock,
                                    usize field) -> Status {
        return set_reference_field(machine, object, field,
                                   lock.is_null() ? object : lock);
    };
    add(registry, "java/io/Reader", "<init>", "()V",
        [initialize_lock](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto stored = initialize_lock(machine, *object, {}, kReaderLockField);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/Reader", "<init>", "(Ljava/lang/Object;)V",
        [initialize_lock](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto lock = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!lock || lock->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Reader lock is null");
            }
            auto stored = initialize_lock(machine, *object, *lock,
                                          kReaderLockField);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/Writer", "<init>", "()V",
        [initialize_lock](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto stored = initialize_lock(machine, *object, {}, kWriterLockField);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/Writer", "<init>", "(Ljava/lang/Object;)V",
        [initialize_lock](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto lock = arguments[1].as_reference();
            if (!object) return std::unexpected(object.error());
            if (!lock || lock->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Writer lock is null");
            }
            auto stored = initialize_lock(machine, *object, *lock,
                                          kWriterLockField);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    const auto initialize_reader = [](Machine& machine,
                                      std::span<const Value> arguments,
                                      bool named)
        -> Result<std::optional<Value>> {
        auto object = receiver(arguments);
        auto input = arguments[1].as_reference();
        if (!object) return std::unexpected(object.error());
        if (!input || input->is_null()) {
            return fail_java("java/lang/NullPointerException",
                             "InputStreamReader input is null");
        }
        StreamCharset charset = StreamCharset::latin1;
        if (named) {
            auto name = arguments[2].as_reference();
            if (!name) return std::unexpected(name.error());
            auto resolved = resolve_stream_charset(machine, *name);
            if (!resolved) return std::unexpected(resolved.error());
            charset = *resolved;
        }
        auto lock = set_reference_field(machine, *object, kReaderLockField,
                                        *object);
        auto stored_input = set_reference_field(machine, *object,
                                                kReaderInputField, *input);
        auto stored_charset = set_int_field(
            machine, *object, kReaderCharsetField,
            static_cast<i32>(charset));
        auto stored_pending = set_int_field(machine, *object,
                                            kReaderPendingField, -1);
        auto stored_closed = set_int_field(machine, *object,
                                           kReaderClosedField, 0);
        if (!lock) return std::unexpected(lock.error());
        if (!stored_input) return std::unexpected(stored_input.error());
        if (!stored_charset) return std::unexpected(stored_charset.error());
        if (!stored_pending) return std::unexpected(stored_pending.error());
        if (!stored_closed) return std::unexpected(stored_closed.error());
        return std::optional<Value> {};
    };
    add(registry, "java/io/InputStreamReader", "<init>",
        "(Ljava/io/InputStream;)V",
        [initialize_reader](Machine& machine, std::span<const Value> arguments) {
            return initialize_reader(machine, arguments, false);
        });
    add(registry, "java/io/InputStreamReader", "<init>",
        "(Ljava/io/InputStream;Ljava/lang/String;)V",
        [initialize_reader](Machine& machine, std::span<const Value> arguments) {
            return initialize_reader(machine, arguments, true);
        });
    add(registry, "java/io/InputStreamReader", "read", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = reader_read_character(machine, *object);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, "java/io/InputStreamReader", "read", "([CII)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto array = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!object || !array || !offset || !length) {
                return fail(ErrorCode::invalid_argument,
                            "InputStreamReader.read arguments are invalid");
            }
            auto array_length = char_array_length(machine, *array);
            if (!array_length) return std::unexpected(array_length.error());
            auto range = validate_char_range(*array_length, *offset, *length);
            if (!range) return std::unexpected(range.error());
            if (*length == 0) return std::optional<Value>(Value::from_int(0));
            i32 count = 0;
            while (count < *length) {
                auto value = reader_read_character(machine, *object);
                if (!value) return std::unexpected(value.error());
                if (*value < 0) break;
                auto stored = set_char_array_value(
                    machine, *array, static_cast<usize>(*offset + count),
                    static_cast<char16_t>(static_cast<u16>(*value)));
                if (!stored) return std::unexpected(stored.error());
                ++count;
            }
            return std::optional<Value>(Value::from_int(
                count == 0 ? -1 : count));
        });
    add(registry, "java/io/InputStreamReader", "ready", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto opened = require_open(machine, *object,
                                       kReaderClosedField,
                                       "InputStreamReader");
            if (!opened) return std::unexpected(opened.error());
            auto pending = int_field(machine, *object, kReaderPendingField);
            auto input = reference_field(machine, *object, kReaderInputField);
            if (!pending || !input) {
                return fail(ErrorCode::invalid_state,
                            "InputStreamReader state is invalid");
            }
            auto available = stream_available(machine, *input, 0U);
            if (!available) return std::unexpected(available.error());
            return std::optional<Value>(Value::from_int(
                *pending >= 0 || *available > 0 ? 1 : 0));
        });
    add(registry, "java/io/InputStreamReader", "skip", "(J)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto requested = arguments[1].as_long();
            if (!object) return std::unexpected(object.error());
            if (!requested) return std::unexpected(requested.error());
            auto opened = require_open(machine, *object,
                                       kReaderClosedField,
                                       "InputStreamReader");
            if (!opened) return std::unexpected(opened.error());
            auto input = reference_field(machine, *object, kReaderInputField);
            if (!input) return std::unexpected(input.error());
            auto skipped = stream_skip(machine, *input, *requested, 0U);
            if (!skipped) return std::unexpected(skipped.error());
            return std::optional<Value>(Value::from_long(*skipped));
        });
    add(registry, "java/io/InputStreamReader", "markSupported", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = int_field(machine, *object, kReaderClosedField);
            if (!closed) return std::unexpected(closed.error());
            if (*closed != 0) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto input = reference_field(machine, *object, kReaderInputField);
            if (!input) return std::unexpected(input.error());
            auto supported = is_instance(machine, *input,
                                         "java/io/ByteArrayInputStream");
            if (!supported) return std::unexpected(supported.error());
            return std::optional<Value>(Value::from_int(*supported ? 1 : 0));
        });
    add(registry, "java/io/InputStreamReader", "mark", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto opened = require_open(machine, *object,
                                       kReaderClosedField,
                                       "InputStreamReader");
            if (!opened) return std::unexpected(opened.error());
            auto input = reference_field(machine, *object, kReaderInputField);
            if (!input) return std::unexpected(input.error());
            auto supported = is_instance(machine, *input,
                                         "java/io/ByteArrayInputStream");
            if (!supported) return std::unexpected(supported.error());
            if (!*supported) {
                return fail_java("java/io/IOException",
                                 "mark() not supported");
            }
            auto position = int_field(machine, *input,
                                      kByteInputPositionField);
            if (!position) return std::unexpected(position.error());
            auto stored = set_int_field(machine, *input,
                                        kByteInputMarkField, *position);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/InputStreamReader", "reset", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto opened = require_open(machine, *object,
                                       kReaderClosedField,
                                       "InputStreamReader");
            if (!opened) return std::unexpected(opened.error());
            auto input = reference_field(machine, *object, kReaderInputField);
            if (!input) return std::unexpected(input.error());
            auto supported = is_instance(machine, *input,
                                         "java/io/ByteArrayInputStream");
            if (!supported) return std::unexpected(supported.error());
            if (!*supported) {
                return fail_java("java/io/IOException",
                                 "mark/reset is not supported");
            }
            auto mark = int_field(machine, *input, kByteInputMarkField);
            if (!mark) return std::unexpected(mark.error());
            auto stored = set_int_field(machine, *input,
                                        kByteInputPositionField, *mark);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry, "java/io/InputStreamReader", "getEncoding",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = int_field(machine, *object, kReaderClosedField);
            if (!closed) return std::unexpected(closed.error());
            if (*closed != 0) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto charset = object_charset(machine, *object,
                                          kReaderCharsetField);
            if (!charset) return std::unexpected(charset.error());
            auto name = create_string(machine, charset_display_name(*charset));
            if (!name) return std::unexpected(name.error());
            return std::optional<Value>(Value::from_reference(*name));
        });
    add(registry, "java/io/InputStreamReader", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = int_field(machine, *object, kReaderClosedField);
            if (!closed) return std::unexpected(closed.error());
            if (*closed == 0) {
                auto input = reference_field(machine, *object,
                                             kReaderInputField);
                if (!input) return std::unexpected(input.error());
                auto status = stream_close_input(machine, *input, 0U);
                if (!status) return std::unexpected(status.error());
                auto stored = set_int_field(machine, *object,
                                            kReaderClosedField, 1);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "java/io/Reader", "read", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto array = machine.heap().allocate_array(
                "[C", 1U, Value::from_int(0));
            if (!array) return std::unexpected(array.error());
            auto root = machine.pin_native_root(*array);
            if (!root) return std::unexpected(root.error());
            const Value call_arguments[] {
                Value::from_reference(*array),
                Value::from_int(0),
                Value::from_int(1),
            };
            auto runtime = machine.heap().class_name(*object);
            if (!runtime) return std::unexpected(runtime.error());
            auto invoked = machine.invoke_instance(
                *object, *runtime, "read", "([CII)I", call_arguments);
            if (!invoked) return std::unexpected(invoked.error());
            if (invoked->throwable.has_value()) {
                auto thrown = machine.heap().class_name(*invoked->throwable);
                if (!thrown) return std::unexpected(thrown.error());
                return fail_java(*thrown, "Reader.read override threw");
            }
            if (!invoked->return_value.has_value()) {
                return fail(ErrorCode::internal_error,
                            "Reader.read override returned no value");
            }
            auto count = invoked->return_value->as_int();
            if (!count) return std::unexpected(count.error());
            if (*count < 0) return std::optional<Value>(Value::from_int(-1));
            auto character = char_array_value(machine, *array, 0U);
            if (!character) return std::unexpected(character.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(*character)));
        });
    add(registry, "java/io/Reader", "read", "([C)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto array = arguments[1].as_reference();
            if (!object || !array) {
                return fail(ErrorCode::invalid_argument,
                            "Reader.read arguments are invalid");
            }
            auto length = char_array_length(machine, *array);
            if (!length) return std::unexpected(length.error());
            if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail_java("java/lang/OutOfMemoryError",
                                 "char array exceeds int range");
            }
            const Value call_arguments[] {
                Value::from_reference(*array),
                Value::from_int(0),
                Value::from_int(static_cast<i32>(*length)),
            };
            auto runtime = machine.heap().class_name(*object);
            if (!runtime) return std::unexpected(runtime.error());
            auto invoked = machine.invoke_instance(
                *object, *runtime, "read", "([CII)I", call_arguments);
            if (!invoked) return std::unexpected(invoked.error());
            if (invoked->throwable.has_value()) {
                auto thrown = machine.heap().class_name(*invoked->throwable);
                if (!thrown) return std::unexpected(thrown.error());
                return fail_java(*thrown, "Reader.read override threw");
            }
            return invoked->return_value;
        });
    add(registry, "java/io/Reader", "skip", "(J)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto requested = arguments[1].as_long();
            if (!object) return std::unexpected(object.error());
            if (!requested) return std::unexpected(requested.error());
            if (*requested < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Reader.skip count is negative");
            }
            i64 skipped = 0;
            while (skipped < *requested) {
                auto runtime = machine.heap().class_name(*object);
                if (!runtime) return std::unexpected(runtime.error());
                auto invoked = machine.invoke_instance(*object, *runtime,
                                                       "read", "()I");
                if (!invoked) return std::unexpected(invoked.error());
                if (invoked->throwable.has_value()) {
                    auto thrown = machine.heap().class_name(*invoked->throwable);
                    if (!thrown) return std::unexpected(thrown.error());
                    return fail_java(*thrown, "Reader.read override threw");
                }
                if (!invoked->return_value.has_value()) break;
                auto value = invoked->return_value->as_int();
                if (!value) return std::unexpected(value.error());
                if (*value < 0) break;
                ++skipped;
            }
            return std::optional<Value>(Value::from_long(skipped));
        });
    add(registry, "java/io/Reader", "ready", "()Z",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, "java/io/Reader", "markSupported", "()Z",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, "java/io/Reader", "mark", "(I)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return fail_java("java/io/IOException",
                             "mark is not supported");
        });
    add(registry, "java/io/Reader", "reset", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return fail_java("java/io/IOException",
                             "reset is not supported");
        });

    const auto initialize_writer = [](Machine& machine,
                                      std::span<const Value> arguments,
                                      bool named)
        -> Result<std::optional<Value>> {
        auto object = receiver(arguments);
        auto output = arguments[1].as_reference();
        if (!object) return std::unexpected(object.error());
        if (!output || output->is_null()) {
            return fail_java("java/lang/NullPointerException",
                             "OutputStreamWriter output is null");
        }
        StreamCharset charset = StreamCharset::latin1;
        if (named) {
            auto name = arguments[2].as_reference();
            if (!name) return std::unexpected(name.error());
            auto resolved = resolve_stream_charset(machine, *name);
            if (!resolved) return std::unexpected(resolved.error());
            charset = *resolved;
        }
        auto lock = set_reference_field(machine, *object, kWriterLockField,
                                        *object);
        auto stored_output = set_reference_field(machine, *object,
                                                 kWriterOutputField, *output);
        auto stored_charset = set_int_field(
            machine, *object, kWriterCharsetField,
            static_cast<i32>(charset));
        auto stored_pending = set_int_field(machine, *object,
                                            kWriterPendingField, -1);
        auto stored_closed = set_int_field(machine, *object,
                                           kWriterClosedField, 0);
        if (!lock) return std::unexpected(lock.error());
        if (!stored_output) return std::unexpected(stored_output.error());
        if (!stored_charset) return std::unexpected(stored_charset.error());
        if (!stored_pending) return std::unexpected(stored_pending.error());
        if (!stored_closed) return std::unexpected(stored_closed.error());
        return std::optional<Value> {};
    };
    add(registry, "java/io/OutputStreamWriter", "<init>",
        "(Ljava/io/OutputStream;)V",
        [initialize_writer](Machine& machine, std::span<const Value> arguments) {
            return initialize_writer(machine, arguments, false);
        });
    add(registry, "java/io/OutputStreamWriter", "<init>",
        "(Ljava/io/OutputStream;Ljava/lang/String;)V",
        [initialize_writer](Machine& machine, std::span<const Value> arguments) {
            return initialize_writer(machine, arguments, true);
        });
    add(registry, "java/io/OutputStreamWriter", "write", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto character = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!character) return std::unexpected(character.error());
            auto written = writer_write_character(
                machine, *object,
                static_cast<char16_t>(static_cast<u16>(*character)));
            if (!written) return std::unexpected(written.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/OutputStreamWriter", "write", "([CII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto array = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!object || !array || !offset || !length) {
                return fail(ErrorCode::invalid_argument,
                            "OutputStreamWriter.write arguments are invalid");
            }
            auto array_length = char_array_length(machine, *array);
            if (!array_length) return std::unexpected(array_length.error());
            auto range = validate_char_range(*array_length, *offset, *length);
            if (!range) return std::unexpected(range.error());
            for (i32 index = 0; index < *length; ++index) {
                auto character = char_array_value(
                    machine, *array, static_cast<usize>(*offset + index));
                if (!character) return std::unexpected(character.error());
                auto written = writer_write_character(machine, *object,
                                                      *character);
                if (!written) return std::unexpected(written.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/io/OutputStreamWriter", "write",
        "(Ljava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto string = arguments[1].as_reference();
            auto offset = arguments[2].as_int();
            auto length = arguments[3].as_int();
            if (!object || !string || !offset || !length) {
                return fail(ErrorCode::invalid_argument,
                            "OutputStreamWriter.write arguments are invalid");
            }
            if (string->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Writer string is null");
            }
            auto text = machine.heap().string_value(*string);
            if (!text) return std::unexpected(text.error());
            auto range = validate_char_range(text->size(), *offset, *length);
            if (!range) return std::unexpected(range.error());
            auto written = writer_write_text(
                machine, *object, *text, static_cast<usize>(*offset),
                static_cast<usize>(*length));
            if (!written) return std::unexpected(written.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/OutputStreamWriter", "getEncoding",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = int_field(machine, *object, kWriterClosedField);
            if (!closed) return std::unexpected(closed.error());
            if (*closed != 0) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto charset = object_charset(machine, *object,
                                          kWriterCharsetField);
            if (!charset) return std::unexpected(charset.error());
            auto name = create_string(machine, charset_display_name(*charset));
            if (!name) return std::unexpected(name.error());
            return std::optional<Value>(Value::from_reference(*name));
        });
    add(registry, "java/io/OutputStreamWriter", "flush", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto opened = require_open(machine, *object,
                                       kWriterClosedField,
                                       "OutputStreamWriter");
            if (!opened) return std::unexpected(opened.error());
            auto pending = writer_flush_pending(machine, *object);
            if (!pending) return std::unexpected(pending.error());
            auto output = reference_field(machine, *object,
                                          kWriterOutputField);
            if (!output) return std::unexpected(output.error());
            auto flushed = stream_flush(machine, *output, 0U);
            if (!flushed) return std::unexpected(flushed.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/OutputStreamWriter", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = int_field(machine, *object, kWriterClosedField);
            if (!closed) return std::unexpected(closed.error());
            if (*closed == 0) {
                auto pending = writer_flush_pending(machine, *object);
                if (!pending) return std::unexpected(pending.error());
                auto output = reference_field(machine, *object,
                                              kWriterOutputField);
                if (!output) return std::unexpected(output.error());
                auto flushed = stream_flush(machine, *output, 0U);
                if (!flushed) return std::unexpected(flushed.error());
                auto status = stream_close_output(machine, *output, 0U);
                if (!status) return std::unexpected(status.error());
                auto stored = set_int_field(machine, *object,
                                            kWriterClosedField, 1);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    const auto invoke_writer_range = [](Machine& machine,
                                        ObjectRef object,
                                        ObjectRef array,
                                        i32 offset,
                                        i32 length)
        -> Result<std::optional<Value>> {
        const Value call_arguments[] {
            Value::from_reference(array),
            Value::from_int(offset),
            Value::from_int(length),
        };
        auto runtime = machine.heap().class_name(object);
        if (!runtime) return std::unexpected(runtime.error());
        auto invoked = machine.invoke_instance(
            object, *runtime, "write", "([CII)V", call_arguments);
        if (!invoked) return std::unexpected(invoked.error());
        if (invoked->throwable.has_value()) {
            auto thrown = machine.heap().class_name(*invoked->throwable);
            if (!thrown) return std::unexpected(thrown.error());
            return fail_java(*thrown, "Writer.write override threw");
        }
        return invoked->return_value;
    };
    add(registry, "java/io/Writer", "write", "(I)V",
        [invoke_writer_range](Machine& machine,
                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto character = arguments[1].as_int();
            if (!object) return std::unexpected(object.error());
            if (!character) return std::unexpected(character.error());
            auto array = machine.heap().allocate_array(
                "[C", 1U, Value::from_int(*character & 0xFFFF));
            if (!array) return std::unexpected(array.error());
            auto root = machine.pin_native_root(*array);
            if (!root) return std::unexpected(root.error());
            return invoke_writer_range(machine, *object, *array, 0, 1);
        });
    add(registry, "java/io/Writer", "write", "([C)V",
        [invoke_writer_range](Machine& machine,
                              std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto array = arguments[1].as_reference();
            if (!object || !array) {
                return fail(ErrorCode::invalid_argument,
                            "Writer.write arguments are invalid");
            }
            auto length = char_array_length(machine, *array);
            if (!length) return std::unexpected(length.error());
            if (*length > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail_java("java/lang/OutOfMemoryError",
                                 "char array exceeds int range");
            }
            return invoke_writer_range(machine, *object, *array, 0,
                                       static_cast<i32>(*length));
        });
    const auto write_string = [&registry, invoke_writer_range](
        const char* descriptor,
        bool ranged) {
        add(registry, "java/io/Writer", "write", descriptor,
            [ranged, invoke_writer_range](
                Machine& machine,
                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto string = arguments[1].as_reference();
                if (!object) return std::unexpected(object.error());
                if (!string || string->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "Writer string is null");
                }
                auto text = machine.heap().string_value(*string);
                if (!text) return std::unexpected(text.error());
                i32 offset = 0;
                i32 length = static_cast<i32>(text->size());
                if (ranged) {
                    auto parsed_offset = arguments[2].as_int();
                    auto parsed_length = arguments[3].as_int();
                    if (!parsed_offset || !parsed_length) {
                        return fail(ErrorCode::invalid_argument,
                                    "Writer string range is invalid");
                    }
                    offset = *parsed_offset;
                    length = *parsed_length;
                }
                auto range = validate_char_range(text->size(), offset, length);
                if (!range) return std::unexpected(range.error());
                auto array = machine.heap().allocate_array(
                    "[C", static_cast<usize>(length), Value::from_int(0));
                if (!array) return std::unexpected(array.error());
                auto root = machine.pin_native_root(*array);
                if (!root) return std::unexpected(root.error());
                for (i32 index = 0; index < length; ++index) {
                    auto stored = set_char_array_value(
                        machine, *array, static_cast<usize>(index),
                        (*text)[static_cast<usize>(offset + index)]);
                    if (!stored) return std::unexpected(stored.error());
                }
                return invoke_writer_range(machine, *object, *array,
                                           0, length);
            });
    };
    write_string("(Ljava/lang/String;)V", false);
    write_string("(Ljava/lang/String;II)V", true);
}

void register_data_output(NativeMethodRegistry& registry) {
    const auto add_integer_write = [&registry](const char* name,
                                               const char* descriptor,
                                               usize byte_count,
                                               auto extract) {
        add(registry, "java/io/DataOutputStream", name, descriptor,
            [byte_count, extract](Machine& machine,
                                  std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                if (!output) return std::unexpected(output.error());
                auto extracted = extract(arguments[1]);
                if (!extracted) return std::unexpected(extracted.error());
                const u64 bits = *extracted;
                std::vector<u8> bytes(byte_count);
                for (usize index = 0; index < byte_count; ++index) {
                    const usize shift = (byte_count - 1U - index) * 8U;
                    bytes[index] = static_cast<u8>(bits >> shift);
                }
                auto written = write_bytes(machine, *output, bytes);
                if (!written) return std::unexpected(written.error());
                return std::optional<Value> {};
            });
    };
    add_integer_write("writeBoolean", "(Z)V", 1,
        [](const Value& value) -> Result<u64> {
            auto integer = value.as_int();
            if (!integer) return std::unexpected(integer.error());
            return *integer == 0 ? 0U : 1U;
        });
    add_integer_write("writeByte", "(I)V", 1,
        [](const Value& value) -> Result<u64> {
            auto integer = value.as_int();
            if (!integer) return std::unexpected(integer.error());
            return static_cast<u8>(*integer);
        });
    add_integer_write("writeShort", "(I)V", 2,
        [](const Value& value) -> Result<u64> {
            auto integer = value.as_int();
            if (!integer) return std::unexpected(integer.error());
            return static_cast<u16>(*integer);
        });
    add_integer_write("writeChar", "(I)V", 2,
        [](const Value& value) -> Result<u64> {
            auto integer = value.as_int();
            if (!integer) return std::unexpected(integer.error());
            return static_cast<u16>(*integer);
        });
    add_integer_write("writeInt", "(I)V", 4,
        [](const Value& value) -> Result<u64> {
            auto integer = value.as_int();
            if (!integer) return std::unexpected(integer.error());
            return static_cast<u32>(*integer);
        });
    add_integer_write("writeLong", "(J)V", 8,
        [](const Value& value) -> Result<u64> {
            auto integer = value.as_long();
            if (!integer) return std::unexpected(integer.error());
            return static_cast<u64>(*integer);
        });
    add_integer_write("writeFloat", "(F)V", 4,
        [](const Value& value) -> Result<u64> {
            auto number = value.as_float();
            if (!number) return std::unexpected(number.error());
            return std::bit_cast<u32>(*number);
        });
    add_integer_write("writeDouble", "(D)V", 8,
        [](const Value& value) -> Result<u64> {
            auto number = value.as_double();
            if (!number) return std::unexpected(number.error());
            return std::bit_cast<u64>(*number);
        });

    const auto add_string_write = [&registry](const char* name,
                                              bool wide) {
        add(registry, "java/io/DataOutputStream", name,
            "(Ljava/lang/String;)V",
            [wide](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto output = receiver(arguments);
                auto string = arguments[1].as_reference();
                if (!output) return std::unexpected(output.error());
                if (!string || string->is_null())
                    return fail_java("java/lang/NullPointerException",
                                     "DataOutput string is null");
                auto text = machine.heap().string_value(*string);
                if (!text) return std::unexpected(text.error());
                std::vector<u8> bytes;
                bytes.reserve(text->size() * (wide ? 2U : 1U));
                for (const char16_t character : *text) {
                    const u16 value = static_cast<u16>(character);
                    if (wide) bytes.push_back(static_cast<u8>(value >> 8U));
                    bytes.push_back(static_cast<u8>(value));
                }
                auto written = write_bytes(machine, *output, bytes);
                if (!written) return std::unexpected(written.error());
                return std::optional<Value> {};
            });
    };
    add_string_write("writeBytes", false);
    add_string_write("writeChars", true);
    add(registry, "java/io/DataOutputStream", "writeUTF",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto output = receiver(arguments);
            auto string = arguments[1].as_reference();
            if (!output) return std::unexpected(output.error());
            if (!string || string->is_null())
                return fail_java("java/lang/NullPointerException",
                                 "DataOutput UTF string is null");
            auto text = machine.heap().string_value(*string);
            if (!text) return std::unexpected(text.error());
            auto encoded = encode_modified_utf8(*text);
            if (!encoded) return std::unexpected(encoded.error());
            if (encoded->size() > 65535U)
                return fail_java("java/io/UTFDataFormatException",
                                 "modified UTF-8 string exceeds 65535 bytes");
            std::vector<u8> bytes;
            bytes.reserve(encoded->size() + 2U);
            bytes.push_back(static_cast<u8>(encoded->size() >> 8U));
            bytes.push_back(static_cast<u8>(encoded->size()));
            for (const char byte : *encoded)
                bytes.push_back(static_cast<u8>(
                    static_cast<unsigned char>(byte)));
            auto written = write_bytes(machine, *output, bytes);
            if (!written) return std::unexpected(written.error());
            return std::optional<Value> {};
        });
    add(registry, "java/io/DataOutputStream", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto output = receiver(arguments);
            if (!output) return std::unexpected(output.error());
            auto written = int_field(machine, *output,
                                     kDataOutputWrittenField);
            if (!written) return std::unexpected(written.error());
            return std::optional<Value>(Value::from_int(*written));
        });
}

} // namespace

Result<std::vector<u8>> read_input_stream_all(Machine& machine,
                                              ObjectRef stream,
                                              usize maximum_bytes) {
    if (stream.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Image input stream is null");
    }
    std::vector<u8> bytes;
    bytes.reserve(std::min<usize>(maximum_bytes, 16U * 1024U));
    while (true) {
        auto value = stream_read_one(machine, stream, 0U);
        if (!value) return std::unexpected(value.error());
        if (*value < 0) break;
        if (bytes.size() >= maximum_bytes) {
            return fail(ErrorCode::overflow,
                        "input stream exceeds the graphics decode budget");
        }
        bytes.push_back(static_cast<u8>(*value));
    }
    return bytes;
}

void register_io_natives(NativeMethodRegistry& registry) {
    register_base_streams(registry);
    register_byte_array_streams(registry);
    register_filter_streams(registry);
    register_reader_writer(registry);
    register_data_input(registry);
    register_data_output(registry);
}

} // namespace phoneme::vm
