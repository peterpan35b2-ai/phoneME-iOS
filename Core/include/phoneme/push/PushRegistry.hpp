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
    std::string target;
    std::string midlet;
    i64 created_at_millis{0};
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
    [[nodiscard]] Result<std::optional<std::string>>
    midlet_for(std::string_view connection);
    [[nodiscard]] Result<std::optional<std::string>>
    filter_for(std::string_view connection);

    // Returns the previous alarm time, or zero when no alarm existed.
    // A non-positive time removes the current alarm registration.
    [[nodiscard]] Result<i64> register_alarm(std::string midlet, i64 time_millis);

    // These methods only persist launch intent. They never start a VM.
    [[nodiscard]] Status notify_connection_available(std::string_view connection,
                                                     i64 received_at_millis);
    [[nodiscard]] Status collect_due_alarms(i64 now_millis);

    // iOS policy gate: foreground_only requires app_in_foreground. The
    // system_managed policy additionally permits delivery while iOS has
    // explicitly granted a finite background execution window.
    [[nodiscard]] Result<std::vector<LaunchRequest>>
    eligible_launch_requests(i64 now_millis, bool app_in_foreground,
                             bool background_execution_granted,
                             usize limit = 32U);
    [[nodiscard]] Status acknowledge_launch_request(u64 request_id);
    [[nodiscard]] Result<usize> pending_launch_count();

  private:
    struct State final
    {
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
    [[nodiscard]] std::string state_path_unlocked() const;

    [[nodiscard]] static Status validate_connection(std::string_view connection);
    [[nodiscard]] static Status validate_midlet(std::string_view midlet);
    [[nodiscard]] static Status validate_filter(std::string_view filter);

    mutable std::mutex mutex_;
    std::string root_directory_;
    SuiteId suite_id_{};
    State state_;
    bool configured_{false};
  };

} // namespace phoneme::push
