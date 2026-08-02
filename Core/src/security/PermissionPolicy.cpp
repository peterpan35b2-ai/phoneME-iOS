#include "phoneme/security/PermissionPolicy.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace phoneme::security {
namespace {

constexpr std::string_view kPersistenceHeader =
    "PHONEME-PERMISSIONS-1";
constexpr usize kMaximumPermissionLength = 512U;

[[nodiscard]] bool starts_with(std::string_view value,
                               std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] Status write_all(int descriptor,
                               std::string_view bytes) noexcept {
    usize offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const ssize_t written = ::write(descriptor,
                                        bytes.data() + offset,
                                        remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail(ErrorCode::io_error,
                        "failed to write permission decisions");
        }
        if (written == 0) {
            return fail(ErrorCode::io_error,
                        "permission decision write made no progress");
        }
        offset += static_cast<usize>(written);
    }
    return {};
}

} // namespace

Status PermissionPolicy::configure(PermissionPolicyConfig config) {
    if (!config.suite_id.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "permission policy requires a valid suite ID");
    }

    std::unordered_set<std::string> declared_permissions;
    declared_permissions.reserve(config.declared_permissions.size());
    for (const std::string& permission : config.declared_permissions) {
        auto canonical = canonicalize_permission_name(permission);
        if (!canonical) {
            return std::unexpected(canonical.error());
        }
        declared_permissions.insert(std::move(*canonical));
    }

    std::scoped_lock lock(mutex_);
    suite_id_ = config.suite_id;
    trust_ = config.trust;
    persistence_path_ = std::move(config.persistence_path);
    declared_permissions_ = std::move(declared_permissions);
    session_decisions_.clear();
    blanket_decisions_.clear();
    prompt_ = std::move(config.prompt);
    enforce_declared_permissions_ = config.enforce_declared_permissions;
    trusted_default_allow_ = config.trusted_default_allow;
    configured_ = true;
    ++configuration_generation_;

    auto loaded = load_persistent_locked();
    if (!loaded) {
        configured_ = false;
        blanket_decisions_.clear();
        return loaded;
    }
    return {};
}

PermissionDecision PermissionPolicy::check(
    std::string_view permission) const noexcept {
    auto canonical = canonicalize_permission_name(permission);
    if (!canonical) {
        return PermissionDecision::denied;
    }

    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return PermissionDecision::unknown;
    }
    if (!is_declared_locked(*canonical)) {
        return PermissionDecision::denied;
    }
    const PermissionDecision stored = check_locked(*canonical);
    if (stored != PermissionDecision::unknown) {
        return stored;
    }
    if (trust_ == SuiteTrust::trusted && trusted_default_allow_) {
        return PermissionDecision::allowed;
    }
    return PermissionDecision::unknown;
}

Result<PermissionResponse> PermissionPolicy::request(
    std::string_view permission,
    std::string resource,
    bool user_initiated) {
    auto canonical = canonicalize_permission_name(permission);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }

    PermissionPromptCallback prompt;
    PermissionRequest prompt_request;
    bool trusted_default_allow = false;
    u64 configuration_generation = 0;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::invalid_state,
                        "permission policy is not configured");
        }
        if (!is_declared_locked(*canonical)) {
            return PermissionResponse {
                .decision = PermissionDecision::denied,
                .scope = PermissionScope::one_shot,
            };
        }

        if (const auto blanket = blanket_decisions_.find(*canonical);
            blanket != blanket_decisions_.end()) {
            return PermissionResponse {
                .decision = blanket->second,
                .scope = PermissionScope::blanket,
            };
        }
        if (const auto session = session_decisions_.find(*canonical);
            session != session_decisions_.end()) {
            return PermissionResponse {
                .decision = session->second,
                .scope = PermissionScope::session,
            };
        }

        prompt = prompt_;
        trusted_default_allow =
            trust_ == SuiteTrust::trusted && trusted_default_allow_;
        configuration_generation = configuration_generation_;
        prompt_request = PermissionRequest {
            .suite_id = suite_id_,
            .trust = trust_,
            .domain = domain_for_permission(*canonical),
            .permission = *canonical,
            .resource = std::move(resource),
            .user_initiated = user_initiated,
        };
    }

    std::unique_lock<std::mutex> prompt_lock;
    if (!trusted_default_allow && prompt) {
        prompt_lock = std::unique_lock<std::mutex>(prompt_mutex_);
        std::scoped_lock lock(mutex_);
        if (!configured_ || suite_id_ != prompt_request.suite_id ||
            configuration_generation_ != configuration_generation) {
            return fail(ErrorCode::invalid_state,
                        "permission policy changed while waiting to prompt");
        }
        if (!is_declared_locked(*canonical)) {
            return PermissionResponse {
                .decision = PermissionDecision::denied,
                .scope = PermissionScope::one_shot,
            };
        }
        if (const auto blanket = blanket_decisions_.find(*canonical);
            blanket != blanket_decisions_.end()) {
            return PermissionResponse {
                .decision = blanket->second,
                .scope = PermissionScope::blanket,
            };
        }
        if (const auto session = session_decisions_.find(*canonical);
            session != session_decisions_.end()) {
            return PermissionResponse {
                .decision = session->second,
                .scope = PermissionScope::session,
            };
        }
        prompt = prompt_;
    }

    PermissionResponse response;
    if (trusted_default_allow) {
        response = PermissionResponse {
            .decision = PermissionDecision::allowed,
            .scope = PermissionScope::session,
        };
    } else if (prompt) {
        response = prompt(prompt_request);
    } else {
        response = PermissionResponse {
            .decision = PermissionDecision::denied,
            .scope = PermissionScope::one_shot,
        };
    }

    if (response.decision == PermissionDecision::unknown) {
        response.decision = PermissionDecision::denied;
        response.scope = PermissionScope::one_shot;
    }

    std::scoped_lock lock(mutex_);
    if (!configured_ || suite_id_ != prompt_request.suite_id ||
        configuration_generation_ != configuration_generation) {
        return fail(ErrorCode::invalid_state,
                    "permission policy changed while prompting");
    }
    if (const auto blanket = blanket_decisions_.find(*canonical);
        blanket != blanket_decisions_.end()) {
        return PermissionResponse {
            .decision = blanket->second,
            .scope = PermissionScope::blanket,
        };
    }
    if (const auto session = session_decisions_.find(*canonical);
        session != session_decisions_.end()) {
        return PermissionResponse {
            .decision = session->second,
            .scope = PermissionScope::session,
        };
    }
    if (response.scope == PermissionScope::session) {
        session_decisions_.insert_or_assign(*canonical, response.decision);
    } else if (response.scope == PermissionScope::blanket) {
        const auto existing = blanket_decisions_.find(*canonical);
        const bool had_existing = existing != blanket_decisions_.end();
        const PermissionDecision old_decision = had_existing
            ? existing->second
            : PermissionDecision::unknown;
        blanket_decisions_.insert_or_assign(*canonical, response.decision);
        auto saved = save_persistent_locked();
        if (!saved) {
            if (had_existing) {
                blanket_decisions_.insert_or_assign(*canonical, old_decision);
            } else {
                blanket_decisions_.erase(*canonical);
            }
            return std::unexpected(saved.error());
        }
    }
    return response;
}

Status PermissionPolicy::require(std::string_view permission,
                                 std::string resource,
                                 bool user_initiated) {
    auto response = request(permission, std::move(resource), user_initiated);
    if (!response) {
        return std::unexpected(response.error());
    }
    if (response->decision != PermissionDecision::allowed) {
        return fail_java("java/lang/SecurityException",
                         "permission denied: " + std::string(permission));
    }
    return {};
}

Status PermissionPolicy::set_blanket_decision(
    std::string_view permission,
    PermissionDecision decision) {
    if (decision == PermissionDecision::unknown) {
        return fail(ErrorCode::invalid_argument,
                    "blanket permission decision cannot be unknown");
    }
    auto canonical = canonicalize_permission_name(permission);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }

    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::invalid_state,
                    "permission policy is not configured");
    }
    if (!is_declared_locked(*canonical)) {
        return fail(ErrorCode::invalid_argument,
                    "permission is not declared by the suite");
    }

    const auto existing = blanket_decisions_.find(*canonical);
    const bool had_existing = existing != blanket_decisions_.end();
    const PermissionDecision old_decision = had_existing
        ? existing->second
        : PermissionDecision::unknown;
    blanket_decisions_.insert_or_assign(*canonical, decision);
    auto saved = save_persistent_locked();
    if (!saved) {
        if (had_existing) {
            blanket_decisions_.insert_or_assign(*canonical, old_decision);
        } else {
            blanket_decisions_.erase(*canonical);
        }
        return saved;
    }
    return {};
}

Status PermissionPolicy::clear_blanket_decision(
    std::string_view permission) {
    auto canonical = canonicalize_permission_name(permission);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }

    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::invalid_state,
                    "permission policy is not configured");
    }
    const auto existing = blanket_decisions_.find(*canonical);
    if (existing == blanket_decisions_.end()) {
        return {};
    }
    const PermissionDecision old_decision = existing->second;
    blanket_decisions_.erase(existing);
    auto saved = save_persistent_locked();
    if (!saved) {
        blanket_decisions_.insert_or_assign(*canonical, old_decision);
        return saved;
    }
    return {};
}

Status PermissionPolicy::clear_all_blanket_decisions() {
    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::invalid_state,
                    "permission policy is not configured");
    }
    auto old_decisions = blanket_decisions_;
    blanket_decisions_.clear();
    auto saved = save_persistent_locked();
    if (!saved) {
        blanket_decisions_ = std::move(old_decisions);
        return saved;
    }
    return {};
}

void PermissionPolicy::reset_session() noexcept {
    std::scoped_lock lock(mutex_);
    session_decisions_.clear();
}

SuiteId PermissionPolicy::suite_id() const noexcept {
    std::scoped_lock lock(mutex_);
    return suite_id_;
}

SuiteTrust PermissionPolicy::trust() const noexcept {
    std::scoped_lock lock(mutex_);
    return trust_;
}

Result<std::string> PermissionPolicy::canonicalize_permission_name(
    std::string_view permission) {
    usize first = 0;
    while (first < permission.size() &&
           (permission[first] == ' ' || permission[first] == '\t' ||
            permission[first] == '\r' || permission[first] == '\n')) {
        ++first;
    }
    usize last = permission.size();
    while (last > first &&
           (permission[last - 1U] == ' ' || permission[last - 1U] == '\t' ||
            permission[last - 1U] == '\r' || permission[last - 1U] == '\n')) {
        --last;
    }
    if (first == last) {
        return fail(ErrorCode::invalid_argument,
                    "permission name is empty");
    }
    const std::string_view trimmed = permission.substr(first, last - first);
    if (trimmed.size() > kMaximumPermissionLength) {
        return fail(ErrorCode::invalid_argument,
                    "permission name is too long");
    }
    for (const char character : trimmed) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20U || byte >= 0x7FU) {
            return fail(ErrorCode::invalid_argument,
                        "permission name contains invalid characters");
        }
    }
    return std::string(trimmed);
}

PermissionDomain PermissionPolicy::domain_for_permission(
    std::string_view permission) noexcept {
    if (starts_with(permission,
                    "javax.microedition.io.Connector.file.")) {
        return PermissionDomain::filesystem;
    }
    if (starts_with(permission, "javax.microedition.media.") ||
        starts_with(permission, "javax.microedition.amms.")) {
        return PermissionDomain::media;
    }
    if (starts_with(permission, "javax.microedition.io.Connector.") ||
        starts_with(permission, "javax.wireless.messaging.") ||
        starts_with(permission, "javax.microedition.sip.")) {
        return PermissionDomain::network;
    }
    return PermissionDomain::unknown;
}

PermissionDecision PermissionPolicy::check_locked(
    std::string_view canonical_permission) const noexcept {
    if (const auto blanket = blanket_decisions_.find(
            std::string(canonical_permission));
        blanket != blanket_decisions_.end()) {
        return blanket->second;
    }
    if (const auto session = session_decisions_.find(
            std::string(canonical_permission));
        session != session_decisions_.end()) {
        return session->second;
    }
    return PermissionDecision::unknown;
}

bool PermissionPolicy::is_declared_locked(
    std::string_view canonical_permission) const noexcept {
    if (!enforce_declared_permissions_) {
        return true;
    }
    return declared_permissions_.contains(std::string(canonical_permission));
}

Status PermissionPolicy::load_persistent_locked() {
    if (persistence_path_.empty()) {
        return {};
    }

    std::error_code error;
    const bool exists = std::filesystem::exists(persistence_path_, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to inspect permission decision file");
    }
    if (!exists) {
        return {};
    }

    std::ifstream input(persistence_path_, std::ios::binary);
    if (!input.is_open()) {
        return fail(ErrorCode::io_error,
                    "failed to open permission decision file");
    }
    std::string line;
    if (!std::getline(input, line) || line != kPersistenceHeader) {
        return fail(ErrorCode::io_error,
                    "permission decision file has an invalid header");
    }

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line.size() < 3U || line[1] != '\t' ||
            (line[0] != 'A' && line[0] != 'D')) {
            return fail(ErrorCode::io_error,
                        "permission decision file is malformed");
        }
        auto canonical = canonicalize_permission_name(
            std::string_view(line).substr(2));
        if (!canonical) {
            return fail(ErrorCode::io_error,
                        "permission decision file contains an invalid name");
        }
        blanket_decisions_.insert_or_assign(
            std::move(*canonical),
            line[0] == 'A' ? PermissionDecision::allowed
                           : PermissionDecision::denied);
    }
    if (!input.eof()) {
        return fail(ErrorCode::io_error,
                    "failed while reading permission decision file");
    }
    return {};
}

Status PermissionPolicy::save_persistent_locked() {
    if (persistence_path_.empty()) {
        return {};
    }

    const std::filesystem::path destination(persistence_path_);
    const std::filesystem::path parent = destination.parent_path();
    std::error_code error;
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return fail(ErrorCode::io_error,
                        "failed to create permission decision directory");
        }
    }

    std::vector<std::pair<std::string, PermissionDecision>> decisions;
    decisions.reserve(blanket_decisions_.size());
    for (const auto& entry : blanket_decisions_) {
        decisions.push_back(entry);
    }
    std::sort(decisions.begin(), decisions.end(),
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });

    std::string contents(kPersistenceHeader);
    contents.push_back('\n');
    for (const auto& [permission, decision] : decisions) {
        contents.push_back(decision == PermissionDecision::allowed ? 'A' : 'D');
        contents.push_back('\t');
        contents.append(permission);
        contents.push_back('\n');
    }

    const std::string temporary_path = persistence_path_ + ".tmp";
    const int descriptor = ::open(temporary_path.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC,
                                  0600);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "failed to create temporary permission decision file");
    }

    auto written = write_all(descriptor, contents);
    if (written && ::fsync(descriptor) != 0) {
        written = fail(ErrorCode::io_error,
                       "failed to sync permission decision file");
    }
    if (::close(descriptor) != 0 && written) {
        written = fail(ErrorCode::io_error,
                       "failed to close permission decision file");
    }
    if (!written) {
        ::unlink(temporary_path.c_str());
        return written;
    }
    if (::rename(temporary_path.c_str(), persistence_path_.c_str()) != 0) {
        ::unlink(temporary_path.c_str());
        return fail(ErrorCode::io_error,
                    "failed to replace permission decision file");
    }
    return {};
}

} // namespace phoneme::security
