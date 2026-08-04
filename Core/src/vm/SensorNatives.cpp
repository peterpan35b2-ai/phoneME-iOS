#include "SensorNatives.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr std::string_view kMeasurementRange =
    "javax/microedition/sensor/MeasurementRange";
constexpr std::string_view kSensorManager =
    "javax/microedition/sensor/SensorManager";
constexpr std::string_view kChannelImpl = "phoneme/sensor/ChannelInfoImpl";
constexpr std::string_view kSensorImpl = "phoneme/sensor/SensorInfoImpl";
constexpr std::string_view kDataImpl = "phoneme/sensor/DataImpl";
constexpr std::string_view kConnectionImpl =
    "phoneme/sensor/SensorConnectionImpl";

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) std::terminate();
}

[[nodiscard]] Result<ObjectRef> receiver(std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "sensor native receiver is missing");
    }
    auto object = arguments.front().as_reference();
    if (!object) return std::unexpected(object.error());
    if (object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "sensor native receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "sensor reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!allow_null && value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "sensor reference argument is null");
    }
    return *value;
}

[[nodiscard]] Result<i32> int_argument(std::span<const Value> arguments,
                                       usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "sensor integer argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<FieldLocation> field_location(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    return machine.class_states().resolve_field(owner, name, descriptor, false);
}

[[nodiscard]] Result<Value> field_value(Machine& machine,
                                        ObjectRef object,
                                        std::string_view owner,
                                        std::string_view name,
                                        std::string_view descriptor) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().field(object, location->index);
}

[[nodiscard]] Status set_field(Machine& machine,
                               ObjectRef object,
                               std::string_view owner,
                               std::string_view name,
                               std::string_view descriptor,
                               Value value) {
    auto location = field_location(machine, owner, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index, value);
}

[[nodiscard]] Result<ObjectRef> reference_field(
    Machine& machine,
    ObjectRef object,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto value = field_value(machine, object, owner, name, descriptor);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Result<i32> integer_field(Machine& machine,
                                        ObjectRef object,
                                        std::string_view owner,
                                        std::string_view name) {
    auto value = field_value(machine, object, owner, name, "I");
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<i64> long_field(Machine& machine,
                                     ObjectRef object,
                                     std::string_view owner,
                                     std::string_view name) {
    auto value = field_value(machine, object, owner, name, "J");
    if (!value) return std::unexpected(value.error());
    return value->as_long();
}

[[nodiscard]] Result<double> double_field(Machine& machine,
                                          ObjectRef object,
                                          std::string_view owner,
                                          std::string_view name) {
    auto value = field_value(machine, object, owner, name, "D");
    if (!value) return std::unexpected(value.error());
    return value->as_double();
}

[[nodiscard]] Result<ObjectRef> allocate_instance(Machine& machine,
                                                  std::string_view class_name) {
    auto object = machine.class_states().allocate_instance(machine.heap(),
                                                           class_name);
    if (object || object.error().code != ErrorCode::overflow) return object;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.class_states().allocate_instance(machine.heap(), class_name);
}

[[nodiscard]] Result<ObjectRef> allocate_array(Machine& machine,
                                               std::string_view descriptor,
                                               usize length,
                                               Value initial) {
    auto array = machine.heap().allocate_array(std::string(descriptor), length,
                                                initial);
    if (array || array.error().code != ErrorCode::overflow) return array;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.heap().allocate_array(std::string(descriptor), length,
                                         initial);
}

[[nodiscard]] Result<ObjectRef> make_string(Machine& machine,
                                            std::u16string text) {
    auto object = allocate_instance(machine, "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] Result<std::u16string> string_value(Machine& machine,
                                                  ObjectRef object) {
    if (object.is_null()) return std::u16string {};
    return machine.heap().string_value(object);
}

[[nodiscard]] i64 now_millis() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] Result<ObjectRef> make_measurement_range(Machine& machine) {
    auto range = allocate_instance(machine, kMeasurementRange);
    if (!range) return std::unexpected(range.error());
    auto root = machine.pin_native_root(*range);
    if (!root) return std::unexpected(root.error());
    auto smallest = set_field(machine, *range, kMeasurementRange,
                              "smallestValue", "D",
                              Value::from_double(-16.0));
    auto largest = set_field(machine, *range, kMeasurementRange,
                             "largestValue", "D",
                             Value::from_double(16.0));
    auto resolution = set_field(machine, *range, kMeasurementRange,
                                "resolution", "D",
                                Value::from_double(0.001));
    if (!smallest) return std::unexpected(smallest.error());
    if (!largest) return std::unexpected(largest.error());
    if (!resolution) return std::unexpected(resolution.error());
    return *range;
}

[[nodiscard]] Result<ObjectRef> make_channel(Machine& machine,
                                             std::u16string name) {
    auto channel = allocate_instance(machine, kChannelImpl);
    if (!channel) return std::unexpected(channel.error());
    auto channel_root = machine.pin_native_root(*channel);
    if (!channel_root) return std::unexpected(channel_root.error());
    auto name_string = make_string(machine, std::move(name));
    auto unit_string = make_string(machine, u"m/s^2");
    auto range = make_measurement_range(machine);
    if (!name_string || !unit_string || !range) {
        if (!name_string) return std::unexpected(name_string.error());
        if (!unit_string) return std::unexpected(unit_string.error());
        return std::unexpected(range.error());
    }
    auto name_root = machine.pin_native_root(*name_string);
    auto unit_root = machine.pin_native_root(*unit_string);
    auto range_root = machine.pin_native_root(*range);
    if (!name_root || !unit_root || !range_root) {
        return fail(ErrorCode::internal_error,
                    "failed to root sensor channel values");
    }
    auto ranges = allocate_array(
        machine, "[Ljavax/microedition/sensor/MeasurementRange;", 1U,
        Value::from_reference({}));
    if (!ranges) return std::unexpected(ranges.error());
    auto ranges_root = machine.pin_native_root(*ranges);
    if (!ranges_root) return std::unexpected(ranges_root.error());
    auto element = machine.heap().set_element(
        *ranges, 0U, Value::from_reference(*range));
    if (!element) return std::unexpected(element.error());

    const std::array<Status, 5> stored {{
        set_field(machine, *channel, kChannelImpl, "name",
                  "Ljava/lang/String;", Value::from_reference(*name_string)),
        set_field(machine, *channel, kChannelImpl, "dataType", "I",
                  Value::from_int(1)),
        set_field(machine, *channel, kChannelImpl, "scale", "I",
                  Value::from_int(0)),
        set_field(machine, *channel, kChannelImpl, "unit",
                  "Ljava/lang/String;", Value::from_reference(*unit_string)),
        set_field(machine, *channel, kChannelImpl, "ranges",
                  "[Ljavax/microedition/sensor/MeasurementRange;",
                  Value::from_reference(*ranges)),
    }};
    for (const auto& status : stored) {
        if (!status) return std::unexpected(status.error());
    }
    return *channel;
}

[[nodiscard]] Result<ObjectRef> make_sensor_info(Machine& machine,
                                                 ObjectRef requested_url = {}) {
    auto sensor = allocate_instance(machine, kSensorImpl);
    if (!sensor) return std::unexpected(sensor.error());
    auto sensor_root = machine.pin_native_root(*sensor);
    if (!sensor_root) return std::unexpected(sensor_root.error());

    ObjectRef url = requested_url;
    NativeRootScope url_root;
    if (url.is_null()) {
        auto created = make_string(
            machine,
            u"sensor:acceleration;contextType=device;model=phoneME-iOS");
        if (!created) return std::unexpected(created.error());
        url = *created;
        auto rooted = machine.pin_native_root(url);
        if (!rooted) return std::unexpected(rooted.error());
        url_root = std::move(*rooted);
    }
    auto quantity = make_string(machine, u"acceleration");
    if (!quantity) return std::unexpected(quantity.error());
    auto quantity_root = machine.pin_native_root(*quantity);
    if (!quantity_root) return std::unexpected(quantity_root.error());

    const std::array<std::u16string, 3> channel_names {{u"axis_x", u"axis_y",
                                                       u"axis_z"}};
    auto channels = allocate_array(
        machine, "[Ljavax/microedition/sensor/ChannelInfo;",
        channel_names.size(), Value::from_reference({}));
    if (!channels) return std::unexpected(channels.error());
    auto channels_root = machine.pin_native_root(*channels);
    if (!channels_root) return std::unexpected(channels_root.error());
    std::vector<NativeRootScope> channel_roots;
    channel_roots.reserve(channel_names.size());
    for (usize index = 0; index < channel_names.size(); ++index) {
        auto channel = make_channel(machine, channel_names[index]);
        if (!channel) return std::unexpected(channel.error());
        auto root = machine.pin_native_root(*channel);
        if (!root) return std::unexpected(root.error());
        channel_roots.push_back(std::move(*root));
        auto element = machine.heap().set_element(
            *channels, index, Value::from_reference(*channel));
        if (!element) return std::unexpected(element.error());
    }

    auto url_stored = set_field(machine, *sensor, kSensorImpl, "url",
                                "Ljava/lang/String;",
                                Value::from_reference(url));
    auto quantity_stored = set_field(machine, *sensor, kSensorImpl, "quantity",
                                     "Ljava/lang/String;",
                                     Value::from_reference(*quantity));
    auto channels_stored = set_field(
        machine, *sensor, kSensorImpl, "channels",
        "[Ljavax/microedition/sensor/ChannelInfo;",
        Value::from_reference(*channels));
    if (!url_stored) return std::unexpected(url_stored.error());
    if (!quantity_stored) return std::unexpected(quantity_stored.error());
    if (!channels_stored) return std::unexpected(channels_stored.error());
    return *sensor;
}

[[nodiscard]] Result<ObjectRef> make_data(Machine& machine,
                                          ObjectRef channel,
                                          usize sample_count) {
    auto data = allocate_instance(machine, kDataImpl);
    if (!data) return std::unexpected(data.error());
    auto data_root = machine.pin_native_root(*data);
    if (!data_root) return std::unexpected(data_root.error());
    auto doubles = allocate_array(machine, "[D", sample_count,
                                  Value::from_double(0.0));
    auto integers = allocate_array(machine, "[I", sample_count,
                                   Value::from_int(0));
    auto objects = allocate_array(machine, "[Ljava/lang/Object;", sample_count,
                                  Value::from_reference({}));
    if (!doubles || !integers || !objects) {
        if (!doubles) return std::unexpected(doubles.error());
        if (!integers) return std::unexpected(integers.error());
        return std::unexpected(objects.error());
    }
    auto r1 = machine.pin_native_root(*doubles);
    auto r2 = machine.pin_native_root(*integers);
    auto r3 = machine.pin_native_root(*objects);
    if (!r1 || !r2 || !r3) {
        return fail(ErrorCode::internal_error,
                    "failed to root sensor data arrays");
    }
    const std::array<Status, 5> stored {{
        set_field(machine, *data, kDataImpl, "channel",
                  "Ljavax/microedition/sensor/ChannelInfo;",
                  Value::from_reference(channel)),
        set_field(machine, *data, kDataImpl, "doubleValues", "[D",
                  Value::from_reference(*doubles)),
        set_field(machine, *data, kDataImpl, "intValues", "[I",
                  Value::from_reference(*integers)),
        set_field(machine, *data, kDataImpl, "objectValues",
                  "[Ljava/lang/Object;", Value::from_reference(*objects)),
        set_field(machine, *data, kDataImpl, "timestamp", "J",
                  Value::from_long(now_millis())),
    }};
    for (const auto& status : stored) {
        if (!status) return std::unexpected(status.error());
    }
    return *data;
}

[[nodiscard]] Result<ObjectRef> data_for_connection(Machine& machine,
                                                    ObjectRef connection,
                                                    i32 requested_samples) {
    if (requested_samples <= 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "sensor buffer size must be positive");
    }
    const usize sample_count = static_cast<usize>(
        std::clamp(requested_samples, 1, 256));
    auto sensor = reference_field(
        machine, connection, kConnectionImpl, "sensorInfo",
        "Ljavax/microedition/sensor/SensorInfo;");
    if (!sensor) return std::unexpected(sensor.error());
    auto channels = reference_field(
        machine, *sensor, kSensorImpl, "channels",
        "[Ljavax/microedition/sensor/ChannelInfo;");
    if (!channels) return std::unexpected(channels.error());
    auto channel_count = machine.heap().array_length(*channels);
    if (!channel_count) return std::unexpected(channel_count.error());
    auto result = allocate_array(machine, "[Ljavax/microedition/sensor/Data;",
                                 *channel_count, Value::from_reference({}));
    if (!result) return std::unexpected(result.error());
    auto result_root = machine.pin_native_root(*result);
    if (!result_root) return std::unexpected(result_root.error());
    for (usize index = 0; index < *channel_count; ++index) {
        auto channel_value = machine.heap().element(*channels, index);
        if (!channel_value) return std::unexpected(channel_value.error());
        auto channel = channel_value->as_reference();
        if (!channel) return std::unexpected(channel.error());
        auto data = make_data(machine, *channel, sample_count);
        if (!data) return std::unexpected(data.error());
        auto data_root = machine.pin_native_root(*data);
        if (!data_root) return std::unexpected(data_root.error());
        auto stored = machine.heap().set_element(
            *result, index, Value::from_reference(*data));
        if (!stored) return std::unexpected(stored.error());
    }
    return *result;
}

[[nodiscard]] Status require_open(Machine& machine, ObjectRef connection) {
    auto closed = integer_field(machine, connection, kConnectionImpl, "closed");
    if (!closed) return std::unexpected(closed.error());
    if (*closed != 0) {
        return fail_java("java/io/IOException", "sensor connection is closed");
    }
    return {};
}

[[nodiscard]] Result<std::optional<Value>> reference_getter(
    Machine& machine,
    std::span<const Value> arguments,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) {
    auto object = receiver(arguments);
    if (!object) return std::unexpected(object.error());
    auto value = reference_field(machine, *object, owner, name, descriptor);
    if (!value) return std::unexpected(value.error());
    return std::optional<Value>(Value::from_reference(*value));
}

void register_measurement_range(NativeMethodRegistry& registry) {
    add(registry, std::string(kMeasurementRange), "<init>", "(DDD)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            if (arguments.size() < 4U) {
                return fail(ErrorCode::invalid_argument,
                            "MeasurementRange arguments are missing");
            }
            auto smallest = arguments[1].as_double();
            auto largest = arguments[2].as_double();
            auto resolution = arguments[3].as_double();
            if (!smallest || !largest || !resolution) {
                if (!smallest) return std::unexpected(smallest.error());
                if (!largest) return std::unexpected(largest.error());
                return std::unexpected(resolution.error());
            }
            if (*smallest > *largest || *resolution < 0.0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid sensor measurement range");
            }
            auto a = set_field(machine, *object, kMeasurementRange,
                               "smallestValue", "D",
                               Value::from_double(*smallest));
            auto b = set_field(machine, *object, kMeasurementRange,
                               "largestValue", "D",
                               Value::from_double(*largest));
            auto c = set_field(machine, *object, kMeasurementRange,
                               "resolution", "D",
                               Value::from_double(*resolution));
            if (!a) return std::unexpected(a.error());
            if (!b) return std::unexpected(b.error());
            if (!c) return std::unexpected(c.error());
            return std::optional<Value> {};
        });
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"getSmallestValue",
                                                           "smallestValue"},
             {"getLargestValue", "largestValue"},
             {"getResolution", "resolution"}}) {
        add(registry, std::string(kMeasurementRange), std::string(method),
            "()D",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto value = double_field(machine, *object, kMeasurementRange,
                                          field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_double(*value));
            });
    }
}

void register_channel_info(NativeMethodRegistry& registry) {
    for (const auto& [method, field, descriptor] : {
             std::tuple<std::string_view, std::string_view, std::string_view>{
                 "getName", "name", "Ljava/lang/String;"},
             {"getMeasurementRanges", "ranges",
              "[Ljavax/microedition/sensor/MeasurementRange;"},
             {"getUnit", "unit", "Ljava/lang/String;"}}) {
        add(registry, std::string(kChannelImpl), std::string(method),
            "()" + std::string(descriptor),
            [field, descriptor](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                return reference_getter(machine, arguments, kChannelImpl,
                                        field, descriptor);
            });
    }
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"getDataType",
                                                           "dataType"},
             {"getScale", "scale"}}) {
        add(registry, std::string(kChannelImpl), std::string(method), "()I",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto value = integer_field(machine, *object, kChannelImpl,
                                           field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    }
    add(registry, std::string(kChannelImpl), "getAccuracy", "()F",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_float(0.001F));
        });
}

void register_sensor_info(NativeMethodRegistry& registry) {
    for (const auto& [method, field, descriptor] : {
             std::tuple<std::string_view, std::string_view, std::string_view>{
                 "getChannelInfos", "channels",
                 "[Ljavax/microedition/sensor/ChannelInfo;"},
             {"getQuantity", "quantity", "Ljava/lang/String;"},
             {"getUrl", "url", "Ljava/lang/String;"}}) {
        add(registry, std::string(kSensorImpl), std::string(method),
            "()" + std::string(descriptor),
            [field, descriptor](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                return reference_getter(machine, arguments, kSensorImpl,
                                        field, descriptor);
            });
    }
    for (const auto& [method, value] : {
             std::pair<std::string_view, i32>{"getConnectionType", 1},
             {"getMaxBufferSize", 256},
             {"isAvailabilityPushSupported", 0},
             {"isAvailable", 1},
             {"isConditionPushSupported", 0}}) {
        const std::string descriptor = method.front() == 'i' ? "()Z" : "()I";
        add(registry, std::string(kSensorImpl), std::string(method), descriptor,
            [value](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                return std::optional<Value>(Value::from_int(value));
            });
    }
    for (const auto& [method, value] : {
             std::pair<std::string_view, std::u16string_view>{
                 "getContextType", u"device"},
             {"getDescription", u"phoneME iOS accelerometer"},
             {"getModel", u"phoneME-iOS"}}) {
        add(registry, std::string(kSensorImpl), std::string(method),
            "()Ljava/lang/String;",
            [value](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                auto text = make_string(machine, std::u16string(value));
                if (!text) return std::unexpected(text.error());
                return std::optional<Value>(Value::from_reference(*text));
            });
    }
    add(registry, std::string(kSensorImpl), "getProperty",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_reference({}));
        });
    add(registry, std::string(kSensorImpl), "getPropertyNames",
        "()[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto names = allocate_array(machine, "[Ljava/lang/String;", 0U,
                                        Value::from_reference({}));
            if (!names) return std::unexpected(names.error());
            return std::optional<Value>(Value::from_reference(*names));
        });
}

void register_data(NativeMethodRegistry& registry) {
    for (const auto& [method, field, descriptor] : {
             std::tuple<std::string_view, std::string_view, std::string_view>{
                 "getChannelInfo", "channel",
                 "Ljavax/microedition/sensor/ChannelInfo;"},
             {"getDoubleValues", "doubleValues", "[D"},
             {"getIntValues", "intValues", "[I"},
             {"getObjectValues", "objectValues", "[Ljava/lang/Object;"}}) {
        add(registry, std::string(kDataImpl), std::string(method),
            "()" + std::string(descriptor),
            [field, descriptor](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                return reference_getter(machine, arguments, kDataImpl,
                                        field, descriptor);
            });
    }
    add(registry, std::string(kDataImpl), "getTimestamp", "(I)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            if (!object || !index) {
                if (!object) return std::unexpected(object.error());
                return std::unexpected(index.error());
            }
            auto values = reference_field(machine, *object, kDataImpl,
                                          "doubleValues", "[D");
            if (!values) return std::unexpected(values.error());
            auto length = machine.heap().array_length(*values);
            if (!length) return std::unexpected(length.error());
            if (*index < 0 || static_cast<usize>(*index) >= *length) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "sensor sample index is out of range");
            }
            auto timestamp = long_field(machine, *object, kDataImpl,
                                        "timestamp");
            if (!timestamp) return std::unexpected(timestamp.error());
            return std::optional<Value>(Value::from_long(*timestamp));
        });
    add(registry, std::string(kDataImpl), "getUncertainty", "(I)F",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_float(0.001F));
        });
    add(registry, std::string(kDataImpl), "isValid", "(I)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto index = int_argument(arguments, 1U);
            if (!object || !index) {
                if (!object) return std::unexpected(object.error());
                return std::unexpected(index.error());
            }
            auto values = reference_field(machine, *object, kDataImpl,
                                          "doubleValues", "[D");
            if (!values) return std::unexpected(values.error());
            auto length = machine.heap().array_length(*values);
            if (!length) return std::unexpected(length.error());
            return std::optional<Value>(Value::from_int(
                *index >= 0 && static_cast<usize>(*index) < *length ? 1 : 0));
        });
}

[[nodiscard]] Result<std::optional<Value>> connection_data(
    Machine& machine,
    std::span<const Value> arguments) {
    auto connection = receiver(arguments);
    auto samples = int_argument(arguments, 1U);
    if (!connection || !samples) {
        if (!connection) return std::unexpected(connection.error());
        return std::unexpected(samples.error());
    }
    auto open = require_open(machine, *connection);
    if (!open) return std::unexpected(open.error());
    auto data = data_for_connection(machine, *connection, *samples);
    if (!data) return std::unexpected(data.error());
    return std::optional<Value>(Value::from_reference(*data));
}

[[nodiscard]] Result<std::optional<Value>> set_listener(
    Machine& machine,
    std::span<const Value> arguments) {
    auto connection = receiver(arguments);
    auto listener = reference_argument(arguments, 1U, false);
    auto samples = int_argument(arguments, 2U);
    if (!connection || !listener || !samples) {
        if (!connection) return std::unexpected(connection.error());
        if (!listener) return std::unexpected(listener.error());
        return std::unexpected(samples.error());
    }
    auto open = require_open(machine, *connection);
    if (!open) return std::unexpected(open.error());
    if (*samples <= 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "sensor listener buffer size must be positive");
    }
    auto stored_listener = set_field(
        machine, *connection, kConnectionImpl, "listener",
        "Ljavax/microedition/sensor/DataListener;",
        Value::from_reference(*listener));
    auto stored_size = set_field(machine, *connection, kConnectionImpl,
                                 "bufferSize", "I",
                                 Value::from_int(*samples));
    if (!stored_listener) return std::unexpected(stored_listener.error());
    if (!stored_size) return std::unexpected(stored_size.error());

    auto data = data_for_connection(machine, *connection, *samples);
    if (!data) return std::unexpected(data.error());
    auto data_root = machine.pin_native_root(*data);
    if (!data_root) return std::unexpected(data_root.error());
    const std::array<Value, 3> callback_arguments {{
        Value::from_reference(*connection), Value::from_reference(*data),
        Value::from_int(0),
    }};
    auto callback = machine.invoke_instance(
        *listener, "javax/microedition/sensor/DataListener", "dataReceived",
        "(Ljavax/microedition/sensor/SensorConnection;[Ljavax/microedition/sensor/Data;Z)V",
        callback_arguments);
    if (!callback) return std::unexpected(callback.error());
    if (callback->throwable.has_value()) {
        auto type = machine.heap().class_name(*callback->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, "sensor DataListener callback threw");
    }
    return std::optional<Value> {};
}

void register_connection(NativeMethodRegistry& registry) {
    add(registry, std::string(kConnectionImpl), "close", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto connection = receiver(arguments);
            if (!connection) return std::unexpected(connection.error());
            auto closed = set_field(machine, *connection, kConnectionImpl,
                                    "closed", "I", Value::from_int(1));
            auto listener = set_field(
                machine, *connection, kConnectionImpl, "listener",
                "Ljavax/microedition/sensor/DataListener;",
                Value::from_reference({}));
            if (!closed) return std::unexpected(closed.error());
            if (!listener) return std::unexpected(listener.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kConnectionImpl), "getData",
        "(I)[Ljavax/microedition/sensor/Data;", connection_data);
    add(registry, std::string(kConnectionImpl), "getData",
        "(IJZZZ)[Ljavax/microedition/sensor/Data;", connection_data);
    add(registry, std::string(kConnectionImpl), "getSensorInfo",
        "()Ljavax/microedition/sensor/SensorInfo;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            return reference_getter(
                machine, arguments, kConnectionImpl, "sensorInfo",
                "Ljavax/microedition/sensor/SensorInfo;");
        });
    add(registry, std::string(kConnectionImpl), "getState", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto connection = receiver(arguments);
            if (!connection) return std::unexpected(connection.error());
            auto closed = integer_field(machine, *connection, kConnectionImpl,
                                        "closed");
            auto listener = reference_field(
                machine, *connection, kConnectionImpl, "listener",
                "Ljavax/microedition/sensor/DataListener;");
            if (!closed || !listener) {
                if (!closed) return std::unexpected(closed.error());
                return std::unexpected(listener.error());
            }
            const i32 state = *closed != 0 ? 0 : (!listener->is_null() ? 2 : 1);
            return std::optional<Value>(Value::from_int(state));
        });
    add(registry, std::string(kConnectionImpl), "removeDataListener", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto connection = receiver(arguments);
            if (!connection) return std::unexpected(connection.error());
            auto open = require_open(machine, *connection);
            if (!open) return std::unexpected(open.error());
            auto stored = set_field(
                machine, *connection, kConnectionImpl, "listener",
                "Ljavax/microedition/sensor/DataListener;",
                Value::from_reference({}));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kConnectionImpl), "setDataListener",
        "(Ljavax/microedition/sensor/DataListener;I)V", set_listener);
    add(registry, std::string(kConnectionImpl), "setDataListener",
        "(Ljavax/microedition/sensor/DataListener;IJZZZ)V", set_listener);
}

void register_manager(NativeMethodRegistry& registry) {
    const auto find = [](Machine& machine, std::span<const Value> arguments)
        -> Result<std::optional<Value>> {
        ObjectRef quantity {};
        if (!arguments.empty()) {
            auto value = reference_argument(arguments, 0U);
            if (!value) return std::unexpected(value.error());
            quantity = *value;
        }
        if (!quantity.is_null()) {
            auto text = string_value(machine, quantity);
            if (!text) return std::unexpected(text.error());
            if (*text != u"acceleration" && *text != u"accelerometer") {
                auto empty = allocate_array(
                    machine, "[Ljavax/microedition/sensor/SensorInfo;", 0U,
                    Value::from_reference({}));
                if (!empty) return std::unexpected(empty.error());
                return std::optional<Value>(Value::from_reference(*empty));
            }
        }
        auto sensor = make_sensor_info(machine);
        if (!sensor) return std::unexpected(sensor.error());
        auto sensor_root = machine.pin_native_root(*sensor);
        if (!sensor_root) return std::unexpected(sensor_root.error());
        auto array = allocate_array(
            machine, "[Ljavax/microedition/sensor/SensorInfo;", 1U,
            Value::from_reference({}));
        if (!array) return std::unexpected(array.error());
        auto stored = machine.heap().set_element(
            *array, 0U, Value::from_reference(*sensor));
        if (!stored) return std::unexpected(stored.error());
        return std::optional<Value>(Value::from_reference(*array));
    };
    add(registry, std::string(kSensorManager), "findSensors",
        "(Ljava/lang/String;Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;",
        find);
    add(registry, std::string(kSensorManager), "findSensors",
        "(Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;", find);
    for (const auto& descriptor : {
             "(Ljavax/microedition/sensor/SensorListener;Ljava/lang/String;)V",
             "(Ljavax/microedition/sensor/SensorListener;Ljavax/microedition/sensor/SensorInfo;)V"}) {
        add(registry, std::string(kSensorManager), "addSensorListener",
            descriptor,
            [](Machine&, std::span<const Value>)
                -> Result<std::optional<Value>> {
                return std::optional<Value> {};
            });
    }
    add(registry, std::string(kSensorManager), "removeSensorListener",
        "(Ljavax/microedition/sensor/SensorListener;)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
}

} // namespace

Result<ObjectRef> open_sensor_connection(Machine& machine,
                                         ObjectRef url,
                                         std::string_view text,
                                         i32 mode) {
    if (!text.starts_with("sensor:")) {
        return fail(ErrorCode::invalid_argument,
                    "not a sensor connection URL");
    }
    if (mode != 1 && mode != 3) {
        return fail_java("java/lang/IllegalArgumentException",
                         "sensor connections require READ or READ_WRITE mode");
    }
    auto connection = allocate_instance(machine, kConnectionImpl);
    if (!connection) return std::unexpected(connection.error());
    auto connection_root = machine.pin_native_root(*connection);
    if (!connection_root) return std::unexpected(connection_root.error());
    auto sensor = make_sensor_info(machine, url);
    if (!sensor) return std::unexpected(sensor.error());
    auto stored_sensor = set_field(
        machine, *connection, kConnectionImpl, "sensorInfo",
        "Ljavax/microedition/sensor/SensorInfo;",
        Value::from_reference(*sensor));
    auto stored_closed = set_field(machine, *connection, kConnectionImpl,
                                   "closed", "I", Value::from_int(0));
    auto stored_listener = set_field(
        machine, *connection, kConnectionImpl, "listener",
        "Ljavax/microedition/sensor/DataListener;", Value::from_reference({}));
    auto stored_buffer = set_field(machine, *connection, kConnectionImpl,
                                   "bufferSize", "I", Value::from_int(1));
    if (!stored_sensor) return std::unexpected(stored_sensor.error());
    if (!stored_closed) return std::unexpected(stored_closed.error());
    if (!stored_listener) return std::unexpected(stored_listener.error());
    if (!stored_buffer) return std::unexpected(stored_buffer.error());
    return *connection;
}

void register_sensor_natives(NativeMethodRegistry& registry) {
    register_measurement_range(registry);
    register_channel_info(registry);
    register_sensor_info(registry);
    register_data(registry);
    register_connection(registry);
    register_manager(registry);
}

} // namespace phoneme::vm
