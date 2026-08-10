#include "phoneme/vm/Heap.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include "phoneme/base/Checked.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"
#include "phoneme/vm/VmTrace.hpp"

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

[[nodiscard]] std::optional<HeapArrayKind> heap_array_kind(
    std::string_view class_name) noexcept {
    if (class_name == "[Z") return HeapArrayKind::boolean;
    if (class_name == "[B") return HeapArrayKind::byte;
    if (class_name == "[C") return HeapArrayKind::character;
    if (class_name == "[S") return HeapArrayKind::short_integer;
    if (class_name == "[I") return HeapArrayKind::integer;
    if (class_name == "[J") return HeapArrayKind::long_integer;
    if (class_name == "[F") return HeapArrayKind::float32;
    if (class_name == "[D") return HeapArrayKind::float64;
    if (class_name.starts_with("[L") || class_name.starts_with("[[")) {
        return HeapArrayKind::reference;
    }
    return std::nullopt;
}

[[nodiscard]] std::string reference_array_component(
    std::string_view class_name) {
    if (class_name.starts_with("[L") && class_name.ends_with(';') &&
        class_name.size() >= 4U) {
        return std::string(class_name.substr(2U, class_name.size() - 3U));
    }
    if (class_name.starts_with("[[")) {
        return std::string(class_name.substr(1U));
    }
    return {};
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
                         std::to_string(
                             g_heap_access_context.current_bytecode_pc());
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
    update_automatic_collection_threshold_unlocked();
}

Result<ObjectRef> Heap::allocate_object(std::string class_name,
                                        usize field_count) {
    PerformanceCounters::record_locked_heap_operation();
    if (class_name.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "object class name must not be empty");
    }

    std::scoped_lock lock(mutex_);
    auto object_bytes = estimate_object_bytes(class_name, field_count, 0U, 0U);
    if (!object_bytes) {
        ++failed_allocations_;
        PerformanceCounters::record_failed_allocation();
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
        .weak_referent = std::nullopt,
        .array_kind = std::nullopt,
        .is_array = false,
        .is_string = false,
        .marked = false,
    };
    auto allocated = allocate_unlocked(std::move(object), *object_bytes);
    if (allocated) {
        PerformanceCounters::record_allocation(
            AllocationPayloadKind::object, *object_bytes);
    }
    return allocated;
}

Result<ObjectRef> Heap::allocate_array(std::string class_name,
                                       usize length,
                                       Value initial_value) {
    PerformanceCounters::record_locked_heap_operation();
    if (class_name.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "array class name must not be empty");
    }

    std::scoped_lock lock(mutex_);
    auto object_bytes = estimate_object_bytes(class_name, 0U, length, 0U);
    if (!object_bytes) {
        ++failed_allocations_;
        PerformanceCounters::record_failed_allocation();
        return std::unexpected(object_bytes.error());
    }
    auto capacity = ensure_capacity_unlocked(*object_bytes);
    if (!capacity) {
        return std::unexpected(capacity.error());
    }

    const auto array_kind = heap_array_kind(class_name);
    Object object {
        .class_name = std::move(class_name),
        .fields = {},
        .elements = std::vector<Value>(length, initial_value),
        .string_payload = {},
        .weak_referent = std::nullopt,
        .array_kind = array_kind,
        .is_array = true,
        .is_string = false,
        .marked = false,
    };
    auto allocated = allocate_unlocked(std::move(object), *object_bytes);
    if (allocated) {
        PerformanceCounters::record_allocation(
            AllocationPayloadKind::array, *object_bytes);
    }
    return allocated;
}

Result<ObjectRef> Heap::clone_object(ObjectRef reference) {
    PerformanceCounters::record_locked_heap_operation();
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
    const usize clone_bytes = slots_[*slot].accounted_bytes;
    auto allocated = allocate_unlocked(std::move(clone), clone_bytes);
    if (allocated) {
        PerformanceCounters::record_allocation(
            AllocationPayloadKind::clone, clone_bytes);
    }
    return allocated;
}

Result<Value> Heap::field(ObjectRef reference, usize index) const {
    PerformanceCounters::record_locked_heap_operation();
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
    PerformanceCounters::record_locked_heap_operation();
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

Result<Value> Heap::vm_field(ObjectRef reference, usize index) const {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.vm_field", reference);
    }
    const Object& object = slots_[*slot].object;
    if (object.is_array || index >= object.fields.size()) {
        return fail(ErrorCode::out_of_range, "object field index is out of range");
    }
    return object.fields[index];
}

Status Heap::vm_set_field(ObjectRef reference, usize index, Value value) {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.vm_set_field", reference);
    }
    Object& object = slots_[*slot].object;
    if (object.is_array || index >= object.fields.size()) {
        return fail(ErrorCode::out_of_range, "object field index is out of range");
    }
    object.fields[index] = value;
    return {};
}

Result<Value> Heap::element(ObjectRef reference, usize index) const {
    PerformanceCounters::record_locked_heap_operation();
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
    PerformanceCounters::record_locked_heap_operation();
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

Result<HeapArrayInfo> Heap::array_info(ObjectRef reference) const {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.array_info", reference);
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    const auto kind = object.array_kind;
    if (!kind.has_value()) {
        return fail(ErrorCode::invalid_state, "array has an invalid class descriptor");
    }
    HeapArrayInfo info {
        .kind = *kind,
        .length = object.elements.size(),
    };
    if (*kind == HeapArrayKind::reference) {
        info.reference_component = reference_array_component(object.class_name);
        if (info.reference_component.empty()) {
            return fail(ErrorCode::invalid_state,
                        "reference array has an invalid component descriptor");
        }
    }
    return info;
}

Result<HeapArrayInfo> Heap::vm_array_info(ObjectRef reference) const {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.vm_array_info", reference);
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    const auto kind = object.array_kind;
    if (!kind.has_value()) {
        return fail(ErrorCode::invalid_state, "array has an invalid class descriptor");
    }
    HeapArrayInfo info {
        .kind = *kind,
        .length = object.elements.size(),
    };
    if (*kind == HeapArrayKind::reference) {
        info.reference_component = reference_array_component(object.class_name);
        if (info.reference_component.empty()) {
            return fail(ErrorCode::invalid_state,
                        "reference array has an invalid component descriptor");
        }
    }
    return info;
}

Result<HeapArrayElementSnapshot> Heap::array_element_snapshot(
    ObjectRef reference,
    usize index) const {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(
            slot.error(), "Heap.array_element_snapshot", reference);
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    if (index >= object.elements.size()) {
        return fail(ErrorCode::out_of_range, "array index is out of range");
    }
    const auto kind = object.array_kind;
    if (!kind.has_value()) {
        return fail(ErrorCode::invalid_state, "array has an invalid class descriptor");
    }
    return HeapArrayElementSnapshot {
        .kind = *kind,
        .value = object.elements[index],
    };
}

Result<HeapArrayElementSnapshot> Heap::vm_array_element_snapshot(
    ObjectRef reference,
    usize index) const {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(
            slot.error(), "Heap.vm_array_element_snapshot", reference);
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    if (index >= object.elements.size()) {
        return fail(ErrorCode::out_of_range, "array index is out of range");
    }
    const auto kind = object.array_kind;
    if (!kind.has_value()) {
        return fail(ErrorCode::invalid_state, "array has an invalid class descriptor");
    }
    return HeapArrayElementSnapshot {
        .kind = *kind,
        .value = object.elements[index],
    };
}

Status Heap::set_element_checked(ObjectRef reference,
                                 usize index,
                                 HeapArrayKind expected_kind,
                                 Value value) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(
            slot.error(), "Heap.set_element_checked", reference);
    }
    Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    if (index >= object.elements.size()) {
        return fail(ErrorCode::out_of_range, "array index is out of range");
    }
    const auto actual_kind = object.array_kind;
    if (!actual_kind.has_value() || *actual_kind != expected_kind) {
        return fail(ErrorCode::invalid_state,
                    "array element kind does not match requested store");
    }
    object.elements[index] = value;
    return {};
}

Status Heap::vm_set_element_checked(ObjectRef reference,
                                    usize index,
                                    HeapArrayKind expected_kind,
                                    Value value) {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(
            slot.error(), "Heap.vm_set_element_checked", reference);
    }
    Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    if (index >= object.elements.size()) {
        return fail(ErrorCode::out_of_range, "array index is out of range");
    }
    const auto actual_kind = object.array_kind;
    if (!actual_kind.has_value() || *actual_kind != expected_kind) {
        return fail(ErrorCode::invalid_state,
                    "array element kind does not match requested store");
    }
    object.elements[index] = value;
    return {};
}

Result<HeapArrayPayloadLease> Heap::array_payload_lease(
    ObjectRef reference,
    HeapArrayKind expected_kind) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(
            slot.error(), "Heap.array_payload_lease", reference);
    }
    Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    if (!object.array_kind.has_value() ||
        *object.array_kind != expected_kind) {
        return fail(ErrorCode::invalid_state,
                    "array payload kind does not match requested lease");
    }
    return HeapArrayPayloadLease {
        .first_payload = object.elements.empty()
            ? nullptr
            : object.elements.front().raw_bits_address_unchecked(),
        .length = object.elements.size(),
    };
}

Status Heap::fill_array_range(ObjectRef reference,
                              usize index,
                              usize length,
                              Value value) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(),
                                 "Heap.fill_array_range", reference);
    }
    Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state,
                    "array range fill requires an array object");
    }
    if (index > object.elements.size() ||
        length > object.elements.size() - index) {
        return fail(ErrorCode::out_of_range,
                    "array range fill is outside array bounds");
    }
    std::fill_n(
        object.elements.begin() + static_cast<std::ptrdiff_t>(index),
        length,
        value);
    return {};
}

Status Heap::copy_array_range(ObjectRef source,
                              usize source_index,
                              ObjectRef destination,
                              usize destination_index,
                              usize length) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto source_slot = resolve_slot_unlocked(source);
    if (!source_slot) {
        return heap_access_error(source_slot.error(),
                                 "Heap.copy_array_range source", source);
    }
    auto destination_slot = resolve_slot_unlocked(destination);
    if (!destination_slot) {
        return heap_access_error(destination_slot.error(),
                                 "Heap.copy_array_range destination",
                                 destination);
    }

    Object& source_object = slots_[*source_slot].object;
    Object& destination_object = slots_[*destination_slot].object;
    if (!source_object.is_array || !destination_object.is_array) {
        return fail(ErrorCode::invalid_state,
                    "array range copy requires array objects");
    }
    if (source_index > source_object.elements.size() ||
        length > source_object.elements.size() - source_index ||
        destination_index > destination_object.elements.size() ||
        length > destination_object.elements.size() - destination_index) {
        return fail(ErrorCode::out_of_range,
                    "array range copy is outside array bounds");
    }
    if (length == 0U) return {};

    auto source_begin = source_object.elements.begin() +
                        static_cast<std::ptrdiff_t>(source_index);
    auto destination_begin = destination_object.elements.begin() +
                             static_cast<std::ptrdiff_t>(destination_index);
    if (*source_slot == *destination_slot &&
        destination_index > source_index &&
        destination_index < source_index + length) {
        std::copy_backward(
            source_begin,
            source_begin + static_cast<std::ptrdiff_t>(length),
            destination_begin + static_cast<std::ptrdiff_t>(length));
    } else {
        std::copy_n(source_begin, length, destination_begin);
    }
    return {};
}

Result<std::vector<u8>> Heap::read_byte_array(ObjectRef reference,
                                               usize offset,
                                               usize length) const {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(),
                                 "Heap.read_byte_array", reference);
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array || object.class_name != "[B") {
        return fail(ErrorCode::invalid_argument,
                    "byte array read requires byte[]");
    }
    if (offset > object.elements.size() ||
        length > object.elements.size() - offset) {
        return fail(ErrorCode::out_of_range,
                    "byte array read is outside array bounds");
    }

    std::vector<u8> bytes;
    bytes.reserve(length);
    for (usize index = 0; index < length; ++index) {
        auto integer = object.elements[offset + index].as_int();
        if (!integer) return std::unexpected(integer.error());
        bytes.push_back(static_cast<u8>(static_cast<i8>(*integer)));
    }
    return bytes;
}

Status Heap::write_byte_array(ObjectRef reference,
                              usize offset,
                              std::span<const u8> bytes) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(),
                                 "Heap.write_byte_array", reference);
    }
    Object& object = slots_[*slot].object;
    if (!object.is_array || object.class_name != "[B") {
        return fail(ErrorCode::invalid_argument,
                    "byte array write requires byte[]");
    }
    if (offset > object.elements.size() ||
        bytes.size() > object.elements.size() - offset) {
        return fail(ErrorCode::out_of_range,
                    "byte array write is outside array bounds");
    }

    for (usize index = 0; index < bytes.size(); ++index) {
        object.elements[offset + index] = Value::from_int(
            static_cast<i32>(static_cast<i8>(bytes[index])));
    }
    return {};
}

Result<usize> Heap::array_length(ObjectRef reference) const {
    PerformanceCounters::record_locked_heap_operation();
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

Result<usize> Heap::vm_array_length(ObjectRef reference) const {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.vm_array_length", reference);
    }
    const Object& object = slots_[*slot].object;
    if (!object.is_array) {
        return fail(ErrorCode::invalid_state, "object is not an array");
    }
    return object.elements.size();
}

Result<std::string> Heap::class_name(ObjectRef reference) const {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.class_name", reference);
    }
    return slots_[*slot].object.class_name;
}

Result<std::string> Heap::vm_class_name(ObjectRef reference) const {
    PerformanceCounters::record_vm_fast_heap_operation();
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.vm_class_name", reference);
    }
    return slots_[*slot].object.class_name;
}

Status Heap::attach_string(ObjectRef reference, std::u16string value) {
    PerformanceCounters::record_locked_heap_operation();
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
        PerformanceCounters::record_failed_allocation();
        return std::unexpected(updated_bytes.error());
    }
    if (*updated_bytes > target.accounted_bytes) {
        const usize growth = *updated_bytes - target.accounted_bytes;
        if (growth > limits_.maximum_bytes - live_bytes_) {
            ++failed_allocations_;
            PerformanceCounters::record_failed_allocation();
            return fail(ErrorCode::overflow, "Java heap byte limit reached");
        }
        live_bytes_ += growth;
        peak_bytes_ = std::max(peak_bytes_, live_bytes_);
    } else {
        live_bytes_ -= target.accounted_bytes - *updated_bytes;
    }

    const usize previous_bytes = target.accounted_bytes;
    object.string_payload = std::move(value);
    object.is_string = true;
    target.accounted_bytes = *updated_bytes;
    if (*updated_bytes > previous_bytes) {
        PerformanceCounters::record_allocation(
            AllocationPayloadKind::string_payload,
            *updated_bytes - previous_bytes);
    }
    return {};
}

Result<std::u16string> Heap::string_value(ObjectRef reference) const {
    PerformanceCounters::record_locked_heap_operation();
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

Status Heap::set_weak_referent(ObjectRef reference, ObjectRef referent) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.set_weak_referent", reference);
    }
    if (!referent.is_null()) {
        auto referent_slot = resolve_slot_unlocked(referent);
        if (!referent_slot) {
            return heap_access_error(referent_slot.error(),
                                     "Heap.set_weak_referent referent",
                                     referent);
        }
    }
    slots_[*slot].object.weak_referent = referent;
    return {};
}

Result<ObjectRef> Heap::weak_referent(ObjectRef reference) const {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.weak_referent", reference);
    }
    const auto referent = slots_[*slot].object.weak_referent;
    if (!referent.has_value() || referent->is_null()) {
        return ObjectRef {};
    }
    auto referent_slot = resolve_slot_unlocked(*referent);
    if (!referent_slot) {
        return ObjectRef {};
    }
    return *referent;
}

Status Heap::clear_weak_referent(ObjectRef reference) {
    PerformanceCounters::record_locked_heap_operation();
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(reference);
    if (!slot) {
        return heap_access_error(slot.error(), "Heap.clear_weak_referent", reference);
    }
    slots_[*slot].object.weak_referent.reset();
    return {};
}

Status Heap::collect(std::span<const ObjectRef> roots) {
    PerformanceCounters::record_locked_heap_operation();
    const auto collection_started = std::chrono::steady_clock::now();
    std::scoped_lock lock(mutex_);
    const usize bytes_before = live_bytes_;
    usize objects_scanned = 0U;
    usize primitive_bytes_scanned = 0U;
    for (Slot& slot : slots_) {
        if (slot.occupied) {
            ++objects_scanned;
            if (slot.object.is_array && slot.object.class_name.size() >= 2U &&
                slot.object.class_name.front() == '[' &&
                slot.object.class_name[1U] != 'L' &&
                slot.object.class_name[1U] != '[') {
                primitive_bytes_scanned = saturated_add(
                    primitive_bytes_scanned,
                    slot.object.elements.size() * sizeof(Value));
            }
            slot.object.marked = false;
        }
    }
    vm_trace("gc",
             "begin roots=%zu live=%zu bytes=%zu capacity=%zu",
             roots.size(),
             objects_scanned,
             bytes_before,
             slots_.size());

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

    for (Slot& slot : slots_) {
        if (!slot.occupied || !slot.object.weak_referent.has_value()) {
            continue;
        }
        const ObjectRef referent = *slot.object.weak_referent;
        if (referent.is_null() || referent.slot() == 0U) {
            slot.object.weak_referent.reset();
            continue;
        }
        const usize referent_index = static_cast<usize>(referent.slot() - 1U);
        if (referent_index >= slots_.size()) {
            slot.object.weak_referent.reset();
            continue;
        }
        const Slot& target = slots_[referent_index];
        if (!target.occupied || target.generation != referent.generation() ||
            !target.object.marked) {
            slot.object.weak_referent.reset();
        }
    }

    usize objects_reclaimed = 0U;
    for (usize index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (!slot.occupied || slot.object.marked) {
            continue;
        }
        ++objects_reclaimed;
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
    update_automatic_collection_threshold_unlocked();
    const auto collection_finished = std::chrono::steady_clock::now();
    const auto pause = std::chrono::duration_cast<std::chrono::nanoseconds>(
        collection_finished - collection_started).count();
    PerformanceCounters::record_gc(
        pause > 0 ? static_cast<u64>(pause) : 0U,
        roots.size(),
        objects_scanned,
        objects_reclaimed,
        primitive_bytes_scanned);
    vm_trace("gc",
             "end roots=%zu scanned=%zu reclaimed=%zu live=%zu->%zu "
             "bytes=%zu->%zu pause_us=%lld collections=%llu",
             roots.size(),
             objects_scanned,
             objects_reclaimed,
             objects_scanned,
             objects_scanned - objects_reclaimed,
             bytes_before,
             live_bytes_,
             static_cast<long long>(pause / 1'000),
             static_cast<unsigned long long>(collections_));
    return {};
}

void Heap::clear() noexcept {
    std::scoped_lock lock(mutex_);
    slots_.clear();
    free_slots_.clear();
    live_bytes_ = 0U;
    peak_bytes_ = 0U;
    update_automatic_collection_threshold_unlocked();
    collections_ = 0U;
    failed_allocations_ = 0U;
}

usize Heap::estimated_bytes() const noexcept {
    std::scoped_lock lock(mutex_);
    return live_bytes_;
}

bool Heap::automatic_collection_due() const noexcept {
    std::scoped_lock lock(mutex_);
    return automatic_collection_threshold_ != 0U &&
           live_bytes_ >= automatic_collection_threshold_;
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
        PerformanceCounters::record_failed_allocation();
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
        PerformanceCounters::record_failed_allocation();
        return fail(ErrorCode::overflow, "Java heap object limit reached");
    }
    if (live_bytes_ > limits_.maximum_bytes ||
        object_bytes > limits_.maximum_bytes - live_bytes_) {
        ++failed_allocations_;
        PerformanceCounters::record_failed_allocation();
        return fail(ErrorCode::overflow, "Java heap byte limit reached");
    }
    return {};
}

void Heap::update_automatic_collection_threshold_unlocked() noexcept {
    const usize maximum = limits_.maximum_bytes;
    if (maximum == 0U) {
        automatic_collection_threshold_ = 0U;
        return;
    }
    if (live_bytes_ >= maximum) {
        automatic_collection_threshold_ = maximum;
        return;
    }

    // Start collecting at half the configured heap. After each collection,
    // retain half of the remaining free space as headroom. This adapts upward
    // for games with a genuinely large live set while keeping enough reserve
    // that small native allocations (notably String payload attachment) cannot
    // hit the hard limit between interpreter safepoints.
    const usize initial = std::max<usize>(maximum / 2U, 1U);
    const usize remaining = maximum - live_bytes_;
    const usize growth = std::max<usize>(remaining / 2U, 1U);
    const usize adaptive = live_bytes_ + growth;
    automatic_collection_threshold_ = std::max(initial, adaptive);
    automatic_collection_threshold_ =
        std::min(automatic_collection_threshold_, maximum);
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

    // Primitive Java arrays cannot contain object references. Skipping their
    // payload here is especially important for image/audio buffers and M3G
    // geometry, where a single reachable array can contain tens of thousands
    // of Value slots. Multi-dimensional arrays and object arrays remain
    // reference arrays and must still be traced element-by-element.
    const bool trace_array_elements =
        !slot.object.is_array ||
        !slot.object.array_kind.has_value() ||
        *slot.object.array_kind == HeapArrayKind::reference;
    if (trace_array_elements) {
        for (const Value& value : slot.object.elements) {
            append_reference(value);
        }
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
