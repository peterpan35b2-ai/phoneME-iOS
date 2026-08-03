#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_bluetooth_class(
    std::string_view name) {
    if (name == "javax/bluetooth/BluetoothStateException") {
        return make_class(std::string(name), "java/io/IOException", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/bluetooth/BluetoothConnectionException") {
        return make_class(std::string(name), "java/io/IOException", kOrdinary, {
            field(kPublic | kStatic | kFinal, "UNKNOWN_PSM", "I"),
            field(kPublic | kStatic | kFinal, "SECURITY_BLOCK", "I"),
            field(kPublic | kStatic | kFinal, "NO_RESOURCES", "I"),
            field(kPublic | kStatic | kFinal, "FAILED_NOINFO", "I"),
            field(kPublic | kStatic | kFinal, "TIMEOUT", "I"),
            field(kPublic | kStatic | kFinal, "UNACCEPTABLE_PARAMS", "I"),
            field(kPrivate, "status", "I"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "<init>", "(ILjava/lang/String;)V"),
            method(kPublic, "getStatus", "()I"),
        });
    }
    if (name == "javax/bluetooth/ServiceRegistrationException") {
        return make_class(std::string(name), "java/io/IOException", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/bluetooth/DiscoveryListener") {
        const u16 api = kPublic | kAbstract;
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "INQUIRY_COMPLETED", "I"),
            field(kPublic | kStatic | kFinal, "INQUIRY_TERMINATED", "I"),
            field(kPublic | kStatic | kFinal, "INQUIRY_ERROR", "I"),
            field(kPublic | kStatic | kFinal, "SERVICE_SEARCH_COMPLETED", "I"),
            field(kPublic | kStatic | kFinal, "SERVICE_SEARCH_TERMINATED", "I"),
            field(kPublic | kStatic | kFinal, "SERVICE_SEARCH_ERROR", "I"),
            field(kPublic | kStatic | kFinal, "SERVICE_SEARCH_NO_RECORDS", "I"),
            field(kPublic | kStatic | kFinal,
                  "SERVICE_SEARCH_DEVICE_NOT_REACHABLE", "I"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(api, "deviceDiscovered",
                   "(Ljavax/bluetooth/RemoteDevice;Ljavax/bluetooth/DeviceClass;)V"),
            method(api, "inquiryCompleted", "(I)V"),
            method(api, "servicesDiscovered",
                   "(I[Ljavax/bluetooth/ServiceRecord;)V"),
            method(api, "serviceSearchCompleted", "(II)V"),
        });
    }
    if (name == "javax/bluetooth/LocalDevice") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kStatic, "singleton", "Ljavax/bluetooth/LocalDevice;"),
            field(kPrivate, "agent", "Ljavax/bluetooth/DiscoveryAgent;"),
            field(kPrivate, "discoverable", "I"),
        }, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "getLocalDevice",
                   "()Ljavax/bluetooth/LocalDevice;"),
            method(kPublic | kStatic, "getProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic | kStatic, "isPowerOn", "()Z"),
            method(kPublic, "getDiscoveryAgent",
                   "()Ljavax/bluetooth/DiscoveryAgent;"),
            method(kPublic, "getFriendlyName", "()Ljava/lang/String;"),
            method(kPublic, "getBluetoothAddress", "()Ljava/lang/String;"),
            method(kPublic, "getDeviceClass", "()Ljavax/bluetooth/DeviceClass;"),
            method(kPublic, "setDiscoverable", "(I)Z"),
            method(kPublic, "getDiscoverable", "()I"),
            method(kPublic, "getRecord",
                   "(Ljavax/microedition/io/Connection;)Ljavax/bluetooth/ServiceRecord;"),
            method(kPublic, "updateRecord", "(Ljavax/bluetooth/ServiceRecord;)V"),
        });
    }
    if (name == "javax/bluetooth/DiscoveryAgent") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPublic | kStatic | kFinal, "NOT_DISCOVERABLE", "I"),
            field(kPublic | kStatic | kFinal, "GIAC", "I"),
            field(kPublic | kStatic | kFinal, "LIAC", "I"),
            field(kPublic | kStatic | kFinal, "CACHED", "I"),
            field(kPublic | kStatic | kFinal, "PREKNOWN", "I"),
            field(kPrivate, "nextTransaction", "I"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(kPublic, "retrieveDevices", "(I)[Ljavax/bluetooth/RemoteDevice;"),
            method(kPublic, "startInquiry",
                   "(ILjavax/bluetooth/DiscoveryListener;)Z"),
            method(kPublic, "cancelInquiry",
                   "(Ljavax/bluetooth/DiscoveryListener;)Z"),
            method(kPublic, "searchServices",
                   "([I[Ljavax/bluetooth/UUID;Ljavax/bluetooth/RemoteDevice;"
                   "Ljavax/bluetooth/DiscoveryListener;)I"),
            method(kPublic, "cancelServiceSearch", "(I)Z"),
            method(kPublic, "selectService",
                   "(Ljavax/bluetooth/UUID;IZ)Ljava/lang/String;"),
        });
    }
    if (name == "javax/bluetooth/UUID") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "value", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "(J)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Z)V"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        });
    }
    if (name == "javax/bluetooth/RemoteDevice") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate | kFinal, "address", "Ljava/lang/String;"),
        }, {
            method(kProtected, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic | kStatic, "getRemoteDevice",
                   "(Ljavax/microedition/io/Connection;)Ljavax/bluetooth/RemoteDevice;"),
            method(kPublic, "getBluetoothAddress", "()Ljava/lang/String;"),
            method(kPublic, "getFriendlyName", "(Z)Ljava/lang/String;"),
            method(kPublic, "isTrustedDevice", "()Z"),
            method(kPublic, "authenticate", "()Z"),
            method(kPublic, "authorize", "(Ljavax/microedition/io/Connection;)Z"),
            method(kPublic, "encrypt", "(Ljavax/microedition/io/Connection;Z)Z"),
            method(kPublic, "isAuthenticated", "()Z"),
            method(kPublic, "isAuthorized", "(Ljavax/microedition/io/Connection;)Z"),
            method(kPublic, "isEncrypted", "()Z"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
        });
    }
    if (name == "javax/bluetooth/DeviceClass") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "record", "I"),
        }, {
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "getMajorDeviceClass", "()I"),
            method(kPublic, "getMinorDeviceClass", "()I"),
            method(kPublic, "getServiceClasses", "()I"),
        });
    }
    if (name == "javax/bluetooth/DataElement") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPublic | kStatic | kFinal, "NULL", "I"),
            field(kPublic | kStatic | kFinal, "U_INT_1", "I"),
            field(kPublic | kStatic | kFinal, "U_INT_2", "I"),
            field(kPublic | kStatic | kFinal, "U_INT_4", "I"),
            field(kPublic | kStatic | kFinal, "U_INT_8", "I"),
            field(kPublic | kStatic | kFinal, "U_INT_16", "I"),
            field(kPublic | kStatic | kFinal, "INT_1", "I"),
            field(kPublic | kStatic | kFinal, "INT_2", "I"),
            field(kPublic | kStatic | kFinal, "INT_4", "I"),
            field(kPublic | kStatic | kFinal, "INT_8", "I"),
            field(kPublic | kStatic | kFinal, "INT_16", "I"),
            field(kPublic | kStatic | kFinal, "UUID", "I"),
            field(kPublic | kStatic | kFinal, "STRING", "I"),
            field(kPublic | kStatic | kFinal, "BOOL", "I"),
            field(kPublic | kStatic | kFinal, "DATSEQ", "I"),
            field(kPublic | kStatic | kFinal, "DATALT", "I"),
            field(kPublic | kStatic | kFinal, "URL", "I"),
            field(kPrivate | kFinal, "type", "I"),
            field(kPrivate, "longValue", "J"),
            field(kPrivate, "objectValue", "Ljava/lang/Object;"),
            field(kPrivate, "sequence", "Ljava/util/Vector;"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "<init>", "(Z)V"),
            method(kPublic, "<init>", "(IJ)V"),
            method(kPublic, "<init>", "(ILjava/lang/Object;)V"),
            method(kPublic, "getDataType", "()I"),
            method(kPublic, "getLong", "()J"),
            method(kPublic, "getBoolean", "()Z"),
            method(kPublic, "getValue", "()Ljava/lang/Object;"),
            method(kPublic, "getSize", "()I"),
            method(kPublic, "addElement", "(Ljavax/bluetooth/DataElement;)V"),
            method(kPublic, "insertElementAt", "(Ljavax/bluetooth/DataElement;I)V"),
            method(kPublic, "removeElement", "(Ljavax/bluetooth/DataElement;)Z"),
        });
    }
    if (name == "javax/bluetooth/ServiceRecord") {
        const u16 api = kPublic | kAbstract;
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal,
                  "NOAUTHENTICATE_NOENCRYPT", "I"),
            field(kPublic | kStatic | kFinal,
                  "AUTHENTICATE_NOENCRYPT", "I"),
            field(kPublic | kStatic | kFinal, "AUTHENTICATE_ENCRYPT", "I"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(api, "getHostDevice", "()Ljavax/bluetooth/RemoteDevice;"),
            method(api, "getAttributeIDs", "()[I"),
            method(api, "getAttributeValue", "(I)Ljavax/bluetooth/DataElement;"),
            method(api, "populateRecord", "([I)Z"),
            method(api, "setAttributeValue", "(ILjavax/bluetooth/DataElement;)Z"),
            method(api, "getConnectionURL", "(IZ)Ljava/lang/String;"),
            method(api, "setDeviceServiceClasses", "(I)V"),
        });
    }
    if (name == "javax/bluetooth/L2CAPConnection") {
        const u16 api = kPublic | kAbstract;
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getReceiveMTU", "()I"),
            method(api, "getTransmitMTU", "()I"),
            method(api, "ready", "()Z"),
            method(api, "receive", "([B)I"),
            method(api, "send", "([B)V"),
            method(api, "close", "()V"),
        }, {"javax/microedition/io/Connection"});
    }
    if (name == "javax/bluetooth/L2CAPConnectionNotifier") {
        const u16 api = kPublic | kAbstract;
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "acceptAndOpen", "()Ljavax/bluetooth/L2CAPConnection;"),
            method(api, "close", "()V"),
        }, {"javax/microedition/io/Connection"});
    }
    return nullptr;
}

} // namespace

void register_bluetooth_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_bluetooth_class);
}

} // namespace phoneme::vm
