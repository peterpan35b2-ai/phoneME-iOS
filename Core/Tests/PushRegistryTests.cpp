#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#include "phoneme/push/PushRegistry.hpp"

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::abort();
  }
}

void test_registry_lifecycle() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("phoneme-push-" + std::to_string(::getpid()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  require(!error, "clear push test root");

  constexpr phoneme::SuiteId first_suite{41};
  constexpr phoneme::SuiteId second_suite{42};
  constexpr std::string_view connection = "socket://:41001";
  constexpr std::string_view midlet = "corefixture.PushOps";

  phoneme::push::PushRegistry first;
  phoneme::push::PushRegistry second;
  require(first.configure(root.string(), first_suite).has_value(),
          "configure first suite push registry");
  require(second.configure(root.string(), second_suite).has_value(),
          "configure second suite push registry");

  require(first
              .register_connection(std::string(connection), std::string(midlet),
                                   "*")
              .has_value(),
          "register suite-owned push connection");
  require(!second
               .register_connection(std::string(connection),
                                    std::string(midlet), "*")
               .has_value(),
          "reject connection already owned by another suite");

  auto all = first.list_connections(false);
  require(all.has_value() && all->size() == 1U && all->front() == connection,
          "list registered push connection");
  auto available = first.list_connections(true);
  require(available.has_value() && available->empty(),
          "connection is not available before host notification");

  require(first.notify_connection_available(connection, 1'000).has_value(),
          "queue connection launch request");
  require(first.notify_connection_available(connection, 1'001).has_value(),
          "coalesce duplicate connection launch request");
  auto count = first.pending_launch_count();
  require(count.has_value() && *count == 1U,
          "duplicate connection notifications coalesce");

  auto blocked = first.eligible_launch_requests(1'001, false, true, 8U);
  require(blocked.has_value() && blocked->empty(),
          "foreground-only policy blocks background delivery");

  auto foreground = first.eligible_launch_requests(1'001, true, false, 8U);
  require(foreground.has_value() && foreground->size() == 1U &&
              foreground->front().kind ==
                  phoneme::push::LaunchRequestKind::connection,
          "foreground delivery exposes queued request");

  require(first
              .set_background_policy(
                  phoneme::push::BackgroundPolicy::system_managed)
              .has_value(),
          "enable system-managed background policy");
  auto background = first.eligible_launch_requests(1'001, false, true, 8U);
  require(background.has_value() && background->size() == 1U,
          "iOS-granted background window exposes queued request");
  const phoneme::u64 connection_request_id = background->front().id;
  require(first.mark_launching(connection_request_id, 1'001, 100).has_value(),
          "mark connection request launching");
  auto leased = first.eligible_launch_requests(1'050, false, true, 8U);
  require(leased.has_value() && leased->empty(),
          "launch lease hides in-flight request");

  phoneme::push::PushRegistry crash_reopened;
  require(crash_reopened.configure(root.string(), first_suite).has_value(),
          "reopen registry after simulated launch crash");
  auto recovered_early =
      crash_reopened.eligible_launch_requests(1'101, false, true, 8U);
  require(recovered_early.has_value() && recovered_early->empty(),
          "expired lease applies retry backoff");
  auto recovered =
      crash_reopened.eligible_launch_requests(2'101, false, true, 8U);
  require(recovered.has_value() && recovered->size() == 1U &&
              recovered->front().id == connection_request_id &&
              recovered->front().attempt_count == 1U,
          "crashed launch is recovered after backoff");
  require(crash_reopened
              .mark_launching(connection_request_id, 2'101, 100)
              .has_value(),
          "mark recovered request launching");
  require(crash_reopened
              .fail_launch_request(connection_request_id, 2'102)
              .has_value(),
          "record launch failure");
  auto retry_early =
      crash_reopened.eligible_launch_requests(4'101, false, true, 8U);
  require(retry_early.has_value() && retry_early->empty(),
          "second attempt uses exponential backoff");
  auto retry =
      crash_reopened.eligible_launch_requests(4'102, false, true, 8U);
  require(retry.has_value() && retry->size() == 1U &&
              retry->front().attempt_count == 2U,
          "failed launch becomes eligible after retry delay");
  require(crash_reopened
              .acknowledge_launch_request(connection_request_id)
              .has_value(),
          "acknowledge recovered connection request");

  auto previous_alarm = crash_reopened.register_alarm(std::string(midlet), 5'000);
  require(previous_alarm.has_value() && *previous_alarm == 0,
          "register first alarm");
  previous_alarm = crash_reopened.register_alarm(std::string(midlet), 6'000);
  require(previous_alarm.has_value() && *previous_alarm == 5'000,
          "alarm replacement returns previous time");
  auto next_alarm = crash_reopened.next_alarm_time();
  require(next_alarm.has_value() && next_alarm->has_value() &&
              **next_alarm == 6'000,
          "expose next wall-clock alarm deadline");
  require(crash_reopened.collect_due_alarms(5'999).has_value(),
          "collect alarms before deadline");
  count = crash_reopened.pending_launch_count();
  require(count.has_value() && *count == 0U,
          "alarm remains pending until deadline");
  require(crash_reopened.collect_due_alarms(6'000).has_value(),
          "collect due alarm into launch queue");

  phoneme::push::PushRegistry reopened;
  require(reopened.configure(root.string(), first_suite).has_value(),
          "reopen persisted push registry");
  auto persisted = reopened.eligible_launch_requests(6'000, false, true, 8U);
  require(persisted.has_value() && persisted->size() == 1U &&
              persisted->front().kind ==
                  phoneme::push::LaunchRequestKind::alarm &&
              persisted->front().midlet == midlet,
          "launch queue survives a fresh registry instance");
  require(
      reopened.acknowledge_launch_request(persisted->front().id).has_value(),
      "acknowledge persisted alarm request");

  auto owner_midlet = reopened.midlet_for(connection);
  auto owner_filter = reopened.filter_for(connection);
  require(owner_midlet.has_value() && owner_midlet->has_value() &&
              **owner_midlet == midlet,
          "persist connection MIDlet mapping");
  require(owner_filter.has_value() && owner_filter->has_value() &&
              **owner_filter == "*",
          "persist connection filter mapping");

  auto removed = reopened.unregister_connection(connection);
  require(removed.has_value() && *removed, "unregister owned connection");
  require(second
              .register_connection(std::string(connection), std::string(midlet),
                                   "127.0.0.1")
              .has_value(),
          "release ownership for another suite");
  require(!second
               .register_connection("datagram://:42002", std::string(midlet),
                                    "10.0.*")
               .has_value(),
          "reject malformed IPv4 push filter");
  constexpr std::string_view filtered_connection = "datagram://:42001";
  require(second
              .register_connection(std::string(filtered_connection),
                                   std::string(midlet), "10.*.??.5")
              .has_value(),
          "register wildcard IPv4 push filter");
  require(second.notify_connection_available(filtered_connection,
                                               "11.2.34.5", 5'000)
              .has_value(),
          "ignore source rejected by push filter without host error");
  count = second.pending_launch_count();
  require(count.has_value() && *count == 0U,
          "rejected push source does not queue launch request");
  require(second.notify_connection_available(filtered_connection,
                                               "10.2.34.5", 5'001)
              .has_value(),
          "accept source matching wildcard push filter");
  count = second.pending_launch_count();
  require(count.has_value() && *count == 1U,
          "matching push source queues launch request");
  require(!second.notify_connection_available(filtered_connection,
                                                "999.2.34.5", 5'002)
               .has_value(),
          "reject malformed push source address");

  std::filesystem::remove_all(root, error);
  require(!error, "remove push test root");
}

void test_corruption_detection() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("phoneme-push-corrupt-" + std::to_string(::getpid()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  require(!error, "clear corruption test root");

  constexpr phoneme::SuiteId suite{77};
  phoneme::push::PushRegistry registry;
  require(registry.configure(root.string(), suite).has_value(),
          "configure corruption registry");
  require(
      registry
          .register_connection("datagram://:5000", "corefixture.PushOps", "*")
          .has_value(),
      "persist corruption fixture");

  const std::filesystem::path state = root / "77.push";
  std::fstream stream(state, std::ios::in | std::ios::out | std::ios::binary);
  require(static_cast<bool>(stream), "open persisted registry for corruption");
  stream.seekg(12, std::ios::beg);
  char byte = 0;
  stream.read(&byte, 1);
  require(static_cast<bool>(stream), "read byte to corrupt");
  byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x5AU);
  stream.seekp(12, std::ios::beg);
  stream.write(&byte, 1);
  stream.close();

  phoneme::push::PushRegistry reopened;
  auto configured = reopened.configure(root.string(), suite);
  require(!configured.has_value() &&
              configured.error().code == phoneme::ErrorCode::checksum_mismatch,
          "detect corrupted push registry checksum");

  std::filesystem::remove_all(root, error);
  require(!error, "remove corruption test root");
}

} // namespace

int main() {
  test_registry_lifecycle();
  test_corruption_detection();
  std::cout << "Push registry tests passed\n";
  return 0;
}
