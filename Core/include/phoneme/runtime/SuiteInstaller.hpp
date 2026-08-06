#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/runtime/JadParser.hpp"

namespace phoneme::runtime {

struct SuiteInstallerCapabilities final {
    std::vector<std::string> profiles {
        "MIDP-1.0", "MIDP-2.0", "MIDP-2.1"};
    std::vector<std::string> configurations {
        "CLDC-1.0", "CLDC-1.1", "CLDC-1.1.1"};
};

struct SuiteInstallerLimits final {
    archive::ZipLimits archive_limits {};
    SuiteInstallerCapabilities capabilities {};
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

enum class ArchiveSignatureState : u8 {
    unsigned_archive,
    metadata_present_unverified,
};

struct ArchiveSignatureFile final {
    std::string entry_name;
    std::array<u8, 32> sha256 {};
    u64 size {0};
};

struct ArchiveTrustEvidence final {
    ArchiveSignatureState signature_state {
        ArchiveSignatureState::unsigned_archive};
    std::vector<ArchiveSignatureFile> signature_files;

    [[nodiscard]] bool has_signature_metadata() const noexcept {
        return signature_state ==
            ArchiveSignatureState::metadata_present_unverified;
    }

    // Item 05 only records deterministic evidence. Trust must remain false
    // until item 06 verifies the certificate chain and signed manifest.
    [[nodiscard]] constexpr bool cryptographically_verified() const noexcept {
        return false;
    }
};

struct SuiteDescriptor final {
    std::string name;
    std::string vendor;
    std::string version;
    std::string jar_url;
    u64 declared_jar_size {0};
    std::vector<std::string> midlet_classes;
    std::vector<std::string> declared_required_permissions;
    std::vector<std::string> declared_optional_permissions;
    std::vector<std::string> declared_permissions;
    bool has_permission_declarations {false};
    ArchiveTrustEvidence trust_evidence;
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

    // Adds a host-owned namespace to the MIDP vendor/name identity without
    // changing any manifest property visible to the application. This lets a
    // multi-application host keep distinct imports of the same suite metadata.
    [[nodiscard]] static Status scope_identity(
        SuiteDescriptor& descriptor,
        std::string_view identity_scope);

    [[nodiscard]] static SuiteId stable_suite_id(
        const std::array<u8, 32>& identity_sha256) noexcept;

    [[nodiscard]] static std::string digest_hex(
        const std::array<u8, 32>& digest);
};

} // namespace phoneme::runtime
