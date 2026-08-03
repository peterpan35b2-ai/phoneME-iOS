#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "phoneme/security/PermissionPolicy.hpp"

namespace {

using phoneme::ErrorCode;
using phoneme::SuiteId;
using phoneme::security::PermissionDecision;
using phoneme::security::PermissionDomain;
using phoneme::security::PermissionPolicy;
using phoneme::security::PermissionPolicyConfig;
using phoneme::security::PermissionRequest;
using phoneme::security::PermissionResponse;
using phoneme::security::PermissionScope;
using phoneme::security::SuiteTrust;
namespace permissions = phoneme::security::permissions;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void test_scopes_and_persistence(const std::filesystem::path& root) {
    const auto decisions = root / "101.permissions";
    int http_prompts = 0;
    int file_prompts = 0;
    int media_prompts = 0;

    PermissionPolicy policy;
    require(policy.configure(PermissionPolicyConfig {
                .suite_id = SuiteId {101},
                .trust = SuiteTrust::untrusted,
                .persistence_path = decisions.string(),
                .declared_permissions = {
                    std::string(permissions::connector_http),
                    std::string(permissions::connector_file_read),
                    std::string(permissions::media_record),
                },
                .enforce_declared_permissions = true,
                .prompt = [&](const PermissionRequest& request) {
                    if (request.permission == permissions::connector_http) {
                        ++http_prompts;
                        return PermissionResponse {
                            PermissionDecision::allowed,
                            PermissionScope::one_shot,
                        };
                    }
                    if (request.permission ==
                        permissions::connector_file_read) {
                        ++file_prompts;
                        return PermissionResponse {
                            PermissionDecision::allowed,
                            PermissionScope::session,
                        };
                    }
                    ++media_prompts;
                    return PermissionResponse {
                        PermissionDecision::denied,
                        PermissionScope::blanket,
                    };
                },
            }).has_value(),
            "configure untrusted policy");

    auto http_first = policy.request(permissions::connector_http);
    auto http_second = policy.request(permissions::connector_http);
    require(http_first.has_value() && http_second.has_value() &&
                http_first->decision == PermissionDecision::allowed &&
                http_second->decision == PermissionDecision::allowed &&
                http_prompts == 2,
            "one-shot decisions are not cached");

    auto file_first = policy.request(permissions::connector_file_read);
    auto file_second = policy.request(permissions::connector_file_read);
    require(file_first.has_value() && file_second.has_value() &&
                file_second->scope == PermissionScope::session &&
                file_prompts == 1,
            "session decisions are cached");

    auto denied = policy.require(permissions::media_record);
    require(!denied.has_value() &&
                denied.error().code == ErrorCode::java_exception &&
                denied.error().java_exception_class ==
                    "java/lang/SecurityException" &&
                media_prompts == 1,
            "denial becomes SecurityException");

    auto undeclared = policy.request("vendor.permission.undeclared");
    require(undeclared.has_value() &&
                undeclared->decision == PermissionDecision::denied,
            "undeclared permission is denied without prompt");

    PermissionPolicy reloaded;
    require(reloaded.configure(PermissionPolicyConfig {
                .suite_id = SuiteId {101},
                .trust = SuiteTrust::untrusted,
                .persistence_path = decisions.string(),
                .declared_permissions = {
                    std::string(permissions::connector_http),
                    std::string(permissions::connector_file_read),
                    std::string(permissions::media_record),
                },
                .enforce_declared_permissions = true,
            }).has_value(),
            "reload persistent decisions");
    require(reloaded.check(permissions::media_record) ==
                PermissionDecision::denied &&
                reloaded.check(permissions::connector_file_read) ==
                PermissionDecision::unknown,
            "blanket persists while session expires");
}

void test_trusted_and_domains() {
    int prompts = 0;
    PermissionPolicy trusted;
    require(trusted.configure(PermissionPolicyConfig {
                .suite_id = SuiteId {102},
                .trust = SuiteTrust::trusted,
                .prompt = [&](const PermissionRequest&) {
                    ++prompts;
                    return PermissionResponse {
                        PermissionDecision::denied,
                        PermissionScope::one_shot,
                    };
                },
            }).has_value(),
            "configure trusted policy");
    auto result = trusted.request("vendor.permission.compatibility");
    require(result.has_value() &&
                result->decision == PermissionDecision::allowed &&
                result->scope == PermissionScope::session && prompts == 0,
            "trusted suite bypasses prompt");

    require(PermissionPolicy::domain_for_permission(
                permissions::connector_socket) == PermissionDomain::network &&
                PermissionPolicy::domain_for_permission(
                    permissions::connector_file_write) ==
                    PermissionDomain::filesystem &&
                PermissionPolicy::domain_for_permission(
                    permissions::media_capture_audio) ==
                    PermissionDomain::media,
            "permission names map to domains");
}

void test_prompt_reentry_is_rejected() {
    PermissionPolicy policy;
    bool nested_rejected = false;
    require(policy.configure(PermissionPolicyConfig {
                .suite_id = SuiteId {104},
                .trust = SuiteTrust::untrusted,
                .declared_permissions = {
                    std::string(permissions::connector_http),
                },
                .enforce_declared_permissions = true,
                .prompt = [&](const PermissionRequest&) {
                    auto nested = policy.request(
                        permissions::connector_http,
                        "http://nested.invalid");
                    nested_rejected = !nested.has_value() &&
                        nested.error().code == ErrorCode::invalid_state;
                    return PermissionResponse {
                        PermissionDecision::allowed,
                        PermissionScope::one_shot,
                    };
                },
            }).has_value(),
            "configure recursive prompt policy");

    auto outer = policy.request(permissions::connector_http,
                                "http://outer.invalid");
    require(outer.has_value() &&
                outer->decision == PermissionDecision::allowed &&
                nested_rejected,
            "same-policy prompt re-entry is rejected without deadlock");
}

void test_concurrent_prompt_coalescing() {
    std::atomic<int> prompts {0};
    PermissionPolicy policy;
    require(policy.configure(PermissionPolicyConfig {
                .suite_id = SuiteId {103},
                .trust = SuiteTrust::untrusted,
                .declared_permissions = {
                    std::string(permissions::connector_https),
                },
                .enforce_declared_permissions = true,
                .prompt = [&](const PermissionRequest&) {
                    prompts.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                    return PermissionResponse {
                        PermissionDecision::allowed,
                        PermissionScope::session,
                    };
                },
            }).has_value(),
            "configure concurrent policy");

    std::atomic<int> allowed {0};
    const auto request = [&] {
        auto result = policy.request(permissions::connector_https);
        if (result.has_value() &&
            result->decision == PermissionDecision::allowed) {
            allowed.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::thread first(request);
    std::thread second(request);
    first.join();
    second.join();

    require(allowed.load(std::memory_order_relaxed) == 2 &&
                prompts.load(std::memory_order_relaxed) == 1,
            "concurrent session requests share one prompt");
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        "phoneme-security-policy-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    require(!error, "clear security test directory");
    std::filesystem::create_directories(root, error);
    require(!error, "create security test directory");

    test_scopes_and_persistence(root);
    test_trusted_and_domains();
    test_prompt_reentry_is_rejected();
    test_concurrent_prompt_coalescing();

    std::filesystem::remove_all(root, error);
    require(!error, "remove security test directory");
    std::cout << "Security policy tests passed\n";
    return 0;
}
