#include "Jdk8CompatNativesParts.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include <zlib.h>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kInputDataField = 0U;
constexpr usize kInputPositionField = 1U;
constexpr usize kInputClosedField = 2U;
constexpr usize kOutputDelegateField = 0U;
constexpr usize kOutputDataField = 1U;
constexpr usize kOutputSizeField = 2U;
constexpr usize kOutputClosedField = 3U;
constexpr usize kInflaterInputField = 0U;
constexpr usize kInflaterOutputField = 1U;
constexpr usize kInflaterOffsetField = 2U;
constexpr usize kInflaterFinishedField = 3U;
constexpr usize kInflaterNeedsInputField = 4U;
constexpr usize kInflaterNeedsDictionaryField = 5U;
constexpr usize kInflaterEndedField = 6U;
constexpr usize kInitialOutputCapacity = 512U;
constexpr usize kZlibChunkSize = 8192U;

[[nodiscard]] Result<ObjectRef> allocate_byte_array(Machine& machine,
                                                     usize length) {
    return machine.heap().allocate_array("[B", length, Value::from_int(0));
}

[[nodiscard]] Result<std::vector<u8>> read_input_stream(
    Machine& machine,
    ObjectRef input) {
    std::vector<u8> compressed;
    while (true) {
        auto read = invoke_checked(machine, input, "java/io/InputStream",
                                   "read", "()I");
        if (!read) return std::unexpected(read.error());
        if (!read->has_value()) {
            return fail(ErrorCode::internal_error,
                        "InputStream.read returned no value");
        }
        auto value = read->value().as_int();
        if (!value) return std::unexpected(value.error());
        if (*value < 0) break;
        compressed.push_back(static_cast<u8>(*value & 0xFF));
    }
    return compressed;
}

[[nodiscard]] Result<std::vector<u8>> inflate_gzip(
    std::span<const u8> compressed) {
    if (compressed.size() > static_cast<usize>(
            std::numeric_limits<uInt>::max())) {
        return fail(ErrorCode::overflow,
                    "GZIP input is too large for zlib");
    }
    z_stream stream {};
    stream.next_in = const_cast<Bytef*>(
        reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return fail_java("java/util/zip/ZipException",
                         "Cannot initialize GZIP inflater");
    }

    std::vector<u8> output;
    std::array<u8, kZlibChunkSize> chunk {};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const usize produced = chunk.size() - stream.avail_out;
        output.insert(output.end(), chunk.begin(), chunk.begin() +
                      static_cast<std::ptrdiff_t>(produced));
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        return fail_java("java/util/zip/ZipException",
                         "Invalid or truncated GZIP stream");
    }
    return output;
}

[[nodiscard]] Result<std::vector<u8>> deflate_gzip(
    std::span<const u8> source) {
    if (source.size() > static_cast<usize>(
            std::numeric_limits<uInt>::max())) {
        return fail(ErrorCode::overflow,
                    "GZIP output is too large for zlib");
    }
    z_stream stream {};
    stream.next_in = const_cast<Bytef*>(
        reinterpret_cast<const Bytef*>(source.data()));
    stream.avail_in = static_cast<uInt>(source.size());
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return fail_java("java/io/IOException",
                         "Cannot initialize GZIP compressor");
    }

    std::vector<u8> output;
    std::array<u8, kZlibChunkSize> chunk {};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = deflate(&stream, Z_FINISH);
        const usize produced = chunk.size() - stream.avail_out;
        output.insert(output.end(), chunk.begin(), chunk.begin() +
                      static_cast<std::ptrdiff_t>(produced));
    }
    deflateEnd(&stream);
    if (status != Z_STREAM_END) {
        return fail_java("java/io/IOException",
                         "GZIP compression failed");
    }
    return output;
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

void register_gzip_input(NativeMethodRegistry& registry) {
    add(registry, "java/util/zip/GZIPInputStream", "<init>",
        "(Ljava/io/InputStream;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto input = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!input) return std::unexpected(input.error());
            auto compressed = read_input_stream(machine, *input);
            if (!compressed) return std::unexpected(compressed.error());
            auto decompressed = inflate_gzip(*compressed);
            if (!decompressed) return std::unexpected(decompressed.error());
            auto data = allocate_byte_array(machine, decompressed->size());
            if (!data) return std::unexpected(data.error());
            auto data_root = machine.pin_native_root(*data);
            if (!data_root) return std::unexpected(data_root.error());
            auto written = machine.heap().write_byte_array(*data, 0U,
                                                            *decompressed);
            if (!written) return std::unexpected(written.error());
            auto data_stored = set_reference_field(machine, *object,
                                                   kInputDataField, *data);
            auto position_stored = set_int_field(machine, *object,
                                                 kInputPositionField, 0);
            auto closed_stored = set_int_field(machine, *object,
                                               kInputClosedField, 0);
            if (!data_stored) return std::unexpected(data_stored.error());
            if (!position_stored) {
                return std::unexpected(position_stored.error());
            }
            if (!closed_stored) {
                return std::unexpected(closed_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/zip/GZIPInputStream", "read", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto opened = require_open(machine, *object, kInputClosedField,
                                       "GZIPInputStream");
            if (!opened) return std::unexpected(opened.error());
            auto data = reference_field(machine, *object, kInputDataField);
            auto position = int_field(machine, *object, kInputPositionField);
            if (!data) return std::unexpected(data.error());
            if (!position) return std::unexpected(position.error());
            auto length = machine.heap().array_length(*data);
            if (!length) return std::unexpected(length.error());
            if (*position < 0 || static_cast<usize>(*position) >= *length) {
                return std::optional<Value>(Value::from_int(-1));
            }
            auto value = machine.heap().element(
                *data, static_cast<usize>(*position));
            if (!value) return std::unexpected(value.error());
            auto byte = value->as_int();
            if (!byte) return std::unexpected(byte.error());
            auto advanced = set_int_field(machine, *object,
                                          kInputPositionField,
                                          *position + 1);
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(Value::from_int(*byte & 0xFF));
        });
    add(registry, "java/util/zip/GZIPInputStream", "read", "([BII)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto destination = reference_argument(arguments, 1U);
            auto offset = int_argument(arguments, 2U);
            auto requested = int_argument(arguments, 3U);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            if (!offset) return std::unexpected(offset.error());
            if (!requested) return std::unexpected(requested.error());
            auto opened = require_open(machine, *object, kInputClosedField,
                                       "GZIPInputStream");
            if (!opened) return std::unexpected(opened.error());
            auto destination_length = machine.heap().array_length(*destination);
            if (!destination_length) {
                return std::unexpected(destination_length.error());
            }
            if (*offset < 0 || *requested < 0 ||
                static_cast<usize>(*offset) > *destination_length ||
                static_cast<usize>(*requested) >
                    *destination_length - static_cast<usize>(*offset)) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "GZIPInputStream read range is invalid");
            }
            if (*requested == 0) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto data = reference_field(machine, *object, kInputDataField);
            auto position = int_field(machine, *object, kInputPositionField);
            if (!data) return std::unexpected(data.error());
            if (!position) return std::unexpected(position.error());
            auto length = machine.heap().array_length(*data);
            if (!length) return std::unexpected(length.error());
            if (*position < 0 || static_cast<usize>(*position) >= *length) {
                return std::optional<Value>(Value::from_int(-1));
            }
            const usize available = *length - static_cast<usize>(*position);
            const usize count = std::min(
                available, static_cast<usize>(*requested));
            auto copied = machine.heap().copy_array_range(
                *data, static_cast<usize>(*position), *destination,
                static_cast<usize>(*offset), count);
            if (!copied) return std::unexpected(copied.error());
            auto advanced = set_int_field(
                machine, *object, kInputPositionField,
                *position + static_cast<i32>(count));
            if (!advanced) return std::unexpected(advanced.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(count)));
        });
    add(registry, "java/util/zip/GZIPInputStream", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = set_int_field(machine, *object,
                                        kInputClosedField, 1);
            if (!closed) return std::unexpected(closed.error());
            return std::optional<Value> {};
        });
}

[[nodiscard]] Status ensure_output_capacity(Machine& machine,
                                            ObjectRef object,
                                            i32 minimum) {
    auto data = reference_field(machine, object, kOutputDataField);
    if (!data) return std::unexpected(data.error());
    if (data->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "GZIPOutputStream buffer is not initialized");
    }
    auto capacity = machine.heap().array_length(*data);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    usize next = *capacity == 0U ? kInitialOutputCapacity : *capacity * 2U;
    if (next < static_cast<usize>(minimum)) {
        next = static_cast<usize>(minimum);
    }
    auto replacement = allocate_byte_array(machine, next);
    if (!replacement) return std::unexpected(replacement.error());
    auto replacement_root = machine.pin_native_root(*replacement);
    if (!replacement_root) {
        return std::unexpected(replacement_root.error());
    }
    auto size = int_field(machine, object, kOutputSizeField);
    if (!size) return std::unexpected(size.error());
    auto copied = machine.heap().copy_array_range(
        *data, 0U, *replacement, 0U, static_cast<usize>(*size));
    if (!copied) return std::unexpected(copied.error());
    return set_reference_field(machine, object, kOutputDataField,
                               *replacement);
}

void register_gzip_output(NativeMethodRegistry& registry) {
    add(registry, "java/util/zip/GZIPOutputStream", "<init>",
        "(Ljava/io/OutputStream;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto output = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!output) return std::unexpected(output.error());
            auto data = allocate_byte_array(machine, kInitialOutputCapacity);
            if (!data) return std::unexpected(data.error());
            auto output_stored = set_reference_field(
                machine, *object, kOutputDelegateField, *output);
            auto data_stored = set_reference_field(
                machine, *object, kOutputDataField, *data);
            auto size_stored = set_int_field(machine, *object,
                                             kOutputSizeField, 0);
            auto closed_stored = set_int_field(machine, *object,
                                               kOutputClosedField, 0);
            if (!output_stored) {
                return std::unexpected(output_stored.error());
            }
            if (!data_stored) return std::unexpected(data_stored.error());
            if (!size_stored) return std::unexpected(size_stored.error());
            if (!closed_stored) {
                return std::unexpected(closed_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/zip/GZIPOutputStream", "write", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto value = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto opened = require_open(machine, *object, kOutputClosedField,
                                       "GZIPOutputStream");
            if (!opened) return std::unexpected(opened.error());
            auto size = int_field(machine, *object, kOutputSizeField);
            if (!size) return std::unexpected(size.error());
            auto capacity = ensure_output_capacity(machine, *object,
                                                   *size + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto data = reference_field(machine, *object, kOutputDataField);
            if (!data) return std::unexpected(data.error());
            auto stored = machine.heap().set_element(
                *data, static_cast<usize>(*size),
                Value::from_int(static_cast<i32>(
                    static_cast<i8>(*value & 0xFF))));
            if (!stored) return std::unexpected(stored.error());
            auto size_stored = set_int_field(machine, *object,
                                             kOutputSizeField, *size + 1);
            if (!size_stored) {
                return std::unexpected(size_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/zip/GZIPOutputStream", "write", "([BII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto source = reference_argument(arguments, 1U);
            auto offset = int_argument(arguments, 2U);
            auto requested = int_argument(arguments, 3U);
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            if (!offset) return std::unexpected(offset.error());
            if (!requested) return std::unexpected(requested.error());
            auto opened = require_open(machine, *object, kOutputClosedField,
                                       "GZIPOutputStream");
            if (!opened) return std::unexpected(opened.error());
            auto source_length = machine.heap().array_length(*source);
            if (!source_length) return std::unexpected(source_length.error());
            if (*offset < 0 || *requested < 0 ||
                static_cast<usize>(*offset) > *source_length ||
                static_cast<usize>(*requested) >
                    *source_length - static_cast<usize>(*offset)) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "GZIPOutputStream write range is invalid");
            }
            auto size = int_field(machine, *object, kOutputSizeField);
            if (!size) return std::unexpected(size.error());
            if (*requested > std::numeric_limits<i32>::max() - *size) {
                return fail(ErrorCode::overflow,
                            "GZIPOutputStream size overflow");
            }
            auto capacity = ensure_output_capacity(
                machine, *object, *size + *requested);
            if (!capacity) return std::unexpected(capacity.error());
            auto data = reference_field(machine, *object, kOutputDataField);
            if (!data) return std::unexpected(data.error());
            auto copied = machine.heap().copy_array_range(
                *source, static_cast<usize>(*offset), *data,
                static_cast<usize>(*size), static_cast<usize>(*requested));
            if (!copied) return std::unexpected(copied.error());
            auto size_stored = set_int_field(machine, *object,
                                             kOutputSizeField,
                                             *size + *requested);
            if (!size_stored) {
                return std::unexpected(size_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/zip/GZIPOutputStream", "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto closed = int_field(machine, *object, kOutputClosedField);
            if (!closed) return std::unexpected(closed.error());
            if (*closed != 0) return std::optional<Value> {};
            auto data = reference_field(machine, *object, kOutputDataField);
            auto size = int_field(machine, *object, kOutputSizeField);
            auto output = reference_field(machine, *object,
                                          kOutputDelegateField);
            if (!data) return std::unexpected(data.error());
            if (!size) return std::unexpected(size.error());
            if (!output) return std::unexpected(output.error());
            auto source = machine.heap().read_byte_array(
                *data, 0U, static_cast<usize>(*size));
            if (!source) return std::unexpected(source.error());
            auto compressed = deflate_gzip(*source);
            if (!compressed) return std::unexpected(compressed.error());
            auto compressed_array = allocate_byte_array(machine,
                                                        compressed->size());
            if (!compressed_array) {
                return std::unexpected(compressed_array.error());
            }
            auto compressed_root = machine.pin_native_root(*compressed_array);
            if (!compressed_root) {
                return std::unexpected(compressed_root.error());
            }
            auto written = machine.heap().write_byte_array(
                *compressed_array, 0U, *compressed);
            if (!written) return std::unexpected(written.error());
            const std::array<Value, 3> write_arguments {
                Value::from_reference(*compressed_array), Value::from_int(0),
                Value::from_int(static_cast<i32>(compressed->size())),
            };
            auto delegated = invoke_checked(
                machine, *output, "java/io/OutputStream", "write",
                "([BII)V", write_arguments);
            if (!delegated) return std::unexpected(delegated.error());
            auto delegated_close = invoke_checked(
                machine, *output, "java/io/OutputStream", "close", "()V");
            if (!delegated_close) {
                return std::unexpected(delegated_close.error());
            }
            auto marked = set_int_field(machine, *object,
                                        kOutputClosedField, 1);
            if (!marked) return std::unexpected(marked.error());
            return std::optional<Value> {};
        });
}

void register_inflater(NativeMethodRegistry& registry) {
    add(registry, "java/util/zip/Inflater", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto input_stored = set_reference_field(
                machine, *object, kInflaterInputField, {});
            auto output_stored = set_reference_field(
                machine, *object, kInflaterOutputField, {});
            auto offset_stored = set_int_field(
                machine, *object, kInflaterOffsetField, 0);
            auto finished_stored = set_int_field(
                machine, *object, kInflaterFinishedField, 0);
            auto needs_input_stored = set_int_field(
                machine, *object, kInflaterNeedsInputField, 1);
            auto dictionary_stored = set_int_field(
                machine, *object, kInflaterNeedsDictionaryField, 0);
            auto ended_stored = set_int_field(
                machine, *object, kInflaterEndedField, 0);
            if (!input_stored) return std::unexpected(input_stored.error());
            if (!output_stored) return std::unexpected(output_stored.error());
            if (!offset_stored) return std::unexpected(offset_stored.error());
            if (!finished_stored) return std::unexpected(finished_stored.error());
            if (!needs_input_stored) {
                return std::unexpected(needs_input_stored.error());
            }
            if (!dictionary_stored) {
                return std::unexpected(dictionary_stored.error());
            }
            if (!ended_stored) return std::unexpected(ended_stored.error());
            return std::optional<Value> {};
        });

    add(registry, "java/util/zip/Inflater", "setInput", "([B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto input = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!input) return std::unexpected(input.error());
            auto ended = int_field(machine, *object, kInflaterEndedField);
            if (!ended) return std::unexpected(ended.error());
            if (*ended != 0) {
                return fail_java("java/lang/IllegalStateException",
                                 "Inflater has been ended");
            }
            auto input_stored = set_reference_field(
                machine, *object, kInflaterInputField, *input);
            auto output_stored = set_reference_field(
                machine, *object, kInflaterOutputField, {});
            auto offset_stored = set_int_field(
                machine, *object, kInflaterOffsetField, 0);
            auto finished_stored = set_int_field(
                machine, *object, kInflaterFinishedField, 0);
            auto needs_input_stored = set_int_field(
                machine, *object, kInflaterNeedsInputField, 0);
            auto dictionary_stored = set_int_field(
                machine, *object, kInflaterNeedsDictionaryField, 0);
            if (!input_stored) return std::unexpected(input_stored.error());
            if (!output_stored) return std::unexpected(output_stored.error());
            if (!offset_stored) return std::unexpected(offset_stored.error());
            if (!finished_stored) return std::unexpected(finished_stored.error());
            if (!needs_input_stored) {
                return std::unexpected(needs_input_stored.error());
            }
            if (!dictionary_stored) {
                return std::unexpected(dictionary_stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "java/util/zip/Inflater", "inflate", "([B)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto destination = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto ended = int_field(machine, *object, kInflaterEndedField);
            if (!ended) return std::unexpected(ended.error());
            if (*ended != 0) {
                return fail_java("java/lang/IllegalStateException",
                                 "Inflater has been ended");
            }
            auto output = reference_field(machine, *object, kInflaterOutputField);
            if (!output) return std::unexpected(output.error());
            if (output->is_null()) {
                auto input = reference_field(machine, *object, kInflaterInputField);
                if (!input) return std::unexpected(input.error());
                if (input->is_null()) {
                    auto marked = set_int_field(
                        machine, *object, kInflaterNeedsInputField, 1);
                    if (!marked) return std::unexpected(marked.error());
                    return std::optional<Value>(Value::from_int(0));
                }
                auto input_length = machine.heap().array_length(*input);
                if (!input_length) return std::unexpected(input_length.error());
                auto compressed = machine.heap().read_byte_array(
                    *input, 0U, *input_length);
                if (!compressed) return std::unexpected(compressed.error());
                if (compressed->size() > static_cast<usize>(
                        std::numeric_limits<uInt>::max())) {
                    return fail_java("java/util/zip/DataFormatException",
                                     "Inflater input is too large");
                }

                z_stream stream {};
                stream.next_in = compressed->empty()
                    ? nullptr
                    : reinterpret_cast<Bytef*>(compressed->data());
                stream.avail_in = static_cast<uInt>(compressed->size());
                if (inflateInit(&stream) != Z_OK) {
                    return fail_java("java/util/zip/DataFormatException",
                                     "Cannot initialize inflater");
                }
                std::vector<u8> inflated;
                std::array<u8, kZlibChunkSize> chunk {};
                int status = Z_OK;
                while (status == Z_OK) {
                    stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
                    stream.avail_out = static_cast<uInt>(chunk.size());
                    status = ::inflate(&stream, Z_NO_FLUSH);
                    const usize produced = chunk.size() - stream.avail_out;
                    inflated.insert(
                        inflated.end(), chunk.begin(),
                        chunk.begin() + static_cast<std::ptrdiff_t>(produced));
                }
                if (status == Z_NEED_DICT) {
                    inflateEnd(&stream);
                    auto marked = set_int_field(
                        machine, *object, kInflaterNeedsDictionaryField, 1);
                    if (!marked) return std::unexpected(marked.error());
                    return std::optional<Value>(Value::from_int(0));
                }
                if (status != Z_STREAM_END) {
                    const bool consumed_input = stream.avail_in == 0U;
                    inflateEnd(&stream);
                    if (consumed_input && status == Z_BUF_ERROR) {
                        auto marked = set_int_field(
                            machine, *object, kInflaterNeedsInputField, 1);
                        if (!marked) return std::unexpected(marked.error());
                        return std::optional<Value>(Value::from_int(0));
                    }
                    return fail_java("java/util/zip/DataFormatException",
                                     "Invalid zlib stream");
                }
                inflateEnd(&stream);

                auto data = allocate_byte_array(machine, inflated.size());
                if (!data) return std::unexpected(data.error());
                auto data_root = machine.pin_native_root(*data);
                if (!data_root) return std::unexpected(data_root.error());
                auto written = machine.heap().write_byte_array(*data, 0U, inflated);
                if (!written) return std::unexpected(written.error());
                auto stored = set_reference_field(
                    machine, *object, kInflaterOutputField, *data);
                if (!stored) return std::unexpected(stored.error());
                output = *data;
            }

            auto destination_length = machine.heap().array_length(*destination);
            auto output_length = machine.heap().array_length(*output);
            auto offset = int_field(machine, *object, kInflaterOffsetField);
            if (!destination_length) {
                return std::unexpected(destination_length.error());
            }
            if (!output_length) return std::unexpected(output_length.error());
            if (!offset) return std::unexpected(offset.error());
            if (*offset < 0 || static_cast<usize>(*offset) > *output_length) {
                return fail(ErrorCode::invalid_state,
                            "Inflater output position is invalid");
            }
            const usize remaining =
                *output_length - static_cast<usize>(*offset);
            const usize count = std::min(remaining, *destination_length);
            if (count != 0U) {
                auto copied = machine.heap().copy_array_range(
                    *output, static_cast<usize>(*offset), *destination,
                    0U, count);
                if (!copied) return std::unexpected(copied.error());
            }
            const usize next = static_cast<usize>(*offset) + count;
            auto offset_stored = set_int_field(
                machine, *object, kInflaterOffsetField,
                static_cast<i32>(next));
            auto finished_stored = set_int_field(
                machine, *object, kInflaterFinishedField,
                next == *output_length ? 1 : 0);
            if (!offset_stored) return std::unexpected(offset_stored.error());
            if (!finished_stored) return std::unexpected(finished_stored.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(count)));
        });

    const auto flag = [&registry](const char* name, usize field) {
        add(registry, "java/util/zip/Inflater", name, "()Z",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto value = int_field(machine, *object, field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(
                    Value::from_int(*value != 0 ? 1 : 0));
            });
    };
    flag("finished", kInflaterFinishedField);
    flag("needsInput", kInflaterNeedsInputField);
    flag("needsDictionary", kInflaterNeedsDictionaryField);

    add(registry, "java/util/zip/Inflater", "end", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto ended = set_int_field(machine, *object, kInflaterEndedField, 1);
            auto input_cleared = set_reference_field(
                machine, *object, kInflaterInputField, {});
            auto output_cleared = set_reference_field(
                machine, *object, kInflaterOutputField, {});
            if (!ended) return std::unexpected(ended.error());
            if (!input_cleared) return std::unexpected(input_cleared.error());
            if (!output_cleared) return std::unexpected(output_cleared.error());
            return std::optional<Value> {};
        });
}

} // namespace

void register_jdk8_zip_natives(NativeMethodRegistry& registry) {
    register_gzip_input(registry);
    register_gzip_output(registry);
    register_inflater(registry);
}

} // namespace phoneme::vm
