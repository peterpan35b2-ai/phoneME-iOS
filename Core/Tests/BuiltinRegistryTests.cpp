#include <algorithm>
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

void require_method_flags(
    const phoneme::vm::BuiltinClassRegistry::ClassPtr& klass,
    std::string_view name,
    std::string_view descriptor,
    std::uint16_t mask,
    std::uint16_t expected,
    const char* message) {
    const auto* method = klass == nullptr
        ? nullptr
        : klass->find_method(name, descriptor);
    require(method != nullptr &&
                (method->access_flags & mask) == expected,
            message);
}

void test_lang_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_lang_classes(registry);

    const auto object = registry.find("java/lang/Object");
    const auto char_sequence = registry.find("java/lang/CharSequence");
    const auto string = registry.find("java/lang/String");
    const auto buffer = registry.find("java/lang/StringBuffer");
    const auto system = registry.find("java/lang/System");
    const auto runtime = registry.find("java/lang/Runtime");
    const auto thread = registry.find("java/lang/Thread");
    const auto integer = registry.find("java/lang/Integer");
    const auto throwable = registry.find("java/lang/Throwable");
    const auto no_class = registry.find("java/lang/NoClassDefFoundError");

    require(object != nullptr && object->super_name().empty(),
            "lang registry owns Object");
    require_method(object, "wait", "(JI)V",
                   "Object exposes nanosecond wait overload");
    const auto* clone = object->find_method("clone", "()Ljava/lang/Object;");
    require(clone != nullptr && (clone->access_flags & 0x0001U) != 0U,
            "CLDC Object.clone is public");
    require(char_sequence != nullptr &&
                (char_sequence->access_flags() & 0x0200U) != 0U,
            "CharSequence is exposed as an interface");
    require_method(char_sequence, "toString", "()Ljava/lang/String;",
                   "CharSequence exposes interface toString");
    require(string != nullptr && string->super_name() == "java/lang/Object" &&
                std::find(string->interfaces().begin(),
                          string->interfaces().end(),
                          "java/lang/CharSequence") != string->interfaces().end(),
            "lang registry owns String and implements CharSequence");
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
    require(integer != nullptr && integer->super_name() == "java/lang/Object",
            "CLDC Integer extends Object directly");
    require_method(throwable, "addSuppressed", "(Ljava/lang/Throwable;)V",
                   "Throwable exposes Java 8 suppressed exception support");
    require_method(throwable, "getSuppressed", "()[Ljava/lang/Throwable;",
                   "Throwable exposes suppressed exception snapshots");
    require(no_class != nullptr && no_class->super_name() == "java/lang/Error",
            "CLDC NoClassDefFoundError extends Error directly");
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
    const auto print_stream = registry.find("java/io/PrintStream");
    const auto charset = registry.find("java/nio/charset/Charset");

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
    require(print_stream != nullptr &&
                print_stream->super_name() == "java/io/OutputStream",
            "CLDC PrintStream extends OutputStream directly");
    require(print_stream != nullptr && !print_stream->fields().empty() &&
                print_stream->fields().front().name == "out",
            "PrintStream preserves native delegate layout");
    require_method(charset, "forName",
                   "(Ljava/lang/String;)Ljava/nio/charset/Charset;",
                   "Charset exposes Java-compatible name lookup");
    require(registry.find("java/util/Calendar") == nullptr,
            "io registry does not claim util classes");
}

void test_util_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_util_classes(registry);

    const auto list = registry.find("java/util/List");
    const auto array_list = registry.find("java/util/ArrayList");
    const auto array_deque = registry.find("java/util/ArrayDeque");
    const auto deque_iterator = registry.find("java/util/ArrayDequeIterator");
    const auto local_time = registry.find("java/time/LocalTime");
    const auto locale = registry.find("java/util/Locale");
    const auto vector = registry.find("java/util/Vector");
    const auto stack = registry.find("java/util/Stack");
    const auto table = registry.find("java/util/Hashtable");
    const auto random = registry.find("java/util/Random");
    const auto thread_local_random =
        registry.find("java/util/concurrent/ThreadLocalRandom");
    const auto calendar = registry.find("java/util/Calendar");
    const auto zone = registry.find("java/util/TimeZone");
    const auto zone_impl =
        registry.find("com/sun/cldc/util/j2me/TimeZoneImpl");
    const auto timer_task = registry.find("java/util/TimerTask");

    require(list != nullptr && (list->access_flags() & 0x0200U) != 0U,
            "List is exposed as an interface");
    require_method(list, "add", "(Ljava/lang/Object;)Z",
                   "List exposes object insertion");
    require_method(list, "of", "()Ljava/util/List;",
                   "List exposes Java 9 empty immutable factory");
    require(array_list != nullptr && array_list->fields().size() == 4U &&
                array_list->fields()[0].name == "elementData" &&
                array_list->fields()[1].name == "size" &&
                array_list->fields()[2].name == "capacityIncrement" &&
                array_list->fields()[3].name == "mutationMode" &&
                array_list->interfaces().size() == 1U &&
                array_list->interfaces().front() == "java/util/List",
            "ArrayList preserves native storage layout and implements List");
    require_method(array_list, "contains", "(Ljava/lang/Object;)Z",
                   "ArrayList exposes value lookup");
    require(array_deque != nullptr && array_deque->fields().size() == 2U &&
                array_deque->fields()[0].name == "elements" &&
                array_deque->fields()[1].name == "size" &&
                array_deque->interfaces().size() == 3U &&
                array_deque->interfaces()[0] == "java/util/Deque",
            "ArrayDeque preserves compact storage and implements Deque");
    require_method(array_deque, "push", "(Ljava/lang/Object;)V",
                   "ArrayDeque exposes stack insertion");
    require_method(array_deque, "descendingIterator",
                   "()Ljava/util/Iterator;",
                   "ArrayDeque exposes descending iteration");
    require(deque_iterator != nullptr &&
                deque_iterator->interfaces().size() == 1U &&
                deque_iterator->interfaces().front() == "java/util/Iterator",
            "ArrayDeque iterator implements Iterator");
    require_method(local_time, "of", "(II)Ljava/time/LocalTime;",
                   "LocalTime exposes hour/minute factory");
    require(locale != nullptr && !locale->fields().empty() &&
                locale->fields().front().name == "ROOT",
            "Locale exposes the locale-neutral ROOT constant");
    require_method(local_time, "withNano", "(I)Ljava/time/LocalTime;",
                   "LocalTime exposes immutable nano normalization");
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
    require(thread_local_random != nullptr &&
                thread_local_random->super_name() == "java/util/Random",
            "ThreadLocalRandom extends Random");
    require_method(thread_local_random, "current",
                   "()Ljava/util/concurrent/ThreadLocalRandom;",
                   "ThreadLocalRandom exposes current singleton lookup");
    require_method(thread_local_random, "nextLong", "(JJ)J",
                   "ThreadLocalRandom exposes ranged nextLong");
    require_method(calendar, "getInstance",
                   "(Ljava/util/TimeZone;)Ljava/util/Calendar;",
                   "Calendar exposes timezone factory");
    require_method(zone, "getTimeZone",
                   "(Ljava/lang/String;)Ljava/util/TimeZone;",
                   "TimeZone exposes ID lookup");
    require(zone != nullptr && (zone->access_flags() & 0x0400U) != 0U,
            "TimeZone remains abstract like CLDC");
    require_method_flags(zone, "getOffset", "(IIIIII)I",
                         0x0401U, 0x0401U,
                         "TimeZone.getOffset is public abstract");
    require(zone_impl != nullptr &&
                zone_impl->super_name() == "java/util/TimeZone",
            "phoneME TimeZoneImpl is the concrete implementation");
    require(timer_task != nullptr && timer_task->interfaces().size() == 1U &&
                timer_task->interfaces().front() == "java/lang/Runnable",
            "TimerTask implements Runnable");
    require(registry.find("java/io/DataInputStream") == nullptr,
            "util registry does not claim io classes");
}

void test_headless_compat_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_headless_compat_classes(registry);

    const auto iterable = registry.find("java/lang/Iterable");
    const auto closeable = registry.find("java/io/Closeable");
    const auto collection = registry.find("java/util/Collection");
    const auto iterator = registry.find("java/util/Iterator");
    const auto queue = registry.find("java/util/Queue");
    const auto deque = registry.find("java/util/Deque");
    const auto map = registry.find("java/util/Map");
    const auto hash_map = registry.find("java/util/HashMap");
    const auto hash_set = registry.find("java/util/HashSet");
    const auto arrays = registry.find("java/util/Arrays");
    const auto collections = registry.find("java/util/Collections");
    const auto encoder = registry.find("java/util/Base64$Encoder");

    require(iterable != nullptr && (iterable->access_flags() & 0x0200U) != 0U,
            "headless profile exposes Iterable as an interface");
    require_method(iterable, "iterator", "()Ljava/util/Iterator;",
                   "Iterable exposes iterator");
    require(closeable != nullptr && closeable->interfaces().size() == 1U &&
                closeable->interfaces().front() == "java/lang/AutoCloseable",
            "Closeable extends AutoCloseable");
    require(collection != nullptr && collection->interfaces().size() == 1U &&
                collection->interfaces().front() == "java/lang/Iterable",
            "Collection extends Iterable");
    require_method(iterator, "next", "()Ljava/lang/Object;",
                   "Iterator exposes next");
    require(queue != nullptr && queue->interfaces().size() == 1U &&
                queue->interfaces().front() == "java/util/Collection",
            "Queue extends Collection");
    require_method(queue, "poll", "()Ljava/lang/Object;",
                   "Queue exposes non-throwing removal");
    require(deque != nullptr && deque->interfaces().size() == 1U &&
                deque->interfaces().front() == "java/util/Queue",
            "Deque extends Queue");
    require_method(deque, "addFirst", "(Ljava/lang/Object;)V",
                   "Deque exposes front insertion");
    require_method(map, "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   "Map exposes put");
    require(hash_map != nullptr && hash_map->fields().size() == 4U &&
                hash_map->fields()[0].name == "keys" &&
                hash_map->fields()[0].descriptor == "[Ljava/lang/Object;" &&
                hash_map->fields()[1].name == "values" &&
                hash_map->fields()[1].descriptor == "[Ljava/lang/Object;" &&
                hash_map->fields()[2].name == "size" &&
                hash_map->fields()[2].descriptor == "I" &&
                hash_map->fields()[3].name == "hashes" &&
                hash_map->fields()[3].descriptor == "[I",
            "HashMap preserves hash-aware native storage layout");
    require_method(hash_map, "keySet", "()Ljava/util/Set;",
                   "HashMap exposes keySet");
    require(hash_set != nullptr && hash_set->fields().size() == 1U,
            "HashSet uses one compact backing-map field");
    require_method(arrays, "copyOf", "([BI)[B",
                   "Arrays exposes byte copyOf");
    require_method(collections, "sort", "(Ljava/util/List;)V",
                   "Collections exposes bounded list sorting");
    require_method(encoder, "encodeToString", "([B)Ljava/lang/String;",
                   "Base64 encoder exposes string output");
    require(registry.find("java/awt/Graphics") == nullptr &&
                registry.find("javax/swing/JFrame") == nullptr &&
                registry.find("java/sql/Connection") == nullptr,
            "headless profile does not claim desktop or server APIs");
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
    require_method_flags(sprite, "collidesWith",
                         "(Ljavax/microedition/lcdui/game/TiledLayer;Z)Z",
                         0x0011U, 0x0011U,
                         "Sprite collision methods are final");
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

void test_vendor_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_vendor_classes(registry);

    const auto system = registry.find("com/sprintpcs/util/System");
    const auto listener =
        registry.find("com/sprintpcs/util/SystemEventListener");
    require_method(system, "setExitURI", "(Ljava/lang/String;)V",
                   "SprintPCS System exposes exit URI compatibility");
    require_method(system, "getProtectedProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;",
                   "SprintPCS System exposes protected property lookup");
    require(listener != nullptr &&
                (listener->access_flags() & 0x0200U) != 0U,
            "SprintPCS SystemEventListener is an interface");
}

void test_xml_registry() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_xml_classes(registry);

    const auto factory = registry.find("javax/xml/parsers/SAXParserFactory");
    const auto parser = registry.find("javax/xml/parsers/SAXParser");
    const auto input = registry.find("org/xml/sax/InputSource");
    const auto handler = registry.find("org/xml/sax/helpers/DefaultHandler");
    const auto attributes = registry.find("org/xml/sax/Attributes");

    require_method(factory, "newInstance",
                   "()Ljavax/xml/parsers/SAXParserFactory;",
                   "JAXP exposes SAXParserFactory.newInstance");
    require_method(parser, "parse",
                   "(Lorg/xml/sax/InputSource;Lorg/xml/sax/helpers/DefaultHandler;)V",
                   "JAXP SAXParser exposes InputSource parse");
    require_method(input, "<init>", "(Ljava/io/InputStream;)V",
                   "SAX InputSource accepts InputStream");
    require(handler != nullptr && handler->interfaces().size() == 4U,
            "DefaultHandler implements the SAX callback interfaces");
    require(attributes != nullptr &&
                (attributes->access_flags() & 0x0200U) != 0U,
            "SAX Attributes is an interface");
}

void test_composed_registry() {
    const auto string = phoneme::vm::load_builtin_class("java/lang/String");
    const auto data_input =
        phoneme::vm::load_builtin_class("java/io/DataInputStream");
    const auto vector = phoneme::vm::load_builtin_class("java/util/Vector");
    const auto hash_map = phoneme::vm::load_builtin_class("java/util/HashMap");
    const auto arrays = phoneme::vm::load_builtin_class("java/util/Arrays");
    const auto base64 = phoneme::vm::load_builtin_class("java/util/Base64");
    const auto message_digest =
        phoneme::vm::load_builtin_class("java/security/MessageDigest");
    const auto form =
        phoneme::vm::load_builtin_class("javax/microedition/lcdui/Form");
    const auto sprite =
        phoneme::vm::load_builtin_class("javax/microedition/lcdui/game/Sprite");

    require(string.has_value(), "composed registry resolves lang classes");
    require(data_input.has_value(), "composed registry resolves io classes");
    require(vector.has_value(), "composed registry resolves util classes");
    require(hash_map.has_value(),
            "composed registry resolves headless HashMap compatibility");
    require(arrays.has_value(),
            "composed registry resolves headless Arrays compatibility");
    require(base64.has_value(),
            "composed registry resolves Java 8 Base64 compatibility");
    require(message_digest.has_value(),
            "composed registry resolves SHA-256 MessageDigest compatibility");
    require_method(*message_digest, "getInstance",
                   "(Ljava/lang/String;)Ljava/security/MessageDigest;",
                   "MessageDigest exposes Java-compatible factory");
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
    const auto canvas = registry.find("javax/microedition/lcdui/Canvas");
    const auto game_canvas =
        registry.find("javax/microedition/lcdui/game/GameCanvas");

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
    require_method_flags(canvas, "repaint", "()V",
                         0x0011U, 0x0011U,
                         "Canvas.repaint is public final");
    require_method_flags(game_canvas, "paint",
                         "(Ljavax/microedition/lcdui/Graphics;)V",
                         0x0005U, 0x0001U,
                         "GameCanvas.paint is public");
    require(registry.find("java/lang/String") == nullptr,
            "lcdui registry does not claim lang classes");
}

} // namespace

int main() {
    test_lang_registry();
    test_io_registry();
    test_util_registry();
    test_headless_compat_registry();
    test_lcdui_registry();
    test_connection_registry();
    test_game_registry();
    test_vendor_registry();
    test_xml_registry();
    test_composed_registry();
    std::cout << "Builtin registry package tests passed\n";
    return 0;
}
