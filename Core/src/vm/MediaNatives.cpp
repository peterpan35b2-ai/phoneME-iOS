#include "MediaNatives.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/media/MediaService.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr usize kPlayerNativeIdField = 0;
constexpr usize kPlayerListenersField = 1;
constexpr usize kPlayerListenerCountField = 2;
constexpr usize kPlayerVolumeControlField = 3;
constexpr usize kPlayerToneControlField = 4;
constexpr usize kPlayerContentTypeField = 5;
constexpr usize kPlayerTimeBaseField = 6;
constexpr usize kControlPlayerField = 0;
constexpr usize kMaximumStreamBytes = 32U * 1024U * 1024U;

constexpr std::array<std::string_view, 13> kContentTypes {
    "audio/mpeg",
    "audio/mp3",
    "audio/x-wav",
    "audio/wav",
    "audio/basic",
    "audio/aac",
    "audio/mp4",
    "audio/x-m4a",
    "audio/midi",
    "audio/x-midi",
    "audio/sp-midi",
    "audio/amr",
    "audio/x-tone-seq",
};

constexpr std::array<std::string_view, 5> kProtocols {
    "file", "http", "https", "resource", "device",
};

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod implementation) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(implementation));
    if (!registered) std::terminate();
}

[[nodiscard]] Result<ObjectRef> receiver(std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "media native method is missing its receiver");
    }
    auto reference = arguments.front().as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "media native receiver is null");
    }
    return *reference;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "media native reference argument is missing");
    }
    auto reference = arguments[index].as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (!allow_null && reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "media reference argument is null");
    }
    return *reference;
}

[[nodiscard]] Result<i32> integer_argument(std::span<const Value> arguments,
                                           usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "media native integer argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<i64> long_argument(std::span<const Value> arguments,
                                        usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "media native long argument is missing");
    }
    return arguments[index].as_long();
}

[[nodiscard]] Result<ObjectRef> create_string(Machine& machine,
                                              std::u16string text) {
    auto object = machine.class_states().allocate_instance(machine.heap(),
                                                           "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] Result<ObjectRef> create_ascii_string(Machine& machine,
                                                    std::string_view text) {
    std::u16string converted;
    converted.reserve(text.size());
    for (const char character : text) {
        converted.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
    }
    return create_string(machine, std::move(converted));
}

void append_utf8(std::string& output, u32 code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] std::string utf8_text(std::u16string_view text) {
    std::string output;
    output.reserve(text.size() * 2U);
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
        append_utf8(output, code_point);
    }
    return output;
}

[[nodiscard]] Result<std::string> string_utf8(Machine& machine,
                                              ObjectRef string,
                                              bool allow_null = false) {
    if (string.is_null()) {
        if (allow_null) return std::string {};
        return fail_java("java/lang/NullPointerException",
                         "media String argument is null");
    }
    auto text = machine.heap().string_value(string);
    if (!text) return std::unexpected(text.error());
    return utf8_text(*text);
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

[[nodiscard]] std::string normalize_content_type(std::string type) {
    const usize parameter = type.find(';');
    if (parameter != std::string::npos) {
        type.resize(parameter);
    }
    while (!type.empty() &&
           std::isspace(static_cast<unsigned char>(type.front())) != 0) {
        type.erase(type.begin());
    }
    while (!type.empty() &&
           std::isspace(static_cast<unsigned char>(type.back())) != 0) {
        type.pop_back();
    }
    type = lower_ascii(std::move(type));
    if (type == "audio/mp3") return "audio/mpeg";
    if (type == "audio/wav") return "audio/x-wav";
    if (type == "audio/x-m4a") return "audio/mp4";
    if (type == "audio/x-midi" || type == "audio/sp-midi") {
        return "audio/midi";
    }
    return type;
}

[[nodiscard]] std::string infer_content_type(std::string_view name) {
    std::string lower = lower_ascii(std::string(name));
    const usize query = lower.find_first_of("?#");
    if (query != std::string::npos) lower.resize(query);
    if (lower.ends_with(".mp3")) return "audio/mpeg";
    if (lower.ends_with(".wav")) return "audio/x-wav";
    if (lower.ends_with(".aac")) return "audio/aac";
    if (lower.ends_with(".m4a") || lower.ends_with(".mp4")) {
        return "audio/mp4";
    }
    if (lower.ends_with(".mid") || lower.ends_with(".midi")) {
        return "audio/midi";
    }
    if (lower.ends_with(".amr")) return "audio/amr";
    return "application/octet-stream";
}

[[nodiscard]] bool supported_content_type(std::string_view type) noexcept {
    const std::string normalized = normalize_content_type(std::string(type));
    return std::ranges::any_of(kContentTypes,
                               [&](std::string_view candidate) {
                                   return normalize_content_type(
                                              std::string(candidate)) ==
                                          normalized;
                               });
}

[[nodiscard]] bool supported_protocol(std::string_view protocol) noexcept {
    return std::ranges::find(kProtocols, protocol) != kProtocols.end();
}

[[nodiscard]] std::unexpected<Error> map_media_error(const Error& error) {
    switch (error.code) {
    case ErrorCode::invalid_argument:
    case ErrorCode::out_of_range:
        return fail_java("java/lang/IllegalArgumentException", error.message);
    case ErrorCode::invalid_state:
        return fail_java("java/lang/IllegalStateException", error.message);
    default:
        return fail_java("javax/microedition/media/MediaException",
                         error.message);
    }
}

[[nodiscard]] Result<Value> field_value(Machine& machine,
                                        ObjectRef object,
                                        usize index) {
    return machine.heap().field(object, index);
}

[[nodiscard]] Result<i32> int_field(Machine& machine,
                                    ObjectRef object,
                                    usize index) {
    auto value = field_value(machine, object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<ObjectRef> reference_field(Machine& machine,
                                                ObjectRef object,
                                                usize index) {
    auto value = field_value(machine, object, index);
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
    return machine.heap().set_field(object,
                                    index,
                                    Value::from_reference(value));
}

[[nodiscard]] Result<i32> player_id(Machine& machine, ObjectRef player) {
    return int_field(machine, player, kPlayerNativeIdField);
}

[[nodiscard]] Result<ObjectRef> create_time_base(Machine& machine) {
    return machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/media/SystemTimeBase");
}

[[nodiscard]] Result<ObjectRef> create_player_object(
    Machine& machine,
    i32 native_id,
    std::string_view content_type) {
    auto player = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/media/IOSPlayer");
    if (!player) return std::unexpected(player.error());
    auto listeners = machine.heap().allocate_array(
        "[Ljavax/microedition/media/PlayerListener;",
        4U,
        Value::from_reference(ObjectRef {}));
    if (!listeners) return std::unexpected(listeners.error());
    auto volume = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/media/IOSVolumeControl");
    if (!volume) return std::unexpected(volume.error());
    ObjectRef tone;
    if (normalize_content_type(std::string(content_type)) ==
        "audio/x-tone-seq") {
        auto allocated = machine.class_states().allocate_instance(
            machine.heap(), "javax/microedition/media/IOSToneControl");
        if (!allocated) return std::unexpected(allocated.error());
        tone = *allocated;
    }
    auto type_string = create_ascii_string(machine, content_type);
    if (!type_string) return std::unexpected(type_string.error());
    auto time_base = create_time_base(machine);
    if (!time_base) return std::unexpected(time_base.error());

    auto status = set_int_field(machine, *player, kPlayerNativeIdField,
                                native_id);
    if (!status) return std::unexpected(status.error());
    status = set_reference_field(machine, *player, kPlayerListenersField,
                                 *listeners);
    if (!status) return std::unexpected(status.error());
    status = set_int_field(machine, *player, kPlayerListenerCountField, 0);
    if (!status) return std::unexpected(status.error());
    status = set_reference_field(machine, *player, kPlayerVolumeControlField,
                                 *volume);
    if (!status) return std::unexpected(status.error());
    status = set_reference_field(machine, *player, kPlayerToneControlField,
                                 tone);
    if (!status) return std::unexpected(status.error());
    status = set_reference_field(machine, *player, kPlayerContentTypeField,
                                 *type_string);
    if (!status) return std::unexpected(status.error());
    status = set_reference_field(machine, *player, kPlayerTimeBaseField,
                                 *time_base);
    if (!status) return std::unexpected(status.error());
    status = set_reference_field(machine, *volume, kControlPlayerField,
                                 *player);
    if (!status) return std::unexpected(status.error());
    if (!tone.is_null()) {
        status = set_reference_field(machine, tone, kControlPlayerField,
                                     *player);
        if (!status) return std::unexpected(status.error());
    }
    return *player;
}

[[nodiscard]] Result<ObjectRef> create_string_array(
    Machine& machine,
    std::span<const std::string_view> values) {
    auto array = machine.heap().allocate_array(
        "[Ljava/lang/String;",
        values.size(),
        Value::from_reference(ObjectRef {}));
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < values.size(); ++index) {
        auto string = create_ascii_string(machine, values[index]);
        if (!string) return std::unexpected(string.error());
        auto stored = machine.heap().set_element(
            *array, index, Value::from_reference(*string));
        if (!stored) return std::unexpected(stored.error());
    }
    return *array;
}

[[nodiscard]] Result<ObjectRef> boxed_long(Machine& machine, i64 value) {
    auto object = machine.class_states().allocate_instance(machine.heap(),
                                                           "java/lang/Long");
    if (!object) return std::unexpected(object.error());
    auto stored = machine.heap().set_field(*object, 0, Value::from_long(value));
    if (!stored) return std::unexpected(stored.error());
    return *object;
}

[[nodiscard]] std::string_view event_name(media::MediaEventKind kind) noexcept {
    switch (kind) {
    case media::MediaEventKind::started:
        return "started";
    case media::MediaEventKind::stopped:
        return "stopped";
    case media::MediaEventKind::end_of_media:
        return "endOfMedia";
    case media::MediaEventKind::closed:
        return "closed";
    case media::MediaEventKind::error:
        return "error";
    case media::MediaEventKind::volume_changed:
        return "volumeChanged";
    }
    return "error";
}

[[nodiscard]] Result<ObjectRef> event_data(Machine& machine,
                                           ObjectRef player,
                                           const media::MediaEvent& event) {
    if (event.kind == media::MediaEventKind::started ||
        event.kind == media::MediaEventKind::stopped ||
        event.kind == media::MediaEventKind::end_of_media) {
        return boxed_long(machine, event.media_time);
    }
    if (event.kind == media::MediaEventKind::error) {
        return create_ascii_string(machine, event.detail);
    }
    if (event.kind == media::MediaEventKind::volume_changed) {
        return reference_field(machine, player, kPlayerVolumeControlField);
    }
    return ObjectRef {};
}

void dispatch_event(Machine& machine,
                    ObjectRef player,
                    const media::MediaEvent& event) {
    auto listeners = reference_field(machine, player, kPlayerListenersField);
    auto count = int_field(machine, player, kPlayerListenerCountField);
    if (!listeners || !count || listeners->is_null() || *count <= 0) {
        return;
    }
    auto event_string = create_ascii_string(machine, event_name(event.kind));
    auto data = event_data(machine, player, event);
    if (!event_string || !data) return;

    const usize listener_count = static_cast<usize>(*count);
    for (usize index = 0; index < listener_count; ++index) {
        auto value = machine.heap().element(*listeners, index);
        if (!value) continue;
        auto listener = value->as_reference();
        if (!listener || listener->is_null()) continue;
        const std::array<Value, 3> arguments {
            Value::from_reference(player),
            Value::from_reference(*event_string),
            Value::from_reference(*data),
        };
        (void)machine.invoke_instance(
            *listener,
            "javax/microedition/media/PlayerListener",
            "playerUpdate",
            "(Ljavax/microedition/media/Player;Ljava/lang/String;Ljava/lang/Object;)V",
            arguments,
            1'000'000);
    }
}

void synchronize_and_dispatch(Machine& machine, ObjectRef player) {
    auto id = player_id(machine, player);
    if (!id) return;
    auto event = machine.media().synchronize(*id);
    if (event && event->has_value()) {
        dispatch_event(machine, player, **event);
    }
}

[[nodiscard]] Result<std::vector<u8>> copy_byte_array(Machine& machine,
                                                      ObjectRef array) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "media byte array is null");
    }
    auto length = machine.heap().array_length(array);
    if (!length) return std::unexpected(length.error());
    std::vector<u8> bytes;
    bytes.reserve(*length);
    for (usize index = 0; index < *length; ++index) {
        auto value = machine.heap().element(array, index);
        if (!value) return std::unexpected(value.error());
        auto integer = value->as_int();
        if (!integer) return std::unexpected(integer.error());
        bytes.push_back(static_cast<u8>(static_cast<u32>(*integer) & 0xFFU));
    }
    return bytes;
}

[[nodiscard]] Result<std::vector<u8>> read_input_stream(Machine& machine,
                                                        ObjectRef stream) {
    if (stream.is_null()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "media InputStream is null");
    }
    constexpr usize kBufferSize = 4'096U;
    auto buffer = machine.heap().allocate_array(
        "[B", kBufferSize, Value::from_int(0));
    if (!buffer) return std::unexpected(buffer.error());
    std::vector<u8> output;
    for (;;) {
        const std::array<Value, 3> arguments {
            Value::from_reference(*buffer),
            Value::from_int(0),
            Value::from_int(static_cast<i32>(kBufferSize)),
        };
        auto read = machine.invoke_instance(stream,
                                            "java/io/InputStream",
                                            "read",
                                            "([BII)I",
                                            arguments,
                                            2'000'000);
        if (!read) return std::unexpected(read.error());
        if (read->throwable.has_value()) {
            auto class_name = machine.heap().class_name(*read->throwable);
            return fail_java(class_name ? *class_name : "java/io/IOException",
                             "InputStream failed while creating media player");
        }
        if (!read->return_value.has_value()) {
            return fail(ErrorCode::internal_error,
                        "InputStream.read returned no value");
        }
        auto count = read->return_value->as_int();
        if (!count) return std::unexpected(count.error());
        if (*count < 0) break;
        if (*count == 0) continue;
        if (static_cast<usize>(*count) > kBufferSize ||
            output.size() > kMaximumStreamBytes -
                                static_cast<usize>(*count)) {
            return fail_java("java/io/IOException",
                             "media stream exceeds 32 MiB buffer limit");
        }
        for (i32 index = 0; index < *count; ++index) {
            auto value = machine.heap().element(*buffer,
                                                static_cast<usize>(index));
            if (!value) return std::unexpected(value.error());
            auto integer = value->as_int();
            if (!integer) return std::unexpected(integer.error());
            output.push_back(
                static_cast<u8>(static_cast<u32>(*integer) & 0xFFU));
        }
    }
    return output;
}

[[nodiscard]] Result<std::optional<Value>> create_player_from_locator(
    Machine& machine,
    ObjectRef locator_string) {
    auto locator = string_utf8(machine, locator_string);
    if (!locator) return std::unexpected(locator.error());
    if (locator->empty()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "media locator is empty");
    }

    std::string type;
    Result<i32> native_id = fail(ErrorCode::internal_error,
                                 "media player was not created");
    if (locator->starts_with("resource:")) {
        std::string resource = locator->substr(9U);
        while (!resource.empty() && resource.front() == '/') {
            resource.erase(resource.begin());
        }
        auto data = machine.classes().read_resource(resource);
        if (!data) {
            return fail_java("java/io/IOException", data.error().message);
        }
        type = normalize_content_type(infer_content_type(resource));
        native_id = machine.media().create_data(std::move(*data), type);
    } else {
        const usize separator = locator->find(':');
        if (separator == std::string::npos || separator == 0U) {
            return fail_java("javax/microedition/media/MediaException",
                             "unsupported media locator");
        }
        const std::string protocol = lower_ascii(locator->substr(0, separator));
        if (!supported_protocol(protocol)) {
            return fail_java("javax/microedition/media/MediaException",
                             "unsupported media protocol: " + protocol);
        }
        if (*locator == "device://tone") {
            type = "audio/x-tone-seq";
            native_id = machine.media().create_data({}, type);
        } else if (*locator == "device://midi") {
            type = "audio/midi";
            native_id = machine.media().create_locator(*locator, type);
        } else {
            type = normalize_content_type(infer_content_type(*locator));
            native_id = machine.media().create_locator(*locator, type);
        }
    }
    if (!native_id) return map_media_error(native_id.error());
    auto player = create_player_object(machine, *native_id, type);
    if (!player) return std::unexpected(player.error());
    return std::optional<Value>(Value::from_reference(*player));
}

[[nodiscard]] Result<std::optional<Value>> create_player_from_stream(
    Machine& machine,
    ObjectRef stream,
    ObjectRef type_string) {
    auto data = read_input_stream(machine, stream);
    if (!data) return std::unexpected(data.error());
    std::string type;
    if (!type_string.is_null()) {
        auto text = string_utf8(machine, type_string);
        if (!text) return std::unexpected(text.error());
        type = normalize_content_type(std::move(*text));
    }
    if (type.empty() || type == "application/octet-stream") {
        if (data->size() >= 12U &&
            std::equal(data->begin(), data->begin() + 4, "RIFF") &&
            std::equal(data->begin() + 8, data->begin() + 12, "WAVE")) {
            type = "audio/x-wav";
        } else if (data->size() >= 3U && (*data)[0] == 'I' &&
                   (*data)[1] == 'D' && (*data)[2] == '3') {
            type = "audio/mpeg";
        }
    }
    if (!supported_content_type(type)) {
        return fail_java("javax/microedition/media/MediaException",
                         "unsupported or unknown media content type");
    }
    auto native_id = machine.media().create_data(std::move(*data), type);
    if (!native_id) return map_media_error(native_id.error());
    auto player = create_player_object(machine, *native_id, type);
    if (!player) return std::unexpected(player.error());
    return std::optional<Value>(Value::from_reference(*player));
}

[[nodiscard]] Result<ObjectRef> player_from_control(Machine& machine,
                                                    ObjectRef control) {
    auto player = reference_field(machine, control, kControlPlayerField);
    if (!player) return std::unexpected(player.error());
    if (player->is_null()) {
        return fail_java("java/lang/IllegalStateException",
                         "media control is detached");
    }
    return *player;
}

void register_manager(NativeMethodRegistry& registry) {
    add(registry,
        "javax/microedition/media/Manager",
        "<init>",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/Manager",
        "getSupportedContentTypes",
        "(Ljava/lang/String;)[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "getSupportedContentTypes expects one argument");
            }
            auto protocol = arguments[0].as_reference();
            if (!protocol) return std::unexpected(protocol.error());
            if (!protocol->is_null()) {
                auto text = string_utf8(machine, *protocol);
                if (!text) return std::unexpected(text.error());
                if (!supported_protocol(lower_ascii(std::move(*text)))) {
                    auto empty = machine.heap().allocate_array(
                        "[Ljava/lang/String;",
                        0U,
                        Value::from_reference(ObjectRef {}));
                    if (!empty) return std::unexpected(empty.error());
                    return std::optional<Value>(Value::from_reference(*empty));
                }
            }
            auto result = create_string_array(machine, kContentTypes);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry,
        "javax/microedition/media/Manager",
        "getSupportedProtocols",
        "(Ljava/lang/String;)[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "getSupportedProtocols expects one argument");
            }
            auto type = arguments[0].as_reference();
            if (!type) return std::unexpected(type.error());
            if (!type->is_null()) {
                auto text = string_utf8(machine, *type);
                if (!text) return std::unexpected(text.error());
                if (!supported_content_type(*text)) {
                    auto empty = machine.heap().allocate_array(
                        "[Ljava/lang/String;",
                        0U,
                        Value::from_reference(ObjectRef {}));
                    if (!empty) return std::unexpected(empty.error());
                    return std::optional<Value>(Value::from_reference(*empty));
                }
            }
            auto result = create_string_array(machine, kProtocols);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });

    add(registry,
        "javax/microedition/media/Manager",
        "createPlayer",
        "(Ljava/lang/String;)Ljavax/microedition/media/Player;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto locator = reference_argument(arguments, 0U, false);
            if (!locator) return std::unexpected(locator.error());
            return create_player_from_locator(machine, *locator);
        });

    add(registry,
        "javax/microedition/media/Manager",
        "createPlayer",
        "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto stream = reference_argument(arguments, 0U, false);
            auto type = reference_argument(arguments, 1U, true);
            if (!stream) return std::unexpected(stream.error());
            if (!type) return std::unexpected(type.error());
            return create_player_from_stream(machine, *stream, *type);
        });

    add(registry,
        "javax/microedition/media/Manager",
        "playTone",
        "(III)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto note = integer_argument(arguments, 0U);
            auto duration = integer_argument(arguments, 1U);
            auto volume = integer_argument(arguments, 2U);
            if (!note || !duration || !volume) {
                return fail(ErrorCode::invalid_argument,
                            "playTone arguments are invalid");
            }
            auto played = machine.media().play_tone(*note, *duration, *volume);
            if (!played) return map_media_error(played.error());
            if (!*played) {
                return fail_java("javax/microedition/media/MediaException",
                                 "platform failed to play tone");
            }
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/Manager",
        "getSystemTimeBase",
        "()Ljavax/microedition/media/TimeBase;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto time_base = create_time_base(machine);
            if (!time_base) return std::unexpected(time_base.error());
            return std::optional<Value>(Value::from_reference(*time_base));
        });
}

void register_player(NativeMethodRegistry& registry) {
    add(registry,
        "javax/microedition/media/IOSPlayer",
        "<init>",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });

    const auto lifecycle = [&](std::string name,
                               auto operation) {
        add(registry,
            "javax/microedition/media/IOSPlayer",
            std::move(name),
            "()V",
            [operation](Machine& machine,
                        std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto player = receiver(arguments);
                if (!player) return std::unexpected(player.error());
                auto id = player_id(machine, *player);
                if (!id) return std::unexpected(id.error());
                auto result = operation(machine.media(), *id);
                if (!result) return map_media_error(result.error());
                if constexpr (requires { result->has_value(); }) {
                    if (result->has_value()) {
                        dispatch_event(machine, *player, **result);
                    }
                }
                return std::optional<Value> {};
            });
    };

    lifecycle("realize",
              [](media::MediaService& service, i32 id) {
                  return service.realize(id);
              });
    lifecycle("prefetch",
              [](media::MediaService& service, i32 id) {
                  return service.prefetch(id);
              });
    lifecycle("start",
              [](media::MediaService& service, i32 id) {
                  return service.start(id);
              });
    lifecycle("stop",
              [](media::MediaService& service, i32 id) {
                  return service.stop(id);
              });
    lifecycle("deallocate",
              [](media::MediaService& service, i32 id) {
                  return service.deallocate(id);
              });
    lifecycle("close",
              [](media::MediaService& service, i32 id) {
                  return service.close(id);
              });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "setTimeBase",
        "(Ljavax/microedition/media/TimeBase;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            auto time_base = reference_argument(arguments, 1U, true);
            if (!player) return std::unexpected(player.error());
            if (!time_base) return std::unexpected(time_base.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state == media::PlayerState::closed) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is closed");
            }
            if (snapshot->state == media::PlayerState::started) {
                return fail_java("java/lang/IllegalStateException",
                                 "cannot change time base while started");
            }
            ObjectRef value = *time_base;
            if (value.is_null()) {
                auto system = create_time_base(machine);
                if (!system) return std::unexpected(system.error());
                value = *system;
            }
            auto stored = set_reference_field(machine,
                                               *player,
                                               kPlayerTimeBaseField,
                                               value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getTimeBase",
        "()Ljavax/microedition/media/TimeBase;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state < media::PlayerState::realized) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is not realized");
            }
            auto value = reference_field(machine, *player, kPlayerTimeBaseField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "setMediaTime",
        "(J)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            auto time = long_argument(arguments, 1U);
            if (!player) return std::unexpected(player.error());
            if (!time) return std::unexpected(time.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto result = machine.media().set_media_time(*id, *time);
            if (!result) return map_media_error(result.error());
            return std::optional<Value>(Value::from_long(*result));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getMediaTime",
        "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            if (!player) return std::unexpected(player.error());
            synchronize_and_dispatch(machine, *player);
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state == media::PlayerState::closed) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is closed");
            }
            return std::optional<Value>(Value::from_long(snapshot->media_time));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getState",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            if (!player) return std::unexpected(player.error());
            synchronize_and_dispatch(machine, *player);
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(snapshot->state)));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getDuration",
        "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            if (!player) return std::unexpected(player.error());
            synchronize_and_dispatch(machine, *player);
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state == media::PlayerState::closed) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is closed");
            }
            return std::optional<Value>(Value::from_long(snapshot->duration));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getContentType",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state < media::PlayerState::realized) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is not realized");
            }
            auto type = reference_field(machine,
                                        *player,
                                        kPlayerContentTypeField);
            if (!type) return std::unexpected(type.error());
            return std::optional<Value>(Value::from_reference(*type));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "setLoopCount",
        "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            auto count = integer_argument(arguments, 1U);
            if (!player) return std::unexpected(player.error());
            if (!count) return std::unexpected(count.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto status = machine.media().set_loop_count(*id, *count);
            if (!status) return map_media_error(status.error());
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "addPlayerListener",
        "(Ljavax/microedition/media/PlayerListener;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            auto listener = reference_argument(arguments, 1U, true);
            if (!player) return std::unexpected(player.error());
            if (!listener) return std::unexpected(listener.error());
            if (listener->is_null()) return std::optional<Value> {};
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state == media::PlayerState::closed) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is closed");
            }
            auto array = reference_field(machine,
                                         *player,
                                         kPlayerListenersField);
            auto count = int_field(machine,
                                   *player,
                                   kPlayerListenerCountField);
            if (!array || !count) {
                return fail(ErrorCode::internal_error,
                            "player listener storage is invalid");
            }
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*array,
                                                    static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto existing = value->as_reference();
                if (!existing) return std::unexpected(existing.error());
                if (*existing == *listener) return std::optional<Value> {};
            }
            auto length = machine.heap().array_length(*array);
            if (!length) return std::unexpected(length.error());
            ObjectRef target = *array;
            if (static_cast<usize>(*count) >= *length) {
                const usize next_length = std::max<usize>(4U, *length * 2U);
                auto expanded = machine.heap().allocate_array(
                    "[Ljavax/microedition/media/PlayerListener;",
                    next_length,
                    Value::from_reference(ObjectRef {}));
                if (!expanded) return std::unexpected(expanded.error());
                for (usize index = 0; index < *length; ++index) {
                    auto value = machine.heap().element(*array, index);
                    if (!value) return std::unexpected(value.error());
                    auto stored = machine.heap().set_element(*expanded,
                                                             index,
                                                             *value);
                    if (!stored) return std::unexpected(stored.error());
                }
                target = *expanded;
                auto stored = set_reference_field(machine,
                                                  *player,
                                                  kPlayerListenersField,
                                                  target);
                if (!stored) return std::unexpected(stored.error());
            }
            auto stored = machine.heap().set_element(
                target,
                static_cast<usize>(*count),
                Value::from_reference(*listener));
            if (!stored) return std::unexpected(stored.error());
            stored = set_int_field(machine,
                                   *player,
                                   kPlayerListenerCountField,
                                   *count + 1);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "removePlayerListener",
        "(Ljavax/microedition/media/PlayerListener;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            auto listener = reference_argument(arguments, 1U, true);
            if (!player) return std::unexpected(player.error());
            if (!listener) return std::unexpected(listener.error());
            if (listener->is_null()) return std::optional<Value> {};
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state == media::PlayerState::closed) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is closed");
            }
            auto array = reference_field(machine,
                                         *player,
                                         kPlayerListenersField);
            auto count = int_field(machine,
                                   *player,
                                   kPlayerListenerCountField);
            if (!array || !count) {
                return fail(ErrorCode::internal_error,
                            "player listener storage is invalid");
            }
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*array,
                                                    static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto existing = value->as_reference();
                if (!existing) return std::unexpected(existing.error());
                if (*existing != *listener) continue;
                for (i32 move = index; move + 1 < *count; ++move) {
                    auto next = machine.heap().element(
                        *array, static_cast<usize>(move + 1));
                    if (!next) return std::unexpected(next.error());
                    auto stored = machine.heap().set_element(
                        *array, static_cast<usize>(move), *next);
                    if (!stored) return std::unexpected(stored.error());
                }
                auto cleared = machine.heap().set_element(
                    *array,
                    static_cast<usize>(*count - 1),
                    Value::from_reference(ObjectRef {}));
                if (!cleared) return std::unexpected(cleared.error());
                auto stored = set_int_field(machine,
                                            *player,
                                            kPlayerListenerCountField,
                                            *count - 1);
                if (!stored) return std::unexpected(stored.error());
                break;
            }
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getControl",
        "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            auto type = reference_argument(arguments, 1U, false);
            if (!player) return std::unexpected(player.error());
            if (!type) return std::unexpected(type.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state < media::PlayerState::realized) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is not realized");
            }
            auto text = string_utf8(machine, *type);
            if (!text) return std::unexpected(text.error());
            ObjectRef control;
            if (*text == "VolumeControl" ||
                *text == "javax.microedition.media.control.VolumeControl") {
                auto volume = reference_field(machine,
                                              *player,
                                              kPlayerVolumeControlField);
                if (!volume) return std::unexpected(volume.error());
                control = *volume;
            } else if (*text == "ToneControl" ||
                       *text == "javax.microedition.media.control.ToneControl") {
                auto tone = reference_field(machine,
                                            *player,
                                            kPlayerToneControlField);
                if (!tone) return std::unexpected(tone.error());
                control = *tone;
            }
            return std::optional<Value>(Value::from_reference(control));
        });

    add(registry,
        "javax/microedition/media/IOSPlayer",
        "getControls",
        "()[Ljavax/microedition/media/Control;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto player = receiver(arguments);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            if (snapshot->state < media::PlayerState::realized) {
                return fail_java("java/lang/IllegalStateException",
                                 "player is not realized");
            }
            auto volume = reference_field(machine,
                                          *player,
                                          kPlayerVolumeControlField);
            auto tone = reference_field(machine,
                                        *player,
                                        kPlayerToneControlField);
            if (!volume || !tone) {
                return fail(ErrorCode::internal_error,
                            "player controls are invalid");
            }
            const usize count = tone->is_null() ? 1U : 2U;
            auto controls = machine.heap().allocate_array(
                "[Ljavax/microedition/media/Control;",
                count,
                Value::from_reference(ObjectRef {}));
            if (!controls) return std::unexpected(controls.error());
            auto stored = machine.heap().set_element(
                *controls, 0U, Value::from_reference(*volume));
            if (!stored) return std::unexpected(stored.error());
            if (!tone->is_null()) {
                stored = machine.heap().set_element(
                    *controls, 1U, Value::from_reference(*tone));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*controls));
        });
}

void register_controls(NativeMethodRegistry& registry) {
    add(registry,
        "javax/microedition/media/IOSVolumeControl",
        "<init>",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSVolumeControl",
        "setLevel",
        "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto control = receiver(arguments);
            auto level = integer_argument(arguments, 1U);
            if (!control) return std::unexpected(control.error());
            if (!level) return std::unexpected(level.error());
            auto player = player_from_control(machine, *control);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            const i32 clamped = std::clamp(*level, 0, 100);
            auto status = machine.media().set_volume(*id, clamped);
            if (!status) return map_media_error(status.error());
            dispatch_event(machine,
                           *player,
                           media::MediaEvent {
                               .kind = media::MediaEventKind::volume_changed,
                               .player_id = *id,
                           });
            return std::optional<Value>(Value::from_int(clamped));
        });

    add(registry,
        "javax/microedition/media/IOSVolumeControl",
        "getLevel",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto control = receiver(arguments);
            if (!control) return std::unexpected(control.error());
            auto player = player_from_control(machine, *control);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            return std::optional<Value>(Value::from_int(snapshot->volume));
        });

    add(registry,
        "javax/microedition/media/IOSVolumeControl",
        "setMute",
        "(Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto control = receiver(arguments);
            auto muted = integer_argument(arguments, 1U);
            if (!control) return std::unexpected(control.error());
            if (!muted) return std::unexpected(muted.error());
            auto player = player_from_control(machine, *control);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto status = machine.media().set_mute(*id, *muted != 0);
            if (!status) return map_media_error(status.error());
            dispatch_event(machine,
                           *player,
                           media::MediaEvent {
                               .kind = media::MediaEventKind::volume_changed,
                               .player_id = *id,
                           });
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSVolumeControl",
        "isMuted",
        "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto control = receiver(arguments);
            if (!control) return std::unexpected(control.error());
            auto player = player_from_control(machine, *control);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto snapshot = machine.media().snapshot(*id);
            if (!snapshot) return map_media_error(snapshot.error());
            return std::optional<Value>(
                Value::from_int(snapshot->muted ? 1 : 0));
        });

    add(registry,
        "javax/microedition/media/IOSToneControl",
        "<init>",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });

    add(registry,
        "javax/microedition/media/IOSToneControl",
        "setSequence",
        "([B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto control = receiver(arguments);
            auto sequence = reference_argument(arguments, 1U, false);
            if (!control) return std::unexpected(control.error());
            if (!sequence) return std::unexpected(sequence.error());
            auto player = player_from_control(machine, *control);
            if (!player) return std::unexpected(player.error());
            auto id = player_id(machine, *player);
            if (!id) return std::unexpected(id.error());
            auto bytes = copy_byte_array(machine, *sequence);
            if (!bytes) return std::unexpected(bytes.error());
            auto status = machine.media().set_tone_sequence(*id,
                                                            std::move(*bytes));
            if (!status) return map_media_error(status.error());
            return std::optional<Value> {};
        });
}

void register_time_base(NativeMethodRegistry& registry) {
    add(registry,
        "javax/microedition/media/SystemTimeBase",
        "<init>",
        "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
    add(registry,
        "javax/microedition/media/SystemTimeBase",
        "getTime",
        "()J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return std::optional<Value>(Value::from_long(now));
        });
}

} // namespace

void register_media_natives(NativeMethodRegistry& registry) {
    register_manager(registry);
    register_player(registry);
    register_controls(registry);
    register_time_base(registry);
}

} // namespace phoneme::vm
