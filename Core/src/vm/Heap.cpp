#include "phoneme/vm/Heap.hpp"

#include <limits>
#include <utility>

#include "phoneme/base/Checked.hpp"

namespace phoneme::vm {
namespace {

[[nodiscard]] bool supports_text_payload(std::string_view class_name) noexcept {
    return class_name == "java/lang/String" ||
           class_name == "java/lang/StringBuilder" ||
           class_name == "java/lang/StringBuffer";
}

} // namespace

Heap::Heap(usize maximum_objects) : maximum_objects_(maximum_objects) {
    const usize hard_limit = static_cast<usize>(std::numeric_limits<u32>::max()) - 1;
    if (maximum_objects_ > hard_limit) {
        maximum_objects_ = hard_limit;
    }
}

Result<ObjectRef> Heap::allocate_object(std::string class_name,
                                        usize field_count) {
    if (class_name.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "object class name must not be empty");
    }
    Object object {
        .class_name = std::move(class_name),
        .fields = std::vector<Value>(field_count),
        .elements = {},
        .string_payload = {},
        .is_array = false,
        .is_string = false,
        .marked = false,
    };
    std::scoped_lock lock(mutex_);
    return allocate_unlocked(std::move(object));
}

Result<ObjectRef> Heap::allocate_array(std::string class_name,
                                       usize length,
                                       Value initial_value) {
    if (class_name.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "array class name must not be empty");
    }
    Object object {
        .class_name = std::move(class_name),
        .fields = {},
        .elements = std::vector<Value>(length, initial_value),
        .string_payload = {},
        .is_array = true,
        .is_string = false,
        .marked = false,
    };
    std::scoped_lock lock(mutex_);
    return allocate_unlocked(std::move(object));
}

Result<ObjectRef> Heap::clone_object(ObjectRef reference) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    Object clone = slots_[*slot].object;
    clone.marked = false;
    return allocate_unlocked(std::move(clone));
}

Result<Value> Heap::field(ObjectRef reference, usize index) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    const Object& object = slots_[*slot].object;
    if (object.is_array || index >= object.fields.size()) {
        return fail(ErrorCode::out_of_range, "object field index is out of range");
    }
    return object.fields[index];
}

Status Heap::set_field(ObjectRef reference, usize index, Value value) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    Object& object = slots_[*slot].object;
    if (object.is_array || index >= object.fields.size()) {
        return fail(ErrorCode::out_of_range, "object field index is out of range");
    }
    object.fields[index] = value;
    return {};
}

Result<Value> Heap::element(ObjectRef reference, usize index) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array || index >= object.elements.size()) {
        return fail(ErrorCode::out_of_range, "array index is out of range");
    }
    return object.elements[index];
}

Status Heap::set_element(ObjectRef reference, usize index, Value value) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    Object& object = slots_[*slot].object;
    if (!object.is_array || index >= object.elements.size()) {
        return fail(ErrorCode::out_of_range, "array index is out of range");
    }
    object.elements[index] = value;
    return {};
}

Result<usize> Heap::array_length(ObjectRef reference) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    return object.elements.size();
}

Result<std::string> Heap::class_name(ObjectRef reference) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    return slots_[*slot].object.class_name;
}

Status Heap::attach_string(ObjectRef reference, std::u16string value) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    Object& object = slots_[*slot].object;
    if (object.is_array || !supports_text_payload(object.class_name)) {
        return fail(ErrorCode::invalid_argument,
                    "text payload can only be attached to String or a string builder");
    }
    object.string_payload = std::move(value);
    object.is_string = true;
    return {};
}

Result<std::u16string> Heap::string_value(ObjectRef reference) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_string || !supports_text_payload(object.class_name)) {
        return fail(ErrorCode::invalid_state,
                    "object does not contain a Java text payload");
    }
    return object.string_payload;
}

Status Heap::collect(std::span<const ObjectRef> roots) {
    std::scoped_lock lock(mutex_);
    for (Slot& slot : slots_) {
        if (slot.occupied) {
            slot.object.marked = false;
        }
    }

    std::vector<ObjectRef> pending;
    pending.reserve(roots.size());
    for (ObjectRef root : roots) {
        if (!root.is_null()) {
            pending.push_back(root);
        }
    }

    while (!pending.empty()) {
        const ObjectRef reference = pending.back();
        pending.pop_back();
        mark_unlocked(reference, pending);
    }

    for (usize index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (!slot.occupied || slot.object.marked) {
            continue;
        }
        slot.object = {};
        slot.occupied = false;
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        free_slots_.push_back(index);
    }
    ++collections_;
    return {};
}

void Heap::clear() noexcept {
    std::scoped_lock lock(mutex_);
    slots_.clear();
    free_slots_.clear();
    collections_ = 0;
}

HeapStats Heap::stats() const noexcept {
    std::scoped_lock lock(mutex_);
    HeapStats result {.collections = collections_};
    for (const Slot& slot : slots_) {
        if (!slot.occupied) {
            continue;
        }
        ++result.live_objects;
        result.live_slots += slot.object.fields.size() + slot.object.elements.size();
        result.estimated_bytes += estimate_object_bytes(slot.object);
    }
    return result;
}

Result<usize> Heap::resolve_slot_unlocked(ObjectRef reference) const {
    if (reference.is_null() || reference.slot() == 0) {
        return fail(ErrorCode::invalid_argument, "null object reference");
    }
    const usize index = static_cast<usize>(reference.slot() - 1U);
    if (index >= slots_.size()) {
        return fail(ErrorCode::invalid_argument, "object reference slot is invalid");
    }
    const Slot& slot = slots_[index];
    if (!slot.occupied || slot.generation != reference.generation()) {
        return fail(ErrorCode::invalid_argument, "stale object reference");
    }
    return index;
}

Result<ObjectRef> Heap::allocate_unlocked(Object object) {
    usize index = 0;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        if (slots_.size() >= maximum_objects_) {
            return fail(ErrorCode::overflow, "Java heap object limit reached");
        }
        index = slots_.size();
        slots_.push_back({});
    }

    Slot& slot = slots_[index];
    slot.occupied = true;
    slot.object = std::move(object);

    const usize encoded_slot = index + 1;
    auto slot32 = checked_narrow<u32>(encoded_slot);
    if (!slot32) {
        slot.object = {};
        slot.occupied = false;
        free_slots_.push_back(index);
        return std::unexpected(slot32.error());
    }
    return ObjectRef::make(*slot32, slot.generation);
}

void Heap::mark_unlocked(ObjectRef root, std::vector<ObjectRef>& pending) {
    if (root.is_null() || root.slot() == 0) {
        return;
    }
    const usize index = static_cast<usize>(root.slot() - 1U);
    if (index >= slots_.size()) {
        return;
    }
    Slot& slot = slots_[index];
    if (!slot.occupied || slot.generation != root.generation() ||
        slot.object.marked) {
        return;
    }
    slot.object.marked = true;

    const auto append_reference = [&pending](const Value& value) {
        if (value.kind() != ValueKind::reference) {
            return;
        }
        auto reference = value.as_reference();
        if (reference && !reference->is_null()) {
            pending.push_back(*reference);
        }
    };
    for (const Value& value : slot.object.fields) {
        append_reference(value);
    }
    for (const Value& value : slot.object.elements) {
        append_reference(value);
    }
}

usize Heap::estimate_object_bytes(const Object& object) noexcept {
    return sizeof(Object) + object.class_name.capacity() +
           object.fields.capacity() * sizeof(Value) +
           object.elements.capacity() * sizeof(Value) +
           object.string_payload.capacity() * sizeof(char16_t);
}

} // namespace phoneme::vm
