#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_pim_class(
    std::string_view name) {
    const u16 api = kPublic | kAbstract;

    if (name == "javax/microedition/pim/PIMException") {
        return make_class(std::string(name), "java/lang/Exception", kOrdinary, {
            field(kPrivate, "reason", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;I)V"),
            method(kPublic, "getReason", "()I"),
        });
    }
    if (name == "javax/microedition/pim/FieldFullException") {
        return make_class(std::string(name), "java/lang/RuntimeException",
                          kOrdinary, {
            field(kPrivate, "field", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;I)V"),
            method(kPublic, "getField", "()I"),
        });
    }
    if (name == "javax/microedition/pim/PIMItem") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "ATTR_NONE", "I"),
            field(kPublic | kStatic | kFinal, "BINARY", "I"),
            field(kPublic | kStatic | kFinal, "BOOLEAN", "I"),
            field(kPublic | kStatic | kFinal, "DATE", "I"),
            field(kPublic | kStatic | kFinal, "INT", "I"),
            field(kPublic | kStatic | kFinal, "STRING", "I"),
            field(kPublic | kStatic | kFinal, "STRING_ARRAY", "I"),
        }, {
            method(api, "addBinary", "(II[BII)V"),
            method(api, "addBoolean", "(IIZ)V"),
            method(api, "addDate", "(IIJ)V"),
            method(api, "addInt", "(III)V"),
            method(api, "addString", "(IILjava/lang/String;)V"),
            method(api, "addStringArray", "(II[Ljava/lang/String;)V"),
            method(api, "commit", "()V"),
            method(api, "countValues", "(I)I"),
            method(api, "getAttributes", "(II)I"),
            method(api, "getBinary", "(II)[B"),
            method(api, "getBoolean", "(II)Z"),
            method(api, "getDate", "(II)J"),
            method(api, "getFields", "()[I"),
            method(api, "getInt", "(II)I"),
            method(api, "getPIMList", "()Ljavax/microedition/pim/PIMList;"),
            method(api, "getString", "(II)Ljava/lang/String;"),
            method(api, "getStringArray", "(II)[Ljava/lang/String;"),
            method(api, "isModified", "()Z"),
            method(api, "maxCategories", "()I"),
            method(api, "removeFromCategory", "(Ljava/lang/String;)V"),
            method(api, "removeValue", "(II)V"),
            method(api, "setBinary", "(III[BII)V"),
            method(api, "setBoolean", "(IIZ)V"),
            method(api, "setDate", "(IIJ)V"),
            method(api, "setInt", "(III)V"),
            method(api, "setString", "(IILjava/lang/String;)V"),
            method(api, "setStringArray", "(II[Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/microedition/pim/Contact") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "ADDR", "I"),
            field(kPublic | kStatic | kFinal, "EMAIL", "I"),
            field(kPublic | kStatic | kFinal, "FORMATTED_ADDR", "I"),
            field(kPublic | kStatic | kFinal, "FORMATTED_NAME", "I"),
            field(kPublic | kStatic | kFinal, "NAME", "I"),
            field(kPublic | kStatic | kFinal, "NICKNAME", "I"),
            field(kPublic | kStatic | kFinal, "NOTE", "I"),
            field(kPublic | kStatic | kFinal, "ORG", "I"),
            field(kPublic | kStatic | kFinal, "PHOTO", "I"),
            field(kPublic | kStatic | kFinal, "PHOTO_URL", "I"),
            field(kPublic | kStatic | kFinal, "PUBLIC_KEY", "I"),
            field(kPublic | kStatic | kFinal, "PUBLIC_KEY_STRING", "I"),
            field(kPublic | kStatic | kFinal, "REVISION", "I"),
            field(kPublic | kStatic | kFinal, "TEL", "I"),
            field(kPublic | kStatic | kFinal, "TITLE", "I"),
            field(kPublic | kStatic | kFinal, "UID", "I"),
            field(kPublic | kStatic | kFinal, "URL", "I"),
        }, {
            method(api, "addToCategory", "(Ljava/lang/String;)V"),
            method(api, "getCategories", "()[Ljava/lang/String;"),
        }, {"javax/microedition/pim/PIMItem"});
    }
    if (name == "javax/microedition/pim/PIMList") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "addCategory", "(Ljava/lang/String;)V"),
            method(api, "close", "()V"),
            method(api, "deleteCategory", "(Ljava/lang/String;Z)V"),
            method(api, "getArrayElementLabel", "(II)Ljava/lang/String;"),
            method(api, "getArraySize", "(I)I"),
            method(api, "getAttributeLabel", "(I)Ljava/lang/String;"),
            method(api, "getCategories", "()[Ljava/lang/String;"),
            method(api, "getFieldDataType", "(I)I"),
            method(api, "getFieldLabel", "(I)Ljava/lang/String;"),
            method(api, "getName", "()Ljava/lang/String;"),
            method(api, "getSupportedArrayElements", "(I)[I"),
            method(api, "getSupportedAttributes", "(I)[I"),
            method(api, "getSupportedFields", "()[I"),
            method(api, "isSupportedArrayElement", "(II)Z"),
            method(api, "isSupportedAttribute", "(II)Z"),
            method(api, "isSupportedField", "(I)Z"),
            method(api, "items", "()Ljava/util/Enumeration;"),
            method(api, "items", "(Ljavax/microedition/pim/PIMItem;)Ljava/util/Enumeration;"),
            method(api, "items", "(Ljava/lang/String;)Ljava/util/Enumeration;"),
            method(api, "itemsByCategory", "(Ljava/lang/String;)Ljava/util/Enumeration;"),
            method(api, "maxCategories", "()I"),
            method(api, "renameCategory", "(Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/microedition/pim/ContactList") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "createContact", "()Ljavax/microedition/pim/Contact;"),
            method(api, "importContact", "(Ljavax/microedition/pim/Contact;)Ljavax/microedition/pim/Contact;"),
            method(api, "removeContact", "(Ljavax/microedition/pim/Contact;)V"),
        }, {"javax/microedition/pim/PIMList"});
    }
    if (name == "javax/microedition/pim/PIM") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kAbstract | kSuper, {
            field(kPublic | kStatic | kFinal, "CONTACT_LIST", "I"),
            field(kPublic | kStatic | kFinal, "EVENT_LIST", "I"),
            field(kPublic | kStatic | kFinal, "TODO_LIST", "I"),
            field(kPublic | kStatic | kFinal, "READ_ONLY", "I"),
            field(kPublic | kStatic | kFinal, "WRITE_ONLY", "I"),
            field(kPublic | kStatic | kFinal, "READ_WRITE", "I"),
            field(kPublic | kStatic | kFinal, "SERIAL_FORMAT_VCARD_21", "Ljava/lang/String;"),
            field(kPublic | kStatic | kFinal, "SERIAL_FORMAT_VCARD_30", "Ljava/lang/String;"),
        }, {
            method(kProtected, "<init>", "()V"),
            method(kPublic | kStatic, "getInstance", "()Ljavax/microedition/pim/PIM;"),
            method(api, "fromSerialFormat", "(Ljava/io/InputStream;Ljava/lang/String;)[Ljavax/microedition/pim/PIMItem;"),
            method(api, "listPIMLists", "(I)[Ljava/lang/String;"),
            method(api, "openPIMList", "(II)Ljavax/microedition/pim/PIMList;"),
            method(api, "openPIMList", "(IILjava/lang/String;)Ljavax/microedition/pim/PIMList;"),
            method(api, "supportedSerialFormats", "(I)[Ljava/lang/String;"),
            method(api, "toSerialFormat", "(Ljavax/microedition/pim/PIMItem;Ljava/io/OutputStream;Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "phoneme/pim/PIMImpl") {
        return make_class(std::string(name), "javax/microedition/pim/PIM",
                          kOrdinary | kFinal, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "fromSerialFormat", "(Ljava/io/InputStream;Ljava/lang/String;)[Ljavax/microedition/pim/PIMItem;"),
            method(kPublic, "listPIMLists", "(I)[Ljava/lang/String;"),
            method(kPublic, "openPIMList", "(II)Ljavax/microedition/pim/PIMList;"),
            method(kPublic, "openPIMList", "(IILjava/lang/String;)Ljavax/microedition/pim/PIMList;"),
            method(kPublic, "supportedSerialFormats", "(I)[Ljava/lang/String;"),
            method(kPublic, "toSerialFormat", "(Ljavax/microedition/pim/PIMItem;Ljava/io/OutputStream;Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "phoneme/pim/EmptyPIMList") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "closed", "I"),
            field(kPrivate, "name", "Ljava/lang/String;"),
        }, {
            method(kPublic, "close", "()V"),
            method(kPublic, "getName", "()Ljava/lang/String;"),
            method(kPublic, "items", "()Ljava/util/Enumeration;"),
            method(kPublic, "items", "(Ljavax/microedition/pim/PIMItem;)Ljava/util/Enumeration;"),
            method(kPublic, "items", "(Ljava/lang/String;)Ljava/util/Enumeration;"),
            method(kPublic, "itemsByCategory", "(Ljava/lang/String;)Ljava/util/Enumeration;"),
            method(kPublic, "hasMoreElements", "()Z"),
            method(kPublic, "nextElement", "()Ljava/lang/Object;"),
            method(kPublic, "getCategories", "()[Ljava/lang/String;"),
            method(kPublic, "getSupportedFields", "()[I"),
            method(kPublic, "maxCategories", "()I"),
        }, {"javax/microedition/pim/PIMList", "java/util/Enumeration"});
    }

    return nullptr;
}

} // namespace

void register_pim_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_pim_class);
}

} // namespace phoneme::vm
