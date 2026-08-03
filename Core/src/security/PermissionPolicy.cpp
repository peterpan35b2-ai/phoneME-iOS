#include "phoneme/security/PermissionPolicy.hpp"

#include "phoneme/security/PermissionCatalog.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace phoneme::security {
namespace {

constexpr std::string_view kPersistenceHeaderV1 =
    "PHONEME-PERMISSIONS-1";
constexpr std::string_view kPersistenceHeaderV2 =
    "PHONEME-PERMISSIONS-2";
constexpr std::string_view kSuitePrefix = "suite\t";
constexpr std::string_view kChecksumPrefix = "checksum\t";
constexpr usize kMaximumPermissionLength = 512U;
constexpr usize kMaximumPersistenceBytes = 1024U * 1024U;
constexpr usize kMaximumPromptResourceLength = 512U;
thread_local const PermissionPolicy* g_active_prompt_policy = nullptr;

class PromptReentryGuard final {
public:
    explicit PromptReentryGuard(const PermissionPolicy* policy) noexcept
        : previous_(g_active_prompt_policy) {
        g_active_prompt_policy = policy;
    }

    ~PromptReentryGuard() { g_active_prompt_policy = previous_; }

    PromptReentryGuard(const PromptReentryGuard&) = delete;
    PromptReentryGuard& operator=(const PromptReentryGuard&) = delete;

private:
    const PermissionPolicy* previous_;
};

[[nodiscard]] bool starts_with(std::string_view value,
                               std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] Status write_all(int descriptor,
                               std::string_view bytes) noexcept {
    usize offset = 0U;
    while (offset < bytes.size()) {
        const usize remaining = bytes.size() - offset;
        const ssize_t written = ::write(descriptor,
                                        bytes.data() + offset,
                                        remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
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

[[nodiscard]] u64 checksum_bytes(std::string_view bytes) noexcept {
    u64 checksum = 14695981039346656037ULL;
    for (const char character : bytes) {
        checksum ^= static_cast<u64>(
            static_cast<unsigned char>(character));
        checksum *= 1099511628211ULL;
    }
    return checksum;
}

[[nodiscard]] std::string checksum_text(std::string_view bytes) {
    constexpr std::array<char, 16U> digits {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    u64 value = checksum_bytes(bytes);
    std::string text(16U, '0');
    for (usize index = 0U; index < text.size(); ++index) {
        const usize shift = (text.size() - 1U - index) * 4U;
        text[index] = digits[static_cast<usize>((value >> shift) & 0xFU)];
    }
    return text;
}

[[nodiscard]] Result<u64> parse_checksum(std::string_view text) {
    if (text.size() != 16U) {
        return fail(ErrorCode::io_error,
                    "permission decision checksum has invalid length");
    }
    u64 value = 0U;
    const auto parsed = std::from_chars(text.data(),
                                        text.data() + text.size(),
                                        value, 16);
    if (parsed.ec != std::errc {} ||
        parsed.ptr != text.data() + text.size()) {
        return fail(ErrorCode::io_error,
                    "permission decision checksum is malformed");
    }
    return value;
}

[[nodiscard]] Status sync_directory(
    const std::filesystem::path& directory) noexcept {
    if (directory.empty()) return {};
    const int descriptor = ::open(directory.c_str(), O_RDONLY);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "failed to open permission decision directory");
    }
    Status result;
    if (::fsync(descriptor) != 0) {
        result = fail(ErrorCode::io_error,
                      "failed to sync permission decision directory");
    }
    if (::close(descriptor) != 0 && result) {
        result = fail(ErrorCode::io_error,
                      "failed to close permission decision directory");
    }
    return result;
}

[[nodiscard]] std::string sanitize_prompt_resource(std::string resource) {
    resource.erase(
        std::remove_if(resource.begin(), resource.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte < 0x20U || byte == 0x7FU;
        }),
        resource.end());

    const usize scheme = resource.find("://");
    if (scheme != std::string::npos) {
        const usize authority_start = scheme + 3U;
        const usize authority_end = resource.find('/', authority_start);
        const usize at = resource.rfind('@', authority_end);
        if (at != std::string::npos && at >= authority_start &&
            (authority_end == std::string::npos || at < authority_end)) {
            resource.erase(authority_start, at + 1U - authority_start);
        }
    }
    const usize secret_suffix = resource.find_first_of("?#");
    if (secret_suffix != std::string::npos) {
        resource.erase(secret_suffix);
    }
    if (resource.size() > kMaximumPromptResourceLength) {
        resource.resize(kMaximumPromptResourceLength);
    }
    return resource;
}

[[nodiscard]] Status append_canonical_permissions(
    const std::vector<std::string>& source,
    std::unordered_set<std::string>& destination) {
    destination.reserve(destination.size() + source.size());
    for (const std::string& permission : source) {
        auto canonical = PermissionPolicy::canonicalize_permission_name(
            permission);
        if (!canonical) return std::unexpected(canonical.error());
        destination.insert(std::move(*canonical));
    }
    return {};
}

[[nodiscard]] Result<PermissionDecision> parse_decision_line(
    std::string_view line,
    std::string& permission) {
    if (line.size() < 3U || line[1] != '\t' ||
        (line[0] != 'A' && line[0] != 'D')) {
        return fail(ErrorCode::io_error,
                    "permission decision file is malformed");
    }
    auto canonical = PermissionPolicy::canonicalize_permission_name(
        line.substr(2U));
    if (!canonical) {
        return fail(ErrorCode::io_error,
                    "permission decision file contains an invalid name");
    }
    permission = std::move(*canonical);
    return line[0] == 'A' ? PermissionDecision::allowed
                          : PermissionDecision::denied;
}

} // namespace

Status PermissionPolicy::configure(PermissionPolicyConfig config) {
    if (!config.suite_id.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "permission policy requires a valid suite ID");
    }

    std::unordered_set<std::string> required_permissions;
    std::unordered_set<std::string> optional_permissions;
    auto required = append_canonical_permissions(
        config.required_permissions, required_permissions);
    if (!required) return required;
    auto optional = append_canonical_permissions(
        config.optional_permissions, optional_permissions);
    if (!optional) return optional;

    // Preserve source compatibility for callers that only know the historical
    // combined list. Explicit required/optional metadata always wins.
    std::unordered_set<std::string> legacy_permissions;
    auto legacy = append_canonical_permissions(
        config.declared_permissions, legacy_permissions);
    if (!legacy) return legacy;
    for (std::string permission : legacy_permissions) {
        if (!required_permissions.contains(permission) &&
            !optional_permissions.contains(permission)) {
            optional_permissions.insert(std::move(permission));
        }
    }
    for (const std::string& permission : required_permissions) {
        optional_permissions.erase(permission);
    }

    std::scoped_lock lock(mutex_);
    cancel_prompt_flights_locked(Error::make(
        ErrorCode::invalid_state,
        "permission policy was reconfigured while prompting"));
    suite_id_ = config.suite_id;
    trust_ = config.trust;
    persistence_path_ = std::move(config.persistence_path);
    required_permissions_ = std::move(required_permissions);
    optional_permissions_ = std::move(optional_permissions);
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
    if (!canonical) return PermissionDecision::denied;

    std::scoped_lock lock(mutex_);
    if (!configured_) return PermissionDecision::unknown;
    if (!is_declared_locked(*canonical)) return PermissionDecision::denied;
    const PermissionDecision stored = check_locked(*canonical);
    if (stored != PermissionDecision::unknown) return stored;
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
    if (!canonical) return std::unexpected(canonical.error());
    if (g_active_prompt_policy == this) {
        return fail(ErrorCode::invalid_state,
                    "recursive permission prompt request was rejected");
    }

    PermissionPromptCallback prompt;
    PermissionRequest prompt_request;
    bool trusted_default_allow = false;
    u64 configuration_generation = 0U;
    std::shared_ptr<PromptFlight> flight;
    {
        std::unique_lock lock(mutex_);
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

        if (const auto existing = active_prompts_.find(*canonical);
            existing != active_prompts_.end()) {
            flight = existing->second;
            flight->ready.wait(lock, [&flight] { return flight->completed; });
            if (flight->error.has_value()) {
                return std::unexpected(*flight->error);
            }
            return flight->response;
        }

        flight = std::make_shared<PromptFlight>();
        active_prompts_.insert_or_assign(*canonical, flight);
        prompt = prompt_;
        trusted_default_allow =
            trust_ == SuiteTrust::trusted && trusted_default_allow_;
        configuration_generation = configuration_generation_;
        prompt_request = PermissionRequest {
            .suite_id = suite_id_,
            .trust = trust_,
            .domain = domain_for_permission(*canonical),
            .permission = *canonical,
            .resource = sanitize_prompt_resource(std::move(resource)),
            .user_initiated = user_initiated,
        };
    }

    const auto finish = [this, &canonical, &flight](
        Result<PermissionResponse> result) -> Result<PermissionResponse> {
        std::scoped_lock lock(mutex_);
        if (!flight->completed) {
            if (result) {
                flight->response = *result;
            } else {
                flight->error = result.error();
            }
            flight->completed = true;
            flight->ready.notify_all();
        } else if (flight->error.has_value()) {
            result = std::unexpected(*flight->error);
        } else {
            result = flight->response;
        }
        const auto existing = active_prompts_.find(*canonical);
        if (existing != active_prompts_.end() &&
            existing->second == flight) {
            active_prompts_.erase(existing);
        }
        return result;
    };

    std::unique_lock<std::mutex> prompt_lock;
    if (!trusted_default_allow && prompt) {
        prompt_lock = std::unique_lock<std::mutex>(prompt_mutex_);
        Result<PermissionResponse> early_result =
            fail(ErrorCode::internal_error,
                 "permission prompt recheck did not produce a result");
        bool has_early_result = false;
        {
            std::scoped_lock lock(mutex_);
            if (!configured_ || suite_id_ != prompt_request.suite_id ||
                configuration_generation_ != configuration_generation) {
                early_result = fail(
                    ErrorCode::invalid_state,
                    "permission policy changed while waiting to prompt");
                has_early_result = true;
            } else if (!is_declared_locked(*canonical)) {
                early_result = PermissionResponse {
                    .decision = PermissionDecision::denied,
                    .scope = PermissionScope::one_shot,
                };
                has_early_result = true;
            } else if (const auto blanket =
                           blanket_decisions_.find(*canonical);
                       blanket != blanket_decisions_.end()) {
                early_result = PermissionResponse {
                    .decision = blanket->second,
                    .scope = PermissionScope::blanket,
                };
                has_early_result = true;
            } else if (const auto session =
                           session_decisions_.find(*canonical);
                       session != session_decisions_.end()) {
                early_result = PermissionResponse {
                    .decision = session->second,
                    .scope = PermissionScope::session,
                };
                has_early_result = true;
            } else {
                prompt = prompt_;
            }
        }
        if (has_early_result) return finish(std::move(early_result));
    }

    PermissionResponse response;
    if (trusted_default_allow) {
        response = PermissionResponse {
            .decision = PermissionDecision::allowed,
            .scope = PermissionScope::session,
        };
    } else if (prompt) {
        PromptReentryGuard prompt_scope(this);
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

    Result<PermissionResponse> final_result = response;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_ || suite_id_ != prompt_request.suite_id ||
            configuration_generation_ != configuration_generation) {
            final_result = fail(
                ErrorCode::invalid_state,
                "permission policy changed while prompting");
        } else if (const auto blanket =
                       blanket_decisions_.find(*canonical);
                   blanket != blanket_decisions_.end()) {
            final_result = PermissionResponse {
                .decision = blanket->second,
                .scope = PermissionScope::blanket,
            };
        } else if (const auto session =
                       session_decisions_.find(*canonical);
                   session != session_decisions_.end()) {
            final_result = PermissionResponse {
                .decision = session->second,
                .scope = PermissionScope::session,
            };
        } else if (response.scope == PermissionScope::session) {
            session_decisions_.insert_or_assign(*canonical,
                                                response.decision);
        } else if (response.scope == PermissionScope::blanket) {
            const auto existing = blanket_decisions_.find(*canonical);
            const bool had_existing =
                existing != blanket_decisions_.end();
            const PermissionDecision old_decision = had_existing
                ? existing->second
                : PermissionDecision::unknown;
            blanket_decisions_.insert_or_assign(*canonical,
                                                response.decision);
            auto saved = save_persistent_locked();
            if (!saved) {
                if (had_existing) {
                    blanket_decisions_.insert_or_assign(*canonical,
                                                        old_decision);
                } else {
                    blanket_decisions_.erase(*canonical);
                }
                final_result = std::unexpected(saved.error());
            }
        }
    }
    return finish(std::move(final_result));
}

Status PermissionPolicy::require(std::string_view permission,
                                 std::string resource,
                                 bool user_initiated) {
    auto response = request(permission, std::move(resource), user_initiated);
    if (!response) return std::unexpected(response.error());
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
    if (!canonical) return std::unexpected(canonical.error());

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
    if (!canonical) return std::unexpected(canonical.error());

    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::invalid_state,
                    "permission policy is not configured");
    }
    const auto existing = blanket_decisions_.find(*canonical);
    if (existing == blanket_decisions_.end()) return {};
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

PermissionDeclaration PermissionPolicy::declaration(
    std::string_view permission) const noexcept {
    auto canonical = canonicalize_permission_name(permission);
    if (!canonical) return PermissionDeclaration::undeclared;
    std::scoped_lock lock(mutex_);
    return declaration_locked(*canonical);
}

Result<std::string> PermissionPolicy::canonicalize_permission_name(
    std::string_view permission) {
    usize first = 0U;
    while (first < permission.size() &&
           (permission[first] == ' ' || permission[first] == '\t' ||
            permission[first] == '\r' || permission[first] == '\n')) {
        ++first;
    }
    usize last = permission.size();
    while (last > first &&
           (permission[last - 1U] == ' ' ||
            permission[last - 1U] == '\t' ||
            permission[last - 1U] == '\r' ||
            permission[last - 1U] == '\n')) {
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
    return PermissionCatalog::domain_for(permission);
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

PermissionDeclaration PermissionPolicy::declaration_locked(
    std::string_view canonical_permission) const noexcept {
    const std::string key(canonical_permission);
    if (required_permissions_.contains(key)) {
        return PermissionDeclaration::required;
    }
    if (optional_permissions_.contains(key)) {
        return PermissionDeclaration::optional;
    }
    return PermissionDeclaration::undeclared;
}

bool PermissionPolicy::is_declared_locked(
    std::string_view canonical_permission) const noexcept {
    if (!enforce_declared_permissions_) return true;
    return declaration_locked(canonical_permission) !=
           PermissionDeclaration::undeclared;
}

Status PermissionPolicy::load_persistent_locked() {
    if (persistence_path_.empty()) return {};

    std::error_code error;
    const bool exists = std::filesystem::exists(persistence_path_, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to inspect permission decision file");
    }
    if (!exists) return {};
    const u64 file_size = std::filesystem::file_size(persistence_path_, error);
    if (error || file_size > kMaximumPersistenceBytes) {
        return fail(ErrorCode::io_error,
                    "permission decision file has an invalid size");
    }

    std::ifstream input(persistence_path_, std::ios::binary);
    if (!input.is_open()) {
        return fail(ErrorCode::io_error,
                    "failed to open permission decision file");
    }
    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        return fail(ErrorCode::io_error,
                    "failed while reading permission decision file");
    }

    std::vector<std::string> lines;
    usize cursor = 0U;
    while (cursor <= contents.size()) {
        const usize end = contents.find('\n', cursor);
        std::string line = end == std::string::npos
            ? contents.substr(cursor)
            : contents.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        cursor = end + 1U;
    }
    if (lines.empty()) {
        return fail(ErrorCode::io_error,
                    "permission decision file is empty");
    }

    std::unordered_map<std::string, PermissionDecision> loaded;
    if (lines.front() == kPersistenceHeaderV1) {
        for (usize index = 1U; index < lines.size(); ++index) {
            if (lines[index].empty()) continue;
            std::string permission;
            auto decision = parse_decision_line(lines[index], permission);
            if (!decision) return std::unexpected(decision.error());
            loaded.insert_or_assign(std::move(permission), *decision);
        }
        blanket_decisions_ = std::move(loaded);
        // V1 had no suite binding or integrity checksum. Rewrite it immediately
        // after a successful parse so subsequent starts use the hardened format.
        return save_persistent_locked();
    }
    if (lines.front() != kPersistenceHeaderV2) {
        return fail(ErrorCode::io_error,
                    "permission decision file has an invalid header");
    }
    if (lines.size() < 3U ||
        !starts_with(lines[1], kSuitePrefix)) {
        return fail(ErrorCode::io_error,
                    "permission decision file has no suite binding");
    }

    i32 persisted_suite = 0;
    const std::string_view suite_text(lines[1].data() + kSuitePrefix.size(),
                                      lines[1].size() - kSuitePrefix.size());
    const auto suite_parsed = std::from_chars(
        suite_text.data(), suite_text.data() + suite_text.size(),
        persisted_suite);
    if (suite_parsed.ec != std::errc {} ||
        suite_parsed.ptr != suite_text.data() + suite_text.size() ||
        persisted_suite != suite_id_.value) {
        return fail(ErrorCode::io_error,
                    "permission decision file belongs to another suite");
    }

    usize checksum_index = lines.size();
    while (checksum_index > 0U && lines[checksum_index - 1U].empty()) {
        --checksum_index;
    }
    if (checksum_index <= 2U ||
        !starts_with(lines[checksum_index - 1U], kChecksumPrefix)) {
        return fail(ErrorCode::io_error,
                    "permission decision file has no checksum");
    }
    --checksum_index;

    std::string payload;
    for (usize index = 1U; index < checksum_index; ++index) {
        payload.append(lines[index]);
        payload.push_back('\n');
    }
    auto expected = parse_checksum(
        std::string_view(lines[checksum_index]).substr(
            kChecksumPrefix.size()));
    if (!expected) return std::unexpected(expected.error());
    if (*expected != checksum_bytes(payload)) {
        return fail(ErrorCode::checksum_mismatch,
                    "permission decision file checksum does not match");
    }

    for (usize index = 2U; index < checksum_index; ++index) {
        if (lines[index].empty()) continue;
        std::string permission;
        auto decision = parse_decision_line(lines[index], permission);
        if (!decision) return std::unexpected(decision.error());
        loaded.insert_or_assign(std::move(permission), *decision);
    }
    blanket_decisions_ = std::move(loaded);
    return {};
}

Status PermissionPolicy::save_persistent_locked() {
    if (persistence_path_.empty()) return {};

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
    for (const auto& entry : blanket_decisions_) decisions.push_back(entry);
    std::sort(decisions.begin(), decisions.end(),
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });

    std::string payload(kSuitePrefix);
    payload.append(std::to_string(suite_id_.value));
    payload.push_back('\n');
    for (const auto& [permission, decision] : decisions) {
        payload.push_back(decision == PermissionDecision::allowed ? 'A' : 'D');
        payload.push_back('\t');
        payload.append(permission);
        payload.push_back('\n');
    }

    std::string contents(kPersistenceHeaderV2);
    contents.push_back('\n');
    contents.append(payload);
    contents.append(kChecksumPrefix);
    contents.append(checksum_text(payload));
    contents.push_back('\n');

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
    auto directory_synced = sync_directory(parent);
    if (!directory_synced) return directory_synced;
    return {};
}

void PermissionPolicy::cancel_prompt_flights_locked(Error error) noexcept {
    for (auto& [permission, flight] : active_prompts_) {
        (void)permission;
        if (flight == nullptr || flight->completed) continue;
        flight->error = error;
        flight->completed = true;
        flight->ready.notify_all();
    }
    active_prompts_.clear();
}

} // namespace phoneme::security
