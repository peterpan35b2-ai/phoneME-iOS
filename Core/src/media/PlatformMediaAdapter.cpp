#include "phoneme/media/MediaAdapter.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>

#if defined(PHONEME_WEB)
#include <emscripten.h>
#define PHONEME_WEAK_IMPORT
#define PHONEME_MEDIA_HOST_FALLBACK 0
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define PHONEME_WEAK_IMPORT __attribute__((weak_import))
#define PHONEME_MEDIA_HOST_FALLBACK 0
#else
#define PHONEME_WEAK_IMPORT
#define PHONEME_MEDIA_HOST_FALLBACK 1
#endif
#else
#define PHONEME_WEAK_IMPORT
#define PHONEME_MEDIA_HOST_FALLBACK 1
#endif

extern "C" {
int32_t phoneme_ios_media_create_data(const uint8_t* data,
                                      int32_t length,
                                      const char* content_type)
    PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_create_locator(const char* locator,
                                         const char* content_type)
    PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_start(int32_t handle) PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_stop(int32_t handle) PHONEME_WEAK_IMPORT;
void phoneme_ios_media_close(int32_t handle) PHONEME_WEAK_IMPORT;
void phoneme_ios_media_set_loop_count(int32_t handle,
                                      int32_t count) PHONEME_WEAK_IMPORT;
void phoneme_ios_media_set_volume(int32_t handle,
                                  int32_t level) PHONEME_WEAK_IMPORT;
void phoneme_ios_media_set_mute(int32_t handle,
                                int32_t muted) PHONEME_WEAK_IMPORT;
int64_t phoneme_ios_media_set_time(int32_t handle,
                                   int64_t microseconds) PHONEME_WEAK_IMPORT;
int64_t phoneme_ios_media_get_time(int32_t handle) PHONEME_WEAK_IMPORT;
int64_t phoneme_ios_media_get_duration(int32_t handle) PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_is_playing(int32_t handle) PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_has_ended(int32_t handle) PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_has_error(int32_t handle) PHONEME_WEAK_IMPORT;
int32_t phoneme_ios_media_play_tone(int32_t note,
                                    int32_t duration_milliseconds,
                                    int32_t volume) PHONEME_WEAK_IMPORT;
}

#if defined(PHONEME_WEB)
extern "C" {
int32_t phoneme_ios_media_create_data(const uint8_t* data,
                                      int32_t length,
                                      const char* content_type) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        if (!bridge || !$0 || $1 <= 0 || $1 > 64 * 1024 * 1024) return 0;
        const bytes = HEAPU8.slice($0, $0 + $1);
        return bridge.createData(bytes, UTF8ToString($2)) | 0;
    }, data, length, content_type);
}

int32_t phoneme_ios_media_create_locator(const char* locator,
                                         const char* content_type) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        if (!bridge || !$0) return 0;
        return bridge.createLocator(UTF8ToString($0), UTF8ToString($1)) | 0;
    }, locator, content_type);
}

int32_t phoneme_ios_media_start(int32_t handle) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? (bridge.start($0) | 0) : 0;
    }, handle);
}

int32_t phoneme_ios_media_stop(int32_t handle) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? (bridge.stop($0) | 0) : 0;
    }, handle);
}

void phoneme_ios_media_close(int32_t handle) {
    MAIN_THREAD_EM_ASM({
        globalThis.__phoneMEMediaBridge?.close($0);
    }, handle);
}

void phoneme_ios_media_set_loop_count(int32_t handle, int32_t count) {
    MAIN_THREAD_EM_ASM({
        globalThis.__phoneMEMediaBridge?.setLoopCount($0, $1);
    }, handle, count);
}

void phoneme_ios_media_set_volume(int32_t handle, int32_t level) {
    MAIN_THREAD_EM_ASM({
        globalThis.__phoneMEMediaBridge?.setVolume($0, $1);
    }, handle, level);
}

void phoneme_ios_media_set_mute(int32_t handle, int32_t muted) {
    MAIN_THREAD_EM_ASM({
        globalThis.__phoneMEMediaBridge?.setMute($0, $1);
    }, handle, muted);
}

int64_t phoneme_ios_media_set_time(int32_t handle, int64_t microseconds) {
    const double value = MAIN_THREAD_EM_ASM_DOUBLE({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? Number(bridge.setTime($0, $1)) : -1;
    }, handle, static_cast<double>(microseconds));
    return static_cast<int64_t>(value);
}

int64_t phoneme_ios_media_get_time(int32_t handle) {
    const double value = MAIN_THREAD_EM_ASM_DOUBLE({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? Number(bridge.getTime($0)) : -1;
    }, handle);
    return static_cast<int64_t>(value);
}

int64_t phoneme_ios_media_get_duration(int32_t handle) {
    const double value = MAIN_THREAD_EM_ASM_DOUBLE({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? Number(bridge.getDuration($0)) : -1;
    }, handle);
    return static_cast<int64_t>(value);
}

int32_t phoneme_ios_media_is_playing(int32_t handle) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? (bridge.isPlaying($0) | 0) : 0;
    }, handle);
}

int32_t phoneme_ios_media_has_ended(int32_t handle) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? (bridge.hasEnded($0) | 0) : 0;
    }, handle);
}

int32_t phoneme_ios_media_has_error(int32_t handle) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? (bridge.hasError($0) | 0) : 1;
    }, handle);
}

int32_t phoneme_ios_media_play_tone(int32_t note,
                                    int32_t duration_milliseconds,
                                    int32_t volume) {
    return MAIN_THREAD_EM_ASM_INT({
        const bridge = globalThis.__phoneMEMediaBridge;
        return bridge ? (bridge.playTone($0, $1, $2) | 0) : 0;
    }, note, duration_milliseconds, volume);
}
}
#endif

#if PHONEME_MEDIA_HOST_FALLBACK
extern "C" {
int32_t phoneme_ios_media_create_data(const uint8_t*, int32_t, const char*) {
    return 0;
}
int32_t phoneme_ios_media_create_locator(const char*, const char*) {
    return 0;
}
int32_t phoneme_ios_media_start(int32_t) { return 0; }
int32_t phoneme_ios_media_stop(int32_t) { return 0; }
void phoneme_ios_media_close(int32_t) {}
void phoneme_ios_media_set_loop_count(int32_t, int32_t) {}
void phoneme_ios_media_set_volume(int32_t, int32_t) {}
void phoneme_ios_media_set_mute(int32_t, int32_t) {}
int64_t phoneme_ios_media_set_time(int32_t, int64_t) { return -1; }
int64_t phoneme_ios_media_get_time(int32_t) { return -1; }
int64_t phoneme_ios_media_get_duration(int32_t) { return -1; }
int32_t phoneme_ios_media_is_playing(int32_t) { return 0; }
int32_t phoneme_ios_media_has_ended(int32_t) { return 0; }
int32_t phoneme_ios_media_has_error(int32_t) { return 0; }
int32_t phoneme_ios_media_play_tone(int32_t, int32_t, int32_t) { return 0; }
}
#endif

namespace phoneme::media {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] u32 read_u32_le(const u8* bytes) noexcept {
    return static_cast<u32>(bytes[0]) |
           (static_cast<u32>(bytes[1]) << 8U) |
           (static_cast<u32>(bytes[2]) << 16U) |
           (static_cast<u32>(bytes[3]) << 24U);
}

[[nodiscard]] i64 wav_duration(std::span<const u8> data) noexcept {
    if (data.size() < 44U ||
        std::memcmp(data.data(), "RIFF", 4U) != 0 ||
        std::memcmp(data.data() + 8U, "WAVE", 4U) != 0) {
        return -1;
    }

    u32 byte_rate = 0;
    u32 data_size = 0;
    usize offset = 12U;
    while (offset + 8U <= data.size()) {
        const u8* chunk = data.data() + offset;
        const u32 length = read_u32_le(chunk + 4U);
        const usize payload = offset + 8U;
        if (payload > data.size() ||
            static_cast<usize>(length) > data.size() - payload) {
            break;
        }
        if (std::memcmp(chunk, "fmt ", 4U) == 0 && length >= 12U) {
            byte_rate = read_u32_le(data.data() + payload + 8U);
        } else if (std::memcmp(chunk, "data", 4U) == 0) {
            data_size = length;
        }
        const usize padded = static_cast<usize>(length) +
                             (static_cast<usize>(length) & 1U);
        if (padded > data.size() - payload) {
            break;
        }
        offset = payload + padded;
    }
    if (byte_rate == 0U || data_size == 0U) {
        return -1;
    }
    return static_cast<i64>((static_cast<u64>(data_size) * 1'000'000ULL) /
                            static_cast<u64>(byte_rate));
}

class PlatformMediaAdapter final : public MediaAdapter {
public:
    [[nodiscard]] Result<i32> create_data(
        std::span<const u8> data,
        std::string_view content_type) override {
        if (data.empty()) {
            return fail(ErrorCode::invalid_argument, "media data is empty");
        }
        if (data.size() > static_cast<usize>(INT32_MAX)) {
            return fail(ErrorCode::out_of_range, "media data exceeds bridge limit");
        }
        if (bridge_available()) {
            const std::string type(content_type);
            const i32 handle = phoneme_ios_media_create_data(
                data.data(), static_cast<i32>(data.size()), type.c_str());
            if (handle == 0) {
                return fail(ErrorCode::io_error,
                            "AVFoundation rejected media data");
            }
            return handle;
        }
        return create_fallback(wav_duration(data));
    }

    [[nodiscard]] Result<i32> create_locator(
        std::string_view locator,
        std::string_view content_type) override {
        if (locator.empty()) {
            return fail(ErrorCode::invalid_argument, "media locator is empty");
        }
        if (bridge_available()) {
            const std::string locator_text(locator);
            const std::string type(content_type);
            const i32 handle = phoneme_ios_media_create_locator(
                locator_text.c_str(), type.c_str());
            if (handle == 0) {
                return fail(ErrorCode::io_error,
                            "AVFoundation rejected media locator");
            }
            return handle;
        }
        return create_fallback(-1);
    }

    [[nodiscard]] Status start(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_start(handle) != 0
                ? Status {}
                : fail(ErrorCode::io_error, "AVFoundation failed to start player");
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        entry.value()->playing = true;
        entry.value()->ended = false;
        entry.value()->started_at = Clock::now();
        return {};
    }

    [[nodiscard]] Status stop(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_stop(handle) != 0
                ? Status {}
                : fail(ErrorCode::io_error, "AVFoundation failed to stop player");
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        update_time(*entry.value());
        entry.value()->playing = false;
        return {};
    }

    void close(i32 handle) noexcept override {
        if (bridge_available()) {
            phoneme_ios_media_close(handle);
            return;
        }
        fallback_.erase(handle);
    }

    [[nodiscard]] Status set_loop_count(i32 handle, i32 count) override {
        if (bridge_available()) {
            phoneme_ios_media_set_loop_count(handle, count);
            return {};
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        entry.value()->loop_count = count;
        return {};
    }

    [[nodiscard]] Status set_volume(i32 handle, i32 level) override {
        if (bridge_available()) {
            phoneme_ios_media_set_volume(handle, level);
            return {};
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        entry.value()->volume = level;
        return {};
    }

    [[nodiscard]] Status set_mute(i32 handle, bool muted) override {
        if (bridge_available()) {
            phoneme_ios_media_set_mute(handle, muted ? 1 : 0);
            return {};
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        entry.value()->muted = muted;
        return {};
    }

    [[nodiscard]] Result<i64> set_media_time(i32 handle,
                                            i64 microseconds) override {
        if (bridge_available()) {
            return phoneme_ios_media_set_time(handle, microseconds);
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        entry.value()->media_time = std::max<i64>(0, microseconds);
        entry.value()->started_at = Clock::now();
        return entry.value()->media_time;
    }

    [[nodiscard]] Result<i64> media_time(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_get_time(handle);
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        update_time(*entry.value());
        return entry.value()->media_time;
    }

    [[nodiscard]] Result<i64> duration(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_get_duration(handle);
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        return entry.value()->duration;
    }

    [[nodiscard]] Result<bool> is_playing(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_is_playing(handle) != 0;
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        update_time(*entry.value());
        return entry.value()->playing;
    }

    [[nodiscard]] Result<bool> has_ended(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_has_ended(handle) != 0;
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        update_time(*entry.value());
        return entry.value()->ended;
    }

    [[nodiscard]] Result<bool> has_error(i32 handle) override {
        if (bridge_available()) {
            return phoneme_ios_media_has_error(handle) != 0;
        }
        auto entry = fallback(handle);
        if (!entry) return std::unexpected(entry.error());
        return entry.value()->error;
    }

    [[nodiscard]] Result<bool> play_tone(i32 note,
                                        i32 duration_milliseconds,
                                        i32 volume) override {
        if (bridge_available()) {
            return phoneme_ios_media_play_tone(note,
                                               duration_milliseconds,
                                               volume) != 0;
        }
        return note >= 0 && note <= 127 && duration_milliseconds > 0 &&
               volume >= 0 && volume <= 100;
    }

private:
    struct FallbackEntry final {
        i64 duration {-1};
        i64 media_time {0};
        i32 loop_count {1};
        i32 volume {100};
        bool muted {false};
        bool playing {false};
        bool ended {false};
        bool error {false};
        Clock::time_point started_at {};
    };

    [[nodiscard]] static bool bridge_available() noexcept {
#if defined(PHONEME_WEB)
        return true;
#elif PHONEME_MEDIA_HOST_FALLBACK
        return false;
#else
        return phoneme_ios_media_create_data != nullptr &&
               phoneme_ios_media_create_locator != nullptr &&
               phoneme_ios_media_start != nullptr &&
               phoneme_ios_media_stop != nullptr &&
               phoneme_ios_media_close != nullptr &&
               phoneme_ios_media_set_loop_count != nullptr &&
               phoneme_ios_media_set_volume != nullptr &&
               phoneme_ios_media_set_mute != nullptr &&
               phoneme_ios_media_set_time != nullptr &&
               phoneme_ios_media_get_time != nullptr &&
               phoneme_ios_media_get_duration != nullptr &&
               phoneme_ios_media_is_playing != nullptr &&
               phoneme_ios_media_has_ended != nullptr &&
               phoneme_ios_media_has_error != nullptr &&
               phoneme_ios_media_play_tone != nullptr;
#endif
    }

    [[nodiscard]] Result<i32> create_fallback(i64 duration) {
        const i32 handle = next_fallback_handle_++;
        fallback_.insert_or_assign(handle, FallbackEntry {.duration = duration});
        return handle;
    }

    [[nodiscard]] Result<FallbackEntry*> fallback(i32 handle) {
        const auto iterator = fallback_.find(handle);
        if (iterator == fallback_.end()) {
            return fail(ErrorCode::invalid_argument, "unknown media handle");
        }
        return &iterator->second;
    }

    static void update_time(FallbackEntry& entry) noexcept {
        if (!entry.playing) {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - entry.started_at).count();
        entry.started_at = Clock::now();
        if (elapsed > 0) {
            entry.media_time += elapsed;
        }
        if (entry.duration > 0 && entry.media_time >= entry.duration) {
            if (entry.loop_count == -1 || entry.loop_count > 1) {
                if (entry.loop_count > 1) {
                    --entry.loop_count;
                }
                entry.media_time %= entry.duration;
            } else {
                entry.media_time = entry.duration;
                entry.playing = false;
                entry.ended = true;
            }
        }
    }

    std::unordered_map<i32, FallbackEntry> fallback_;
    i32 next_fallback_handle_ {1};
};

} // namespace

std::unique_ptr<MediaAdapter> make_platform_media_adapter() {
    return std::make_unique<PlatformMediaAdapter>();
}

} // namespace phoneme::media
