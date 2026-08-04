#include "VendorNatives.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "phoneme/graphics/Color.hpp"
#include "phoneme/graphics/Image.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

#if defined(__APPLE__)
extern "C" __attribute__((weak)) int phoneme_ios_device_set_vibrate(int) {
    return 0;
}
extern "C" int phoneme_ios_device_start_vibrate(int, std::int64_t)
    __attribute__((weak_import));
extern "C" int phoneme_ios_device_stop_vibrate(void)
    __attribute__((weak_import));
extern "C" __attribute__((weak)) int phoneme_ios_device_set_backlight(int) {
    return 0;
}
extern "C" int phoneme_ios_device_flash_lights(std::int64_t)
    __attribute__((weak_import));
#endif

namespace phoneme::vm {
namespace {

constexpr std::string_view kSprintSystem = "com/sprintpcs/util/System";
constexpr std::string_view kSamsungAudioClip = "com/samsung/util/AudioClip";
constexpr std::string_view kSprintClip = "com/sprintpcs/media/Clip";
constexpr std::string_view kSprintPlayer = "com/sprintpcs/media/Player";
constexpr std::string_view kSiemensSms = "com/siemens/mp/gsm/SMS";
constexpr std::string_view kVodafoneImageEncoder = "com/vodafone/util/ImageEncoder";
constexpr std::string_view kMidpScheduler = "com/sun/midp/midlet/Scheduler";
constexpr std::string_view kSiemensLight = "com/siemens/mp/game/Light";
constexpr std::string_view kSiemensVibrator =
    "com/siemens/mp/game/Vibrator";
constexpr std::string_view kSamsungLight = "com/samsung/util/LCDLight";
constexpr std::string_view kMotorolaLighting =
    "com/motorola/multimedia/Lighting";
constexpr i32 kMaximumVendorFeedbackDurationMs = 300'000;

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

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "vendor native reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!allow_null && value->is_null()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "vendor argument must not be null");
    }
    return *value;
}

[[nodiscard]] Result<i32> int_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " integer argument is missing");
    }
    auto value = arguments[index].as_int();
    if (!value) return std::unexpected(value.error());
    return *value;
}

void set_backlight(bool enabled) noexcept {
#if defined(__APPLE__)
    if (phoneme_ios_device_set_backlight != nullptr) {
        (void)phoneme_ios_device_set_backlight(enabled ? 1 : 0);
    }
#else
    (void)enabled;
#endif
}

void flash_backlight(i32 duration_ms) noexcept {
#if defined(__APPLE__)
    if (duration_ms > 0 && phoneme_ios_device_flash_lights != nullptr) {
        (void)phoneme_ios_device_flash_lights(duration_ms);
    }
#else
    (void)duration_ms;
#endif
}

void set_vibration(bool enabled) noexcept {
#if defined(__APPLE__)
    if (phoneme_ios_device_set_vibrate != nullptr) {
        (void)phoneme_ios_device_set_vibrate(enabled ? 1 : 0);
    } else if (!enabled && phoneme_ios_device_stop_vibrate != nullptr) {
        (void)phoneme_ios_device_stop_vibrate();
    } else if (enabled && phoneme_ios_device_start_vibrate != nullptr) {
        (void)phoneme_ios_device_start_vibrate(
            100, kMaximumVendorFeedbackDurationMs);
    }
#else
    (void)enabled;
#endif
}

void trigger_vibration(i32 duration_ms) noexcept {
#if defined(__APPLE__)
    if (duration_ms <= 0) {
        set_vibration(false);
    } else if (phoneme_ios_device_start_vibrate != nullptr) {
        (void)phoneme_ios_device_start_vibrate(100, duration_ms);
    } else {
        set_vibration(true);
    }
#else
    (void)duration_ms;
#endif
}

void register_noop_constructor(NativeMethodRegistry& registry,
                               std::string_view owner) {
    add(registry, std::string(owner), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = reference_argument(arguments, 0U, false);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });
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

[[nodiscard]] Result<FieldLocation> vendor_field(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    return machine.class_states().resolve_field(owner, name, descriptor, false);
}

[[nodiscard]] Status set_vendor_field(Machine& machine,
                                      ObjectRef object,
                                      std::string_view owner,
                                      std::string_view name,
                                      std::string_view descriptor,
                                      Value value) {
    auto location = vendor_field(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index, value);
}

[[nodiscard]] Result<ObjectRef> vendor_reference_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto location = vendor_field(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    auto value = machine.heap().field(object, location->index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Result<i32> vendor_integer_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name) {
    auto location = vendor_field(machine, owner, name, "I");
    if (!location) return std::unexpected(location.error());
    auto value = machine.heap().field(object, location->index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<FieldLocation> vendor_static_field(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    return machine.class_states().resolve_field(owner, name, descriptor, true);
}

[[nodiscard]] Result<ObjectRef> vendor_static_reference_field(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto location = vendor_static_field(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    auto value = machine.class_states().static_field(*location);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Status set_vendor_static_reference_field(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    ObjectRef value) {
    auto location = vendor_static_field(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.class_states().set_static_field(
        *location, Value::from_reference(value));
}

[[nodiscard]] Result<std::optional<Value>> invoke_static_checked(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments = {}) {
    auto result = machine.invoke_static(owner, name, descriptor, arguments);
    if (!result) return std::unexpected(result.error());
    if (result->throwable.has_value()) {
        auto type = machine.heap().class_name(*result->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, std::string(owner) + "." + std::string(name) +
                                    " failed");
    }
    return result->return_value;
}

[[nodiscard]] Status invoke_player_void(Machine& machine,
                                        ObjectRef player,
                                        std::string_view name,
                                        std::string_view descriptor,
                                        std::span<const Value> arguments = {}) {
    if (player.is_null()) return {};
    auto result = machine.invoke_instance(player, "javax/microedition/media/Player",
                                          name, descriptor, arguments);
    if (!result) return std::unexpected(result.error());
    if (result->throwable.has_value()) {
        auto type = machine.heap().class_name(*result->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, std::string("media Player.") +
                                    std::string(name) + " failed");
    }
    return {};
}

[[nodiscard]] Status set_player_volume(Machine& machine,
                                       ObjectRef player,
                                       i32 samsung_level) {
    if (player.is_null()) return {};
    auto control_name = make_string(machine, u"VolumeControl");
    if (!control_name) return std::unexpected(control_name.error());
    auto control_root = machine.pin_native_root(*control_name);
    if (!control_root) return std::unexpected(control_root.error());
    const Value argument = Value::from_reference(*control_name);
    auto result = machine.invoke_instance(
        player, "javax/microedition/media/Player", "getControl",
        "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
        std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (result->throwable.has_value()) {
        auto type = machine.heap().class_name(*result->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, "media Player.getControl failed");
    }
    if (!result->return_value.has_value()) return {};
    auto control = result->return_value->as_reference();
    if (!control) return std::unexpected(control.error());
    if (control->is_null()) return {};
    const i32 level = std::clamp(samsung_level, 0, 5) * 20;
    const Value level_argument = Value::from_int(level);
    auto adjusted = machine.invoke_instance(
        *control, "javax/microedition/media/control/VolumeControl", "setLevel",
        "(I)I", std::span<const Value>(&level_argument, 1U));
    if (!adjusted) return std::unexpected(adjusted.error());
    if (adjusted->throwable.has_value()) {
        auto type = machine.heap().class_name(*adjusted->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, "media VolumeControl.setLevel failed");
    }
    return {};
}

[[nodiscard]] Result<ObjectRef> samsung_content_type(Machine& machine,
                                                      i32 type) {
    switch (type) {
    case 1:
        return make_string(machine, u"audio/midi");
    case 2:
        return make_string(machine, u"application/vnd.smaf");
    case 3:
        return make_string(machine, u"audio/mpeg");
    default:
        return ObjectRef {};
    }
}

[[nodiscard]] Status stop_sprint_player(Machine& machine,
                                        std::string_view field_name) {
    auto player = vendor_static_reference_field(
        machine, kSprintPlayer, field_name,
        "Ljavax/microedition/media/Player;");
    if (!player) return std::unexpected(player.error());
    if (!player->is_null()) {
        auto closed = invoke_player_void(machine, *player, "close", "()V");
        if (!closed) return closed;
    }
    return set_vendor_static_reference_field(
        machine, kSprintPlayer, field_name,
        "Ljavax/microedition/media/Player;", {});
}

[[nodiscard]] Result<ObjectRef> create_player_from_locator(
    Machine& machine,
    ObjectRef locator) {
    const Value argument = Value::from_reference(locator);
    auto result = invoke_static_checked(
        machine, "javax/microedition/media/Manager", "createPlayer",
        "(Ljava/lang/String;)Ljavax/microedition/media/Player;",
        std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Manager.createPlayer returned no value");
    }
    return (*result)->as_reference();
}

[[nodiscard]] Result<ObjectRef> create_player_from_bytes(
    Machine& machine,
    ObjectRef bytes,
    ObjectRef content_type) {
    auto stream = allocate_instance(machine, "java/io/ByteArrayInputStream");
    if (!stream) return std::unexpected(stream.error());
    auto stream_root = machine.pin_native_root(*stream);
    if (!stream_root) return std::unexpected(stream_root.error());
    const Value byte_argument = Value::from_reference(bytes);
    auto initialized = machine.invoke_instance(
        *stream, "java/io/ByteArrayInputStream", "<init>", "([B)V",
        std::span<const Value>(&byte_argument, 1U));
    if (!initialized) return std::unexpected(initialized.error());
    if (initialized->throwable.has_value()) {
        return fail_java("java/io/IOException",
                         "vendor audio stream initialization failed");
    }
    const std::array<Value, 2> arguments {{
        Value::from_reference(*stream), Value::from_reference(content_type),
    }};
    auto result = invoke_static_checked(
        machine, "javax/microedition/media/Manager", "createPlayer",
        "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",
        arguments);
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "Manager.createPlayer returned no value");
    }
    return (*result)->as_reference();
}

void append_be32(std::vector<u8>& output, u32 value) {
    output.push_back(static_cast<u8>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<u8>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<u8>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<u8>(value & 0xFFU));
}

[[nodiscard]] Status append_png_chunk(
    std::vector<u8>& output,
    const std::array<u8, 4>& type,
    std::span<const u8> payload) {
    if (payload.size() > static_cast<usize>(std::numeric_limits<u32>::max()) ||
        payload.size() > static_cast<usize>(std::numeric_limits<uInt>::max())) {
        return fail(ErrorCode::out_of_range, "PNG chunk exceeds zlib limits");
    }
    append_be32(output, static_cast<u32>(payload.size()));
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), payload.begin(), payload.end());
    uLong checksum = crc32(0L, Z_NULL, 0U);
    checksum = crc32(checksum, type.data(), static_cast<uInt>(type.size()));
    if (!payload.empty()) {
        checksum = crc32(checksum, payload.data(),
                         static_cast<uInt>(payload.size()));
    }
    append_be32(output, static_cast<u32>(checksum));
    return {};
}

[[nodiscard]] Result<std::vector<u8>> encode_png_region(
    const graphics::Image& image,
    i32 x,
    i32 y,
    i32 width,
    i32 height) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x > image.width() - width || y > image.height() - height) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Vodafone image region is outside the source image");
    }
    const usize pixel_bytes = static_cast<usize>(width) * 4U;
    if (pixel_bytes > std::numeric_limits<usize>::max() - 1U) {
        return fail(ErrorCode::overflow, "PNG scanline size overflow");
    }
    const usize row_bytes = pixel_bytes + 1U;
    if (static_cast<usize>(height) >
        std::numeric_limits<usize>::max() / row_bytes) {
        return fail(ErrorCode::overflow, "PNG source size overflow");
    }
    std::vector<u8> raw(row_bytes * static_cast<usize>(height));
    usize offset = 0U;
    for (i32 row = 0; row < height; ++row) {
        raw[offset++] = 0U;
        for (i32 column = 0; column < width; ++column) {
            auto pixel = image.pixel(x + column, y + row);
            if (!pixel) return std::unexpected(pixel.error());
            raw[offset++] = graphics::red(*pixel);
            raw[offset++] = graphics::green(*pixel);
            raw[offset++] = graphics::blue(*pixel);
            raw[offset++] = graphics::alpha(*pixel);
        }
    }
    if (raw.size() > static_cast<usize>(std::numeric_limits<uLong>::max())) {
        return fail(ErrorCode::out_of_range, "PNG source exceeds zlib limits");
    }
    const uLong source_size = static_cast<uLong>(raw.size());
    uLongf compressed_size = compressBound(source_size);
    std::vector<u8> compressed(static_cast<usize>(compressed_size));
    const int compression = compress2(
        compressed.data(), &compressed_size, raw.data(), source_size,
        Z_BEST_SPEED);
    if (compression != Z_OK) {
        return fail(ErrorCode::internal_error, "PNG compression failed");
    }
    compressed.resize(static_cast<usize>(compressed_size));

    std::array<u8, 13> header {};
    header[0] = static_cast<u8>((static_cast<u32>(width) >> 24U) & 0xFFU);
    header[1] = static_cast<u8>((static_cast<u32>(width) >> 16U) & 0xFFU);
    header[2] = static_cast<u8>((static_cast<u32>(width) >> 8U) & 0xFFU);
    header[3] = static_cast<u8>(static_cast<u32>(width) & 0xFFU);
    header[4] = static_cast<u8>((static_cast<u32>(height) >> 24U) & 0xFFU);
    header[5] = static_cast<u8>((static_cast<u32>(height) >> 16U) & 0xFFU);
    header[6] = static_cast<u8>((static_cast<u32>(height) >> 8U) & 0xFFU);
    header[7] = static_cast<u8>(static_cast<u32>(height) & 0xFFU);
    header[8] = 8U;
    header[9] = 6U;

    constexpr std::array<u8, 8> signature {{
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
    }};
    constexpr std::array<u8, 4> ihdr {{'I', 'H', 'D', 'R'}};
    constexpr std::array<u8, 4> idat {{'I', 'D', 'A', 'T'}};
    constexpr std::array<u8, 4> iend {{'I', 'E', 'N', 'D'}};
    std::vector<u8> png;
    png.reserve(signature.size() + header.size() + compressed.size() + 64U);
    png.insert(png.end(), signature.begin(), signature.end());
    auto status = append_png_chunk(png, ihdr, header);
    if (!status) return std::unexpected(status.error());
    status = append_png_chunk(png, idat, compressed);
    if (!status) return std::unexpected(status.error());
    status = append_png_chunk(png, iend, {});
    if (!status) return std::unexpected(status.error());
    return png;
}

[[nodiscard]] Result<std::optional<Value>> system_property(
    Machine& machine,
    ObjectRef key) {
    const Value argument = Value::from_reference(key);
    auto result = machine.invoke_static("java/lang/System", "getProperty",
                                        "(Ljava/lang/String;)Ljava/lang/String;",
                                        std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (result->throwable.has_value()) {
        auto type = machine.heap().class_name(*result->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, "SprintPCS property lookup failed");
    }
    return result->return_value;
}

} // namespace

void register_vendor_natives(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kSprintSystem);

    add(registry, std::string(kSprintSystem), "addSystemListener",
        "(Lcom/sprintpcs/util/SystemEventListener;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto listener = reference_argument(arguments, 0U);
            if (!listener) return std::unexpected(listener.error());
            // iOS has no Sprint handset system-event bus. Treat registration as
            // accepted so legacy applications can continue without depending
            // on the emulator-only com.sun.kvem implementation.
            return std::optional<Value> {};
        });

    for (const std::string_view method : {"getProtectedProperty",
                                          "getSystemState"}) {
        add(registry, std::string(kSprintSystem), std::string(method),
            "(Ljava/lang/String;)Ljava/lang/String;",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto key = reference_argument(arguments, 0U, false);
                if (!key) return std::unexpected(key.error());
                return system_property(machine, *key);
            });
    }

    add(registry, std::string(kSprintSystem), "promptMasterVolume", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            // Volume is controlled by the native media/UI bridge. The Sprint
            // prompt is advisory, so a stable no-op is preferable to failing.
            return std::optional<Value> {};
        });

    add(registry, std::string(kSprintSystem), "setExitURI",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto uri = reference_argument(arguments, 0U, false);
            if (!uri) return std::unexpected(uri.error());
            auto text = machine.heap().string_value(*uri);
            if (!text) return std::unexpected(text.error());
            if (text->empty()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "SprintPCS exit URI is empty");
            }
            // Preserve the request for the iOS host/lifecycle layer. Legacy
            // games commonly call this immediately before notifyDestroyed().
            machine.set_system_property(u"com.sprintpcs.exitURI",
                                        std::move(*text));
            return std::optional<Value> {};
        });

    add(registry, std::string(kSamsungAudioClip), "<init>",
        "(ILjava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto clip = reference_argument(arguments, 0U, false);
            auto type = int_argument(arguments, 1U, "Samsung AudioClip.<init>");
            auto resource = reference_argument(arguments, 2U, false);
            if (!clip) return std::unexpected(clip.error());
            if (!type) return std::unexpected(type.error());
            if (!resource) return std::unexpected(resource.error());
            auto text = machine.heap().string_value(*resource);
            if (!text) return std::unexpected(text.error());
            std::u16string locator_text = *text;
            if (!locator_text.starts_with(u"resource:")) {
                locator_text.insert(0U, u"resource:");
            }
            auto locator = make_string(machine, std::move(locator_text));
            if (!locator) return std::unexpected(locator.error());
            auto locator_root = machine.pin_native_root(*locator);
            if (!locator_root) return std::unexpected(locator_root.error());
            auto player = create_player_from_locator(machine, *locator);
            if (!player) return std::unexpected(player.error());
            auto a = set_vendor_field(machine, *clip, kSamsungAudioClip,
                                      "type", "I", Value::from_int(*type));
            auto b = set_vendor_field(machine, *clip, kSamsungAudioClip,
                                      "resource", "Ljava/lang/String;",
                                      Value::from_reference(*resource));
            auto c = set_vendor_field(machine, *clip, kSamsungAudioClip,
                                      "player", "Ljavax/microedition/media/Player;",
                                      Value::from_reference(*player));
            if (!a) return std::unexpected(a.error());
            if (!b) return std::unexpected(b.error());
            if (!c) return std::unexpected(c.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kSamsungAudioClip), "<init>", "(I[BII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto clip = reference_argument(arguments, 0U, false);
            auto type = int_argument(arguments, 1U, "Samsung AudioClip.<init>");
            auto source = reference_argument(arguments, 2U, false);
            auto offset = int_argument(arguments, 3U, "Samsung AudioClip.<init>");
            auto length = int_argument(arguments, 4U, "Samsung AudioClip.<init>");
            if (!clip) return std::unexpected(clip.error());
            if (!type) return std::unexpected(type.error());
            if (!source) return std::unexpected(source.error());
            if (!offset) return std::unexpected(offset.error());
            if (!length) return std::unexpected(length.error());
            auto source_length = machine.heap().array_length(*source);
            if (!source_length) return std::unexpected(source_length.error());
            if (*offset < 0 || *length < 0 ||
                static_cast<usize>(*offset) > *source_length ||
                static_cast<usize>(*length) >
                    *source_length - static_cast<usize>(*offset)) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Samsung AudioClip byte range is invalid");
            }
            auto bytes = machine.heap().read_byte_array(
                *source, static_cast<usize>(*offset),
                static_cast<usize>(*length));
            if (!bytes) return std::unexpected(bytes.error());
            auto data = machine.heap().allocate_array("[B", bytes->size(),
                                                      Value::from_int(0));
            if (!data) return std::unexpected(data.error());
            auto written = machine.heap().write_byte_array(*data, 0U, *bytes);
            if (!written) return std::unexpected(written.error());
            auto data_root = machine.pin_native_root(*data);
            if (!data_root) return std::unexpected(data_root.error());
            auto content_type = samsung_content_type(machine, *type);
            if (!content_type) return std::unexpected(content_type.error());
            std::optional<NativeRootScope> content_type_root;
            if (!content_type->is_null()) {
                auto root = machine.pin_native_root(*content_type);
                if (!root) return std::unexpected(root.error());
                content_type_root.emplace(std::move(*root));
            }
            auto player = create_player_from_bytes(
                machine, *data, *content_type);
            if (!player) return std::unexpected(player.error());
            auto a = set_vendor_field(machine, *clip, kSamsungAudioClip,
                                      "type", "I", Value::from_int(*type));
            auto b = set_vendor_field(machine, *clip, kSamsungAudioClip,
                                      "data", "[B", Value::from_reference(*data));
            auto c = set_vendor_field(machine, *clip, kSamsungAudioClip,
                                      "player", "Ljavax/microedition/media/Player;",
                                      Value::from_reference(*player));
            if (!a) return std::unexpected(a.error());
            if (!b) return std::unexpected(b.error());
            if (!c) return std::unexpected(c.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kSamsungAudioClip), "play", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto clip = reference_argument(arguments, 0U, false);
            auto loop = int_argument(arguments, 1U, "Samsung AudioClip.play");
            auto volume = int_argument(arguments, 2U, "Samsung AudioClip.play");
            if (!clip) return std::unexpected(clip.error());
            if (!loop) return std::unexpected(loop.error());
            if (!volume) return std::unexpected(volume.error());
            auto player = vendor_reference_field(
                machine, *clip, kSamsungAudioClip, "player",
                "Ljavax/microedition/media/Player;");
            if (!player) return std::unexpected(player.error());
            const Value loop_value = Value::from_int(*loop <= 0 ? -1 : *loop);
            auto configured = invoke_player_void(
                machine, *player, "setLoopCount", "(I)V",
                std::span<const Value>(&loop_value, 1U));
            if (!configured) return std::unexpected(configured.error());
            auto volume_set = set_player_volume(machine, *player, *volume);
            if (!volume_set) return std::unexpected(volume_set.error());
            auto started = invoke_player_void(machine, *player, "start", "()V");
            if (!started) return std::unexpected(started.error());
            return std::optional<Value> {};
        });
    for (const auto& [method, player_method] : {
             std::pair<std::string_view, std::string_view>{"stop", "stop"},
             {"pause", "stop"}, {"resume", "start"}}) {
        add(registry, std::string(kSamsungAudioClip), std::string(method), "()V",
            [player_method](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto clip = reference_argument(arguments, 0U, false);
                if (!clip) return std::unexpected(clip.error());
                auto player = vendor_reference_field(
                    machine, *clip, kSamsungAudioClip, "player",
                    "Ljavax/microedition/media/Player;");
                if (!player) return std::unexpected(player.error());
                auto status = invoke_player_void(machine, *player, player_method,
                                                 "()V");
                if (!status) return std::unexpected(status.error());
                return std::optional<Value> {};
            });
    }

    add(registry, std::string(kSprintClip), "<init>",
        "(Ljava/lang/String;Ljava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto clip = reference_argument(arguments, 0U, false);
            auto resource = reference_argument(arguments, 1U, false);
            auto content_type = reference_argument(arguments, 2U, true);
            auto priority = int_argument(arguments, 3U, "Sprint Clip.<init>");
            auto vibration = int_argument(arguments, 4U, "Sprint Clip.<init>");
            if (!clip) return std::unexpected(clip.error());
            if (!resource) return std::unexpected(resource.error());
            if (!content_type) return std::unexpected(content_type.error());
            if (!priority) return std::unexpected(priority.error());
            if (!vibration) return std::unexpected(vibration.error());
            auto text = machine.heap().string_value(*resource);
            if (!text) return std::unexpected(text.error());
            std::u16string locator_text = *text;
            if (!locator_text.starts_with(u"resource:")) {
                locator_text.insert(0U, u"resource:");
            }
            auto locator = make_string(machine, std::move(locator_text));
            if (!locator) return std::unexpected(locator.error());
            auto player = create_player_from_locator(machine, *locator);
            if (!player) return std::unexpected(player.error());
            const std::array<Status, 5> stored {{
                set_vendor_field(machine, *clip, kSprintClip, "resource",
                                 "Ljava/lang/String;", Value::from_reference(*resource)),
                set_vendor_field(machine, *clip, kSprintClip, "contentType",
                                 "Ljava/lang/String;", Value::from_reference(*content_type)),
                set_vendor_field(machine, *clip, kSprintClip, "priority", "I",
                                 Value::from_int(*priority)),
                set_vendor_field(machine, *clip, kSprintClip, "vibration", "I",
                                 Value::from_int(*vibration)),
                set_vendor_field(machine, *clip, kSprintClip, "player",
                                 "Ljavax/microedition/media/Player;",
                                 Value::from_reference(*player)),
            }};
            for (const auto& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            return std::optional<Value> {};
        });
    add(registry, std::string(kSprintClip), "<init>",
        "([BLjava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto clip = reference_argument(arguments, 0U, false);
            auto data = reference_argument(arguments, 1U, false);
            auto content_type = reference_argument(arguments, 2U, true);
            auto priority = int_argument(arguments, 3U, "Sprint Clip.<init>");
            auto vibration = int_argument(arguments, 4U, "Sprint Clip.<init>");
            if (!clip) return std::unexpected(clip.error());
            if (!data) return std::unexpected(data.error());
            if (!content_type) return std::unexpected(content_type.error());
            if (!priority) return std::unexpected(priority.error());
            if (!vibration) return std::unexpected(vibration.error());
            auto player = create_player_from_bytes(machine, *data, *content_type);
            if (!player) return std::unexpected(player.error());
            const std::array<Status, 4> stored {{
                set_vendor_field(machine, *clip, kSprintClip, "contentType",
                                 "Ljava/lang/String;", Value::from_reference(*content_type)),
                set_vendor_field(machine, *clip, kSprintClip, "priority", "I",
                                 Value::from_int(*priority)),
                set_vendor_field(machine, *clip, kSprintClip, "vibration", "I",
                                 Value::from_int(*vibration)),
                set_vendor_field(machine, *clip, kSprintClip, "player",
                                 "Ljavax/microedition/media/Player;",
                                 Value::from_reference(*player)),
            }};
            for (const auto& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            return std::optional<Value> {};
        });
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"getPriority", "priority"},
             {"getVibration", "vibration"}}) {
        add(registry, std::string(kSprintClip), std::string(method), "()I",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto clip = reference_argument(arguments, 0U, false);
                if (!clip) return std::unexpected(clip.error());
                auto value = vendor_integer_field(machine, *clip, kSprintClip, field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    }
    add(registry, std::string(kSprintClip), "getPlayer",
        "()Ljavax/microedition/media/Player;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto clip = reference_argument(arguments, 0U, false);
            if (!clip) return std::unexpected(clip.error());
            auto player = vendor_reference_field(machine, *clip, kSprintClip,
                                                 "player",
                                                 "Ljavax/microedition/media/Player;");
            if (!player) return std::unexpected(player.error());
            return std::optional<Value>(Value::from_reference(*player));
        });
    for (const std::string_view method : {"play", "playBackground"}) {
        const std::string_view channel = method == "play"
            ? "foregroundPlayer"
            : "backgroundPlayer";
        add(registry, std::string(kSprintPlayer), std::string(method),
            "(Lcom/sprintpcs/media/Clip;I)V",
            [channel](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto clip = reference_argument(arguments, 0U, false);
                auto repeat = int_argument(arguments, 1U, "Sprint Player.play");
                if (!clip) return std::unexpected(clip.error());
                if (!repeat) return std::unexpected(repeat.error());
                if (*repeat < -1) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Sprint repeat count must be -1 or greater");
                }
                auto player = vendor_reference_field(
                    machine, *clip, kSprintClip, "player",
                    "Ljavax/microedition/media/Player;");
                if (!player) return std::unexpected(player.error());
                auto stopped = stop_sprint_player(machine, channel);
                if (!stopped) return std::unexpected(stopped.error());
                const i32 mmapi_loops = *repeat == -1 ? -1 : *repeat + 1;
                const Value loop = Value::from_int(mmapi_loops);
                auto configured = invoke_player_void(
                    machine, *player, "setLoopCount", "(I)V",
                    std::span<const Value>(&loop, 1U));
                if (!configured) return std::unexpected(configured.error());
                auto stored = set_vendor_static_reference_field(
                    machine, kSprintPlayer, channel,
                    "Ljavax/microedition/media/Player;", *player);
                if (!stored) return std::unexpected(stored.error());
                auto vibration = vendor_integer_field(
                    machine, *clip, kSprintClip, "vibration");
                if (!vibration) return std::unexpected(vibration.error());
                if (*vibration > 0) trigger_vibration(*vibration);
                auto started = invoke_player_void(machine, *player, "start", "()V");
                if (!started) {
                    (void)set_vendor_static_reference_field(
                        machine, kSprintPlayer, channel,
                        "Ljavax/microedition/media/Player;", {});
                    return std::unexpected(started.error());
                }
                return std::optional<Value> {};
            });
    }
    add(registry, std::string(kSprintPlayer), "stop", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto foreground = stop_sprint_player(machine, "foregroundPlayer");
            auto background = stop_sprint_player(machine, "backgroundPlayer");
            if (!foreground) return std::unexpected(foreground.error());
            if (!background) return std::unexpected(background.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kSiemensSms), "send",
        "(Ljava/lang/String;Ljava/lang/String;)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto number = reference_argument(arguments, 0U, false);
            auto message = reference_argument(arguments, 1U, false);
            if (!number) return std::unexpected(number.error());
            if (!message) return std::unexpected(message.error());
            // iOS does not permit silent SMS transmission. Siemens callers use
            // this integer as a handset status code; zero is the stable
            // unsupported/cancelled result and keeps fallback WMA logic usable.
            return std::optional<Value>(Value::from_int(0));
        });

    add(registry, std::string(kVodafoneImageEncoder), "createEncoder",
        "(I)Lcom/vodafone/util/ImageEncoder;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto type = int_argument(arguments, 0U, "ImageEncoder.createEncoder");
            if (!type) return std::unexpected(type.error());
            auto encoder = allocate_instance(machine, kVodafoneImageEncoder);
            if (!encoder) return std::unexpected(encoder.error());
            auto stored = set_vendor_field(machine, *encoder,
                                           kVodafoneImageEncoder, "imageType",
                                           "I", Value::from_int(*type));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*encoder));
        });
    add(registry, std::string(kVodafoneImageEncoder), "encodeOffscreen",
        "(Ljavax/microedition/lcdui/Image;IIII)[B",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto encoder = reference_argument(arguments, 0U, false);
            auto image = reference_argument(arguments, 1U, false);
            auto x = int_argument(arguments, 2U, "ImageEncoder.encodeOffscreen");
            auto y = int_argument(arguments, 3U, "ImageEncoder.encodeOffscreen");
            auto width = int_argument(arguments, 4U, "ImageEncoder.encodeOffscreen");
            auto height = int_argument(arguments, 5U, "ImageEncoder.encodeOffscreen");
            if (!encoder) return std::unexpected(encoder.error());
            if (!image) return std::unexpected(image.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!width) return std::unexpected(width.error());
            if (!height) return std::unexpected(height.error());
            auto type = vendor_integer_field(
                machine, *encoder, kVodafoneImageEncoder, "imageType");
            if (!type) return std::unexpected(type.error());
            if (*type != 0) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto payload = machine.graphics().image(image->bits);
            if (!payload) return std::unexpected(payload.error());
            auto png = encode_png_region(**payload, *x, *y, *width, *height);
            if (!png) return std::unexpected(png.error());
            auto bytes = machine.heap().allocate_array(
                "[B", png->size(), Value::from_int(0));
            if (!bytes) return std::unexpected(bytes.error());
            auto written = machine.heap().write_byte_array(*bytes, 0U, *png);
            if (!written) return std::unexpected(written.error());
            return std::optional<Value>(Value::from_reference(*bytes));
        });

    add(registry, std::string(kMidpScheduler), "getScheduler",
        "()Lcom/sun/midp/midlet/Scheduler;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto scheduler = allocate_instance(machine, kMidpScheduler);
            if (!scheduler) return std::unexpected(scheduler.error());
            return std::optional<Value>(Value::from_reference(*scheduler));
        });
    add(registry, std::string(kMidpScheduler), "getActiveMIDlet",
        "()Ljavax/microedition/midlet/MIDlet;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto scheduler = reference_argument(arguments, 0U, false);
            if (!scheduler) return std::unexpected(scheduler.error());
            auto owner = vendor_static_reference_field(
                machine, "javax/microedition/lcdui/Display", "ownerMidlet",
                "Ljavax/microedition/midlet/MIDlet;");
            if (!owner) return std::unexpected(owner.error());
            return std::optional<Value>(Value::from_reference(*owner));
        });

    register_noop_constructor(registry, kSiemensLight);
    for (const std::string_view method : {"setLightOn", "setLightOff"}) {
        add(registry, std::string(kSiemensLight), std::string(method), "()V",
            [enabled = method == "setLightOn"](
                Machine&, std::span<const Value>)
                -> Result<std::optional<Value>> {
                set_backlight(enabled);
                return std::optional<Value> {};
            });
    }

    register_noop_constructor(registry, kSiemensVibrator);
    add(registry, std::string(kSiemensVibrator), "startVibrator", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            set_vibration(true);
            return std::optional<Value> {};
        });
    add(registry, std::string(kSiemensVibrator), "stopVibrator", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            set_vibration(false);
            return std::optional<Value> {};
        });
    add(registry, std::string(kSiemensVibrator), "triggerVibrator", "(I)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto duration = int_argument(
                arguments, 0U, "Siemens Vibrator.triggerVibrator");
            if (!duration) return std::unexpected(duration.error());
            if (*duration < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Siemens vibration duration is negative");
            }
            trigger_vibration(*duration);
            return std::optional<Value> {};
        });

    register_noop_constructor(registry, kSamsungLight);
    add(registry, std::string(kSamsungLight), "on", "(I)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto duration = int_argument(
                arguments, 0U, "Samsung LCDLight.on");
            if (!duration) return std::unexpected(duration.error());
            if (*duration < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Samsung light duration is negative");
            }
            if (*duration == 0) {
                set_backlight(true);
            } else {
                flash_backlight(*duration);
            }
            return std::optional<Value> {};
        });
    add(registry, std::string(kSamsungLight), "off", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            set_backlight(false);
            return std::optional<Value> {};
        });

    register_noop_constructor(registry, kMotorolaLighting);
    for (const std::string_view method : {"backlightOn", "backlightOff"}) {
        add(registry, std::string(kMotorolaLighting), std::string(method), "()V",
            [enabled = method == "backlightOn"](
                Machine&, std::span<const Value>)
                -> Result<std::optional<Value>> {
                set_backlight(enabled);
                return std::optional<Value> {};
            });
    }
}

} // namespace phoneme::vm
