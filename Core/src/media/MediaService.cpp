#include "phoneme/media/MediaService.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <numbers>
#include <utility>

namespace phoneme::media {
namespace {

constexpr std::string_view kToneContentType = "audio/x-tone-seq";
constexpr i32 kSampleRate = 22'050;
constexpr i32 kMaximumToneSeconds = 600;

[[nodiscard]] i32 clamp_volume(i32 level) noexcept {
    return std::clamp(level, 0, 100);
}

[[nodiscard]] bool is_tone_type(std::string_view type) noexcept {
    return type == kToneContentType;
}

void append_u16_le(std::vector<u8>& output, u16 value) {
    output.push_back(static_cast<u8>(value & 0xFFU));
    output.push_back(static_cast<u8>((value >> 8U) & 0xFFU));
}

void append_u32_le(std::vector<u8>& output, u32 value) {
    output.push_back(static_cast<u8>(value & 0xFFU));
    output.push_back(static_cast<u8>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<u8>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<u8>((value >> 24U) & 0xFFU));
}

struct ToneSegment final {
    i32 note {-1};
    i32 duration_milliseconds {0};
    i32 volume {100};
};

[[nodiscard]] Result<std::vector<ToneSegment>> parse_tone_sequence(
    std::span<const u8> sequence,
    i32 default_volume) {
    constexpr i32 kSilence = -1;
    constexpr i32 kVersion = -2;
    constexpr i32 kTempo = -3;
    constexpr i32 kResolution = -4;
    constexpr i32 kBlockStart = -5;
    constexpr i32 kBlockEnd = -6;
    constexpr i32 kPlayBlock = -7;
    constexpr i32 kSetVolume = -8;
    constexpr i32 kRepeat = -9;

    if (sequence.empty()) {
        return fail(ErrorCode::invalid_argument, "tone sequence is empty");
    }

    i32 tempo = 120;
    i32 resolution = 64;
    i32 volume = clamp_volume(default_volume);
    std::vector<ToneSegment> segments;

    const auto signed_value = [](u8 value) noexcept {
        return static_cast<i32>(static_cast<i8>(value));
    };
    const auto duration_for = [&](i32 units) -> Result<i32> {
        if (units <= 0 || tempo <= 0 || resolution <= 0) {
            return fail(ErrorCode::invalid_argument,
                        "tone sequence contains invalid timing");
        }
        const i64 numerator = 240'000LL * static_cast<i64>(units);
        const i64 denominator = static_cast<i64>(tempo) *
                                static_cast<i64>(resolution);
        const i64 duration = std::max<i64>(1, numerator / denominator);
        if (duration > std::numeric_limits<i32>::max()) {
            return fail(ErrorCode::out_of_range,
                        "tone segment duration exceeds supported range");
        }
        return static_cast<i32>(duration);
    };

    usize index = 0;
    while (index < sequence.size()) {
        const i32 command = signed_value(sequence[index++]);
        if (command >= 0 || command == kSilence) {
            if (index >= sequence.size()) {
                return fail(ErrorCode::invalid_argument,
                            "tone sequence is truncated after note");
            }
            const i32 units = static_cast<i32>(sequence[index++]);
            auto duration = duration_for(units);
            if (!duration) return std::unexpected(duration.error());
            segments.push_back(ToneSegment {
                .note = command,
                .duration_milliseconds = *duration,
                .volume = volume,
            });
            continue;
        }

        if (command == kVersion || command == kTempo ||
            command == kResolution || command == kSetVolume ||
            command == kBlockStart || command == kPlayBlock) {
            if (index >= sequence.size()) {
                return fail(ErrorCode::invalid_argument,
                            "tone sequence is truncated after command");
            }
            const i32 argument = static_cast<i32>(sequence[index++]);
            if (command == kTempo) {
                tempo = std::max(1, argument * 4);
            } else if (command == kResolution) {
                resolution = std::max(1, argument);
            } else if (command == kSetVolume) {
                volume = clamp_volume(argument);
            }
            continue;
        }

        if (command == kRepeat) {
            if (index + 2U > sequence.size()) {
                return fail(ErrorCode::invalid_argument,
                            "tone repeat command is truncated");
            }
            const i32 repeat_count = static_cast<i32>(sequence[index++]);
            const i32 repeated_note = signed_value(sequence[index++]);
            if (index >= sequence.size()) {
                return fail(ErrorCode::invalid_argument,
                            "tone repeat note is missing duration");
            }
            const i32 units = static_cast<i32>(sequence[index++]);
            auto duration = duration_for(units);
            if (!duration) return std::unexpected(duration.error());
            for (i32 repeat = 0; repeat < repeat_count; ++repeat) {
                segments.push_back(ToneSegment {
                    .note = repeated_note,
                    .duration_milliseconds = *duration,
                    .volume = volume,
                });
            }
            continue;
        }

        if (command == kBlockEnd) {
            continue;
        }

        return fail(ErrorCode::unsupported_feature,
                    "tone sequence contains an unsupported command");
    }

    if (segments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "tone sequence does not contain playable notes");
    }
    return segments;
}

} // namespace

MediaService::MediaService() : MediaService(make_platform_media_adapter()) {}

MediaService::MediaService(std::unique_ptr<MediaAdapter> adapter)
    : adapter_(std::move(adapter)) {
    if (adapter_ == nullptr) {
        std::terminate();
    }
}

MediaService::~MediaService() { close_all(); }

Result<i32> MediaService::create_locator(std::string locator,
                                         std::string content_type) {
    if (locator.empty()) {
        return fail(ErrorCode::invalid_argument, "media locator is empty");
    }
    std::scoped_lock lock(mutex_);
    const i32 id = next_player_id_++;
    players_.insert_or_assign(id, Player {
        .id = id,
        .source_kind = SourceKind::locator,
        .locator = std::move(locator),
        .content_type = std::move(content_type),
    });
    return id;
}

Result<i32> MediaService::create_data(std::vector<u8> data,
                                      std::string content_type) {
    if (data.empty() && !is_tone_type(content_type)) {
        return fail(ErrorCode::invalid_argument, "media data is empty");
    }
    std::scoped_lock lock(mutex_);
    const i32 id = next_player_id_++;
    players_.insert_or_assign(id, Player {
        .id = id,
        .source_kind = SourceKind::data,
        .data = std::move(data),
        .content_type = std::move(content_type),
    });
    return id;
}

Status MediaService::realize(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    return realize_unlocked(**player);
}

Status MediaService::prefetch(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    auto realized = realize_unlocked(**player);
    if (!realized) return realized;
    auto tone_handle = ensure_tone_handle_unlocked(**player);
    if (!tone_handle) return tone_handle;
    if ((*player)->state < PlayerState::prefetched) {
        (*player)->state = PlayerState::prefetched;
    }
    return {};
}

Result<std::optional<MediaEvent>> MediaService::start(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    if ((*player)->state == PlayerState::started) {
        return std::optional<MediaEvent> {};
    }
    auto realized = realize_unlocked(**player);
    if (!realized) return std::unexpected(realized.error());
    auto tone_handle = ensure_tone_handle_unlocked(**player);
    if (!tone_handle) return std::unexpected(tone_handle.error());
    if ((*player)->state < PlayerState::prefetched) {
        (*player)->state = PlayerState::prefetched;
    }

    if ((*player)->adapter_handle == 0) {
        return fail(ErrorCode::invalid_state,
                    "player has no platform media handle");
    }
    if (!suspended_) {
        auto started = adapter_->start((*player)->adapter_handle);
        if (!started) return std::unexpected(started.error());
    } else {
        (*player)->resume_after_suspend = true;
    }
    (*player)->state = PlayerState::started;
    return std::optional<MediaEvent>(MediaEvent {
        .kind = MediaEventKind::started,
        .player_id = player_id,
        .media_time = 0,
    });
}

Result<std::optional<MediaEvent>> MediaService::stop(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    if ((*player)->state != PlayerState::started) {
        return std::optional<MediaEvent> {};
    }
    i64 media_time = 0;
    if ((*player)->adapter_handle != 0) {
        auto time = adapter_->media_time((*player)->adapter_handle);
        if (time) media_time = *time;
        auto stopped = adapter_->stop((*player)->adapter_handle);
        if (!stopped) return std::unexpected(stopped.error());
    }
    (*player)->state = PlayerState::prefetched;
    (*player)->resume_after_suspend = false;
    return std::optional<MediaEvent>(MediaEvent {
        .kind = MediaEventKind::stopped,
        .player_id = player_id,
        .media_time = media_time,
    });
}

Status MediaService::deallocate(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed ||
        (*player)->state == PlayerState::unrealized) {
        return {};
    }
    if ((*player)->adapter_handle != 0) {
        (void)adapter_->stop((*player)->adapter_handle);
    }
    (*player)->state = PlayerState::realized;
    (*player)->resume_after_suspend = false;
    return {};
}

Result<std::optional<MediaEvent>> MediaService::close(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return std::optional<MediaEvent> {};
    }
    close_unlocked(**player);
    return std::optional<MediaEvent>(MediaEvent {
        .kind = MediaEventKind::closed,
        .player_id = player_id,
    });
}

Status MediaService::set_loop_count(i32 player_id, i32 count) {
    if (count == 0 || count < -1) {
        return fail(ErrorCode::invalid_argument, "invalid player loop count");
    }
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    if ((*player)->state == PlayerState::started) {
        return fail(ErrorCode::invalid_state,
                    "loop count cannot change while player is started");
    }
    (*player)->loop_count = count;
    if ((*player)->adapter_handle != 0) {
        return adapter_->set_loop_count((*player)->adapter_handle, count);
    }
    return {};
}

Status MediaService::set_volume(i32 player_id, i32 level) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    (*player)->volume = clamp_volume(level);
    if ((*player)->adapter_handle != 0) {
        return adapter_->set_volume((*player)->adapter_handle,
                                    (*player)->volume);
    }
    return {};
}

Status MediaService::set_mute(i32 player_id, bool muted) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    (*player)->muted = muted;
    if ((*player)->adapter_handle != 0) {
        return adapter_->set_mute((*player)->adapter_handle, muted);
    }
    return {};
}

Result<i64> MediaService::set_media_time(i32 player_id, i64 microseconds) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state < PlayerState::realized ||
        (*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is not realized");
    }
    auto tone_handle = ensure_tone_handle_unlocked(**player);
    if (!tone_handle) return std::unexpected(tone_handle.error());
    if ((*player)->adapter_handle == 0) {
        return 0;
    }
    return adapter_->set_media_time((*player)->adapter_handle,
                                    std::max<i64>(0, microseconds));
}

Result<PlayerSnapshot> MediaService::snapshot(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    auto synchronized = synchronize_unlocked(**player);
    if (!synchronized) return std::unexpected(synchronized.error());
    if ((*player)->state >= PlayerState::realized &&
        (*player)->state != PlayerState::closed &&
        !(*player)->tone_sequence.empty()) {
        auto tone_handle = ensure_tone_handle_unlocked(**player);
        if (!tone_handle) return std::unexpected(tone_handle.error());
    }

    PlayerSnapshot result {
        .state = (*player)->state,
        .loop_count = (*player)->loop_count,
        .volume = (*player)->volume,
        .muted = (*player)->muted,
        .content_type = (*player)->content_type,
    };
    if ((*player)->adapter_handle != 0 &&
        (*player)->state != PlayerState::closed) {
        auto media_time = adapter_->media_time((*player)->adapter_handle);
        if (media_time) result.media_time = *media_time;
        auto duration = adapter_->duration((*player)->adapter_handle);
        if (duration) result.duration = *duration;
    }
    return result;
}

Result<std::optional<MediaEvent>> MediaService::synchronize(i32 player_id) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    return synchronize_unlocked(**player);
}

Status MediaService::set_tone_sequence(i32 player_id,
                                       std::vector<u8> sequence) {
    std::scoped_lock lock(mutex_);
    auto player = player_unlocked(player_id);
    if (!player) return std::unexpected(player.error());
    if ((*player)->state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    if (!is_tone_type((*player)->content_type)) {
        return fail(ErrorCode::invalid_state,
                    "ToneControl is unavailable for this player");
    }
    if ((*player)->state >= PlayerState::prefetched) {
        return fail(ErrorCode::invalid_state,
                    "tone sequence cannot change after prefetch");
    }
    if ((*player)->adapter_handle != 0) {
        adapter_->close((*player)->adapter_handle);
        (*player)->adapter_handle = 0;
    }
    (*player)->tone_sequence = std::move(sequence);
    return {};
}

Result<bool> MediaService::play_tone(i32 note,
                                     i32 duration_milliseconds,
                                     i32 volume) {
    if (note < 0 || note > 127 || duration_milliseconds <= 0) {
        return fail(ErrorCode::invalid_argument, "invalid tone parameters");
    }
    std::scoped_lock lock(mutex_);
    return adapter_->play_tone(note,
                               duration_milliseconds,
                               clamp_volume(volume));
}

void MediaService::suspend() noexcept {
    std::scoped_lock lock(mutex_);
    if (suspended_) return;
    suspended_ = true;
    for (auto& [id, player] : players_) {
        (void)id;
        if (player.state == PlayerState::started &&
            player.adapter_handle != 0) {
            player.resume_after_suspend = true;
            (void)adapter_->stop(player.adapter_handle);
        }
    }
}

void MediaService::resume() noexcept {
    std::scoped_lock lock(mutex_);
    if (!suspended_) return;
    suspended_ = false;
    for (auto& [id, player] : players_) {
        (void)id;
        if (player.state == PlayerState::started &&
            player.resume_after_suspend && player.adapter_handle != 0) {
            if (adapter_->start(player.adapter_handle)) {
                player.resume_after_suspend = false;
            }
        }
    }
}

void MediaService::close_all() noexcept {
    std::scoped_lock lock(mutex_);
    for (auto& [id, player] : players_) {
        (void)id;
        close_unlocked(player);
    }
    players_.clear();
}

Result<MediaService::Player*> MediaService::player_unlocked(i32 player_id) {
    const auto iterator = players_.find(player_id);
    if (iterator == players_.end()) {
        return fail(ErrorCode::invalid_argument, "unknown media player");
    }
    return &iterator->second;
}

Status MediaService::realize_unlocked(Player& player) {
    if (player.state == PlayerState::closed) {
        return fail(ErrorCode::invalid_state, "player is closed");
    }
    if (player.state >= PlayerState::realized) {
        return {};
    }
    if (is_tone_type(player.content_type)) {
        player.state = PlayerState::realized;
        return {};
    }

    Result<i32> handle = player.source_kind == SourceKind::data
        ? adapter_->create_data(player.data, player.content_type)
        : adapter_->create_locator(player.locator, player.content_type);
    if (!handle) return std::unexpected(handle.error());
    player.adapter_handle = *handle;

    auto loop = adapter_->set_loop_count(*handle, player.loop_count);
    if (!loop) {
        adapter_->close(*handle);
        player.adapter_handle = 0;
        return loop;
    }
    auto volume = adapter_->set_volume(*handle, player.volume);
    if (!volume) {
        adapter_->close(*handle);
        player.adapter_handle = 0;
        return volume;
    }
    auto mute = adapter_->set_mute(*handle, player.muted);
    if (!mute) {
        adapter_->close(*handle);
        player.adapter_handle = 0;
        return mute;
    }
    player.state = PlayerState::realized;
    return {};
}

Status MediaService::ensure_tone_handle_unlocked(Player& player) {
    if (!is_tone_type(player.content_type) || player.adapter_handle != 0) {
        return {};
    }
    auto wave = build_tone_wave(player);
    if (!wave) return std::unexpected(wave.error());
    auto handle = adapter_->create_data(*wave, "audio/x-wav");
    if (!handle) return std::unexpected(handle.error());
    player.adapter_handle = *handle;

    auto loop = adapter_->set_loop_count(*handle, player.loop_count);
    if (!loop) {
        adapter_->close(*handle);
        player.adapter_handle = 0;
        return loop;
    }
    auto volume = adapter_->set_volume(*handle, player.volume);
    if (!volume) {
        adapter_->close(*handle);
        player.adapter_handle = 0;
        return volume;
    }
    auto mute = adapter_->set_mute(*handle, player.muted);
    if (!mute) {
        adapter_->close(*handle);
        player.adapter_handle = 0;
        return mute;
    }
    return {};
}

Result<std::optional<MediaEvent>> MediaService::synchronize_unlocked(
    Player& player) {
    if (player.state != PlayerState::started ||
        player.adapter_handle == 0 || suspended_) {
        return std::optional<MediaEvent> {};
    }
    auto failed = adapter_->has_error(player.adapter_handle);
    if (!failed) return std::unexpected(failed.error());
    if (*failed) {
        player.state = PlayerState::prefetched;
        return std::optional<MediaEvent>(MediaEvent {
            .kind = MediaEventKind::error,
            .player_id = player.id,
            .detail = "platform media playback failed",
        });
    }
    auto playing = adapter_->is_playing(player.adapter_handle);
    if (!playing) return std::unexpected(playing.error());
    if (*playing) return std::optional<MediaEvent> {};
    auto ended = adapter_->has_ended(player.adapter_handle);
    if (!ended) return std::unexpected(ended.error());
    if (!*ended) return std::optional<MediaEvent> {};
    auto time = adapter_->media_time(player.adapter_handle);
    player.state = PlayerState::prefetched;
    return std::optional<MediaEvent>(MediaEvent {
        .kind = MediaEventKind::end_of_media,
        .player_id = player.id,
        .media_time = time ? *time : 0,
    });
}

Result<std::vector<u8>> MediaService::build_tone_wave(
    const Player& player) const {
    auto segments = parse_tone_sequence(player.tone_sequence, player.volume);
    if (!segments) return std::unexpected(segments.error());

    u64 total_samples = 0;
    for (const ToneSegment& segment : *segments) {
        const u64 samples = (static_cast<u64>(segment.duration_milliseconds) *
                             static_cast<u64>(kSampleRate)) / 1'000ULL;
        total_samples += std::max<u64>(1, samples);
    }
    const u64 maximum_samples = static_cast<u64>(kSampleRate) *
                                static_cast<u64>(kMaximumToneSeconds);
    if (total_samples == 0 || total_samples > maximum_samples ||
        total_samples > (std::numeric_limits<u32>::max() - 44ULL) / 2ULL) {
        return fail(ErrorCode::out_of_range,
                    "tone sequence is too long");
    }

    const u32 data_size = static_cast<u32>(total_samples * 2ULL);
    std::vector<u8> wave;
    wave.reserve(static_cast<usize>(44U + data_size));
    wave.insert(wave.end(), {'R', 'I', 'F', 'F'});
    append_u32_le(wave, 36U + data_size);
    wave.insert(wave.end(), {'W', 'A', 'V', 'E'});
    wave.insert(wave.end(), {'f', 'm', 't', ' '});
    append_u32_le(wave, 16U);
    append_u16_le(wave, 1U);
    append_u16_le(wave, 1U);
    append_u32_le(wave, static_cast<u32>(kSampleRate));
    append_u32_le(wave, static_cast<u32>(kSampleRate * 2));
    append_u16_le(wave, 2U);
    append_u16_le(wave, 16U);
    wave.insert(wave.end(), {'d', 'a', 't', 'a'});
    append_u32_le(wave, data_size);

    double phase = 0.0;
    for (const ToneSegment& segment : *segments) {
        const u64 sample_count = std::max<u64>(
            1,
            (static_cast<u64>(segment.duration_milliseconds) *
             static_cast<u64>(kSampleRate)) / 1'000ULL);
        const double amplitude = 0.30 *
            (static_cast<double>(segment.volume) / 100.0) * 32767.0;
        const double frequency = segment.note < 0
            ? 0.0
            : 440.0 * std::pow(2.0,
                               (static_cast<double>(segment.note) - 69.0) /
                                   12.0);
        const double phase_step = frequency == 0.0
            ? 0.0
            : (2.0 * std::numbers::pi_v<double> * frequency) /
                  static_cast<double>(kSampleRate);
        for (u64 sample_index = 0; sample_index < sample_count;
             ++sample_index) {
            const i16 sample = frequency == 0.0
                ? 0
                : static_cast<i16>(std::sin(phase) * amplitude);
            append_u16_le(wave, static_cast<u16>(sample));
            phase += phase_step;
            if (phase >= 2.0 * std::numbers::pi_v<double>) {
                phase -= 2.0 * std::numbers::pi_v<double>;
            }
        }
    }
    return wave;
}

void MediaService::close_unlocked(Player& player) noexcept {
    if (player.adapter_handle != 0) {
        adapter_->close(player.adapter_handle);
        player.adapter_handle = 0;
    }
    player.state = PlayerState::closed;
    player.resume_after_suspend = false;
}

} // namespace phoneme::media
