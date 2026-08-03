#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <unistd.h>

#include "phoneme/push/PushDispatcher.hpp"

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "PushDispatcherTests failure: " << message << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("phoneme-push-dispatch-" + std::to_string(::getpid()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  require(!error, "clear dispatcher test root");

  phoneme::push::PushRegistry registry;
  require(registry.configure(root.string(), phoneme::SuiteId {91}).has_value(),
          "configure dispatcher registry");
  require(registry.set_background_policy(
              phoneme::push::BackgroundPolicy::system_managed).has_value(),
          "enable background execution policy");
  require(registry.register_connection(
              "datagram://:42001", "corefixture.PushOps",
              "10.*.??.5").has_value(),
          "register datagram listener");
  require(registry.register_connection(
              "sms://:5000", "corefixture.PushOps",
              "+8491*").has_value(),
          "register SMS listener");
  require(!registry.register_connection(
               "http://example.com", "corefixture.PushOps", "*").has_value(),
          "reject unsupported push scheme");
  require(!registry.register_connection(
               "socket://example.com:80", "corefixture.PushOps", "*")
               .has_value(),
          "reject outbound socket URI as push listener");

  phoneme::push::PushDispatcher dispatcher(registry);
  auto registrations = dispatcher.listener_registrations();
  require(registrations.has_value() && registrations->size() == 2U,
          "expose listener reconciliation snapshot");

  require(dispatcher.connection_available(
              "datagram://:42001", "10.2.34.5", 1'000).has_value(),
          "accept matching datagram source");
  require(dispatcher.connection_available(
              "datagram://:42001", "10.2.34.5", 1'001).has_value(),
          "coalesce duplicate datagram event");
  require(dispatcher.connection_available(
              "datagram://:42001", "10.3.44.5", 1'002).has_value(),
          "queue distinct datagram source");
  require(dispatcher.connection_available(
              "sms://:5000", "+84912345678", 1'003).has_value(),
          "accept matching SMS source");
  require(dispatcher.connection_available(
              "sms://:5000", "+12025550123", 1'004).has_value(),
          "ignore rejected SMS source without host error");

  auto pending = registry.pending_launch_count();
  require(pending.has_value() && *pending == 3U,
          "coalesce only identical event identity");

  phoneme::push::PushExecutionWindow denied {
      .now_millis = 1'010,
      .app_in_foreground = false,
      .background_execution_granted = false,
      .limit = 8U,
  };
  auto blocked = dispatcher.poll(denied);
  require(blocked.has_value() && blocked->empty(),
          "do not assume an always-on iOS background window");

  denied.background_execution_granted = true;
  auto eligible = dispatcher.poll(denied);
  require(eligible.has_value() && eligible->size() == 3U,
          "deliver queued events only inside granted execution window");
  const phoneme::u64 first_id = eligible->front().id;
  require(dispatcher.begin_launch(first_id, 1'010, 50).has_value(),
          "lease selected launch request");
  auto while_leased = dispatcher.poll(denied);
  require(while_leased.has_value() && while_leased->size() == 2U,
          "in-flight request is not delivered twice");
  require(dispatcher.complete_launch(first_id, false, 1'020).has_value(),
          "requeue failed launch with backoff");
  auto before_retry = dispatcher.poll(
      phoneme::push::PushExecutionWindow {
          .now_millis = 2'019,
          .app_in_foreground = false,
          .background_execution_granted = true,
          .limit = 8U,
      });
  require(before_retry.has_value() && before_retry->size() == 2U,
          "failed request remains hidden during backoff");
  auto after_retry = dispatcher.poll(
      phoneme::push::PushExecutionWindow {
          .now_millis = 2'020,
          .app_in_foreground = false,
          .background_execution_granted = true,
          .limit = 8U,
      });
  require(after_retry.has_value() && after_retry->size() == 3U,
          "failed request returns after backoff");
  require(dispatcher.begin_launch(first_id, 2'020).has_value(),
          "lease retried request");
  require(dispatcher.complete_launch(first_id, true, 2'020).has_value(),
          "acknowledge successful retried launch");

  require(registry.register_alarm("corefixture.PushOps", 5'000).has_value(),
          "register dispatcher alarm");
  auto alarm = dispatcher.next_alarm_time();
  require(alarm.has_value() && alarm->has_value() && **alarm == 5'000,
          "expose next alarm to iOS scheduler");
  require(dispatcher.tick(4'000).has_value(),
          "backward clock observation does not fire alarm");
  require(dispatcher.tick(5'000).has_value(),
          "wall clock reaching deadline queues alarm");

  phoneme::push::PushRegistry restarted;
  require(restarted.configure(root.string(), phoneme::SuiteId {91}).has_value(),
          "recover dispatcher state after process restart");
  auto recovered = restarted.pending_launch_count();
  require(recovered.has_value() && *recovered == 3U,
          "persist distinct connection events and due alarm");
  require(restarted.remove_suite_state().has_value(),
          "remove suite push state during uninstall");

  phoneme::push::PushRegistry after_uninstall;
  require(after_uninstall.configure(root.string(), phoneme::SuiteId {91})
              .has_value(),
          "configure registry after uninstall cleanup");
  auto empty = after_uninstall.pending_launch_count();
  auto no_listeners = after_uninstall.connection_registrations();
  require(empty.has_value() && *empty == 0U && no_listeners.has_value() &&
              no_listeners->empty(),
          "suite uninstall removes queue and listener ownership");

  std::filesystem::remove_all(root, error);
  require(!error, "remove dispatcher test root");
  std::cout << "PushDispatcherTests passed\n";
  return 0;
}
