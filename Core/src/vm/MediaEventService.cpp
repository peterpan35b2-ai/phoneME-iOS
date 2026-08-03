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
    condition_.notify_all();
}

void MediaEventService::wake() noexcept {
    condition_.notify_all();
}

void MediaEventService::shutdown() noexcept {
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) return;
        shutting_down_ = true;
        players_.clear();
        pending_.clear();
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
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            condition_.wait_for(lock, kPollInterval, [this, &stop_token] {
                return shutting_down_ || stop_token.stop_requested() ||
                       !pending_.empty();
            });
            if (shutting_down_ || stop_token.stop_requested()) break;
        }

        poll_players();
        dispatch_pending();
    }
    return std::optional<ObjectRef> {};
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

void MediaEventService::poll_players() {
    std::vector<std::pair<i32, ObjectRef>> players;
    {
        std::scoped_lock lock(mutex_);
        players.reserve(players_.size());
        for (const auto& [player_id, registration] : players_) {
            players.emplace_back(player_id, registration.player);
        }
    }

    for (const auto& [player_id, player] : players) {
        if (!machine_.heap().class_name(player)) {
            (void)machine_.media().close(player_id);
            unregister_player(player_id);
            continue;
        }

        auto event = machine_.media().synchronize(player_id);
        if (!event || !event->has_value()) continue;
        const bool closed =
            (**event).kind == media::MediaEventKind::closed;
        (void)queue_event(player, std::move(**event));
        if (closed) unregister_player(player_id);
    }
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
