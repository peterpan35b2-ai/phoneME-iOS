#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string_view>

#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MediaEventDispatch.hpp"

namespace phoneme::vm {

// Item 01 owns only the scheduler/Thread/Object surface. Keep this standalone
// test independent from unrelated native modules that may be changing in
// parallel; CoreNatives still calls these registrars, so provide empty module
// boundaries here rather than compiling their implementations.
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
void register_io_natives(NativeMethodRegistry&) {}
void register_lcdui_natives(NativeMethodRegistry&) {}
void register_m3g_natives(NativeMethodRegistry&) {}
void register_math_natives(NativeMethodRegistry&) {}
void register_media_natives(NativeMethodRegistry&) {}
void register_push_natives(NativeMethodRegistry&) {}
void register_rms_natives(NativeMethodRegistry&) {}
void register_security_natives(NativeMethodRegistry&) {}
void register_string_encoding_natives(NativeMethodRegistry&) {}
void register_time_natives(NativeMethodRegistry&) {}
void register_util_natives(NativeMethodRegistry&) {}
void register_wrapper_natives(NativeMethodRegistry&) {}

Status dispatch_media_event(Machine&,
                            ObjectRef,
                            const media::MediaEvent&) {
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
            require(busy_cpu_seconds < busy_wall_seconds * 0.85,
                    "main Java thread busy loop yields host CPU");
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

        // The old lifetime-wide 10M instruction budget killed real game loops
        // such as Ninja School shortly after launch. A scheduler-owned worker
        // must remain alive across many cooperative VM quanta instead. Run
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
        // scheduler's sleep ratio, so keep the performance regression gate on
        // normal builds while still exercising all semantics under sanitizers.
        if (performance_gate_enabled) {
            require(busy_cpu_seconds < busy_wall_seconds * 0.85,
                    "sustained busy Java worker yields host CPU");
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
            require(hidden_cpu_seconds < hidden_wall_seconds * 0.25,
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
