#include <cstdlib>
#include <iostream>
#include <string_view>

#include "phoneme/vm/BuiltinClassRegistry.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void require_method(const phoneme::vm::BuiltinClassRegistry::ClassPtr& klass,
                    std::string_view name,
                    std::string_view descriptor,
                    const char* message) {
    require(klass != nullptr && klass->find_method(name, descriptor) != nullptr,
            message);
}

void test_lang_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_lang_classes(registry);

    const auto object = registry.find("java/lang/Object");
    const auto string = registry.find("java/lang/String");
    const auto buffer = registry.find("java/lang/StringBuffer");
    const auto system = registry.find("java/lang/System");
    const auto runtime = registry.find("java/lang/Runtime");
    const auto thread = registry.find("java/lang/Thread");

    require(object != nullptr && object->super_name().empty(),
            "lang registry owns Object");
    require_method(object, "wait", "(JI)V",
                   "Object exposes nanosecond wait overload");
    const auto* clone = object->find_method("clone", "()Ljava/lang/Object;");
    require(clone != nullptr && (clone->access_flags & 0x0001U) != 0U,
            "CLDC Object.clone is public");
    require(string != nullptr && string->super_name() == "java/lang/Object",
            "lang registry owns String");
    require_method(string, "<init>", "([BIILjava/lang/String;)V",
                   "String exposes ranged charset constructor");
    require_method(string, "getBytes", "(Ljava/lang/String;)[B",
                   "String exposes charset encoder");
    require_method(buffer, "append", "(Ljava/lang/String;)Ljava/lang/StringBuffer;",
                   "StringBuffer append returns StringBuffer");
    require_method(system, "linkLegacyWtChain",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Z",
                   "System exposes legacy Zelix chain linker");
    require_method(runtime, "exit", "(I)V",
                   "Runtime exposes CLDC exit");
    require_method(thread, "<init>", "(Ljava/lang/String;)V",
                   "Thread exposes name constructor");
    require_method(thread, "getName", "()Ljava/lang/String;",
                   "Thread exposes name getter");
    require_method(thread, "checkAccess", "()V",
                   "Thread exposes CLDC access check");
    require_method(thread, "toString", "()Ljava/lang/String;",
                   "Thread exposes CLDC text form");
    require(registry.find("java/util/Vector") == nullptr,
            "lang registry does not claim util classes");
}

void test_io_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_io_classes(registry);

    const auto input = registry.find("java/io/InputStream");
    const auto data_input = registry.find("java/io/DataInputStream");
    const auto data_output = registry.find("java/io/DataOutputStream");
    const auto byte_output = registry.find("java/io/ByteArrayOutputStream");
    const auto interrupted = registry.find("java/io/InterruptedIOException");

    require(input != nullptr && input->super_name() == "java/lang/Object",
            "io registry owns InputStream");
    require(data_input != nullptr &&
                data_input->super_name() == "java/io/InputStream",
            "DataInputStream matches CLDC InputStream hierarchy");
    require(data_input->interfaces().size() == 1U &&
                data_input->interfaces().front() == "java/io/DataInput",
            "DataInputStream implements DataInput");
    require_method(data_input, "read", "()I",
                   "DataInputStream exposes direct byte reader");
    require_method(data_input, "markSupported", "()Z",
                   "DataInputStream exposes mark support");
    require_method(data_input, "readUTF", "()Ljava/lang/String;",
                   "DataInputStream exposes modified UTF reader");
    require(data_output != nullptr &&
                data_output->super_name() == "java/io/OutputStream",
            "DataOutputStream matches CLDC OutputStream hierarchy");
    require_method(data_output, "flush", "()V",
                   "DataOutputStream exposes flush");
    require_method(data_output, "writeUTF", "(Ljava/lang/String;)V",
                   "DataOutputStream exposes modified UTF writer");
    require_method(byte_output, "toByteArray", "()[B",
                   "ByteArrayOutputStream exposes byte extraction");
    require(interrupted != nullptr &&
                interrupted->super_name() == "java/io/IOException",
            "InterruptedIOException extends IOException");
    require(!interrupted->fields().empty() &&
                interrupted->fields().front().name == "bytesTransferred",
            "InterruptedIOException exposes bytesTransferred");
    require(registry.find("java/util/Calendar") == nullptr,
            "io registry does not claim util classes");
}

void test_util_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_util_classes(registry);

    const auto vector = registry.find("java/util/Vector");
    const auto stack = registry.find("java/util/Stack");
    const auto table = registry.find("java/util/Hashtable");
    const auto random = registry.find("java/util/Random");
    const auto calendar = registry.find("java/util/Calendar");
    const auto zone = registry.find("java/util/TimeZone");

    require(vector != nullptr && vector->fields().size() == 3U,
            "Vector preserves native field layout");
    require_method(vector, "elements", "()Ljava/util/Enumeration;",
                   "Vector exposes Enumeration");
    require(stack != nullptr && stack->super_name() == "java/util/Vector",
            "Stack extends Vector");
    require_method(table, "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   "Hashtable exposes put");
    require_method(random, "nextInt", "(I)I",
                   "Random exposes bounded nextInt");
    require_method(calendar, "getInstance",
                   "(Ljava/util/TimeZone;)Ljava/util/Calendar;",
                   "Calendar exposes timezone factory");
    require_method(zone, "getTimeZone",
                   "(Ljava/lang/String;)Ljava/util/TimeZone;",
                   "TimeZone exposes ID lookup");
    require(registry.find("java/io/DataInputStream") == nullptr,
            "util registry does not claim io classes");
}

void test_connection_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_connection_classes(registry);

    const auto notifier =
        registry.find("javax/microedition/io/StreamConnectionNotifier");
    const auto server =
        registry.find("javax/microedition/io/ServerSocketConnection");
    const auto secure =
        registry.find("javax/microedition/io/SecureConnection");
    const auto native_server =
        registry.find("javax/microedition/io/NativeServerSocketConnection");
    const auto certificate_exception =
        registry.find("javax/microedition/pki/CertificateException");

    require(notifier != nullptr && notifier->interfaces().size() == 1U &&
                notifier->interfaces().front() ==
                    "javax/microedition/io/Connection",
            "StreamConnectionNotifier extends Connection");
    require_method(notifier, "acceptAndOpen",
                   "()Ljavax/microedition/io/StreamConnection;",
                   "StreamConnectionNotifier exposes acceptAndOpen");
    require(server != nullptr && server->interfaces().size() == 1U &&
                server->interfaces().front() ==
                    "javax/microedition/io/StreamConnectionNotifier",
            "ServerSocketConnection extends StreamConnectionNotifier");
    require(secure != nullptr && secure->interfaces().size() == 1U &&
                secure->interfaces().front() ==
                    "javax/microedition/io/SocketConnection",
            "SecureConnection extends SocketConnection");
    require_method(secure, "getSecurityInfo",
                   "()Ljavax/microedition/io/SecurityInfo;",
                   "SecureConnection exposes TLS metadata");
    require(native_server != nullptr &&
                native_server->interfaces().size() == 1U &&
                native_server->interfaces().front() ==
                    "javax/microedition/io/ServerSocketConnection",
            "native server retains public notifier hierarchy");
    require(certificate_exception != nullptr &&
                certificate_exception->super_name() == "java/io/IOException",
            "CertificateException extends IOException");
    require_method(certificate_exception, "<init>",
                   "(Ljavax/microedition/pki/Certificate;B)V",
                   "CertificateException exposes reason constructor");
    require_method(certificate_exception, "getCertificate",
                   "()Ljavax/microedition/pki/Certificate;",
                   "CertificateException exposes failing certificate");
    require_method(certificate_exception, "getReason", "()B",
                   "CertificateException exposes failure reason");
}

void test_game_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_game_classes(registry);

    const auto layer =
        registry.find("javax/microedition/lcdui/game/Layer");
    const auto sprite =
        registry.find("javax/microedition/lcdui/game/Sprite");
    const auto tiled =
        registry.find("javax/microedition/lcdui/game/TiledLayer");
    const auto manager =
        registry.find("javax/microedition/lcdui/game/LayerManager");

    require(layer != nullptr &&
                layer->super_name() == "java/lang/Object",
            "game registry owns Layer");
    require(sprite != nullptr &&
                sprite->super_name() == "javax/microedition/lcdui/game/Layer",
            "Sprite extends Layer");
    require_method(sprite, "collidesWith",
                   "(Ljavax/microedition/lcdui/game/TiledLayer;Z)Z",
                   "Sprite exposes TiledLayer collision");
    require(tiled != nullptr &&
                tiled->super_name() == "javax/microedition/lcdui/game/Layer",
            "TiledLayer extends Layer");
    require_method(tiled, "createAnimatedTile", "(I)I",
                   "TiledLayer exposes animated tiles");
    require_method(manager, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;II)V",
                   "LayerManager exposes viewport painting");
    require(registry.find("javax/microedition/lcdui/Form") == nullptr,
            "game registry does not claim LCDUI screen classes");
}

void test_composed_registry() {
    const auto string = phoneme::vm::load_builtin_class("java/lang/String");
    const auto data_input =
        phoneme::vm::load_builtin_class("java/io/DataInputStream");
    const auto vector = phoneme::vm::load_builtin_class("java/util/Vector");
    const auto form =
        phoneme::vm::load_builtin_class("javax/microedition/lcdui/Form");
    const auto sprite =
        phoneme::vm::load_builtin_class("javax/microedition/lcdui/game/Sprite");

    require(string.has_value(), "composed registry resolves lang classes");
    require(data_input.has_value(), "composed registry resolves io classes");
    require(vector.has_value(), "composed registry resolves util classes");
    require(form.has_value(), "composed registry resolves lcdui classes");
    require(sprite.has_value(), "composed registry resolves Game API classes");
    require(!phoneme::vm::load_builtin_class("java/lang/ProcessBuilder").has_value(),
            "composed registry rejects unported classes");
}

void test_lcdui_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_lcdui_classes(registry);

    const auto choice = registry.find("javax/microedition/lcdui/Choice");
    const auto form = registry.find("javax/microedition/lcdui/Form");
    const auto list = registry.find("javax/microedition/lcdui/List");
    const auto text_field = registry.find("javax/microedition/lcdui/TextField");

    require(choice != nullptr && choice->interfaces().empty(),
            "Choice is declared as an interface class");
    require(form != nullptr &&
                form->super_name() == "javax/microedition/lcdui/Screen",
            "Form extends Screen");
    require_method(form, "append", "(Ljavax/microedition/lcdui/Item;)I",
                   "Form exposes Item append");
    require(list != nullptr && list->interfaces().size() == 1U &&
                list->interfaces().front() == "javax/microedition/lcdui/Choice",
            "List implements Choice");
    require_method(text_field, "setConstraints", "(I)V",
                   "TextField exposes constraints");
    require(registry.find("java/lang/String") == nullptr,
            "lcdui registry does not claim lang classes");
}

} // namespace

int main() {
    test_lang_registry();
    test_io_registry();
    test_util_registry();
    test_lcdui_registry();
    test_connection_registry();
    test_game_registry();
    test_composed_registry();
    std::cout << "Builtin registry package tests passed\n";
    return 0;
}
