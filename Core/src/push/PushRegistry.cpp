#include "phoneme/push/PushRegistry.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <utility>

namespace phoneme::push {
namespace {

constexpr std::array<u8, 8> kMagic{
    static_cast<u8>('P'), static_cast<u8>('M'), static_cast<u8>('P'),
    static_cast<u8>('U'), static_cast<u8>('S'), static_cast<u8>('H'),
    static_cast<u8>('0'), static_cast<u8>('1'),
};
constexpr u32 kVersion = 2U;
constexpr u32 kLegacyVersion = 1U;
constexpr usize kMaximumFileBytes = 16U * 1024U * 1024U;
constexpr u32 kMaximumEntries = 4'096U;
constexpr u32 kMaximumStringBytes = 4'096U;
constexpr i64 kLaunchLifetimeMillis = 24LL * 60LL * 60LL * 1'000LL;
constexpr i64 kMaximumRetryDelayMillis = 5LL * 60LL * 1'000LL;
constexpr u32 kMaximumLaunchAttempts = 8U;

[[nodiscard]] std::mutex &persistence_mutex() {
  static std::mutex mutex;
  return mutex;
}

void append_u8(std::vector<u8> &output, u8 value) { output.push_back(value); }

void append_u32(std::vector<u8> &output, u32 value) {
  for (u32 shift = 0; shift < 32U; shift += 8U) {
    output.push_back(static_cast<u8>((value >> shift) & 0xFFU));
  }
}

void append_i32(std::vector<u8> &output, i32 value) {
  append_u32(output, static_cast<u32>(value));
}

void append_u64(std::vector<u8> &output, u64 value) {
  for (u32 shift = 0; shift < 64U; shift += 8U) {
    output.push_back(static_cast<u8>((value >> shift) & 0xFFU));
  }
}

void append_i64(std::vector<u8> &output, i64 value) {
  append_u64(output, static_cast<u64>(value));
}

void append_string(std::vector<u8> &output, std::string_view value) {
  append_u32(output, static_cast<u32>(value.size()));
  output.insert(output.end(), value.begin(), value.end());
}

[[nodiscard]] u64 checksum(std::span<const u8> bytes) noexcept {
  u64 hash = 1469598103934665603ULL;
  for (const u8 byte : bytes) {
    hash ^= static_cast<u64>(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}

class Reader final {
public:
  explicit Reader(std::span<const u8> bytes) : bytes_(bytes) {}

  [[nodiscard]] Result<u8> read_u8() {
    if (offset_ >= bytes_.size()) {
      return fail(ErrorCode::io_error, "push registry file is truncated");
    }
    return bytes_[offset_++];
  }

  [[nodiscard]] Result<u32> read_u32() {
    if (bytes_.size() - offset_ < 4U) {
      return fail(ErrorCode::io_error, "push registry file is truncated");
    }
    u32 value = 0;
    for (u32 shift = 0; shift < 32U; shift += 8U) {
      value |= static_cast<u32>(bytes_[offset_++]) << shift;
    }
    return value;
  }

  [[nodiscard]] Result<i32> read_i32() {
    auto value = read_u32();
    if (!value)
      return std::unexpected(value.error());
    return static_cast<i32>(*value);
  }

  [[nodiscard]] Result<u64> read_u64() {
    if (bytes_.size() - offset_ < 8U) {
      return fail(ErrorCode::io_error, "push registry file is truncated");
    }
    u64 value = 0;
    for (u32 shift = 0; shift < 64U; shift += 8U) {
      value |= static_cast<u64>(bytes_[offset_++]) << shift;
    }
    return value;
  }

  [[nodiscard]] Result<i64> read_i64() {
    auto value = read_u64();
    if (!value)
      return std::unexpected(value.error());
    return static_cast<i64>(*value);
  }

  [[nodiscard]] Result<std::string> read_string() {
    auto length = read_u32();
    if (!length)
      return std::unexpected(length.error());
    if (*length > kMaximumStringBytes ||
        static_cast<usize>(*length) > bytes_.size() - offset_) {
      return fail(ErrorCode::io_error,
                  "push registry string length is invalid");
    }
    const usize count = static_cast<usize>(*length);
    std::string value(reinterpret_cast<const char *>(bytes_.data() + offset_),
                      count);
    offset_ += count;
    return value;
  }

  [[nodiscard]] usize offset() const noexcept { return offset_; }

private:
  std::span<const u8> bytes_;
  usize offset_{0};
};

[[nodiscard]] Result<std::vector<u8>> read_file(const std::string &path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return fail(ErrorCode::io_error,
                "failed to stat push registry: " + error.message());
  }
  if (size > kMaximumFileBytes ||
      size > static_cast<std::uintmax_t>(
                 std::numeric_limits<std::streamsize>::max())) {
    return fail(ErrorCode::overflow, "push registry file is too large");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return fail(ErrorCode::io_error,
                "failed to open push registry for reading");
  }
  std::vector<u8> bytes(static_cast<usize>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  if (!input || input.peek() != std::ifstream::traits_type::eof()) {
    return fail(ErrorCode::io_error,
                "failed to read complete push registry file");
  }
  return bytes;
}

[[nodiscard]] Status write_atomic(const std::string &path,
                                  std::span<const u8> bytes) {
  const std::string temporary = path + ".tmp";
  const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    return fail(ErrorCode::io_error,
                "failed to open push registry temporary file: " +
                    std::string(std::strerror(errno)));
  }

  usize written = 0;
  while (written < bytes.size()) {
    const usize remaining = bytes.size() - written;
    const usize chunk = std::min(
        remaining, static_cast<usize>(std::numeric_limits<ssize_t>::max()));
    const ssize_t result = ::write(descriptor, bytes.data() + written, chunk);
    if (result < 0 && errno == EINTR)
      continue;
    if (result <= 0) {
      const std::string message = std::strerror(errno);
      (void)::close(descriptor);
      (void)::unlink(temporary.c_str());
      return fail(ErrorCode::io_error,
                  "failed to write push registry: " + message);
    }
    written += static_cast<usize>(result);
  }

  if (::fsync(descriptor) != 0) {
    const std::string message = std::strerror(errno);
    (void)::close(descriptor);
    (void)::unlink(temporary.c_str());
    return fail(ErrorCode::io_error,
                "failed to sync push registry: " + message);
  }
  if (::close(descriptor) != 0) {
    const std::string message = std::strerror(errno);
    (void)::unlink(temporary.c_str());
    return fail(ErrorCode::io_error,
                "failed to close push registry: " + message);
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    const std::string message = std::strerror(errno);
    (void)::unlink(temporary.c_str());
    return fail(ErrorCode::io_error,
                "failed to replace push registry: " + message);
  }
  return {};
}

[[nodiscard]] bool contains_nul(std::string_view value) noexcept {
  return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool uses_ip_filter(std::string_view connection) noexcept {
  return connection.starts_with("socket://") ||
         connection.starts_with("serversocket://") ||
         connection.starts_with("datagram://");
}

[[nodiscard]] bool uses_text_filter(std::string_view connection) noexcept {
  return connection.starts_with("sms://") ||
         connection.starts_with("mms://") ||
         connection.starts_with("cbs://");
}

[[nodiscard]] bool wildcard_matches(std::string_view pattern,
                                    std::string_view value) noexcept {
  std::vector<bool> current(value.size() + 1U, false);
  std::vector<bool> next(value.size() + 1U, false);
  current[0] = true;
  for (const char token : pattern) {
    std::fill(next.begin(), next.end(), false);
    if (token == '*') {
      next[0] = current[0];
      for (usize index = 1U; index <= value.size(); ++index) {
        next[index] = current[index] || next[index - 1U];
      }
    } else {
      for (usize index = 1U; index <= value.size(); ++index) {
        next[index] = current[index - 1U] &&
            (token == '?' || token == value[index - 1U]);
      }
    }
    current.swap(next);
  }
  return current[value.size()];
}

[[nodiscard]] Result<u16> parse_local_port(std::string_view connection,
                                           std::string_view prefix) {
  if (!connection.starts_with(prefix)) {
    return fail(ErrorCode::invalid_argument,
                "push connection scheme is invalid");
  }
  const std::string_view authority = connection.substr(prefix.size());
  if (authority.size() < 2U || authority.front() != ':' ||
      authority.find_first_of("/?#", 1U) != std::string_view::npos) {
    return fail(ErrorCode::invalid_argument,
                "push listener connection must use :port with no path");
  }
  u32 port = 0U;
  const auto parsed = std::from_chars(authority.data() + 1U,
                                      authority.data() + authority.size(),
                                      port);
  if (parsed.ec != std::errc {} ||
      parsed.ptr != authority.data() + authority.size() ||
      port == 0U || port > std::numeric_limits<u16>::max()) {
    return fail(ErrorCode::invalid_argument,
                "push listener port is invalid");
  }
  return static_cast<u16>(port);
}

[[nodiscard]] bool split_ipv4(std::string_view value,
                              bool allow_wildcards,
                              std::array<std::string_view, 4>& parts) noexcept {
  usize start = 0U;
  for (usize index = 0U; index < parts.size(); ++index) {
    const usize end = value.find('.', start);
    if ((index + 1U < parts.size() && end == std::string_view::npos) ||
        (index + 1U == parts.size() && end != std::string_view::npos)) {
      return false;
    }
    const usize limit = end == std::string_view::npos ? value.size() : end;
    if (limit == start) return false;
    parts[index] = value.substr(start, limit - start);
    bool has_wildcard = false;
    for (char character : parts[index]) {
      if (character == '*' || character == '?') {
        if (!allow_wildcards) return false;
        has_wildcard = true;
      } else if (character < '0' || character > '9') {
        return false;
      }
    }
    if (!has_wildcard) {
      unsigned value_number = 0U;
      const auto parsed = std::from_chars(parts[index].data(),
                                          parts[index].data() +
                                              parts[index].size(),
                                          value_number);
      if (parsed.ec != std::errc {} ||
          parsed.ptr != parts[index].data() + parts[index].size() ||
          value_number > 255U) {
        return false;
      }
    }
    start = limit + 1U;
  }
  return true;
}

[[nodiscard]] bool wildcard_segment_matches(std::string_view pattern,
                                            std::string_view value) noexcept {
  std::array<bool, 4> current {};
  std::array<bool, 4> next {};
  if (value.size() >= current.size()) return false;
  current[0] = true;
  for (char token : pattern) {
    next.fill(false);
    if (token == '*') {
      next[0] = current[0];
      for (usize index = 1U; index <= value.size(); ++index) {
        next[index] = current[index] || next[index - 1U];
      }
    } else {
      for (usize index = 1U; index <= value.size(); ++index) {
        next[index] = current[index - 1U] &&
            (token == '?' || token == value[index - 1U]);
      }
    }
    current = next;
  }
  return current[value.size()];
}

} // namespace

Status PushRegistry::configure(std::string root_directory, SuiteId suite_id) {
  if (root_directory.empty() || !suite_id.valid()) {
    return fail(ErrorCode::invalid_argument,
                "push registry requires a root and valid suite ID");
  }

  std::error_code error;
  std::filesystem::create_directories(root_directory, error);
  if (error) {
    return fail(ErrorCode::io_error,
                "failed to create push registry directory: " + error.message());
  }

  std::scoped_lock lock(mutex_, persistence_mutex());
  root_directory_ = std::move(root_directory);
  suite_id_ = suite_id;
  configured_ = true;
  auto loaded = reload_unlocked();
  if (!loaded) {
    configured_ = false;
    state_ = State{};
    return loaded;
  }
  return {};
}

SuiteId PushRegistry::owner_suite() const noexcept {
  std::scoped_lock lock(mutex_);
  return suite_id_;
}

Status PushRegistry::set_background_policy(BackgroundPolicy policy) {
  if (policy != BackgroundPolicy::foreground_only &&
      policy != BackgroundPolicy::system_managed) {
    return fail(ErrorCode::invalid_argument,
                "invalid push registry background policy");
  }
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return loaded;
  if (state_.policy == policy)
    return {};
  State previous = state_;
  state_.policy = policy;
  auto persisted = persist_unlocked();
  if (!persisted)
    state_ = std::move(previous);
  return persisted;
}

BackgroundPolicy PushRegistry::background_policy() const noexcept {
  std::scoped_lock lock(mutex_);
  return state_.policy;
}

Status PushRegistry::register_connection(std::string connection,
                                         std::string midlet,
                                         std::string filter) {
  auto connection_valid = validate_connection(connection);
  auto midlet_valid = validate_midlet(midlet);
  auto filter_valid = validate_filter(connection, filter);
  if (!connection_valid)
    return connection_valid;
  if (!midlet_valid)
    return midlet_valid;
  if (!filter_valid)
    return filter_valid;

  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return loaded;
  auto owner = connection_owner_unlocked(connection);
  if (!owner)
    return std::unexpected(owner.error());
  if (owner->has_value()) {
    return fail(ErrorCode::invalid_state,
                "push connection is already owned by suite " +
                    std::to_string((*owner)->value));
  }

  State previous = state_;
  ConnectionRegistration registration{
      .connection = connection,
      .midlet = std::move(midlet),
      .filter = std::move(filter),
  };
  state_.connections.emplace(std::move(connection), std::move(registration));
  auto persisted = persist_unlocked();
  if (!persisted)
    state_ = std::move(previous);
  return persisted;
}

Result<bool> PushRegistry::unregister_connection(std::string_view connection) {
  auto valid = validate_connection(connection);
  if (!valid)
    return std::unexpected(valid.error());
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return std::unexpected(loaded.error());
  const auto found = state_.connections.find(std::string(connection));
  if (found == state_.connections.end())
    return false;

  State previous = state_;
  state_.connections.erase(found);
  std::erase_if(state_.requests, [connection](const LaunchRequest &request) {
    return request.kind == LaunchRequestKind::connection &&
           request.target == connection;
  });
  auto persisted = persist_unlocked();
  if (!persisted) {
    state_ = std::move(previous);
    return std::unexpected(persisted.error());
  }
  return true;
}

Result<std::vector<std::string>>
PushRegistry::list_connections(bool available_only) {
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return std::unexpected(loaded.error());

  std::vector<std::string> result;
  result.reserve(state_.connections.size());
  for (const auto &[connection, registration] : state_.connections) {
    (void)registration;
    if (available_only) {
      const bool pending =
          std::any_of(state_.requests.begin(), state_.requests.end(),
                      [&connection](const LaunchRequest &request) {
                        return request.kind == LaunchRequestKind::connection &&
                               request.target == connection;
                      });
      if (!pending)
        continue;
    }
    result.push_back(connection);
  }
  return result;
}

Result<std::vector<ConnectionRegistration>>
PushRegistry::connection_registrations() {
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return std::unexpected(loaded.error());
  std::vector<ConnectionRegistration> result;
  result.reserve(state_.connections.size());
  for (const auto& [connection, registration] : state_.connections) {
    (void)connection;
    result.push_back(registration);
  }
  return result;
}

Result<std::optional<std::string>>
PushRegistry::midlet_for(std::string_view connection) {
  auto valid = validate_connection(connection);
  if (!valid)
    return std::unexpected(valid.error());
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return std::unexpected(loaded.error());
  const auto found = state_.connections.find(std::string(connection));
  if (found == state_.connections.end())
    return std::optional<std::string>{};
  return std::optional<std::string>(found->second.midlet);
}

Result<std::optional<std::string>>
PushRegistry::filter_for(std::string_view connection) {
  auto valid = validate_connection(connection);
  if (!valid)
    return std::unexpected(valid.error());
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return std::unexpected(loaded.error());
  const auto found = state_.connections.find(std::string(connection));
  if (found == state_.connections.end())
    return std::optional<std::string>{};
  return std::optional<std::string>(found->second.filter);
}

Result<i64> PushRegistry::register_alarm(std::string midlet, i64 time_millis) {
  auto valid = validate_midlet(midlet);
  if (!valid)
    return std::unexpected(valid.error());
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return std::unexpected(loaded.error());

  const auto found = state_.alarms.find(midlet);
  const i64 previous_time =
      found == state_.alarms.end() ? 0 : found->second.time_millis;
  State previous = state_;
  if (time_millis <= 0) {
    state_.alarms.erase(midlet);
  } else {
    AlarmRegistration registration{
        .midlet = midlet,
        .time_millis = time_millis,
    };
    state_.alarms.insert_or_assign(std::move(midlet), std::move(registration));
  }
  auto persisted = persist_unlocked();
  if (!persisted) {
    state_ = std::move(previous);
    return std::unexpected(persisted.error());
  }
  return previous_time;
}

Result<std::optional<i64>> PushRegistry::next_alarm_time() {
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return std::unexpected(loaded.error());
  if (state_.alarms.empty()) return std::optional<i64> {};
  const auto earliest = std::min_element(
      state_.alarms.begin(), state_.alarms.end(),
      [](const auto& left, const auto& right) {
        return left.second.time_millis < right.second.time_millis;
      });
  return std::optional<i64>(earliest->second.time_millis);
}

Status PushRegistry::notify_connection_available(std::string_view connection,
                                                 i64 received_at_millis) {
  return notify_connection_available_impl(connection, std::nullopt,
                                          received_at_millis);
}

Status PushRegistry::notify_connection_available(
    std::string_view connection,
    std::string_view source_address,
    i64 received_at_millis) {
  return notify_connection_available_impl(connection, source_address,
                                          received_at_millis);
}

Status PushRegistry::notify_connection_available_impl(
    std::string_view connection,
    std::optional<std::string_view> source_address,
    i64 received_at_millis) {
  auto valid = validate_connection(connection);
  if (!valid) return valid;
  if (received_at_millis < 0) {
    return fail(ErrorCode::invalid_argument,
                "push event wall-clock time must be nonnegative");
  }
  if (source_address.has_value() &&
      (source_address->size() > kMaximumStringBytes ||
       contains_nul(*source_address))) {
    return fail(ErrorCode::invalid_argument,
                "push source address is invalid");
  }
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return loaded;
  const auto registration = state_.connections.find(std::string(connection));
  if (registration == state_.connections.end()) {
    return fail(ErrorCode::out_of_range,
                "push connection is not registered by this suite");
  }
  if (source_address.has_value() && uses_ip_filter(connection)) {
    std::array<std::string_view, 4> source_parts {};
    if (!split_ipv4(*source_address, false, source_parts)) {
      return fail(ErrorCode::invalid_argument,
                  "push source address is not valid IPv4");
    }
  }
  if (source_address.has_value() &&
      !source_matches_filter(connection, registration->second.filter,
                             *source_address)) {
    return {};
  }
  const std::string source = source_address.has_value()
      ? std::string(*source_address)
      : std::string {};
  const bool already_pending =
      std::any_of(state_.requests.begin(), state_.requests.end(),
                  [connection, &source](const LaunchRequest &request) {
                    return request.kind == LaunchRequestKind::connection &&
                           request.target == connection &&
                           request.source_address == source;
                  });
  if (already_pending)
    return {};
  if (state_.requests.size() >= kMaximumEntries) {
    return fail(ErrorCode::overflow,
                "push launch queue is full");
  }
  if (state_.next_request_id == 0 ||
      state_.next_request_id == std::numeric_limits<u64>::max()) {
    return fail(ErrorCode::overflow,
                "push launch request ID space is exhausted");
  }

  State previous = state_;
  state_.requests.push_back(LaunchRequest{
      .id = state_.next_request_id++,
      .kind = LaunchRequestKind::connection,
      .state = LaunchRequestState::pending,
      .target = std::string(connection),
      .midlet = registration->second.midlet,
      .source_address = source,
      .created_at_millis = received_at_millis,
      .next_attempt_millis = received_at_millis,
      .lease_deadline_millis = 0,
      .expires_at_millis = launch_expiry(received_at_millis),
      .attempt_count = 0U,
  });
  auto persisted = persist_unlocked();
  if (!persisted)
    state_ = std::move(previous);
  return persisted;
}

Status PushRegistry::collect_due_alarms(i64 now_millis) {
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded)
    return loaded;
  State previous = state_;
  if (!queue_due_alarms_unlocked(now_millis))
    return {};
  auto persisted = persist_unlocked();
  if (!persisted)
    state_ = std::move(previous);
  return persisted;
}

Result<std::vector<LaunchRequest>>
PushRegistry::eligible_launch_requests(i64 now_millis, bool app_in_foreground,
                                       bool background_execution_granted,
                                       usize limit) {
  if (now_millis < 0) {
    return fail(ErrorCode::invalid_argument,
                "push launch wall-clock time must be nonnegative");
  }
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return std::unexpected(loaded.error());

  State previous = state_;
  const bool alarms_changed = queue_due_alarms_unlocked(now_millis);
  const bool launches_changed = recover_launch_requests_unlocked(now_millis);
  const bool changed = alarms_changed || launches_changed;
  if (changed) {
    auto persisted = persist_unlocked();
    if (!persisted) {
      state_ = std::move(previous);
      return std::unexpected(persisted.error());
    }
  }

  const bool eligible =
      app_in_foreground || (state_.policy == BackgroundPolicy::system_managed &&
                            background_execution_granted);
  if (!eligible || limit == 0U) return std::vector<LaunchRequest> {};

  std::vector<LaunchRequest> result;
  result.reserve(std::min(limit, state_.requests.size()));
  for (const LaunchRequest& request : state_.requests) {
    if (result.size() >= limit) break;
    if (request.state != LaunchRequestState::pending ||
        request.next_attempt_millis > now_millis ||
        request.attempt_count >= kMaximumLaunchAttempts) {
      continue;
    }
    result.push_back(request);
  }
  return result;
}

Status PushRegistry::mark_launching(u64 request_id,
                                    i64 now_millis,
                                    i64 lease_millis) {
  if (request_id == 0U || now_millis < 0 || lease_millis <= 0) {
    return fail(ErrorCode::invalid_argument,
                "push launch lease arguments are invalid");
  }
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return loaded;
  State previous = state_;
  const bool recovered = recover_launch_requests_unlocked(now_millis);
  const auto found = std::find_if(
      state_.requests.begin(), state_.requests.end(),
      [request_id](const LaunchRequest& request) {
        return request.id == request_id;
      });
  if (found == state_.requests.end()) {
    if (recovered) {
      auto persisted = persist_unlocked();
      if (!persisted) state_ = std::move(previous);
    }
    return fail(ErrorCode::out_of_range,
                "push launch request does not exist");
  }
  if (found->state != LaunchRequestState::pending ||
      found->next_attempt_millis > now_millis) {
    if (recovered) {
      auto persisted = persist_unlocked();
      if (!persisted) state_ = std::move(previous);
    }
    return fail(ErrorCode::invalid_state,
                "push launch request is not eligible");
  }
  if (found->attempt_count >= kMaximumLaunchAttempts) {
    state_.requests.erase(found);
  } else {
    found->state = LaunchRequestState::launching;
    ++found->attempt_count;
    found->lease_deadline_millis =
        lease_millis > std::numeric_limits<i64>::max() - now_millis
            ? std::numeric_limits<i64>::max()
            : now_millis + lease_millis;
  }
  auto persisted = persist_unlocked();
  if (!persisted) state_ = std::move(previous);
  return persisted;
}

Status PushRegistry::acknowledge_launch_request(u64 request_id) {
  if (request_id == 0U) {
    return fail(ErrorCode::invalid_argument,
                "push launch request ID must be nonzero");
  }
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return loaded;
  const auto found = std::find_if(
      state_.requests.begin(), state_.requests.end(),
      [request_id](const LaunchRequest& request) {
        return request.id == request_id;
      });
  if (found == state_.requests.end()) {
    return fail(ErrorCode::out_of_range,
                "push launch request does not exist");
  }
  State previous = state_;
  state_.requests.erase(found);
  auto persisted = persist_unlocked();
  if (!persisted) state_ = std::move(previous);
  return persisted;
}

Status PushRegistry::fail_launch_request(u64 request_id,
                                         i64 now_millis) {
  if (request_id == 0U || now_millis < 0) {
    return fail(ErrorCode::invalid_argument,
                "push launch failure arguments are invalid");
  }
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return loaded;
  const auto found = std::find_if(
      state_.requests.begin(), state_.requests.end(),
      [request_id](const LaunchRequest& request) {
        return request.id == request_id;
      });
  if (found == state_.requests.end()) {
    return fail(ErrorCode::out_of_range,
                "push launch request does not exist");
  }

  State previous = state_;
  if (found->attempt_count >= kMaximumLaunchAttempts ||
      (found->expires_at_millis > 0 &&
       now_millis >= found->expires_at_millis)) {
    state_.requests.erase(found);
  } else {
    found->state = LaunchRequestState::pending;
    found->lease_deadline_millis = 0;
    const i64 delay = retry_delay(found->attempt_count);
    found->next_attempt_millis =
        delay > std::numeric_limits<i64>::max() - now_millis
            ? std::numeric_limits<i64>::max()
            : now_millis + delay;
  }
  auto persisted = persist_unlocked();
  if (!persisted) state_ = std::move(previous);
  return persisted;
}

Result<usize> PushRegistry::pending_launch_count() {
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto loaded = reload_unlocked();
  if (!loaded) return std::unexpected(loaded.error());
  return state_.requests.size();
}

Status PushRegistry::remove_suite_state() {
  std::scoped_lock lock(mutex_, persistence_mutex());
  auto configured = require_configured_unlocked();
  if (!configured) return configured;
  const std::string path = state_path_unlocked();
  const std::string temporary = path + ".tmp";
  if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
    return fail(ErrorCode::io_error,
                "failed to remove push registry state: " +
                    std::string(std::strerror(errno)));
  }
  if (::unlink(temporary.c_str()) != 0 && errno != ENOENT) {
    return fail(ErrorCode::io_error,
                "failed to remove push registry recovery state: " +
                    std::string(std::strerror(errno)));
  }
  state_ = State {};
  return {};
}

Status PushRegistry::require_configured_unlocked() const {
  if (!configured_ || root_directory_.empty() || !suite_id_.valid()) {
    return fail(ErrorCode::not_configured, "push registry is not configured");
  }
  return {};
}

Status PushRegistry::reload_unlocked() {
  auto configured = require_configured_unlocked();
  if (!configured)
    return configured;

  const std::string path = state_path_unlocked();
  const std::string temporary = path + ".tmp";
  std::error_code error;
  const bool has_state = std::filesystem::exists(path, error);
  if (error) {
    return fail(ErrorCode::io_error,
                "failed to inspect push registry: " + error.message());
  }
  const bool has_temporary = std::filesystem::exists(temporary, error);
  if (error) {
    return fail(ErrorCode::io_error,
                "failed to inspect push registry recovery file: " +
                    error.message());
  }
  if (!has_state && has_temporary) {
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
      return fail(ErrorCode::io_error,
                  "failed to recover push registry temporary file: " +
                      std::string(std::strerror(errno)));
    }
  } else if (has_state && has_temporary) {
    (void)::unlink(temporary.c_str());
  }

  if (!has_state && !has_temporary) {
    state_ = State{};
    return {};
  }
  auto loaded = read_state_file_unlocked(path, suite_id_);
  if (!loaded) return std::unexpected(loaded.error());
  state_ = std::move(*loaded);
  if (state_.persistence_version == kLegacyVersion) {
    state_.persistence_version = kVersion;
    auto migrated = persist_unlocked();
    if (!migrated) return migrated;
  }
  return {};
}

Result<PushRegistry::State> PushRegistry::read_state_file_unlocked(
    const std::string &path, std::optional<SuiteId> expected_suite) const {
  auto bytes = read_file(path);
  if (!bytes)
    return std::unexpected(bytes.error());
  constexpr usize minimum_size =
      kMagic.size() + 4U + 4U + 1U + 8U + 4U + 4U + 4U + 8U;
  if (bytes->size() < minimum_size) {
    return fail(ErrorCode::io_error, "push registry file is truncated");
  }

  const usize payload_size = bytes->size() - 8U;
  u64 stored_checksum = 0;
  for (u32 shift = 0; shift < 64U; shift += 8U) {
    stored_checksum |=
        static_cast<u64>(
            (*bytes)[payload_size + static_cast<usize>(shift / 8U)])
        << shift;
  }
  if (checksum(std::span<const u8>(*bytes).first(payload_size)) !=
      stored_checksum) {
    return fail(ErrorCode::checksum_mismatch,
                "push registry checksum mismatch");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes->begin())) {
    return fail(ErrorCode::io_error, "push registry magic is invalid");
  }

  Reader reader(
      std::span<const u8>(*bytes).first(payload_size).subspan(kMagic.size()));
  auto version = reader.read_u32();
  auto suite_value = reader.read_i32();
  auto policy_value = reader.read_u8();
  auto next_request_id = reader.read_u64();
  if (!version || !suite_value || !policy_value || !next_request_id) {
    return fail(ErrorCode::io_error, "push registry header is invalid");
  }
  if ((*version != kLegacyVersion && *version != kVersion) ||
      *suite_value <= 0) {
    return fail(ErrorCode::unsupported_feature,
                "push registry version or suite ID is unsupported");
  }
  const SuiteId stored_suite{*suite_value};
  if (expected_suite.has_value() && stored_suite != *expected_suite) {
    return fail(ErrorCode::invalid_state,
                "push registry suite ownership mismatch");
  }
  if (*policy_value > static_cast<u8>(BackgroundPolicy::system_managed)) {
    return fail(ErrorCode::io_error,
                "push registry background policy is invalid");
  }

  State state;
  state.persistence_version = *version;
  state.policy = static_cast<BackgroundPolicy>(*policy_value);
  state.next_request_id = *next_request_id;

  auto connection_count = reader.read_u32();
  if (!connection_count || *connection_count > kMaximumEntries) {
    return fail(ErrorCode::io_error,
                "push registry connection count is invalid");
  }
  for (u32 index = 0; index < *connection_count; ++index) {
    auto connection = reader.read_string();
    auto midlet = reader.read_string();
    auto filter = reader.read_string();
    if (!connection || !midlet || !filter) {
      return fail(ErrorCode::io_error,
                  "push connection registration is malformed");
    }
    if (!validate_connection(*connection) || !validate_midlet(*midlet) ||
        !validate_filter(*connection, *filter)) {
      return fail(ErrorCode::io_error,
                  "push connection registration is invalid");
    }
    const std::string key = *connection;
    const auto [entry, inserted] =
        state.connections.emplace(key, ConnectionRegistration{
                                           .connection = key,
                                           .midlet = std::move(*midlet),
                                           .filter = std::move(*filter),
                                       });
    (void)entry;
    if (!inserted) {
      return fail(ErrorCode::io_error,
                  "push registry contains duplicate connections");
    }
  }

  auto alarm_count = reader.read_u32();
  if (!alarm_count || *alarm_count > kMaximumEntries) {
    return fail(ErrorCode::io_error, "push registry alarm count is invalid");
  }
  for (u32 index = 0; index < *alarm_count; ++index) {
    auto midlet = reader.read_string();
    auto time = reader.read_i64();
    if (!midlet || !time || *time <= 0 || !validate_midlet(*midlet)) {
      return fail(ErrorCode::io_error, "push alarm registration is invalid");
    }
    const std::string key = *midlet;
    const auto [entry, inserted] =
        state.alarms.emplace(key, AlarmRegistration{
                                      .midlet = key,
                                      .time_millis = *time,
                                  });
    (void)entry;
    if (!inserted) {
      return fail(ErrorCode::io_error,
                  "push registry contains duplicate alarms");
    }
  }

  auto request_count = reader.read_u32();
  if (!request_count || *request_count > kMaximumEntries) {
    return fail(ErrorCode::io_error, "push launch request count is invalid");
  }
  u64 maximum_request_id = 0;
  state.requests.reserve(static_cast<usize>(*request_count));
  for (u32 index = 0; index < *request_count; ++index) {
    auto id = reader.read_u64();
    auto kind = reader.read_u8();
    Result<u8> state_value = static_cast<u8>(LaunchRequestState::pending);
    Result<u32> attempt_count = 0U;
    Result<i64> created = 0;
    Result<i64> next_attempt = 0;
    Result<i64> lease_deadline = 0;
    Result<i64> expires_at = 0;
    Result<std::string> target = std::string {};
    Result<std::string> midlet = std::string {};
    Result<std::string> source = std::string {};
    if (*version == kLegacyVersion) {
      created = reader.read_i64();
      target = reader.read_string();
      midlet = reader.read_string();
      if (created) {
        next_attempt = *created;
        expires_at = launch_expiry(*created);
      }
    } else {
      state_value = reader.read_u8();
      attempt_count = reader.read_u32();
      created = reader.read_i64();
      next_attempt = reader.read_i64();
      lease_deadline = reader.read_i64();
      expires_at = reader.read_i64();
      target = reader.read_string();
      midlet = reader.read_string();
      source = reader.read_string();
    }
    if (!id || !kind || !state_value || !attempt_count || !created ||
        !next_attempt || !lease_deadline || !expires_at || !target ||
        !midlet || !source || *id == 0U ||
        (*kind != static_cast<u8>(LaunchRequestKind::connection) &&
         *kind != static_cast<u8>(LaunchRequestKind::alarm)) ||
        *state_value > static_cast<u8>(LaunchRequestState::launching) ||
        *attempt_count > kMaximumLaunchAttempts || *created < 0 ||
        *next_attempt < 0 || *lease_deadline < 0 || *expires_at < 0 ||
        source->size() > kMaximumStringBytes || contains_nul(*source) ||
        !validate_midlet(*midlet)) {
      return fail(ErrorCode::io_error, "push launch request is invalid");
    }
    if (*kind == static_cast<u8>(LaunchRequestKind::connection) &&
        !validate_connection(*target)) {
      return fail(ErrorCode::io_error,
                  "push connection launch request is invalid");
    }
    if (*kind == static_cast<u8>(LaunchRequestKind::alarm) &&
        *target != *midlet) {
      return fail(ErrorCode::io_error,
                  "push alarm launch target is invalid");
    }
    state.requests.push_back(LaunchRequest{
        .id = *id,
        .kind = static_cast<LaunchRequestKind>(*kind),
        .state = static_cast<LaunchRequestState>(*state_value),
        .target = std::move(*target),
        .midlet = std::move(*midlet),
        .source_address = std::move(*source),
        .created_at_millis = *created,
        .next_attempt_millis = *next_attempt,
        .lease_deadline_millis = *lease_deadline,
        .expires_at_millis = *expires_at,
        .attempt_count = *attempt_count,
    });
    maximum_request_id = std::max(maximum_request_id, *id);
  }

  if (reader.offset() + kMagic.size() != payload_size) {
    return fail(ErrorCode::io_error,
                "push registry has unexpected trailing data");
  }
  if (state.next_request_id == 0U ||
      state.next_request_id <= maximum_request_id) {
    return fail(ErrorCode::io_error,
                "push registry next request ID is invalid");
  }
  return state;
}

Status PushRegistry::persist_unlocked() const {
  auto configured = require_configured_unlocked();
  if (!configured)
    return configured;
  if (state_.connections.size() > kMaximumEntries ||
      state_.alarms.size() > kMaximumEntries ||
      state_.requests.size() > kMaximumEntries) {
    return fail(ErrorCode::overflow,
                "push registry exceeds persistence limits");
  }

  std::vector<u8> bytes;
  bytes.reserve(256U + state_.connections.size() * 96U +
                state_.alarms.size() * 48U + state_.requests.size() * 128U);
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  append_u32(bytes, kVersion);
  append_i32(bytes, suite_id_.value);
  append_u8(bytes, static_cast<u8>(state_.policy));
  append_u64(bytes, state_.next_request_id);

  append_u32(bytes, static_cast<u32>(state_.connections.size()));
  for (const auto &[connection, registration] : state_.connections) {
    append_string(bytes, connection);
    append_string(bytes, registration.midlet);
    append_string(bytes, registration.filter);
  }
  append_u32(bytes, static_cast<u32>(state_.alarms.size()));
  for (const auto &[midlet, alarm] : state_.alarms) {
    append_string(bytes, midlet);
    append_i64(bytes, alarm.time_millis);
  }
  append_u32(bytes, static_cast<u32>(state_.requests.size()));
  for (const LaunchRequest &request : state_.requests) {
    append_u64(bytes, request.id);
    append_u8(bytes, static_cast<u8>(request.kind));
    append_u8(bytes, static_cast<u8>(request.state));
    append_u32(bytes, request.attempt_count);
    append_i64(bytes, request.created_at_millis);
    append_i64(bytes, request.next_attempt_millis);
    append_i64(bytes, request.lease_deadline_millis);
    append_i64(bytes, request.expires_at_millis);
    append_string(bytes, request.target);
    append_string(bytes, request.midlet);
    append_string(bytes, request.source_address);
  }
  append_u64(bytes, checksum(bytes));
  return write_atomic(state_path_unlocked(), bytes);
}

Result<std::optional<SuiteId>>
PushRegistry::connection_owner_unlocked(std::string_view connection) const {
  std::error_code error;
  std::filesystem::directory_iterator iterator(root_directory_, error);
  const std::filesystem::directory_iterator end;
  while (!error && iterator != end) {
    const std::filesystem::directory_entry entry = *iterator;
    iterator.increment(error);
    std::error_code entry_error;
    const bool regular = entry.is_regular_file(entry_error);
    if (entry_error) {
      return fail(ErrorCode::io_error,
                  "failed to inspect push registry ownership entry: " +
                      entry_error.message());
    }
    if (!regular || entry.path().extension() != ".push") {
      continue;
    }
    const std::string stem = entry.path().stem().string();
    i32 suite_value = 0;
    const auto parsed =
        std::from_chars(stem.data(), stem.data() + stem.size(), suite_value);
    if (parsed.ec != std::errc{} || parsed.ptr != stem.data() + stem.size() ||
        suite_value <= 0) {
      continue;
    }
    const SuiteId owner{suite_value};
    auto state = read_state_file_unlocked(entry.path().string(), owner);
    if (!state)
      return std::unexpected(state.error());
    if (state->connections.contains(std::string(connection))) {
      return std::optional<SuiteId>(owner);
    }
  }
  if (error) {
    return fail(ErrorCode::io_error,
                "failed to scan push registry ownership: " + error.message());
  }
  return std::optional<SuiteId>{};
}

bool PushRegistry::queue_due_alarms_unlocked(i64 now_millis) {
  bool changed = false;
  for (auto iterator = state_.alarms.begin();
       iterator != state_.alarms.end();) {
    if (iterator->second.time_millis > now_millis) {
      ++iterator;
      continue;
    }
    const std::string midlet = iterator->second.midlet;
    const i64 alarm_time = iterator->second.time_millis;
    const bool already_pending =
        std::any_of(state_.requests.begin(), state_.requests.end(),
                    [&midlet](const LaunchRequest& request) {
                      return request.kind == LaunchRequestKind::alarm &&
                             request.target == midlet;
                    });
    const bool can_queue = state_.requests.size() < kMaximumEntries &&
        state_.next_request_id != 0U &&
        state_.next_request_id != std::numeric_limits<u64>::max();
    if (!already_pending && can_queue) {
      state_.requests.push_back(LaunchRequest{
          .id = state_.next_request_id++,
          .kind = LaunchRequestKind::alarm,
          .state = LaunchRequestState::pending,
          .target = midlet,
          .midlet = midlet,
          .source_address = {},
          .created_at_millis = alarm_time,
          .next_attempt_millis = now_millis,
          .lease_deadline_millis = 0,
          .expires_at_millis = launch_expiry(now_millis),
          .attempt_count = 0U,
      });
    }
    if (!already_pending && !can_queue) {
      ++iterator;
      continue;
    }
    iterator = state_.alarms.erase(iterator);
    changed = true;
  }
  return changed;
}

bool PushRegistry::recover_launch_requests_unlocked(i64 now_millis) {
  bool changed = false;
  auto iterator = state_.requests.begin();
  while (iterator != state_.requests.end()) {
    if ((iterator->expires_at_millis > 0 &&
         now_millis >= iterator->expires_at_millis) ||
        iterator->attempt_count >= kMaximumLaunchAttempts) {
      iterator = state_.requests.erase(iterator);
      changed = true;
      continue;
    }
    if (iterator->state == LaunchRequestState::launching &&
        iterator->lease_deadline_millis > 0 &&
        now_millis >= iterator->lease_deadline_millis) {
      iterator->state = LaunchRequestState::pending;
      iterator->lease_deadline_millis = 0;
      const i64 delay = retry_delay(iterator->attempt_count);
      iterator->next_attempt_millis =
          delay > std::numeric_limits<i64>::max() - now_millis
              ? std::numeric_limits<i64>::max()
              : now_millis + delay;
      changed = true;
    }
    ++iterator;
  }
  return changed;
}

std::string PushRegistry::state_path_unlocked() const {
  return root_directory_ + "/" + std::to_string(suite_id_.value) + ".push";
}

Status PushRegistry::validate_connection(std::string_view connection) {
  if (connection.empty() || connection.size() > 2'048U ||
      contains_nul(connection)) {
    return fail(ErrorCode::invalid_argument,
                "push connection is empty or too long");
  }
  if (connection.starts_with("socket://")) {
    auto port = parse_local_port(connection, "socket://");
    if (!port) return std::unexpected(port.error());
    return {};
  }
  if (connection.starts_with("serversocket://")) {
    auto port = parse_local_port(connection, "serversocket://");
    if (!port) return std::unexpected(port.error());
    return {};
  }
  if (connection.starts_with("datagram://")) {
    auto port = parse_local_port(connection, "datagram://");
    if (!port) return std::unexpected(port.error());
    return {};
  }
  if (connection.starts_with("sms://")) {
    auto port = parse_local_port(connection, "sms://");
    if (!port) return std::unexpected(port.error());
    return {};
  }
  if (connection.starts_with("cbs://")) {
    auto port = parse_local_port(connection, "cbs://");
    if (!port) return std::unexpected(port.error());
    return {};
  }
  if (connection.starts_with("mms://:")) {
    const std::string_view application_id = connection.substr(7U);
    if (application_id.empty() || application_id.size() > 32U) {
      return fail(ErrorCode::invalid_argument,
                  "MMS push application ID is invalid");
    }
    for (const char character : application_id) {
      const bool valid = (character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '.' ||
          character == '_' || character == '-';
      if (!valid) {
        return fail(ErrorCode::invalid_argument,
                    "MMS push application ID contains invalid characters");
      }
    }
    return {};
  }
  return fail(ErrorCode::unsupported_feature,
              "push connection scheme is not supported");
}

Status PushRegistry::validate_midlet(std::string_view midlet) {
  if (midlet.empty() || midlet.size() > 512U || contains_nul(midlet)) {
    return fail(ErrorCode::invalid_argument,
                "push MIDlet class name is empty or too long");
  }
  for (const char character : midlet) {
    const unsigned char value = static_cast<unsigned char>(character);
    const bool valid = (value >= static_cast<unsigned char>('a') &&
                        value <= static_cast<unsigned char>('z')) ||
                       (value >= static_cast<unsigned char>('A') &&
                        value <= static_cast<unsigned char>('Z')) ||
                       (value >= static_cast<unsigned char>('0') &&
                        value <= static_cast<unsigned char>('9')) ||
                       character == '.' || character == '/' ||
                       character == '_' || character == '$';
    if (!valid) {
      return fail(ErrorCode::invalid_argument,
                  "push MIDlet class name contains invalid characters");
    }
  }
  return {};
}

Status PushRegistry::validate_filter(std::string_view connection,
                                     std::string_view filter) {
  if (filter.empty() || filter.size() > 2'048U || contains_nul(filter)) {
    return fail(ErrorCode::invalid_argument,
                "push connection filter is empty or too long");
  }
  for (const char character : filter) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20U || byte == 0x7FU) {
      return fail(ErrorCode::invalid_argument,
                  "push connection filter contains control characters");
    }
  }
  if (uses_ip_filter(connection)) {
    if (filter == "*") return {};
    std::array<std::string_view, 4> parts {};
    if (!split_ipv4(filter, true, parts)) {
      return fail(ErrorCode::invalid_argument,
                  "push IP filter must be '*' or four IPv4 fields using digits, '*' and '?'");
    }
    return {};
  }
  if (uses_text_filter(connection)) return {};
  return fail(ErrorCode::unsupported_feature,
              "push connection filter scheme is not supported");
}

bool PushRegistry::source_matches_filter(
    std::string_view connection,
    std::string_view filter,
    std::string_view source_address) noexcept {
  if (filter == "*") return true;
  if (uses_text_filter(connection)) {
    return wildcard_matches(filter, source_address);
  }
  std::array<std::string_view, 4> filter_parts {};
  std::array<std::string_view, 4> source_parts {};
  if (!split_ipv4(filter, true, filter_parts) ||
      !split_ipv4(source_address, false, source_parts)) {
    return false;
  }
  for (usize index = 0U; index < filter_parts.size(); ++index) {
    if (!wildcard_segment_matches(filter_parts[index], source_parts[index])) {
      return false;
    }
  }
  return true;
}

i64 PushRegistry::launch_expiry(i64 created_at_millis) noexcept {
  if (created_at_millis < 0 ||
      created_at_millis >
          std::numeric_limits<i64>::max() - kLaunchLifetimeMillis) {
    return std::numeric_limits<i64>::max();
  }
  return created_at_millis + kLaunchLifetimeMillis;
}

i64 PushRegistry::retry_delay(u32 attempt_count) noexcept {
  if (attempt_count == 0U) return 1'000;
  const u32 shift = std::min<u32>(attempt_count - 1U, 18U);
  const i64 delay = 1'000LL << shift;
  return std::min(delay, kMaximumRetryDelayMillis);
}

} // namespace phoneme::push
