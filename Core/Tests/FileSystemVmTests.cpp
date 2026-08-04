#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

phoneme::security::SharedPermissionPolicy make_file_policy(
    phoneme::SuiteId suite_id,
    phoneme::security::PermissionPromptCallback prompt,
    std::vector<std::string> declared_permissions) {
    auto policy = std::make_shared<phoneme::security::PermissionPolicy>();
    auto configured = policy->configure(
        phoneme::security::PermissionPolicyConfig {
            .suite_id = suite_id,
            .trust = phoneme::security::SuiteTrust::untrusted,
            .declared_permissions = std::move(declared_permissions),
            .enforce_declared_permissions = true,
            .prompt = std::move(prompt),
        });
    require(configured.has_value(), "configure filesystem VM permission policy");
    return policy;
}

phoneme::security::SharedPermissionPolicy allowed_file_policy() {
    return make_file_policy(
        phoneme::SuiteId {808},
        [](const phoneme::security::PermissionRequest&) {
            return phoneme::security::PermissionResponse {
                phoneme::security::PermissionDecision::allowed,
                phoneme::security::PermissionScope::session,
            };
        },
        {
            std::string(
                phoneme::security::permissions::connector_file_read),
            std::string(
                phoneme::security::permissions::connector_file_write),
        });
}

void invoke_int(phoneme::vm::Machine& machine,
                const char* method,
                phoneme::i32 expected) {
    auto result = machine.invoke_static("corefixture/FileOps",
                                        method,
                                        "()I",
                                        {},
                                        25'000'000);
    require(result.has_value(), method);
    require(result->completed_normally(), method);
    require(result->return_value.has_value(), method);
    auto value = result->return_value->as_int();
    require(value.has_value() && *value == expected, method);
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 3, "usage: FileSystemVmTests <fixture-jar> <runtime-root>");
    const std::filesystem::path runtime_root(argv[2]);
    const auto sandbox = runtime_root / "suite";
    const auto temporary = runtime_root / "temporary";
    std::error_code error;
    std::filesystem::remove_all(runtime_root, error);
    error.clear();

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(argv[1]).has_value(),
            "add filesystem VM fixture archive");
    phoneme::vm::Machine machine(classes);
    machine.set_permission_policy(allowed_file_policy());
    require(machine.configure_filesystem(sandbox.string(), temporary.string())
                .has_value(),
            "configure filesystem VM sandbox");

    invoke_int(machine, "resourceLookup", 1);
    invoke_int(machine, "resourceTraversalBlocked", 1);
    invoke_int(machine, "fileRoundTrip", 1);
    invoke_int(machine, "javaIoFileCompatibility", 1);
    invoke_int(machine, "suppressedExceptions", 1);
    invoke_int(machine, "traversalBlocked", 1);
    invoke_int(machine, "closedHandleRejected", 1);
    invoke_int(machine, "offsetAndOpenHandlePolicy", 1);
    invoke_int(machine, "modeAndClosedExceptions", 1);
    invoke_int(machine, "surfaceSemantics", 1);
    invoke_int(machine, "rootEnumeration", 1);
    invoke_int(machine, "rootListenerRegistry", 1);
    invoke_int(machine, "hiddenListingPolicy", 1);

    phoneme::vm::Machine denied_machine(classes);
    denied_machine.set_permission_policy(make_file_policy(
        phoneme::SuiteId {809},
        [](const phoneme::security::PermissionRequest&) {
            return phoneme::security::PermissionResponse {
                phoneme::security::PermissionDecision::denied,
                phoneme::security::PermissionScope::one_shot,
            };
        },
        {
            std::string(
                phoneme::security::permissions::connector_file_read),
        }));
    require(denied_machine.configure_filesystem(
                (runtime_root / "denied-suite").string(),
                (runtime_root / "denied-temporary").string())
                .has_value(),
            "configure denied filesystem VM sandbox");
    invoke_int(denied_machine, "connectorPermissionDenied", 1);

    const auto recheck_sandbox = runtime_root / "recheck-suite";
    std::filesystem::create_directories(recheck_sandbox, error);
    require(!error, "create stream recheck sandbox");
    {
        std::ofstream gate(recheck_sandbox / "gate.bin",
                           std::ios::binary | std::ios::trunc);
        gate << "gate";
        require(gate.good(), "create stream permission gate fixture");
    }
    auto requests = std::make_shared<std::atomic_int>(0);
    auto canonical_resource = std::make_shared<std::atomic_bool>(true);
    phoneme::vm::Machine recheck_machine(classes);
    recheck_machine.set_permission_policy(make_file_policy(
        phoneme::SuiteId {810},
        [requests, canonical_resource](
            const phoneme::security::PermissionRequest& request) {
            if (request.resource != "file:///gate.bin") {
                canonical_resource->store(false, std::memory_order_relaxed);
            }
            const int index = requests->fetch_add(
                1, std::memory_order_relaxed);
            return phoneme::security::PermissionResponse {
                index == 0
                    ? phoneme::security::PermissionDecision::allowed
                    : phoneme::security::PermissionDecision::denied,
                phoneme::security::PermissionScope::one_shot,
            };
        },
        {
            std::string(
                phoneme::security::permissions::connector_file_read),
        }));
    require(recheck_machine.configure_filesystem(
                recheck_sandbox.string(),
                (runtime_root / "recheck-temporary").string())
                .has_value(),
            "configure stream recheck VM sandbox");
    invoke_int(recheck_machine, "streamPermissionRechecked", 1);
    require(requests->load(std::memory_order_relaxed) == 2,
            "file stream open rechecks permission");
    require(canonical_resource->load(std::memory_order_relaxed),
            "filesystem permission resource is canonical");

    std::filesystem::remove_all(runtime_root, error);
    require(!error, "remove filesystem VM runtime root");
    std::cout << "FileSystemVmTests: PASS\n";
    return 0;
}
