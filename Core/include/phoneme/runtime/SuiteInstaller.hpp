#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/runtime/JadParser.hpp"

namespace phoneme::runtime {

struct SuiteInstallerLimits final {
    archive::ZipLimits archive_limits {};
    AttributeParserLimits jad_limits {};
    AttributeParserLimits manifest_limits {
        .maximum_document_bytes = 256U * 1024U,
        .maximum_physical_line_bytes = 16U * 1024U,
        .maximum_logical_value_bytes = 64U * 1024U,
        .maximum_properties = 2048U,
        .maximum_key_bytes = 256U,
        .duplicate_policy = DuplicatePropertyPolicy::last_wins,
        .stop_at_first_blank_line = true,
    };
};

struct SuiteDescriptor final {
    std::string name;
    std::string vendor;
    std::string version;
    std::string jar_url;
    u64 declared_jar_size {0};
    std::vector<std::string> midlet_classes;
    std::vector<std::string> declared_permissions;
    bool has_permission_declarations {false};
    std::unordered_map<std::string, std::string> properties;
    std::string identity_key;
    std::array<u8, 32> identity_sha256 {};
    std::array<u8, 32> archive_sha256 {};
    u32 archive_crc32 {0};
    u64 archive_size {0};
};

class SuiteInstaller final {
public:
    [[nodiscard]] static Result<SuiteDescriptor> inspect(
        const std::string& jar_path,
        const std::optional<std::string>& jad_path = std::nullopt,
        const SuiteInstallerLimits& limits = {});

    [[nodiscard]] static Result<i32> compare_versions(
        std::string_view left,
        std::string_view right);

    [[nodiscard]] static SuiteId stable_suite_id(
        const std::array<u8, 32>& identity_sha256) noexcept;

    [[nodiscard]] static std::string digest_hex(
        const std::array<u8, 32>& digest);
};

} // namespace phoneme::runtime
