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

    require(object != nullptr && object->super_name().empty(),
            "lang registry owns Object");
    require(string != nullptr && string->super_name() == "java/lang/Object",
            "lang registry owns String");
    require_method(string, "<init>", "([BIILjava/lang/String;)V",
                   "String exposes ranged charset constructor");
    require_method(string, "getBytes", "(Ljava/lang/String;)[B",
                   "String exposes charset encoder");
    require_method(buffer, "append", "(Ljava/lang/String;)Ljava/lang/StringBuffer;",
                   "StringBuffer append returns StringBuffer");
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

    require(input != nullptr && input->super_name() == "java/lang/Object",
            "io registry owns InputStream");
    require(data_input != nullptr &&
                data_input->super_name() == "java/io/FilterInputStream",
            "DataInputStream extends FilterInputStream");
    require(data_input->interfaces().size() == 1U &&
                data_input->interfaces().front() == "java/io/DataInput",
            "DataInputStream implements DataInput");
    require_method(data_input, "readUTF", "()Ljava/lang/String;",
                   "DataInputStream exposes modified UTF reader");
    require_method(data_output, "writeUTF", "(Ljava/lang/String;)V",
                   "DataOutputStream exposes modified UTF writer");
    require_method(byte_output, "toByteArray", "()[B",
                   "ByteArrayOutputStream exposes byte extraction");
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

void test_composed_registry() {
    const auto string = phoneme::vm::load_builtin_class("java/lang/String");
    const auto data_input =
        phoneme::vm::load_builtin_class("java/io/DataInputStream");
    const auto vector = phoneme::vm::load_builtin_class("java/util/Vector");
    const auto form =
        phoneme::vm::load_builtin_class("javax/microedition/lcdui/Form");

    require(string.has_value(), "composed registry resolves lang classes");
    require(data_input.has_value(), "composed registry resolves io classes");
    require(vector.has_value(), "composed registry resolves util classes");
    require(form.has_value(), "composed registry resolves lcdui classes");
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
    test_composed_registry();
    std::cout << "Builtin registry package tests passed\n";
    return 0;
}
