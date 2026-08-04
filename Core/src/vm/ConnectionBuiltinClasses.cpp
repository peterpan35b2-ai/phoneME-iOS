#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>
#include <vector>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] std::vector<classfile::Method> stream_methods() {
    return {
        method(kPublic, "close", "()V"),
        method(kPublic, "openInputStream", "()Ljava/io/InputStream;"),
        method(kPublic, "openDataInputStream", "()Ljava/io/DataInputStream;"),
        method(kPublic, "openOutputStream", "()Ljava/io/OutputStream;"),
        method(kPublic, "openDataOutputStream", "()Ljava/io/DataOutputStream;"),
    };
}

[[nodiscard]] std::vector<classfile::Method> http_methods() {
    auto methods = stream_methods();
    const std::vector<classfile::Method> additional {
        method(kPublic, "getURL", "()Ljava/lang/String;"),
        method(kPublic, "getProtocol", "()Ljava/lang/String;"),
        method(kPublic, "getHost", "()Ljava/lang/String;"),
        method(kPublic, "getFile", "()Ljava/lang/String;"),
        method(kPublic, "getRef", "()Ljava/lang/String;"),
        method(kPublic, "getQuery", "()Ljava/lang/String;"),
        method(kPublic, "getPort", "()I"),
        method(kPublic, "getRequestMethod", "()Ljava/lang/String;"),
        method(kPublic, "setRequestMethod", "(Ljava/lang/String;)V"),
        method(kPublic, "getRequestProperty", "(Ljava/lang/String;)Ljava/lang/String;"),
        method(kPublic, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"),
        method(kPublic, "getResponseCode", "()I"),
        method(kPublic, "getResponseMessage", "()Ljava/lang/String;"),
        method(kPublic, "getExpiration", "()J"),
        method(kPublic, "getDate", "()J"),
        method(kPublic, "getLastModified", "()J"),
        method(kPublic, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;"),
        method(kPublic, "getHeaderField", "(I)Ljava/lang/String;"),
        method(kPublic, "getHeaderFieldKey", "(I)Ljava/lang/String;"),
        method(kPublic, "getHeaderFieldInt", "(Ljava/lang/String;I)I"),
        method(kPublic, "getHeaderFieldDate", "(Ljava/lang/String;J)J"),
        method(kPublic, "getLength", "()J"),
        method(kPublic, "getType", "()Ljava/lang/String;"),
        method(kPublic, "getEncoding", "()Ljava/lang/String;"),
    };
    methods.insert(methods.end(), additional.begin(), additional.end());
    return methods;
}

[[nodiscard]] std::vector<classfile::Method> data_input_methods() {
    return {
        method(kPublic, "readFully", "([B)V"),
        method(kPublic, "readFully", "([BII)V"),
        method(kPublic, "skipBytes", "(I)I"),
        method(kPublic, "readBoolean", "()Z"),
        method(kPublic, "readByte", "()B"),
        method(kPublic, "readUnsignedByte", "()I"),
        method(kPublic, "readShort", "()S"),
        method(kPublic, "readUnsignedShort", "()I"),
        method(kPublic, "readChar", "()C"),
        method(kPublic, "readInt", "()I"),
        method(kPublic, "readLong", "()J"),
        method(kPublic, "readFloat", "()F"),
        method(kPublic, "readDouble", "()D"),
        method(kPublic, "readLine", "()Ljava/lang/String;"),
        method(kPublic, "readUTF", "()Ljava/lang/String;"),
    };
}

[[nodiscard]] std::vector<classfile::Method> data_output_methods() {
    return {
        method(kPublic, "write", "(I)V"),
        method(kPublic, "write", "([B)V"),
        method(kPublic, "write", "([BII)V"),
        method(kPublic, "writeBoolean", "(Z)V"),
        method(kPublic, "writeByte", "(I)V"),
        method(kPublic, "writeShort", "(I)V"),
        method(kPublic, "writeChar", "(I)V"),
        method(kPublic, "writeInt", "(I)V"),
        method(kPublic, "writeLong", "(J)V"),
        method(kPublic, "writeFloat", "(F)V"),
        method(kPublic, "writeDouble", "(D)V"),
        method(kPublic, "writeBytes", "(Ljava/lang/String;)V"),
        method(kPublic, "writeChars", "(Ljava/lang/String;)V"),
        method(kPublic, "writeUTF", "(Ljava/lang/String;)V"),
    };
}

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_connection_class(
    std::string_view name) {
    if (name == "javax/microedition/io/ConnectionNotFoundException") {
        return make_class(name.data(), "java/io/IOException", kOrdinary, {},
                          {
                              method(kPublic, "<init>", "()V"),
                              method(kPublic, "<init>", "(Ljava/lang/String;)V"),
                          });
    }
    if (name == "javax/microedition/io/ContentConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getType", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getEncoding", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getLength", "()J"),
                          },
                          {"javax/microedition/io/StreamConnection"});
    }
    if (name == "javax/microedition/io/StreamConnectionNotifier") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "acceptAndOpen",
                                     "()Ljavax/microedition/io/StreamConnection;"),
                          },
                          {"javax/microedition/io/Connection"});
    }
    if (name == "java/net/Socket") {
        return make_class(name.data(), "java/lang/Object", kOrdinary,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                          },
                          {
                              method(kPublic, "<init>", "()V"),
                              method(kPublic, "<init>",
                                     "(Ljava/lang/String;I)V"),
                              method(kPublic, "getInputStream",
                                     "()Ljava/io/InputStream;"),
                              method(kPublic, "getOutputStream",
                                     "()Ljava/io/OutputStream;"),
                              method(kPublic, "close", "()V"),
                              method(kPublic, "isClosed", "()Z"),
                          });
    }
    if (name == "javax/microedition/io/SocketConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract,
                          {
                              field(kPublic | kStatic | kFinal, "DELAY", "B"),
                              field(kPublic | kStatic | kFinal, "LINGER", "B"),
                              field(kPublic | kStatic | kFinal, "KEEPALIVE", "B"),
                              field(kPublic | kStatic | kFinal, "RCVBUF", "B"),
                              field(kPublic | kStatic | kFinal, "SNDBUF", "B"),
                          },
                          {
                              method(kStatic, "<clinit>", "()V"),
                              method(kPublic | kAbstract, "getAddress", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getLocalAddress", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getPort", "()I"),
                              method(kPublic | kAbstract, "getLocalPort", "()I"),
                              method(kPublic | kAbstract, "setSocketOption", "(BI)V"),
                              method(kPublic | kAbstract, "getSocketOption", "(B)I"),
                          },
                          {"javax/microedition/io/StreamConnection"});
    }
    if (name == "javax/microedition/io/SecureConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getSecurityInfo",
                                     "()Ljavax/microedition/io/SecurityInfo;"),
                          },
                          {"javax/microedition/io/SocketConnection"});
    }
    if (name == "javax/microedition/io/ServerSocketConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getLocalAddress", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getLocalPort", "()I"),
                          },
                          {"javax/microedition/io/StreamConnectionNotifier"});
    }
    if (name == "javax/microedition/io/UDPDatagramConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "getLocalAddress",
                   "()Ljava/lang/String;"),
            method(kPublic | kAbstract, "getLocalPort", "()I"),
        }, {"javax/microedition/io/DatagramConnection"});
    }
    if (name == "javax/microedition/io/DatagramConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getMaximumLength", "()I"),
                              method(kPublic | kAbstract, "getNominalLength", "()I"),
                              method(kPublic | kAbstract, "send", "(Ljavax/microedition/io/Datagram;)V"),
                              method(kPublic | kAbstract, "receive", "(Ljavax/microedition/io/Datagram;)V"),
                              method(kPublic | kAbstract, "newDatagram", "(I)Ljavax/microedition/io/Datagram;"),
                              method(kPublic | kAbstract, "newDatagram", "(ILjava/lang/String;)Ljavax/microedition/io/Datagram;"),
                              method(kPublic | kAbstract, "newDatagram", "([BI)Ljavax/microedition/io/Datagram;"),
                              method(kPublic | kAbstract, "newDatagram", "([BILjava/lang/String;)Ljavax/microedition/io/Datagram;"),
                              method(kPublic | kAbstract, "newDatagram", "([BII)Ljavax/microedition/io/Datagram;"),
                          },
                          {"javax/microedition/io/Connection"});
    }
    if (name == "javax/microedition/io/Datagram") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getAddress", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "setAddress", "(Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "setAddress", "(Ljavax/microedition/io/Datagram;)V"),
                              method(kPublic | kAbstract, "getData", "()[B"),
                              method(kPublic | kAbstract, "getOffset", "()I"),
                              method(kPublic | kAbstract, "getLength", "()I"),
                              method(kPublic | kAbstract, "setData", "([BII)V"),
                              method(kPublic | kAbstract, "setLength", "(I)V"),
                              method(kPublic | kAbstract, "reset", "()V"),
                          },
                          {"java/io/DataInput", "java/io/DataOutput"});
    }
    if (name == "javax/microedition/io/SecurityInfo") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getServerCertificate", "()Ljavax/microedition/pki/Certificate;"),
                              method(kPublic | kAbstract, "getProtocolName", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getProtocolVersion", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getCipherSuite", "()Ljava/lang/String;"),
                          });
    }
    if (name == "javax/microedition/pki/CertificateException") {
        return make_class(name.data(), "java/io/IOException", kOrdinary,
                          {
                              field(kPublic | kStatic | kFinal,
                                    "BAD_EXTENSIONS", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "CERTIFICATE_CHAIN_TOO_LONG", "B"),
                              field(kPublic | kStatic | kFinal, "EXPIRED", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "UNAUTHORIZED_INTERMEDIATE_CA", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "MISSING_SIGNATURE", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "NOT_YET_VALID", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "SITENAME_MISMATCH", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "UNRECOGNIZED_ISSUER", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "UNSUPPORTED_SIGALG", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "INAPPROPRIATE_KEY_USAGE", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "BROKEN_CHAIN", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "ROOT_CA_EXPIRED", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "UNSUPPORTED_PUBLIC_KEY_TYPE", "B"),
                              field(kPublic | kStatic | kFinal,
                                    "VERIFICATION_FAILED", "B"),
                              field(kPrivate, "cert",
                                    "Ljavax/microedition/pki/Certificate;"),
                              field(kPrivate, "reason", "B"),
                          },
                          {
                              method(kStatic, "<clinit>", "()V"),
                              method(kPublic, "<init>",
                                     "(Ljavax/microedition/pki/Certificate;B)V"),
                              method(kPublic, "<init>",
                                     "(Ljava/lang/String;Ljavax/microedition/pki/Certificate;B)V"),
                              method(kPublic, "getCertificate",
                                     "()Ljavax/microedition/pki/Certificate;"),
                              method(kPublic, "getReason", "()B"),
                          });
    }
    if (name == "javax/microedition/pki/Certificate") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getSubject", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getIssuer", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getType", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getVersion", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getSigAlgName", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getNotBefore", "()J"),
                              method(kPublic | kAbstract, "getNotAfter", "()J"),
                              method(kPublic | kAbstract, "getSerialNumber", "()Ljava/lang/String;"),
                          });
    }
    if (name == "javax/microedition/io/HttpConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract,
                          {
                              field(kPublic | kStatic | kFinal, "HEAD", "Ljava/lang/String;"),
                              field(kPublic | kStatic | kFinal, "GET", "Ljava/lang/String;"),
                              field(kPublic | kStatic | kFinal, "POST", "Ljava/lang/String;"),
                              field(kPublic | kStatic | kFinal, "HTTP_OK", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_CREATED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_ACCEPTED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_NOT_AUTHORITATIVE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_NO_CONTENT", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_RESET", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_PARTIAL", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_MULT_CHOICE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_MOVED_PERM", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_MOVED_TEMP", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_SEE_OTHER", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_NOT_MODIFIED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_USE_PROXY", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_TEMP_REDIRECT", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_BAD_REQUEST", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_UNAUTHORIZED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_PAYMENT_REQUIRED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_FORBIDDEN", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_NOT_FOUND", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_BAD_METHOD", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_NOT_ACCEPTABLE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_PROXY_AUTH", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_CLIENT_TIMEOUT", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_CONFLICT", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_GONE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_LENGTH_REQUIRED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_PRECON_FAILED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_ENTITY_TOO_LARGE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_REQ_TOO_LONG", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_UNSUPPORTED_TYPE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_UNSUPPORTED_RANGE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_EXPECT_FAILED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_INTERNAL_ERROR", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_NOT_IMPLEMENTED", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_BAD_GATEWAY", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_UNAVAILABLE", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_GATEWAY_TIMEOUT", "I"),
                              field(kPublic | kStatic | kFinal, "HTTP_VERSION", "I"),
                          },
                          {
                              method(kStatic, "<clinit>", "()V"),
                              method(kPublic | kAbstract, "getURL", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getProtocol", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getHost", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getFile", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getRef", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getQuery", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getPort", "()I"),
                              method(kPublic | kAbstract, "getRequestMethod", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "setRequestMethod", "(Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "getRequestProperty", "(Ljava/lang/String;)Ljava/lang/String;"),
                              method(kPublic | kAbstract, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "getResponseCode", "()I"),
                              method(kPublic | kAbstract, "getResponseMessage", "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getExpiration", "()J"),
                              method(kPublic | kAbstract, "getDate", "()J"),
                              method(kPublic | kAbstract, "getLastModified", "()J"),
                              method(kPublic | kAbstract, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getHeaderField", "(I)Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getHeaderFieldKey", "(I)Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getHeaderFieldInt", "(Ljava/lang/String;I)I"),
                              method(kPublic | kAbstract, "getHeaderFieldDate", "(Ljava/lang/String;J)J"),
                          },
                          {"javax/microedition/io/ContentConnection"});
    }
    if (name == "javax/microedition/io/HttpsConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getSecurityInfo", "()Ljavax/microedition/io/SecurityInfo;"),
                              method(kPublic | kAbstract, "getPort", "()I"),
                          },
                          {"javax/microedition/io/HttpConnection"});
    }
    if (name == "javax/microedition/io/NativeSecurityInfo") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "certificate", "Ljavax/microedition/pki/Certificate;"),
                              field(kPrivate, "protocolName", "Ljava/lang/String;"),
                              field(kPrivate, "protocolVersion", "Ljava/lang/String;"),
                              field(kPrivate, "cipherSuite", "Ljava/lang/String;"),
                          },
                          {
                              method(kPublic, "getServerCertificate", "()Ljavax/microedition/pki/Certificate;"),
                              method(kPublic, "getProtocolName", "()Ljava/lang/String;"),
                              method(kPublic, "getProtocolVersion", "()Ljava/lang/String;"),
                              method(kPublic, "getCipherSuite", "()Ljava/lang/String;"),
                          },
                          {"javax/microedition/io/SecurityInfo"});
    }
    if (name == "javax/microedition/pki/NativeCertificate") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "subject", "Ljava/lang/String;"),
                              field(kPrivate, "issuer", "Ljava/lang/String;"),
                              field(kPrivate, "serialNumber", "Ljava/lang/String;"),
                              field(kPrivate, "notBefore", "J"),
                              field(kPrivate, "notAfter", "J"),
                          },
                          {
                              method(kPublic, "getSubject", "()Ljava/lang/String;"),
                              method(kPublic, "getIssuer", "()Ljava/lang/String;"),
                              method(kPublic, "getType", "()Ljava/lang/String;"),
                              method(kPublic, "getVersion", "()Ljava/lang/String;"),
                              method(kPublic, "getSigAlgName", "()Ljava/lang/String;"),
                              method(kPublic, "getNotBefore", "()J"),
                              method(kPublic, "getNotAfter", "()J"),
                              method(kPublic, "getSerialNumber", "()Ljava/lang/String;"),
                          },
                          {"javax/microedition/pki/Certificate"});
    }
    if (name == "javax/microedition/io/NativeInputStream") {
        return make_class(name.data(), "java/io/InputStream", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                              field(kPrivate, "closed", "Z"),
                              field(kPrivate, "ownsConnection", "Z"),
                              field(kPrivate, "buffer", "[B"),
                              field(kPrivate, "bufferPosition", "I"),
                              field(kPrivate, "bufferCount", "I"),
                          },
                          {
                              method(kPublic, "read", "()I"),
                              method(kPublic, "read", "([B)I"),
                              method(kPublic, "read", "([BII)I"),
                              method(kPublic, "skip", "(J)J"),
                              method(kPublic, "available", "()I"),
                              method(kPublic, "close", "()V"),
                          });
    }
    if (name == "javax/microedition/io/NativeOutputStream") {
        return make_class(name.data(), "java/io/OutputStream", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                              field(kPrivate, "closed", "Z"),
                              field(kPrivate, "ownsConnection", "Z"),
                          },
                          {
                              method(kPublic, "write", "(I)V"),
                              method(kPublic, "write", "([B)V"),
                              method(kPublic, "write", "([BII)V"),
                              method(kPublic, "flush", "()V"),
                              method(kPublic, "close", "()V"),
                          });
    }
    if (name == "javax/microedition/io/NativeSocketConnection") {
        auto methods = stream_methods();
        const std::vector<classfile::Method> socket_methods {
            method(kPublic, "getAddress", "()Ljava/lang/String;"),
            method(kPublic, "getLocalAddress", "()Ljava/lang/String;"),
            method(kPublic, "getPort", "()I"),
            method(kPublic, "getLocalPort", "()I"),
            method(kPublic, "setSocketOption", "(BI)V"),
            method(kPublic, "getSocketOption", "(B)I"),
        };
        methods.insert(methods.end(), socket_methods.begin(), socket_methods.end());
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                          }, std::move(methods),
                          {"javax/microedition/io/SocketConnection"});
    }
    if (name == "javax/microedition/io/NativeServerSocketConnection") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                          },
                          {
                              method(kPublic, "close", "()V"),
                              method(kPublic, "acceptAndOpen", "()Ljavax/microedition/io/StreamConnection;"),
                              method(kPublic, "getLocalAddress", "()Ljava/lang/String;"),
                              method(kPublic, "getLocalPort", "()I"),
                          },
                          {"javax/microedition/io/ServerSocketConnection"});
    }
    if (name == "javax/microedition/io/NativeDatagramConnection") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                          },
                          {
                              method(kPublic, "close", "()V"),
                              method(kPublic, "getMaximumLength", "()I"),
                              method(kPublic, "getNominalLength", "()I"),
                              method(kPublic, "send", "(Ljavax/microedition/io/Datagram;)V"),
                              method(kPublic, "receive", "(Ljavax/microedition/io/Datagram;)V"),
                              method(kPublic, "newDatagram", "(I)Ljavax/microedition/io/Datagram;"),
                              method(kPublic, "newDatagram", "(ILjava/lang/String;)Ljavax/microedition/io/Datagram;"),
                              method(kPublic, "newDatagram", "([BI)Ljavax/microedition/io/Datagram;"),
                              method(kPublic, "newDatagram", "([BILjava/lang/String;)Ljavax/microedition/io/Datagram;"),
                              method(kPublic, "newDatagram", "([BII)Ljavax/microedition/io/Datagram;"),
                              method(kPublic, "getLocalAddress", "()Ljava/lang/String;"),
                              method(kPublic, "getLocalPort", "()I"),
                          },
                          {"javax/microedition/io/UDPDatagramConnection"});
    }
    if (name == "javax/microedition/io/NativeDatagram") {
        auto methods = std::vector<classfile::Method> {
            method(kPublic, "getAddress", "()Ljava/lang/String;"),
            method(kPublic, "setAddress", "(Ljava/lang/String;)V"),
            method(kPublic, "setAddress", "(Ljavax/microedition/io/Datagram;)V"),
            method(kPublic, "getData", "()[B"),
            method(kPublic, "getOffset", "()I"),
            method(kPublic, "getLength", "()I"),
            method(kPublic, "setData", "([BII)V"),
            method(kPublic, "setLength", "(I)V"),
            method(kPublic, "reset", "()V"),
        };
        auto input = data_input_methods();
        auto output = data_output_methods();
        methods.insert(methods.end(), input.begin(), input.end());
        methods.insert(methods.end(), output.begin(), output.end());
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "data", "[B"),
                              field(kPrivate, "offset", "I"),
                              field(kPrivate, "length", "I"),
                              field(kPrivate, "position", "I"),
                              field(kPrivate, "address", "Ljava/lang/String;"),
                          }, std::move(methods),
                          {"javax/microedition/io/Datagram"});
    }
    if (name == "javax/microedition/io/NativeHttpConnection") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                          }, http_methods(),
                          {"javax/microedition/io/HttpConnection",
                           "javax/microedition/io/OutputConnection"});
    }
    if (name == "javax/microedition/io/NativeHttpsConnection") {
        auto methods = http_methods();
        methods.push_back(method(kPublic, "getSecurityInfo",
                                 "()Ljavax/microedition/io/SecurityInfo;"));
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "handle", "I"),
                              field(kPrivate, "generation", "I"),
                          }, std::move(methods),
                          {"javax/microedition/io/HttpsConnection",
                           "javax/microedition/io/OutputConnection"});
    }
    if (name == "javax/wireless/messaging/Message") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getAddress",
                                     "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "setAddress",
                                     "(Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "getTimestamp",
                                     "()Ljava/util/Date;"),
                          });
    }
    if (name == "javax/wireless/messaging/TextMessage") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getPayloadText",
                                     "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "setPayloadText",
                                     "(Ljava/lang/String;)V"),
                          },
                          {"javax/wireless/messaging/Message"});
    }
    if (name == "javax/wireless/messaging/BinaryMessage") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "getPayloadData",
                                     "()[B"),
                              method(kPublic | kAbstract, "setPayloadData",
                                     "([B)V"),
                          },
                          {"javax/wireless/messaging/Message"});
    }
    if (name == "javax/wireless/messaging/MessagePart") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "content", "[B"),
                              field(kPrivate, "contentType", "Ljava/lang/String;"),
                              field(kPrivate, "contentId", "Ljava/lang/String;"),
                              field(kPrivate, "contentLocation", "Ljava/lang/String;"),
                              field(kPrivate, "encoding", "Ljava/lang/String;"),
                          },
                          {
                              method(kPublic, "<init>",
                                     "([BIILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
                              method(kPublic, "<init>",
                                     "(Ljava/io/InputStream;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
                              method(kPublic, "getContent", "()[B"),
                              method(kPublic, "getContentAsStream", "()Ljava/io/InputStream;"),
                              method(kPublic, "getContentID", "()Ljava/lang/String;"),
                              method(kPublic, "getContentLocation", "()Ljava/lang/String;"),
                              method(kPublic, "getContentType", "()Ljava/lang/String;"),
                              method(kPublic, "getEncoding", "()Ljava/lang/String;"),
                              method(kPublic, "getLength", "()I"),
                          });
    }
    if (name == "javax/wireless/messaging/MultipartMessage") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract, "addAddress",
                                     "(Ljava/lang/String;Ljava/lang/String;)Z"),
                              method(kPublic | kAbstract, "addMessagePart",
                                     "(Ljavax/wireless/messaging/MessagePart;)V"),
                              method(kPublic | kAbstract, "getAddresses",
                                     "(Ljava/lang/String;)[Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getHeader",
                                     "(Ljava/lang/String;)Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getMessagePart",
                                     "(Ljava/lang/String;)Ljavax/wireless/messaging/MessagePart;"),
                              method(kPublic | kAbstract, "getMessageParts",
                                     "()[Ljavax/wireless/messaging/MessagePart;"),
                              method(kPublic | kAbstract, "getStartContentId",
                                     "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "getSubject",
                                     "()Ljava/lang/String;"),
                              method(kPublic | kAbstract, "removeAddress",
                                     "(Ljava/lang/String;Ljava/lang/String;)Z"),
                              method(kPublic | kAbstract, "removeAddresses",
                                     "(Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "removeMessagePart",
                                     "(Ljavax/wireless/messaging/MessagePart;)Z"),
                              method(kPublic | kAbstract, "removeMessagePartId",
                                     "(Ljava/lang/String;)Z"),
                              method(kPublic | kAbstract, "removeMessagePartLocation",
                                     "(Ljava/lang/String;)Z"),
                              method(kPublic | kAbstract, "setHeader",
                                     "(Ljava/lang/String;Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "setStartContentId",
                                     "(Ljava/lang/String;)V"),
                              method(kPublic | kAbstract, "setSubject",
                                     "(Ljava/lang/String;)V"),
                          },
                          {"javax/wireless/messaging/Message"});
    }
    if (name == "javax/wireless/messaging/MessageListener") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {},
                          {
                              method(kPublic | kAbstract,
                                     "notifyIncomingMessage",
                                     "(Ljavax/wireless/messaging/MessageConnection;)V"),
                          });
    }
    if (name == "javax/wireless/messaging/MessageConnection") {
        return make_class(name.data(), "java/lang/Object",
                          kPublic | kInterface | kAbstract,
                          {
                              field(kPublic | kStatic | kFinal,
                                    "TEXT_MESSAGE", "Ljava/lang/String;"),
                              field(kPublic | kStatic | kFinal,
                                    "BINARY_MESSAGE", "Ljava/lang/String;"),
                              field(kPublic | kStatic | kFinal,
                                    "MULTIPART_MESSAGE", "Ljava/lang/String;"),
                          },
                          {
                              method(kStatic, "<clinit>", "()V"),
                              method(kPublic | kAbstract, "newMessage",
                                     "(Ljava/lang/String;)Ljavax/wireless/messaging/Message;"),
                              method(kPublic | kAbstract, "newMessage",
                                     "(Ljava/lang/String;Ljava/lang/String;)Ljavax/wireless/messaging/Message;"),
                              method(kPublic | kAbstract, "send",
                                     "(Ljavax/wireless/messaging/Message;)V"),
                              method(kPublic | kAbstract, "receive",
                                     "()Ljavax/wireless/messaging/Message;"),
                              method(kPublic | kAbstract, "numberOfSegments",
                                     "(Ljavax/wireless/messaging/Message;)I"),
                              method(kPublic | kAbstract, "setMessageListener",
                                     "(Ljavax/wireless/messaging/MessageListener;)V"),
                          },
                          {"javax/microedition/io/Connection"});
    }
    if (name == "javax/wireless/messaging/SizeExceededException") {
        return make_class(name.data(), "java/io/IOException", kOrdinary, {},
                          {
                              method(kPublic, "<init>", "()V"),
                              method(kPublic, "<init>",
                                     "(Ljava/lang/String;)V"),
                          });
    }
    if (name == "javax/wireless/messaging/NativeMessageConnection") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "address", "Ljava/lang/String;"),
                              field(kPrivate, "mode", "I"),
                              field(kPrivate, "closed", "Z"),
                              field(kPrivate, "listener",
                                    "Ljavax/wireless/messaging/MessageListener;"),
                              field(kPrivate, "protocol", "Ljava/lang/String;"),
                          },
                          {
                              method(kPublic, "close", "()V"),
                              method(kPublic, "newMessage",
                                     "(Ljava/lang/String;)Ljavax/wireless/messaging/Message;"),
                              method(kPublic, "newMessage",
                                     "(Ljava/lang/String;Ljava/lang/String;)Ljavax/wireless/messaging/Message;"),
                              method(kPublic, "send",
                                     "(Ljavax/wireless/messaging/Message;)V"),
                              method(kPublic, "receive",
                                     "()Ljavax/wireless/messaging/Message;"),
                              method(kPublic, "numberOfSegments",
                                     "(Ljavax/wireless/messaging/Message;)I"),
                              method(kPublic, "setMessageListener",
                                     "(Ljavax/wireless/messaging/MessageListener;)V"),
                          },
                          {"javax/wireless/messaging/MessageConnection"});
    }
    if (name == "javax/wireless/messaging/NativeTextMessage") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "address", "Ljava/lang/String;"),
                              field(kPrivate, "timestamp", "J"),
                              field(kPrivate, "payload", "Ljava/lang/String;"),
                          },
                          {
                              method(kPublic, "getAddress", "()Ljava/lang/String;"),
                              method(kPublic, "setAddress", "(Ljava/lang/String;)V"),
                              method(kPublic, "getTimestamp", "()Ljava/util/Date;"),
                              method(kPublic, "getPayloadText", "()Ljava/lang/String;"),
                              method(kPublic, "setPayloadText", "(Ljava/lang/String;)V"),
                          },
                          {"javax/wireless/messaging/TextMessage"});
    }
    if (name == "javax/wireless/messaging/NativeMultipartMessage") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "address", "Ljava/lang/String;"),
                              field(kPrivate, "timestamp", "J"),
                              field(kPrivate, "subject", "Ljava/lang/String;"),
                              field(kPrivate, "startContentId", "Ljava/lang/String;"),
                              field(kPrivate, "parts", "[Ljavax/wireless/messaging/MessagePart;"),
                              field(kPrivate, "partCount", "I"),
                          },
                          {
                              method(kPublic, "getAddress", "()Ljava/lang/String;"),
                              method(kPublic, "setAddress", "(Ljava/lang/String;)V"),
                              method(kPublic, "getTimestamp", "()Ljava/util/Date;"),
                              method(kPublic, "addAddress", "(Ljava/lang/String;Ljava/lang/String;)Z"),
                              method(kPublic, "addMessagePart", "(Ljavax/wireless/messaging/MessagePart;)V"),
                              method(kPublic, "getAddresses", "(Ljava/lang/String;)[Ljava/lang/String;"),
                              method(kPublic, "getHeader", "(Ljava/lang/String;)Ljava/lang/String;"),
                              method(kPublic, "getMessagePart", "(Ljava/lang/String;)Ljavax/wireless/messaging/MessagePart;"),
                              method(kPublic, "getMessageParts", "()[Ljavax/wireless/messaging/MessagePart;"),
                              method(kPublic, "getStartContentId", "()Ljava/lang/String;"),
                              method(kPublic, "getSubject", "()Ljava/lang/String;"),
                              method(kPublic, "removeAddress", "(Ljava/lang/String;Ljava/lang/String;)Z"),
                              method(kPublic, "removeAddresses", "(Ljava/lang/String;)V"),
                              method(kPublic, "removeMessagePart", "(Ljavax/wireless/messaging/MessagePart;)Z"),
                              method(kPublic, "removeMessagePartId", "(Ljava/lang/String;)Z"),
                              method(kPublic, "removeMessagePartLocation", "(Ljava/lang/String;)Z"),
                              method(kPublic, "setHeader", "(Ljava/lang/String;Ljava/lang/String;)V"),
                              method(kPublic, "setStartContentId", "(Ljava/lang/String;)V"),
                              method(kPublic, "setSubject", "(Ljava/lang/String;)V"),
                          },
                          {"javax/wireless/messaging/MultipartMessage"});
    }
    if (name == "javax/wireless/messaging/NativeBinaryMessage") {
        return make_class(name.data(), "java/lang/Object", kOrdinary | kFinal,
                          {
                              field(kPrivate, "address", "Ljava/lang/String;"),
                              field(kPrivate, "timestamp", "J"),
                              field(kPrivate, "payload", "[B"),
                          },
                          {
                              method(kPublic, "getAddress", "()Ljava/lang/String;"),
                              method(kPublic, "setAddress", "(Ljava/lang/String;)V"),
                              method(kPublic, "getTimestamp", "()Ljava/util/Date;"),
                              method(kPublic, "getPayloadData", "()[B"),
                              method(kPublic, "setPayloadData", "([B)V"),
                          },
                          {"javax/wireless/messaging/BinaryMessage"});
    }
    return nullptr;
}

} // namespace

void register_connection_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_connection_class);
}

} // namespace phoneme::vm
