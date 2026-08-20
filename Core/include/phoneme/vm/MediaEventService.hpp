#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "phoneme/media/MediaService.hpp"
#include "phoneme/vm/NativeRootScope.hpp"

namespace phoneme::vm {

class Machine;

struct MediaEventServiceDiagnostics final {
    usize registered_players {0U};
    usize pending_events {0U};
    bool worker_started {false};
    bool polling {false};
};

class MediaEventService final {
public:
    explicit MediaEventService(Machine& machine) noexcept;
    ~MediaEventService();

    MediaEventService(const MediaEventService&) = delete;
    MediaEventService& operator=(const MediaEventService&) = delete;

    [[nodiscard]] Status register_player(i32 player_id,
                                         ObjectRef player);
    void unregister_player(i32 player_id) noexcept;
    [[nodiscard]] Status enqueue(ObjectRef player,
                                 media::MediaEvent event);
    void wake() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] MediaEventServiceDiagnostics diagnostics() const noexcept;

private:
    struct Registration final {
        ObjectRef player {};
    };

    struct PendingEvent final {
        NativeRootScope player_root;
        media::MediaEvent event;
    };

    [[nodiscard]] Status start_worker_locked();
    [[nodiscard]] Result<std::optional<ObjectRef>> run_worker(
        std::stop_token stop_token);
    [[nodiscard]] Status queue_event(ObjectRef player,
                                     media::MediaEvent event);
    [[nodiscard]] bool poll_players();
    void dispatch_pending();

    Machine& machine_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::unordered_map<i32, Registration> players_;
    std::deque<PendingEvent> pending_;
    ObjectRef worker_thread_ {};
    bool worker_started_ {false};
    bool poll_requested_ {false};
    bool shutting_down_ {false};
};

} // namespace phoneme::vm
