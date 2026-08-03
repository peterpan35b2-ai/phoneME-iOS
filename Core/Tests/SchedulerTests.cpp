#include <chrono>
#include <cstdlib>
#include <iostream>

#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {

// Item 01 owns only the scheduler/Thread/Object surface. Keep this standalone
// test independent from unrelated native modules that may be changing in
// parallel; CoreNatives still calls these registrars, so provide empty module
// boundaries here rather than compiling their implementations.
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
void register_math_natives(NativeMethodRegistry&) {}
void register_media_natives(NativeMethodRegistry&) {}
void register_push_natives(NativeMethodRegistry&) {}
void register_rms_natives(NativeMethodRegistry&) {}
void register_security_natives(NativeMethodRegistry&) {}
void register_string_encoding_natives(NativeMethodRegistry&) {}
void register_time_natives(NativeMethodRegistry&) {}
void register_util_natives(NativeMethodRegistry&) {}
void register_wrapper_natives(NativeMethodRegistry&) {}

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
        phoneme::vm::Machine busy_machine(classes);
        busy_machine.scheduler().set_deterministic(true);
        auto started = busy_machine.invoke_static("corefixture/ThreadOps",
                                                  "startBusyThread",
                                                  "()I",
                                                  {},
                                                  20'000'000U);
        require(started.has_value() && started->completed_normally() &&
                    started->return_value.has_value() &&
                    started->return_value->as_int().value_or(0) == 1,
                "start non-cooperative Java worker");

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
