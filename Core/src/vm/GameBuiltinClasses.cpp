#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_game_class(std::string_view name) {
    if (name == "javax/microedition/lcdui/game/Layer") {
        return make_class("javax/microedition/lcdui/game/Layer",
                          "java/lang/Object", kOrdinary | kAbstract, {
            field(kPrivate, "x", "I"),
            field(kPrivate, "y", "I"),
            field(kPrivate, "width", "I"),
            field(kPrivate, "height", "I"),
            field(kPrivate, "visible", "Z"),
        }, {
            method(kProtected, "<init>", "(II)V"),
            method(kPublic | kFinal, "getX", "()I"),
            method(kPublic | kFinal, "getY", "()I"),
            method(kPublic | kFinal, "getWidth", "()I"),
            method(kPublic | kFinal, "getHeight", "()I"),
            method(kPublic, "setPosition", "(II)V"),
            method(kPublic, "move", "(II)V"),
            method(kPublic, "setVisible", "(Z)V"),
            method(kPublic | kFinal, "isVisible", "()Z"),
            method(kPublic | kAbstract, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/game/Sprite") {
        return make_class("javax/microedition/lcdui/game/Sprite",
                          "javax/microedition/lcdui/game/Layer", kOrdinary, {
            field(kPrivate, "image", "Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "frameWidth", "I"),
            field(kPrivate, "frameHeight", "I"),
            field(kPrivate, "rawFrameCount", "I"),
            field(kPrivate, "frameSequence", "[I"),
            field(kPrivate, "sequenceIndex", "I"),
            field(kPrivate, "transform", "I"),
            field(kPrivate, "referenceX", "I"),
            field(kPrivate, "referenceY", "I"),
            field(kPrivate, "collisionX", "I"),
            field(kPrivate, "collisionY", "I"),
            field(kPrivate, "collisionWidth", "I"),
            field(kPrivate, "collisionHeight", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_NONE", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_MIRROR_ROT180", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_MIRROR", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_ROT180", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_MIRROR_ROT270", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_ROT90", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_ROT270", "I"),
            field(kPublic | kStatic | kFinal, "TRANS_MIRROR_ROT90", "I"),
        }, {
            method(kPublic, "<init>",
                   "(Ljavax/microedition/lcdui/Image;)V"),
            method(kPublic, "<init>",
                   "(Ljavax/microedition/lcdui/Image;II)V"),
            method(kPublic, "<init>",
                   "(Ljavax/microedition/lcdui/game/Sprite;)V"),
            method(kPublic, "setImage",
                   "(Ljavax/microedition/lcdui/Image;II)V"),
            method(kPublic, "defineReferencePixel", "(II)V"),
            method(kPublic, "setRefPixelPosition", "(II)V"),
            method(kPublic, "getRefPixelX", "()I"),
            method(kPublic, "getRefPixelY", "()I"),
            method(kPublic, "defineCollisionRectangle", "(IIII)V"),
            method(kPublic, "setFrame", "(I)V"),
            method(kPublic | kFinal, "getFrame", "()I"),
            method(kPublic, "getRawFrameCount", "()I"),
            method(kPublic, "getFrameSequenceLength", "()I"),
            method(kPublic, "setFrameSequence", "([I)V"),
            method(kPublic, "nextFrame", "()V"),
            method(kPublic, "prevFrame", "()V"),
            method(kPublic, "setTransform", "(I)V"),
            method(kPublic | kFinal, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
            method(kPublic | kFinal, "collidesWith",
                   "(Ljavax/microedition/lcdui/game/Sprite;Z)Z"),
            method(kPublic | kFinal, "collidesWith",
                   "(Ljavax/microedition/lcdui/game/TiledLayer;Z)Z"),
            method(kPublic | kFinal, "collidesWith",
                   "(Ljavax/microedition/lcdui/Image;IIZ)Z"),
        });
    }
    if (name == "javax/microedition/lcdui/game/TiledLayer") {
        return make_class("javax/microedition/lcdui/game/TiledLayer",
                          "javax/microedition/lcdui/game/Layer", kOrdinary, {
            field(kPrivate, "columns", "I"),
            field(kPrivate, "rows", "I"),
            field(kPrivate, "image", "Ljavax/microedition/lcdui/Image;"),
            field(kPrivate, "tileWidth", "I"),
            field(kPrivate, "tileHeight", "I"),
            field(kPrivate, "staticCount", "I"),
            field(kPrivate, "cells", "[I"),
            field(kPrivate, "animated", "[I"),
        }, {
            method(kPublic, "<init>",
                   "(IILjavax/microedition/lcdui/Image;II)V"),
            method(kPublic | kFinal, "getColumns", "()I"),
            method(kPublic | kFinal, "getRows", "()I"),
            method(kPublic | kFinal, "getCellWidth", "()I"),
            method(kPublic | kFinal, "getCellHeight", "()I"),
            method(kPublic, "getCell", "(II)I"),
            method(kPublic, "setCell", "(III)V"),
            method(kPublic, "fillCells", "(IIIII)V"),
            method(kPublic, "createAnimatedTile", "(I)I"),
            method(kPublic, "getAnimatedTile", "(I)I"),
            method(kPublic, "setAnimatedTile", "(II)V"),
            method(kPublic, "setStaticTileSet",
                   "(Ljavax/microedition/lcdui/Image;II)V"),
            method(kPublic | kFinal, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
        });
    }
    if (name == "javax/microedition/lcdui/game/LayerManager") {
        return make_class("javax/microedition/lcdui/game/LayerManager",
                          "java/lang/Object", kOrdinary, {
            field(kPrivate, "layers",
                  "[Ljavax/microedition/lcdui/game/Layer;"),
            field(kPrivate, "count", "I"),
            field(kPrivate, "viewX", "I"),
            field(kPrivate, "viewY", "I"),
            field(kPrivate, "viewWidth", "I"),
            field(kPrivate, "viewHeight", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "append",
                   "(Ljavax/microedition/lcdui/game/Layer;)V"),
            method(kPublic, "insert",
                   "(Ljavax/microedition/lcdui/game/Layer;I)V"),
            method(kPublic, "remove",
                   "(Ljavax/microedition/lcdui/game/Layer;)V"),
            method(kPublic, "getLayerAt",
                   "(I)Ljavax/microedition/lcdui/game/Layer;"),
            method(kPublic, "getSize", "()I"),
            method(kPublic, "setViewWindow", "(IIII)V"),
            method(kPublic, "paint",
                   "(Ljavax/microedition/lcdui/Graphics;II)V"),
        });
    }
    return nullptr;
}

} // namespace

void register_game_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_game_class);
}

} // namespace phoneme::vm
