#include "phoneme/vm/RuntimeMetadata.hpp"

#include <cstdint>
#include <limits>
#include <utility>

#include "phoneme/vm/PerformanceCounters.hpp"

namespace phoneme::vm {

Status OperandResolutionEntry::begin(
    OperandResolutionKind expected_kind) {
    if (expected_kind == OperandResolutionKind::none) {
        return fail(ErrorCode::invalid_argument,
                    "decoded operand resolution kind must be concrete");
    }
    if (state != OperandResolutionState::unresolved) {
        return fail(ErrorCode::invalid_state,
                    "decoded operand resolution did not begin unresolved");
    }
    kind = expected_kind;
    state = OperandResolutionState::resolving;
    return {};
}

Status OperandResolutionEntry::resolve_direct_call(
    MethodId resolved_method,
    NativeMethodId resolved_native_method,
    u64 resolved_native_generation) {
    if (state != OperandResolutionState::resolving ||
        kind != OperandResolutionKind::direct_call ||
        !resolved_method.valid() || resolved_native_generation == 0U) {
        return fail(ErrorCode::invalid_state,
                    "decoded direct call completed in an invalid state");
    }
    target_method = resolved_method;
    receiver_class = {};
    target_native_method = resolved_native_method;
    native_generation = resolved_native_generation;
    native_binding_cached = true;
    target_class = {};
    target_class_file.reset();
    target_class_name.clear();
    target_array_name.clear();
    failure.reset();
    state = OperandResolutionState::resolved;
    return {};
}

Status OperandResolutionEntry::begin_virtual_call(
    ClassId resolved_receiver_class) {
    if (!resolved_receiver_class.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "decoded virtual call requires a receiver class");
    }
    auto began = begin(OperandResolutionKind::virtual_call);
    if (!began) return began;
    receiver_class = resolved_receiver_class;
    return {};
}

Status OperandResolutionEntry::resolve_virtual_call(
    MethodId resolved_method,
    NativeMethodId resolved_native_method,
    u64 resolved_native_generation) {
    if (state != OperandResolutionState::resolving ||
        kind != OperandResolutionKind::virtual_call ||
        !receiver_class.valid() || !resolved_method.valid() ||
        resolved_native_generation == 0U) {
        return fail(ErrorCode::invalid_state,
                    "decoded virtual call completed in an invalid state");
    }
    target_method = resolved_method;
    target_native_method = resolved_native_method;
    native_generation = resolved_native_generation;
    native_binding_cached = true;
    target_class = {};
    target_class_file.reset();
    target_class_name.clear();
    target_array_name.clear();
    failure.reset();
    state = OperandResolutionState::resolved;
    return {};
}

Status OperandResolutionEntry::update_native_binding(
    NativeMethodId resolved_native_method,
    u64 resolved_native_generation) {
    if (state != OperandResolutionState::resolved ||
        (kind != OperandResolutionKind::direct_call &&
         kind != OperandResolutionKind::virtual_call) ||
        !target_method.valid() || resolved_native_generation == 0U) {
        return fail(ErrorCode::invalid_state,
                    "decoded native binding updated in an invalid state");
    }
    target_native_method = resolved_native_method;
    native_generation = resolved_native_generation;
    native_binding_cached = true;
    return {};
}

Status OperandResolutionEntry::resolve_class_reference(
    std::string resolved_class_name,
    ClassId resolved_class,
    std::shared_ptr<const classfile::ClassFile> resolved_class_file,
    std::string resolved_array_name) {
    if (state != OperandResolutionState::resolving ||
        kind != OperandResolutionKind::class_reference ||
        resolved_class_name.empty()) {
        return fail(ErrorCode::invalid_state,
                    "decoded class reference completed in an invalid state");
    }
    if (resolved_class_file != nullptr &&
        (!resolved_class.valid() ||
         resolved_class_file->name() != resolved_class_name)) {
        return fail(ErrorCode::invalid_state,
                    "decoded class reference metadata is inconsistent");
    }
    target_method = {};
    receiver_class = {};
    target_native_method = {};
    native_generation = 0U;
    native_binding_cached = false;
    target_class = resolved_class;
    target_class_file = std::move(resolved_class_file);
    target_class_name = std::move(resolved_class_name);
    target_array_name = std::move(resolved_array_name);
    failure.reset();
    state = OperandResolutionState::resolved;
    return {};
}

Status OperandResolutionEntry::fail_resolution(Error error) {
    if (state != OperandResolutionState::resolving ||
        kind == OperandResolutionKind::none ||
        error.code == ErrorCode::none) {
        return fail(ErrorCode::invalid_state,
                    "decoded operand failure was cached in an invalid state");
    }
    target_method = {};
    if (kind != OperandResolutionKind::virtual_call)
        receiver_class = {};
    target_native_method = {};
    native_generation = 0U;
    native_binding_cached = false;
    target_class = {};
    target_class_file.reset();
    target_class_name.clear();
    target_array_name.clear();
    failure = std::move(error);
    state = OperandResolutionState::failed;
    return {};
}

void OperandResolutionEntry::reset() noexcept {
    state = OperandResolutionState::unresolved;
    kind = OperandResolutionKind::none;
    target_method = {};
    receiver_class = {};
    target_native_method = {};
    native_generation = 0U;
    native_binding_cached = false;
    target_class = {};
    target_class_file.reset();
    target_class_name.clear();
    target_array_name.clear();
    failure.reset();
}

OperandResolutionTable::OperandResolutionTable(const DecodedMethod& decoded)
    : entries_(decoded.operands.size()) {
    for (const DecodedInstruction& instruction : decoded.instructions) {
        if (instruction.operand_index == kInvalidDecodedIndex ||
            instruction.operand_index >= entries_.size()) {
            continue;
        }
        entries_[instruction.operand_index].bytecode_pc =
            instruction.bytecode_pc;
    }
}

Result<OperandResolutionEntry*> OperandResolutionTable::entry(
    u32 operand_index,
    u32 bytecode_pc) noexcept {
    if (operand_index >= entries_.size()) {
        return fail(ErrorCode::out_of_range,
                    "decoded operand resolution index is out of range");
    }
    OperandResolutionEntry& resolved = entries_[operand_index];
    if (resolved.bytecode_pc == kInvalidDecodedIndex) {
        return fail(ErrorCode::invalid_state,
                    "decoded operand is not owned by an instruction");
    }
    if (resolved.bytecode_pc != bytecode_pc) {
        return fail(ErrorCode::invalid_state,
                    "decoded operand resolution BCI does not match its owner");
    }
    return &resolved;
}

const OperandResolutionEntry* OperandResolutionTable::entry_for_test(
    u32 operand_index) const noexcept {
    return operand_index < entries_.size() ? &entries_[operand_index] : nullptr;
}

usize RuntimeMetadata::MethodPointerKeyHash::operator()(
    MethodPointerKey key) const noexcept {
    const auto owner = reinterpret_cast<std::uintptr_t>(key.owner);
    const auto method = reinterpret_cast<std::uintptr_t>(key.method);
    const usize owner_hash = std::hash<std::uintptr_t>{}(owner);
    const usize method_hash = std::hash<std::uintptr_t>{}(method);
    return owner_hash ^ (method_hash + static_cast<usize>(0x9E3779B9U) +
                         (owner_hash << 6U) + (owner_hash >> 2U));
}

Result<std::shared_ptr<const RuntimeClass>> RuntimeMetadata::publish_class(
    std::shared_ptr<const classfile::ClassFile> class_file) {
    if (class_file == nullptr || class_file->name().empty()) {
        return fail(ErrorCode::invalid_argument,
                    "runtime class metadata requires a named class file");
    }

    std::scoped_lock lock(mutex_);
    if (const auto existing = classes_by_pointer_.find(class_file.get());
        existing != classes_by_pointer_.end()) {
        return existing->second;
    }
    if (const auto existing = classes_by_name_.find(class_file->name());
        existing != classes_by_name_.end()) {
        classes_by_pointer_.emplace(class_file.get(), existing->second);
        return existing->second;
    }
    if (next_class_id_ == 0U) {
        return fail(ErrorCode::overflow, "runtime class ID space is exhausted");
    }

    const std::string super_name = class_file->super_name();
    std::vector<std::string> interface_names = class_file->interfaces();
    std::vector<ClassId> interface_ids;
    interface_ids.reserve(interface_names.size());
    for (const std::string& interface_name : interface_names) {
        interface_ids.push_back(class_id_unlocked(interface_name));
    }
    const ClassId super_id = class_id_unlocked(super_name);
    auto runtime_class = std::make_shared<const RuntimeClass>(RuntimeClass {
        .id = ClassId {next_class_id_++},
        .class_file = std::move(class_file),
        .super_name = super_name,
        .super_id = super_id,
        .interface_names = std::move(interface_names),
        .interface_ids = std::move(interface_ids),
    });
    classes_by_pointer_.emplace(runtime_class->class_file.get(), runtime_class);
    classes_by_name_.emplace(runtime_class->class_file->name(), runtime_class);
    classes_by_id_.emplace(runtime_class->id, runtime_class);
    return runtime_class;
}

Result<std::shared_ptr<const RuntimeMethod>> RuntimeMetadata::publish_method(
    const std::shared_ptr<const classfile::ClassFile>& owner,
    const classfile::Method& method) {
    if (owner == nullptr) {
        return fail(ErrorCode::invalid_argument,
                    "runtime method metadata requires an owning class");
    }
    auto runtime_class = publish_class(owner);
    if (!runtime_class) return std::unexpected(runtime_class.error());
    auto descriptor = method_descriptor(method.descriptor);
    if (!descriptor) return std::unexpected(descriptor.error());

    const MethodPointerKey key {.owner = owner.get(), .method = &method};
    std::scoped_lock lock(mutex_);
    if (const auto existing = methods_.find(key); existing != methods_.end()) {
        return existing->second;
    }
    if (next_method_id_ == 0U) {
        return fail(ErrorCode::overflow, "runtime method ID space is exhausted");
    }
    const MethodId method_id {next_method_id_};
    std::shared_ptr<const DecodedMethod> decoded;
    std::shared_ptr<OperandResolutionTable> operand_resolutions;
    if (method.code.has_value()) {
        auto decoded_method = decode_method(method_id, method);
        if (!decoded_method) {
            return std::unexpected(decoded_method.error());
        }
        decoded = std::make_shared<const DecodedMethod>(
            std::move(*decoded_method));
        operand_resolutions = std::make_shared<OperandResolutionTable>(*decoded);
    }
    ++next_method_id_;
    auto runtime_method = std::make_shared<const RuntimeMethod>(RuntimeMethod {
        .id = method_id,
        .declaring_class = (*runtime_class)->id,
        .owner = owner,
        .method = &method,
        .descriptor = std::move(*descriptor),
        .decoded = std::move(decoded),
        .operand_resolutions = std::move(operand_resolutions),
    });
    methods_.emplace(key, runtime_method);
    methods_by_id_.emplace(runtime_method->id, runtime_method);
    return runtime_method;
}

Result<std::shared_ptr<const CachedMethodDescriptor>>
RuntimeMetadata::method_descriptor(std::string_view descriptor) {
    if (descriptor.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "method descriptor must not be empty");
    }
    {
        std::scoped_lock lock(mutex_);
        if (const auto cached = descriptors_.find(descriptor);
            cached != descriptors_.end()) {
            PerformanceCounters::record_descriptor_cache(true);
            return cached->second;
        }
    }
    PerformanceCounters::record_descriptor_cache(false);
    auto parsed = parse_cached_descriptor(descriptor);
    if (!parsed) return std::unexpected(parsed.error());

    std::scoped_lock lock(mutex_);
    const auto [iterator, inserted] = descriptors_.emplace(
        std::string(descriptor), *parsed);
    (void)inserted;
    return iterator->second;
}

std::shared_ptr<const RuntimeClass> RuntimeMetadata::find_class(
    std::string_view internal_name) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto found = classes_by_name_.find(internal_name);
    return found == classes_by_name_.end() ? nullptr : found->second;
}

std::shared_ptr<const RuntimeClass> RuntimeMetadata::find_class(
    ClassId id) const noexcept {
    if (!id.valid()) return nullptr;
    std::scoped_lock lock(mutex_);
    const auto found = classes_by_id_.find(id);
    return found == classes_by_id_.end() ? nullptr : found->second;
}

std::shared_ptr<const RuntimeMethod> RuntimeMetadata::find_method(
    const classfile::ClassFile* owner,
    const classfile::Method* method) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto found = methods_.find(MethodPointerKey {
        .owner = owner,
        .method = method,
    });
    return found == methods_.end() ? nullptr : found->second;
}

std::shared_ptr<const RuntimeMethod> RuntimeMetadata::find_method(
    MethodId id) const noexcept {
    if (!id.valid()) return nullptr;
    std::scoped_lock lock(mutex_);
    const auto found = methods_by_id_.find(id);
    return found == methods_by_id_.end() ? nullptr : found->second;
}

u64 RuntimeMetadata::generation() const noexcept {
    return generation_.load(std::memory_order_acquire);
}

void RuntimeMetadata::clear() noexcept {
    std::scoped_lock lock(mutex_);
    classes_by_name_.clear();
    classes_by_pointer_.clear();
    classes_by_id_.clear();
    methods_.clear();
    methods_by_id_.clear();
    descriptors_.clear();
    next_class_id_ = 1U;
    next_method_id_ = 1U;
    advance_generation_unlocked();
}

Result<std::shared_ptr<const CachedMethodDescriptor>>
RuntimeMetadata::parse_cached_descriptor(std::string_view descriptor) {
    auto parsed = parse_method_descriptor(descriptor);
    if (!parsed) return std::unexpected(parsed.error());

    const usize slots_without_receiver = parsed->parameter_slots(false);
    const usize slots_with_receiver = parsed->parameter_slots(true);
    const usize argument_values = parsed->parameters.size();
    const JavaTypeKind return_kind = parsed->return_type.kind;
    const bool category_two = parsed->return_type.slot_count() == 2U;
    return std::make_shared<const CachedMethodDescriptor>(
        CachedMethodDescriptor {
            .descriptor = std::move(*parsed),
            .argument_values = argument_values,
            .argument_slots_without_receiver = slots_without_receiver,
            .argument_slots_with_receiver = slots_with_receiver,
            .return_kind = return_kind,
            .returns_category_two = category_two,
        });
}

ClassId RuntimeMetadata::class_id_unlocked(
    std::string_view internal_name) const noexcept {
    if (internal_name.empty()) return {};
    const auto found = classes_by_name_.find(internal_name);
    return found == classes_by_name_.end() ? ClassId {} : found->second->id;
}

void RuntimeMetadata::advance_generation_unlocked() noexcept {
    const u64 current = generation_.load(std::memory_order_relaxed);
    generation_.store(
        current == std::numeric_limits<u64>::max() ? 1U : current + 1U,
        std::memory_order_release);
}

} // namespace phoneme::vm
