#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] std::vector<classfile::Method> choice_methods(
    const char* short_constructor,
    const char* full_constructor) {
    return {
        method(kPublic, "<init>", short_constructor),
        method(kPublic, "<init>", full_constructor),
        method(kPublic, "size", "()I"),
        method(kPublic, "getString", "(I)Ljava/lang/String;"),
        method(kPublic, "getImage", "(I)Ljavax/microedition/lcdui/Image;"),
        method(kPublic, "append",
               "(Ljava/lang/String;Ljavax/microedition/lcdui/Image;)I"),
        method(kPublic, "insert",
               "(ILjava/lang/String;Ljavax/microedition/lcdui/Image;)V"),
        method(kPublic, "delete", "(I)V"),
        method(kPublic, "deleteAll", "()V"),
        method(kPublic, "set",
               "(ILjava/lang/String;Ljavax/microedition/lcdui/Image;)V"),
        method(kPublic, "isSelected", "(I)Z"),
        method(kPublic, "getSelectedIndex", "()I"),
        method(kPublic, "getSelectedFlags", "([Z)I"),
        method(kPublic, "setSelectedIndex", "(IZ)V"),
        method(kPublic, "setSelectedFlags", "([Z)V"),
        method(kPublic, "setFitPolicy", "(I)V"),
        method(kPublic, "getFitPolicy", "()I"),
    };
}

[[nodiscard]] ClassPtr build_lcdui_class(std::string_view name) {
    if (name == "javax/microedition/lcdui/Choice") {
        return make_class("javax/microedition/lcdui/Choice", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "size", "()I"),
            method(kPublic | kAbstract, "getString", "(I)Ljava/lang/String;"),
            method(kPublic | kAbstract, "getImage",
                   "(I)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kAbstract, "append",
                   "(Ljava/lang/String;Ljavax/microedition/lcdui/Image;)I"),
            method(kPublic | kAbstract, "insert",
                   "(ILjava/lang/String;Ljavax/microedition/lcdui/Image;)V"),
            method(kPublic | kAbstract, "delete", "(I)V"),
            method(kPublic | kAbstract, "deleteAll", "()V"),
            method(kPublic | kAbstract, "set",
                   "(ILjava/lang/String;Ljavax/microedition/lcdui/Image;)V"),
            method(kPublic | kAbstract, "isSelected", "(I)Z"),
            method(kPublic | kAbstract, "getSelectedIndex", "()I"),
            method(kPublic | kAbstract, "getSelectedFlags", "([Z)I"),
            method(kPublic | kAbstract, "setSelectedIndex", "(IZ)V"),
            method(kPublic | kAbstract, "setSelectedFlags", "([Z)V"),
            method(kPublic | kAbstract, "setFitPolicy", "(I)V"),
            method(kPublic | kAbstract, "getFitPolicy", "()I"),
        });
    }
    if (name == "javax/microedition/lcdui/Image") {
        return make_class("javax/microedition/lcdui/Image", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "width", "I"),
            field(kPrivate, "height", "I"),
            field(kPrivate, "mutable", "Z"),
        }, {
            method(kPublic | kStatic, "createImage",
                   "(II)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "([BII)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "(Ljavax/microedition/lcdui/Image;)"
                   "Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "(Ljavax/microedition/lcdui/Image;IIIII)"
                   "Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createRGBImage",
                   "([IIIZ)Ljavax/microedition/lcdui/Image;"),
            method(kPublic, "getGraphics",
                   "()Ljavax/microedition/lcdui/Graphics;"),
            method(kPublic, "getWidth", "()I"),
            method(kPublic, "getHeight", "()I"),
            method(kPublic, "isMutable", "()Z"),
            method(kPublic, "getRGB", "([IIIIIII)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Canvas") {
        return make_class("javax/microedition/lcdui/Canvas",
                          "javax/microedition/lcdui/Displayable",
                          kOrdinary | kAbstract, {}, {
            method(kProtected, "<init>", "()V"),
            method(kProtected | kAbstract, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
            method(kPublic, "getWidth", "()I"),
            method(kPublic, "getHeight", "()I"),
            method(kPublic, "isDoubleBuffered", "()Z"),
            method(kPublic, "hasPointerEvents", "()Z"),
            method(kPublic, "hasPointerMotionEvents", "()Z"),
            method(kPublic, "hasRepeatEvents", "()Z"),
            method(kPublic, "getGameAction", "(I)I"),
            method(kPublic, "getKeyCode", "(I)I"),
            method(kPublic, "getKeyName", "(I)Ljava/lang/String;"),
            method(kPublic, "repaint", "()V"),
            method(kPublic, "repaint", "(IIII)V"),
            method(kPublic, "serviceRepaints", "()V"),
            method(kPublic, "setFullScreenMode", "(Z)V"),
            method(kProtected, "keyPressed", "(I)V"),
            method(kProtected, "keyReleased", "(I)V"),
            method(kProtected, "keyRepeated", "(I)V"),
            method(kProtected, "pointerPressed", "(II)V"),
            method(kProtected, "pointerReleased", "(II)V"),
            method(kProtected, "pointerDragged", "(II)V"),
            method(kProtected, "showNotify", "()V"),
            method(kProtected, "hideNotify", "()V"),
            method(kProtected, "sizeChanged", "(II)V"),
        });
    }
    if (name == "javax/microedition/lcdui/game/GameCanvas") {
        return make_class("javax/microedition/lcdui/game/GameCanvas",
                          "javax/microedition/lcdui/Canvas",
                          kOrdinary | kAbstract, {}, {
            method(kProtected, "<init>", "(Z)V"),
            method(kPublic, "getKeyStates", "()I"),
            method(kProtected, "getGraphics",
                   "()Ljavax/microedition/lcdui/Graphics;"),
            method(kPublic, "flushGraphics", "()V"),
            method(kPublic, "flushGraphics", "(IIII)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Graphics") {
        return make_class("javax/microedition/lcdui/Graphics",
                          "java/lang/Object", kOrdinary | kFinal, {
            field(kPrivate, "target", "Ljavax/microedition/lcdui/Image;"),
        }, {
            method(kPublic, "setColor", "(I)V"),
            method(kPublic, "setColor", "(III)V"),
            method(kPublic, "getColor", "()I"),
            method(kPublic, "getRedComponent", "()I"),
            method(kPublic, "getGreenComponent", "()I"),
            method(kPublic, "getBlueComponent", "()I"),
            method(kPublic, "setGrayScale", "(I)V"),
            method(kPublic, "getGrayScale", "()I"),
            method(kPublic, "setStrokeStyle", "(I)V"),
            method(kPublic, "getStrokeStyle", "()I"),
            method(kPublic, "translate", "(II)V"),
            method(kPublic, "getTranslateX", "()I"),
            method(kPublic, "getTranslateY", "()I"),
            method(kPublic, "setClip", "(IIII)V"),
            method(kPublic, "clipRect", "(IIII)V"),
            method(kPublic, "getClipX", "()I"),
            method(kPublic, "getClipY", "()I"),
            method(kPublic, "getClipWidth", "()I"),
            method(kPublic, "getClipHeight", "()I"),
            method(kPublic, "setFont", "(Ljavax/microedition/lcdui/Font;)V"),
            method(kPublic, "getFont", "()Ljavax/microedition/lcdui/Font;"),
            method(kPublic, "drawLine", "(IIII)V"),
            method(kPublic, "fillRect", "(IIII)V"),
            method(kPublic, "drawRect", "(IIII)V"),
            method(kPublic, "drawRoundRect", "(IIIIII)V"),
            method(kPublic, "fillRoundRect", "(IIIIII)V"),
            method(kPublic, "drawArc", "(IIIIII)V"),
            method(kPublic, "fillArc", "(IIIIII)V"),
            method(kPublic, "fillTriangle", "(IIIIII)V"),
            method(kPublic, "drawImage",
                   "(Ljavax/microedition/lcdui/Image;III)V"),
            method(kPublic, "drawRegion",
                   "(Ljavax/microedition/lcdui/Image;IIIIIIII)V"),
            method(kPublic, "copyArea", "(IIIIIII)V"),
            method(kPublic, "drawRGB", "([IIIIIIIZ)V"),
            method(kPublic, "drawString", "(Ljava/lang/String;III)V"),
            method(kPublic, "drawSubstring",
                   "(Ljava/lang/String;IIIII)V"),
            method(kPublic, "drawChar", "(CIII)V"),
            method(kPublic, "drawChars", "([CIIIII)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Font") {
        return make_class("javax/microedition/lcdui/Font", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "face", "I"),
            field(kPrivate, "style", "I"),
            field(kPrivate, "size", "I"),
        }, {
            method(kPublic | kStatic, "getDefaultFont",
                   "()Ljavax/microedition/lcdui/Font;"),
            method(kPublic | kStatic, "getFont",
                   "(III)Ljavax/microedition/lcdui/Font;"),
            method(kPublic, "getFace", "()I"),
            method(kPublic, "getStyle", "()I"),
            method(kPublic, "getSize", "()I"),
            method(kPublic, "isPlain", "()Z"),
            method(kPublic, "isBold", "()Z"),
            method(kPublic, "isItalic", "()Z"),
            method(kPublic, "isUnderlined", "()Z"),
            method(kPublic, "getHeight", "()I"),
            method(kPublic, "getBaselinePosition", "()I"),
            method(kPublic, "charWidth", "(C)I"),
            method(kPublic, "charsWidth", "([CII)I"),
            method(kPublic, "stringWidth", "(Ljava/lang/String;)I"),
            method(kPublic, "substringWidth", "(Ljava/lang/String;II)I"),
        });
    }
    if (name == "javax/microedition/lcdui/Command") {
        return make_class("javax/microedition/lcdui/Command", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate, "nativeId", "I"),
            field(kPrivate, "label", "Ljava/lang/String;"),
            field(kPrivate, "longLabel", "Ljava/lang/String;"),
            field(kPrivate, "commandType", "I"),
            field(kPrivate, "priority", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;II)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;II)V"),
            method(kPublic, "getLabel", "()Ljava/lang/String;"),
            method(kPublic, "getLongLabel", "()Ljava/lang/String;"),
            method(kPublic, "getCommandType", "()I"),
            method(kPublic, "getPriority", "()I"),
        });
    }
    if (name == "javax/microedition/lcdui/CommandListener") {
        return make_class("javax/microedition/lcdui/CommandListener",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "commandAction",
                   "(Ljavax/microedition/lcdui/Command;"
                   "Ljavax/microedition/lcdui/Displayable;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Displayable") {
        return make_class("javax/microedition/lcdui/Displayable",
                          "java/lang/Object", kOrdinary | kAbstract, {
            field(kPrivate, "nativeId", "I"),
            field(kPrivate, "componentType", "I"),
            field(kPrivate, "title", "Ljava/lang/String;"),
            field(kPrivate, "commandListener",
                  "Ljavax/microedition/lcdui/CommandListener;"),
            field(kPrivate, "commands", "[Ljavax/microedition/lcdui/Command;"),
            field(kPrivate, "commandCount", "I"),
            field(kPrivate, "shown", "Z"),
        }, {
            method(kProtected, "<init>", "()V"),
            method(kPublic, "getTitle", "()Ljava/lang/String;"),
            method(kPublic, "setTitle", "(Ljava/lang/String;)V"),
            method(kPublic, "addCommand", "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setCommandListener",
                   "(Ljavax/microedition/lcdui/CommandListener;)V"),
            method(kPublic, "isShown", "()Z"),
        });
    }
    if (name == "javax/microedition/lcdui/Screen") {
        return make_class("javax/microedition/lcdui/Screen",
                          "javax/microedition/lcdui/Displayable",
                          kOrdinary | kAbstract, {}, {
            method(kProtected, "<init>", "()V"),
        });
    }
    if (name == "javax/microedition/lcdui/Display") {
        return make_class("javax/microedition/lcdui/Display", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kStatic, "singleton",
                  "Ljavax/microedition/lcdui/Display;"),
            field(kPrivate, "current",
                  "Ljavax/microedition/lcdui/Displayable;"),
        }, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "getDisplay",
                   "(Ljavax/microedition/midlet/MIDlet;)"
                   "Ljavax/microedition/lcdui/Display;"),
            method(kPublic, "getCurrent",
                   "()Ljavax/microedition/lcdui/Displayable;"),
            method(kPublic, "setCurrent",
                   "(Ljavax/microedition/lcdui/Displayable;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Item") {
        return make_class("javax/microedition/lcdui/Item", "java/lang/Object",
                          kOrdinary | kAbstract, {
            field(kPrivate, "nativeId", "I"),
            field(kPrivate, "componentType", "I"),
            field(kPrivate, "label", "Ljava/lang/String;"),
            field(kPrivate, "parentId", "I"),
            field(kPrivate, "layout", "I"),
            field(kPrivate, "itemCommandListener",
                  "Ljavax/microedition/lcdui/ItemCommandListener;"),
        }, {
            method(kProtected, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "getLabel", "()Ljava/lang/String;"),
            method(kPublic, "setLabel", "(Ljava/lang/String;)V"),
            method(kPublic, "getLayout", "()I"),
            method(kPublic, "setLayout", "(I)V"),
            method(kPublic, "setItemCommandListener",
                   "(Ljavax/microedition/lcdui/ItemCommandListener;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/ItemCommandListener") {
        return make_class("javax/microedition/lcdui/ItemCommandListener",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "commandAction",
                   "(Ljavax/microedition/lcdui/Command;"
                   "Ljavax/microedition/lcdui/Item;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Form") {
        return make_class("javax/microedition/lcdui/Form",
                          "javax/microedition/lcdui/Screen", kOrdinary, {
            field(kPrivate, "items", "[Ljavax/microedition/lcdui/Item;"),
            field(kPrivate, "itemCount", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;[Ljavax/microedition/lcdui/Item;)V"),
            method(kPublic, "append", "(Ljavax/microedition/lcdui/Item;)I"),
            method(kPublic, "append", "(Ljava/lang/String;)I"),
            method(kPublic, "insert", "(ILjavax/microedition/lcdui/Item;)V"),
            method(kPublic, "set", "(ILjavax/microedition/lcdui/Item;)V"),
            method(kPublic, "delete", "(I)V"),
            method(kPublic, "deleteAll", "()V"),
            method(kPublic, "get", "(I)Ljavax/microedition/lcdui/Item;"),
            method(kPublic, "size", "()I"),
        });
    }
    if (name == "javax/microedition/lcdui/ChoiceGroup") {
        return make_class("javax/microedition/lcdui/ChoiceGroup",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "choiceType", "I"),
            field(kPrivate, "strings", "[Ljava/lang/String;"),
            field(kPrivate, "images", "[Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "selected", "[Z"),
            field(kPrivate, "choiceCount", "I"),
            field(kPrivate, "fitPolicy", "I"),
        }, choice_methods("(Ljava/lang/String;I)V",
                          "(Ljava/lang/String;I[Ljava/lang/String;"
                          "[Ljavax/microedition/lcdui/Image;)V"),
                          {"javax/microedition/lcdui/Choice"});
    }
    if (name == "javax/microedition/lcdui/List") {
        auto methods = choice_methods(
            "(Ljava/lang/String;I)V",
            "(Ljava/lang/String;I[Ljava/lang/String;"
            "[Ljavax/microedition/lcdui/Image;)V");
        methods.insert(methods.begin(), method(kStatic, "<clinit>", "()V"));
        methods.push_back(method(kPublic, "setSelectCommand",
                                 "(Ljavax/microedition/lcdui/Command;)V"));
        return make_class("javax/microedition/lcdui/List",
                          "javax/microedition/lcdui/Screen", kOrdinary, {
            field(kPrivate, "choiceNativeId", "I"),
            field(kPrivate, "choiceType", "I"),
            field(kPrivate, "strings", "[Ljava/lang/String;"),
            field(kPrivate, "images", "[Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "selected", "[Z"),
            field(kPrivate, "choiceCount", "I"),
            field(kPrivate, "fitPolicy", "I"),
            field(kPrivate, "selectCommand",
                  "Ljavax/microedition/lcdui/Command;"),
            field(kPublic | kStatic | kFinal, "SELECT_COMMAND",
                  "Ljavax/microedition/lcdui/Command;"),
        }, std::move(methods), {"javax/microedition/lcdui/Choice"});
    }
    if (name == "javax/microedition/lcdui/StringItem") {
        return make_class("javax/microedition/lcdui/StringItem",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "text", "Ljava/lang/String;"),
            field(kPrivate, "appearanceMode", "I"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;I)V"),
            method(kPublic, "getText", "()Ljava/lang/String;"),
            method(kPublic, "setText", "(Ljava/lang/String;)V"),
            method(kPublic, "getAppearanceMode", "()I"),
        });
    }
    if (name == "javax/microedition/lcdui/TextField") {
        return make_class("javax/microedition/lcdui/TextField",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "text", "Ljava/lang/String;"),
            field(kPrivate, "maxSize", "I"),
            field(kPrivate, "constraints", "I"),
            field(kPrivate, "caretPosition", "I"),
            field(kPrivate, "initialInputMode", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;II)V"),
            method(kPublic, "getString", "()Ljava/lang/String;"),
            method(kPublic, "setString", "(Ljava/lang/String;)V"),
            method(kPublic, "getMaxSize", "()I"),
            method(kPublic, "setMaxSize", "(I)I"),
            method(kPublic, "size", "()I"),
            method(kPublic, "getCaretPosition", "()I"),
            method(kPublic, "setConstraints", "(I)V"),
            method(kPublic, "getConstraints", "()I"),
            method(kPublic, "setInitialInputMode", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Gauge") {
        return make_class("javax/microedition/lcdui/Gauge",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "interactive", "Z"),
            field(kPrivate, "maxValue", "I"),
            field(kPrivate, "value", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;ZII)V"),
            method(kPublic, "isInteractive", "()Z"),
            method(kPublic, "getMaxValue", "()I"),
            method(kPublic, "setMaxValue", "(I)V"),
            method(kPublic, "getValue", "()I"),
            method(kPublic, "setValue", "(I)V"),
        });
    }
    if (name == "javax/microedition/lcdui/DateField") {
        return make_class("javax/microedition/lcdui/DateField",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "date", "Ljava/util/Date;"),
            field(kPrivate, "inputMode", "I"),
            field(kPrivate, "timeZone", "Ljava/util/TimeZone;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;I)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;ILjava/util/TimeZone;)V"),
            method(kPublic, "getDate", "()Ljava/util/Date;"),
            method(kPublic, "setDate", "(Ljava/util/Date;)V"),
            method(kPublic, "getInputMode", "()I"),
            method(kPublic, "setInputMode", "(I)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Spacer") {
        return make_class("javax/microedition/lcdui/Spacer",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "minimumWidth", "I"),
            field(kPrivate, "minimumHeight", "I"),
        }, {
            method(kPublic, "<init>", "(II)V"),
            method(kPublic, "setMinimumSize", "(II)V"),
            method(kPublic, "getMinimumWidth", "()I"),
            method(kPublic, "getMinimumHeight", "()I"),
        });
    }
    if (name == "javax/microedition/lcdui/TextBox") {
        return make_class("javax/microedition/lcdui/TextBox",
                          "javax/microedition/lcdui/Screen", kOrdinary, {
            field(kPrivate, "peerId", "I"),
            field(kPrivate, "text", "Ljava/lang/String;"),
            field(kPrivate, "maxSize", "I"),
            field(kPrivate, "constraints", "I"),
            field(kPrivate, "caretPosition", "I"),
            field(kPrivate, "initialInputMode", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;II)V"),
            method(kPublic, "getString", "()Ljava/lang/String;"),
            method(kPublic, "setString", "(Ljava/lang/String;)V"),
            method(kPublic, "getMaxSize", "()I"),
            method(kPublic, "setMaxSize", "(I)I"),
            method(kPublic, "size", "()I"),
            method(kPublic, "getCaretPosition", "()I"),
            method(kPublic, "setConstraints", "(I)V"),
            method(kPublic, "getConstraints", "()I"),
            method(kPublic, "setInitialInputMode", "(Ljava/lang/String;)V"),
        });
    }

    return nullptr;
}

} // namespace

void register_lcdui_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_lcdui_class);
}

} // namespace phoneme::vm
