#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "phoneme/security/PermissionCatalog.hpp"
#include "phoneme/security/PermissionPolicy.hpp"

namespace {

namespace security = phoneme::security;
using phoneme::ErrorCode;
using phoneme::SuiteId;

[[noreturn]] void fail_test(const char* message) {
    std::cerr << "SecurityIntegrationTests failure: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) fail_test(message);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "failed to open test file");
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.is_open(), "failed to create test file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(output.good(), "failed to write test file");
}

security::PermissionPolicyConfig base_config(
    SuiteId suite,
    const std::filesystem::path& path = {}) {
    return security::PermissionPolicyConfig {
        .suite_id = suite,
        .trust = security::SuiteTrust::untrusted,
        .persistence_path = path.string(),
        .required_permissions = {
            std::string(security::permissions::connector_http),
            std::string(security::permissions::connector_file_read),
        },
        .optional_permissions = {
            std::string(security::permissions::media_record),
            std::string(security::permissions::push_registry),
            std::string(security::permissions::platform_request),
        },
        .enforce_declared_permissions = true,
        .trusted_default_allow = true,
    };
}

void test_declaration_and_catalog() {
    security::PermissionPolicy policy;
    auto config = base_config(SuiteId {11});
    require(policy.configure(std::move(config)).has_value(),
            "failed to configure declaration policy");
    require(policy.declaration(security::permissions::connector_http) ==
                security::PermissionDeclaration::required,
            "required declaration was not preserved");
    require(policy.declaration(security::permissions::media_record) ==
                security::PermissionDeclaration::optional,
            "optional declaration was not preserved");
    require(policy.declaration("vendor.permission.missing") ==
                security::PermissionDeclaration::undeclared,
            "undeclared permission was misclassified");
    require(policy.check("vendor.permission.missing") ==
                security::PermissionDecision::denied,
            "undeclared permission was not denied");

    require(security::PermissionCatalog::known(
                security::permissions::connector_https),
            "HTTPS is missing from permission catalog");
    require(security::PermissionCatalog::domain_for(
                security::permissions::push_registry) ==
                security::PermissionDomain::push,
            "PushRegistry permission domain is incorrect");
    require(security::PermissionCatalog::domain_for(
                security::permissions::platform_request) ==
                security::PermissionDomain::platform,
            "platformRequest permission domain is incorrect");
}

void test_one_shot_prompt_coalescing() {
    security::PermissionPolicy policy;
    std::atomic<int> prompt_count {0};
    auto config = base_config(SuiteId {12});
    config.prompt = [&](const security::PermissionRequest& request) {
        require(request.resource == "https://example.com/private/path",
                "prompt resource was not sanitized");
        prompt_count.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return security::PermissionResponse {
            .decision = security::PermissionDecision::allowed,
            .scope = security::PermissionScope::one_shot,
        };
    };
    require(policy.configure(std::move(config)).has_value(),
            "failed to configure coalescing policy");

    constexpr int kWorkers = 12;
    std::atomic<bool> start {false};
    std::atomic<int> allowed {0};
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int index = 0; index < kWorkers; ++index) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto response = policy.request(
                security::permissions::connector_http,
                "https://user:secret@example.com/private/path?token=hidden#x");
            if (response && response->decision ==
                                security::PermissionDecision::allowed) {
                allowed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread& worker : workers) worker.join();

    require(allowed.load(std::memory_order_relaxed) == kWorkers,
            "coalesced callers did not receive the same decision");
    require(prompt_count.load(std::memory_order_relaxed) == 1,
            "concurrent one-shot requests produced duplicate prompts");

    auto sequential = policy.request(
        security::permissions::connector_http,
        "https://example.com/private/path");
    require(sequential.has_value() &&
                prompt_count.load(std::memory_order_relaxed) == 2,
            "sequential one-shot request was incorrectly cached");
}

void test_session_and_reentrant_prompt() {
    security::PermissionPolicy policy;
    std::atomic<int> prompt_count {0};
    std::atomic<bool> callback_observed_policy {false};
    auto config = base_config(SuiteId {13});
    config.prompt = [&](const security::PermissionRequest&) {
        prompt_count.fetch_add(1, std::memory_order_relaxed);
        callback_observed_policy.store(
            policy.declaration(security::permissions::connector_file_read) ==
                security::PermissionDeclaration::required &&
            policy.check(security::permissions::connector_file_read) ==
                security::PermissionDecision::unknown,
            std::memory_order_relaxed);
        auto recursive = policy.request(security::permissions::media_record);
        require(!recursive.has_value() &&
                    recursive.error().code == ErrorCode::invalid_state,
                "recursive host prompt was not rejected safely");
        return security::PermissionResponse {
            .decision = security::PermissionDecision::allowed,
            .scope = security::PermissionScope::session,
        };
    };
    require(policy.configure(std::move(config)).has_value(),
            "failed to configure session policy");

    require(policy.request(
                security::permissions::connector_file_read).has_value(),
            "first session request failed");
    require(policy.request(
                security::permissions::connector_file_read).has_value(),
            "cached session request failed");
    require(prompt_count.load(std::memory_order_relaxed) == 1,
            "session permission prompted more than once");
    require(callback_observed_policy.load(std::memory_order_relaxed),
            "prompt callback could not re-enter non-prompt policy APIs");

    policy.reset_session();
    require(policy.request(
                security::permissions::connector_file_read).has_value(),
            "session request after reset failed");
    require(prompt_count.load(std::memory_order_relaxed) == 2,
            "session reset did not revoke cached decision");
}

void test_blanket_persistence_and_corruption(
    const std::filesystem::path& root) {
    const auto decision_path = root / "21.permissions";
    security::PermissionPolicy policy;
    auto config = base_config(SuiteId {21}, decision_path);
    config.prompt = [](const security::PermissionRequest&) {
        return security::PermissionResponse {
            .decision = security::PermissionDecision::denied,
            .scope = security::PermissionScope::blanket,
        };
    };
    require(policy.configure(std::move(config)).has_value(),
            "failed to configure blanket policy");
    auto denied = policy.require(security::permissions::media_record);
    require(!denied.has_value() &&
                denied.error().java_exception_class ==
                    "java/lang/SecurityException",
            "blanket denial did not map to SecurityException");

    const std::string persisted = read_file(decision_path);
    require(persisted.starts_with("PHONEME-PERMISSIONS-2\n") &&
                persisted.find("suite\t21\n") != std::string::npos &&
                persisted.find("checksum\t") != std::string::npos,
            "permission persistence did not use V2 suite-bound format");

    security::PermissionPolicy reloaded;
    require(reloaded.configure(base_config(SuiteId {21}, decision_path))
                .has_value(),
            "failed to reload blanket decision");
    require(reloaded.check(security::permissions::media_record) ==
                security::PermissionDecision::denied,
            "blanket decision did not survive restart");

    security::PermissionPolicy wrong_suite;
    auto wrong = wrong_suite.configure(
        base_config(SuiteId {22}, decision_path));
    require(!wrong.has_value() && wrong.error().code == ErrorCode::io_error,
            "suite binding did not reject copied permission file");

    std::string corrupted = persisted;
    const std::string permission(security::permissions::media_record);
    const std::size_t location = corrupted.find(permission);
    require(location != std::string::npos,
            "persisted decision is missing expected permission");
    corrupted[location] = corrupted[location] == 'j' ? 'k' : 'j';
    write_file(decision_path, corrupted);

    security::PermissionPolicy corrupt_policy;
    auto corrupt = corrupt_policy.configure(
        base_config(SuiteId {21}, decision_path));
    require(!corrupt.has_value() &&
                corrupt.error().code == ErrorCode::checksum_mismatch,
            "permission corruption did not fail checksum validation");
}

void test_v1_migration(const std::filesystem::path& root) {
    const auto path = root / "31.permissions";
    write_file(path,
               "PHONEME-PERMISSIONS-1\n"
               "A\tjavax.microedition.io.Connector.http\n");

    security::PermissionPolicy policy;
    require(policy.configure(base_config(SuiteId {31}, path)).has_value(),
            "V1 permission file did not migrate");
    require(policy.check(security::permissions::connector_http) ==
                security::PermissionDecision::allowed,
            "migrated blanket decision was lost");
    const std::string migrated = read_file(path);
    require(migrated.starts_with("PHONEME-PERMISSIONS-2\n") &&
                migrated.find("suite\t31\n") != std::string::npos,
            "V1 permission file was not rewritten as V2");
}

void test_trusted_default_and_isolation(
    const std::filesystem::path& root) {
    int trusted_prompts = 0;
    security::PermissionPolicy trusted;
    auto trusted_config = base_config(SuiteId {41}, root / "41.permissions");
    trusted_config.trust = security::SuiteTrust::trusted;
    trusted_config.prompt = [&](const security::PermissionRequest&) {
        ++trusted_prompts;
        return security::PermissionResponse {
            .decision = security::PermissionDecision::denied,
            .scope = security::PermissionScope::one_shot,
        };
    };
    require(trusted.configure(std::move(trusted_config)).has_value(),
            "failed to configure trusted policy");
    auto trusted_result = trusted.request(
        security::permissions::connector_http);
    require(trusted_result.has_value() &&
                trusted_result->decision ==
                    security::PermissionDecision::allowed &&
                trusted_prompts == 0,
            "trusted default policy unexpectedly prompted");

    security::PermissionPolicy isolated;
    require(isolated.configure(
                base_config(SuiteId {42}, root / "42.permissions"))
                .has_value(),
            "failed to configure isolated policy");
    require(isolated.check(security::permissions::media_record) ==
                security::PermissionDecision::unknown,
            "permission decision leaked across suites");
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "phoneme-security-integration-tests";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    require(!cleanup_error, "failed to clear security test root");
    std::filesystem::create_directories(root, cleanup_error);
    require(!cleanup_error, "failed to create security test root");

    test_declaration_and_catalog();
    test_one_shot_prompt_coalescing();
    test_session_and_reentrant_prompt();
    test_blanket_persistence_and_corruption(root);
    test_v1_migration(root);
    test_trusted_default_and_isolation(root);

    std::filesystem::remove_all(root, cleanup_error);
    require(!cleanup_error, "failed to remove security test root");
    std::cout << "SecurityIntegrationTests passed\n";
    return 0;
}
