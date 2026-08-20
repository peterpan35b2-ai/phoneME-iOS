#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "phoneme/media/MediaService.hpp"

namespace {

using namespace phoneme;
using namespace phoneme::media;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

class FakeMediaAdapter final : public MediaAdapter {
public:
    struct Entry final {
        i64 time {0};
        i64 duration {5'000'000};
        i32 loop_count {1};
        i32 volume {100};
        bool muted {false};
        bool playing {false};
        bool ended {false};
        bool error {false};
    };

    [[nodiscard]] Result<i32> create_data(
        std::span<const u8> data,
        std::string_view) override {
        if (data.empty()) {
            return fail(ErrorCode::invalid_argument, "empty fake media");
        }
        return create_entry();
    }

    [[nodiscard]] Result<i32> create_locator(
        std::string_view locator,
        std::string_view) override {
        if (locator.empty()) {
            return fail(ErrorCode::invalid_argument, "empty fake locator");
        }
        return create_entry();
    }

    [[nodiscard]] Status start(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return unknown_handle();
        if (fail_next_start) {
            fail_next_start = false;
            return fail(ErrorCode::io_error, "simulated stale media handle");
        }
        entry->playing = true;
        entry->ended = false;
        ++start_count;
        return {};
    }

    [[nodiscard]] Status stop(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return unknown_handle();
        entry->playing = false;
        ++stop_count;
        return {};
    }

    void close(i32 handle) noexcept override {
        if (entries.erase(handle) != 0U) ++close_count;
    }

    [[nodiscard]] Status set_loop_count(i32 handle, i32 count) override {
        auto* entry = find(handle);
        if (entry == nullptr) return unknown_handle();
        entry->loop_count = count;
        return {};
    }

    [[nodiscard]] Status set_volume(i32 handle, i32 level) override {
        auto* entry = find(handle);
        if (entry == nullptr) return unknown_handle();
        entry->volume = level;
        return {};
    }

    [[nodiscard]] Status set_mute(i32 handle, bool muted) override {
        auto* entry = find(handle);
        if (entry == nullptr) return unknown_handle();
        entry->muted = muted;
        return {};
    }

    [[nodiscard]] Result<i64> set_media_time(i32 handle,
                                             i64 microseconds) override {
        auto* entry = find(handle);
        if (entry == nullptr) return std::unexpected(unknown_handle().error());
        entry->time = microseconds;
        ++set_time_count;
        return entry->time;
    }

    [[nodiscard]] Result<i64> media_time(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return std::unexpected(unknown_handle().error());
        return entry->time;
    }

    [[nodiscard]] Result<i64> duration(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return std::unexpected(unknown_handle().error());
        return entry->duration;
    }

    [[nodiscard]] Result<bool> is_playing(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return std::unexpected(unknown_handle().error());
        return entry->playing;
    }

    [[nodiscard]] Result<bool> has_ended(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return std::unexpected(unknown_handle().error());
        return entry->ended;
    }

    [[nodiscard]] Result<bool> has_error(i32 handle) override {
        auto* entry = find(handle);
        if (entry == nullptr) return std::unexpected(unknown_handle().error());
        return entry->error;
    }

    [[nodiscard]] Result<bool> play_tone(i32, i32, i32) override {
        return true;
    }

    usize create_count {0};
    usize close_count {0};
    usize start_count {0};
    usize stop_count {0};
    usize set_time_count {0};
    bool fail_next_start {false};
    std::unordered_map<i32, Entry> entries;

private:
    [[nodiscard]] i32 create_entry() {
        const i32 handle = next_handle_++;
        entries.emplace(handle, Entry {});
        ++create_count;
        return handle;
    }

    [[nodiscard]] Entry* find(i32 handle) {
        const auto iterator = entries.find(handle);
        return iterator == entries.end() ? nullptr : &iterator->second;
    }

    [[nodiscard]] static Status unknown_handle() {
        return fail(ErrorCode::invalid_argument, "unknown fake media handle");
    }

    i32 next_handle_ {1};
};

void test_deallocate_releases_and_recreates_platform_handle() {
    auto adapter = std::make_unique<FakeMediaAdapter>();
    auto* fake = adapter.get();
    MediaService service(std::move(adapter));

    auto player = service.create_data({1U, 2U, 3U}, "audio/x-wav");
    require(player.has_value(), "create media player");
    require(service.realize(*player).has_value(), "realize player");
    require(fake->create_count == 1U && fake->entries.size() == 1U,
            "realize creates one platform handle");

    require(service.prefetch(*player).has_value(), "prefetch player");
    auto set_time = service.set_media_time(*player, 1'250'000);
    require(set_time.has_value() && *set_time == 1'250'000,
            "set media time before playback");
    auto started = service.start(*player);
    require(started.has_value(), "start player");
    auto polling = service.event_poll_needed(*player);
    require(polling.has_value() && *polling,
            "started player requires MMAPI event polling");

    require(service.deallocate(*player).has_value(), "deallocate player");
    require(fake->close_count == 1U && fake->entries.empty(),
            "deallocate closes scarce platform media handle");
    auto snapshot = service.snapshot(*player);
    require(snapshot.has_value() && snapshot->state == PlayerState::realized &&
                snapshot->media_time == 1'250'000,
            "deallocate preserves REALIZED state and cached media time");
    polling = service.event_poll_needed(*player);
    require(polling.has_value() && !*polling,
            "deallocated player does not require periodic polling");

    require(service.prefetch(*player).has_value(),
            "prefetch recreates deallocated handle");
    require(fake->create_count == 2U && fake->entries.size() == 1U &&
                fake->set_time_count >= 2U,
            "recreated handle restores cached media time");
    snapshot = service.snapshot(*player);
    require(snapshot.has_value() && snapshot->state == PlayerState::prefetched &&
                snapshot->media_time == 1'250'000,
            "re-prefetched player preserves playback position");

    fake->fail_next_start = true;
    started = service.start(*player);
    require(started.has_value(),
            "restart player after stale platform handle is recreated");
    require(fake->create_count == 3U && fake->close_count == 2U &&
                fake->entries.size() == 1U,
            "start retries exactly once with a fresh platform handle");
    require(service.close(*player).has_value(), "close player");
    require(fake->close_count == 3U && fake->entries.empty(),
            "close releases recreated platform handle");
    snapshot = service.snapshot(*player);
    require(snapshot.has_value() && snapshot->state == PlayerState::closed,
            "closed state remains observable");
}

} // namespace

int main() {
    test_deallocate_releases_and_recreates_platform_handle();
    std::cout << "Media service tests passed\n";
    return 0;
}
