#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "phoneme/media/MediaAdapter.hpp"

namespace phoneme::media {

enum class PlayerState : i32 {
    closed = 0,
    unrealized = 100,
    realized = 200,
    prefetched = 300,
    started = 400,
};

enum class MediaEventKind : u8 {
    started,
    stopped,
    end_of_media,
    closed,
    error,
    volume_changed,
};

struct MediaEvent final {
    MediaEventKind kind {MediaEventKind::started};
    i32 player_id {0};
    i64 media_time {0};
    std::string detail;
};

struct PlayerSnapshot final {
    PlayerState state {PlayerState::closed};
    i64 media_time {-1};
    i64 duration {-1};
    i32 loop_count {1};
    i32 volume {100};
    bool muted {false};
    std::string content_type;
};

class MediaService final {
public:
    MediaService();
    explicit MediaService(std::unique_ptr<MediaAdapter> adapter);
    ~MediaService();

    MediaService(const MediaService&) = delete;
    MediaService& operator=(const MediaService&) = delete;

    [[nodiscard]] Result<i32> create_locator(std::string locator,
                                             std::string content_type);
    [[nodiscard]] Result<i32> create_data(std::vector<u8> data,
                                          std::string content_type);

    [[nodiscard]] Status realize(i32 player_id);
    [[nodiscard]] Status prefetch(i32 player_id);
    [[nodiscard]] Result<std::optional<MediaEvent>> start(i32 player_id);
    [[nodiscard]] Result<std::optional<MediaEvent>> stop(i32 player_id);
    [[nodiscard]] Status deallocate(i32 player_id);
    [[nodiscard]] Result<std::optional<MediaEvent>> close(i32 player_id);

    [[nodiscard]] Status set_loop_count(i32 player_id, i32 count);
    [[nodiscard]] Status set_volume(i32 player_id, i32 level);
    [[nodiscard]] Status set_mute(i32 player_id, bool muted);
    [[nodiscard]] Result<i64> set_media_time(i32 player_id,
                                            i64 microseconds);
    [[nodiscard]] Result<PlayerSnapshot> snapshot(i32 player_id);
    [[nodiscard]] Result<std::optional<MediaEvent>> synchronize(i32 player_id);
    [[nodiscard]] Status set_tone_sequence(i32 player_id,
                                           std::vector<u8> sequence);
    [[nodiscard]] Result<bool> play_tone(i32 note,
                                        i32 duration_milliseconds,
                                        i32 volume);

    void suspend() noexcept;
    void resume() noexcept;
    void close_all() noexcept;

private:
    enum class SourceKind : u8 {
        locator,
        data,
    };

    struct Player final {
        i32 id {0};
        i32 adapter_handle {0};
        SourceKind source_kind {SourceKind::locator};
        PlayerState state {PlayerState::unrealized};
        std::string locator;
        std::vector<u8> data;
        std::string content_type;
        std::vector<u8> tone_sequence;
        i32 loop_count {1};
        i32 volume {100};
        bool muted {false};
        bool resume_after_suspend {false};
    };

    [[nodiscard]] Result<Player*> player_unlocked(i32 player_id);
    [[nodiscard]] Status realize_unlocked(Player& player);
    [[nodiscard]] Status ensure_tone_handle_unlocked(Player& player);
    [[nodiscard]] Result<std::optional<MediaEvent>>
    synchronize_unlocked(Player& player);
    [[nodiscard]] Result<std::vector<u8>> build_tone_wave(
        const Player& player) const;
    void close_unlocked(Player& player) noexcept;

    std::unique_ptr<MediaAdapter> adapter_;
    std::mutex mutex_;
    std::unordered_map<i32, Player> players_;
    i32 next_player_id_ {1};
    bool suspended_ {false};
};

} // namespace phoneme::media
