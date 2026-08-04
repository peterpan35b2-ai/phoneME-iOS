#pragma once

#include <array>
#include <cstdint>

#include "phoneme/base/Types.hpp"

#ifndef PHONEME_ENABLE_VM_PROFILING
#define PHONEME_ENABLE_VM_PROFILING 0
#endif

namespace phoneme::vm {

enum class AllocationPayloadKind : u8 {
    object,
    array,
    clone,
    string_payload,
    count,
};

struct PerformanceCounterSnapshot final {
    u64 executed_bytecodes {0};
    std::array<u64, 256> opcode_counts {};
    u64 method_invocations {0};
    u64 native_invocations {0};
    u64 maximum_java_call_depth {0};
    u64 exception_dispatches {0};
    u64 class_initializations {0};
    u64 instruction_budget_exits {0};
    u64 scheduler_quanta {0};

    u64 class_cache_hits {0};
    u64 class_cache_misses {0};
    u64 method_resolution_hits {0};
    u64 method_resolution_misses {0};
    u64 declared_method_resolution_hits {0};
    u64 declared_method_resolution_misses {0};
    u64 field_resolution_hits {0};
    u64 field_resolution_misses {0};
    u64 assignability_cache_hits {0};
    u64 assignability_cache_misses {0};
    u64 native_registry_lookups {0};
    u64 metadata_key_constructions {0};
    u64 virtual_inline_cache_hits {0};
    u64 virtual_inline_cache_misses {0};
    u64 direct_call_cache_hits {0};
    u64 direct_call_cache_misses {0};
    u64 operand_resolution_hits {0};
    u64 operand_resolution_misses {0};
    u64 operand_resolution_failures {0};
    u64 descriptor_cache_hits {0};
    u64 descriptor_cache_misses {0};
    u64 decoded_methods {0};
    u64 decoded_instructions {0};
    u64 decoded_operands {0};
    u64 decoded_switch_entries {0};
    u64 decoded_opcode_dispatches {0};
    u64 decoded_operand_dispatches {0};

    std::array<u64, static_cast<usize>(AllocationPayloadKind::count)>
        allocations_by_kind {};
    std::array<u64, static_cast<usize>(AllocationPayloadKind::count)>
        allocated_bytes_by_kind {};
    u64 failed_allocations {0};
    u64 public_locked_heap_operations {0};
    u64 vm_fast_heap_operations {0};
    u64 gc_count {0};
    u64 gc_total_nanoseconds {0};
    u64 gc_max_pause_nanoseconds {0};
    u64 gc_roots_scanned {0};
    u64 gc_objects_scanned {0};
    u64 gc_objects_reclaimed {0};
    u64 gc_primitive_bytes_scanned {0};

    u64 scheduler_state_transitions {0};
    u64 scheduler_queue_erase_scans {0};
    u64 scheduler_yields {0};
    u64 scheduler_sleeps {0};
    u64 scheduler_event_wakeups {0};
    u64 scheduler_spurious_wakeups {0};
};

class PerformanceCounters final {
public:
    [[nodiscard]] static constexpr bool enabled() noexcept {
        return PHONEME_ENABLE_VM_PROFILING != 0;
    }

#if PHONEME_ENABLE_VM_PROFILING
    static void reset() noexcept;
    [[nodiscard]] static PerformanceCounterSnapshot snapshot() noexcept;
    static void flush_thread_local() noexcept;

    static void record_opcode(u8 opcode) noexcept;
    static void record_method_invocation() noexcept;
    static void record_native_invocation() noexcept;
    static void observe_java_call_depth(usize depth) noexcept;
    static void record_exception_dispatch() noexcept;
    static void record_class_initialization() noexcept;
    static void record_instruction_budget_exit() noexcept;
    static void record_scheduler_quantum() noexcept;

    static void record_class_cache(bool hit) noexcept;
    static void record_method_resolution(bool hit, bool declared) noexcept;
    static void record_field_resolution(bool hit) noexcept;
    static void record_assignability_cache(bool hit) noexcept;
    static void record_native_registry_lookup() noexcept;
    static void record_metadata_key_construction() noexcept;
    static void record_virtual_inline_cache(bool hit) noexcept;
    static void record_direct_call_cache(bool hit) noexcept;
    static void record_operand_resolution(bool hit) noexcept;
    static void record_operand_resolution_failure() noexcept;
    static void record_descriptor_cache(bool hit) noexcept;
    static void record_decoded_method(usize instructions,
                                      usize operands,
                                      usize switch_entries) noexcept;
    static void record_decoded_opcode_dispatch() noexcept;
    static void record_decoded_operand_dispatch() noexcept;

    static void record_allocation(AllocationPayloadKind kind, usize bytes) noexcept;
    static void record_failed_allocation() noexcept;
    static void record_locked_heap_operation() noexcept;
    static void record_vm_fast_heap_operation() noexcept;
    static void record_gc(u64 pause_nanoseconds,
                          usize roots_scanned,
                          usize objects_scanned,
                          usize objects_reclaimed,
                          usize primitive_bytes_scanned) noexcept;

    static void record_scheduler_state_transition() noexcept;
    static void record_scheduler_queue_erase_scan(usize visited) noexcept;
    static void record_scheduler_yield() noexcept;
    static void record_scheduler_sleep() noexcept;
    static void record_scheduler_event_wakeup() noexcept;
    static void record_scheduler_spurious_wakeup() noexcept;
#else
    static constexpr void reset() noexcept {}
    [[nodiscard]] static constexpr PerformanceCounterSnapshot snapshot() noexcept {
        return {};
    }
    static constexpr void flush_thread_local() noexcept {}

    static constexpr void record_opcode(u8) noexcept {}
    static constexpr void record_method_invocation() noexcept {}
    static constexpr void record_native_invocation() noexcept {}
    static constexpr void observe_java_call_depth(usize) noexcept {}
    static constexpr void record_exception_dispatch() noexcept {}
    static constexpr void record_class_initialization() noexcept {}
    static constexpr void record_instruction_budget_exit() noexcept {}
    static constexpr void record_scheduler_quantum() noexcept {}

    static constexpr void record_class_cache(bool) noexcept {}
    static constexpr void record_method_resolution(bool, bool) noexcept {}
    static constexpr void record_field_resolution(bool) noexcept {}
    static constexpr void record_assignability_cache(bool) noexcept {}
    static constexpr void record_native_registry_lookup() noexcept {}
    static constexpr void record_metadata_key_construction() noexcept {}
    static constexpr void record_virtual_inline_cache(bool) noexcept {}
    static constexpr void record_direct_call_cache(bool) noexcept {}
    static constexpr void record_operand_resolution(bool) noexcept {}
    static constexpr void record_operand_resolution_failure() noexcept {}
    static constexpr void record_descriptor_cache(bool) noexcept {}
    static constexpr void record_decoded_method(usize, usize, usize) noexcept {}
    static constexpr void record_decoded_opcode_dispatch() noexcept {}
    static constexpr void record_decoded_operand_dispatch() noexcept {}

    static constexpr void record_allocation(AllocationPayloadKind, usize) noexcept {}
    static constexpr void record_failed_allocation() noexcept {}
    static constexpr void record_locked_heap_operation() noexcept {}
    static constexpr void record_vm_fast_heap_operation() noexcept {}
    static constexpr void record_gc(u64, usize, usize, usize, usize) noexcept {}

    static constexpr void record_scheduler_state_transition() noexcept {}
    static constexpr void record_scheduler_queue_erase_scan(usize) noexcept {}
    static constexpr void record_scheduler_yield() noexcept {}
    static constexpr void record_scheduler_sleep() noexcept {}
    static constexpr void record_scheduler_event_wakeup() noexcept {}
    static constexpr void record_scheduler_spurious_wakeup() noexcept {}
#endif
};

} // namespace phoneme::vm
