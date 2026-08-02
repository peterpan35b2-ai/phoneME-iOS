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
  require(first.acknowledge_launch_request(background->front().id).has_value(),
          "acknowledge delivered connection request");

  auto previous_alarm = first.register_alarm(std::string(midlet), 3'000);
  require(previous_alarm.has_value() && *previous_alarm == 0,
          "register first alarm");
  previous_alarm = first.register_alarm(std::string(midlet), 4'000);
  require(previous_alarm.has_value() && *previous_alarm == 3'000,
          "alarm replacement returns previous time");
  require(first.collect_due_alarms(3'999).has_value(),
          "collect alarms before deadline");
  count = first.pending_launch_count();
  require(count.has_value() && *count == 0U,
          "alarm remains pending until deadline");
  require(first.collect_due_alarms(4'000).has_value(),
          "collect due alarm into launch queue");

  phoneme::push::PushRegistry reopened;
  require(reopened.configure(root.string(), first_suite).has_value(),
          "reopen persisted push registry");
  auto persisted = reopened.eligible_launch_requests(4'000, false, true, 8U);
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
