#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/Heap.hpp"

namespace phoneme::vm {

struct FieldLocation final {
    std::string declaring_class;
    std::string name;
    std::string descriptor;
    usize index {0};
    bool is_static {false};
    std::optional<u16> constant_value_index;
};

struct ClassLayout final {
    std::string class_name;
    std::string super_name;
    usize instance_field_slots {0};
    std::unordered_map<std::string, usize> instance_fields;
    std::vector<Value> instance_defaults;
};

class ClassStateRegistry final {
public:
    explicit ClassStateRegistry(ClassRepository& classes) noexcept
        : classes_(classes) {}

    [[nodiscard]] Result<std::shared_ptr<const ClassLayout>> layout(
        std::string_view class_name);
    [[nodiscard]] Result<FieldLocation> resolve_field(
        std::string_view owner,
        std::string_view name,
        std::string_view descriptor,
        bool require_static);
    [[nodiscard]] Result<ObjectRef> allocate_instance(
        Heap& heap,
        std::string_view class_name);
    [[nodiscard]] Result<Value> static_field(const FieldLocation& field);
    [[nodiscard]] Status set_static_field(const FieldLocation& field, Value value);
    void append_reference_roots(std::vector<ObjectRef>& roots) const;
    void clear() noexcept;

private:
    [[nodiscard]] Result<std::shared_ptr<const ClassLayout>> build_layout(
        std::string class_name);
    [[nodiscard]] static Result<Value> default_value(
        std::string_view descriptor);
    [[nodiscard]] static std::string field_key(
        std::string_view declaring_class,
        std::string_view name,
        std::string_view descriptor);

    ClassRepository& classes_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<const ClassLayout>> layouts_;
    std::unordered_map<std::string, Value> static_fields_;
};

} // namespace phoneme::vm
