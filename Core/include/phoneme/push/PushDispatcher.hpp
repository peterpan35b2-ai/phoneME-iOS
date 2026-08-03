#pragma once

#include <string_view>
#include <vector>

#include "phoneme/push/PushRegistry.hpp"

namespace phoneme::push {

struct PushExecutionWindow final {
  i64 now_millis {0};
  bool app_in_foreground {false};
  bool background_execution_granted {false};
  usize limit {32U};
};

class PushDispatcher final {
public:
  explicit PushDispatcher(PushRegistry& registry) noexcept
      : registry_(registry) {}

  [[nodiscard]] Result<std::vector<ConnectionRegistration>>
  listener_registrations();
  [[nodiscard]] Result<std::optional<i64>> next_alarm_time();

  [[nodiscard]] Status connection_available(std::string_view connection,
                                             std::string_view source_address,
                                             i64 received_at_millis);
  [[nodiscard]] Status tick(i64 wall_clock_millis);

  [[nodiscard]] Result<std::vector<LaunchRequest>> poll(
      const PushExecutionWindow& window);
  [[nodiscard]] Status begin_launch(u64 request_id,
                                    i64 now_millis,
                                    i64 lease_millis = 60'000);
  [[nodiscard]] Status complete_launch(u64 request_id,
                                       bool success,
                                       i64 now_millis);

private:
  PushRegistry& registry_;
};

} // namespace phoneme::push
