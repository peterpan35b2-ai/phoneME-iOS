#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::runtime {

struct SuiteDatabaseRecord final {
    SuiteId id;
    std::string identity_key;
    std::array<u8, 32> identity_sha256 {};
    std::array<u8, 32> archive_sha256 {};
    std::string name;
    std::string vendor;
    std::string version;
    std::string jar_relative_path;
    std::string jad_relative_path;
    std::vector<std::string> midlet_classes;
    std::vector<std::string> declared_permissions;
    bool has_permission_declarations {false};
    std::unordered_map<std::string, std::string> properties;
    u32 archive_crc32 {0};
    u64 archive_size {0};
    u64 declared_jar_size {0};
};

struct SuiteDatabaseSnapshot final {
    u64 generation {0};
    std::vector<SuiteDatabaseRecord> records;
    bool recovered_from_backup {false};
};

class SuiteDatabase final {
public:
    SuiteDatabase() = default;
    explicit SuiteDatabase(std::string path) : path_(std::move(path)) {}

    void set_path(std::string path) { path_ = std::move(path); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    [[nodiscard]] Result<SuiteDatabaseSnapshot> load(
        bool recover_from_backup = true) const;
    [[nodiscard]] Status commit(const SuiteDatabaseSnapshot& snapshot) const;

private:
    [[nodiscard]] Result<SuiteDatabaseSnapshot> load_path(
        const std::string& path) const;

    std::string path_;
};

} // namespace phoneme::runtime
