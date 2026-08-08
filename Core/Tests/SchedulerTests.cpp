#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <future>
#include <iostream>
#include <memory>
#include <string_view>

#include "phoneme/vm/CanvasBridge.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MediaEventDispatch.hpp"

namespace phoneme::vm {

// Item 01 owns only the scheduler/Thread/Object surface. Keep this standalone
// test independent from unrelated native modules that may be changing in
// parallel; CoreNatives still calls these registrars, so provide empty module
// boundaries here rather than compiling their implementations.
void register_amms_natives(NativeMethodRegistry&) {}
void register_array_deque_natives(NativeMethodRegistry&) {}
void register_bluetooth_natives(NativeMethodRegistry&) {}
void register_canvas_natives(NativeMethodRegistry&) {}
void register_class_natives(NativeMethodRegistry&) {}
void register_choice_natives(NativeMethodRegistry&) {}
void register_connection_natives(NativeMethodRegistry&) {}
void register_console_natives(NativeMethodRegistry&) {}
void register_file_natives(NativeMethodRegistry&) {}
void register_game_canvas_natives(NativeMethodRegistry&) {}
void register_game_api_natives(NativeMethodRegistry&) {}
void register_graphics_natives(NativeMethodRegistry&) {}
void register_image_natives(NativeMethodRegistry&) {}
void register_jdk8_compat_natives(NativeMethodRegistry&) {}
void register_io_natives(NativeMethodRegistry&) {}
void register_lcdui_natives(NativeMethodRegistry&) {}
void register_m3g_natives(NativeMethodRegistry&) {}
void register_math_natives(NativeMethodRegistry&) {}
void register_media_natives(NativeMethodRegistry&) {}
void register_pim_natives(NativeMethodRegistry&) {}
void register_push_natives(NativeMethodRegistry&) {}
void register_reference_natives(NativeMethodRegistry&) {}
void register_rms_natives(NativeMethodRegistry&) {}
void register_security_natives(NativeMethodRegistry&) {}
void register_sensor_natives(NativeMethodRegistry&) {}
void register_string_encoding_natives(NativeMethodRegistry&) {}
void register_time_natives(NativeMethodRegistry&) {}
void register_util_natives(NativeMethodRegistry&) {}
void register_vendor_natives(NativeMethodRegistry&) {}
void register_wrapper_natives(NativeMethodRegistry&) {}
void register_xml_natives(NativeMethodRegistry&) {}

Status dispatch_media_event(Machine&,
                            ObjectRef,
                            const media::MediaEvent&) {
    return {};
}

Status pump_lcdui_alert_timeouts(Machine&) {
    return {};
}

} // namespace phoneme::vm

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

std::chrono::milliseconds measure_frame_pacing(
    phoneme::vm::ClassRepository& classes,
    phoneme::vm::FramePacingMode mode,
    int frames_per_second,
    int requested_sleep_millis,
    int iterations,
    bool asynchronous_repaint_request = false) {
    phoneme::vm::Machine machine(classes);
    machine.configure_frame_pacing(frames_per_second, mode);

    auto thread_root = machine.allocate_pinned_instance("java/lang/Thread");
    auto target_root = machine.allocate_pinned_instance("java/lang/Object");
    require(thread_root.has_value() && target_root.has_value(),
            "allocate frame pacing test thread");
    auto thread = thread_root->get();
    auto target = target_root->get();
    require(thread.has_value() && target.has_value(),
            "resolve frame pacing test roots");
    require(machine.initialize_java_thread(*thread, *target).has_value(),
            "initialize frame pacing test thread");

    auto completion = std::make_shared<std::promise<long long>>();
    auto future = completion->get_future();
    require(machine.scheduler().start_native_thread(
                machine,
                *thread,
                [&machine,
                 completion,
                 requested_sleep_millis,
                 iterations,
                 asynchronous_repaint_request](std::stop_token)
                    -> phoneme::Result<
                        std::optional<phoneme::vm::ObjectRef>> {
                    const auto started = std::chrono::steady_clock::now();
                    for (int index = 0; index < iterations; ++index) {
                        if (asynchronous_repaint_request) {
                            machine.note_frame_pacing_request();
                        } else {
                            machine.pace_frame_publication();
                            machine.note_frame_pacing_boundary();
                        }
                        if (requested_sleep_millis > 0) {
                            auto slept = machine.sleep_current_thread(
                                requested_sleep_millis);
                            if (!slept) {
                                completion->set_value(-1);
                                return std::unexpected(slept.error());
                            }
                        }
                    }
                    completion->set_value(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count());
                    return std::optional<phoneme::vm::ObjectRef> {};
                }).has_value(),
            "start frame pacing test thread");
    require(future.wait_for(std::chrono::seconds(5)) ==
                std::future_status::ready,
            "frame pacing test completes");
    const long long elapsed = future.get();
    require(elapsed >= 0, "frame pacing test sleep succeeds");
    return std::chrono::milliseconds(elapsed);
}

std::chrono::milliseconds measure_cap_reconfiguration_release(
    phoneme::vm::ClassRepository& classes) {
    phoneme::vm::Machine machine(classes);
    machine.configure_frame_pacing(
        1,
        phoneme::vm::FramePacingMode::cap);

    auto thread_root = machine.allocate_pinned_instance("java/lang/Thread");
    auto target_root = machine.allocate_pinned_instance("java/lang/Object");
    require(thread_root.has_value() && target_root.has_value(),
            "allocate cap reconfiguration test thread");
    auto thread = thread_root->get();
    auto target = target_root->get();
    require(thread.has_value() && target.has_value(),
            "resolve cap reconfiguration test roots");
    require(machine.initialize_java_thread(*thread, *target).has_value(),
            "initialize cap reconfiguration test thread");

    auto waiting = std::make_shared<std::promise<void>>();
    auto waiting_future = waiting->get_future();
    auto completion = std::make_shared<std::promise<long long>>();
    auto completion_future = completion->get_future();
    require(machine.scheduler().start_native_thread(
                machine,
                *thread,
                [&machine, waiting, completion](std::stop_token)
                    -> phoneme::Result<
                        std::optional<phoneme::vm::ObjectRef>> {
                    machine.pace_frame_publication();
                    machine.note_frame_pacing_boundary();
                    waiting->set_value();
                    const auto started = std::chrono::steady_clock::now();
                    machine.pace_frame_publication();
                    completion->set_value(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count());
                    return std::optional<phoneme::vm::ObjectRef> {};
                }).has_value(),
            "start cap reconfiguration test thread");
    require(waiting_future.wait_for(std::chrono::seconds(1)) ==
                std::future_status::ready,
            "cap reconfiguration wait begins");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    machine.configure_frame_pacing(
        60,
        phoneme::vm::FramePacingMode::native);
    require(completion_future.wait_for(std::chrono::seconds(1)) ==
                std::future_status::ready,
            "cap reconfiguration releases the publication gate");
    return std::chrono::milliseconds(completion_future.get());
}

class BlockingWaitProbeBridge final : public phoneme::vm::CanvasBridge {
public:
    explicit BlockingWaitProbeBridge(phoneme::vm::Machine& machine)
        : machine_(machine) {}

    bool observed_released_execution {false};

    phoneme::Status register_canvas(phoneme::vm::ObjectRef,
                                    bool,
                                    bool) override { return {}; }
    phoneme::Status set_display_visible(phoneme::vm::ObjectRef,
                                        bool) override { return {}; }
    phoneme::Status flush_visibility_callbacks() override { return {}; }
    phoneme::Status request_repaint(phoneme::vm::ObjectRef,
                                    phoneme::vm::CanvasRect) override {
        return {};
    }
    phoneme::Status request_service_repaints(
        phoneme::vm::ObjectRef) override { return {}; }
    phoneme::Status pump_blocking_wait_work() override {
        observed_released_execution = !machine_.executing_on_current_thread();
        if (!observed_released_execution) {
            return phoneme::fail(
                phoneme::ErrorCode::invalid_state,
                "blocking Canvas pump retained stale VM execution ownership");
        }
        return {};
    }
    phoneme::Status set_fullscreen(phoneme::vm::ObjectRef,
                                   bool) override { return {}; }
    phoneme::Result<phoneme::Dimensions> canvas_dimensions(
        phoneme::vm::ObjectRef) const override {
        return phoneme::Dimensions {1, 1};
    }
    phoneme::Dimensions display_dimensions() const noexcept override {
        return {1, 1};
    }
    bool pointer_events_supported() const noexcept override { return false; }
    bool pointer_motion_supported() const noexcept override { return false; }
    bool repeat_events_supported() const noexcept override { return false; }
    phoneme::i32 game_action_for_key(phoneme::i32) const noexcept override {
        return 0;
    }
    phoneme::Result<phoneme::i32> key_code_for_action(
        phoneme::i32) const override { return 0; }
    std::string key_name(phoneme::i32) const override { return {}; }
    phoneme::i32 game_key_states(
        phoneme::vm::ObjectRef) const noexcept override { return 0; }
    phoneme::Result<phoneme::vm::ObjectRef> game_graphics(
        phoneme::vm::ObjectRef) override {
        return phoneme::vm::ObjectRef {};
    }
    phoneme::Status request_game_flush(phoneme::vm::ObjectRef,
                                       phoneme::vm::CanvasRect) override {
        return {};
    }
    void append_reference_roots(
        std::vector<phoneme::vm::ObjectRef>&) const override {}

private:
    phoneme::vm::Machine& machine_;
};

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: SchedulerTests <fixture.jar>");
    const char* sanitizer = std::getenv("PHONEME_SANITIZER");
    const bool performance_gate_enabled =
        sanitizer == nullptr || *sanitizer == '\0' ||
        std::string_view(sanitizer) == "none";

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(argv[1]).has_value(),
            "add scheduler fixture archive");

    if (performance_gate_enabled) {
        const auto native_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::native,
            60,
            40,
            12);
        const auto override_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::override_game_loop,
            60,
            40,
            12);
        const auto override_async_repaint_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::override_game_loop,
            60,
            40,
            12,
            true);
        const auto override_loading_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::override_game_loop,
            30,
            0,
            20);
        const auto override_short_sleep_loading_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::override_game_loop,
            30,
            2,
            20);
        const auto capped_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::cap,
            10,
            10,
            5);
        const auto capped_busy_loop_elapsed = measure_frame_pacing(
            classes,
            phoneme::vm::FramePacingMode::cap,
            20,
            0,
            6);
        const auto cap_reconfiguration_release =
            measure_cap_reconfiguration_release(classes);

        require(native_elapsed >= std::chrono::milliseconds(400),
                "native pacing preserves the game's requested sleeps");
        require(override_elapsed >= std::chrono::milliseconds(180) &&
                    override_elapsed <= std::chrono::milliseconds(420) &&
                    override_elapsed + std::chrono::milliseconds(100) <
                        native_elapsed,
                "override pacing retargets a stable 40 ms render loop to 60 FPS");
        require(override_async_repaint_elapsed >= std::chrono::milliseconds(180) &&
                    override_async_repaint_elapsed <= std::chrono::milliseconds(420) &&
                    override_async_repaint_elapsed + std::chrono::milliseconds(100) <
                        native_elapsed,
                "override pacing retargets asynchronous repaint/sleep loops");
        require(override_loading_elapsed < std::chrono::milliseconds(180),
                "override pacing does not throttle loading-style frame publications");
        require(override_short_sleep_loading_elapsed <
                    std::chrono::milliseconds(180),
                "override pacing never stretches a loader's short Java sleep");
        require(capped_elapsed >= std::chrono::milliseconds(380) &&
                    capped_elapsed <= std::chrono::milliseconds(650),
                "cap pacing limits actual frame publications without drift");
        require(capped_busy_loop_elapsed >= std::chrono::milliseconds(220) &&
                    capped_busy_loop_elapsed <= std::chrono::milliseconds(450),
                "cap pacing limits non-sleeping loops without over-throttling");
        require(cap_reconfiguration_release < std::chrono::milliseconds(250),
                "frame pacing changes release an active cap immediately");
    }

    {
        phoneme::vm::Machine wait_machine(classes);
        BlockingWaitProbeBridge bridge(wait_machine);
        wait_machine.configure_canvas_bridge(&bridge);
        auto waited = wait_machine.invoke_static(
            "corefixture/ThreadOps",
            "timedWaitForCanvasPump",
            "()I",
            {},
            10'000'000U);
        require(waited.has_value() && waited->completed_normally() &&
                    waited->return_value.has_value() &&
                    waited->return_value->as_int().value_or(0) == 1,
                "timed Object.wait completes while Canvas work is pumped");
        require(bridge.observed_released_execution,
                "blocking Canvas pump sees the VM execution gate released");
        wait_machine.configure_canvas_bridge(nullptr);
    }

    {
        phoneme::vm::Machine native_lifetime_machine(classes);
        auto thread_root = native_lifetime_machine.allocate_pinned_instance(
            "java/lang/Thread");
        auto target_root = native_lifetime_machine.allocate_pinned_instance(
            "java/lang/Object");
        require(thread_root.has_value() && target_root.has_value(),
                "allocate native scheduler lifetime fixtures");
        auto thread = thread_root->get();
        auto target = target_root->get();
        require(thread.has_value() && target.has_value(),
                "resolve native scheduler lifetime fixture roots");
        require(native_lifetime_machine.initialize_java_thread(
                    *thread, *target).has_value(),
                "register native scheduler fixture thread");
        require(native_lifetime_machine.scheduler().start_native_thread(
                    native_lifetime_machine,
                    *thread,
                    [](std::stop_token)
                        -> phoneme::Result<std::optional<phoneme::vm::ObjectRef>> {
                        return std::optional<phoneme::vm::ObjectRef> {};
                    }).has_value(),
                "start ephemeral native scheduler fixture");
        require(thread_root->release().has_value() &&
                    target_root->release().has_value(),
                "release external native scheduler fixture roots");

        bool terminated = false;
        for (int attempt = 0; attempt < 200 && !terminated; ++attempt) {
            const auto snapshot = native_lifetime_machine.scheduler().snapshot();
            for (const auto& entry : snapshot.threads) {
                if (entry.object == *thread &&
                    entry.state == phoneme::vm::JavaThreadState::terminated) {
                    terminated = true;
                    break;
                }
            }
            if (!terminated) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        require(terminated, "ephemeral native scheduler fixture terminates");

        std::vector<phoneme::vm::ObjectRef> roots;
        native_lifetime_machine.scheduler().append_reference_roots(roots);
        require(std::find(roots.begin(), roots.end(), *thread) == roots.end() &&
                    std::find(roots.begin(), roots.end(), *target) == roots.end(),
                "terminated native task releases Thread and Runnable GC roots");

        auto next_thread_root =
            native_lifetime_machine.allocate_pinned_instance("java/lang/Thread");
        require(next_thread_root.has_value(),
                "allocate next native task for pruning");
        auto next_thread = next_thread_root->get();
        require(next_thread.has_value(),
                "resolve next native task thread");
        require(native_lifetime_machine.initialize_java_thread(
                    *next_thread, {}).has_value(),
                "register next native task thread");
        require(native_lifetime_machine.scheduler().start_native_thread(
                    native_lifetime_machine,
                    *next_thread,
                    [](std::stop_token)
                        -> phoneme::Result<std::optional<phoneme::vm::ObjectRef>> {
                        return std::optional<phoneme::vm::ObjectRef> {};
                    }).has_value(),
                "start next native task and prune completed record");
        const auto pruned_snapshot =
            native_lifetime_machine.scheduler().snapshot();
        require(std::none_of(
                    pruned_snapshot.threads.begin(),
                    pruned_snapshot.threads.end(),
                    [thread](const phoneme::vm::JavaThreadSnapshot& entry) {
                        return entry.object == *thread;
                    }),
                "next native task prunes prior terminated native record");
    }

    {
        phoneme::vm::Machine retirement_machine(classes);
        for (int iteration = 0; iteration < 256; ++iteration) {
            auto thread_root = retirement_machine.allocate_pinned_instance(
                "java/lang/Thread");
            require(thread_root.has_value(),
                    "allocate native-thread retirement fixture");
            auto thread = thread_root->get();
            require(thread.has_value(),
                    "resolve native-thread retirement fixture");
            require(retirement_machine.initialize_java_thread(
                        *thread, phoneme::vm::ObjectRef {}).has_value(),
                    "initialize native-thread retirement fixture");
            require(retirement_machine.scheduler().start_native_thread(
                        retirement_machine, *thread,
                        [](std::stop_token)
                            -> phoneme::Result<std::optional<
                                phoneme::vm::ObjectRef>> {
                            return std::optional<phoneme::vm::ObjectRef> {};
                        }).has_value(),
                    "start short native Java task");
            if ((iteration & 7) == 0) {
                std::this_thread::yield();
            }
        }
        retirement_machine.shutdown();
    }

    {
        phoneme::vm::Machine callback_machine(classes);
        auto first_root = callback_machine.allocate_pinned_instance(
            "java/lang/Object");
        auto second_root = callback_machine.allocate_pinned_instance(
            "java/lang/Object");
        require(first_root.has_value() && second_root.has_value(),
                "allocate serial callback coalescing fixtures");
        auto first = first_root->get();
        auto second = second_root->get();
        require(first.has_value() && second.has_value(),
                "resolve serial callback fixture roots");

        callback_machine.set_serial_callback_coalescing(true);
        for (int iteration = 0; iteration < 128; ++iteration) {
            require(callback_machine.enqueue_serial_callback(*first).has_value(),
                    "coalesce repeated hidden serial callback");
        }
        require(callback_machine.enqueue_serial_callback(*second).has_value(),
                "retain distinct hidden serial callback");
        require(callback_machine.pending_serial_callbacks() == 2U,
                "hidden callSerially queue keeps one entry per Runnable");

        callback_machine.set_serial_callback_coalescing(false);
        require(callback_machine.enqueue_serial_callback(*first).has_value(),
                "foreground serial callback preserves repeated scheduling");
        require(callback_machine.pending_serial_callbacks() == 3U,
                "foreground callSerially queue restores normal semantics");
    }

    phoneme::vm::Machine machine(classes);
    auto registered_gc = machine.natives().register_method(
        "java/lang/System",
        "gc",
        "()V",
        [](phoneme::vm::Machine& active_machine,
           std::span<const phoneme::vm::Value> arguments)
            -> phoneme::Result<std::optional<phoneme::vm::Value>> {
            if (!arguments.empty()) {
                return phoneme::fail(phoneme::ErrorCode::invalid_argument,
                                     "System.gc expects no arguments");
            }
            auto collected = active_machine.collect_garbage();
            if (!collected) {
                return std::unexpected(collected.error());
            }
            return std::optional<phoneme::vm::Value> {};
        });
    require(registered_gc.has_value(),
            "register scheduler-test System.gc native");
    machine.scheduler().set_deterministic(true);
    auto result = machine.invoke_static("corefixture/ThreadOps",
                                        "run",
                                        "()I",
                                        {},
                                        250'000'000U);
    if (!result) {
        std::cerr << "VM invoke error: " << result.error().message << '\n';
    }
    require(result.has_value(), "invoke scheduler fixture through VM");
    require(result->completed_normally() && result->return_value.has_value(),
            "scheduler fixture completes normally");
    auto status = result->return_value->as_int();
    if (status && *status != 0) {
        std::cerr << "ThreadOps failure code: " << *status << '\n';
    }
    require(status.has_value() && *status == 0,
            "Thread, monitor, wait, sleep, join, interruption and GC semantics");

    const auto snapshot = machine.scheduler().snapshot();
    require(snapshot.threads.size() >= 2U,
            "scheduler records main and worker Java threads");
    for (const auto& thread : snapshot.threads) {
        if (thread.id == 1U) {
            require(thread.state == phoneme::vm::JavaThreadState::running,
                    "main Java thread remains running");
        } else {
            require(thread.state == phoneme::vm::JavaThreadState::terminated ||
                        thread.state == phoneme::vm::JavaThreadState::new_thread,
                    "started workers terminate and unstarted threads remain new");
            require(!thread.alive,
                    "no child Java thread remains alive");
        }
    }
    require(snapshot.blocked.empty(),
            "no blocked Java threads remain after fixture completion");
    require(snapshot.sleeping.empty(),
            "no sleeping Java threads remain after fixture completion");

    {
        phoneme::vm::Machine busy_main_machine(classes);
        const std::array<phoneme::vm::Value, 1> arguments {
            phoneme::vm::Value::from_int(1'500),
        };
        const std::clock_t busy_cpu_start = std::clock();
        const auto busy_wall_start = std::chrono::steady_clock::now();
        auto busy = busy_main_machine.invoke_static(
            "corefixture/ThreadOps",
            "busyMainThreadFor",
            "(I)I",
            arguments,
            250'000'000U);
        const double busy_cpu_seconds =
            static_cast<double>(std::clock() - busy_cpu_start) /
            static_cast<double>(CLOCKS_PER_SEC);
        const double busy_wall_seconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - busy_wall_start).count();
        std::cout << "Main busy-loop pacing: wall=" << busy_wall_seconds
                  << "s cpu=" << busy_cpu_seconds << "s\n";
        require(busy.has_value() && busy->completed_normally() &&
                    busy->return_value.has_value() &&
                    busy->return_value->as_int().value_or(0) == 1,
                "host-driven main Java thread completes busy workload");
        if (performance_gate_enabled) {
            // Foreground quanta cooperatively release the VM gate, but they
            // must not impose a CPU duty cycle. Loading and asset parsing are
            // legitimate sustained workloads and should run near host speed.
            require(busy_cpu_seconds > busy_wall_seconds * 0.80,
                    "foreground main Java thread is not duty-cycle throttled");
        }
    }

    {
        phoneme::vm::Machine busy_machine(classes);
        auto started = busy_machine.invoke_static("corefixture/ThreadOps",
                                                  "startBusyThread",
                                                  "()I",
                                                  {},
                                                  20'000'000U);
        require(started.has_value() && started->completed_normally() &&
                    started->return_value.has_value() &&
                    started->return_value->as_int().value_or(0) == 1,
                "start non-cooperative Java worker");

        // The old lifetime-wide 10M instruction budget killed long-running
        // game loops shortly after launch. A scheduler-owned worker must remain
        // alive across many cooperative VM quanta instead. Run
        // this section with the production pacing path and print process CPU
        // time so regressions can be compared without device Instruments.
        const std::clock_t busy_cpu_start = std::clock();
        const auto busy_wall_start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(3));
        const double busy_cpu_seconds =
            static_cast<double>(std::clock() - busy_cpu_start) /
            static_cast<double>(CLOCKS_PER_SEC);
        const double busy_wall_seconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - busy_wall_start).count();
        std::cout << "Busy-loop pacing: wall=" << busy_wall_seconds
                  << "s cpu=" << busy_cpu_seconds << "s\n";
        // Sanitizer instrumentation adds process CPU that is unrelated to the
        // scheduler's pacing ratio, so keep the performance regression gate on
        // normal builds while still exercising all semantics under sanitizers.
        if (performance_gate_enabled) {
            require(busy_cpu_seconds > busy_wall_seconds * 0.75,
                    "foreground Java worker is not duty-cycle throttled");
        }

        auto additional = busy_machine.invoke_static(
            "corefixture/ThreadOps",
            "startAdditionalBusyThread",
            "()I",
            {},
            20'000'000U);
        require(additional.has_value() && additional->completed_normally() &&
                    additional->return_value.has_value() &&
                    additional->return_value->as_int().value_or(0) == 1,
                "start a second non-cooperative Java worker");

        busy_machine.scheduler().set_host_foreground(false);
        const std::clock_t hidden_cpu_start = std::clock();
        const auto hidden_wall_start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const double hidden_cpu_seconds =
            static_cast<double>(std::clock() - hidden_cpu_start) /
            static_cast<double>(CLOCKS_PER_SEC);
        const double hidden_wall_seconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - hidden_wall_start).count();
        std::cout << "Hidden two-thread pacing: wall=" << hidden_wall_seconds
                  << "s cpu=" << hidden_cpu_seconds << "s\n";
        if (performance_gate_enabled) {
            require(hidden_cpu_seconds < hidden_wall_seconds * 0.10,
                    "hidden VM shares one low-duty CPU gate across threads");
        }
        busy_machine.scheduler().set_host_foreground(true);

        auto alive = busy_machine.invoke_static(
            "corefixture/ThreadOps",
            "busyThreadIsAlive",
            "()I",
            {},
            1'000'000U);
        require(alive.has_value() && alive->completed_normally() &&
                    alive->return_value.has_value() &&
                    alive->return_value->as_int().value_or(0) == 1,
                "long-lived Java worker is not killed by a lifetime budget");

        const auto shutdown_start = std::chrono::steady_clock::now();
        busy_machine.scheduler().shutdown();
        const auto shutdown_elapsed =
            std::chrono::steady_clock::now() - shutdown_start;
        require(shutdown_elapsed < std::chrono::seconds(2),
                "scheduler shutdown cancels and joins a busy Java worker");
    }

    std::cout << "Scheduler tests passed\n";
    return 0;
}
