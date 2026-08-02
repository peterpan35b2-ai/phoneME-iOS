#include "phoneme/runtime/SuiteStore.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

#include "phoneme/base/Checked.hpp"
#include "phoneme/vm/ModifiedUtf8.hpp"

namespace phoneme::runtime {
namespace {

[[nodiscard]] std::string trim(std::string_view value) {
    usize first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    usize last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

[[nodiscard]] std::string class_entry_name(std::string_view binary_name) {
    std::string entry(binary_name);
    std::replace(entry.begin(), entry.end(), '.', '/');
    entry.append(".class");
    return entry;
}

struct Manifest final {
    std::string display_name;
    std::vector<std::string> midlets;
    std::unordered_map<std::string, std::string> properties;
};

[[nodiscard]] Manifest parse_manifest(std::span<const u8> bytes) {
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(text);
    std::vector<std::string> logical_lines;
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line.front() == ' ' && !logical_lines.empty()) {
            logical_lines.back().append(line.substr(1));
        } else {
            logical_lines.push_back(line);
        }
    }

    Manifest manifest;
    for (const std::string& logical_line : logical_lines) {
        const usize separator = logical_line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view(logical_line).substr(0, separator));
        const std::string value = trim(std::string_view(logical_line).substr(separator + 1));
        if (!key.empty()) {
            manifest.properties.insert_or_assign(key, value);
        }

        if (key == "MIDlet-Name") {
            manifest.display_name = value;
            continue;
        }
        if (!key.starts_with("MIDlet-") || key == "MIDlet-Vendor" ||
            key == "MIDlet-Version" || key == "MIDlet-Name") {
            continue;
        }

        const usize last_comma = value.rfind(',');
        if (last_comma == std::string::npos) {
            continue;
        }
        std::string class_name = trim(std::string_view(value).substr(last_comma + 1));
        if (!class_name.empty()) {
            manifest.midlets.push_back(std::move(class_name));
        }
    }
    return manifest;
}

} // namespace

Result<SuiteId> SuiteStore::install(const std::string& jar_path) {
    auto inspected = inspect_jar(jar_path);
    if (!inspected) {
        return std::unexpected(inspected.error());
    }

    for (const auto& [existing_id, existing] : suites_) {
        if (existing.archive_crc32 == inspected->archive_crc32 &&
            existing.archive_size == inspected->archive_size) {
            return SuiteId {existing_id};
        }
    }

    const SuiteId id = allocate_id(inspected->archive_crc32);
    inspected->id = id;
    suites_.insert_or_assign(id.value, std::move(*inspected));
    return id;
}

const Suite* SuiteStore::find(SuiteId id) const noexcept {
    const auto iterator = suites_.find(id.value);
    return iterator == suites_.end() ? nullptr : &iterator->second;
}

Result<classfile::ClassFile> SuiteStore::load_class(
    SuiteId id,
    std::string_view binary_name) const {
    const Suite* suite = find(id);
    if (suite == nullptr) {
        return fail(ErrorCode::invalid_argument, "suite ID does not exist");
    }

    auto archive = archive::ZipArchive::open(suite->jar_path);
    if (!archive) {
        return std::unexpected(archive.error());
    }

    const std::string entry_name = class_entry_name(binary_name);
    auto class_bytes = archive->read(entry_name);
    if (!class_bytes) {
        return std::unexpected(class_bytes.error());
    }

    auto parsed = classfile::ClassFile::parse(*class_bytes);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    std::string expected_name(binary_name);
    std::replace(expected_name.begin(), expected_name.end(), '.', '/');
    if (parsed->name() != expected_name) {
        return fail(ErrorCode::malformed_class,
                    "class entry name does not match class-file declaration");
    }
    return parsed;
}

bool SuiteStore::contains_main_class(SuiteId id,
                                     std::string_view binary_name) const {
    return load_class(id, binary_name).has_value();
}

void SuiteStore::clear() noexcept { suites_.clear(); }

Result<Suite> SuiteStore::inspect_jar(const std::string& jar_path) {
    auto archive = archive::ZipArchive::open(jar_path);
    if (!archive) {
        return std::unexpected(archive.error());
    }

    const auto raw = archive->raw_bytes();
    Suite suite {
        .id = {},
        .jar_path = jar_path,
        .display_name = {},
        .declared_midlets = {},
        .properties = {},
        .archive_crc32 = archive::crc32(raw),
        .archive_size = static_cast<u64>(raw.size()),
    };

    if (const archive::ZipEntry* manifest_entry =
            archive->find("META-INF/MANIFEST.MF");
        manifest_entry != nullptr) {
        auto bytes = archive->read(*manifest_entry);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        Manifest manifest = parse_manifest(*bytes);
        suite.display_name = std::move(manifest.display_name);
        suite.declared_midlets = std::move(manifest.midlets);
        for (const auto& [key, value] : manifest.properties) {
            auto decoded_key = vm::decode_modified_utf8(key);
            auto decoded_value = vm::decode_modified_utf8(value);
            if (!decoded_key) return std::unexpected(decoded_key.error());
            if (!decoded_value) return std::unexpected(decoded_value.error());
            suite.properties.insert_or_assign(std::move(*decoded_key),
                                              std::move(*decoded_value));
        }
    }

    for (const std::string& main_class : suite.declared_midlets) {
        const std::string entry_name = class_entry_name(main_class);
        const archive::ZipEntry* entry = archive->find(entry_name);
        if (entry == nullptr) {
            return fail(ErrorCode::class_not_found,
                        "manifest references missing MIDlet class: " + main_class);
        }
        auto bytes = archive->read(*entry);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto parsed = classfile::ClassFile::parse(*bytes);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
    }

    return suite;
}

SuiteId SuiteStore::allocate_id(u32 archive_crc32) const noexcept {
    i32 candidate = static_cast<i32>(archive_crc32 & 0x7FFFFFFFU);
    if (candidate == 0) {
        candidate = 1;
    }

    while (suites_.contains(candidate)) {
        if (candidate == std::numeric_limits<i32>::max()) {
            candidate = 1;
        } else {
            ++candidate;
        }
    }
    return SuiteId {candidate};
}

} // namespace phoneme::runtime
