#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/DecodedMethod.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/MetadataId.hpp"

namespace phoneme::vm {

struct CachedMethodDescriptor final {
    MethodDescriptor descriptor;
    usize argument_values {0};
    usize argument_slots_without_receiver {0};
    usize argument_slots_with_receiver {0};
    JavaTypeKind return_kind {JavaTypeKind::void_type};
    bool returns_category_two {false};

    [[nodiscard]] usize argument_slots(bool has_receiver) const noexcept {
        return has_receiver ? argument_slots_with_receiver
                            : argument_slots_without_receiver;
    }
};

struct RuntimeClass final {
    ClassId id;
    std::shared_ptr<const classfile::ClassFile> class_file;
    std::string super_name;
    ClassId super_id;
    std::vector<std::string> interface_names;
    std::vector<ClassId> interface_ids;
};

struct RuntimeMethod final {
    MethodId id;
    ClassId declaring_class;
    std::shared_ptr<const classfile::ClassFile> owner;
    const classfile::Method* method {nullptr};
    std::shared_ptr<const CachedMethodDescriptor> descriptor;
    std::shared_ptr<const DecodedMethod> decoded;
};

class RuntimeMetadata final {
public:
    RuntimeMetadata() = default;

    RuntimeMetadata(const RuntimeMetadata&) = delete;
    RuntimeMetadata& operator=(const RuntimeMetadata&) = delete;

    [[nodiscard]] Result<std::shared_ptr<const RuntimeClass>> publish_class(
        std::shared_ptr<const classfile::ClassFile> class_file);
    [[nodiscard]] Result<std::shared_ptr<const RuntimeMethod>> publish_method(
        const std::shared_ptr<const classfile::ClassFile>& owner,
        const classfile::Method& method);
    [[nodiscard]] Result<std::shared_ptr<const CachedMethodDescriptor>>
    method_descriptor(std::string_view descriptor);

    [[nodiscard]] std::shared_ptr<const RuntimeClass> find_class(
        std::string_view internal_name) const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeClass> find_class(
        ClassId id) const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeMethod> find_method(
        const classfile::ClassFile* owner,
        const classfile::Method* method) const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeMethod> find_method(
        MethodId id) const noexcept;

    [[nodiscard]] u64 generation() const noexcept;
    void clear() noexcept;

private:
    struct TransparentStringHash final {
        using is_transparent = void;

        [[nodiscard]] usize operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
        [[nodiscard]] usize operator()(const std::string& value) const noexcept {
            return (*this)(std::string_view(value));
        }
    };

    struct TransparentStringEqual final {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view left,
                                      std::string_view right) const noexcept {
            return left == right;
        }
    };

    struct MethodPointerKey final {
        const classfile::ClassFile* owner {nullptr};
        const classfile::Method* method {nullptr};

        friend bool operator==(MethodPointerKey,
                               MethodPointerKey) noexcept = default;
    };

    struct MethodPointerKeyHash final {
        [[nodiscard]] usize operator()(MethodPointerKey key) const noexcept;
    };

    [[nodiscard]] static Result<std::shared_ptr<const CachedMethodDescriptor>>
    parse_cached_descriptor(std::string_view descriptor);
    [[nodiscard]] ClassId class_id_unlocked(
        std::string_view internal_name) const noexcept;
    void advance_generation_unlocked() noexcept;

    mutable std::mutex mutex_;
    u64 generation_ {1};
    u32 next_class_id_ {1};
    u32 next_method_id_ {1};
    std::unordered_map<std::string,
                       std::shared_ptr<const RuntimeClass>,
                       TransparentStringHash,
                       TransparentStringEqual> classes_by_name_;
    std::unordered_map<const classfile::ClassFile*,
                       std::shared_ptr<const RuntimeClass>> classes_by_pointer_;
    std::unordered_map<ClassId,
                       std::shared_ptr<const RuntimeClass>,
                       MetadataIdHash<ClassId>> classes_by_id_;
    std::unordered_map<MethodPointerKey,
                       std::shared_ptr<const RuntimeMethod>,
                       MethodPointerKeyHash> methods_;
    std::unordered_map<MethodId,
                       std::shared_ptr<const RuntimeMethod>,
                       MetadataIdHash<MethodId>> methods_by_id_;
    std::unordered_map<std::string,
                       std::shared_ptr<const CachedMethodDescriptor>,
                       TransparentStringHash,
                       TransparentStringEqual> descriptors_;
};

} // namespace phoneme::vm
