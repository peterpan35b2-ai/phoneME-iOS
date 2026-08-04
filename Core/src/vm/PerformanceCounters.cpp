#include "phoneme/vm/PerformanceCounters.hpp"

#if PHONEME_ENABLE_VM_PROFILING

#include <algorithm>
#include <mutex>

namespace phoneme::vm {
namespace {

struct GlobalCounters final {
    std::mutex mutex;
    PerformanceCounterSnapshot snapshot;
};

GlobalCounters& global_counters() noexcept {
    static GlobalCounters counters;
    return counters;
}

thread_local PerformanceCounterSnapshot g_local_counters {};
thread_local bool g_local_dirty {false};

void mark_dirty() noexcept {
    g_local_dirty = true;
}

void merge_snapshot(PerformanceCounterSnapshot& destination,
                    const PerformanceCounterSnapshot& source) noexcept {
    destination.executed_bytecodes += source.executed_bytecodes;
    for (usize index = 0; index < destination.opcode_counts.size(); ++index) {
        destination.opcode_counts[index] += source.opcode_counts[index];
    }
    destination.method_invocations += source.method_invocations;
    destination.native_invocations += source.native_invocations;
    destination.maximum_java_call_depth = std::max(
        destination.maximum_java_call_depth,
        source.maximum_java_call_depth);
    destination.exception_dispatches += source.exception_dispatches;
    destination.class_initializations += source.class_initializations;
    destination.instruction_budget_exits += source.instruction_budget_exits;
    destination.scheduler_quanta += source.scheduler_quanta;

    destination.class_cache_hits += source.class_cache_hits;
    destination.class_cache_misses += source.class_cache_misses;
    destination.method_resolution_hits += source.method_resolution_hits;
    destination.method_resolution_misses += source.method_resolution_misses;
    destination.declared_method_resolution_hits +=
        source.declared_method_resolution_hits;
    destination.declared_method_resolution_misses +=
        source.declared_method_resolution_misses;
    destination.field_resolution_hits += source.field_resolution_hits;
    destination.field_resolution_misses += source.field_resolution_misses;
    destination.assignability_cache_hits += source.assignability_cache_hits;
    destination.assignability_cache_misses += source.assignability_cache_misses;
    destination.native_registry_lookups += source.native_registry_lookups;
    destination.metadata_key_constructions +=
        source.metadata_key_constructions;
    destination.virtual_inline_cache_hits += source.virtual_inline_cache_hits;
    destination.virtual_inline_cache_misses += source.virtual_inline_cache_misses;
    destination.direct_call_cache_hits += source.direct_call_cache_hits;
    destination.direct_call_cache_misses += source.direct_call_cache_misses;
    destination.operand_resolution_hits += source.operand_resolution_hits;
    destination.operand_resolution_misses += source.operand_resolution_misses;
    destination.operand_resolution_failures += source.operand_resolution_failures;
    destination.descriptor_cache_hits += source.descriptor_cache_hits;
    destination.descriptor_cache_misses += source.descriptor_cache_misses;
    destination.decoded_methods += source.decoded_methods;
    destination.decoded_instructions += source.decoded_instructions;
    destination.decoded_operands += source.decoded_operands;
    destination.decoded_switch_entries += source.decoded_switch_entries;
    destination.decoded_opcode_dispatches += source.decoded_opcode_dispatches;
    destination.decoded_operand_dispatches += source.decoded_operand_dispatches;

    for (usize index = 0; index < destination.allocations_by_kind.size(); ++index) {
        destination.allocations_by_kind[index] +=
            source.allocations_by_kind[index];
        destination.allocated_bytes_by_kind[index] +=
            source.allocated_bytes_by_kind[index];
    }
    destination.failed_allocations += source.failed_allocations;
    destination.public_locked_heap_operations +=
        source.public_locked_heap_operations;
    destination.vm_fast_heap_operations += source.vm_fast_heap_operations;
    destination.gc_count += source.gc_count;
    destination.gc_total_nanoseconds += source.gc_total_nanoseconds;
    destination.gc_max_pause_nanoseconds = std::max(
        destination.gc_max_pause_nanoseconds,
        source.gc_max_pause_nanoseconds);
    destination.gc_roots_scanned += source.gc_roots_scanned;
    destination.gc_objects_scanned += source.gc_objects_scanned;
    destination.gc_objects_reclaimed += source.gc_objects_reclaimed;
    destination.gc_primitive_bytes_scanned += source.gc_primitive_bytes_scanned;

    destination.scheduler_state_transitions +=
        source.scheduler_state_transitions;
    destination.scheduler_queue_erase_scans +=
        source.scheduler_queue_erase_scans;
    destination.scheduler_yields += source.scheduler_yields;
    destination.scheduler_sleeps += source.scheduler_sleeps;
    destination.scheduler_event_wakeups += source.scheduler_event_wakeups;
    destination.scheduler_spurious_wakeups += source.scheduler_spurious_wakeups;
}

} // namespace

void PerformanceCounters::reset() noexcept {
    auto& global = global_counters();
    std::scoped_lock lock(global.mutex);
    global.snapshot = {};
    g_local_counters = {};
    g_local_dirty = false;
}

PerformanceCounterSnapshot PerformanceCounters::snapshot() noexcept {
    flush_thread_local();
    auto& global = global_counters();
    std::scoped_lock lock(global.mutex);
    return global.snapshot;
}

void PerformanceCounters::flush_thread_local() noexcept {
    if (!g_local_dirty) return;
    auto& global = global_counters();
    {
        std::scoped_lock lock(global.mutex);
        merge_snapshot(global.snapshot, g_local_counters);
    }
    g_local_counters = {};
    g_local_dirty = false;
}

void PerformanceCounters::record_opcode(u8 opcode) noexcept {
    ++g_local_counters.executed_bytecodes;
    ++g_local_counters.opcode_counts[opcode];
    mark_dirty();
}

void PerformanceCounters::record_method_invocation() noexcept {
    ++g_local_counters.method_invocations;
    mark_dirty();
}

void PerformanceCounters::record_native_invocation() noexcept {
    ++g_local_counters.native_invocations;
    mark_dirty();
}

void PerformanceCounters::observe_java_call_depth(usize depth) noexcept {
    g_local_counters.maximum_java_call_depth = std::max(
        g_local_counters.maximum_java_call_depth,
        static_cast<u64>(depth));
    mark_dirty();
}

void PerformanceCounters::record_exception_dispatch() noexcept {
    ++g_local_counters.exception_dispatches;
    mark_dirty();
}

void PerformanceCounters::record_class_initialization() noexcept {
    ++g_local_counters.class_initializations;
    mark_dirty();
}

void PerformanceCounters::record_instruction_budget_exit() noexcept {
    ++g_local_counters.instruction_budget_exits;
    mark_dirty();
}

void PerformanceCounters::record_scheduler_quantum() noexcept {
    ++g_local_counters.scheduler_quanta;
    mark_dirty();
}

void PerformanceCounters::record_class_cache(bool hit) noexcept {
    if (hit) ++g_local_counters.class_cache_hits;
    else ++g_local_counters.class_cache_misses;
    mark_dirty();
}

void PerformanceCounters::record_method_resolution(bool hit,
                                                      bool declared) noexcept {
    if (declared) {
        if (hit) ++g_local_counters.declared_method_resolution_hits;
        else ++g_local_counters.declared_method_resolution_misses;
    } else {
        if (hit) ++g_local_counters.method_resolution_hits;
        else ++g_local_counters.method_resolution_misses;
    }
    mark_dirty();
}

void PerformanceCounters::record_field_resolution(bool hit) noexcept {
    if (hit) ++g_local_counters.field_resolution_hits;
    else ++g_local_counters.field_resolution_misses;
    mark_dirty();
}

void PerformanceCounters::record_assignability_cache(bool hit) noexcept {
    if (hit) ++g_local_counters.assignability_cache_hits;
    else ++g_local_counters.assignability_cache_misses;
    mark_dirty();
}

void PerformanceCounters::record_native_registry_lookup() noexcept {
    ++g_local_counters.native_registry_lookups;
    mark_dirty();
}

void PerformanceCounters::record_metadata_key_construction() noexcept {
    ++g_local_counters.metadata_key_constructions;
    mark_dirty();
}

void PerformanceCounters::record_virtual_inline_cache(bool hit) noexcept {
    if (hit) ++g_local_counters.virtual_inline_cache_hits;
    else ++g_local_counters.virtual_inline_cache_misses;
    mark_dirty();
}

void PerformanceCounters::record_direct_call_cache(bool hit) noexcept {
    if (hit) ++g_local_counters.direct_call_cache_hits;
    else ++g_local_counters.direct_call_cache_misses;
    mark_dirty();
}

void PerformanceCounters::record_operand_resolution(bool hit) noexcept {
    if (hit) ++g_local_counters.operand_resolution_hits;
    else ++g_local_counters.operand_resolution_misses;
    mark_dirty();
}

void PerformanceCounters::record_operand_resolution_failure() noexcept {
    ++g_local_counters.operand_resolution_failures;
    mark_dirty();
}

void PerformanceCounters::record_descriptor_cache(bool hit) noexcept {
    if (hit) ++g_local_counters.descriptor_cache_hits;
    else ++g_local_counters.descriptor_cache_misses;
    mark_dirty();
}

void PerformanceCounters::record_decoded_method(
    usize instructions,
    usize operands,
    usize switch_entries) noexcept {
    ++g_local_counters.decoded_methods;
    g_local_counters.decoded_instructions += static_cast<u64>(instructions);
    g_local_counters.decoded_operands += static_cast<u64>(operands);
    g_local_counters.decoded_switch_entries +=
        static_cast<u64>(switch_entries);
    mark_dirty();
}

void PerformanceCounters::record_decoded_opcode_dispatch() noexcept {
    ++g_local_counters.decoded_opcode_dispatches;
    mark_dirty();
}

void PerformanceCounters::record_decoded_operand_dispatch() noexcept {
    ++g_local_counters.decoded_operand_dispatches;
    mark_dirty();
}

void PerformanceCounters::record_allocation(AllocationPayloadKind kind,
                                             usize bytes) noexcept {
    const usize index = static_cast<usize>(kind);
    if (index >= g_local_counters.allocations_by_kind.size()) return;
    ++g_local_counters.allocations_by_kind[index];
    g_local_counters.allocated_bytes_by_kind[index] += static_cast<u64>(bytes);
    mark_dirty();
}

void PerformanceCounters::record_failed_allocation() noexcept {
    ++g_local_counters.failed_allocations;
    mark_dirty();
}

void PerformanceCounters::record_locked_heap_operation() noexcept {
    ++g_local_counters.public_locked_heap_operations;
    mark_dirty();
}

void PerformanceCounters::record_vm_fast_heap_operation() noexcept {
    ++g_local_counters.vm_fast_heap_operations;
    mark_dirty();
}

void PerformanceCounters::record_gc(u64 pause_nanoseconds,
                                    usize roots_scanned,
                                    usize objects_scanned,
                                    usize objects_reclaimed,
                                    usize primitive_bytes_scanned) noexcept {
    ++g_local_counters.gc_count;
    g_local_counters.gc_total_nanoseconds += pause_nanoseconds;
    g_local_counters.gc_max_pause_nanoseconds = std::max(
        g_local_counters.gc_max_pause_nanoseconds,
        pause_nanoseconds);
    g_local_counters.gc_roots_scanned += static_cast<u64>(roots_scanned);
    g_local_counters.gc_objects_scanned += static_cast<u64>(objects_scanned);
    g_local_counters.gc_objects_reclaimed += static_cast<u64>(objects_reclaimed);
    g_local_counters.gc_primitive_bytes_scanned +=
        static_cast<u64>(primitive_bytes_scanned);
    mark_dirty();
}

void PerformanceCounters::record_scheduler_state_transition() noexcept {
    ++g_local_counters.scheduler_state_transitions;
    mark_dirty();
}

void PerformanceCounters::record_scheduler_queue_erase_scan(usize visited) noexcept {
    g_local_counters.scheduler_queue_erase_scans += static_cast<u64>(visited);
    mark_dirty();
}

void PerformanceCounters::record_scheduler_yield() noexcept {
    ++g_local_counters.scheduler_yields;
    mark_dirty();
}

void PerformanceCounters::record_scheduler_sleep() noexcept {
    ++g_local_counters.scheduler_sleeps;
    mark_dirty();
}

void PerformanceCounters::record_scheduler_event_wakeup() noexcept {
    ++g_local_counters.scheduler_event_wakeups;
    mark_dirty();
}

void PerformanceCounters::record_scheduler_spurious_wakeup() noexcept {
    ++g_local_counters.scheduler_spurious_wakeups;
    mark_dirty();
}

} // namespace phoneme::vm

#endif
