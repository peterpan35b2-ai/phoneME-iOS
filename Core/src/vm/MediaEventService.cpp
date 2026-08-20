#include "phoneme/vm/MediaEventService.hpp"

#include <chrono>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MediaEventDispatch.hpp"

namespace phoneme::vm {
namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(25);

} // namespace

MediaEventService::MediaEventService(Machine& machine) noexcept
    : machine_(machine) {}

MediaEventService::~MediaEventService() {
    shutdown();
}

Status MediaEventService::register_player(i32 player_id,
                                          ObjectRef player) {
    if (player_id <= 0 || player.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "media event registration is invalid");
    }

    std::scoped_lock lock(mutex_);
    if (shutting_down_) {
        return fail(ErrorCode::invalid_state,
                    "media event service is shutting down");
    }
    players_.insert_or_assign(player_id, Registration {.player = player});
    poll_requested_ = true;
    auto started = start_worker_locked();
    if (!started) {
        players_.erase(player_id);
        return started;
    }
    condition_.notify_all();
    return {};
}

void MediaEventService::unregister_player(i32 player_id) noexcept {
    std::scoped_lock lock(mutex_);
    players_.erase(player_id);
    poll_requested_ = true;
    condition_.notify_all();
}

void MediaEventService::wake() noexcept {
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) return;
        poll_requested_ = true;
    }
    condition_.notify_all();
}

MediaEventServiceDiagnostics MediaEventService::diagnostics() const noexcept {
    std::scoped_lock lock(mutex_);
    return MediaEventServiceDiagnostics {
        .registered_players = players_.size(),
        .pending_events = pending_.size(),
        .worker_started = worker_started_,
        .polling = poll_requested_,
    };
}

void MediaEventService::shutdown() noexcept {
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) return;
        shutting_down_ = true;
        players_.clear();
        pending_.clear();
        poll_requested_ = true;
    }
    condition_.notify_all();
}

Status MediaEventService::start_worker_locked() {
    if (worker_started_) return {};

    auto thread = machine_.class_states().allocate_instance(
        machine_.heap(), "java/lang/Thread");
    if (!thread) return std::unexpected(thread.error());
    auto initialized = machine_.initialize_java_thread(*thread, {});
    if (!initialized) return initialized;

    worker_thread_ = *thread;
    worker_started_ = true;
    auto started = machine_.scheduler().start_native_thread(
        machine_, *thread,
        [this](std::stop_token stop_token)
            -> Result<std::optional<ObjectRef>> {
            return run_worker(stop_token);
        });
    if (!started) {
        worker_started_ = false;
        worker_thread_ = {};
        return started;
    }
    return {};
}

Result<std::optional<ObjectRef>> MediaEventService::run_worker(
    std::stop_token stop_token) {
    bool continuous_poll = false;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            const auto wake_predicate = [this, &stop_token] {
                return shutting_down_ || stop_token.stop_requested() ||
                       poll_requested_ || !pending_.empty();
            };
            if (continuous_poll) {
                condition_.wait_for(lock, kPollInterval, wake_predicate);
            } else {
                condition_.wait(lock, wake_predicate);
            }
            if (shutting_down_ || stop_token.stop_requested()) break;
            poll_requested_ = false;
        }

        // A registered Player only needs periodic synchronization while the
        // platform backend is actually playing. Realized/prefetched/stopped
        // players used to keep this worker waking every 25 ms for the rest of
        // the VM lifetime, which was a persistent mobile CPU/thermal tax after
        // the first sound effect. Lifecycle operations call wake(), so an idle
        // worker resumes immediately when playback starts again.
        continuous_poll = poll_players();
        dispatch_pending();
    }
    return std::optional<ObjectRef> {};
}

Status MediaEventService::enqueue(ObjectRef player,
                                  media::MediaEvent event) {
    return queue_event(player, std::move(event));
}

Status MediaEventService::queue_event(ObjectRef player,
                                      media::MediaEvent event) {
    auto root = machine_.pin_native_root(player);
    if (!root) return std::unexpected(root.error());

    std::scoped_lock lock(mutex_);
    if (shutting_down_) return {};
    pending_.push_back(PendingEvent {
        .player_root = std::move(*root),
        .event = std::move(event),
    });
    condition_.notify_all();
    return {};
}

bool MediaEventService::poll_players() {
    std::vector<std::pair<i32, ObjectRef>> players;
    {
        std::scoped_lock lock(mutex_);
        players.reserve(players_.size());
        for (const auto& [player_id, registration] : players_) {
            players.emplace_back(player_id, registration.player);
        }
    }

    bool needs_continuous_poll = false;
    for (const auto& [player_id, player] : players) {
        if (!machine_.heap().class_name(player)) {
            (void)machine_.media().close(player_id);
            unregister_player(player_id);
            continue;
        }

        auto event = machine_.media().synchronize(player_id);
        if (event && event->has_value()) {
            const bool closed =
                (**event).kind == media::MediaEventKind::closed;
            (void)queue_event(player, std::move(**event));
            if (closed) {
                unregister_player(player_id);
                continue;
            }
        }

        auto poll_needed = machine_.media().event_poll_needed(player_id);
        if (poll_needed && *poll_needed) {
            needs_continuous_poll = true;
        }
    }
    return needs_continuous_poll;
}

void MediaEventService::dispatch_pending() {
    for (;;) {
        std::optional<PendingEvent> pending;
        {
            std::scoped_lock lock(mutex_);
            if (pending_.empty() || shutting_down_) return;
            pending.emplace(std::move(pending_.front()));
            pending_.pop_front();
        }
        auto player = pending->player_root.get();
        if (!player) continue;
        // Listener failures are isolated to the listener invocation. A broken
        // listener must not terminate the shared MMAPI dispatcher and starve
        // every other Player in the suite.
        (void)dispatch_media_event(machine_, *player, pending->event);
    }
}

} // namespace phoneme::vm
