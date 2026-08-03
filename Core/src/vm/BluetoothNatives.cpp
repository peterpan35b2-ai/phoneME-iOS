#include "BluetoothNatives.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr const char* kLocalDevice = "javax/bluetooth/LocalDevice";
constexpr const char* kDiscoveryAgent = "javax/bluetooth/DiscoveryAgent";
constexpr const char* kDiscoveryListener = "javax/bluetooth/DiscoveryListener";
constexpr const char* kUuid = "javax/bluetooth/UUID";
constexpr const char* kRemoteDevice = "javax/bluetooth/RemoteDevice";
constexpr const char* kDeviceClass = "javax/bluetooth/DeviceClass";
constexpr const char* kDataElement = "javax/bluetooth/DataElement";
constexpr const char* kBluetoothStateException =
    "javax/bluetooth/BluetoothStateException";
constexpr const char* kBluetoothConnectionException =
    "javax/bluetooth/BluetoothConnectionException";
constexpr const char* kServiceRegistrationException =
    "javax/bluetooth/ServiceRegistrationException";
constexpr const char* kVector = "java/util/Vector";

constexpr i32 kNotDiscoverable = 0;
constexpr i32 kGiac = 0x009E8B33;
constexpr i32 kLiac = 0x009E8B00;
constexpr i32 kCached = 0;
constexpr i32 kPreknown = 1;
constexpr i32 kInquiryCompleted = 0;
constexpr i32 kServiceSearchNoRecords = 4;

constexpr i32 kDataNull = 0x00;
constexpr i32 kUnsignedInt1 = 0x08;
constexpr i32 kUnsignedInt2 = 0x09;
constexpr i32 kUnsignedInt4 = 0x0A;
constexpr i32 kUnsignedInt8 = 0x0B;
constexpr i32 kUnsignedInt16 = 0x0C;
constexpr i32 kSignedInt1 = 0x10;
constexpr i32 kSignedInt2 = 0x11;
constexpr i32 kSignedInt4 = 0x12;
constexpr i32 kSignedInt8 = 0x13;
constexpr i32 kSignedInt16 = 0x14;
constexpr i32 kDataUuid = 0x18;
constexpr i32 kDataString = 0x20;
constexpr i32 kDataBool = 0x28;
constexpr i32 kDataSequence = 0x30;
constexpr i32 kDataAlternative = 0x38;
constexpr i32 kDataUrl = 0x40;

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod implementation) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(implementation));
    if (!registered) std::abort();
}

[[nodiscard]] Result<ObjectRef> receiver(
    std::span<const Value> arguments,
    std::string_view operation) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " receiver is missing");
    }
    auto object = arguments.front().as_reference();
    if (!object) return std::unexpected(object.error());
    if (object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation,
    bool nullable = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " argument is missing");
    }
    auto object = arguments[index].as_reference();
    if (!object) return std::unexpected(object.error());
    if (!nullable && object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " argument is null");
    }
    return *object;
}

[[nodiscard]] Result<i32> int_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " int argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<i64> long_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " long argument is missing");
    }
    return arguments[index].as_long();
}

[[nodiscard]] Result<FieldLocation> field_location(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    bool is_static = false) {
    return machine.class_states().resolve_field(owner, name, descriptor,
                                                is_static);
}

[[nodiscard]] Status set_reference_field(Machine& machine,
                                         ObjectRef object,
                                         std::string_view owner,
                                         std::string_view name,
                                         std::string_view descriptor,
                                         ObjectRef value) {
    auto field = field_location(machine, owner, name, descriptor);
    if (!field) return std::unexpected(field.error());
    return machine.heap().set_field(object, field->index,
                                    Value::from_reference(value));
}

[[nodiscard]] Result<ObjectRef> reference_field(Machine& machine,
                                                ObjectRef object,
                                                std::string_view owner,
                                                std::string_view name,
                                                std::string_view descriptor) {
    auto field = field_location(machine, owner, name, descriptor);
    if (!field) return std::unexpected(field.error());
    auto value = machine.heap().field(object, field->index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Status set_int_field(Machine& machine,
                                   ObjectRef object,
                                   std::string_view owner,
                                   std::string_view name,
                                   i32 value) {
    auto field = field_location(machine, owner, name, "I");
    if (!field) return std::unexpected(field.error());
    return machine.heap().set_field(object, field->index,
                                    Value::from_int(value));
}

[[nodiscard]] Result<i32> int_field(Machine& machine,
                                    ObjectRef object,
                                    std::string_view owner,
                                    std::string_view name) {
    auto field = field_location(machine, owner, name, "I");
    if (!field) return std::unexpected(field.error());
    auto value = machine.heap().field(object, field->index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Status set_long_field(Machine& machine,
                                    ObjectRef object,
                                    std::string_view owner,
                                    std::string_view name,
                                    i64 value) {
    auto field = field_location(machine, owner, name, "J");
    if (!field) return std::unexpected(field.error());
    return machine.heap().set_field(object, field->index,
                                    Value::from_long(value));
}

[[nodiscard]] Result<i64> long_field(Machine& machine,
                                     ObjectRef object,
                                     std::string_view owner,
                                     std::string_view name) {
    auto field = field_location(machine, owner, name, "J");
    if (!field) return std::unexpected(field.error());
    auto value = machine.heap().field(object, field->index);
    if (!value) return std::unexpected(value.error());
    return value->as_long();
}

[[nodiscard]] Status set_static_int(Machine& machine,
                                    std::string_view owner,
                                    std::string_view name,
                                    i32 value) {
    auto field = field_location(machine, owner, name, "I", true);
    if (!field) return std::unexpected(field.error());
    return machine.class_states().set_static_field(*field,
                                                   Value::from_int(value));
}

[[nodiscard]] Result<std::optional<Value>> invoke_instance_checked(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments = {}) {
    auto result = machine.invoke_instance(object, owner, name, descriptor,
                                          arguments);
    if (!result) return std::unexpected(result.error());
    if (!result->completed_normally()) {
        return fail(ErrorCode::java_exception,
                    std::string(owner) + "." + std::string(name) +
                        " threw while servicing Bluetooth native");
    }
    return result->return_value;
}

[[nodiscard]] Status schedule_listener_callback(
    Machine& machine,
    ObjectRef listener,
    std::string method,
    std::string descriptor,
    std::vector<Value> arguments) {
    auto thread_root = machine.allocate_pinned_instance("java/lang/Thread");
    if (!thread_root) return std::unexpected(thread_root.error());
    auto thread = thread_root->get();
    if (!thread) return std::unexpected(thread.error());

    // Store the listener as the native thread's target. Scheduler GC root
    // enumeration keeps both the thread object and listener alive until the
    // callback finishes, without requiring a separate global callback table.
    auto initialized = machine.initialize_java_thread(*thread, listener);
    if (!initialized) return initialized;

    return machine.scheduler().start_native_thread(
        machine, *thread,
        [&machine,
         listener,
         method = std::move(method),
         descriptor = std::move(descriptor),
         arguments = std::move(arguments)](std::stop_token stop_token) mutable
            -> Result<std::optional<ObjectRef>> {
            if (stop_token.stop_requested()) {
                return std::optional<ObjectRef> {};
            }
            auto callback = invoke_instance_checked(
                machine, listener, kDiscoveryListener, method, descriptor,
                arguments);
            if (!callback) return std::unexpected(callback.error());
            return std::optional<ObjectRef> {};
        });
}

[[nodiscard]] Status initialize_vector(Machine& machine,
                                       ObjectRef element) {
    auto vector = machine.class_states().allocate_instance(machine.heap(),
                                                           kVector);
    if (!vector) return std::unexpected(vector.error());
    auto initialized = invoke_instance_checked(machine, *vector, kVector,
                                               "<init>", "()V");
    if (!initialized) return std::unexpected(initialized.error());
    return set_reference_field(machine, element, kDataElement, "sequence",
                               "Ljava/util/Vector;", *vector);
}

[[nodiscard]] bool is_sequence_type(i32 type) noexcept {
    return type == kDataSequence || type == kDataAlternative;
}

[[nodiscard]] bool is_long_type(i32 type) noexcept {
    switch (type) {
    case kUnsignedInt1:
    case kUnsignedInt2:
    case kUnsignedInt4:
    case kSignedInt1:
    case kSignedInt2:
    case kSignedInt4:
    case kSignedInt8:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_byte_array_type(i32 type) noexcept {
    return type == kUnsignedInt8 || type == kUnsignedInt16 ||
           type == kSignedInt16;
}

[[nodiscard]] Result<usize> expected_byte_array_length(i32 type) {
    switch (type) {
    case kUnsignedInt8: return 8U;
    case kUnsignedInt16:
    case kSignedInt16: return 16U;
    default:
        return fail(ErrorCode::invalid_argument,
                    "Bluetooth data type is not byte-array backed");
    }
}

[[nodiscard]] Status validate_long_value(i32 type, i64 value) {
    switch (type) {
    case kUnsignedInt1:
        if (value < 0 || value > 0xFF) break;
        return {};
    case kUnsignedInt2:
        if (value < 0 || value > 0xFFFF) break;
        return {};
    case kUnsignedInt4:
        if (value < 0 || static_cast<u64>(value) > 0xFFFF'FFFFULL) break;
        return {};
    case kSignedInt1:
        if (value < std::numeric_limits<i8>::min() ||
            value > std::numeric_limits<i8>::max()) break;
        return {};
    case kSignedInt2:
        if (value < std::numeric_limits<i16>::min() ||
            value > std::numeric_limits<i16>::max()) break;
        return {};
    case kSignedInt4:
        if (value < std::numeric_limits<i32>::min() ||
            value > std::numeric_limits<i32>::max()) break;
        return {};
    case kSignedInt8:
        return {};
    default:
        return fail_java("java/lang/IllegalArgumentException",
                         "DataElement type cannot be constructed from long");
    }
    return fail_java("java/lang/IllegalArgumentException",
                     "DataElement integer value is outside its type range");
}

[[nodiscard]] Result<ObjectRef> sequence_vector(Machine& machine,
                                                ObjectRef element,
                                                std::string_view operation) {
    auto type = int_field(machine, element, kDataElement, "type");
    if (!type) return std::unexpected(type.error());
    if (!is_sequence_type(*type)) {
        return fail_java("java/lang/ClassCastException",
                         std::string(operation) +
                             " requires DATSEQ or DATALT");
    }
    auto vector = reference_field(machine, element, kDataElement, "sequence",
                                  "Ljava/util/Vector;");
    if (!vector) return std::unexpected(vector.error());
    if (vector->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "Bluetooth sequence has no Vector storage");
    }
    return *vector;
}

[[nodiscard]] Result<ObjectRef> create_java_string(Machine& machine,
                                                   std::u16string text) {
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] std::u16string ascii_string(std::string_view text) {
    std::u16string result;
    result.reserve(text.size());
    for (const char character : text) {
        result.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

[[nodiscard]] Result<ObjectRef> create_ascii_string(Machine& machine,
                                                    std::string_view text) {
    return create_java_string(machine, ascii_string(text));
}


[[nodiscard]] Result<ObjectRef> local_device(Machine& machine) {
    auto singleton_field = field_location(
        machine, kLocalDevice, "singleton", "Ljavax/bluetooth/LocalDevice;",
        true);
    if (!singleton_field) return std::unexpected(singleton_field.error());
    auto current = machine.class_states().static_field(*singleton_field);
    if (!current) return std::unexpected(current.error());
    auto current_reference = current->as_reference();
    if (!current_reference) return std::unexpected(current_reference.error());
    if (!current_reference->is_null()) return *current_reference;

    auto object = machine.class_states().allocate_instance(machine.heap(),
                                                           kLocalDevice);
    if (!object) return std::unexpected(object.error());
    auto root = machine.pin_native_root(*object);
    if (!root) return std::unexpected(root.error());
    auto agent = machine.class_states().allocate_instance(machine.heap(),
                                                          kDiscoveryAgent);
    if (!agent) return std::unexpected(agent.error());
    auto stored_agent = set_reference_field(
        machine, *object, kLocalDevice, "agent",
        "Ljavax/bluetooth/DiscoveryAgent;", *agent);
    if (!stored_agent) return std::unexpected(stored_agent.error());
    auto stored_mode = set_int_field(machine, *object, kLocalDevice,
                                     "discoverable", 0);
    if (!stored_mode) return std::unexpected(stored_mode.error());
    auto stored_singleton = machine.class_states().set_static_field(
        *singleton_field, Value::from_reference(*object));
    if (!stored_singleton) return std::unexpected(stored_singleton.error());
    return *object;
}

[[nodiscard]] bool ascii_hex(char16_t character) noexcept {
    return (character >= u'0' && character <= u'9') ||
           (character >= u'a' && character <= u'f') ||
           (character >= u'A' && character <= u'F');
}

[[nodiscard]] Result<std::u16string> normalized_uuid(
    std::u16string text,
    bool short_uuid) {
    text.erase(std::remove(text.begin(), text.end(), u'-'), text.end());
    for (char16_t& character : text) {
        if (!ascii_hex(character)) {
            return fail_java("java/lang/IllegalArgumentException",
                             "Bluetooth UUID contains a non-hex digit");
        }
        if (character >= u'A' && character <= u'F') {
            character = static_cast<char16_t>(character + (u'a' - u'A'));
        }
    }
    if (short_uuid) {
        if (text.empty() || text.size() > 8U) {
            return fail_java("java/lang/IllegalArgumentException",
                             "short Bluetooth UUID exceeds 32 bits");
        }
        text.insert(text.begin(), 8U - text.size(), u'0');
        text.append(u"00001000800000805f9b34fb");
    } else if (text.size() != 32U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Bluetooth UUID must contain 32 hex digits");
    }
    return text;
}

[[nodiscard]] Result<std::u16string> normalized_address(
    std::u16string text) {
    if (text.size() != 12U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Bluetooth address must contain 12 hexadecimal digits");
    }
    for (char16_t& character : text) {
        if (!ascii_hex(character)) {
            return fail_java("java/lang/IllegalArgumentException",
                             "Bluetooth address contains a non-hex digit");
        }
        if (character >= u'a' && character <= u'f') {
            character = static_cast<char16_t>(character - (u'a' - u'A'));
        }
    }
    return text;
}

[[nodiscard]] Result<std::u16string> uuid_text(Machine& machine,
                                              ObjectRef uuid) {
    auto value = reference_field(machine, uuid, kUuid, "value",
                                 "Ljava/lang/String;");
    if (!value) return std::unexpected(value.error());
    if (value->is_null()) {
        return fail(ErrorCode::invalid_state,
                    "Bluetooth UUID has no value");
    }
    return machine.heap().string_value(*value);
}

[[nodiscard]] Status initialize_uuid(Machine& machine,
                                     ObjectRef object,
                                     std::u16string text) {
    auto string = create_java_string(machine, std::move(text));
    if (!string) return std::unexpected(string.error());
    return set_reference_field(machine, object, kUuid, "value",
                               "Ljava/lang/String;", *string);
}

[[nodiscard]] Result<std::optional<Value>> noop_constructor(
    std::span<const Value> arguments,
    std::string_view operation) {
    auto object = receiver(arguments, operation);
    if (!object) return std::unexpected(object.error());
    return std::optional<Value> {};
}

[[nodiscard]] Result<std::optional<Value>> false_result() {
    return std::optional<Value>(Value::from_int(0));
}

} // namespace

void register_bluetooth_natives(NativeMethodRegistry& registry) {
    add(registry, kBluetoothConnectionException, "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            static constexpr std::array<std::pair<std::string_view, i32>, 6>
                constants {{
                    {"UNKNOWN_PSM", 1},
                    {"SECURITY_BLOCK", 2},
                    {"NO_RESOURCES", 3},
                    {"FAILED_NOINFO", 4},
                    {"TIMEOUT", 5},
                    {"UNACCEPTABLE_PARAMS", 6},
                }};
            for (const auto& [name, value] : constants) {
                auto stored = set_static_int(machine,
                                             kBluetoothConnectionException,
                                             name, value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kDiscoveryListener, "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            static constexpr std::array<std::pair<std::string_view, i32>, 8>
                constants {{
                    {"INQUIRY_COMPLETED", 0},
                    {"INQUIRY_TERMINATED", 5},
                    {"INQUIRY_ERROR", 7},
                    {"SERVICE_SEARCH_COMPLETED", 1},
                    {"SERVICE_SEARCH_TERMINATED", 2},
                    {"SERVICE_SEARCH_ERROR", 3},
                    {"SERVICE_SEARCH_NO_RECORDS", 4},
                    {"SERVICE_SEARCH_DEVICE_NOT_REACHABLE", 6},
                }};
            for (const auto& [name, value] : constants) {
                auto stored = set_static_int(machine, kDiscoveryListener,
                                             name, value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kDiscoveryAgent, "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            static constexpr std::array<std::pair<std::string_view, i32>, 5>
                constants {{
                    {"NOT_DISCOVERABLE", kNotDiscoverable},
                    {"GIAC", kGiac},
                    {"LIAC", kLiac},
                    {"CACHED", kCached},
                    {"PREKNOWN", kPreknown},
                }};
            for (const auto& [name, value] : constants) {
                auto stored = set_static_int(machine, kDiscoveryAgent,
                                             name, value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            static constexpr std::array<std::pair<std::string_view, i32>, 17>
                constants {{
                    {"NULL", kDataNull},
                    {"U_INT_1", kUnsignedInt1},
                    {"U_INT_2", kUnsignedInt2},
                    {"U_INT_4", kUnsignedInt4},
                    {"U_INT_8", kUnsignedInt8},
                    {"U_INT_16", kUnsignedInt16},
                    {"INT_1", kSignedInt1},
                    {"INT_2", kSignedInt2},
                    {"INT_4", kSignedInt4},
                    {"INT_8", kSignedInt8},
                    {"INT_16", kSignedInt16},
                    {"UUID", kDataUuid},
                    {"STRING", kDataString},
                    {"BOOL", kDataBool},
                    {"DATSEQ", kDataSequence},
                    {"DATALT", kDataAlternative},
                    {"URL", kDataUrl},
                }};
            for (const auto& [name, value] : constants) {
                auto stored = set_static_int(machine, kDataElement,
                                             name, value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "javax/bluetooth/ServiceRecord", "<clinit>", "()V",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            static constexpr std::array<std::pair<std::string_view, i32>, 3>
                constants {{
                    {"NOAUTHENTICATE_NOENCRYPT", 0},
                    {"AUTHENTICATE_NOENCRYPT", 1},
                    {"AUTHENTICATE_ENCRYPT", 2},
                }};
            for (const auto& [name, value] : constants) {
                auto stored = set_static_int(machine,
                                             "javax/bluetooth/ServiceRecord",
                                             name, value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    add(registry, kBluetoothStateException, "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            return noop_constructor(arguments, "BluetoothStateException.<init>");
        });
    add(registry, kBluetoothStateException, "<init>",
        "(Ljava/lang/String;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            return noop_constructor(arguments, "BluetoothStateException.<init>");
        });
    add(registry, kServiceRegistrationException, "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            return noop_constructor(arguments,
                                    "ServiceRegistrationException.<init>");
        });
    add(registry, kServiceRegistrationException, "<init>",
        "(Ljava/lang/String;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            return noop_constructor(arguments,
                                    "ServiceRegistrationException.<init>");
        });
    const auto connection_exception_constructor = [&registry](
        const char* descriptor) {
        add(registry, kBluetoothConnectionException, "<init>", descriptor,
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments,
                                       "BluetoothConnectionException.<init>");
                auto status = int_argument(arguments, 1U,
                                           "BluetoothConnectionException.<init>");
                if (!object) return std::unexpected(object.error());
                if (!status) return std::unexpected(status.error());
                auto stored = set_int_field(machine, *object,
                                            kBluetoothConnectionException,
                                            "status", *status);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    connection_exception_constructor("(I)V");
    connection_exception_constructor("(ILjava/lang/String;)V");
    add(registry, kBluetoothConnectionException, "getStatus", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments,
                                   "BluetoothConnectionException.getStatus");
            if (!object) return std::unexpected(object.error());
            auto status = int_field(machine, *object,
                                    kBluetoothConnectionException, "status");
            if (!status) return std::unexpected(status.error());
            return std::optional<Value>(Value::from_int(*status));
        });

    add(registry, kLocalDevice, "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            return noop_constructor(arguments, "LocalDevice.<init>");
        });
    add(registry, kLocalDevice, "getLocalDevice",
        "()Ljavax/bluetooth/LocalDevice;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto device = local_device(machine);
            if (!device) return std::unexpected(device.error());
            return std::optional<Value>(Value::from_reference(*device));
        });
    add(registry, kLocalDevice, "isPowerOn", "()Z",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, kLocalDevice, "getDiscoveryAgent",
        "()Ljavax/bluetooth/DiscoveryAgent;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.getDiscoveryAgent");
            if (!object) return std::unexpected(object.error());
            auto agent = reference_field(machine, *object, kLocalDevice,
                                         "agent",
                                         "Ljavax/bluetooth/DiscoveryAgent;");
            if (!agent) return std::unexpected(agent.error());
            return std::optional<Value>(Value::from_reference(*agent));
        });
    add(registry, kLocalDevice, "setDiscoverable", "(I)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.setDiscoverable");
            auto mode = int_argument(arguments, 1U,
                                     "LocalDevice.setDiscoverable");
            if (!object) return std::unexpected(object.error());
            if (!mode) return std::unexpected(mode.error());
            if (*mode != kNotDiscoverable && *mode != kGiac &&
                *mode != kLiac) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "unsupported Bluetooth discoverable mode");
            }
            auto stored = set_int_field(machine, *object, kLocalDevice,
                                        "discoverable", *mode);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, kLocalDevice, "getDiscoverable", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.getDiscoverable");
            if (!object) return std::unexpected(object.error());
            auto mode = int_field(machine, *object, kLocalDevice,
                                  "discoverable");
            if (!mode) return std::unexpected(mode.error());
            return std::optional<Value>(Value::from_int(*mode));
        });
    add(registry, kLocalDevice, "getFriendlyName", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.getFriendlyName");
            if (!object) return std::unexpected(object.error());
            auto name = create_ascii_string(machine, "phoneME iOS");
            if (!name) return std::unexpected(name.error());
            return std::optional<Value>(Value::from_reference(*name));
        });
    add(registry, kLocalDevice, "getBluetoothAddress", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments,
                                   "LocalDevice.getBluetoothAddress");
            if (!object) return std::unexpected(object.error());
            auto address = create_ascii_string(machine, "000000000000");
            if (!address) return std::unexpected(address.error());
            return std::optional<Value>(Value::from_reference(*address));
        });
    add(registry, kLocalDevice, "getDeviceClass",
        "()Ljavax/bluetooth/DeviceClass;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.getDeviceClass");
            if (!object) return std::unexpected(object.error());
            auto device_class = machine.class_states().allocate_instance(
                machine.heap(), kDeviceClass);
            if (!device_class) return std::unexpected(device_class.error());
            auto stored = set_int_field(machine, *device_class,
                                        kDeviceClass, "record", 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*device_class));
        });
    add(registry, kLocalDevice, "getProperty",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto key = reference_argument(arguments, 0U,
                                          "LocalDevice.getProperty", false);
            if (!key) return std::unexpected(key.error());
            auto text = machine.heap().string_value(*key);
            if (!text) return std::unexpected(text.error());
            std::string_view value;
            if (*text == u"bluetooth.api.version") value = "1.1";
            else if (*text == u"bluetooth.connected.devices.max") value = "0";
            else if (*text == u"bluetooth.sd.trans.max") value = "1";
            else if (*text == u"bluetooth.connected.inquiry.scan") value = "false";
            else if (*text == u"bluetooth.connected.page.scan") value = "false";
            else if (*text == u"bluetooth.connected.inquiry") value = "false";
            else if (*text == u"bluetooth.connected.page") value = "false";
            else return std::optional<Value>(Value::from_reference({}));
            auto result = create_ascii_string(machine, value);
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_reference(*result));
        });
    add(registry, kLocalDevice, "getRecord",
        "(Ljavax/microedition/io/Connection;)Ljavax/bluetooth/ServiceRecord;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.getRecord");
            auto connection = reference_argument(arguments, 1U,
                                                  "LocalDevice.getRecord",
                                                  false);
            if (!object) return std::unexpected(object.error());
            if (!connection) return std::unexpected(connection.error());
            return fail_java("java/lang/IllegalArgumentException",
                             "connection is not a Bluetooth notifier");
        });
    add(registry, kLocalDevice, "updateRecord",
        "(Ljavax/bluetooth/ServiceRecord;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "LocalDevice.updateRecord");
            auto record = reference_argument(arguments, 1U,
                                              "LocalDevice.updateRecord",
                                              false);
            if (!object) return std::unexpected(object.error());
            if (!record) return std::unexpected(record.error());
            return fail_java(kServiceRegistrationException,
                             "Bluetooth service registration adapter is unavailable");
        });

    add(registry, kDiscoveryAgent, "retrieveDevices",
        "(I)[Ljavax/bluetooth/RemoteDevice;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DiscoveryAgent.retrieveDevices");
            auto option = int_argument(arguments, 1U,
                                       "DiscoveryAgent.retrieveDevices");
            if (!object) return std::unexpected(object.error());
            if (!option) return std::unexpected(option.error());
            if (*option != kCached && *option != kPreknown) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "retrieveDevices option must be CACHED or PREKNOWN");
            }
            return std::optional<Value>(Value::from_reference({}));
        });
    add(registry, kDiscoveryAgent, "startInquiry",
        "(ILjavax/bluetooth/DiscoveryListener;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DiscoveryAgent.startInquiry");
            auto access_code = int_argument(arguments, 1U,
                                            "DiscoveryAgent.startInquiry");
            auto listener = reference_argument(arguments, 2U,
                                               "DiscoveryAgent.startInquiry",
                                               false);
            if (!object) return std::unexpected(object.error());
            if (!access_code) return std::unexpected(access_code.error());
            if (!listener) return std::unexpected(listener.error());
            if (*access_code < kLiac || *access_code > kGiac) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Bluetooth inquiry access code is invalid");
            }
            auto scheduled = schedule_listener_callback(
                machine, *listener, "inquiryCompleted", "(I)V",
                {Value::from_int(kInquiryCompleted)});
            if (!scheduled) return std::unexpected(scheduled.error());
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, kDiscoveryAgent, "cancelInquiry",
        "(Ljavax/bluetooth/DiscoveryListener;)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DiscoveryAgent.cancelInquiry");
            if (!object) return std::unexpected(object.error());
            return false_result();
        });
    add(registry, kDiscoveryAgent, "searchServices",
        "([I[Ljavax/bluetooth/UUID;Ljavax/bluetooth/RemoteDevice;"
        "Ljavax/bluetooth/DiscoveryListener;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DiscoveryAgent.searchServices");
            auto uuid_set = reference_argument(arguments, 2U,
                                               "DiscoveryAgent.searchServices",
                                               false);
            auto remote = reference_argument(arguments, 3U,
                                             "DiscoveryAgent.searchServices",
                                             false);
            auto listener = reference_argument(arguments, 4U,
                                               "DiscoveryAgent.searchServices",
                                               false);
            if (!object) return std::unexpected(object.error());
            if (!uuid_set) return std::unexpected(uuid_set.error());
            if (!remote) return std::unexpected(remote.error());
            if (!listener) return std::unexpected(listener.error());
            auto uuid_count = machine.heap().array_length(*uuid_set);
            if (!uuid_count) return std::unexpected(uuid_count.error());
            if (*uuid_count == 0U) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Bluetooth UUID set must not be empty");
            }
            auto transaction = int_field(machine, *object, kDiscoveryAgent,
                                         "nextTransaction");
            if (!transaction) return std::unexpected(transaction.error());
            i32 next = *transaction == std::numeric_limits<i32>::max()
                ? 1
                : *transaction + 1;
            if (next <= 0) next = 1;
            auto stored = set_int_field(machine, *object, kDiscoveryAgent,
                                        "nextTransaction", next);
            if (!stored) return std::unexpected(stored.error());
            auto scheduled = schedule_listener_callback(
                machine, *listener, "serviceSearchCompleted", "(II)V",
                {Value::from_int(next),
                 Value::from_int(kServiceSearchNoRecords)});
            if (!scheduled) return std::unexpected(scheduled.error());
            return std::optional<Value>(Value::from_int(next));
        });
    add(registry, kDiscoveryAgent, "cancelServiceSearch", "(I)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments,
                                   "DiscoveryAgent.cancelServiceSearch");
            if (!object) return std::unexpected(object.error());
            return false_result();
        });
    add(registry, kDiscoveryAgent, "selectService",
        "(Ljavax/bluetooth/UUID;IZ)Ljava/lang/String;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DiscoveryAgent.selectService");
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_reference({}));
        });

    add(registry, kUuid, "<init>", "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "UUID.<init>");
            auto value = long_argument(arguments, 1U, "UUID.<init>");
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            if (*value < 0 || static_cast<u64>(*value) > 0xFFFF'FFFFULL) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Bluetooth UUID long is outside 32 bits");
            }
            std::array<char, 9> buffer {};
            auto converted = std::to_chars(buffer.data(), buffer.data() + 8,
                                           static_cast<u32>(*value), 16);
            if (converted.ec != std::errc {}) {
                return fail(ErrorCode::internal_error,
                            "failed to format Bluetooth UUID");
            }
            std::string short_text(buffer.data(), converted.ptr);
            short_text.insert(short_text.begin(), 8U - short_text.size(), '0');
            auto normalized = normalized_uuid(ascii_string(short_text), true);
            if (!normalized) return std::unexpected(normalized.error());
            auto initialized = initialize_uuid(machine, *object,
                                               std::move(*normalized));
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, kUuid, "<init>", "(Ljava/lang/String;Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "UUID.<init>");
            auto source = reference_argument(arguments, 1U,
                                             "UUID.<init>", false);
            auto short_uuid = int_argument(arguments, 2U, "UUID.<init>");
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            if (!short_uuid) return std::unexpected(short_uuid.error());
            auto text = machine.heap().string_value(*source);
            if (!text) return std::unexpected(text.error());
            auto normalized = normalized_uuid(std::move(*text),
                                              *short_uuid != 0);
            if (!normalized) return std::unexpected(normalized.error());
            auto initialized = initialize_uuid(machine, *object,
                                               std::move(*normalized));
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, kUuid, "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "UUID.toString");
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, kUuid, "value",
                                         "Ljava/lang/String;");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
    add(registry, kUuid, "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "UUID.hashCode");
            if (!object) return std::unexpected(object.error());
            auto text = uuid_text(machine, *object);
            if (!text) return std::unexpected(text.error());
            i32 hash = 0;
            for (char16_t character : *text) {
                hash = static_cast<i32>(static_cast<u32>(hash) * 31U +
                                        static_cast<u16>(character));
            }
            return std::optional<Value>(Value::from_int(hash));
        });
    add(registry, kUuid, "equals", "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "UUID.equals");
            auto other = reference_argument(arguments, 1U, "UUID.equals");
            if (!object) return std::unexpected(object.error());
            if (!other) return std::unexpected(other.error());
            if (other->is_null()) return false_result();
            auto is_uuid = machine.object_is_instance(*other, kUuid);
            if (!is_uuid) return std::unexpected(is_uuid.error());
            if (!*is_uuid) return false_result();
            auto left = uuid_text(machine, *object);
            auto right = uuid_text(machine, *other);
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            return std::optional<Value>(Value::from_int(*left == *right ? 1 : 0));
        });

    add(registry, kRemoteDevice, "<init>", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.<init>");
            auto address = reference_argument(arguments, 1U,
                                              "RemoteDevice.<init>", false);
            if (!object) return std::unexpected(object.error());
            if (!address) return std::unexpected(address.error());
            auto text = machine.heap().string_value(*address);
            if (!text) return std::unexpected(text.error());
            auto normalized = normalized_address(std::move(*text));
            if (!normalized) return std::unexpected(normalized.error());
            auto canonical = create_java_string(machine, std::move(*normalized));
            if (!canonical) return std::unexpected(canonical.error());
            auto stored = set_reference_field(machine, *object, kRemoteDevice,
                "address", "Ljava/lang/String;", *canonical);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kRemoteDevice, "getRemoteDevice",
        "(Ljavax/microedition/io/Connection;)Ljavax/bluetooth/RemoteDevice;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto connection = reference_argument(arguments, 0U,
                                                  "RemoteDevice.getRemoteDevice",
                                                  false);
            if (!connection) return std::unexpected(connection.error());
            return fail_java("java/io/IOException",
                             "Bluetooth connection adapter is unavailable");
        });
    add(registry, kRemoteDevice, "getBluetoothAddress",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments,
                                   "RemoteDevice.getBluetoothAddress");
            if (!object) return std::unexpected(object.error());
            auto address = reference_field(machine, *object, kRemoteDevice,
                                           "address", "Ljava/lang/String;");
            if (!address) return std::unexpected(address.error());
            return std::optional<Value>(Value::from_reference(*address));
        });
    add(registry, kRemoteDevice, "getFriendlyName", "(Z)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.getFriendlyName");
            if (!object) return std::unexpected(object.error());
            auto address = reference_field(machine, *object, kRemoteDevice,
                                           "address", "Ljava/lang/String;");
            if (!address) return std::unexpected(address.error());
            return std::optional<Value>(Value::from_reference(*address));
        });
    for (const char* method_name : {
             "isTrustedDevice", "authenticate", "isAuthenticated",
             "isEncrypted"}) {
        add(registry, kRemoteDevice, method_name, "()Z",
            [method_name](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, method_name);
                if (!object) return std::unexpected(object.error());
                return false_result();
            });
    }
    add(registry, kRemoteDevice, "authorize",
        "(Ljavax/microedition/io/Connection;)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.authorize");
            if (!object) return std::unexpected(object.error());
            return false_result();
        });
    add(registry, kRemoteDevice, "isAuthorized",
        "(Ljavax/microedition/io/Connection;)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.isAuthorized");
            if (!object) return std::unexpected(object.error());
            return false_result();
        });
    add(registry, kRemoteDevice, "encrypt",
        "(Ljavax/microedition/io/Connection;Z)Z",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.encrypt");
            if (!object) return std::unexpected(object.error());
            return false_result();
        });
    add(registry, kRemoteDevice, "equals", "(Ljava/lang/Object;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.equals");
            auto other = reference_argument(arguments, 1U,
                                            "RemoteDevice.equals");
            if (!object) return std::unexpected(object.error());
            if (!other || other->is_null()) return false_result();
            auto is_remote = machine.object_is_instance(*other, kRemoteDevice);
            if (!is_remote) return std::unexpected(is_remote.error());
            if (!*is_remote) return false_result();
            auto left = reference_field(machine, *object, kRemoteDevice,
                                        "address", "Ljava/lang/String;");
            auto right = reference_field(machine, *other, kRemoteDevice,
                                         "address", "Ljava/lang/String;");
            if (!left) return std::unexpected(left.error());
            if (!right) return std::unexpected(right.error());
            auto left_text = machine.heap().string_value(*left);
            auto right_text = machine.heap().string_value(*right);
            if (!left_text) return std::unexpected(left_text.error());
            if (!right_text) return std::unexpected(right_text.error());
            return std::optional<Value>(Value::from_int(
                *left_text == *right_text ? 1 : 0));
        });
    add(registry, kRemoteDevice, "hashCode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "RemoteDevice.hashCode");
            if (!object) return std::unexpected(object.error());
            auto address = reference_field(machine, *object, kRemoteDevice,
                                           "address", "Ljava/lang/String;");
            if (!address) return std::unexpected(address.error());
            auto text = machine.heap().string_value(*address);
            if (!text) return std::unexpected(text.error());
            i32 hash = 0;
            for (char16_t character : *text) {
                hash = static_cast<i32>(static_cast<u32>(hash) * 31U +
                                        static_cast<u16>(character));
            }
            return std::optional<Value>(Value::from_int(hash));
        });

    add(registry, kDeviceClass, "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DeviceClass.<init>");
            auto record = int_argument(arguments, 1U, "DeviceClass.<init>");
            if (!object) return std::unexpected(object.error());
            if (!record) return std::unexpected(record.error());
            auto stored = set_int_field(machine, *object, kDeviceClass,
                                        "record", *record);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    for (const auto& [method_name, mask] :
         std::array<std::pair<const char*, i32>, 3> {{
             {"getMajorDeviceClass", 0x1F00},
             {"getMinorDeviceClass", 0x00FC},
             {"getServiceClasses", 0xFFE000},
         }}) {
        add(registry, kDeviceClass, method_name, "()I",
            [method_name, mask](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, method_name);
                if (!object) return std::unexpected(object.error());
                auto record = int_field(machine, *object, kDeviceClass,
                                        "record");
                if (!record) return std::unexpected(record.error());
                return std::optional<Value>(Value::from_int(*record & mask));
            });
    }

    add(registry, kDataElement, "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.<init>");
            auto type = int_argument(arguments, 1U, "DataElement.<init>");
            if (!object) return std::unexpected(object.error());
            if (!type) return std::unexpected(type.error());
            if (*type != kDataNull && !is_sequence_type(*type)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "DataElement(int) requires NULL, DATSEQ or DATALT");
            }
            auto stored = set_int_field(machine, *object, kDataElement,
                                        "type", *type);
            if (!stored) return std::unexpected(stored.error());
            if (is_sequence_type(*type)) {
                auto initialized = initialize_vector(machine, *object);
                if (!initialized) return std::unexpected(initialized.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "<init>", "(Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.<init>");
            auto value = int_argument(arguments, 1U, "DataElement.<init>");
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored_type = set_int_field(machine, *object, kDataElement,
                                             "type", kDataBool);
            if (!stored_type) return std::unexpected(stored_type.error());
            auto stored_value = set_long_field(machine, *object, kDataElement,
                                               "longValue",
                                               *value == 0 ? 0 : 1);
            if (!stored_value) return std::unexpected(stored_value.error());
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "<init>", "(IJ)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.<init>");
            auto type = int_argument(arguments, 1U, "DataElement.<init>");
            auto value = long_argument(arguments, 2U, "DataElement.<init>");
            if (!object) return std::unexpected(object.error());
            if (!type) return std::unexpected(type.error());
            if (!value) return std::unexpected(value.error());
            auto valid = validate_long_value(*type, *value);
            if (!valid) return std::unexpected(valid.error());
            auto stored_type = set_int_field(machine, *object, kDataElement,
                                             "type", *type);
            if (!stored_type) return std::unexpected(stored_type.error());
            auto stored_value = set_long_field(machine, *object, kDataElement,
                                               "longValue", *value);
            if (!stored_value) return std::unexpected(stored_value.error());
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "<init>", "(ILjava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.<init>");
            auto type = int_argument(arguments, 1U, "DataElement.<init>");
            auto value = reference_argument(arguments, 2U,
                                            "DataElement.<init>", false);
            if (!object) return std::unexpected(object.error());
            if (!type) return std::unexpected(type.error());
            if (!value) return std::unexpected(value.error());

            if (*type == kDataUuid) {
                auto valid = machine.object_is_instance(*value, kUuid);
                if (!valid) return std::unexpected(valid.error());
                if (!*valid) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "DataElement UUID value has wrong class");
                }
            } else if (*type == kDataString || *type == kDataUrl) {
                auto valid = machine.object_is_instance(*value,
                                                        "java/lang/String");
                if (!valid) return std::unexpected(valid.error());
                if (!*valid) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "DataElement string value has wrong class");
                }
            } else if (is_byte_array_type(*type)) {
                auto class_name = machine.heap().class_name(*value);
                if (!class_name) return std::unexpected(class_name.error());
                if (*class_name != "[B") {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "DataElement integer bytes require byte[]");
                }
                auto expected = expected_byte_array_length(*type);
                if (!expected) return std::unexpected(expected.error());
                auto actual = machine.heap().array_length(*value);
                if (!actual) return std::unexpected(actual.error());
                if (*actual != *expected) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "DataElement integer byte[] has wrong length");
                }
            } else {
                return fail_java("java/lang/IllegalArgumentException",
                                 "DataElement type cannot be constructed from Object");
            }

            auto stored_type = set_int_field(machine, *object, kDataElement,
                                             "type", *type);
            if (!stored_type) return std::unexpected(stored_type.error());
            auto stored_value = set_reference_field(
                machine, *object, kDataElement, "objectValue",
                "Ljava/lang/Object;", *value);
            if (!stored_value) return std::unexpected(stored_value.error());
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "getDataType", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.getDataType");
            if (!object) return std::unexpected(object.error());
            auto type = int_field(machine, *object, kDataElement, "type");
            if (!type) return std::unexpected(type.error());
            return std::optional<Value>(Value::from_int(*type));
        });
    add(registry, kDataElement, "getLong", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.getLong");
            if (!object) return std::unexpected(object.error());
            auto type = int_field(machine, *object, kDataElement, "type");
            if (!type) return std::unexpected(type.error());
            if (!is_long_type(*type)) {
                return fail_java("java/lang/ClassCastException",
                                 "DataElement does not contain a long value");
            }
            auto value = long_field(machine, *object, kDataElement,
                                    "longValue");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_long(*value));
        });
    add(registry, kDataElement, "getBoolean", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.getBoolean");
            if (!object) return std::unexpected(object.error());
            auto type = int_field(machine, *object, kDataElement, "type");
            if (!type) return std::unexpected(type.error());
            if (*type != kDataBool) {
                return fail_java("java/lang/ClassCastException",
                                 "DataElement does not contain a boolean");
            }
            auto value = long_field(machine, *object, kDataElement,
                                    "longValue");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value != 0 ? 1 : 0));
        });
    add(registry, kDataElement, "getValue", "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.getValue");
            if (!object) return std::unexpected(object.error());
            auto type = int_field(machine, *object, kDataElement, "type");
            if (!type) return std::unexpected(type.error());
            if (*type == kDataNull) {
                return std::optional<Value>(Value::from_reference({}));
            }
            if (is_sequence_type(*type)) {
                auto vector = sequence_vector(machine, *object,
                                              "DataElement.getValue");
                if (!vector) return std::unexpected(vector.error());
                auto enumeration = invoke_instance_checked(
                    machine, *vector, kVector, "elements",
                    "()Ljava/util/Enumeration;");
                if (!enumeration) return std::unexpected(enumeration.error());
                if (!enumeration->has_value()) {
                    return fail(ErrorCode::internal_error,
                                "Vector.elements returned no value");
                }
                return *enumeration;
            }
            if (is_long_type(*type) || *type == kDataBool) {
                return fail_java("java/lang/ClassCastException",
                                 "DataElement primitive requires typed getter");
            }
            auto value = reference_field(machine, *object, kDataElement,
                                         "objectValue", "Ljava/lang/Object;");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
    add(registry, kDataElement, "getSize", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.getSize");
            if (!object) return std::unexpected(object.error());
            auto vector = sequence_vector(machine, *object,
                                          "DataElement.getSize");
            if (!vector) return std::unexpected(vector.error());
            auto size = invoke_instance_checked(machine, *vector, kVector,
                                                "size", "()I");
            if (!size) return std::unexpected(size.error());
            if (!size->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Vector.size returned no value");
            }
            auto count = (*size)->as_int();
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, kDataElement, "addElement",
        "(Ljavax/bluetooth/DataElement;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.addElement");
            auto element = reference_argument(arguments, 1U,
                                              "DataElement.addElement", false);
            if (!object) return std::unexpected(object.error());
            if (!element) return std::unexpected(element.error());
            auto vector = sequence_vector(machine, *object,
                                          "DataElement.addElement");
            if (!vector) return std::unexpected(vector.error());
            const Value value = Value::from_reference(*element);
            auto added = invoke_instance_checked(
                machine, *vector, kVector, "addElement",
                "(Ljava/lang/Object;)V",
                std::span<const Value>(&value, 1U));
            if (!added) return std::unexpected(added.error());
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "insertElementAt",
        "(Ljavax/bluetooth/DataElement;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.insertElementAt");
            auto element = reference_argument(arguments, 1U,
                                              "DataElement.insertElementAt",
                                              false);
            auto index = int_argument(arguments, 2U,
                                      "DataElement.insertElementAt");
            if (!object) return std::unexpected(object.error());
            if (!element) return std::unexpected(element.error());
            if (!index) return std::unexpected(index.error());
            auto vector = sequence_vector(machine, *object,
                                          "DataElement.insertElementAt");
            if (!vector) return std::unexpected(vector.error());
            const std::array<Value, 2> values {
                Value::from_reference(*element),
                Value::from_int(*index),
            };
            auto inserted = invoke_instance_checked(
                machine, *vector, kVector, "insertElementAt",
                "(Ljava/lang/Object;I)V", values);
            if (!inserted) return std::unexpected(inserted.error());
            return std::optional<Value> {};
        });
    add(registry, kDataElement, "removeElement",
        "(Ljavax/bluetooth/DataElement;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "DataElement.removeElement");
            auto element = reference_argument(arguments, 1U,
                                              "DataElement.removeElement",
                                              false);
            if (!object) return std::unexpected(object.error());
            if (!element) return std::unexpected(element.error());
            auto vector = sequence_vector(machine, *object,
                                          "DataElement.removeElement");
            if (!vector) return std::unexpected(vector.error());
            const Value value = Value::from_reference(*element);
            auto removed = invoke_instance_checked(
                machine, *vector, kVector, "removeElement",
                "(Ljava/lang/Object;)Z",
                std::span<const Value>(&value, 1U));
            if (!removed) return std::unexpected(removed.error());
            if (!removed->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Vector.removeElement returned no value");
            }
            auto result = (*removed)->as_int();
            if (!result) return std::unexpected(result.error());
            return std::optional<Value>(Value::from_int(*result != 0 ? 1 : 0));
        });
}

} // namespace phoneme::vm
