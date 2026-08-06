#include "phoneme/runtime/SuiteInstaller.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::runtime {
namespace {

constexpr std::array<u32, 64> kSha256Constants {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

[[nodiscard]] constexpr u32 rotate_right(u32 value, u32 count) noexcept {
    return (value >> count) | (value << (32U - count));
}

class Sha256 final {
public:
    void update(std::span<const u8> bytes) noexcept {
        for (const u8 byte : bytes) {
            block_[block_size_++] = byte;
            total_bytes_ += 1U;
            if (block_size_ == block_.size()) {
                transform(block_);
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] std::array<u8, 32> finish() noexcept {
        const u64 bit_count = total_bytes_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            while (block_size_ < block_.size()) {
                block_[block_size_++] = 0U;
            }
            transform(block_);
            block_size_ = 0;
        }
        while (block_size_ < 56U) {
            block_[block_size_++] = 0U;
        }
        for (usize index = 0; index < 8U; ++index) {
            block_[63U - index] = static_cast<u8>(bit_count >> (index * 8U));
        }
        transform(block_);
        block_size_ = 0;

        std::array<u8, 32> digest {};
        for (usize index = 0; index < state_.size(); ++index) {
            digest[index * 4U] = static_cast<u8>(state_[index] >> 24U);
            digest[index * 4U + 1U] = static_cast<u8>(state_[index] >> 16U);
            digest[index * 4U + 2U] = static_cast<u8>(state_[index] >> 8U);
            digest[index * 4U + 3U] = static_cast<u8>(state_[index]);
        }
        return digest;
    }

private:
    void transform(const std::array<u8, 64>& block) noexcept {
        std::array<u32, 64> schedule {};
        for (usize index = 0; index < 16U; ++index) {
            const usize offset = index * 4U;
            schedule[index] = (static_cast<u32>(block[offset]) << 24U) |
                              (static_cast<u32>(block[offset + 1U]) << 16U) |
                              (static_cast<u32>(block[offset + 2U]) << 8U) |
                              static_cast<u32>(block[offset + 3U]);
        }
        for (usize index = 16U; index < schedule.size(); ++index) {
            const u32 s0 = rotate_right(schedule[index - 15U], 7U) ^
                           rotate_right(schedule[index - 15U], 18U) ^
                           (schedule[index - 15U] >> 3U);
            const u32 s1 = rotate_right(schedule[index - 2U], 17U) ^
                           rotate_right(schedule[index - 2U], 19U) ^
                           (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 +
                              schedule[index - 7U] + s1;
        }

        u32 a = state_[0];
        u32 b = state_[1];
        u32 c = state_[2];
        u32 d = state_[3];
        u32 e = state_[4];
        u32 f = state_[5];
        u32 g = state_[6];
        u32 h = state_[7];

        for (usize index = 0; index < schedule.size(); ++index) {
            const u32 sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                             rotate_right(e, 25U);
            const u32 choose = (e & f) ^ ((~e) & g);
            const u32 temporary1 = h + sum1 + choose +
                                   kSha256Constants[index] + schedule[index];
            const u32 sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                             rotate_right(a, 22U);
            const u32 majority = (a & b) ^ (a & c) ^ (b & c);
            const u32 temporary2 = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<u32, 8> state_ {
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
    };
    std::array<u8, 64> block_ {};
    usize block_size_ {0};
    u64 total_bytes_ {0};
};

[[nodiscard]] std::array<u8, 32> sha256(std::span<const u8> bytes) noexcept {
    Sha256 digest;
    digest.update(bytes);
    return digest.finish();
}

[[nodiscard]] std::array<u8, 32> sha256(std::string_view text) noexcept {
    return sha256(std::span<const u8>(
        reinterpret_cast<const u8*>(text.data()), text.size()));
}

[[nodiscard]] std::string trim_ascii(std::string_view value) {
    usize first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    usize last = value.size();
    while (last > first &&
           (value[last - 1U] == ' ' || value[last - 1U] == '\t')) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

[[nodiscard]] const std::string* find_property(
    const std::unordered_map<std::string, std::string>& properties,
    std::string_view key) noexcept {
    const auto iterator = properties.find(std::string(key));
    return iterator == properties.end() ? nullptr : &iterator->second;
}

[[nodiscard]] Result<std::string> require_property(
    const std::unordered_map<std::string, std::string>& properties,
    std::string_view key) {
    const std::string* value = find_property(properties, key);
    if (value == nullptr || trim_ascii(*value).empty()) {
        return fail(ErrorCode::invalid_argument,
                    "suite is missing required property: " + std::string(key));
    }
    return trim_ascii(*value);
}

[[nodiscard]] bool is_midlet_key(std::string_view key, u32& index) noexcept {
    constexpr std::string_view prefix = "MIDlet-";
    if (!key.starts_with(prefix) || key.size() <= prefix.size()) {
        return false;
    }
    const std::string_view suffix = key.substr(prefix.size());
    if (!std::all_of(suffix.begin(), suffix.end(), [](char value) {
            return std::isdigit(static_cast<unsigned char>(value)) != 0;
        })) {
        return false;
    }
    u32 parsed = 0;
    const auto [end, error] = std::from_chars(
        suffix.data(), suffix.data() + suffix.size(), parsed);
    if (error != std::errc {} || end != suffix.data() + suffix.size() || parsed == 0U) {
        return false;
    }
    index = parsed;
    return true;
}

[[nodiscard]] bool valid_binary_class_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '.' || name.back() == '.') {
        return false;
    }
    bool component_start = true;
    for (const char value : name) {
        if (value == '.') {
            if (component_start) {
                return false;
            }
            component_start = true;
            continue;
        }
        const auto byte = static_cast<unsigned char>(value);
        if (component_start) {
            if (std::isalpha(byte) == 0 && value != '_' && value != '$') {
                return false;
            }
            component_start = false;
        } else if (std::isalnum(byte) == 0 && value != '_' && value != '$') {
            return false;
        }
    }
    return !component_start;
}

[[nodiscard]] std::string class_entry_name(std::string_view binary_name) {
    std::string entry(binary_name);
    std::replace(entry.begin(), entry.end(), '.', '/');
    entry.append(".class");
    return entry;
}

void append_permissions(std::vector<std::string>& output,
                        std::string_view value) {
    usize offset = 0;
    while (offset <= value.size()) {
        const usize comma = value.find(',', offset);
        const usize end = comma == std::string_view::npos ? value.size() : comma;
        std::string permission = trim_ascii(value.substr(offset, end - offset));
        if (!permission.empty() &&
            std::find(output.begin(), output.end(), permission) == output.end()) {
            output.push_back(std::move(permission));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        offset = comma + 1U;
    }
}

[[nodiscard]] std::string ascii_upper(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](char byte) {
        return static_cast<char>(
            std::toupper(static_cast<unsigned char>(byte)));
    });
    return result;
}

[[nodiscard]] bool is_signature_metadata_entry(
    std::string_view entry_name) {
    const std::string uppercase = ascii_upper(entry_name);
    constexpr std::string_view prefix = "META-INF/";
    if (!uppercase.starts_with(prefix)) {
        return false;
    }
    const std::string_view leaf =
        std::string_view(uppercase).substr(prefix.size());
    if (leaf.empty() || leaf.find('/') != std::string_view::npos) {
        return false;
    }
    return leaf.ends_with(".SF") || leaf.ends_with(".RSA") ||
           leaf.ends_with(".DSA") || leaf.ends_with(".EC");
}

[[nodiscard]] Result<ArchiveTrustEvidence> collect_trust_evidence(
    const archive::ZipArchive& archive) {
    ArchiveTrustEvidence evidence;
    for (const archive::ZipEntry& entry : archive.entries()) {
        if (!is_signature_metadata_entry(entry.name)) {
            continue;
        }
        auto bytes = archive.read(entry);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        evidence.signature_files.push_back(ArchiveSignatureFile {
            .entry_name = entry.name,
            .sha256 = sha256(*bytes),
            .size = static_cast<u64>(bytes->size()),
        });
    }
    std::sort(evidence.signature_files.begin(),
              evidence.signature_files.end(),
              [](const ArchiveSignatureFile& left,
                 const ArchiveSignatureFile& right) {
                  return left.entry_name < right.entry_name;
              });
    if (!evidence.signature_files.empty()) {
        evidence.signature_state =
            ArchiveSignatureState::metadata_present_unverified;
    }
    return evidence;
}

[[nodiscard]] Status require_supported_capability(
    std::string_view value,
    std::span<const std::string> supported,
    std::string_view family_prefix,
    std::string_view property_name) {
    bool declares_family = false;
    usize offset = 0;
    while (offset < value.size()) {
        while (offset < value.size() &&
               std::isspace(static_cast<unsigned char>(value[offset])) != 0) {
            ++offset;
        }
        if (offset >= value.size()) break;
        const usize end = value.find_first_of(" \t", offset);
        const std::string_view token = value.substr(
            offset,
            end == std::string_view::npos ? value.size() - offset : end - offset);
        declares_family = declares_family || token.starts_with(family_prefix);
        if (std::find(supported.begin(), supported.end(), token) !=
            supported.end()) {
            return {};
        }
        if (end == std::string_view::npos) break;
        offset = end + 1U;
    }

    if (!declares_family) {
        return fail(ErrorCode::invalid_argument,
                    std::string(property_name) + " does not declare " +
                        std::string(family_prefix));
    }

    std::string message(property_name);
    message.append(" is not supported by this Core: ");
    message.append(value);
    message.append("; supported values: ");
    for (usize index = 0; index < supported.size(); ++index) {
        if (index != 0U) message.append(", ");
        message.append(supported[index]);
    }
    return fail(ErrorCode::unsupported_feature, std::move(message));
}

[[nodiscard]] Result<u64> parse_decimal_u64(std::string_view text,
                                            std::string_view property_name) {
    const std::string trimmed = trim_ascii(text);
    if (trimmed.empty()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(property_name) + " must be a decimal integer");
    }
    u64 value = 0;
    const auto [end, error] = std::from_chars(
        trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (error != std::errc {} || end != trimmed.data() + trimmed.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(property_name) + " must be a decimal integer");
    }
    return value;
}

constexpr usize kMaximumVersionComponents = 64U;

[[nodiscard]] Result<std::vector<u32>> parse_version(
    std::string_view version) {
    std::vector<u32> parts;
    parts.reserve(std::min<usize>(
        static_cast<usize>(std::count(version.begin(), version.end(), '.')) + 1U,
        kMaximumVersionComponents));

    usize offset = 0U;
    while (offset <= version.size()) {
        if (parts.size() >= kMaximumVersionComponents) {
            return fail(ErrorCode::invalid_argument,
                        "MIDlet-Version contains too many numeric components");
        }
        const usize dot = version.find('.', offset);
        const usize end = dot == std::string_view::npos ? version.size() : dot;
        const std::string_view part = version.substr(offset, end - offset);
        if (part.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "MIDlet-Version contains an empty component");
        }

        u32 value = 0U;
        const auto [parsed_end, error] = std::from_chars(
            part.data(), part.data() + part.size(), value);
        if (error != std::errc {} || parsed_end != part.data() + part.size()) {
            return fail(ErrorCode::invalid_argument,
                        "MIDlet-Version contains an invalid numeric component");
        }
        parts.push_back(value);

        if (dot == std::string_view::npos) {
            break;
        }
        offset = dot + 1U;
    }

    if (parts.empty()) {
        return fail(ErrorCode::invalid_argument, "MIDlet-Version is empty");
    }
    while (parts.size() > 1U && parts.back() == 0U) {
        parts.pop_back();
    }
    return parts;
}

[[nodiscard]] Status require_matching_identity_property(
    const AttributeDocument& manifest,
    const AttributeDocument& jad,
    std::string_view key) {
    const std::string* manifest_value = manifest.find(key);
    const std::string* jad_value = jad.find(key);
    if (manifest_value != nullptr && jad_value != nullptr &&
        trim_ascii(*manifest_value) != trim_ascii(*jad_value)) {
        return fail(ErrorCode::invalid_argument,
                    "JAD and manifest disagree on suite identity property: " +
                        std::string(key));
    }
    return {};
}

} // namespace

Result<SuiteDescriptor> SuiteInstaller::inspect(
    const std::string& jar_path,
    const std::optional<std::string>& jad_path,
    const SuiteInstallerLimits& limits) {
    auto archive = archive::ZipArchive::open(jar_path, limits.archive_limits);
    if (!archive) {
        return std::unexpected(archive.error());
    }

    const archive::ZipEntry* manifest_entry =
        archive->find("META-INF/MANIFEST.MF");
    if (manifest_entry == nullptr) {
        return fail(ErrorCode::malformed_archive,
                    "JAR does not contain META-INF/MANIFEST.MF");
    }
    auto manifest_bytes = archive->read(*manifest_entry);
    if (!manifest_bytes) {
        return std::unexpected(manifest_bytes.error());
    }
    auto manifest = JadParser::parse(*manifest_bytes, limits.manifest_limits);
    if (!manifest) {
        return std::unexpected(manifest.error());
    }

    std::optional<AttributeDocument> jad;
    if (jad_path.has_value()) {
        auto parsed_jad = JadParser::parse_file(*jad_path, limits.jad_limits);
        if (!parsed_jad) {
            return std::unexpected(parsed_jad.error());
        }
        jad = std::move(*parsed_jad);
        for (const std::string_view identity_key : {
                 std::string_view("MIDlet-Name"),
                 std::string_view("MIDlet-Vendor"),
                 std::string_view("MIDlet-Version")}) {
            auto matching = require_matching_identity_property(
                *manifest, *jad, identity_key);
            if (!matching) {
                return std::unexpected(matching.error());
            }
        }
    }

    std::unordered_map<std::string, std::string> merged = manifest->properties;
    if (jad.has_value()) {
        for (const auto& [key, value] : jad->properties) {
            merged.insert_or_assign(key, value);
        }
    }

    auto name = require_property(merged, "MIDlet-Name");
    auto vendor = require_property(merged, "MIDlet-Vendor");
    auto version = require_property(merged, "MIDlet-Version");
    auto profile = require_property(merged, "MicroEdition-Profile");
    auto configuration = require_property(merged, "MicroEdition-Configuration");
    if (!name || !vendor || !version || !profile || !configuration) {
        if (!name) return std::unexpected(name.error());
        if (!vendor) return std::unexpected(vendor.error());
        if (!version) return std::unexpected(version.error());
        if (!profile) return std::unexpected(profile.error());
        return std::unexpected(configuration.error());
    }
    auto parsed_version = parse_version(*version);
    if (!parsed_version) {
        return std::unexpected(parsed_version.error());
    }
    auto supported_profile = require_supported_capability(
        *profile,
        limits.capabilities.profiles,
        "MIDP-",
        "MicroEdition-Profile");
    if (!supported_profile) {
        return std::unexpected(supported_profile.error());
    }
    auto supported_configuration = require_supported_capability(
        *configuration,
        limits.capabilities.configurations,
        "CLDC-",
        "MicroEdition-Configuration");
    if (!supported_configuration) {
        return std::unexpected(supported_configuration.error());
    }

    const auto raw = archive->raw_bytes();
    const u64 archive_size = static_cast<u64>(raw.size());
    std::string jar_url;
    u64 declared_size = archive_size;
    if (jad.has_value()) {
        auto required_url = require_property(merged, "MIDlet-Jar-URL");
        auto required_size = require_property(merged, "MIDlet-Jar-Size");
        if (!required_url) return std::unexpected(required_url.error());
        if (!required_size) return std::unexpected(required_size.error());
        auto parsed_size = parse_decimal_u64(*required_size, "MIDlet-Jar-Size");
        if (!parsed_size) return std::unexpected(parsed_size.error());
        if (*parsed_size != archive_size) {
            return fail(ErrorCode::checksum_mismatch,
                        "MIDlet-Jar-Size does not match the selected JAR");
        }
        jar_url = std::move(*required_url);
        declared_size = *parsed_size;
    } else {
        std::error_code error;
        jar_url = std::filesystem::path(jar_path).filename().string();
        if (error) {
            jar_url = jar_path;
        }
    }

    std::vector<std::pair<u32, std::string>> indexed_midlets;
    for (const auto& [key, value] : merged) {
        u32 index = 0;
        if (!is_midlet_key(key, index)) {
            continue;
        }
        const usize comma = value.rfind(',');
        if (comma == std::string::npos) {
            return fail(ErrorCode::invalid_argument,
                        "MIDlet declaration is missing its class name: " + key);
        }
        std::string class_name = trim_ascii(std::string_view(value).substr(comma + 1U));
        if (!valid_binary_class_name(class_name)) {
            return fail(ErrorCode::invalid_argument,
                        "MIDlet declaration contains an invalid class name: " + key);
        }
        indexed_midlets.emplace_back(index, std::move(class_name));
    }
    if (indexed_midlets.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "suite does not declare any MIDlet-n entry");
    }
    std::sort(indexed_midlets.begin(), indexed_midlets.end(),
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });
    for (usize index = 0; index < indexed_midlets.size(); ++index) {
        if (indexed_midlets[index].first != static_cast<u32>(index + 1U)) {
            return fail(ErrorCode::invalid_argument,
                        "MIDlet-n declarations must be contiguous starting at MIDlet-1");
        }
    }

    std::vector<std::string> midlet_classes;
    midlet_classes.reserve(indexed_midlets.size());
    for (auto& [index, class_name] : indexed_midlets) {
        static_cast<void>(index);
        const std::string entry_name = class_entry_name(class_name);
        const archive::ZipEntry* class_entry = archive->find(entry_name);
        if (class_entry == nullptr) {
            return fail(ErrorCode::class_not_found,
                        "suite references a missing MIDlet class: " + class_name);
        }
        auto class_bytes = archive->read(*class_entry);
        if (!class_bytes) {
            return std::unexpected(class_bytes.error());
        }
        auto parsed_class = classfile::ClassFile::parse(*class_bytes);
        if (!parsed_class) {
            return std::unexpected(parsed_class.error());
        }
        std::string expected_name = class_name;
        std::replace(expected_name.begin(), expected_name.end(), '.', '/');
        if (parsed_class->name() != expected_name) {
            return fail(ErrorCode::malformed_class,
                        "MIDlet class entry name does not match class declaration");
        }
        midlet_classes.push_back(std::move(class_name));
    }

    std::vector<std::string> required_permissions;
    std::vector<std::string> optional_permissions;
    if (const std::string* value =
            find_property(merged, "MIDlet-Permissions");
        value != nullptr) {
        append_permissions(required_permissions, *value);
    }
    if (const std::string* value =
            find_property(merged, "MIDlet-Permissions-Opt");
        value != nullptr) {
        append_permissions(optional_permissions, *value);
    }
    std::erase_if(optional_permissions,
                  [&required_permissions](const std::string& permission) {
                      return std::find(required_permissions.begin(),
                                       required_permissions.end(),
                                       permission) !=
                          required_permissions.end();
                  });
    std::vector<std::string> permissions = required_permissions;
    for (const std::string& permission : optional_permissions) {
        if (std::find(permissions.begin(), permissions.end(), permission) ==
            permissions.end()) {
            permissions.push_back(permission);
        }
    }
    const bool has_permission_declarations =
        find_property(merged, "MIDlet-Permissions") != nullptr ||
        find_property(merged, "MIDlet-Permissions-Opt") != nullptr;

    auto trust_evidence = collect_trust_evidence(*archive);
    if (!trust_evidence) {
        return std::unexpected(trust_evidence.error());
    }

    for (const auto& [key, value] : merged) {
        auto decoded_key = JadParser::decode_utf8(key);
        auto decoded_value = JadParser::decode_utf8(value);
        if (!decoded_key) return std::unexpected(decoded_key.error());
        if (!decoded_value) return std::unexpected(decoded_value.error());
    }

    std::string identity_key;
    identity_key.reserve(vendor->size() + name->size() + 1U);
    identity_key.append(*vendor);
    identity_key.push_back('\x1F');
    identity_key.append(*name);

    return SuiteDescriptor {
        .name = std::move(*name),
        .vendor = std::move(*vendor),
        .version = std::move(*version),
        .jar_url = std::move(jar_url),
        .declared_jar_size = declared_size,
        .midlet_classes = std::move(midlet_classes),
        .declared_required_permissions = std::move(required_permissions),
        .declared_optional_permissions = std::move(optional_permissions),
        .declared_permissions = std::move(permissions),
        .has_permission_declarations = has_permission_declarations,
        .trust_evidence = std::move(*trust_evidence),
        .properties = std::move(merged),
        .identity_key = identity_key,
        .identity_sha256 = sha256(identity_key),
        .archive_sha256 = sha256(raw),
        .archive_crc32 = archive::crc32(raw),
        .archive_size = archive_size,
    };
}

Status SuiteInstaller::scope_identity(
    SuiteDescriptor& descriptor,
    std::string_view identity_scope) {
    constexpr usize kMaximumIdentityScopeBytes = 256U;
    constexpr char kIdentityScopeSeparator = '\x1E';

    if (identity_scope.empty()) return {};
    if (identity_scope.size() > kMaximumIdentityScopeBytes ||
        identity_scope.find('\0') != std::string_view::npos ||
        identity_scope.find(kIdentityScopeSeparator) != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "suite identity scope is invalid");
    }

    descriptor.identity_key.push_back(kIdentityScopeSeparator);
    descriptor.identity_key.append(identity_scope);
    descriptor.identity_sha256 = sha256(descriptor.identity_key);
    return {};
}

Result<i32> SuiteInstaller::compare_versions(std::string_view left,
                                             std::string_view right) {
    auto left_parts = parse_version(left);
    auto right_parts = parse_version(right);
    if (!left_parts) return std::unexpected(left_parts.error());
    if (!right_parts) return std::unexpected(right_parts.error());

    const usize component_count =
        std::max(left_parts->size(), right_parts->size());
    for (usize index = 0U; index < component_count; ++index) {
        const u32 left_value =
            index < left_parts->size() ? (*left_parts)[index] : 0U;
        const u32 right_value =
            index < right_parts->size() ? (*right_parts)[index] : 0U;
        if (left_value < right_value) return -1;
        if (left_value > right_value) return 1;
    }
    return 0;
}

SuiteId SuiteInstaller::stable_suite_id(
    const std::array<u8, 32>& identity_sha256) noexcept {
    u32 value = (static_cast<u32>(identity_sha256[0]) << 24U) |
                (static_cast<u32>(identity_sha256[1]) << 16U) |
                (static_cast<u32>(identity_sha256[2]) << 8U) |
                static_cast<u32>(identity_sha256[3]);
    value &= 0x7FFFFFFFU;
    if (value == 0U) {
        value = 1U;
    }
    return SuiteId {static_cast<i32>(value)};
}

std::string SuiteInstaller::digest_hex(const std::array<u8, 32>& digest) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string output;
    output.resize(digest.size() * 2U);
    for (usize index = 0; index < digest.size(); ++index) {
        output[index * 2U] = alphabet[digest[index] >> 4U];
        output[index * 2U + 1U] = alphabet[digest[index] & 0x0FU];
    }
    return output;
}

} // namespace phoneme::runtime
