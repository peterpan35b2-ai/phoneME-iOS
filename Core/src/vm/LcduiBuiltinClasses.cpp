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
        method(kPublic, "setFont",
               "(ILjavax/microedition/lcdui/Font;)V"),
        method(kPublic, "getFont",
               "(I)Ljavax/microedition/lcdui/Font;"),
    };
}

[[nodiscard]] ClassPtr build_lcdui_class(std::string_view name) {
    if (name == "javax/microedition/lcdui/Choice") {
        return make_class("javax/microedition/lcdui/Choice", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "EXCLUSIVE", "I"),
            field(kPublic | kStatic | kFinal, "MULTIPLE", "I"),
            field(kPublic | kStatic | kFinal, "IMPLICIT", "I"),
            field(kPublic | kStatic | kFinal, "POPUP", "I"),
            field(kPublic | kStatic | kFinal, "TEXT_WRAP_DEFAULT", "I"),
            field(kPublic | kStatic | kFinal, "TEXT_WRAP_ON", "I"),
            field(kPublic | kStatic | kFinal, "TEXT_WRAP_OFF", "I"),
        }, {
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
            method(kPublic | kAbstract, "setFont",
                   "(ILjavax/microedition/lcdui/Font;)V"),
            method(kPublic | kAbstract, "getFont",
                   "(I)Ljavax/microedition/lcdui/Font;"),
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
            method(kPublic | kStatic, "createMutableARGB",
                   "(II)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "(Ljava/io/InputStream;)Ljavax/microedition/lcdui/Image;"),
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
            method(kPublic, "setRGB", "([IIIIIII)V"),
        });
    }
    if (name == "com/nokia/mid/ui/DirectGraphics") {
        const u16 api = kPublic | kAbstract;
        return make_class("com/nokia/mid/ui/DirectGraphics",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "FLIP_HORIZONTAL", "I"),
            field(kPublic | kStatic | kFinal, "FLIP_VERTICAL", "I"),
            field(kPublic | kStatic | kFinal, "ROTATE_90", "I"),
            field(kPublic | kStatic | kFinal, "ROTATE_180", "I"),
            field(kPublic | kStatic | kFinal, "ROTATE_270", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_BYTE_1_GRAY", "I"),
            field(kPublic | kStatic | kFinal,
                  "TYPE_BYTE_1_GRAY_VERTICAL", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_BYTE_2_GRAY", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_BYTE_4_GRAY", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_BYTE_8_GRAY", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_BYTE_332_RGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_USHORT_4444_ARGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_USHORT_444_RGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_USHORT_555_RGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_USHORT_1555_ARGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_USHORT_565_RGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_INT_888_RGB", "I"),
            field(kPublic | kStatic | kFinal, "TYPE_INT_8888_ARGB", "I"),
        }, {
            method(api, "setARGBColor", "(I)V"),
            method(api, "getAlphaComponent", "()I"),
            method(api, "getNativePixelFormat", "()I"),
            method(api, "drawImage",
                   "(Ljavax/microedition/lcdui/Image;IIII)V"),
            method(api, "drawTriangle", "(IIIIIII)V"),
            method(api, "fillTriangle", "(IIIIIII)V"),
            method(api, "fillPolygon", "([II[IIII)V"),
            method(api, "drawPolygon", "([II[IIII)V"),
            method(api, "drawPixels", "([B[BIIIIIIII)V"),
            method(api, "drawPixels", "([IZIIIIIIII)V"),
            method(api, "drawPixels", "([SZIIIIIIII)V"),
            method(api, "getPixels", "([B[BIIIIIII)V"),
            method(api, "getPixels", "([IIIIIIII)V"),
            method(api, "getPixels", "([SIIIIIII)V"),
        });
    }
    if (name == "com/nokia/mid/ui/DirectGraphicsImpl") {
        const u16 api = kPublic;
        return make_class("com/nokia/mid/ui/DirectGraphicsImpl",
                          "java/lang/Object", kOrdinary | kFinal, {
            field(kPrivate | kFinal, "graphics",
                  "Ljavax/microedition/lcdui/Graphics;"),
        }, {
            method(kPublic, "<init>",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
            method(api, "setARGBColor", "(I)V"),
            method(api, "getAlphaComponent", "()I"),
            method(api, "getNativePixelFormat", "()I"),
            method(api, "drawImage",
                   "(Ljavax/microedition/lcdui/Image;IIII)V"),
            method(api, "drawTriangle", "(IIIIIII)V"),
            method(api, "fillTriangle", "(IIIIIII)V"),
            method(api, "fillPolygon", "([II[IIII)V"),
            method(api, "drawPolygon", "([II[IIII)V"),
            method(api, "drawPixels", "([B[BIIIIIIII)V"),
            method(api, "drawPixels", "([IZIIIIIIII)V"),
            method(api, "drawPixels", "([SZIIIIIIII)V"),
            method(api, "getPixels", "([B[BIIIIIII)V"),
            method(api, "getPixels", "([IIIIIIII)V"),
            method(api, "getPixels", "([SIIIIIII)V"),
        }, {"com/nokia/mid/ui/DirectGraphics"});
    }
    if (name == "com/nokia/mid/ui/DirectUtils") {
        return make_class("com/nokia/mid/ui/DirectUtils",
                          "java/lang/Object", kOrdinary | kFinal, {}, {
            method(kPublic | kStatic, "getDirectGraphics",
                   "(Ljavax/microedition/lcdui/Graphics;)"
                   "Lcom/nokia/mid/ui/DirectGraphics;"),
            method(kPublic | kStatic, "createImage",
                   "(III)Ljavax/microedition/lcdui/Image;"),
            method(kPublic | kStatic, "createImage",
                   "([BII)Ljavax/microedition/lcdui/Image;"),
        });
    }
    if (name == "com/nokia/mid/ui/DeviceControl") {
        return make_class("com/nokia/mid/ui/DeviceControl",
                          "java/lang/Object", kOrdinary | kFinal, {}, {
            method(kPublic | kStatic, "setLights", "(II)V"),
            method(kPublic | kStatic, "flashLights", "(J)V"),
            method(kPublic | kStatic, "startVibra", "(IJ)V"),
            method(kPublic | kStatic, "stopVibra", "()V"),
        });
    }
    if (name == "com/nokia/mid/ui/FullCanvas") {
        return make_class("com/nokia/mid/ui/FullCanvas",
                          "javax/microedition/lcdui/Canvas",
                          kOrdinary | kAbstract, {
            field(kPublic | kStatic | kFinal, "KEY_UP_ARROW", "I"),
            field(kPublic | kStatic | kFinal, "KEY_DOWN_ARROW", "I"),
            field(kPublic | kStatic | kFinal, "KEY_LEFT_ARROW", "I"),
            field(kPublic | kStatic | kFinal, "KEY_RIGHT_ARROW", "I"),
            field(kPublic | kStatic | kFinal, "KEY_SOFTKEY1", "I"),
            field(kPublic | kStatic | kFinal, "KEY_SOFTKEY2", "I"),
            field(kPublic | kStatic | kFinal, "KEY_SOFTKEY3", "I"),
            field(kPublic | kStatic | kFinal, "KEY_SEND", "I"),
            field(kPublic | kStatic | kFinal, "KEY_END", "I"),
        }, {
            method(kProtected, "<init>", "()V"),
            method(kPublic, "addCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setCommandListener",
                   "(Ljavax/microedition/lcdui/CommandListener;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Canvas") {
        return make_class("javax/microedition/lcdui/Canvas",
                          "javax/microedition/lcdui/Displayable",
                          kOrdinary | kAbstract, {
            field(kPublic | kStatic | kFinal, "KEY_NUM0", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM1", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM2", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM3", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM4", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM5", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM6", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM7", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM8", "I"),
            field(kPublic | kStatic | kFinal, "KEY_NUM9", "I"),
            field(kPublic | kStatic | kFinal, "KEY_STAR", "I"),
            field(kPublic | kStatic | kFinal, "KEY_POUND", "I"),
            field(kPublic | kStatic | kFinal, "UP", "I"),
            field(kPublic | kStatic | kFinal, "DOWN", "I"),
            field(kPublic | kStatic | kFinal, "LEFT", "I"),
            field(kPublic | kStatic | kFinal, "RIGHT", "I"),
            field(kPublic | kStatic | kFinal, "FIRE", "I"),
            field(kPublic | kStatic | kFinal, "GAME_A", "I"),
            field(kPublic | kStatic | kFinal, "GAME_B", "I"),
            field(kPublic | kStatic | kFinal, "GAME_C", "I"),
            field(kPublic | kStatic | kFinal, "GAME_D", "I"),
        }, {
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
            method(kPublic | kFinal, "repaint", "()V"),
            method(kPublic | kFinal, "repaint", "(IIII)V"),
            method(kPublic | kFinal, "serviceRepaints", "()V"),
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
                          kOrdinary, {
            field(kPublic | kStatic | kFinal, "UP_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "DOWN_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "LEFT_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "RIGHT_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "FIRE_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "GAME_A_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "GAME_B_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "GAME_C_PRESSED", "I"),
            field(kPublic | kStatic | kFinal, "GAME_D_PRESSED", "I"),
        }, {
            method(kProtected, "<init>", "(Z)V"),
            method(kPublic, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
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
            field(kPublic | kStatic | kFinal, "HCENTER", "I"),
            field(kPublic | kStatic | kFinal, "VCENTER", "I"),
            field(kPublic | kStatic | kFinal, "LEFT", "I"),
            field(kPublic | kStatic | kFinal, "RIGHT", "I"),
            field(kPublic | kStatic | kFinal, "TOP", "I"),
            field(kPublic | kStatic | kFinal, "BOTTOM", "I"),
            field(kPublic | kStatic | kFinal, "BASELINE", "I"),
            field(kPublic | kStatic | kFinal, "SOLID", "I"),
            field(kPublic | kStatic | kFinal, "DOTTED", "I"),
        }, {
            method(kPublic, "setColor", "(I)V"),
            method(kPublic, "setColor", "(III)V"),
            method(kPublic, "getColor", "()I"),
            method(kPublic, "getDisplayColor", "(I)I"),
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
            method(kPublic, "clearARGBRect", "(IIII)V"),
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
            method(kPublic, "setARGBColor", "(I)V"),
            method(kPublic, "getAlphaComponent", "()I"),
            method(kPublic, "getNativePixelFormat", "()I"),
            method(kPublic, "drawImage",
                   "(Ljavax/microedition/lcdui/Image;IIII)V"),
            method(kPublic, "drawTriangle", "(IIIIIII)V"),
            method(kPublic, "fillTriangle", "(IIIIIII)V"),
            method(kPublic, "fillPolygon", "([II[IIII)V"),
            method(kPublic, "drawPolygon", "([II[IIII)V"),
            method(kPublic, "drawPixels", "([B[BIIIIIIII)V"),
            method(kPublic, "drawPixels", "([IZIIIIIIII)V"),
            method(kPublic, "drawPixels", "([SZIIIIIIII)V"),
            method(kPublic, "getPixels", "([B[BIIIIIII)V"),
            method(kPublic, "getPixels", "([IIIIIIII)V"),
            method(kPublic, "getPixels", "([SIIIIIII)V"),
        }, {"com/nokia/mid/ui/DirectGraphics"});
    }
    if (name == "javax/microedition/lcdui/Font") {
        return make_class("javax/microedition/lcdui/Font", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "face", "I"),
            field(kPrivate, "style", "I"),
            field(kPrivate, "size", "I"),
            field(kPublic | kStatic | kFinal, "FACE_SYSTEM", "I"),
            field(kPublic | kStatic | kFinal, "FACE_MONOSPACE", "I"),
            field(kPublic | kStatic | kFinal, "FACE_PROPORTIONAL", "I"),
            field(kPublic | kStatic | kFinal, "STYLE_PLAIN", "I"),
            field(kPublic | kStatic | kFinal, "STYLE_BOLD", "I"),
            field(kPublic | kStatic | kFinal, "STYLE_ITALIC", "I"),
            field(kPublic | kStatic | kFinal, "STYLE_UNDERLINED", "I"),
            field(kPublic | kStatic | kFinal, "SIZE_MEDIUM", "I"),
            field(kPublic | kStatic | kFinal, "SIZE_SMALL", "I"),
            field(kPublic | kStatic | kFinal, "SIZE_LARGE", "I"),
            field(kPublic | kStatic | kFinal, "FONT_STATIC_TEXT", "I"),
            field(kPublic | kStatic | kFinal, "FONT_INPUT_TEXT", "I"),
        }, {
            method(kPublic | kStatic, "getDefaultFont",
                   "()Ljavax/microedition/lcdui/Font;"),
            method(kPublic | kStatic, "getFont",
                   "(III)Ljavax/microedition/lcdui/Font;"),
            method(kPublic | kStatic, "getFont",
                   "(I)Ljavax/microedition/lcdui/Font;"),
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
            field(kPrivate, "ownerItemId", "I"),
            field(kPublic | kStatic | kFinal, "SCREEN", "I"),
            field(kPublic | kStatic | kFinal, "BACK", "I"),
            field(kPublic | kStatic | kFinal, "CANCEL", "I"),
            field(kPublic | kStatic | kFinal, "OK", "I"),
            field(kPublic | kStatic | kFinal, "HELP", "I"),
            field(kPublic | kStatic | kFinal, "STOP", "I"),
            field(kPublic | kStatic | kFinal, "EXIT", "I"),
            field(kPublic | kStatic | kFinal, "ITEM", "I"),
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
            field(kPrivate, "ticker", "Ljavax/microedition/lcdui/Ticker;"),
            field(kPrivate, "scrollPosition", "I"),
        }, {
            method(kProtected, "<init>", "()V"),
            method(kPublic, "getTitle", "()Ljava/lang/String;"),
            method(kPublic, "setTitle", "(Ljava/lang/String;)V"),
            method(kPublic, "addCommand", "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setCommandListener",
                   "(Ljavax/microedition/lcdui/CommandListener;)V"),
            method(kPublic, "setTicker",
                   "(Ljavax/microedition/lcdui/Ticker;)V"),
            method(kPublic, "getTicker",
                   "()Ljavax/microedition/lcdui/Ticker;"),
            method(kPublic, "getWidth", "()I"),
            method(kPublic, "getHeight", "()I"),
            method(kPublic, "isShown", "()Z"),
            method(kProtected, "sizeChanged", "(II)V"),
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
            field(kPrivate | kStatic, "ownerMidlet",
                  "Ljavax/microedition/midlet/MIDlet;"),
            field(kPrivate, "current",
                  "Ljavax/microedition/lcdui/Displayable;"),
            field(kPublic | kStatic | kFinal, "LIST_ELEMENT", "I"),
            field(kPublic | kStatic | kFinal, "CHOICE_GROUP_ELEMENT", "I"),
            field(kPublic | kStatic | kFinal, "ALERT", "I"),
            field(kPublic | kStatic | kFinal, "TAB", "I"),
            field(kPublic | kStatic | kFinal, "COLOR_BACKGROUND", "I"),
            field(kPublic | kStatic | kFinal, "COLOR_FOREGROUND", "I"),
            field(kPublic | kStatic | kFinal,
                  "COLOR_HIGHLIGHTED_BACKGROUND", "I"),
            field(kPublic | kStatic | kFinal,
                  "COLOR_HIGHLIGHTED_FOREGROUND", "I"),
            field(kPublic | kStatic | kFinal, "COLOR_BORDER", "I"),
            field(kPublic | kStatic | kFinal,
                  "COLOR_HIGHLIGHTED_BORDER", "I"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "getDisplay",
                   "(Ljavax/microedition/midlet/MIDlet;)"
                   "Ljavax/microedition/lcdui/Display;"),
            method(kPublic, "getCurrent",
                   "()Ljavax/microedition/lcdui/Displayable;"),
            method(kPublic, "setCurrent",
                   "(Ljavax/microedition/lcdui/Displayable;)V"),
            method(kPublic, "setCurrent",
                   "(Ljavax/microedition/lcdui/Alert;"
                   "Ljavax/microedition/lcdui/Displayable;)V"),
            method(kPublic, "setCurrentItem",
                   "(Ljavax/microedition/lcdui/Item;)V"),
            method(kPublic, "getColor", "(I)I"),
            method(kPublic, "getBorderStyle", "(Z)I"),
            method(kPublic, "isColor", "()Z"),
            method(kPublic, "numColors", "()I"),
            method(kPublic, "numAlphaLevels", "()I"),
            method(kPublic, "callSerially", "(Ljava/lang/Runnable;)V"),
            method(kPublic, "flashBacklight", "(I)Z"),
            method(kPublic, "vibrate", "(I)Z"),
            method(kPublic, "getBestImageWidth", "(I)I"),
            method(kPublic, "getBestImageHeight", "(I)I"),
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
            field(kPrivate, "itemCommands",
                  "[Ljavax/microedition/lcdui/Command;"),
            field(kPrivate, "itemCommandCount", "I"),
            field(kPrivate, "defaultCommand",
                  "Ljavax/microedition/lcdui/Command;"),
            field(kPrivate, "preferredWidth", "I"),
            field(kPrivate, "preferredHeight", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_DEFAULT", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_LEFT", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_RIGHT", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_CENTER", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_TOP", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_BOTTOM", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_VCENTER", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_NEWLINE_BEFORE", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_NEWLINE_AFTER", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_SHRINK", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_EXPAND", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_VSHRINK", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_VEXPAND", "I"),
            field(kPublic | kStatic | kFinal, "LAYOUT_2", "I"),
            field(kPublic | kStatic | kFinal, "PLAIN", "I"),
            field(kPublic | kStatic | kFinal, "HYPERLINK", "I"),
            field(kPublic | kStatic | kFinal, "BUTTON", "I"),
        }, {
            method(kProtected, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "getLabel", "()Ljava/lang/String;"),
            method(kPublic, "setLabel", "(Ljava/lang/String;)V"),
            method(kPublic, "getLayout", "()I"),
            method(kPublic, "setLayout", "(I)V"),
            method(kPublic, "addCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "removeCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setDefaultCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setItemCommandListener",
                   "(Ljavax/microedition/lcdui/ItemCommandListener;)V"),
            method(kPublic, "getPreferredWidth", "()I"),
            method(kPublic, "getPreferredHeight", "()I"),
            method(kPublic, "setPreferredSize", "(II)V"),
            method(kPublic, "getMinimumWidth", "()I"),
            method(kPublic, "getMinimumHeight", "()I"),
            method(kPublic, "notifyStateChanged", "()V"),
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
    if (name == "javax/microedition/lcdui/ItemStateListener") {
        return make_class("javax/microedition/lcdui/ItemStateListener",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "itemStateChanged",
                   "(Ljavax/microedition/lcdui/Item;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Form") {
        return make_class("javax/microedition/lcdui/Form",
                          "javax/microedition/lcdui/Screen", kOrdinary, {
            field(kPrivate, "items", "[Ljavax/microedition/lcdui/Item;"),
            field(kPrivate, "itemCount", "I"),
            field(kPrivate, "itemStateListener",
                  "Ljavax/microedition/lcdui/ItemStateListener;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;[Ljavax/microedition/lcdui/Item;)V"),
            method(kPublic, "append", "(Ljavax/microedition/lcdui/Item;)I"),
            method(kPublic, "append", "(Ljava/lang/String;)I"),
            method(kPublic, "append", "(Ljavax/microedition/lcdui/Image;)I"),
            method(kPublic, "insert", "(ILjavax/microedition/lcdui/Item;)V"),
            method(kPublic, "set", "(ILjavax/microedition/lcdui/Item;)V"),
            method(kPublic, "delete", "(I)V"),
            method(kPublic, "deleteAll", "()V"),
            method(kPublic, "get", "(I)Ljavax/microedition/lcdui/Item;"),
            method(kPublic, "setItemStateListener",
                   "(Ljavax/microedition/lcdui/ItemStateListener;)V"),
            method(kPublic, "size", "()I"),
            method(kPublic, "getWidth", "()I"),
            method(kPublic, "getHeight", "()I"),
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
            field(kPrivate, "fonts", "[Ljavax/microedition/lcdui/Font;"),
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
        methods.push_back(method(kPublic, "removeCommand",
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
            field(kPrivate, "fonts", "[Ljavax/microedition/lcdui/Font;"),
            field(kPublic | kStatic | kFinal, "SELECT_COMMAND",
                  "Ljavax/microedition/lcdui/Command;"),
        }, std::move(methods), {"javax/microedition/lcdui/Choice"});
    }
    if (name == "javax/microedition/lcdui/StringItem") {
        return make_class("javax/microedition/lcdui/StringItem",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "text", "Ljava/lang/String;"),
            field(kPrivate, "appearanceMode", "I"),
            field(kPrivate, "font", "Ljavax/microedition/lcdui/Font;"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;I)V"),
            method(kPublic, "getText", "()Ljava/lang/String;"),
            method(kPublic, "setText", "(Ljava/lang/String;)V"),
            method(kPublic, "getAppearanceMode", "()I"),
            method(kPublic, "getFont",
                   "()Ljavax/microedition/lcdui/Font;"),
            method(kPublic, "setFont",
                   "(Ljavax/microedition/lcdui/Font;)V"),
            method(kPublic, "setPreferredSize", "(II)V"),
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
            field(kPublic | kStatic | kFinal, "ANY", "I"),
            field(kPublic | kStatic | kFinal, "EMAILADDR", "I"),
            field(kPublic | kStatic | kFinal, "NUMERIC", "I"),
            field(kPublic | kStatic | kFinal, "PHONENUMBER", "I"),
            field(kPublic | kStatic | kFinal, "URL", "I"),
            field(kPublic | kStatic | kFinal, "DECIMAL", "I"),
            field(kPublic | kStatic | kFinal, "CONSTRAINT_MASK", "I"),
            field(kPublic | kStatic | kFinal, "PASSWORD", "I"),
            field(kPublic | kStatic | kFinal, "UNEDITABLE", "I"),
            field(kPublic | kStatic | kFinal, "SENSITIVE", "I"),
            field(kPublic | kStatic | kFinal, "NON_PREDICTIVE", "I"),
            field(kPublic | kStatic | kFinal, "INITIAL_CAPS_WORD", "I"),
            field(kPublic | kStatic | kFinal, "INITIAL_CAPS_SENTENCE", "I"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;II)V"),
            method(kPublic, "getString", "()Ljava/lang/String;"),
            method(kPublic, "setString", "(Ljava/lang/String;)V"),
            method(kPublic, "getChars", "([C)I"),
            method(kPublic, "setChars", "([CII)V"),
            method(kPublic, "insert", "(Ljava/lang/String;I)V"),
            method(kPublic, "insert", "([CIII)V"),
            method(kPublic, "delete", "(II)V"),
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
            field(kPublic | kStatic | kFinal, "INDEFINITE", "I"),
            field(kPublic | kStatic | kFinal, "CONTINUOUS_IDLE", "I"),
            field(kPublic | kStatic | kFinal, "INCREMENTAL_IDLE", "I"),
            field(kPublic | kStatic | kFinal, "CONTINUOUS_RUNNING", "I"),
            field(kPublic | kStatic | kFinal, "INCREMENTAL_UPDATING", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;ZII)V"),
            method(kPublic, "isInteractive", "()Z"),
            method(kPublic, "getMaxValue", "()I"),
            method(kPublic, "setMaxValue", "(I)V"),
            method(kPublic, "getValue", "()I"),
            method(kPublic, "setValue", "(I)V"),
            method(kPublic, "addCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setDefaultCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setItemCommandListener",
                   "(Ljavax/microedition/lcdui/ItemCommandListener;)V"),
            method(kPublic, "setLabel", "(Ljava/lang/String;)V"),
            method(kPublic, "setLayout", "(I)V"),
            method(kPublic, "setPreferredSize", "(II)V"),
        });
    }
    if (name == "javax/microedition/lcdui/DateField") {
        return make_class("javax/microedition/lcdui/DateField",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "date", "Ljava/util/Date;"),
            field(kPrivate, "inputMode", "I"),
            field(kPrivate, "timeZone", "Ljava/util/TimeZone;"),
            field(kPublic | kStatic | kFinal, "DATE", "I"),
            field(kPublic | kStatic | kFinal, "TIME", "I"),
            field(kPublic | kStatic | kFinal, "DATE_TIME", "I"),
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
            method(kPublic, "addCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setDefaultCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setLabel", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/ImageItem") {
        return make_class("javax/microedition/lcdui/ImageItem",
                          "javax/microedition/lcdui/Item", kOrdinary, {
            field(kPrivate, "image", "Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "altText", "Ljava/lang/String;"),
            field(kPrivate, "appearanceMode", "I"),
            field(kPrivate, "imageGeneration", "I"),
        }, {
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljavax/microedition/lcdui/Image;"
                   "ILjava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljavax/microedition/lcdui/Image;"
                   "ILjava/lang/String;I)V"),
            method(kPublic, "getImage",
                   "()Ljavax/microedition/lcdui/Image;"),
            method(kPublic, "setImage",
                   "(Ljavax/microedition/lcdui/Image;)V"),
            method(kPublic, "getAltText", "()Ljava/lang/String;"),
            method(kPublic, "setAltText", "(Ljava/lang/String;)V"),
            method(kPublic, "getAppearanceMode", "()I"),
            method(kPublic, "getLayout", "()I"),
            method(kPublic, "setLayout", "(I)V"),
        });
    }
    if (name == "javax/microedition/lcdui/Ticker") {
        return make_class("javax/microedition/lcdui/Ticker",
                          "java/lang/Object", kOrdinary | kFinal, {
            field(kPrivate, "text", "Ljava/lang/String;"),
            field(kPrivate, "ownerDisplayableId", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "setString", "(Ljava/lang/String;)V"),
            method(kPublic, "getString", "()Ljava/lang/String;"),
        });
    }
    if (name == "javax/microedition/lcdui/CustomItem") {
        return make_class("javax/microedition/lcdui/CustomItem",
                          "javax/microedition/lcdui/Item",
                          kOrdinary | kAbstract, {
            field(kPrivate, "paintImage",
                  "Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "paintGeneration", "I"),
            field(kProtected | kStatic | kFinal, "NONE", "I"),
            field(kProtected | kStatic | kFinal, "TRAVERSE_HORIZONTAL", "I"),
            field(kProtected | kStatic | kFinal, "TRAVERSE_VERTICAL", "I"),
            field(kProtected | kStatic | kFinal, "KEY_PRESS", "I"),
            field(kProtected | kStatic | kFinal, "KEY_RELEASE", "I"),
            field(kProtected | kStatic | kFinal, "KEY_REPEAT", "I"),
            field(kProtected | kStatic | kFinal, "POINTER_PRESS", "I"),
            field(kProtected | kStatic | kFinal, "POINTER_RELEASE", "I"),
            field(kProtected | kStatic | kFinal, "POINTER_DRAG", "I"),
        }, {
            method(kProtected, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "getGameAction", "(I)I"),
            method(kProtected | kFinal, "getInteractionModes", "()I"),
            method(kProtected | kAbstract, "getMinContentWidth", "()I"),
            method(kProtected | kAbstract, "getMinContentHeight", "()I"),
            method(kProtected | kAbstract, "getPrefContentWidth", "(I)I"),
            method(kProtected | kAbstract, "getPrefContentHeight", "(I)I"),
            method(kProtected, "sizeChanged", "(II)V"),
            method(kProtected | kFinal, "invalidate", "()V"),
            method(kProtected | kAbstract, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;II)V"),
            method(kProtected | kFinal, "repaint", "()V"),
            method(kProtected | kFinal, "repaint", "(IIII)V"),
            method(kProtected, "traverse", "(III[I)Z"),
            method(kProtected, "traverseOut", "()V"),
            method(kProtected, "keyPressed", "(I)V"),
            method(kProtected, "keyReleased", "(I)V"),
            method(kProtected, "keyRepeated", "(I)V"),
            method(kProtected, "pointerPressed", "(II)V"),
            method(kProtected, "pointerReleased", "(II)V"),
            method(kProtected, "pointerDragged", "(II)V"),
            method(kProtected, "showNotify", "()V"),
            method(kProtected, "hideNotify", "()V"),
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
            method(kPublic, "getChars", "([C)I"),
            method(kPublic, "setChars", "([CII)V"),
            method(kPublic, "insert", "(Ljava/lang/String;I)V"),
            method(kPublic, "insert", "([CIII)V"),
            method(kPublic, "delete", "(II)V"),
            method(kPublic, "getMaxSize", "()I"),
            method(kPublic, "setMaxSize", "(I)I"),
            method(kPublic, "size", "()I"),
            method(kPublic, "getCaretPosition", "()I"),
            method(kPublic, "setConstraints", "(I)V"),
            method(kPublic, "getConstraints", "()I"),
            method(kPublic, "setInitialInputMode", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/AlertType") {
        return make_class("javax/microedition/lcdui/AlertType",
                          "java/lang/Object", kOrdinary, {
            field(kPrivate, "nativeKind", "I"),
            field(kPublic | kStatic | kFinal, "INFO",
                  "Ljavax/microedition/lcdui/AlertType;"),
            field(kPublic | kStatic | kFinal, "WARNING",
                  "Ljavax/microedition/lcdui/AlertType;"),
            field(kPublic | kStatic | kFinal, "ERROR",
                  "Ljavax/microedition/lcdui/AlertType;"),
            field(kPublic | kStatic | kFinal, "ALARM",
                  "Ljavax/microedition/lcdui/AlertType;"),
            field(kPublic | kStatic | kFinal, "CONFIRMATION",
                  "Ljavax/microedition/lcdui/AlertType;"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(kProtected, "<init>", "()V"),
            method(kPublic, "playSound",
                   "(Ljavax/microedition/lcdui/Display;)Z"),
        });
    }
    if (name == "javax/microedition/lcdui/Alert") {
        return make_class("javax/microedition/lcdui/Alert",
                          "javax/microedition/lcdui/Screen", kOrdinary, {
            field(kPrivate, "alertText", "Ljava/lang/String;"),
            field(kPrivate, "alertImage", "Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "alertType",
                  "Ljavax/microedition/lcdui/AlertType;"),
            field(kPrivate, "timeout", "I"),
            field(kPrivate, "nextDisplayable",
                  "Ljavax/microedition/lcdui/Displayable;"),
            field(kPrivate, "imageGeneration", "I"),
            field(kPrivate, "indicator", "Ljavax/microedition/lcdui/Gauge;"),
            field(kPublic | kStatic | kFinal, "FOREVER", "I"),
            field(kPublic | kStatic | kFinal, "DISMISS_COMMAND",
                  "Ljavax/microedition/lcdui/Command;"),
        }, {
            method(kStatic, "<clinit>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;"
                   "Ljavax/microedition/lcdui/Image;"
                   "Ljavax/microedition/lcdui/AlertType;)V"),
            method(kPublic, "getString", "()Ljava/lang/String;"),
            method(kPublic, "setString", "(Ljava/lang/String;)V"),
            method(kPublic, "getImage",
                   "()Ljavax/microedition/lcdui/Image;"),
            method(kPublic, "setImage",
                   "(Ljavax/microedition/lcdui/Image;)V"),
            method(kPublic, "getType",
                   "()Ljavax/microedition/lcdui/AlertType;"),
            method(kPublic, "setType",
                   "(Ljavax/microedition/lcdui/AlertType;)V"),
            method(kPublic, "getTimeout", "()I"),
            method(kPublic, "setTimeout", "(I)V"),
            method(kPublic, "getDefaultTimeout", "()I"),
            method(kPublic, "getIndicator",
                   "()Ljavax/microedition/lcdui/Gauge;"),
            method(kPublic, "setIndicator",
                   "(Ljavax/microedition/lcdui/Gauge;)V"),
            method(kPublic, "addCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "removeCommand",
                   "(Ljavax/microedition/lcdui/Command;)V"),
            method(kPublic, "setCommandListener",
                   "(Ljavax/microedition/lcdui/CommandListener;)V"),
        });
    }

    return nullptr;
}

} // namespace

void register_lcdui_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_lcdui_class);
}

} // namespace phoneme::vm
