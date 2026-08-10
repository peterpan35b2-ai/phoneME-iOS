#pragma once

#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;

struct HeapLimits final {
    usize maximum_objects {1'000'000};
    usize maximum_bytes {64U * 1024U * 1024U};
};

struct HeapStats final {
    usize live_objects {0};
    usize live_slots {0};
    usize estimated_bytes {0};
    usize peak_estimated_bytes {0};
    usize maximum_objects {0};
    usize maximum_bytes {0};
    usize collections {0};
    usize failed_allocations {0};
};

enum class HeapArrayKind : u8 {
    boolean,
    byte,
    character,
    short_integer,
    integer,
    long_integer,
    float32,
    float64,
    reference,
};

struct HeapArrayInfo final {
    HeapArrayKind kind {HeapArrayKind::integer};
    usize length {0};
    std::string reference_component;
};

struct HeapArrayElementSnapshot final {
    HeapArrayKind kind {HeapArrayKind::integer};
    Value value;
};

struct HeapArrayPayloadLease final {
    u64* first_payload {nullptr};
    usize length {0};
};

struct HeapAccessContext final {
    std::string_view owner;
    std::string_view method;
    std::string_view descriptor;
    usize bytecode_pc {0};
    const usize* live_bytecode_pc {nullptr};

    [[nodiscard]] usize current_bytecode_pc() const noexcept {
        return live_bytecode_pc != nullptr ? *live_bytecode_pc : bytecode_pc;
    }
};

[[nodiscard]] HeapAccessContext current_heap_access_context() noexcept;
void set_heap_access_context(HeapAccessContext context) noexcept;

class Heap final {
public:
    explicit Heap(usize maximum_objects = 1'000'000);
    explicit Heap(HeapLimits limits);

    [[nodiscard]] Result<ObjectRef> allocate_object(std::string class_name,
                                                    usize field_count);
    [[nodiscard]] Result<ObjectRef> allocate_array(std::string class_name,
                                                   usize length,
                                                   Value initial_value = {});
    [[nodiscard]] Result<ObjectRef> clone_object(ObjectRef reference);

    [[nodiscard]] Result<Value> field(ObjectRef reference, usize index) const;
    [[nodiscard]] Status set_field(ObjectRef reference, usize index, Value value);
    [[nodiscard]] Result<Value> element(ObjectRef reference, usize index) const;
    [[nodiscard]] Status set_element(ObjectRef reference, usize index, Value value);
    [[nodiscard]] Result<HeapArrayInfo> array_info(ObjectRef reference) const;
    [[nodiscard]] Result<HeapArrayElementSnapshot> array_element_snapshot(
        ObjectRef reference,
        usize index) const;
    [[nodiscard]] Status set_element_checked(ObjectRef reference,
                                             usize index,
                                             HeapArrayKind expected_kind,
                                             Value value);
    // Returns a stable pointer to the first Value payload for a fixed-size
    // Java array. The caller must keep `reference` rooted for the entire lease
    // lifetime and must not retain the pointer beyond such a rooted region.
    [[nodiscard]] Result<HeapArrayPayloadLease> array_payload_lease(
        ObjectRef reference,
        HeapArrayKind expected_kind);
    [[nodiscard]] Status fill_array_range(ObjectRef reference,
                                           usize index,
                                           usize length,
                                           Value value);
    [[nodiscard]] Status copy_array_range(ObjectRef source,
                                          usize source_index,
                                          ObjectRef destination,
                                          usize destination_index,
                                          usize length);
    [[nodiscard]] Result<std::vector<u8>> read_byte_array(
        ObjectRef reference,
        usize offset,
        usize length) const;
    [[nodiscard]] Status write_byte_array(ObjectRef reference,
                                          usize offset,
                                          std::span<const u8> bytes);
    [[nodiscard]] Result<usize> array_length(ObjectRef reference) const;
    [[nodiscard]] Result<std::string> class_name(ObjectRef reference) const;
    [[nodiscard]] Status attach_string(ObjectRef reference,
                                       std::u16string value);
    [[nodiscard]] Result<std::u16string> string_value(
        ObjectRef reference) const;

    [[nodiscard]] Status set_weak_referent(ObjectRef reference,
                                           ObjectRef referent);
    [[nodiscard]] Result<ObjectRef> weak_referent(ObjectRef reference) const;
    [[nodiscard]] Status clear_weak_referent(ObjectRef reference);

    [[nodiscard]] Status collect(std::span<const ObjectRef> roots);
    void clear() noexcept;
    [[nodiscard]] usize estimated_bytes() const noexcept;
    [[nodiscard]] bool automatic_collection_due() const noexcept;
    [[nodiscard]] HeapStats stats() const noexcept;

private:
    friend class Machine;

    // Fast bytecode-engine accessors. These intentionally skip Heap::mutex_
    // because Machine::execute() already serializes Java execution through the
    // per-VM execution gate. They must only be called from Machine while that
    // gate is held; public/native/host callers continue to use the locked API.
    [[nodiscard]] Result<Value> vm_field(ObjectRef reference,
                                         usize index) const;
    [[nodiscard]] Status vm_set_field(ObjectRef reference,
                                      usize index,
                                      Value value);
    [[nodiscard]] Result<HeapArrayInfo> vm_array_info(
        ObjectRef reference) const;
    [[nodiscard]] Result<HeapArrayElementSnapshot> vm_array_element_snapshot(
        ObjectRef reference,
        usize index) const;
    [[nodiscard]] Status vm_set_element_checked(ObjectRef reference,
                                                usize index,
                                                HeapArrayKind expected_kind,
                                                Value value);
    [[nodiscard]] Result<usize> vm_array_length(ObjectRef reference) const;
    [[nodiscard]] Result<std::string> vm_class_name(ObjectRef reference) const;

    struct Object final {
        std::string class_name;
        std::vector<Value> fields;
        std::vector<Value> elements;
        std::u16string string_payload;
        std::optional<ObjectRef> weak_referent;
        std::optional<HeapArrayKind> array_kind;
        bool is_array {false};
        bool is_string {false};
        bool marked {false};
    };

    struct Slot final {
        u32 generation {1};
        bool occupied {false};
        usize accounted_bytes {0};
        Object object;
    };

    [[nodiscard]] Result<usize> resolve_slot_unlocked(ObjectRef reference) const;
    [[nodiscard]] Result<ObjectRef> allocate_unlocked(Object object,
                                                      usize accounted_bytes);
    [[nodiscard]] Status ensure_capacity_unlocked(usize object_bytes) noexcept;
    void update_automatic_collection_threshold_unlocked() noexcept;
    void mark_unlocked(ObjectRef root, std::vector<ObjectRef>& pending);
    [[nodiscard]] static Result<usize> estimate_object_bytes(
        std::string_view class_name,
        usize field_count,
        usize element_count,
        usize text_length) noexcept;

    HeapLimits limits_ {};
    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<usize> free_slots_;
    usize live_bytes_ {0};
    usize peak_bytes_ {0};
    usize automatic_collection_threshold_ {0};
    usize collections_ {0};
    usize failed_allocations_ {0};
};

} // namespace phoneme::vm
