#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::security {

namespace permissions {
inline constexpr std::string_view connector_http =
    "javax.microedition.io.Connector.http";
inline constexpr std::string_view connector_https =
    "javax.microedition.io.Connector.https";
inline constexpr std::string_view connector_ssl =
    "javax.microedition.io.Connector.ssl";
inline constexpr std::string_view connector_comm =
    "javax.microedition.io.Connector.comm";
inline constexpr std::string_view connector_socket =
    "javax.microedition.io.Connector.socket";
inline constexpr std::string_view connector_server_socket =
    "javax.microedition.io.Connector.serversocket";
inline constexpr std::string_view connector_datagram =
    "javax.microedition.io.Connector.datagram";
inline constexpr std::string_view connector_datagram_receiver =
    "javax.microedition.io.Connector.datagramreceiver";
inline constexpr std::string_view connector_file_read =
    "javax.microedition.io.Connector.file.read";
inline constexpr std::string_view connector_file_write =
    "javax.microedition.io.Connector.file.write";
inline constexpr std::string_view wireless_sms_send =
    "javax.wireless.messaging.sms.send";
inline constexpr std::string_view wireless_sms_receive =
    "javax.wireless.messaging.sms.receive";
inline constexpr std::string_view wireless_mms_send =
    "javax.wireless.messaging.mms.send";
inline constexpr std::string_view wireless_mms_receive =
    "javax.wireless.messaging.mms.receive";
inline constexpr std::string_view wireless_cbs_receive =
    "javax.wireless.messaging.cbs.receive";
inline constexpr std::string_view media_record =
    "javax.microedition.media.control.RecordControl";
inline constexpr std::string_view media_snapshot =
    "javax.microedition.media.control.VideoControl.getSnapshot";
inline constexpr std::string_view media_capture_audio =
    "javax.microedition.media.capture.audio";
inline constexpr std::string_view media_capture_video =
    "javax.microedition.media.capture.video";
inline constexpr std::string_view media_capture_image =
    "javax.microedition.media.capture.image";
inline constexpr std::string_view push_registry =
    "javax.microedition.io.PushRegistry";
inline constexpr std::string_view platform_request =
    "javax.microedition.midlet.platformRequest";
} // namespace permissions

enum class PermissionDomain : u8 {
    unknown = 0,
    network = 1,
    filesystem = 2,
    media = 3,
    push = 4,
    platform = 5,
};

enum class SuiteTrust : u8 {
    untrusted = 0,
    trusted = 1,
};

enum class PermissionDecision : i8 {
    unknown = -1,
    denied = 0,
    allowed = 1,
};

enum class PermissionScope : u8 {
    one_shot = 0,
    session = 1,
    blanket = 2,
};

enum class PermissionDeclaration : u8 {
    undeclared = 0,
    required = 1,
    optional = 2,
};

struct PermissionRequest final {
    SuiteId suite_id;
    SuiteTrust trust {SuiteTrust::untrusted};
    PermissionDomain domain {PermissionDomain::unknown};
    std::string permission;
    std::string resource;
    bool user_initiated {false};
};

struct PermissionResponse final {
    PermissionDecision decision {PermissionDecision::denied};
    PermissionScope scope {PermissionScope::one_shot};
};

using PermissionPromptCallback =
    std::function<PermissionResponse(const PermissionRequest&)>;

struct PermissionPolicyConfig final {
    SuiteId suite_id;
    SuiteTrust trust {SuiteTrust::untrusted};
    std::string persistence_path;
    // Legacy combined declaration list. New callers should populate the
    // required/optional lists so policy diagnostics preserve manifest intent.
    std::vector<std::string> declared_permissions;
    std::vector<std::string> required_permissions;
    std::vector<std::string> optional_permissions;
    bool enforce_declared_permissions {false};
    bool trusted_default_allow {true};
    PermissionPromptCallback prompt;
};

class PermissionPolicy final {
public:
    PermissionPolicy() = default;

    PermissionPolicy(const PermissionPolicy&) = delete;
    PermissionPolicy& operator=(const PermissionPolicy&) = delete;

    [[nodiscard]] Status configure(PermissionPolicyConfig config);

    // Non-interactive MIDlet.checkPermission semantics.
    [[nodiscard]] PermissionDecision check(
        std::string_view permission) const noexcept;
    // Interactive gate used by protected native operations.
    [[nodiscard]] Result<PermissionResponse> request(
        std::string_view permission,
        std::string resource = {},
        bool user_initiated = false);
    // Returns a Java SecurityException error when access is denied.
    [[nodiscard]] Status require(
        std::string_view permission,
        std::string resource = {},
        bool user_initiated = false);

    [[nodiscard]] Status set_blanket_decision(
        std::string_view permission,
        PermissionDecision decision);
    [[nodiscard]] Status clear_blanket_decision(
        std::string_view permission);
    [[nodiscard]] Status clear_all_blanket_decisions();
    void reset_session() noexcept;

    [[nodiscard]] SuiteId suite_id() const noexcept;
    [[nodiscard]] SuiteTrust trust() const noexcept;
    [[nodiscard]] PermissionDeclaration declaration(
        std::string_view permission) const noexcept;

    [[nodiscard]] static Result<std::string> canonicalize_permission_name(
        std::string_view permission);
    [[nodiscard]] static PermissionDomain domain_for_permission(
        std::string_view permission) noexcept;

private:
    [[nodiscard]] PermissionDecision check_locked(
        std::string_view canonical_permission) const noexcept;
    [[nodiscard]] PermissionDeclaration declaration_locked(
        std::string_view canonical_permission) const noexcept;
    [[nodiscard]] bool is_declared_locked(
        std::string_view canonical_permission) const noexcept;
    [[nodiscard]] Status load_persistent_locked();
    [[nodiscard]] Status save_persistent_locked();
    void cancel_prompt_flights_locked(Error error) noexcept;

    struct PromptFlight final {
        std::condition_variable ready;
        bool completed {false};
        PermissionResponse response {};
        std::optional<Error> error;
    };

    mutable std::mutex mutex_;
    std::mutex prompt_mutex_;
    SuiteId suite_id_ {};
    SuiteTrust trust_ {SuiteTrust::untrusted};
    std::string persistence_path_;
    std::unordered_set<std::string> required_permissions_;
    std::unordered_set<std::string> optional_permissions_;
    std::unordered_map<std::string, PermissionDecision> session_decisions_;
    std::unordered_map<std::string, PermissionDecision> blanket_decisions_;
    std::unordered_map<std::string, std::shared_ptr<PromptFlight>>
        active_prompts_;
    PermissionPromptCallback prompt_;
    bool enforce_declared_permissions_ {false};
    bool trusted_default_allow_ {true};
    bool configured_ {false};
    u64 configuration_generation_ {0};
};

using SharedPermissionPolicy = std::shared_ptr<PermissionPolicy>;

} // namespace phoneme::security
