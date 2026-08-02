#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::runtime {

struct Suite final {
    SuiteId id;
    std::string jar_path;
    std::string display_name;
    std::vector<std::string> declared_midlets;
    std::unordered_map<std::u16string, std::u16string> properties;
    u32 archive_crc32 {0};
    u64 archive_size {0};
};

class SuiteStore final {
public:
    [[nodiscard]] Result<SuiteId> install(const std::string& jar_path);
    [[nodiscard]] const Suite* find(SuiteId id) const noexcept;
    [[nodiscard]] Result<classfile::ClassFile> load_class(
        SuiteId id,
        std::string_view binary_name) const;
    [[nodiscard]] bool contains_main_class(SuiteId id,
                                           std::string_view binary_name) const;
    void clear() noexcept;

private:
    [[nodiscard]] static Result<Suite> inspect_jar(const std::string& jar_path);
    [[nodiscard]] SuiteId allocate_id(u32 archive_crc32) const noexcept;

    std::unordered_map<i32, Suite> suites_;
};

} // namespace phoneme::runtime
