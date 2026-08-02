#pragma once

#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

struct HeapStats final {
    usize live_objects {0};
    usize live_slots {0};
    usize estimated_bytes {0};
    usize collections {0};
};

class Heap final {
public:
    explicit Heap(usize maximum_objects = 1'000'000);

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
    [[nodiscard]] Result<usize> array_length(ObjectRef reference) const;
    [[nodiscard]] Result<std::string> class_name(ObjectRef reference) const;
    [[nodiscard]] Status attach_string(ObjectRef reference,
                                       std::u16string value);
    [[nodiscard]] Result<std::u16string> string_value(
        ObjectRef reference) const;

    [[nodiscard]] Status collect(std::span<const ObjectRef> roots);
    void clear() noexcept;
    [[nodiscard]] HeapStats stats() const noexcept;

private:
    struct Object final {
        std::string class_name;
        std::vector<Value> fields;
        std::vector<Value> elements;
        std::u16string string_payload;
        bool is_array {false};
        bool is_string {false};
        bool marked {false};
    };

    struct Slot final {
        u32 generation {1};
        bool occupied {false};
        Object object;
    };

    [[nodiscard]] Result<usize> resolve_slot_unlocked(ObjectRef reference) const;
    [[nodiscard]] Result<ObjectRef> allocate_unlocked(Object object);
    void mark_unlocked(ObjectRef root, std::vector<ObjectRef>& pending);
    [[nodiscard]] static usize estimate_object_bytes(const Object& object) noexcept;

    usize maximum_objects_ {0};
    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<usize> free_slots_;
    usize collections_ {0};
};

} // namespace phoneme::vm
