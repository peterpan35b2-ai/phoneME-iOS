#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_security_class(std::string_view name) {
    if (name == "com/sun/midp/security/PermissionGate") {
        return make_class("com/sun/midp/security/PermissionGate",
                          "java/lang/Object", kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "checkPermission",
                   "(Ljava/lang/String;)I"),
            method(kPublic | kStatic, "requestPermission",
                   "(Ljava/lang/String;Ljava/lang/String;Z)I"),
            method(kPublic | kStatic, "requirePermission",
                   "(Ljava/lang/String;Ljava/lang/String;Z)V"),
        });
    }
    if (name == "java/security/Key") {
        return make_class("java/security/Key", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "getAlgorithm", "()Ljava/lang/String;"),
            method(kPublic | kAbstract, "getFormat", "()Ljava/lang/String;"),
            method(kPublic | kAbstract, "getEncoded", "()[B"),
        });
    }
    if (name == "javax/crypto/SecretKey") {
        return make_class("javax/crypto/SecretKey", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {},
                          {"java/security/Key"});
    }
    if (name == "javax/crypto/spec/SecretKeySpec") {
        return make_class("javax/crypto/spec/SecretKeySpec", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "key", "[B"),
            field(kPrivate | kFinal, "algorithm", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "([BLjava/lang/String;)V"),
            method(kPublic, "getAlgorithm", "()Ljava/lang/String;"),
            method(kPublic, "getFormat", "()Ljava/lang/String;"),
            method(kPublic, "getEncoded", "()[B"),
        }, {"javax/crypto/SecretKey", "java/security/Key"});
    }
    if (name == "javax/crypto/Cipher") {
        return make_class("javax/crypto/Cipher", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "transformation", "Ljava/lang/String;"),
            field(kPrivate, "mode", "I"),
            field(kPrivate, "key", "[B"),
        }, {
            method(kPrivate, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic | kStatic, "getInstance",
                   "(Ljava/lang/String;)Ljavax/crypto/Cipher;"),
            method(kPublic, "init", "(ILjava/security/Key;)V"),
            method(kPublic, "doFinal", "([B)[B"),
        });
    }
    if (name == "javax/crypto/NoSuchPaddingException" ||
        name == "javax/crypto/BadPaddingException" ||
        name == "javax/crypto/IllegalBlockSizeException" ||
        name == "java/security/InvalidKeyException") {
        return make_class(std::string(name), "java/lang/Exception",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/security/MessageDigest") {
        return make_class("java/security/MessageDigest", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "buffer", "[B"),
            field(kPrivate, "count", "I"),
        }, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "getInstance",
                   "(Ljava/lang/String;)Ljava/security/MessageDigest;"),
            method(kPublic, "getAlgorithm", "()Ljava/lang/String;"),
            method(kPublic, "getDigestLength", "()I"),
            method(kPublic, "update", "(B)V"),
            method(kPublic, "update", "([B)V"),
            method(kPublic, "update", "([BII)V"),
            method(kPublic, "digest", "()[B"),
            method(kPublic, "digest", "([B)[B"),
            method(kPublic, "reset", "()V"),
            method(kPublic | kStatic, "isEqual", "([B[B)Z"),
        });
    }
    if (name == "java/security/NoSuchAlgorithmException") {
        return make_class("java/security/NoSuchAlgorithmException",
                          "java/lang/Exception", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    // CLDC 1.1.1 ships AccessController with only checkPermission(); the
    // doPrivileged / getContext machinery is commented out in the source, so
    // those members are deliberately absent here. The body of checkPermission
    // is also commented out, making it a permissive no-op implemented natively.
    if (name == "java/security/AccessController") {
        return make_class("java/security/AccessController",
                          "java/lang/Object", kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "checkPermission",
                   "(Ljava/security/Permission;)V"),
        });
    }
    if (name == "java/security/Permission") {
        return make_class("java/security/Permission", "java/lang/Object",
                          kOrdinary | kAbstract, {
            field(kPrivate, "name", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic | kAbstract, "implies",
                   "(Ljava/security/Permission;)Z"),
            method(kPublic | kAbstract, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "hashCode", "()I"),
            method(kPublic | kAbstract, "getActions",
                   "()Ljava/lang/String;"),
            method(kPublic | kFinal, "getName", "()Ljava/lang/String;"),
            method(kPublic, "newPermissionCollection",
                   "()Ljava/security/PermissionCollection;"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        });
    }
    if (name == "java/security/PermissionCollection") {
        return make_class("java/security/PermissionCollection",
                          "java/lang/Object", kOrdinary | kAbstract, {
            field(kPrivate, "readOnly", "Z"),
        }, {
            method(kPublic | kAbstract, "add",
                   "(Ljava/security/Permission;)V"),
            method(kPublic | kAbstract, "implies",
                   "(Ljava/security/Permission;)Z"),
            method(kPublic | kAbstract, "elements",
                   "()Ljava/util/Enumeration;"),
            method(kPublic, "setReadOnly", "()V"),
            method(kPublic, "isReadOnly", "()Z"),
        });
    }
    if (name == "java/security/BasicPermission") {
        return make_class("java/security/BasicPermission",
                          "java/security/Permission", kOrdinary | kAbstract, {}, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "implies", "(Ljava/security/Permission;)Z"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "getActions", "()Ljava/lang/String;"),
            method(kPublic, "newPermissionCollection",
                   "()Ljava/security/PermissionCollection;"),
        });
    }
    if (name == "java/security/BasicPermissionCollection") {
        return make_class("java/security/BasicPermissionCollection",
                          "java/security/PermissionCollection", kOrdinary | kFinal, {
            field(kPrivate, "entries", "[Ljava/security/Permission;"),
            field(kPrivate, "count", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "add", "(Ljava/security/Permission;)V"),
            method(kPublic, "implies", "(Ljava/security/Permission;)Z"),
            method(kPublic, "elements", "()Ljava/util/Enumeration;"),
        });
    }
    if (name == "java/lang/RuntimePermission") {
        return make_class("java/lang/RuntimePermission",
                          "java/security/BasicPermission", kOrdinary | kFinal, {}, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "java/util/PropertyPermission") {
        return make_class("java/util/PropertyPermission",
                          "java/security/BasicPermission", kOrdinary | kFinal, {
            field(kPrivate, "mask", "I"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "implies", "(Ljava/security/Permission;)Z"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "getActions", "()Ljava/lang/String;"),
            method(kPublic, "getMask", "()I"),
            method(kPublic, "newPermissionCollection",
                   "()Ljava/security/PermissionCollection;"),
        });
    }
    if (name == "java/util/PropertyPermissionCollection") {
        return make_class("java/util/PropertyPermissionCollection",
                          "java/security/PermissionCollection", kOrdinary | kFinal, {
            field(kPrivate, "entries", "[Ljava/util/PropertyPermission;"),
            field(kPrivate, "count", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "add", "(Ljava/security/Permission;)V"),
            method(kPublic, "implies", "(Ljava/security/Permission;)Z"),
            method(kPublic, "elements", "()Ljava/util/Enumeration;"),
        });
    }
    if (name == "java/security/AccessControlException") {
        return make_class("java/security/AccessControlException",
                          "java/lang/SecurityException", kOrdinary, {
            field(kPrivate, "perm", "Ljava/security/Permission;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/security/Permission;)V"),
            method(kPublic, "getPermission",
                   "()Ljava/security/Permission;"),
        });
    }
    return nullptr;
}

} // namespace

void register_security_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_security_class);
}

} // namespace phoneme::vm
