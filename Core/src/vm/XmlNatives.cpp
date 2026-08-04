#include "XmlNatives.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr std::string_view kFactory = "javax/xml/parsers/SAXParserFactory";
constexpr std::string_view kFactoryImpl = "phoneme/xml/SAXParserFactoryImpl";
constexpr std::string_view kParser = "javax/xml/parsers/SAXParser";
constexpr std::string_view kParserImpl = "phoneme/xml/SAXParserImpl";
constexpr std::string_view kInputSource = "org/xml/sax/InputSource";
constexpr std::string_view kDefaultHandler = "org/xml/sax/helpers/DefaultHandler";
constexpr std::string_view kAttributesImpl = "org/xml/sax/helpers/AttributesImpl";
constexpr usize kMaximumXmlBytes = 32U * 1024U * 1024U;
constexpr usize kReadChunk = 4096U;

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
        return fail(ErrorCode::invalid_argument, "XML native is missing receiver");
    }
    auto value = arguments.front().as_reference();
    if (!value) return std::unexpected(value.error());
    if (value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "XML native receiver is null");
    }
    return *value;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "XML native reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!allow_null && value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "XML native reference argument is null");
    }
    return *value;
}

[[nodiscard]] Result<i32> int_argument(std::span<const Value> arguments,
                                       usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "XML native integer argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<FieldLocation> field_location(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    return machine.class_states().resolve_field(owner, name, descriptor, false);
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

[[nodiscard]] Result<i32> int_field(Machine& machine,
                                    ObjectRef object,
                                    std::string_view owner,
                                    std::string_view name) {
    auto location = field_location(machine, owner, name, "I");
    if (!location) return std::unexpected(location.error());
    auto value = machine.heap().field(object, location->index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Status set_reference_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    ObjectRef value) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index,
                                    Value::from_reference(value));
}

[[nodiscard]] Status set_int_field(Machine& machine,
                                   ObjectRef object,
                                   std::string_view owner,
                                   std::string_view name,
                                   i32 value) {
    auto location = field_location(machine, owner, name, "I");
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index,
                                    Value::from_int(value));
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

[[nodiscard]] Result<ObjectRef> make_string(Machine& machine,
                                            std::u16string text) {
    auto object = allocate_instance(machine, "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] Result<std::u16string> string_value(Machine& machine,
                                                  ObjectRef value,
                                                  bool allow_null = false) {
    if (value.is_null()) {
        if (allow_null) return std::u16string {};
        return fail_java("java/lang/NullPointerException", "XML string is null");
    }
    return machine.heap().string_value(value);
}

[[nodiscard]] Result<std::optional<Value>> invoke_checked(
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
        auto thrown = machine.heap().class_name(*invoked->throwable);
        if (!thrown) return std::unexpected(thrown.error());
        return fail_java(*thrown,
                         std::string(owner) + "." + std::string(name) +
                             " threw during XML processing");
    }
    return invoked->return_value;
}

[[nodiscard]] Status invoke_void(Machine& machine,
                                 ObjectRef object,
                                 std::string_view owner,
                                 std::string_view name,
                                 std::string_view descriptor,
                                 std::span<const Value> arguments = {}) {
    auto invoked = invoke_checked(machine, object, owner, name, descriptor,
                                  arguments);
    if (!invoked) return std::unexpected(invoked.error());
    return {};
}

[[nodiscard]] Result<ObjectRef> allocate_string_array(Machine& machine,
                                                       usize length) {
    auto array = machine.heap().allocate_array(
        "[Ljava/lang/String;", length, Value::from_reference({}));
    if (array || array.error().code != ErrorCode::overflow) return array;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.heap().allocate_array(
        "[Ljava/lang/String;", length, Value::from_reference({}));
}

[[nodiscard]] Result<ObjectRef> make_char_array(Machine& machine,
                                                std::u16string_view text) {
    auto array = machine.heap().allocate_array("[C", text.size(),
                                               Value::from_int(0));
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < text.size(); ++index) {
        auto stored = machine.heap().set_element(
            *array, index,
            Value::from_int(static_cast<i32>(static_cast<u16>(text[index]))));
        if (!stored) return std::unexpected(stored.error());
    }
    return *array;
}

[[nodiscard]] Result<std::vector<u8>> read_input_stream(Machine& machine,
                                                        ObjectRef stream) {
    if (stream.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "XML input stream is null");
    }
    auto stream_root = machine.pin_native_root(stream);
    if (!stream_root) return std::unexpected(stream_root.error());
    auto buffer = machine.heap().allocate_array("[B", kReadChunk,
                                                Value::from_int(0));
    if (!buffer) return std::unexpected(buffer.error());
    auto buffer_root = machine.pin_native_root(*buffer);
    if (!buffer_root) return std::unexpected(buffer_root.error());
    auto runtime = machine.heap().class_name(stream);
    if (!runtime) return std::unexpected(runtime.error());

    std::vector<u8> result;
    usize zero_reads = 0;
    while (result.size() < kMaximumXmlBytes) {
        const std::array<Value, 3> arguments {{
            Value::from_reference(*buffer), Value::from_int(0),
            Value::from_int(static_cast<i32>(kReadChunk)),
        }};
        auto read = invoke_checked(machine, stream, *runtime, "read", "([BII)I",
                                   arguments);
        if (!read) return std::unexpected(read.error());
        if (!read->has_value()) {
            return fail(ErrorCode::internal_error,
                        "InputStream.read returned no value");
        }
        auto count = (*read)->as_int();
        if (!count) return std::unexpected(count.error());
        if (*count < 0) return result;
        if (*count == 0) {
            if (++zero_reads > 16U) {
                return fail_java("java/io/IOException",
                                 "XML input stream made no progress");
            }
            machine.cooperative_yield();
            continue;
        }
        zero_reads = 0;
        if (static_cast<usize>(*count) > kReadChunk ||
            result.size() + static_cast<usize>(*count) > kMaximumXmlBytes) {
            return fail_java("org/xml/sax/SAXException",
                             "XML document exceeds size limit");
        }
        auto bytes = machine.heap().read_byte_array(
            *buffer, 0U, static_cast<usize>(*count));
        if (!bytes) return std::unexpected(bytes.error());
        result.insert(result.end(), bytes->begin(), bytes->end());
    }
    return fail_java("org/xml/sax/SAXException",
                     "XML document exceeds size limit");
}

[[nodiscard]] Result<std::u16string> read_character_stream(
    Machine& machine,
    ObjectRef reader) {
    if (reader.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "XML character stream is null");
    }
    auto reader_root = machine.pin_native_root(reader);
    if (!reader_root) return std::unexpected(reader_root.error());
    auto buffer = machine.heap().allocate_array("[C", kReadChunk,
                                                Value::from_int(0));
    if (!buffer) return std::unexpected(buffer.error());
    auto buffer_root = machine.pin_native_root(*buffer);
    if (!buffer_root) return std::unexpected(buffer_root.error());
    auto runtime = machine.heap().class_name(reader);
    if (!runtime) return std::unexpected(runtime.error());

    std::u16string result;
    usize zero_reads = 0;
    while (result.size() < kMaximumXmlBytes) {
        const std::array<Value, 3> arguments {{
            Value::from_reference(*buffer), Value::from_int(0),
            Value::from_int(static_cast<i32>(kReadChunk)),
        }};
        auto read = invoke_checked(machine, reader, *runtime, "read", "([CII)I",
                                   arguments);
        if (!read) return std::unexpected(read.error());
        if (!read->has_value()) {
            return fail(ErrorCode::internal_error, "Reader.read returned no value");
        }
        auto count = (*read)->as_int();
        if (!count) return std::unexpected(count.error());
        if (*count < 0) return result;
        if (*count == 0) {
            if (++zero_reads > 16U) {
                return fail_java("java/io/IOException",
                                 "XML character stream made no progress");
            }
            machine.cooperative_yield();
            continue;
        }
        zero_reads = 0;
        if (static_cast<usize>(*count) > kReadChunk ||
            result.size() + static_cast<usize>(*count) > kMaximumXmlBytes) {
            return fail_java("org/xml/sax/SAXException",
                             "XML document exceeds size limit");
        }
        for (i32 index = 0; index < *count; ++index) {
            auto value = machine.heap().element(*buffer,
                                                static_cast<usize>(index));
            if (!value) return std::unexpected(value.error());
            auto character = value->as_int();
            if (!character) return std::unexpected(character.error());
            result.push_back(static_cast<char16_t>(
                static_cast<u16>(*character)));
        }
    }
    return fail_java("org/xml/sax/SAXException",
                     "XML document exceeds size limit");
}

[[nodiscard]] bool ascii_equal_ci(std::u16string_view left,
                                  std::string_view right) {
    if (left.size() != right.size()) return false;
    for (usize index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index] <= 0x7FU
            ? left[index] : 0U);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

[[nodiscard]] std::u16string decode_utf8(std::span<const u8> bytes,
                                         usize offset = 0U) {
    std::u16string result;
    result.reserve(bytes.size() - std::min(offset, bytes.size()));
    for (usize index = offset; index < bytes.size();) {
        const u32 lead = bytes[index];
        u32 code_point = 0xFFFDU;
        usize length = 1U;
        if (lead < 0x80U) {
            code_point = lead;
        } else if ((lead & 0xE0U) == 0xC0U && index + 1U < bytes.size()) {
            code_point = lead & 0x1FU;
            length = 2U;
        } else if ((lead & 0xF0U) == 0xE0U && index + 2U < bytes.size()) {
            code_point = lead & 0x0FU;
            length = 3U;
        } else if ((lead & 0xF8U) == 0xF0U && index + 3U < bytes.size()) {
            code_point = lead & 0x07U;
            length = 4U;
        }
        bool valid = length == 1U || index + length <= bytes.size();
        for (usize tail = 1U; valid && tail < length; ++tail) {
            if ((bytes[index + tail] & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            code_point = (code_point << 6U) | (bytes[index + tail] & 0x3FU);
        }
        if (!valid || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            code_point = 0xFFFDU;
            length = 1U;
        }
        if (code_point <= 0xFFFFU) {
            result.push_back(static_cast<char16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            result.push_back(static_cast<char16_t>(0xD800U +
                                                   (code_point >> 10U)));
            result.push_back(static_cast<char16_t>(0xDC00U +
                                                   (code_point & 0x3FFU)));
        }
        index += length;
    }
    return result;
}

[[nodiscard]] std::u16string decode_xml_bytes(std::span<const u8> bytes,
                                               std::u16string_view encoding) {
    if (bytes.size() >= 2U && bytes[0] == 0xFEU && bytes[1] == 0xFFU) {
        std::u16string result;
        for (usize index = 2U; index + 1U < bytes.size(); index += 2U) {
            result.push_back(static_cast<char16_t>(
                (static_cast<u16>(bytes[index]) << 8U) | bytes[index + 1U]));
        }
        return result;
    }
    if (bytes.size() >= 2U && bytes[0] == 0xFFU && bytes[1] == 0xFEU) {
        std::u16string result;
        for (usize index = 2U; index + 1U < bytes.size(); index += 2U) {
            result.push_back(static_cast<char16_t>(
                static_cast<u16>(bytes[index]) |
                (static_cast<u16>(bytes[index + 1U]) << 8U)));
        }
        return result;
    }
    if (ascii_equal_ci(encoding, "UTF-16BE")) {
        std::u16string result;
        for (usize index = 0U; index + 1U < bytes.size(); index += 2U) {
            result.push_back(static_cast<char16_t>(
                (static_cast<u16>(bytes[index]) << 8U) | bytes[index + 1U]));
        }
        return result;
    }
    if (ascii_equal_ci(encoding, "UTF-16LE") ||
        ascii_equal_ci(encoding, "UTF-16")) {
        std::u16string result;
        for (usize index = 0U; index + 1U < bytes.size(); index += 2U) {
            result.push_back(static_cast<char16_t>(
                static_cast<u16>(bytes[index]) |
                (static_cast<u16>(bytes[index + 1U]) << 8U)));
        }
        return result;
    }
    if (ascii_equal_ci(encoding, "ISO-8859-1") ||
        ascii_equal_ci(encoding, "ISO8859_1") ||
        ascii_equal_ci(encoding, "WINDOWS-1252")) {
        std::u16string result;
        result.reserve(bytes.size());
        for (const u8 byte : bytes) result.push_back(static_cast<char16_t>(byte));
        return result;
    }
    const usize offset = bytes.size() >= 3U && bytes[0] == 0xEFU &&
                         bytes[1] == 0xBBU && bytes[2] == 0xBFU ? 3U : 0U;
    return decode_utf8(bytes, offset);
}

struct AttributeData final {
    std::u16string uri;
    std::u16string local_name;
    std::u16string q_name;
    std::u16string type {u"CDATA"};
    std::u16string value;
};

[[nodiscard]] Result<ObjectRef> make_attributes(
    Machine& machine,
    const std::vector<AttributeData>& attributes) {
    auto object = allocate_instance(machine, kAttributesImpl);
    if (!object) return std::unexpected(object.error());
    auto object_root = machine.pin_native_root(*object);
    if (!object_root) return std::unexpected(object_root.error());

    const std::array<std::string_view, 5> names {{
        "uris", "localNames", "qNames", "types", "values",
    }};
    std::array<ObjectRef, 5> arrays {};
    std::vector<NativeRootScope> roots;
    roots.reserve(5U + attributes.size() * 5U);
    for (usize column = 0; column < arrays.size(); ++column) {
        auto array = allocate_string_array(machine, attributes.size());
        if (!array) return std::unexpected(array.error());
        arrays[column] = *array;
        auto root = machine.pin_native_root(*array);
        if (!root) return std::unexpected(root.error());
        roots.push_back(std::move(*root));
        auto stored = set_reference_field(machine, *object, kAttributesImpl,
                                          names[column],
                                          "[Ljava/lang/String;", *array);
        if (!stored) return std::unexpected(stored.error());
    }
    for (usize index = 0; index < attributes.size(); ++index) {
        const std::array<std::u16string, 5> values {{
            attributes[index].uri, attributes[index].local_name,
            attributes[index].q_name, attributes[index].type,
            attributes[index].value,
        }};
        for (usize column = 0; column < values.size(); ++column) {
            auto text = make_string(machine, values[column]);
            if (!text) return std::unexpected(text.error());
            auto root = machine.pin_native_root(*text);
            if (!root) return std::unexpected(root.error());
            roots.push_back(std::move(*root));
            auto stored = machine.heap().set_element(
                arrays[column], index, Value::from_reference(*text));
            if (!stored) return std::unexpected(stored.error());
        }
    }
    auto length = set_int_field(machine, *object, kAttributesImpl, "length",
                                static_cast<i32>(attributes.size()));
    if (!length) return std::unexpected(length.error());
    return *object;
}

class SaxEmitter final {
public:
    SaxEmitter(Machine& machine, ObjectRef handler, bool namespace_aware)
        : machine_(machine), handler_(handler), namespace_aware_(namespace_aware) {}

    [[nodiscard]] Status start_document() {
        return invoke_void(machine_, handler_, kDefaultHandler,
                           "startDocument", "()V");
    }

    [[nodiscard]] Status end_document() {
        return invoke_void(machine_, handler_, kDefaultHandler,
                           "endDocument", "()V");
    }

    [[nodiscard]] Status start_element(
        const std::u16string& q_name,
        const std::vector<AttributeData>& attributes) {
        auto uri = make_string(machine_, {});
        auto local = make_string(machine_, namespace_aware_
            ? local_name(q_name) : std::u16string {});
        auto qname = make_string(machine_, q_name);
        auto attrs = make_attributes(machine_, attributes);
        if (!uri || !local || !qname || !attrs) {
            if (!uri) return std::unexpected(uri.error());
            if (!local) return std::unexpected(local.error());
            if (!qname) return std::unexpected(qname.error());
            return std::unexpected(attrs.error());
        }
        auto r1 = machine_.pin_native_root(*uri);
        auto r2 = machine_.pin_native_root(*local);
        auto r3 = machine_.pin_native_root(*qname);
        auto r4 = machine_.pin_native_root(*attrs);
        if (!r1 || !r2 || !r3 || !r4) {
            return fail(ErrorCode::internal_error,
                        "failed to root SAX startElement arguments");
        }
        const std::array<Value, 4> args {{
            Value::from_reference(*uri), Value::from_reference(*local),
            Value::from_reference(*qname), Value::from_reference(*attrs),
        }};
        return invoke_void(machine_, handler_, kDefaultHandler, "startElement",
                           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lorg/xml/sax/Attributes;)V",
                           args);
    }

    [[nodiscard]] Status end_element(const std::u16string& q_name) {
        auto uri = make_string(machine_, {});
        auto local = make_string(machine_, namespace_aware_
            ? local_name(q_name) : std::u16string {});
        auto qname = make_string(machine_, q_name);
        if (!uri || !local || !qname) {
            if (!uri) return std::unexpected(uri.error());
            if (!local) return std::unexpected(local.error());
            return std::unexpected(qname.error());
        }
        auto r1 = machine_.pin_native_root(*uri);
        auto r2 = machine_.pin_native_root(*local);
        auto r3 = machine_.pin_native_root(*qname);
        if (!r1 || !r2 || !r3) {
            return fail(ErrorCode::internal_error,
                        "failed to root SAX endElement arguments");
        }
        const std::array<Value, 3> args {{
            Value::from_reference(*uri), Value::from_reference(*local),
            Value::from_reference(*qname),
        }};
        return invoke_void(machine_, handler_, kDefaultHandler, "endElement",
                           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                           args);
    }

    [[nodiscard]] Status characters(std::u16string_view text) {
        if (text.empty()) return {};
        auto array = make_char_array(machine_, text);
        if (!array) return std::unexpected(array.error());
        auto root = machine_.pin_native_root(*array);
        if (!root) return std::unexpected(root.error());
        const std::array<Value, 3> args {{
            Value::from_reference(*array), Value::from_int(0),
            Value::from_int(static_cast<i32>(text.size())),
        }};
        return invoke_void(machine_, handler_, kDefaultHandler, "characters",
                           "([CII)V", args);
    }

    [[nodiscard]] Status processing_instruction(
        const std::u16string& target,
        const std::u16string& data) {
        auto target_string = make_string(machine_, target);
        auto data_string = make_string(machine_, data);
        if (!target_string || !data_string) {
            if (!target_string) return std::unexpected(target_string.error());
            return std::unexpected(data_string.error());
        }
        auto r1 = machine_.pin_native_root(*target_string);
        auto r2 = machine_.pin_native_root(*data_string);
        if (!r1 || !r2) {
            return fail(ErrorCode::internal_error,
                        "failed to root SAX processing instruction");
        }
        const std::array<Value, 2> args {{
            Value::from_reference(*target_string),
            Value::from_reference(*data_string),
        }};
        return invoke_void(machine_, handler_, kDefaultHandler,
                           "processingInstruction",
                           "(Ljava/lang/String;Ljava/lang/String;)V", args);
    }

private:
    [[nodiscard]] static std::u16string local_name(
        const std::u16string& q_name) {
        const auto separator = q_name.find(u':');
        return separator == std::u16string::npos
            ? q_name : q_name.substr(separator + 1U);
    }

    Machine& machine_;
    ObjectRef handler_ {};
    bool namespace_aware_ {false};
};

class SimpleXmlParser final {
public:
    SimpleXmlParser(std::u16string_view text, SaxEmitter& emitter)
        : text_(text), emitter_(emitter) {}

    [[nodiscard]] Status parse() {
        auto started = emitter_.start_document();
        if (!started) return started;
        skip_bom();
        usize root_count = 0U;
        while (position_ < text_.size()) {
            if (text_[position_] != u'<') {
                auto content = parse_text();
                if (!content) return std::unexpected(content.error());
                auto emitted = emitter_.characters(*content);
                if (!emitted) return emitted;
                continue;
            }
            if (starts_with(u"<!--")) {
                auto skipped = skip_until(u"-->");
                if (!skipped) return skipped;
            } else if (starts_with(u"<![CDATA[")) {
                position_ += 9U;
                const auto end = text_.find(u"]]>", position_);
                if (end == std::u16string_view::npos) return malformed("unterminated CDATA");
                auto emitted = emitter_.characters(text_.substr(position_, end - position_));
                if (!emitted) return emitted;
                position_ = end + 3U;
            } else if (starts_with(u"<?")) {
                auto pi = parse_processing_instruction();
                if (!pi) return pi;
            } else if (starts_with(u"<!DOCTYPE") || starts_with(u"<!doctype")) {
                auto skipped = skip_doctype();
                if (!skipped) return skipped;
            } else if (starts_with(u"</")) {
                auto closed = parse_end_element();
                if (!closed) return closed;
            } else if (starts_with(u"<!")) {
                auto skipped = skip_until(u">");
                if (!skipped) return skipped;
            } else {
                auto opened = parse_start_element();
                if (!opened) return std::unexpected(opened.error());
                if (*opened) ++root_count;
            }
        }
        if (!elements_.empty()) return malformed("unclosed XML element");
        if (root_count == 0U) return malformed("XML document has no root element");
        return emitter_.end_document();
    }

private:
    [[nodiscard]] bool starts_with(std::u16string_view token) const {
        return text_.substr(position_, token.size()) == token;
    }

    void skip_bom() {
        if (!text_.empty() && text_.front() == 0xFEFFU) ++position_;
    }

    void skip_space() {
        while (position_ < text_.size()) {
            const char16_t value = text_[position_];
            if (value != u' ' && value != u'\t' && value != u'\r' &&
                value != u'\n') break;
            ++position_;
        }
    }

    [[nodiscard]] static bool name_character(char16_t value) {
        return (value >= u'a' && value <= u'z') ||
               (value >= u'A' && value <= u'Z') ||
               (value >= u'0' && value <= u'9') || value == u'_' ||
               value == u'-' || value == u'.' || value == u':' ||
               value >= 0x80U;
    }

    [[nodiscard]] Result<std::u16string> parse_name() {
        const usize start = position_;
        while (position_ < text_.size() && name_character(text_[position_])) {
            ++position_;
        }
        if (position_ == start) {
            return fail_java("org/xml/sax/SAXParseException",
                             "expected XML name");
        }
        return std::u16string(text_.substr(start, position_ - start));
    }

    [[nodiscard]] static Result<std::u16string> decode_entities(
        std::u16string_view value) {
        std::u16string result;
        result.reserve(value.size());
        for (usize index = 0; index < value.size();) {
            if (value[index] != u'&') {
                result.push_back(value[index++]);
                continue;
            }
            const usize semicolon = value.find(u';', index + 1U);
            if (semicolon == std::u16string_view::npos) {
                return fail_java("org/xml/sax/SAXParseException",
                                 "unterminated XML entity");
            }
            const auto entity = value.substr(index + 1U,
                                              semicolon - index - 1U);
            if (entity == u"amp") result.push_back(u'&');
            else if (entity == u"lt") result.push_back(u'<');
            else if (entity == u"gt") result.push_back(u'>');
            else if (entity == u"apos") result.push_back(u'\'');
            else if (entity == u"quot") result.push_back(u'"');
            else if (!entity.empty() && entity.front() == u'#') {
                const bool hexadecimal = entity.size() > 1U &&
                    (entity[1] == u'x' || entity[1] == u'X');
                u32 code_point = 0U;
                const usize first = hexadecimal ? 2U : 1U;
                if (first >= entity.size()) {
                    return fail_java("org/xml/sax/SAXParseException",
                                     "empty numeric XML entity");
                }
                for (usize digit_index = first; digit_index < entity.size();
                     ++digit_index) {
                    const char16_t digit = entity[digit_index];
                    u32 value_digit = 0U;
                    if (digit >= u'0' && digit <= u'9') value_digit = digit - u'0';
                    else if (hexadecimal && digit >= u'a' && digit <= u'f') value_digit = 10U + digit - u'a';
                    else if (hexadecimal && digit >= u'A' && digit <= u'F') value_digit = 10U + digit - u'A';
                    else return fail_java("org/xml/sax/SAXParseException",
                                          "invalid numeric XML entity");
                    const u32 base = hexadecimal ? 16U : 10U;
                    if (code_point > (0x10FFFFU - value_digit) / base) {
                        return fail_java("org/xml/sax/SAXParseException",
                                         "numeric XML entity is out of range");
                    }
                    code_point = code_point * base + value_digit;
                }
                if (code_point == 0U || code_point > 0x10FFFFU ||
                    (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
                    return fail_java("org/xml/sax/SAXParseException",
                                     "numeric XML entity is invalid");
                }
                if (code_point <= 0xFFFFU) {
                    result.push_back(static_cast<char16_t>(code_point));
                } else {
                    code_point -= 0x10000U;
                    result.push_back(static_cast<char16_t>(0xD800U +
                                                           (code_point >> 10U)));
                    result.push_back(static_cast<char16_t>(0xDC00U +
                                                           (code_point & 0x3FFU)));
                }
            } else {
                return fail_java("org/xml/sax/SAXParseException",
                                 "unknown XML entity");
            }
            index = semicolon + 1U;
        }
        return result;
    }

    [[nodiscard]] Result<std::u16string> parse_text() {
        const usize start = position_;
        const usize end = text_.find(u'<', start);
        position_ = end == std::u16string_view::npos ? text_.size() : end;
        return decode_entities(text_.substr(start, position_ - start));
    }

    [[nodiscard]] Status skip_until(std::u16string_view terminal) {
        const usize end = text_.find(terminal, position_ + terminal.size());
        if (end == std::u16string_view::npos) return malformed("unterminated XML declaration");
        position_ = end + terminal.size();
        return {};
    }

    [[nodiscard]] Status skip_doctype() {
        usize depth = 0U;
        char16_t quote = 0;
        for (; position_ < text_.size(); ++position_) {
            const char16_t value = text_[position_];
            if (quote != 0) {
                if (value == quote) quote = 0;
                continue;
            }
            if (value == u'\'' || value == u'"') quote = value;
            else if (value == u'[') ++depth;
            else if (value == u']' && depth > 0U) --depth;
            else if (value == u'>' && depth == 0U) {
                ++position_;
                return {};
            }
        }
        return malformed("unterminated DOCTYPE");
    }

    [[nodiscard]] Status parse_processing_instruction() {
        position_ += 2U;
        auto target = parse_name();
        if (!target) return std::unexpected(target.error());
        const usize end = text_.find(u"?>", position_);
        if (end == std::u16string_view::npos) return malformed("unterminated processing instruction");
        skip_space();
        std::u16string data(text_.substr(position_, end - position_));
        while (!data.empty() && (data.back() == u' ' || data.back() == u'\t' ||
                                 data.back() == u'\r' || data.back() == u'\n')) {
            data.pop_back();
        }
        position_ = end + 2U;
        if (*target == u"xml" || *target == u"XML") return {};
        return emitter_.processing_instruction(*target, data);
    }

    [[nodiscard]] Result<bool> parse_start_element() {
        ++position_;
        auto name = parse_name();
        if (!name) return std::unexpected(name.error());
        std::vector<AttributeData> attributes;
        bool self_closing = false;
        while (position_ < text_.size()) {
            skip_space();
            if (starts_with(u"/>")) {
                position_ += 2U;
                self_closing = true;
                break;
            }
            if (position_ < text_.size() && text_[position_] == u'>') {
                ++position_;
                break;
            }
            auto attribute_name = parse_name();
            if (!attribute_name) return std::unexpected(attribute_name.error());
            skip_space();
            if (position_ >= text_.size() || text_[position_] != u'=') {
                return std::unexpected(malformed_error("attribute lacks '='"));
            }
            ++position_;
            skip_space();
            if (position_ >= text_.size() ||
                (text_[position_] != u'\'' && text_[position_] != u'"')) {
                return std::unexpected(malformed_error("attribute value is not quoted"));
            }
            const char16_t quote = text_[position_++];
            const usize end = text_.find(quote, position_);
            if (end == std::u16string_view::npos) {
                return std::unexpected(malformed_error("unterminated attribute value"));
            }
            auto value = decode_entities(text_.substr(position_, end - position_));
            if (!value) return std::unexpected(value.error());
            position_ = end + 1U;
            const auto separator = attribute_name->find(u':');
            attributes.push_back(AttributeData {
                .uri = {},
                .local_name = separator == std::u16string::npos
                    ? *attribute_name : attribute_name->substr(separator + 1U),
                .q_name = std::move(*attribute_name),
                .type = u"CDATA",
                .value = std::move(*value),
            });
        }
        if (position_ > text_.size()) return std::unexpected(malformed_error("unterminated start tag"));
        auto emitted = emitter_.start_element(*name, attributes);
        if (!emitted) return std::unexpected(emitted.error());
        if (self_closing) {
            auto closed = emitter_.end_element(*name);
            if (!closed) return std::unexpected(closed.error());
        } else {
            elements_.push_back(*name);
        }
        return elements_.size() == 1U || (self_closing && elements_.empty());
    }

    [[nodiscard]] Status parse_end_element() {
        position_ += 2U;
        skip_space();
        auto name = parse_name();
        if (!name) return std::unexpected(name.error());
        skip_space();
        if (position_ >= text_.size() || text_[position_] != u'>') {
            return malformed("unterminated end tag");
        }
        ++position_;
        if (elements_.empty() || elements_.back() != *name) {
            return malformed("mismatched XML end tag");
        }
        elements_.pop_back();
        return emitter_.end_element(*name);
    }

    [[nodiscard]] Error malformed_error(std::string message) const {
        return Error::make_java("org/xml/sax/SAXParseException",
                                std::move(message));
    }

    [[nodiscard]] Status malformed(std::string message) const {
        return std::unexpected(malformed_error(std::move(message)));
    }

    std::u16string_view text_;
    SaxEmitter& emitter_;
    usize position_ {0U};
    std::vector<std::u16string> elements_;
};

[[nodiscard]] Result<ObjectRef> open_system_id(Machine& machine,
                                               ObjectRef system_id) {
    const std::array<Value, 1> arguments {{Value::from_reference(system_id)}};
    auto opened = machine.invoke_static("javax/microedition/io/Connector",
                                        "openInputStream",
                                        "(Ljava/lang/String;)Ljava/io/InputStream;",
                                        arguments);
    if (!opened) return std::unexpected(opened.error());
    if (opened->throwable.has_value()) {
        auto thrown = machine.heap().class_name(*opened->throwable);
        if (!thrown) return std::unexpected(thrown.error());
        return fail_java(*thrown, "failed to open XML system identifier");
    }
    if (!opened->return_value.has_value()) {
        return fail(ErrorCode::internal_error,
                    "Connector.openInputStream returned no stream");
    }
    return opened->return_value->as_reference();
}

[[nodiscard]] Result<std::u16string> load_input_source(Machine& machine,
                                                       ObjectRef input) {
    auto character_stream = reference_field(machine, input, kInputSource,
                                             "characterStream",
                                             "Ljava/io/Reader;");
    if (!character_stream) return std::unexpected(character_stream.error());
    if (!character_stream->is_null()) {
        return read_character_stream(machine, *character_stream);
    }
    auto byte_stream = reference_field(machine, input, kInputSource,
                                        "byteStream",
                                        "Ljava/io/InputStream;");
    if (!byte_stream) return std::unexpected(byte_stream.error());
    ObjectRef stream = *byte_stream;
    auto stream_root = machine.pin_native_root(stream);
    if (!stream_root) return std::unexpected(stream_root.error());
    if (stream.is_null()) {
        auto system_id = reference_field(machine, input, kInputSource,
                                          "systemId", "Ljava/lang/String;");
        if (!system_id) return std::unexpected(system_id.error());
        if (system_id->is_null()) {
            return fail_java("org/xml/sax/SAXException",
                             "InputSource has no byte, character, or system stream");
        }
        auto opened = open_system_id(machine, *system_id);
        if (!opened) return std::unexpected(opened.error());
        stream = *opened;
        auto reset = stream_root->reset(stream);
        if (!reset) return std::unexpected(reset.error());
    }
    auto bytes = read_input_stream(machine, stream);
    if (!bytes) return std::unexpected(bytes.error());
    auto encoding_ref = reference_field(machine, input, kInputSource,
                                         "encoding", "Ljava/lang/String;");
    if (!encoding_ref) return std::unexpected(encoding_ref.error());
    std::u16string encoding;
    if (!encoding_ref->is_null()) {
        auto value = machine.heap().string_value(*encoding_ref);
        if (!value) return std::unexpected(value.error());
        encoding = std::move(*value);
    }
    return decode_xml_bytes(*bytes, encoding);
}

[[nodiscard]] Status parse_input_source(Machine& machine,
                                        ObjectRef parser,
                                        ObjectRef input,
                                        ObjectRef handler) {
    if (input.is_null() || handler.is_null()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "SAX parse input and handler must be non-null");
    }
    auto parser_root = machine.pin_native_root(parser);
    auto input_root = machine.pin_native_root(input);
    auto handler_root = machine.pin_native_root(handler);
    if (!parser_root || !input_root || !handler_root) {
        return fail(ErrorCode::internal_error,
                    "failed to root SAX parser state");
    }
    auto xml = load_input_source(machine, input);
    if (!xml) return std::unexpected(xml.error());
    auto namespace_aware = int_field(machine, parser, kParserImpl,
                                     "namespaceAware");
    if (!namespace_aware) return std::unexpected(namespace_aware.error());
    SaxEmitter emitter(machine, handler, *namespace_aware != 0);
    SimpleXmlParser xml_parser(*xml, emitter);
    return xml_parser.parse();
}

[[nodiscard]] Result<ObjectRef> make_input_source(Machine& machine,
                                                  ObjectRef byte_stream,
                                                  ObjectRef system_id = {}) {
    auto input = allocate_instance(machine, kInputSource);
    if (!input) return std::unexpected(input.error());
    auto root = machine.pin_native_root(*input);
    if (!root) return std::unexpected(root.error());
    if (!byte_stream.is_null()) {
        auto stored = set_reference_field(machine, *input, kInputSource,
                                          "byteStream",
                                          "Ljava/io/InputStream;", byte_stream);
        if (!stored) return std::unexpected(stored.error());
    }
    if (!system_id.is_null()) {
        auto stored = set_reference_field(machine, *input, kInputSource,
                                          "systemId", "Ljava/lang/String;",
                                          system_id);
        if (!stored) return std::unexpected(stored.error());
    }
    return *input;
}

[[nodiscard]] Result<ObjectRef> create_parser(Machine& machine,
                                              bool namespace_aware,
                                              bool validating) {
    auto parser = allocate_instance(machine, kParserImpl);
    if (!parser) return std::unexpected(parser.error());
    auto root = machine.pin_native_root(*parser);
    if (!root) return std::unexpected(root.error());
    auto namespace_stored = set_int_field(machine, *parser, kParserImpl,
                                          "namespaceAware",
                                          namespace_aware ? 1 : 0);
    auto validating_stored = set_int_field(machine, *parser, kParserImpl,
                                           "validating",
                                           validating ? 1 : 0);
    if (!namespace_stored) return std::unexpected(namespace_stored.error());
    if (!validating_stored) return std::unexpected(validating_stored.error());
    return *parser;
}

[[nodiscard]] Result<bool> feature_value(Machine& machine,
                                         ObjectRef name,
                                         bool namespace_aware,
                                         bool validating) {
    auto text = string_value(machine, name);
    if (!text) return std::unexpected(text.error());
    if (*text == u"http://xml.org/sax/features/namespaces") {
        return namespace_aware;
    }
    if (*text == u"http://xml.org/sax/features/namespace-prefixes") {
        return !namespace_aware;
    }
    if (*text == u"http://xml.org/sax/features/validation") {
        return validating;
    }
    return fail_java("org/xml/sax/SAXNotRecognizedException",
                     "SAX feature is not recognized");
}

void register_input_source_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kInputSource), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    const std::array<std::pair<std::string_view, std::string_view>, 3> constructors {{
        {"Ljava/lang/String;", "systemId"},
        {"Ljava/io/InputStream;", "byteStream"},
        {"Ljava/io/Reader;", "characterStream"},
    }};
    for (const auto& [descriptor, field] : constructors) {
        add(registry, std::string(kInputSource), "<init>",
            "(" + std::string(descriptor) + ")V",
            [field, descriptor](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = reference_argument(arguments, 1U);
                if (!object || !value) {
                    if (!object) return std::unexpected(object.error());
                    return std::unexpected(value.error());
                }
                auto stored = set_reference_field(machine, *object, kInputSource,
                                                  field, descriptor, *value);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    }
    const std::array<std::tuple<std::string_view, std::string_view,
                                std::string_view>, 5> properties {{
        {"PublicId", "publicId", "Ljava/lang/String;"},
        {"SystemId", "systemId", "Ljava/lang/String;"},
        {"ByteStream", "byteStream", "Ljava/io/InputStream;"},
        {"Encoding", "encoding", "Ljava/lang/String;"},
        {"CharacterStream", "characterStream", "Ljava/io/Reader;"},
    }};
    for (const auto& [suffix, field, descriptor] : properties) {
        add(registry, std::string(kInputSource),
            "set" + std::string(suffix),
            "(" + std::string(descriptor) + ")V",
            [field, descriptor](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = reference_argument(arguments, 1U);
                if (!object || !value) {
                    if (!object) return std::unexpected(object.error());
                    return std::unexpected(value.error());
                }
                auto stored = set_reference_field(machine, *object, kInputSource,
                                                  field, descriptor, *value);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
        add(registry, std::string(kInputSource),
            "get" + std::string(suffix),
            "()" + std::string(descriptor),
            [field, descriptor](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto value = reference_field(machine, *object, kInputSource,
                                             field, descriptor);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_reference(*value));
            });
    }
}

void register_default_handler_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kDefaultHandler), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kDefaultHandler), "resolveEntity",
        "(Ljava/lang/String;Ljava/lang/String;)Lorg/xml/sax/InputSource;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_reference({}));
        });
    const std::array<std::pair<std::string_view, std::string_view>, 15> methods {{
        {"notationDecl", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"},
        {"unparsedEntityDecl", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"},
        {"setDocumentLocator", "(Lorg/xml/sax/Locator;)V"},
        {"startDocument", "()V"}, {"endDocument", "()V"},
        {"startPrefixMapping", "(Ljava/lang/String;Ljava/lang/String;)V"},
        {"endPrefixMapping", "(Ljava/lang/String;)V"},
        {"startElement", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lorg/xml/sax/Attributes;)V"},
        {"endElement", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"},
        {"characters", "([CII)V"}, {"ignorableWhitespace", "([CII)V"},
        {"processingInstruction", "(Ljava/lang/String;Ljava/lang/String;)V"},
        {"skippedEntity", "(Ljava/lang/String;)V"},
        {"warning", "(Lorg/xml/sax/SAXParseException;)V"},
        {"error", "(Lorg/xml/sax/SAXParseException;)V"},
    }};
    for (const auto& [name, descriptor] : methods) {
        add(registry, std::string(kDefaultHandler), std::string(name),
            std::string(descriptor),
            [](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                return std::optional<Value> {};
            });
    }
    add(registry, std::string(kDefaultHandler), "fatalError",
        "(Lorg/xml/sax/SAXParseException;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return fail_java("org/xml/sax/SAXParseException",
                             "fatal SAX parsing error");
        });
}

void register_factory_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kFactory), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kFactory), "newInstance",
        "()Ljavax/xml/parsers/SAXParserFactory;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto factory = allocate_instance(machine, kFactoryImpl);
            if (!factory) return std::unexpected(factory.error());
            return std::optional<Value>(Value::from_reference(*factory));
        });
    for (const auto& [method_name, field_name] : {
             std::pair<std::string_view, std::string_view>{"NamespaceAware", "namespaceAware"},
             {"Validating", "validating"}}) {
        add(registry, std::string(kFactory), "set" + std::string(method_name),
            "(Z)V",
            [field_name](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = int_argument(arguments, 1U);
                if (!object || !value) {
                    if (!object) return std::unexpected(object.error());
                    return std::unexpected(value.error());
                }
                auto stored = set_int_field(machine, *object, kFactory,
                                            field_name, *value != 0 ? 1 : 0);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
        add(registry, std::string(kFactory), "is" + std::string(method_name),
            "()Z",
            [field_name](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto value = int_field(machine, *object, kFactory, field_name);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value != 0 ? 1 : 0));
            });
    }
    add(registry, std::string(kFactoryImpl), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kFactoryImpl), "newSAXParser",
        "()Ljavax/xml/parsers/SAXParser;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto factory = receiver(arguments);
            if (!factory) return std::unexpected(factory.error());
            auto namespaces = int_field(machine, *factory, kFactory,
                                        "namespaceAware");
            auto validating = int_field(machine, *factory, kFactory,
                                        "validating");
            if (!namespaces || !validating) {
                if (!namespaces) return std::unexpected(namespaces.error());
                return std::unexpected(validating.error());
            }
            if (*validating != 0) {
                return fail_java("javax/xml/parsers/ParserConfigurationException",
                                 "validating XML parsers are not supported");
            }
            auto parser = create_parser(machine, *namespaces != 0, false);
            if (!parser) return std::unexpected(parser.error());
            return std::optional<Value>(Value::from_reference(*parser));
        });
    add(registry, std::string(kFactoryImpl), "getFeature",
        "(Ljava/lang/String;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto factory = receiver(arguments);
            auto name = reference_argument(arguments, 1U, false);
            if (!factory || !name) {
                if (!factory) return std::unexpected(factory.error());
                return std::unexpected(name.error());
            }
            auto namespaces = int_field(machine, *factory, kFactory,
                                        "namespaceAware");
            auto validating = int_field(machine, *factory, kFactory,
                                        "validating");
            if (!namespaces || !validating) {
                if (!namespaces) return std::unexpected(namespaces.error());
                return std::unexpected(validating.error());
            }
            auto value = feature_value(machine, *name, *namespaces != 0,
                                       *validating != 0);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value ? 1 : 0));
        });
    add(registry, std::string(kFactoryImpl), "setFeature",
        "(Ljava/lang/String;Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto factory = receiver(arguments);
            auto name = reference_argument(arguments, 1U, false);
            auto enabled = int_argument(arguments, 2U);
            if (!factory || !name || !enabled) {
                if (!factory) return std::unexpected(factory.error());
                if (!name) return std::unexpected(name.error());
                return std::unexpected(enabled.error());
            }
            auto text = string_value(machine, *name);
            if (!text) return std::unexpected(text.error());
            if (*text == u"http://xml.org/sax/features/namespaces") {
                auto stored = set_int_field(machine, *factory, kFactory,
                                            "namespaceAware",
                                            *enabled != 0 ? 1 : 0);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            }
            if (*text == u"http://xml.org/sax/features/validation") {
                if (*enabled != 0) {
                    return fail_java("org/xml/sax/SAXNotSupportedException",
                                     "XML validation is not supported");
                }
                return std::optional<Value> {};
            }
            return fail_java("org/xml/sax/SAXNotRecognizedException",
                             "SAX feature is not recognized");
        });
}

void register_parser_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kParser), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParserImpl), "<init>", "(ZZ)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto namespaces = int_argument(arguments, 1U);
            auto validating = int_argument(arguments, 2U);
            if (!parser || !namespaces || !validating) {
                if (!parser) return std::unexpected(parser.error());
                if (!namespaces) return std::unexpected(namespaces.error());
                return std::unexpected(validating.error());
            }
            auto a = set_int_field(machine, *parser, kParserImpl,
                                   "namespaceAware", *namespaces != 0 ? 1 : 0);
            auto b = set_int_field(machine, *parser, kParserImpl,
                                   "validating", *validating != 0 ? 1 : 0);
            if (!a) return std::unexpected(a.error());
            if (!b) return std::unexpected(b.error());
            return std::optional<Value> {};
        });
    for (const std::string_view name : {"getParser", "getXMLReader"}) {
        const std::string descriptor = name == "getParser"
            ? "()Lorg/xml/sax/Parser;" : "()Lorg/xml/sax/XMLReader;";
        add(registry, std::string(kParserImpl), std::string(name), descriptor,
            [](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto parser = receiver(arguments);
                if (!parser) return std::unexpected(parser.error());
                return std::optional<Value>(Value::from_reference(*parser));
            });
    }
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"isNamespaceAware", "namespaceAware"},
             {"isValidating", "validating"}}) {
        add(registry, std::string(kParserImpl), std::string(method), "()Z",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto parser = receiver(arguments);
                if (!parser) return std::unexpected(parser.error());
                auto value = int_field(machine, *parser, kParserImpl, field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value != 0 ? 1 : 0));
            });
    }

    add(registry, std::string(kParser), "parse",
        "(Lorg/xml/sax/InputSource;Lorg/xml/sax/helpers/DefaultHandler;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto input = reference_argument(arguments, 1U, false);
            auto handler = reference_argument(arguments, 2U, false);
            if (!parser || !input || !handler) {
                if (!parser) return std::unexpected(parser.error());
                if (!input) return std::unexpected(input.error());
                return std::unexpected(handler.error());
            }
            auto parsed = parse_input_source(machine, *parser, *input, *handler);
            if (!parsed) return std::unexpected(parsed.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParser), "parse",
        "(Ljava/io/InputStream;Lorg/xml/sax/helpers/DefaultHandler;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto stream = reference_argument(arguments, 1U, false);
            auto handler = reference_argument(arguments, 2U, false);
            if (!parser || !stream || !handler) {
                if (!parser) return std::unexpected(parser.error());
                if (!stream) return std::unexpected(stream.error());
                return std::unexpected(handler.error());
            }
            auto input = make_input_source(machine, *stream);
            if (!input) return std::unexpected(input.error());
            auto parsed = parse_input_source(machine, *parser, *input, *handler);
            if (!parsed) return std::unexpected(parsed.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParser), "parse",
        "(Ljava/io/InputStream;Lorg/xml/sax/helpers/DefaultHandler;Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto stream = reference_argument(arguments, 1U, false);
            auto handler = reference_argument(arguments, 2U, false);
            auto system = reference_argument(arguments, 3U);
            if (!parser || !stream || !handler || !system) {
                if (!parser) return std::unexpected(parser.error());
                if (!stream) return std::unexpected(stream.error());
                if (!handler) return std::unexpected(handler.error());
                return std::unexpected(system.error());
            }
            auto input = make_input_source(machine, *stream, *system);
            if (!input) return std::unexpected(input.error());
            auto parsed = parse_input_source(machine, *parser, *input, *handler);
            if (!parsed) return std::unexpected(parsed.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParser), "parse",
        "(Ljava/lang/String;Lorg/xml/sax/helpers/DefaultHandler;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto system = reference_argument(arguments, 1U, false);
            auto handler = reference_argument(arguments, 2U, false);
            if (!parser || !system || !handler) {
                if (!parser) return std::unexpected(parser.error());
                if (!system) return std::unexpected(system.error());
                return std::unexpected(handler.error());
            }
            auto input = make_input_source(machine, {}, *system);
            if (!input) return std::unexpected(input.error());
            auto parsed = parse_input_source(machine, *parser, *input, *handler);
            if (!parsed) return std::unexpected(parsed.error());
            return std::optional<Value> {};
        });

    const std::array<std::tuple<std::string_view, std::string_view,
                                std::string_view>, 4> handlers {{
        {"EntityResolver", "entityResolver", "Lorg/xml/sax/EntityResolver;"},
        {"DTDHandler", "dtdHandler", "Lorg/xml/sax/DTDHandler;"},
        {"ContentHandler", "contentHandler", "Lorg/xml/sax/ContentHandler;"},
        {"ErrorHandler", "errorHandler", "Lorg/xml/sax/ErrorHandler;"},
    }};
    for (const auto& [suffix, field, descriptor] : handlers) {
        add(registry, std::string(kParserImpl), "set" + std::string(suffix),
            "(" + std::string(descriptor) + ")V",
            [field, descriptor](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto parser = receiver(arguments);
                auto value = reference_argument(arguments, 1U);
                if (!parser || !value) {
                    if (!parser) return std::unexpected(parser.error());
                    return std::unexpected(value.error());
                }
                auto stored = set_reference_field(machine, *parser, kParserImpl,
                                                  field, descriptor, *value);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
        add(registry, std::string(kParserImpl), "get" + std::string(suffix),
            "()" + std::string(descriptor),
            [field, descriptor](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto parser = receiver(arguments);
                if (!parser) return std::unexpected(parser.error());
                auto value = reference_field(machine, *parser, kParserImpl,
                                             field, descriptor);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_reference(*value));
            });
    }
    add(registry, std::string(kParserImpl), "setDocumentHandler",
        "(Lorg/xml/sax/DocumentHandler;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto value = reference_argument(arguments, 1U);
            if (!parser || !value) {
                if (!parser) return std::unexpected(parser.error());
                return std::unexpected(value.error());
            }
            auto stored = set_reference_field(machine, *parser, kParserImpl,
                                              "documentHandler",
                                              "Lorg/xml/sax/DocumentHandler;",
                                              *value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParserImpl), "setLocale", "(Ljava/util/Locale;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            if (!parser) return std::unexpected(parser.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParserImpl), "parse",
        "(Lorg/xml/sax/InputSource;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto input = reference_argument(arguments, 1U, false);
            if (!parser || !input) {
                if (!parser) return std::unexpected(parser.error());
                return std::unexpected(input.error());
            }
            auto handler = reference_field(machine, *parser, kParserImpl,
                                           "contentHandler",
                                           "Lorg/xml/sax/ContentHandler;");
            if (!handler) return std::unexpected(handler.error());
            if (handler->is_null()) {
                return fail_java("org/xml/sax/SAXException",
                                 "XMLReader has no ContentHandler");
            }
            auto parsed = parse_input_source(machine, *parser, *input, *handler);
            if (!parsed) return std::unexpected(parsed.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParserImpl), "parse", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto system = reference_argument(arguments, 1U, false);
            if (!parser || !system) {
                if (!parser) return std::unexpected(parser.error());
                return std::unexpected(system.error());
            }
            auto input = make_input_source(machine, {}, *system);
            if (!input) return std::unexpected(input.error());
            auto handler = reference_field(machine, *parser, kParserImpl,
                                           "contentHandler",
                                           "Lorg/xml/sax/ContentHandler;");
            if (!handler) return std::unexpected(handler.error());
            if (handler->is_null()) {
                return fail_java("org/xml/sax/SAXException",
                                 "XMLReader has no ContentHandler");
            }
            auto parsed = parse_input_source(machine, *parser, *input, *handler);
            if (!parsed) return std::unexpected(parsed.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kParserImpl), "getFeature",
        "(Ljava/lang/String;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto name = reference_argument(arguments, 1U, false);
            if (!parser || !name) {
                if (!parser) return std::unexpected(parser.error());
                return std::unexpected(name.error());
            }
            auto namespaces = int_field(machine, *parser, kParserImpl,
                                        "namespaceAware");
            auto validating = int_field(machine, *parser, kParserImpl,
                                        "validating");
            if (!namespaces || !validating) {
                if (!namespaces) return std::unexpected(namespaces.error());
                return std::unexpected(validating.error());
            }
            auto value = feature_value(machine, *name, *namespaces != 0,
                                       *validating != 0);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value ? 1 : 0));
        });
    add(registry, std::string(kParserImpl), "setFeature",
        "(Ljava/lang/String;Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto parser = receiver(arguments);
            auto name = reference_argument(arguments, 1U, false);
            auto enabled = int_argument(arguments, 2U);
            if (!parser || !name || !enabled) {
                if (!parser) return std::unexpected(parser.error());
                if (!name) return std::unexpected(name.error());
                return std::unexpected(enabled.error());
            }
            auto text = string_value(machine, *name);
            if (!text) return std::unexpected(text.error());
            if (*text == u"http://xml.org/sax/features/namespaces") {
                auto stored = set_int_field(machine, *parser, kParserImpl,
                                            "namespaceAware",
                                            *enabled != 0 ? 1 : 0);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            }
            if (*text == u"http://xml.org/sax/features/validation") {
                if (*enabled != 0) {
                    return fail_java("org/xml/sax/SAXNotSupportedException",
                                     "XML validation is not supported");
                }
                return std::optional<Value> {};
            }
            return fail_java("org/xml/sax/SAXNotRecognizedException",
                             "SAX feature is not recognized");
        });
    for (const std::string_view method : {"setProperty", "getProperty"}) {
        const std::string descriptor = method == "setProperty"
            ? "(Ljava/lang/String;Ljava/lang/Object;)V"
            : "(Ljava/lang/String;)Ljava/lang/Object;";
        add(registry, std::string(kParserImpl), std::string(method), descriptor,
            [](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto parser = receiver(arguments);
                if (!parser) return std::unexpected(parser.error());
                return fail_java("org/xml/sax/SAXNotRecognizedException",
                                 "SAX property is not recognized");
            });
    }
}

[[nodiscard]] Result<ObjectRef> attributes_array(Machine& machine,
                                                 ObjectRef attributes,
                                                 std::string_view field) {
    return reference_field(machine, attributes, kAttributesImpl, field,
                           "[Ljava/lang/String;");
}

[[nodiscard]] Result<ObjectRef> attribute_at(Machine& machine,
                                             ObjectRef attributes,
                                             std::string_view field,
                                             i32 index) {
    auto length = int_field(machine, attributes, kAttributesImpl, "length");
    if (!length) return std::unexpected(length.error());
    if (index < 0 || index >= *length) return ObjectRef {};
    auto array = attributes_array(machine, attributes, field);
    if (!array) return std::unexpected(array.error());
    auto value = machine.heap().element(*array, static_cast<usize>(index));
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

void register_attributes_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kAttributesImpl), "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            for (const std::string_view field : {"uris", "localNames", "qNames",
                                                 "types", "values"}) {
                auto array = allocate_string_array(machine, 0U);
                if (!array) return std::unexpected(array.error());
                auto stored = set_reference_field(machine, *object,
                                                  kAttributesImpl, field,
                                                  "[Ljava/lang/String;", *array);
                if (!stored) return std::unexpected(stored.error());
            }
            auto stored = set_int_field(machine, *object, kAttributesImpl,
                                        "length", 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kAttributesImpl), "getLength", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAttributesImpl, "length");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"getURI", "uris"},
             {"getLocalName", "localNames"}, {"getQName", "qNames"},
             {"getType", "types"}, {"getValue", "values"}}) {
        add(registry, std::string(kAttributesImpl), std::string(method),
            "(I)Ljava/lang/String;",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto index = int_argument(arguments, 1U);
                if (!object || !index) {
                    if (!object) return std::unexpected(object.error());
                    return std::unexpected(index.error());
                }
                auto value = attribute_at(machine, *object, field, *index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_reference(*value));
            });
    }
    auto find_index = [](Machine& machine, ObjectRef object,
                         std::string_view field, ObjectRef needle)
        -> Result<i32> {
        if (needle.is_null()) return -1;
        auto expected = machine.heap().string_value(needle);
        if (!expected) return std::unexpected(expected.error());
        auto length = int_field(machine, object, kAttributesImpl, "length");
        if (!length) return std::unexpected(length.error());
        for (i32 index = 0; index < *length; ++index) {
            auto candidate = attribute_at(machine, object, field, index);
            if (!candidate) return std::unexpected(candidate.error());
            if (candidate->is_null()) continue;
            auto text = machine.heap().string_value(*candidate);
            if (!text) return std::unexpected(text.error());
            if (*text == *expected) return index;
        }
        return -1;
    };
    add(registry, std::string(kAttributesImpl), "getIndex",
        "(Ljava/lang/String;)I",
        [find_index](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto name = reference_argument(arguments, 1U);
            if (!object || !name) {
                if (!object) return std::unexpected(object.error());
                return std::unexpected(name.error());
            }
            auto index = find_index(machine, *object, "qNames", *name);
            if (!index) return std::unexpected(index.error());
            return std::optional<Value>(Value::from_int(*index));
        });
    add(registry, std::string(kAttributesImpl), "getIndex",
        "(Ljava/lang/String;Ljava/lang/String;)I",
        [find_index](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto uri = reference_argument(arguments, 1U);
            auto local = reference_argument(arguments, 2U);
            if (!object || !uri || !local) {
                if (!object) return std::unexpected(object.error());
                if (!uri) return std::unexpected(uri.error());
                return std::unexpected(local.error());
            }
            auto local_index = find_index(machine, *object, "localNames", *local);
            if (!local_index || *local_index < 0) {
                if (!local_index) return std::unexpected(local_index.error());
                return std::optional<Value>(Value::from_int(-1));
            }
            auto candidate = attribute_at(machine, *object, "uris", *local_index);
            if (!candidate) return std::unexpected(candidate.error());
            auto expected = string_value(machine, *uri, true);
            auto actual = string_value(machine, *candidate, true);
            if (!expected || !actual) {
                if (!expected) return std::unexpected(expected.error());
                return std::unexpected(actual.error());
            }
            return std::optional<Value>(Value::from_int(
                *expected == *actual ? *local_index : -1));
        });
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"getType", "types"},
             {"getValue", "values"}}) {
        add(registry, std::string(kAttributesImpl), std::string(method),
            "(Ljava/lang/String;)Ljava/lang/String;",
            [field, find_index](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto name = reference_argument(arguments, 1U);
                if (!object || !name) {
                    if (!object) return std::unexpected(object.error());
                    return std::unexpected(name.error());
                }
                auto index = find_index(machine, *object, "qNames", *name);
                if (!index) return std::unexpected(index.error());
                if (*index < 0) {
                    return std::optional<Value>(Value::from_reference({}));
                }
                auto value = attribute_at(machine, *object, field, *index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_reference(*value));
            });
        add(registry, std::string(kAttributesImpl), std::string(method),
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            [field, find_index](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto local = reference_argument(arguments, 2U);
                if (!object || !local) {
                    if (!object) return std::unexpected(object.error());
                    return std::unexpected(local.error());
                }
                auto index = find_index(machine, *object, "localNames", *local);
                if (!index) return std::unexpected(index.error());
                if (*index < 0) {
                    return std::optional<Value>(Value::from_reference({}));
                }
                auto value = attribute_at(machine, *object, field, *index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_reference(*value));
            });
    }
    add(registry, std::string(kAttributesImpl), "clear", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto stored = set_int_field(machine, *object, kAttributesImpl,
                                        "length", 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
}

void register_xml_reader_factory(NativeMethodRegistry& registry) {
    for (const std::string_view descriptor : {
             std::string_view("()Lorg/xml/sax/XMLReader;"),
             std::string_view("(Ljava/lang/String;)Lorg/xml/sax/XMLReader;")}) {
        add(registry, "org/xml/sax/helpers/XMLReaderFactory",
            "createXMLReader", std::string(descriptor),
            [](Machine& machine, std::span<const Value>)
                -> Result<std::optional<Value>> {
                auto parser = create_parser(machine, false, false);
                if (!parser) return std::unexpected(parser.error());
                return std::optional<Value>(Value::from_reference(*parser));
            });
    }
}

} // namespace

void register_xml_natives(NativeMethodRegistry& registry) {
    register_input_source_natives(registry);
    register_default_handler_natives(registry);
    register_factory_natives(registry);
    register_parser_natives(registry);
    register_attributes_natives(registry);
    register_xml_reader_factory(registry);
}

} // namespace phoneme::vm
