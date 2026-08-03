#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::push
{

  enum class BackgroundPolicy : u8
  {
    foreground_only = 0,
    system_managed = 1,
  };

  enum class LaunchRequestKind : u8
  {
    connection = 1,
    alarm = 2,
  };

  enum class LaunchRequestState : u8
  {
    pending = 0,
    launching = 1,
  };

  struct ConnectionRegistration final
  {
    std::string connection;
    std::string midlet;
    std::string filter;
  };

  struct AlarmRegistration final
  {
    std::string midlet;
    i64 time_millis{0};
  };

  struct LaunchRequest final
  {
    u64 id{0};
    LaunchRequestKind kind{LaunchRequestKind::connection};
    LaunchRequestState state{LaunchRequestState::pending};
    std::string target;
    std::string midlet;
    std::string source_address;
    i64 created_at_millis{0};
    i64 next_attempt_millis{0};
    i64 lease_deadline_millis{0};
    i64 expires_at_millis{0};
    u32 attempt_count{0};
  };

  class PushRegistry final
  {
  public:
    PushRegistry() = default;

    PushRegistry(const PushRegistry &) = delete;
    PushRegistry &operator=(const PushRegistry &) = delete;

    [[nodiscard]] Status configure(std::string root_directory, SuiteId suite_id);
    [[nodiscard]] SuiteId owner_suite() const noexcept;

    [[nodiscard]] Status set_background_policy(BackgroundPolicy policy);
    [[nodiscard]] BackgroundPolicy background_policy() const noexcept;

    [[nodiscard]] Status register_connection(std::string connection,
                                             std::string midlet,
                                             std::string filter);
    [[nodiscard]] Result<bool> unregister_connection(std::string_view connection);
    [[nodiscard]] Result<std::vector<std::string>>
    list_connections(bool available_only);
    [[nodiscard]] Result<std::vector<ConnectionRegistration>>
    connection_registrations();
    [[nodiscard]] Result<std::optional<std::string>>
    midlet_for(std::string_view connection);
    [[nodiscard]] Result<std::optional<std::string>>
    filter_for(std::string_view connection);

    // Returns the previous alarm time, or zero when no alarm existed.
    // A non-positive time removes the current alarm registration. Alarm times
    // are persisted wall-clock epoch milliseconds; host scheduling should use
    // monotonic waits only as an optimization and always re-check wall time.
    [[nodiscard]] Result<i64> register_alarm(std::string midlet, i64 time_millis);
    [[nodiscard]] Result<std::optional<i64>> next_alarm_time();

    // These methods only persist launch intent. They never start a VM.
    [[nodiscard]] Status notify_connection_available(std::string_view connection,
                                                     i64 received_at_millis);
    [[nodiscard]] Status notify_connection_available(
        std::string_view connection,
        std::string_view source_address,
        i64 received_at_millis);
    [[nodiscard]] Status collect_due_alarms(i64 now_millis);

    // iOS policy gate: foreground_only requires app_in_foreground. The
    // system_managed policy additionally permits delivery while iOS has
    // explicitly granted a finite background execution window.
    [[nodiscard]] Result<std::vector<LaunchRequest>>
    eligible_launch_requests(i64 now_millis, bool app_in_foreground,
                             bool background_execution_granted,
                             usize limit = 32U);
    [[nodiscard]] Status mark_launching(u64 request_id,
                                        i64 now_millis,
                                        i64 lease_millis = 60'000);
    [[nodiscard]] Status acknowledge_launch_request(u64 request_id);
    [[nodiscard]] Status fail_launch_request(u64 request_id,
                                             i64 now_millis);
    [[nodiscard]] Result<usize> pending_launch_count();
    [[nodiscard]] Status remove_suite_state();

  private:
    struct State final
    {
      u32 persistence_version{2U};
      BackgroundPolicy policy{BackgroundPolicy::foreground_only};
      u64 next_request_id{1};
      std::map<std::string, ConnectionRegistration> connections;
      std::map<std::string, AlarmRegistration> alarms;
      std::vector<LaunchRequest> requests;
    };

    [[nodiscard]] Status require_configured_unlocked() const;
    [[nodiscard]] Status reload_unlocked();
    [[nodiscard]] Result<State>
    read_state_file_unlocked(const std::string &path,
                             std::optional<SuiteId> expected_suite) const;
    [[nodiscard]] Status persist_unlocked() const;
    [[nodiscard]] Result<std::optional<SuiteId>>
    connection_owner_unlocked(std::string_view connection) const;
    [[nodiscard]] bool queue_due_alarms_unlocked(i64 now_millis);
    [[nodiscard]] bool recover_launch_requests_unlocked(i64 now_millis);
    [[nodiscard]] Status notify_connection_available_impl(
        std::string_view connection,
        std::optional<std::string_view> source_address,
        i64 received_at_millis);
    [[nodiscard]] std::string state_path_unlocked() const;

    [[nodiscard]] static Status validate_connection(std::string_view connection);
    [[nodiscard]] static Status validate_midlet(std::string_view midlet);
    [[nodiscard]] static Status validate_filter(
        std::string_view connection,
        std::string_view filter);
    [[nodiscard]] static bool source_matches_filter(
        std::string_view connection,
        std::string_view filter,
        std::string_view source_address) noexcept;
    [[nodiscard]] static i64 launch_expiry(i64 created_at_millis) noexcept;
    [[nodiscard]] static i64 retry_delay(u32 attempt_count) noexcept;

    mutable std::mutex mutex_;
    std::string root_directory_;
    SuiteId suite_id_{};
    State state_;
    bool configured_{false};
  };

} // namespace phoneme::push
