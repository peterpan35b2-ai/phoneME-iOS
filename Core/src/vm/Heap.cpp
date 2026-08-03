#include "phoneme/vm/Heap.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "phoneme/base/Checked.hpp"

namespace phoneme::vm {
namespace {

thread_local HeapAccessContext g_heap_access_context {};

[[nodiscard]] bool supports_text_payload(std::string_view class_name) noexcept {
    return class_name == "java/lang/String" ||
           class_name == "java/lang/StringBuilder" ||
           class_name == "java/lang/StringBuffer";
}

[[nodiscard]] usize saturated_add(usize left, usize right) noexcept {
    if (right > std::numeric_limits<usize>::max() - left) {
        return std::numeric_limits<usize>::max();
    }
    return left + right;
}

[[nodiscard]] std::unexpected<Error> heap_access_error(
    Error error,
    std::string_view operation,
    ObjectRef reference) {
    const std::string detail = std::move(error.message);
    error.message = std::string(operation) + " failed for object slot " +
                    std::to_string(reference.slot()) + " generation " +
                    std::to_string(reference.generation());
    if (!g_heap_access_context.owner.empty()) {
        error.message += " while executing " +
                         std::string(g_heap_access_context.owner) + "." +
                         std::string(g_heap_access_context.method) +
                         std::string(g_heap_access_context.descriptor) +
                         " at bytecode " +
                         std::to_string(g_heap_access_context.bytecode_pc);
    }
    error.message += ": " + detail;
    return std::unexpected(std::move(error));
}

} // namespace

HeapAccessContext current_heap_access_context() noexcept {
    return g_heap_access_context;
}

void set_heap_access_context(HeapAccessContext context) noexcept {
    g_heap_access_context = context;
}

Heap::Heap(usize maximum_objects)
    : Heap(HeapLimits {.maximum_objects = maximum_objects}) {}

Heap::Heap(HeapLimits limits) : limits_(limits) {
    const usize hard_limit =
        static_cast<usize>(std::numeric_limits<u32>::max()) - 1U;
    limits_.maximum_objects = std::min(limits_.maximum_objects, hard_limit);
}

Result<ObjectRef> Heap::allocate_object(std::string class_name,
                                        usize field_count) {
    if (class_name.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "object class name must not be empty");
    }

    std::scoped_lock lock(mutex_);
    auto object_bytes = estimate_object_bytes(class_name, field_count, 0U, 0U);
    if (!object_bytes) {
        ++failed_allocations_;
        return std::unexpected(object_bytes.error());
    }
    auto capacity = ensure_capacity_unlocked(*object_bytes);
    if (!capacity) {
        return std::unexpected(capacity.error());
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
    return allocate_unlocked(std::move(object), *object_bytes);
}

Result<ObjectRef> Heap::allocate_array(std::string class_name,
                                       usize length,
                                       Value initial_value) {
    if (class_name.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "array class name must not be empty");
    }

    std::scoped_lock lock(mutex_);
    auto object_bytes = estimate_object_bytes(class_name, 0U, length, 0U);
    if (!object_bytes) {
        ++failed_allocations_;
        return std::unexpected(object_bytes.error());
    }
    auto capacity = ensure_capacity_unlocked(*object_bytes);
    if (!capacity) {
        return std::unexpected(capacity.error());
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
    return allocate_unlocked(std::move(object), *object_bytes);
}

Result<ObjectRef> Heap::clone_object(ObjectRef reference) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.clone_object", reference);
    }

    const Object& source = slots_[*slot].object;
    auto capacity = ensure_capacity_unlocked(slots_[*slot].accounted_bytes);
    if (!capacity) {
        return std::unexpected(capacity.error());
    }

    Object clone = source;
    clone.marked = false;
    return allocate_unlocked(std::move(clone),
                             slots_[*slot].accounted_bytes);
}

Result<Value> Heap::field(ObjectRef reference, usize index) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.field", reference);
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
        return heap_access_error(slot.error(), "Heap.set_field", reference);
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
        return heap_access_error(slot.error(), "Heap.element", reference);
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
        return heap_access_error(slot.error(), "Heap.set_element", reference);
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
        return heap_access_error(slot.error(), "Heap.array_length", reference);
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
        return heap_access_error(slot.error(), "Heap.class_name", reference);
    }
    return slots_[*slot].object.class_name;
}

Status Heap::attach_string(ObjectRef reference, std::u16string value) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.attach_string", reference);
    }
    Slot& target = slots_[*slot];
    Object& object = target.object;
    if (object.is_array || !supports_text_payload(object.class_name)) {
        return fail(ErrorCode::invalid_argument,
                    "text payload can only be attached to String or a string builder");
    }

    auto updated_bytes = estimate_object_bytes(object.class_name,
                                               object.fields.size(),
                                               object.elements.size(),
                                               value.size());
    if (!updated_bytes) {
        ++failed_allocations_;
        return std::unexpected(updated_bytes.error());
    }
    if (*updated_bytes > target.accounted_bytes) {
        const usize growth = *updated_bytes - target.accounted_bytes;
        if (growth > limits_.maximum_bytes - live_bytes_) {
            ++failed_allocations_;
            return fail(ErrorCode::overflow, "Java heap byte limit reached");
        }
        live_bytes_ += growth;
        peak_bytes_ = std::max(peak_bytes_, live_bytes_);
    } else {
        live_bytes_ -= target.accounted_bytes - *updated_bytes;
    }

    object.string_payload = std::move(value);
    object.is_string = true;
    target.accounted_bytes = *updated_bytes;
    return {};
}

Result<std::u16string> Heap::string_value(ObjectRef reference) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.string_value", reference);
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
        live_bytes_ -= slot.accounted_bytes;
        slot.object = {};
        slot.accounted_bytes = 0U;
        slot.occupied = false;
        ++slot.generation;
        if (slot.generation == 0U) {
            slot.generation = 1U;
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
    live_bytes_ = 0U;
    peak_bytes_ = 0U;
    collections_ = 0U;
    failed_allocations_ = 0U;
}

HeapStats Heap::stats() const noexcept {
    std::scoped_lock lock(mutex_);
    HeapStats result {
        .estimated_bytes = live_bytes_,
        .peak_estimated_bytes = peak_bytes_,
        .maximum_objects = limits_.maximum_objects,
        .maximum_bytes = limits_.maximum_bytes,
        .collections = collections_,
        .failed_allocations = failed_allocations_,
    };
    for (const Slot& slot : slots_) {
        if (!slot.occupied) {
            continue;
        }
        ++result.live_objects;
        result.live_slots = saturated_add(
            result.live_slots,
            saturated_add(slot.object.fields.size(),
                          slot.object.elements.size()));
    }
    return result;
}

Result<usize> Heap::resolve_slot_unlocked(ObjectRef reference) const {
    if (reference.is_null() || reference.slot() == 0U) {
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

Result<ObjectRef> Heap::allocate_unlocked(Object object,
                                          usize accounted_bytes) {
    auto capacity = ensure_capacity_unlocked(accounted_bytes);
    if (!capacity) {
        return std::unexpected(capacity.error());
    }

    usize index = 0U;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = slots_.size();
        slots_.push_back({});
    }

    const usize encoded_slot = index + 1U;
    auto slot32 = checked_narrow<u32>(encoded_slot);
    if (!slot32) {
        ++failed_allocations_;
        if (index + 1U == slots_.size()) {
            slots_.pop_back();
        } else {
            free_slots_.push_back(index);
        }
        return std::unexpected(slot32.error());
    }

    Slot& slot = slots_[index];
    slot.occupied = true;
    slot.accounted_bytes = accounted_bytes;
    slot.object = std::move(object);
    live_bytes_ += accounted_bytes;
    peak_bytes_ = std::max(peak_bytes_, live_bytes_);
    return ObjectRef::make(*slot32, slot.generation);
}

Status Heap::ensure_capacity_unlocked(usize object_bytes) noexcept {
    if (free_slots_.empty() && slots_.size() >= limits_.maximum_objects) {
        ++failed_allocations_;
        return fail(ErrorCode::overflow, "Java heap object limit reached");
    }
    if (live_bytes_ > limits_.maximum_bytes ||
        object_bytes > limits_.maximum_bytes - live_bytes_) {
        ++failed_allocations_;
        return fail(ErrorCode::overflow, "Java heap byte limit reached");
    }
    return {};
}

void Heap::mark_unlocked(ObjectRef root, std::vector<ObjectRef>& pending) {
    if (root.is_null() || root.slot() == 0U) {
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

Result<usize> Heap::estimate_object_bytes(std::string_view class_name,
                                          usize field_count,
                                          usize element_count,
                                          usize text_length) noexcept {
    auto class_bytes = checked_add(class_name.size(), 1U);
    if (!class_bytes) {
        return std::unexpected(class_bytes.error());
    }
    auto field_bytes = checked_multiply(field_count, sizeof(Value));
    if (!field_bytes) {
        return std::unexpected(field_bytes.error());
    }
    auto element_bytes = checked_multiply(element_count, sizeof(Value));
    if (!element_bytes) {
        return std::unexpected(element_bytes.error());
    }
    auto text_bytes = checked_multiply(text_length, sizeof(char16_t));
    if (!text_bytes) {
        return std::unexpected(text_bytes.error());
    }

    auto total = checked_add(sizeof(Object), *class_bytes);
    if (!total) {
        return std::unexpected(total.error());
    }
    total = checked_add(*total, *field_bytes);
    if (!total) {
        return std::unexpected(total.error());
    }
    total = checked_add(*total, *element_bytes);
    if (!total) {
        return std::unexpected(total.error());
    }
    return checked_add(*total, *text_bytes);
}

} // namespace phoneme::vm
