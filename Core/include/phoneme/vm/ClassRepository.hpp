#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::vm {

struct ResolvedMethod final {
    std::shared_ptr<const classfile::ClassFile> owner;
    const classfile::Method* method {nullptr};
};

class ClassRepository final {
public:
    [[nodiscard]] Status add_archive(std::string archive_path);
    [[nodiscard]] Result<std::shared_ptr<const classfile::ClassFile>> load(
        std::string_view binary_name);
    [[nodiscard]] Result<ResolvedMethod> resolve_method(
        std::string_view binary_name,
        std::string_view method_name,
        std::string_view descriptor);
    [[nodiscard]] Result<ResolvedMethod> resolve_declared_method(
        std::string_view binary_name,
        std::string_view method_name,
        std::string_view descriptor);
    [[nodiscard]] Result<bool> is_assignable(
        std::string_view source_name,
        std::string_view target_name);
    [[nodiscard]] Result<std::vector<u8>> read_resource(
        std::string_view resource_name) const;
    void clear() noexcept;

private:
    [[nodiscard]] Result<std::shared_ptr<const classfile::ClassFile>> load_uncached(
        std::string_view internal_name) const;
    [[nodiscard]] static std::string normalize_name(std::string_view binary_name);

    mutable std::mutex mutex_;
    std::vector<std::string> archive_paths_;
    std::unordered_map<std::string,
                       std::shared_ptr<const classfile::ClassFile>> cache_;
};

} // namespace phoneme::vm
