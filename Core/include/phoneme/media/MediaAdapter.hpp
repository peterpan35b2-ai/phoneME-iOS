#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "phoneme/base/Error.hpp"

namespace phoneme::media {

class MediaAdapter {
public:
    virtual ~MediaAdapter() = default;

    MediaAdapter(const MediaAdapter&) = delete;
    MediaAdapter& operator=(const MediaAdapter&) = delete;

    [[nodiscard]] virtual Result<i32> create_data(
        std::span<const u8> data,
        std::string_view content_type) = 0;
    [[nodiscard]] virtual Result<i32> create_locator(
        std::string_view locator,
        std::string_view content_type) = 0;
    [[nodiscard]] virtual Status start(i32 handle) = 0;
    [[nodiscard]] virtual Status stop(i32 handle) = 0;
    virtual void close(i32 handle) noexcept = 0;
    [[nodiscard]] virtual Status set_loop_count(i32 handle, i32 count) = 0;
    [[nodiscard]] virtual Status set_volume(i32 handle, i32 level) = 0;
    [[nodiscard]] virtual Status set_mute(i32 handle, bool muted) = 0;
    [[nodiscard]] virtual Result<i64> set_media_time(i32 handle,
                                                    i64 microseconds) = 0;
    [[nodiscard]] virtual Result<i64> media_time(i32 handle) = 0;
    [[nodiscard]] virtual Result<i64> duration(i32 handle) = 0;
    [[nodiscard]] virtual Result<bool> is_playing(i32 handle) = 0;
    [[nodiscard]] virtual Result<bool> has_ended(i32 handle) = 0;
    [[nodiscard]] virtual Result<bool> has_error(i32 handle) = 0;
    [[nodiscard]] virtual Result<bool> play_tone(i32 note,
                                                i32 duration_milliseconds,
                                                i32 volume) = 0;

protected:
    MediaAdapter() = default;
};

[[nodiscard]] std::unique_ptr<MediaAdapter> make_platform_media_adapter();

} // namespace phoneme::media
