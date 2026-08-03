#include "PushNatives.hpp"

#include <algorithm>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "phoneme/push/PushRegistry.hpp"
#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

void add(NativeMethodRegistry &registry, std::string owner, std::string name,
         std::string descriptor, NativeMethod method) {
  auto registered =
      registry.register_method(std::move(owner), std::move(name),
                               std::move(descriptor), std::move(method));
  if (!registered)
    std::terminate();
}

[[nodiscard]] std::unexpected<Error> push_failure(const Error &error) {
  if (error.code == ErrorCode::invalid_argument) {
    return fail_java("java/lang/IllegalArgumentException", error.message);
  }
  return fail_java("java/io/IOException", error.message);
}

[[nodiscard]] std::unexpected<Error> alarm_failure(const Error &error) {
  if (error.code == ErrorCode::invalid_argument) {
    return fail_java("java/lang/IllegalArgumentException", error.message);
  }
  return fail_java("javax/microedition/io/ConnectionNotFoundException",
                   error.message);
}

[[nodiscard]] Result<std::string> utf8_text(Machine &machine, ObjectRef string,
                                            std::string_view label) {
  if (string.is_null()) {
    return fail_java("java/lang/NullPointerException",
                     std::string(label) + " is null");
  }
  auto text = machine.heap().string_value(string);
  if (!text)
    return std::unexpected(text.error());
  std::string result;
  result.reserve(text->size());
  for (usize index = 0; index < text->size(); ++index) {
    u32 code_point = static_cast<u16>((*text)[index]);
    if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
        index + 1U < text->size()) {
      const u32 low = static_cast<u16>((*text)[index + 1U]);
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        code_point =
            0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
        ++index;
      }
    }
    if (code_point <= 0x7FU) {
      result.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
      result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
      result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
      result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
      result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
      result.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
      result.push_back(
          static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
  }
  return result;
}

[[nodiscard]] Result<std::u16string> decode_utf8(std::string_view input) {
  std::u16string output;
  output.reserve(input.size());
  usize index = 0;
  while (index < input.size()) {
    const u8 first = static_cast<u8>(input[index++]);
    u32 code_point = 0;
    usize continuation_count = 0;
    if ((first & 0x80U) == 0U) {
      code_point = first;
    } else if ((first & 0xE0U) == 0xC0U) {
      code_point = first & 0x1FU;
      continuation_count = 1;
    } else if ((first & 0xF0U) == 0xE0U) {
      code_point = first & 0x0FU;
      continuation_count = 2;
    } else if ((first & 0xF8U) == 0xF0U) {
      code_point = first & 0x07U;
      continuation_count = 3;
    } else {
      return fail(ErrorCode::invalid_argument,
                  "push string contains invalid UTF-8");
    }
    if (continuation_count > input.size() - index) {
      return fail(ErrorCode::invalid_argument,
                  "push string contains truncated UTF-8");
    }
    for (usize continuation = 0; continuation < continuation_count;
         ++continuation) {
      const u8 byte = static_cast<u8>(input[index++]);
      if ((byte & 0xC0U) != 0x80U) {
        return fail(ErrorCode::invalid_argument,
                    "push string contains invalid UTF-8 continuation");
      }
      code_point = (code_point << 6U) | (byte & 0x3FU);
    }
    const u32 minimum =
        continuation_count == 0
            ? 0U
            : (continuation_count == 1
                   ? 0x80U
                   : (continuation_count == 2 ? 0x800U : 0x10000U));
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return fail(ErrorCode::invalid_argument,
                  "push string contains invalid UTF-8 code point");
    }
    if (code_point <= 0xFFFFU) {
      output.push_back(static_cast<char16_t>(code_point));
    } else {
      code_point -= 0x10000U;
      output.push_back(static_cast<char16_t>(0xD800U | (code_point >> 10U)));
      output.push_back(static_cast<char16_t>(0xDC00U | (code_point & 0x3FFU)));
    }
  }
  return output;
}

[[nodiscard]] Result<ObjectRef> create_string(Machine &machine,
                                              std::string_view text) {
  auto decoded = decode_utf8(text);
  if (!decoded)
    return std::unexpected(decoded.error());
  auto object = machine.class_states().allocate_instance(machine.heap(),
                                                         "java/lang/String");
  if (!object)
    return std::unexpected(object.error());
  auto attached = machine.heap().attach_string(*object, std::move(*decoded));
  if (!attached)
    return std::unexpected(attached.error());
  return *object;
}

[[nodiscard]] Result<std::string>
string_argument(Machine &machine, std::span<const Value> arguments, usize index,
                std::string_view label) {
  if (index >= arguments.size()) {
    return fail(ErrorCode::invalid_argument,
                "PushRegistry native argument is missing");
  }
  auto reference = arguments[index].as_reference();
  if (!reference)
    return std::unexpected(reference.error());
  return utf8_text(machine, *reference, label);
}

[[nodiscard]] Status require_midlet_class(Machine &machine,
                                          std::string_view class_name) {
  std::string internal_name(class_name);
  std::replace(internal_name.begin(), internal_name.end(), '.', '/');
  auto assignable = machine.classes().is_assignable(
      internal_name, "javax/microedition/midlet/MIDlet");
  if (!assignable) {
    if (assignable.error().code == ErrorCode::class_not_found) {
      return fail_java("java/lang/ClassNotFoundException",
                       "PushRegistry MIDlet class was not found: " +
                           std::string(class_name));
    }
    return std::unexpected(assignable.error());
  }
  if (!*assignable) {
    return fail_java("java/lang/ClassNotFoundException",
                     "PushRegistry target is not a MIDlet: " +
                         std::string(class_name));
  }
  return {};
}

[[nodiscard]] Status require_push_permission(Machine& machine,
                                             std::string_view resource) {
  return machine.permission_policy().require(
      security::permissions::push_registry, std::string(resource), true);
}

[[nodiscard]] Status require_connection_permission(
    Machine& machine,
    std::string_view connection) {
  std::string_view permission;
  if (connection.starts_with("socket://")) {
    permission = security::permissions::connector_socket;
  } else if (connection.starts_with("serversocket://")) {
    permission = security::permissions::connector_server_socket;
  } else if (connection.starts_with("datagram://")) {
    permission = security::permissions::connector_datagram_receiver;
  } else if (connection.starts_with("sms://")) {
    permission = security::permissions::wireless_sms_receive;
  } else if (connection.starts_with("mms://")) {
    permission = security::permissions::wireless_mms_receive;
  } else if (connection.starts_with("cbs://")) {
    permission = security::permissions::wireless_cbs_receive;
  } else {
    return fail_java("java/lang/IllegalArgumentException",
                     "unsupported PushRegistry connection scheme");
  }
  return machine.permission_policy().require(
      permission, std::string(connection), true);
}

[[nodiscard]] Result<std::optional<Value>>
optional_string_result(Machine &machine,
                       Result<std::optional<std::string>> value) {
  if (!value)
    return push_failure(value.error());
  if (!value->has_value()) {
    return std::optional<Value>(Value::from_reference({}));
  }
  auto string = create_string(machine, **value);
  if (!string)
    return std::unexpected(string.error());
  return std::optional<Value>(Value::from_reference(*string));
}

} // namespace

void register_push_natives(NativeMethodRegistry &registry) {
  add(registry, "javax/microedition/io/PushRegistry", "registerConnection",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
      [](Machine &machine,
         std::span<const Value> arguments) -> Result<std::optional<Value>> {
        auto connection =
            string_argument(machine, arguments, 0, "push connection");
        auto midlet = string_argument(machine, arguments, 1, "push MIDlet");
        auto filter = string_argument(machine, arguments, 2, "push filter");
        if (!connection)
          return std::unexpected(connection.error());
        if (!midlet)
          return std::unexpected(midlet.error());
        if (!filter)
          return std::unexpected(filter.error());
        auto valid_midlet = require_midlet_class(machine, *midlet);
        if (!valid_midlet)
          return std::unexpected(valid_midlet.error());
        auto push_allowed = require_push_permission(machine, *connection);
        if (!push_allowed) return std::unexpected(push_allowed.error());
        auto connection_allowed =
            require_connection_permission(machine, *connection);
        if (!connection_allowed) {
          return std::unexpected(connection_allowed.error());
        }
        auto registered = machine.push_registry().register_connection(
            std::move(*connection), std::move(*midlet), std::move(*filter));
        if (!registered)
          return push_failure(registered.error());
        return std::optional<Value>{};
      });

  add(registry, "javax/microedition/io/PushRegistry", "unregisterConnection",
      "(Ljava/lang/String;)Z",
      [](Machine &machine,
         std::span<const Value> arguments) -> Result<std::optional<Value>> {
        auto connection =
            string_argument(machine, arguments, 0, "push connection");
        if (!connection)
          return std::unexpected(connection.error());
        auto allowed = require_push_permission(machine, *connection);
        if (!allowed) return std::unexpected(allowed.error());
        auto removed =
            machine.push_registry().unregister_connection(*connection);
        if (!removed)
          return push_failure(removed.error());
        return std::optional<Value>(Value::from_int(*removed ? 1 : 0));
      });

  add(registry, "javax/microedition/io/PushRegistry", "listConnections",
      "(Z)[Ljava/lang/String;",
      [](Machine &machine,
         std::span<const Value> arguments) -> Result<std::optional<Value>> {
        if (arguments.empty()) {
          return fail(ErrorCode::invalid_argument,
                      "PushRegistry.listConnections flag is missing");
        }
        auto available = arguments[0].as_int();
        if (!available)
          return std::unexpected(available.error());
        auto connections =
            machine.push_registry().list_connections(*available != 0);
        if (!connections)
          return push_failure(connections.error());
        auto array = machine.heap().allocate_array("[Ljava/lang/String;",
                                                   connections->size(),
                                                   Value::from_reference({}));
        if (!array)
          return std::unexpected(array.error());
        auto array_root = machine.pin_native_root(*array);
        if (!array_root) return std::unexpected(array_root.error());
        for (usize index = 0; index < connections->size(); ++index) {
          auto string = create_string(machine, (*connections)[index]);
          if (!string)
            return std::unexpected(string.error());
          auto stored = machine.heap().set_element(
              *array, index, Value::from_reference(*string));
          if (!stored)
            return std::unexpected(stored.error());
        }
        return std::optional<Value>(Value::from_reference(*array));
      });

  add(registry, "javax/microedition/io/PushRegistry", "getMIDlet",
      "(Ljava/lang/String;)Ljava/lang/String;",
      [](Machine &machine,
         std::span<const Value> arguments) -> Result<std::optional<Value>> {
        auto connection =
            string_argument(machine, arguments, 0, "push connection");
        if (!connection)
          return std::unexpected(connection.error());
        return optional_string_result(
            machine, machine.push_registry().midlet_for(*connection));
      });

  add(registry, "javax/microedition/io/PushRegistry", "getFilter",
      "(Ljava/lang/String;)Ljava/lang/String;",
      [](Machine &machine,
         std::span<const Value> arguments) -> Result<std::optional<Value>> {
        auto connection =
            string_argument(machine, arguments, 0, "push connection");
        if (!connection)
          return std::unexpected(connection.error());
        return optional_string_result(
            machine, machine.push_registry().filter_for(*connection));
      });

  add(registry, "javax/microedition/io/PushRegistry", "registerAlarm",
      "(Ljava/lang/String;J)J",
      [](Machine &machine,
         std::span<const Value> arguments) -> Result<std::optional<Value>> {
        auto midlet =
            string_argument(machine, arguments, 0, "push alarm MIDlet");
        if (!midlet)
          return std::unexpected(midlet.error());
        if (arguments.size() < 2U) {
          return fail(ErrorCode::invalid_argument,
                      "PushRegistry.registerAlarm time is missing");
        }
        auto time = arguments[1].as_long();
        if (!time)
          return std::unexpected(time.error());
        auto valid_midlet = require_midlet_class(machine, *midlet);
        if (!valid_midlet)
          return std::unexpected(valid_midlet.error());
        auto allowed = require_push_permission(machine, *midlet);
        if (!allowed) return std::unexpected(allowed.error());
        auto previous =
            machine.push_registry().register_alarm(std::move(*midlet), *time);
        if (!previous)
          return alarm_failure(previous.error());
        return std::optional<Value>(Value::from_long(*previous));
      });
}

} // namespace phoneme::vm
