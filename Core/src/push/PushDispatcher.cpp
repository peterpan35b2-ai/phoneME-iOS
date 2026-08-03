#include "phoneme/push/PushDispatcher.hpp"

namespace phoneme::push {

Result<std::vector<ConnectionRegistration>>
PushDispatcher::listener_registrations() {
  return registry_.connection_registrations();
}

Result<std::optional<i64>> PushDispatcher::next_alarm_time() {
  return registry_.next_alarm_time();
}

Status PushDispatcher::connection_available(std::string_view connection,
                                            std::string_view source_address,
                                            i64 received_at_millis) {
  return registry_.notify_connection_available(connection, source_address,
                                                received_at_millis);
}

Status PushDispatcher::tick(i64 wall_clock_millis) {
  if (wall_clock_millis < 0) {
    return fail(ErrorCode::invalid_argument,
                "push dispatcher wall-clock time must be nonnegative");
  }
  return registry_.collect_due_alarms(wall_clock_millis);
}

Result<std::vector<LaunchRequest>> PushDispatcher::poll(
    const PushExecutionWindow& window) {
  if (window.now_millis < 0) {
    return fail(ErrorCode::invalid_argument,
                "push execution window time must be nonnegative");
  }
  return registry_.eligible_launch_requests(
      window.now_millis, window.app_in_foreground,
      window.background_execution_granted, window.limit);
}

Status PushDispatcher::begin_launch(u64 request_id,
                                    i64 now_millis,
                                    i64 lease_millis) {
  return registry_.mark_launching(request_id, now_millis, lease_millis);
}

Status PushDispatcher::complete_launch(u64 request_id,
                                       bool success,
                                       i64 now_millis) {
  return success
      ? registry_.acknowledge_launch_request(request_id)
      : registry_.fail_launch_request(request_id, now_millis);
}

} // namespace phoneme::push
