#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/security/PermissionSemantics.hpp"

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
namespace semantics = phoneme::security;

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

void test_permission_actions_parsing() {
    require(semantics::parse_actions("read") == semantics::kActionRead,
            "read parses to the read bit");
    require(semantics::parse_actions("write") == semantics::kActionWrite,
            "write parses to the write bit");
    require(semantics::parse_actions("read,write") == semantics::kActionAll,
            "read,write parses to both bits");
    require(semantics::parse_actions("write,read") == semantics::kActionAll,
            "action order is irrelevant");
    require(semantics::parse_actions("READ") == semantics::kActionRead,
            "actions are case-insensitive");
    require(semantics::parse_actions("") == semantics::kActionNone,
            "empty actions yield no mask");
    require(semantics::parse_actions("read, write") < 0,
            "spaces between actions are invalid");
    require(semantics::parse_actions("bogus") < 0,
            "unknown actions are invalid");
    require(semantics::parse_actions("read,") < 0,
            "trailing comma is invalid");

    require(semantics::format_actions(semantics::kActionNone).empty(),
            "no actions format to empty");
    require(semantics::format_actions(semantics::kActionRead) == "read",
            "read formats canonically");
    require(semantics::format_actions(semantics::kActionWrite) == "write",
            "write formats canonically");
    require(semantics::format_actions(semantics::kActionAll) == "read,write",
            "both actions format in fixed order");
}

void test_basic_permission_wildcards() {
    require(semantics::basic_implies_name("exit", "exit"),
            "literal name implies itself");
    require(!semantics::basic_implies_name("exit", "setIO"),
            "unrelated literals do not imply");
    require(semantics::basic_implies_name("java.*", "java.home"),
            "wildcard implies a deeper name");
    require(!semantics::basic_implies_name("java.*", "java"),
            "wildcard does not imply its own stem");
    require(!semantics::basic_implies_name("java.home", "java.*"),
            "literal does not imply a wildcard");
    require(semantics::basic_implies_name("*", "anything.at.all"),
            "global wildcard implies everything");
    require(semantics::basic_implies_name("a.b.*", "a.b.c"),
            "nested wildcard implies a descendant");
    require(!semantics::basic_implies_name("a.b.*", "a.b"),
            "nested wildcard does not imply its stem");
    require(semantics::basic_implies_name("a.*", "a.b.*"),
            "one wildcard can imply another");
}

void test_property_permission_implies() {
    require(semantics::property_implies("java.*", semantics::kActionRead,
                                        "java.home", semantics::kActionRead),
            "matching mask with wildcard name implies");
    require(!semantics::property_implies("java.*", semantics::kActionRead,
                                         "java.home", semantics::kActionAll),
            "read alone does not imply read,write");
    require(semantics::property_implies("java.home", semantics::kActionAll,
                                        "java.home", semantics::kActionRead),
            "read,write implies the read subset");
    require(!semantics::property_implies("java.*", semantics::kActionRead,
                                         "os.name", semantics::kActionRead),
            "non-matching name never implies");
}

void test_string_hashcode() {
    require(semantics::java_string_hashcode(u"") == 0,
            "empty string hashes to zero");
    require(semantics::java_string_hashcode(u"abc") == 96354,
            "hashCode matches the Java reference for abc");
    require(semantics::java_string_hashcode(u"exit") == 3127582,
            "hashCode matches the Java reference for exit");
}

void test_collection_implies() {
    const std::vector<semantics::PermissionEntry> basic_entries {
        semantics::PermissionEntry {.name = "exit"},
        semantics::PermissionEntry {.name = "setIO"},
    };
    require(semantics::basic_collection_implies(basic_entries, "exit"),
            "basic collection implies a stored literal");
    require(!semantics::basic_collection_implies(basic_entries, "setFactory"),
            "basic collection rejects an unstored name");

    const std::vector<semantics::PermissionEntry> wildcard_entries {
        semantics::PermissionEntry {.name = "loadLibrary.*"},
    };
    require(semantics::basic_collection_implies(wildcard_entries,
                                                "loadLibrary.foo"),
            "basic collection honors wildcard entries");

    const std::vector<semantics::PermissionEntry> split_mask {
        semantics::PermissionEntry {.name = "java.home",
                                    .mask = semantics::kActionRead},
        semantics::PermissionEntry {.name = "java.home",
                                    .mask = semantics::kActionWrite},
    };
    require(semantics::property_collection_implies(
                split_mask, "java.home", semantics::kActionAll),
            "duplicate names merge their action masks");
    require(!semantics::property_collection_implies(
                split_mask, "os.name", semantics::kActionRead),
            "property collection rejects unmatched names");
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
    test_permission_actions_parsing();
    test_basic_permission_wildcards();
    test_property_permission_implies();
    test_string_hashcode();
    test_collection_implies();

    std::filesystem::remove_all(root, error);
    require(!error, "remove security test directory");
    std::cout << "Security policy tests passed\n";
    return 0;
}
