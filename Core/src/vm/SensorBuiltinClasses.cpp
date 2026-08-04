#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_sensor_class(
    std::string_view name) {
    const u16 api = kPublic | kAbstract;

    if (name == "javax/microedition/sensor/MeasurementRange") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "smallestValue", "D"),
            field(kPrivate, "largestValue", "D"),
            field(kPrivate, "resolution", "D"),
        }, {
            method(kPublic, "<init>", "(DDD)V"),
            method(kPublic, "getSmallestValue", "()D"),
            method(kPublic, "getLargestValue", "()D"),
            method(kPublic, "getResolution", "()D"),
        });
    }
    if (name == "javax/microedition/sensor/ChannelInfo") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "TYPE_DOUBLE", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_INT", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_OBJECT", "I"),
        }, {
            method(api, "getName", "()Ljava/lang/String;"),
            method(api, "getAccuracy", "()F"),
            method(api, "getDataType", "()I"),
            method(api, "getMeasurementRanges",
                   "()[Ljavax/microedition/sensor/MeasurementRange;"),
            method(api, "getScale", "()I"),
            method(api, "getUnit", "()Ljava/lang/String;"),
        });
    }
    if (name == "javax/microedition/sensor/SensorInfo") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "CONTEXT_TYPE_AMBIENT", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "CONTEXT_TYPE_DEVICE", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "CONTEXT_TYPE_USER", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "CONNECTION_TYPE_EMBEDDED", "I"),
            field(kPublic | kStatic | kFinal, "CONNECTION_TYPE_REMOTE", "I"),
            field(kPublic | kStatic | kFinal, "CONNECTION_TYPE_SHORT_RANGE_WIRELESS", "I"),
            field(kPublic | kStatic | kFinal, "PROP_LATITUDE", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "PROP_LONGITUDE", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "PROP_LOCATION", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "PROP_VENDOR", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "PROP_VERSION", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "PROP_MAX_RATE", "Ljava/lang/String;"),
        }, {
            method(api, "getChannelInfos",
                   "()[Ljavax/microedition/sensor/ChannelInfo;"),
            method(api, "getConnectionType", "()I"),
            method(api, "getContextType", "()Ljava/lang/String;"),
            method(api, "getDescription", "()Ljava/lang/String;"),
            method(api, "getMaxBufferSize", "()I"),
            method(api, "getModel", "()Ljava/lang/String;"),
            method(api, "getProperty", "(Ljava/lang/String;)Ljava/lang/Object;"),
            method(api, "getPropertyNames", "()[Ljava/lang/String;"),
            method(api, "getQuantity", "()Ljava/lang/String;"),
            method(api, "getUrl", "()Ljava/lang/String;"),
            method(api, "isAvailabilityPushSupported", "()Z"),
            method(api, "isAvailable", "()Z"),
            method(api, "isConditionPushSupported", "()Z"),
        });
    }
    if (name == "javax/microedition/sensor/Data") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getChannelInfo", "()Ljavax/microedition/sensor/ChannelInfo;"),
            method(api, "getDoubleValues", "()[D"),
            method(api, "getIntValues", "()[I"),
            method(api, "getObjectValues", "()[Ljava/lang/Object;"),
            method(api, "getTimestamp", "(I)J"),
            method(api, "getUncertainty", "(I)F"),
            method(api, "isValid", "(I)Z"),
        });
    }
    if (name == "javax/microedition/sensor/DataListener") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "dataReceived",
                   "(Ljavax/microedition/sensor/SensorConnection;[Ljavax/microedition/sensor/Data;Z)V"),
        });
    }
    if (name == "javax/microedition/sensor/SensorConnection") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "STATE_CLOSED", "I"),
            field(kPublic | kStatic | kFinal, "STATE_LISTENING", "I"),
            field(kPublic | kStatic | kFinal, "STATE_OPENED", "I"),
        }, {
            method(api, "getData", "(I)[Ljavax/microedition/sensor/Data;"),
            method(api, "getData", "(IJZZZ)[Ljavax/microedition/sensor/Data;"),
            method(api, "getSensorInfo", "()Ljavax/microedition/sensor/SensorInfo;"),
            method(api, "getState", "()I"),
            method(api, "removeDataListener", "()V"),
            method(api, "setDataListener", "(Ljavax/microedition/sensor/DataListener;I)V"),
            method(api, "setDataListener", "(Ljavax/microedition/sensor/DataListener;IJZZZ)V"),
        }, {"javax/microedition/io/Connection"});
    }
    if (name == "javax/microedition/sensor/SensorManager") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "findSensors",
                   "(Ljava/lang/String;Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;"),
            method(kPublic | kStatic, "findSensors",
                   "(Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;"),
            method(kPublic | kStatic, "addSensorListener",
                   "(Ljavax/microedition/sensor/SensorListener;Ljava/lang/String;)V"),
            method(kPublic | kStatic, "addSensorListener",
                   "(Ljavax/microedition/sensor/SensorListener;Ljavax/microedition/sensor/SensorInfo;)V"),
            method(kPublic | kStatic, "removeSensorListener",
                   "(Ljavax/microedition/sensor/SensorListener;)V"),
        });
    }
    if (name == "javax/microedition/sensor/SensorListener") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "sensorAvailable", "(Ljavax/microedition/sensor/SensorInfo;)V"),
            method(api, "sensorUnavailable", "(Ljavax/microedition/sensor/SensorInfo;)V"),
        });
    }

    if (name == "phoneme/sensor/ChannelInfoImpl") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "name", "Ljava/lang/String;"),
            field(kPrivate, "dataType", "I"),
            field(kPrivate, "scale", "I"),
            field(kPrivate, "unit", "Ljava/lang/String;"),
            field(kPrivate, "ranges", "[Ljavax/microedition/sensor/MeasurementRange;"),
        }, {
            method(kPublic, "getName", "()Ljava/lang/String;"),
            method(kPublic, "getAccuracy", "()F"),
            method(kPublic, "getDataType", "()I"),
            method(kPublic, "getMeasurementRanges",
                   "()[Ljavax/microedition/sensor/MeasurementRange;"),
            method(kPublic, "getScale", "()I"),
            method(kPublic, "getUnit", "()Ljava/lang/String;"),
        }, {"javax/microedition/sensor/ChannelInfo"});
    }
    if (name == "phoneme/sensor/SensorInfoImpl") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "url", "Ljava/lang/String;"),
            field(kPrivate, "quantity", "Ljava/lang/String;"),
            field(kPrivate, "channels", "[Ljavax/microedition/sensor/ChannelInfo;"),
        }, {
            method(kPublic, "getChannelInfos",
                   "()[Ljavax/microedition/sensor/ChannelInfo;"),
            method(kPublic, "getConnectionType", "()I"),
            method(kPublic, "getContextType", "()Ljava/lang/String;"),
            method(kPublic, "getDescription", "()Ljava/lang/String;"),
            method(kPublic, "getMaxBufferSize", "()I"),
            method(kPublic, "getModel", "()Ljava/lang/String;"),
            method(kPublic, "getProperty", "(Ljava/lang/String;)Ljava/lang/Object;"),
            method(kPublic, "getPropertyNames", "()[Ljava/lang/String;"),
            method(kPublic, "getQuantity", "()Ljava/lang/String;"),
            method(kPublic, "getUrl", "()Ljava/lang/String;"),
            method(kPublic, "isAvailabilityPushSupported", "()Z"),
            method(kPublic, "isAvailable", "()Z"),
            method(kPublic, "isConditionPushSupported", "()Z"),
        }, {"javax/microedition/sensor/SensorInfo"});
    }
    if (name == "phoneme/sensor/DataImpl") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "channel", "Ljavax/microedition/sensor/ChannelInfo;"),
            field(kPrivate, "doubleValues", "[D"),
            field(kPrivate, "intValues", "[I"),
            field(kPrivate, "objectValues", "[Ljava/lang/Object;"),
            field(kPrivate, "timestamp", "J"),
        }, {
            method(kPublic, "getChannelInfo", "()Ljavax/microedition/sensor/ChannelInfo;"),
            method(kPublic, "getDoubleValues", "()[D"),
            method(kPublic, "getIntValues", "()[I"),
            method(kPublic, "getObjectValues", "()[Ljava/lang/Object;"),
            method(kPublic, "getTimestamp", "(I)J"),
            method(kPublic, "getUncertainty", "(I)F"),
            method(kPublic, "isValid", "(I)Z"),
        }, {"javax/microedition/sensor/Data"});
    }
    if (name == "phoneme/sensor/SensorConnectionImpl") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "sensorInfo", "Ljavax/microedition/sensor/SensorInfo;"),
            field(kPrivate, "closed", "I"),
            field(kPrivate, "listener", "Ljavax/microedition/sensor/DataListener;"),
            field(kPrivate, "bufferSize", "I"),
        }, {
            method(kPublic, "close", "()V"),
            method(kPublic, "getData", "(I)[Ljavax/microedition/sensor/Data;"),
            method(kPublic, "getData", "(IJZZZ)[Ljavax/microedition/sensor/Data;"),
            method(kPublic, "getSensorInfo", "()Ljavax/microedition/sensor/SensorInfo;"),
            method(kPublic, "getState", "()I"),
            method(kPublic, "removeDataListener", "()V"),
            method(kPublic, "setDataListener", "(Ljavax/microedition/sensor/DataListener;I)V"),
            method(kPublic, "setDataListener", "(Ljavax/microedition/sensor/DataListener;IJZZZ)V"),
        }, {"javax/microedition/sensor/SensorConnection"});
    }

    return nullptr;
}

} // namespace

void register_sensor_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_sensor_class);
}

} // namespace phoneme::vm
