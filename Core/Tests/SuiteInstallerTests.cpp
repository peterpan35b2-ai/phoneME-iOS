#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phoneme/runtime/JadParser.hpp"
#include "phoneme/runtime/SuiteInstaller.hpp"
#include "phoneme/runtime/SuiteStore.hpp"

namespace {

using phoneme::ErrorCode;
using phoneme::Result;
using phoneme::SuiteId;
using phoneme::u8;
using phoneme::u64;
using phoneme::runtime::AttributeParserLimits;
using phoneme::runtime::DuplicatePropertyPolicy;
using phoneme::runtime::JadParser;
using phoneme::runtime::SuiteInstaller;
using phoneme::runtime::SuiteStore;
using phoneme::runtime::SuiteStoreConfig;
using phoneme::runtime::SuiteUninstallPolicy;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename T>
void check_error(const Result<T>& result,
                 ErrorCode expected,
                 std::string_view message) {
    check(!result.has_value(), message);
    if (!result.has_value()) {
        check(result.error().code == expected,
              std::string(message) + " (unexpected error code)");
    }
}

bool write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(stream);
}

bool copy_fixture_file(const std::filesystem::path& source,
                       const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::copy_file(
        source, destination,
        std::filesystem::copy_options::overwrite_existing,
        error);
    return !error;
}

u64 fixture_file_size(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    check(!error, "read fixture JAR size");
    return error ? 0U : static_cast<u64>(size);
}

std::string make_jad(const std::filesystem::path& jar,
                     std::string_view version,
                     u64 declared_size,
                     bool continued = false) {
    std::string result;
    result.append("MIDlet-Name: Installer Test\n");
    result.append("MIDlet-Vendor: phoneME\n");
    result.append("MIDlet-Version: ");
    result.append(version);
    result.push_back('\n');
    result.append("MIDlet-Jar-URL: ");
    result.append(jar.filename().string());
    result.push_back('\n');
    result.append("MIDlet-Jar-Size: ");
    result.append(std::to_string(declared_size));
    result.push_back('\n');
    result.append("MicroEdition-Profile: MIDP-2.0\n");
    result.append("MicroEdition-Configuration: CLDC-1.1\n");
    result.append("MIDlet-1: Installer Test,,SuiteApp\n");
    result.append("MIDlet-Permissions: javax.microedition.io.Connector.http,");
    if (continued) {
        result.append("\n javax.microedition.io.Connector.socket\n");
    } else {
        result.append(" javax.microedition.io.Connector.socket\n");
    }
    result.append("Custom-UTF8: Tiếng Việt\n");
    return result;
}

void test_parser() {
    const std::string text =
        "Alpha: first\n"
        " continuation\n"
        "Unicode: Tiếng Việt\n";
    auto parsed = JadParser::parse(std::span<const u8>(
        reinterpret_cast<const u8*>(text.data()), text.size()));
    check(parsed.has_value(), "parse valid continued UTF-8 JAD");
    if (parsed) {
        check(parsed->find("Alpha") != nullptr &&
                  *parsed->find("Alpha") == "firstcontinuation",
              "join continuation without injected whitespace");
        check(parsed->find("Unicode") != nullptr,
              "retain UTF-8 property");
    }

    const std::string duplicate = "A: one\nA: two\n";
    auto rejected = JadParser::parse(std::span<const u8>(
        reinterpret_cast<const u8*>(duplicate.data()), duplicate.size()));
    check_error(rejected, ErrorCode::invalid_argument,
                "reject duplicate JAD property by default");

    AttributeParserLimits last_wins;
    last_wins.duplicate_policy = DuplicatePropertyPolicy::last_wins;
    auto accepted = JadParser::parse(std::span<const u8>(
        reinterpret_cast<const u8*>(duplicate.data()), duplicate.size()),
        last_wins);
    check(accepted.has_value() && accepted->find("A") != nullptr &&
              *accepted->find("A") == "two",
          "support explicit last-wins duplicate policy");

    const std::vector<u8> invalid_utf8 {0xC0U, 0xAFU};
    auto invalid = JadParser::parse(invalid_utf8);
    check_error(invalid, ErrorCode::invalid_argument,
                "reject overlong UTF-8 in JAD");
}

void test_install_flow(const std::filesystem::path& root,
                       const std::filesystem::path& jar_v1,
                       const std::filesystem::path& jar_v2) {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    check(!error, "create suite test root");

    const std::filesystem::path imported_v1 = root / "import-v1.jar";
    const std::filesystem::path imported_v2 = root / "import-v2.jar";
    check(copy_fixture_file(jar_v1, imported_v1), "copy version 1 import fixture");
    check(copy_fixture_file(jar_v2, imported_v2), "copy version 2 import fixture");

    const std::filesystem::path jad_v1 = root / "app-v1.jad";
    const std::filesystem::path jad_v2 = root / "app-v2.jad";
    check(write_text(jad_v1, make_jad(imported_v1, "1.0.0",
                                      fixture_file_size(imported_v1), true)),
          "write version 1 JAD");
    check(write_text(jad_v2, make_jad(imported_v2, "1.1.0",
                                      fixture_file_size(imported_v2))),
          "write version 2 JAD");

    SuiteStore store;
    auto configured = store.configure(SuiteStoreConfig {.root_path = root.string()});
    check(configured.has_value(), "configure persistent suite store");
    if (!configured) {
        std::cerr << "configure error: " << configured.error().message << '\n';
        return;
    }

    auto installed = store.install(jad_v1.string(), imported_v1.string());
    check(installed.has_value(), "install JAD plus JAR");
    if (!installed) return;
    const SuiteId id = *installed;

    const auto* suite = store.find(id);
    check(suite != nullptr, "find installed suite");
    if (suite != nullptr) {
        check(suite->managed, "suite uses managed storage");
        check(suite->display_name == "Installer Test" &&
                  suite->vendor == "phoneME" && suite->version == "1.0.0",
              "persist suite identity and version");
        check(suite->declared_midlets.size() == 1U &&
                  suite->declared_midlets[0] == "SuiteApp",
              "parse MIDlet-n declaration");
        check(suite->declared_permissions.size() == 2U,
              "merge continued permission declaration");
        check(suite->raw_properties.contains("Custom-UTF8") &&
                  suite->properties.contains(u"Custom-UTF8"),
              "expose merged UTF-8 JAD properties to MIDlet runtime");
        check(!suite->jad_path.empty(), "copy JAD into managed suite directory");
    }

    std::filesystem::remove(imported_v1, error);
    std::filesystem::remove(jad_v1, error);
    auto loaded = store.load_class(id, "SuiteApp");
    check(loaded.has_value(),
          "load class after original JAR and JAD imports are deleted");

    store.clear();
    auto restarted = store.configure(SuiteStoreConfig {.root_path = root.string()});
    check(restarted.has_value(), "reload persistent suite database after restart");
    check(store.list().size() == 1U && store.list()[0] == id,
          "list same suite ID after restart");
    check(store.load_class(id, "SuiteApp").has_value(),
          "load suite class after restart");

    const std::filesystem::path rms_marker =
        root / "rms" / std::to_string(id.value) / "marker.bin";
    std::filesystem::create_directories(rms_marker.parent_path(), error);
    check(!error && write_text(rms_marker, "keep"),
          "create RMS preservation marker");

    auto upgraded = store.install(jad_v2.string(), imported_v2.string());
    check(upgraded.has_value() && *upgraded == id,
          "upgrade keeps stable suite ID");
    const auto* upgraded_suite = store.find(id);
    check(upgraded_suite != nullptr && upgraded_suite->version == "1.1.0",
          "upgrade replaces suite version");
    check(std::filesystem::exists(rms_marker, error) && !error,
          "upgrade preserves RMS data");

    const std::filesystem::path downgrade_jad = root / "downgrade.jad";
    check(write_text(downgrade_jad,
                     make_jad(jar_v1, "1.0.0", fixture_file_size(jar_v1))),
          "write downgrade JAD");
    auto downgrade = store.install(downgrade_jad.string(), jar_v1.string());
    check_error(downgrade, ErrorCode::invalid_state,
                "reject suite downgrade by default");

    const std::filesystem::path final_directory =
        root / "suites" / std::to_string(id.value);
    const std::filesystem::path backup_directory =
        root / "suites" / (".backup-" + std::to_string(id.value));
    store.clear();
    std::filesystem::rename(final_directory, backup_directory, error);
    check(!error, "simulate crash after suite backup rename");
    auto recovered_transaction =
        store.configure(SuiteStoreConfig {.root_path = root.string()});
    check(recovered_transaction.has_value(),
          "recover interrupted install transaction");
    check(std::filesystem::is_directory(final_directory, error) && !error,
          "restore suite directory from transaction backup");

    store.clear();
    std::filesystem::remove_all(backup_directory, error);
    error.clear();
    std::filesystem::copy(
        final_directory, backup_directory,
        std::filesystem::copy_options::recursive,
        error);
    check(!error, "create valid backup beside an uncommitted final suite");
    check(copy_fixture_file(jar_v1, final_directory / "app.jar") &&
              copy_fixture_file(downgrade_jad, final_directory / "app.jad"),
          "simulate activated suite files before database commit");
    auto recovered_precommit =
        store.configure(SuiteStoreConfig {.root_path = root.string()});
    check(recovered_precommit.has_value(),
          "select transaction candidate matching persistent SHA-256 record");
    const auto* precommit_suite = store.find(id);
    check(precommit_suite != nullptr && precommit_suite->version == "1.1.0",
          "restore backup when final files do not match committed generation");
    check(!std::filesystem::exists(backup_directory, error) && !error,
          "remove obsolete transaction backup after recovery");

    const std::filesystem::path orphan_stage =
        root / "suites" / (".stage-" + std::to_string(id.value));
    std::filesystem::create_directories(orphan_stage, error);
    check(!error && write_text(orphan_stage / "partial", "partial"),
          "create orphan install stage");
    store.clear();
    auto cleaned_stage =
        store.configure(SuiteStoreConfig {.root_path = root.string()});
    check(cleaned_stage.has_value(), "reload store while orphan stage exists");
    check(!std::filesystem::exists(orphan_stage, error) && !error,
          "remove orphan install stage during recovery");

    store.clear();
    check(write_text(root / "suites.db", "corrupt"),
          "corrupt primary suite database");
    auto recovered_database =
        store.configure(SuiteStoreConfig {.root_path = root.string()});
    check(recovered_database.has_value(),
          "recover corrupt database from checksum-valid backup");
    const auto* recovered_suite = store.find(id);
    check(recovered_suite != nullptr && recovered_suite->version == "1.1.0",
          "database recovery restores current generation");

    auto uninstalled = store.uninstall(id);
    check(uninstalled.has_value(), "uninstall managed suite");
    check(store.find(id) == nullptr, "remove suite from database view");
    check(std::filesystem::exists(rms_marker, error) && !error,
          "default uninstall policy preserves RMS");

    auto reinstalled = store.install(jad_v2.string(), imported_v2.string());
    check(reinstalled.has_value() && *reinstalled == id,
          "reinstall derives the same stable suite ID");

    const std::filesystem::path files_marker =
        root / "files" / std::to_string(id.value) / "marker.bin";
    const std::filesystem::path permission_marker =
        root / "security" / (std::to_string(id.value) + ".permissions");
    std::filesystem::create_directories(files_marker.parent_path(), error);
    std::filesystem::create_directories(permission_marker.parent_path(), error);
    check(!error && write_text(files_marker, "remove") &&
              write_text(permission_marker, "remove"),
          "create suite data removal markers");

    auto removed_data = store.uninstall(
        id,
        SuiteUninstallPolicy {
            .remove_rms = true,
            .remove_files = true,
            .remove_permissions = true,
        });
    check(removed_data.has_value(), "uninstall with data removal policy");
    check(!std::filesystem::exists(rms_marker.parent_path(), error) && !error,
          "remove RMS under explicit uninstall policy");
    check(!std::filesystem::exists(files_marker.parent_path(), error) && !error,
          "remove files under explicit uninstall policy");
    check(!std::filesystem::exists(permission_marker, error) && !error,
          "remove persisted permissions under explicit uninstall policy");
}

void test_validation(const std::filesystem::path& root,
                     const std::filesystem::path& jar_v1,
                     const std::filesystem::path& missing_class_jar,
                     const std::filesystem::path& traversal_jar,
                     const std::filesystem::path& zip_bomb_jar) {
    std::error_code error;
    std::filesystem::create_directories(root, error);
    check(!error, "create validation root");

    SuiteStore jar_only_store;
    auto jar_only_configured = jar_only_store.configure(
        SuiteStoreConfig {.root_path = (root / "jar-only").string()});
    check(jar_only_configured.has_value(), "configure JAR-only suite store");
    auto jar_only_id = jar_only_store.install(jar_v1.string());
    check(jar_only_id.has_value(), "install suite directly from JAR manifest");
    if (jar_only_id) {
        const auto* jar_only_suite = jar_only_store.find(*jar_only_id);
        check(jar_only_suite != nullptr && jar_only_suite->jad_path.empty() &&
                  jar_only_suite->version == "1.0.0",
              "persist JAR-only suite without synthetic JAD");
    }

    auto jar_only_descriptor = SuiteInstaller::inspect(jar_v1.string());
    check(jar_only_descriptor.has_value(), "inspect JAR-only descriptor");
    if (jar_only_descriptor) {
        check(SuiteInstaller::digest_hex(
                  jar_only_descriptor->identity_sha256) ==
                  "1b2844537f8ce654380bc25cef6e0ae654f2ad13cefd4d3e9116c7bc5607530c",
              "derive stable suite identity using SHA-256");
    }

    const std::filesystem::path mismatch_jad = root / "mismatch.jad";
    check(write_text(mismatch_jad,
                     make_jad(jar_v1, "1.0.0", fixture_file_size(jar_v1) + 1U)),
          "write JAD with mismatched size");
    auto mismatch = SuiteInstaller::inspect(
        jar_v1.string(), std::optional<std::string>(mismatch_jad.string()));
    check_error(mismatch, ErrorCode::checksum_mismatch,
                "reject JAD size mismatch");

    auto missing = SuiteInstaller::inspect(missing_class_jar.string());
    check_error(missing, ErrorCode::class_not_found,
                "reject manifest referencing missing MIDlet class");

    auto traversal = SuiteInstaller::inspect(traversal_jar.string());
    check_error(traversal, ErrorCode::malformed_archive,
                "reject JAR path traversal entry");

    auto bomb = SuiteInstaller::inspect(zip_bomb_jar.string());
    check_error(bomb, ErrorCode::out_of_range,
                "reject JAR compression-ratio bomb");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        std::cerr << "usage: SuiteInstallerTests <root> <v1.jar> <v2.jar> "
                     "<missing.jar> <traversal.jar> <zipbomb.jar>\n";
        return 2;
    }

    const std::filesystem::path root(argv[1]);
    test_parser();
    test_install_flow(root / "store", argv[2], argv[3]);
    test_validation(root / "validation", argv[2], argv[4], argv[5], argv[6]);

    if (failures != 0) {
        std::cerr << failures << " suite installer test(s) failed\n";
        return 1;
    }
    std::cout << "Suite installer tests passed\n";
    return 0;
}
