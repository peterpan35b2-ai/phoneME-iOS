#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "phoneme/runtime/ParallelExecutor.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void test_exact_once_partitioning() {
    phoneme::runtime::ParallelExecutor executor(3U);
    constexpr phoneme::usize kItems = 20'000U;
    std::vector<std::atomic<unsigned>> visits(kItems);
    for (auto& visit : visits) visit.store(0U, std::memory_order_relaxed);

    executor.parallel_for(
        kItems,
        1U,
        37U,
        [&](phoneme::usize begin, phoneme::usize end) {
            for (phoneme::usize index = begin; index < end; ++index) {
                visits[index].fetch_add(1U, std::memory_order_relaxed);
            }
        });

    require(executor.active_worker_count() == 3U,
            "parallel executor starts requested workers");
    for (const auto& visit : visits) {
        require(visit.load(std::memory_order_relaxed) == 1U,
                "parallel executor visits each item exactly once");
    }
}

void test_nested_submission_falls_back_inline() {
    phoneme::runtime::ParallelExecutor executor(3U);
    std::atomic<unsigned> outer_calls {0U};
    std::atomic<unsigned> inner_calls {0U};

    executor.parallel_for(
        128U,
        1U,
        1U,
        [&](phoneme::usize begin, phoneme::usize end) {
            for (phoneme::usize index = begin; index < end; ++index) {
                (void)index;
                outer_calls.fetch_add(1U, std::memory_order_relaxed);
                executor.parallel_for(
                    4U,
                    1U,
                    1U,
                    [&](phoneme::usize nested_begin, phoneme::usize nested_end) {
                        inner_calls.fetch_add(
                            static_cast<unsigned>(nested_end - nested_begin),
                            std::memory_order_relaxed);
                    });
            }
        });

    require(outer_calls.load(std::memory_order_relaxed) == 128U,
            "nested test completes outer job");
    require(inner_calls.load(std::memory_order_relaxed) == 512U,
            "nested parallel_for completes without deadlock");
}

void test_small_jobs_stay_inline() {
    phoneme::runtime::ParallelExecutor executor(3U);
    unsigned calls = 0U;
    executor.parallel_for(
        8U,
        64U,
        1U,
        [&](phoneme::usize begin, phoneme::usize end) {
            calls += static_cast<unsigned>(end - begin);
        });
    require(calls == 8U, "small inline job covers all items");
    require(executor.active_worker_count() == 0U,
            "small inline job does not create worker threads");
}

void test_shared_work_budget() {
    using phoneme::runtime::FramePressure;
    using phoneme::runtime::ThermalPressure;
    using phoneme::runtime::WorkClass;

    auto& coordinator = phoneme::runtime::shared_work_coordinator();
    coordinator.set_thermal_pressure(ThermalPressure::nominal);

    coordinator.note_frame_interval(8U, 10U);
    require(coordinator.frame_pressure() == FramePressure::low,
            "fast frame reports low pressure");
    coordinator.note_frame_interval(10U, 10U);
    require(coordinator.frame_pressure() == FramePressure::normal,
            "on-target paced frame stays normal");
    coordinator.note_frame_interval(12U, 10U);
    require(coordinator.frame_pressure() == FramePressure::high,
            "late frame reports high pressure");
    coordinator.note_frame_interval(15U, 10U);
    require(coordinator.frame_pressure() == FramePressure::overloaded,
            "badly late frame reports overloaded pressure");

    coordinator.note_frame_interval(10U, 10U);
    require(coordinator.background_work_allowed(),
            "healthy nominal frame admits background work");
    coordinator.begin_frame_work();
    require(!coordinator.background_work_allowed(),
            "frame work suppresses background work");
    coordinator.end_frame_work();

    require(coordinator.try_begin_background_work(),
            "first background job acquires global slot");
    require(!coordinator.try_begin_background_work(),
            "background CPU slot is globally serialized");
    coordinator.end_background_work();

    coordinator.set_thermal_pressure(ThermalPressure::serious);
    require(!coordinator.background_work_allowed(),
            "serious thermal pressure disables background work");
    require(coordinator.helper_limit(WorkClass::frame_critical, 200'000U) <= 1U,
            "serious thermal pressure limits frame helpers");

    coordinator.set_thermal_pressure(ThermalPressure::critical);
    require(coordinator.helper_limit(WorkClass::frame_critical, 200'000U) == 0U,
            "critical thermal pressure disables native helpers");
    phoneme::runtime::ParallelExecutor executor(3U);
    executor.parallel_for(
        WorkClass::frame_critical,
        200'000U,
        128U,
        1U,
        1U,
        [](phoneme::usize, phoneme::usize) {});
    require(executor.active_worker_count() == 0U,
            "critical thermal pressure keeps frame work inline");

    coordinator.set_thermal_pressure(ThermalPressure::nominal);
    coordinator.note_frame_interval(10U, 10U);
}

} // namespace

int main() {
    test_exact_once_partitioning();
    test_nested_submission_falls_back_inline();
    test_small_jobs_stay_inline();
    test_shared_work_budget();
    std::cout << "ParallelExecutorTests passed\n";
    return 0;
}
