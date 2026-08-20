#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/RuntimeMetadata.hpp"

namespace phoneme::vm {

struct ResolvedMethod final {
    std::shared_ptr<const classfile::ClassFile> owner;
    const classfile::Method* method {nullptr};
    std::shared_ptr<const RuntimeMethod> runtime;
};

class ClassRepository final {
public:
    [[nodiscard]] Status add_archive(std::string archive_path);
    [[nodiscard]] Status add_archive(
        std::string archive_path,
        const std::array<u8, 32>& archive_sha256,
        u32 verification_cache_version,
        const std::unordered_map<std::string, u64>& verified_classes);
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
    [[nodiscard]] RuntimeMetadata& metadata() noexcept { return metadata_; }
    [[nodiscard]] const RuntimeMetadata& metadata() const noexcept {
        return metadata_;
    }
    [[nodiscard]] std::unordered_map<std::string, u64> verified_classes(
        std::string_view archive_path) const;
    [[nodiscard]] static constexpr u32 verification_cache_version() noexcept {
        return 1U;
    }
    void clear() noexcept;

private:
    struct ClasspathArchive final {
        std::string path;
        archive::ZipArchive archive;
        std::array<u8, 32> archive_sha256 {};
        u32 verification_cache_version {0};
        std::unordered_map<std::string, u64> verified_classes;
        bool verification_cache_enabled {false};
    };

    [[nodiscard]] Status add_archive_impl(
        std::string archive_path,
        const std::array<u8, 32>* archive_sha256,
        u32 verification_cache_version,
        const std::unordered_map<std::string, u64>* verified_classes);
    [[nodiscard]] Result<std::shared_ptr<const classfile::ClassFile>> load_uncached(
        std::string_view internal_name);
    [[nodiscard]] static std::string normalize_name(std::string_view binary_name);

    mutable std::mutex mutex_;
    std::vector<ClasspathArchive> archives_;
    std::unordered_map<std::string,
                       std::shared_ptr<const classfile::ClassFile>> cache_;
    std::unordered_map<std::string, ResolvedMethod> method_cache_;
    std::unordered_map<std::string, ResolvedMethod> declared_method_cache_;
    std::unordered_map<std::string, bool> assignability_cache_;
    RuntimeMetadata metadata_;
};

} // namespace phoneme::vm
