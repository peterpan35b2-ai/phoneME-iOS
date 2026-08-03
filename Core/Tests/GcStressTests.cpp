#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include "phoneme/vm/Heap.hpp"
#include "phoneme/vm/NativeRootScope.hpp"
#include "phoneme/vm/RootSet.hpp"

namespace {

using phoneme::ErrorCode;
using phoneme::usize;
using phoneme::vm::Heap;
using phoneme::vm::HeapLimits;
using phoneme::vm::NativeRootScope;
using phoneme::vm::ObjectRef;
using phoneme::vm::RootSet;
using phoneme::vm::Value;

[[noreturn]] void fail_test(const char* message) {
    std::cerr << "GcStressTests failure: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail_test(message);
    }
}

void test_graph_marking_and_stale_generation() {
    Heap heap(HeapLimits {.maximum_objects = 16U, .maximum_bytes = 16U * 1024U});
    auto parent = heap.allocate_object("test/Parent", 1U);
    auto child = heap.allocate_object("test/Child", 0U);
    require(parent.has_value() && child.has_value(),
            "failed to allocate graph objects");
    require(heap.set_field(*parent, 0U, Value::from_reference(*child)).has_value(),
            "failed to connect graph objects");

    const std::array<ObjectRef, 1U> roots {*parent};
    require(heap.collect(roots).has_value(), "rooted collection failed");
    require(heap.stats().live_objects == 2U,
            "reachable child was collected");

    const std::array<ObjectRef, 0U> no_roots {};
    require(heap.collect(no_roots).has_value(), "unrooted collection failed");
    require(heap.stats().live_objects == 0U,
            "unreachable graph was not collected");
    require(!heap.class_name(*child).has_value(),
            "stale reference remained valid after collection");

    auto replacement = heap.allocate_object("test/Replacement", 0U);
    require(replacement.has_value(), "failed to reuse collected slot");
    require(replacement->slot() == child->slot(),
            "free slot was not reused");
    require(replacement->generation() != child->generation(),
            "slot reuse did not advance generation");
}

void test_object_and_byte_limits() {
    Heap object_limited(
        HeapLimits {.maximum_objects = 1U, .maximum_bytes = 64U * 1024U});
    require(object_limited.allocate_object("test/One", 0U).has_value(),
            "first object allocation failed");
    auto second = object_limited.allocate_object("test/Two", 0U);
    require(!second.has_value() && second.error().code == ErrorCode::overflow,
            "object limit did not return overflow");

    Heap byte_limited(
        HeapLimits {.maximum_objects = 32U, .maximum_bytes = 512U});
    auto oversized = byte_limited.allocate_array("[I", 100U,
                                                  Value::from_int(0));
    require(!oversized.has_value() &&
                oversized.error().code == ErrorCode::overflow,
            "byte limit did not reject oversized array");
    require(byte_limited.stats().live_objects == 0U,
            "failed allocation consumed a heap slot");
    require(byte_limited.stats().failed_allocations == 1U,
            "failed allocation was not counted");

    for (usize attempt = 0U; attempt < 16U; ++attempt) {
        auto repeated = byte_limited.allocate_array("[J", 100U,
                                                     Value::from_long(0));
        require(!repeated.has_value(),
                "repeated over-budget allocation unexpectedly succeeded");
    }
    require(byte_limited.stats().live_objects == 0U,
            "repeated allocation failure corrupted heap state");
}

void test_overflow_safe_array_allocation() {
    Heap heap(HeapLimits {
        .maximum_objects = 8U,
        .maximum_bytes = std::numeric_limits<usize>::max(),
    });
    auto array = heap.allocate_array("[B",
                                     std::numeric_limits<usize>::max(),
                                     Value::from_int(0));
    require(!array.has_value() && array.error().code == ErrorCode::overflow,
            "array size multiplication overflow was not rejected");
    require(heap.stats().live_objects == 0U,
            "overflowing allocation changed live object count");
}

void test_string_accounting_is_atomic() {
    Heap heap(HeapLimits {.maximum_objects = 8U, .maximum_bytes = 512U});
    auto string = heap.allocate_object("java/lang/String", 0U);
    require(string.has_value(), "failed to allocate String object");
    require(heap.attach_string(*string, u"small").has_value(),
            "failed to attach small String payload");
    const usize before = heap.stats().estimated_bytes;

    auto oversized = heap.attach_string(*string, std::u16string(512U, u'x'));
    require(!oversized.has_value() &&
                oversized.error().code == ErrorCode::overflow,
            "oversized String payload was accepted");
    auto value = heap.string_value(*string);
    require(value.has_value() && *value == u"small",
            "failed String update mutated the existing payload");
    require(heap.stats().estimated_bytes == before,
            "failed String update changed byte accounting");
}

void test_native_root_scope_lifetime() {
    Heap heap(HeapLimits {.maximum_objects = 16U, .maximum_bytes = 16U * 1024U});
    RootSet roots(8U);
    auto object = heap.allocate_object("test/Pinned", 0U);
    require(object.has_value(), "failed to allocate pinned object");

    phoneme::vm::RootHandle stale_handle {};
    {
        auto scope_result = NativeRootScope::pin(roots, *object);
        require(scope_result.has_value(), "failed to pin native root");
        NativeRootScope scope = std::move(*scope_result);
        stale_handle = scope.handle();

        std::vector<ObjectRef> snapshot;
        roots.append_reference_roots(snapshot);
        require(snapshot.size() == 1U && snapshot.front() == *object,
                "native root snapshot is incorrect");
        require(heap.collect(snapshot).has_value(),
                "collection with native root failed");
        require(heap.class_name(*object).has_value(),
                "native-rooted object was collected");

        auto moved = std::move(scope);
        require(!scope.active() && moved.active(),
                "native root move did not transfer ownership");
        require(moved.reset(*object).has_value(),
                "native root reset failed");
    }

    require(roots.stats().live_roots == 0U,
            "native root scope leaked its pin");
    require(!roots.update(stale_handle, *object).has_value(),
            "stale native root handle remained usable");

    std::vector<ObjectRef> no_roots;
    roots.append_reference_roots(no_roots);
    require(no_roots.empty(), "released native root remained published");
    require(heap.collect(no_roots).has_value(),
            "collection after native root release failed");
    require(!heap.class_name(*object).has_value(),
            "released native-rooted object remained live");
}

void test_root_set_clear_invalidates_handles() {
    RootSet roots(4U);
    auto first = roots.pin(ObjectRef::make(1U, 1U));
    require(first.has_value(), "failed to create root handle");
    roots.clear();
    require(!roots.value(*first).has_value(),
            "RootSet::clear did not invalidate old handle");

    auto second = roots.pin(ObjectRef::make(2U, 1U));
    require(second.has_value(), "failed to reuse cleared root slot");
    require(second->slot() == first->slot(),
            "cleared root slot was not reused");
    require(second->generation() != first->generation(),
            "cleared root slot did not advance generation");
}

void test_root_set_concurrent_pin_unpin() {
    RootSet roots(4096U);
    std::atomic_bool failed {false};
    std::vector<std::thread> workers;
    workers.reserve(8U);
    for (usize worker = 0U; worker < 8U; ++worker) {
        workers.emplace_back([worker, &roots, &failed] {
            for (usize iteration = 0U; iteration < 1000U; ++iteration) {
                const auto slot = static_cast<phoneme::u32>(worker + 1U);
                const auto generation = static_cast<phoneme::u32>(iteration + 1U);
                auto handle = roots.pin(ObjectRef::make(slot, generation));
                if (!handle || !roots.unpin(*handle)) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    require(!failed.load(std::memory_order_acquire),
            "concurrent native root operations failed");
    require(roots.stats().live_roots == 0U,
            "concurrent native root operations leaked roots");
}

} // namespace

int main() {
    test_graph_marking_and_stale_generation();
    test_object_and_byte_limits();
    test_overflow_safe_array_allocation();
    test_string_accounting_is_atomic();
    test_native_root_scope_lifetime();
    test_root_set_clear_invalidates_handles();
    test_root_set_concurrent_pin_unpin();

    std::cout << "GcStressTests passed\n";
    return 0;
}
