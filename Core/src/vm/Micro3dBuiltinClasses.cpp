#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string>
#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

constexpr const char* kPackage = "com/mascotcapsule/micro3d/v3/";

[[nodiscard]] std::string micro3d_name(std::string_view simple_name) {
    return std::string(kPackage) + std::string(simple_name);
}

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_micro3d_class(
    std::string_view name) {
    const std::string vector3d = micro3d_name("Vector3D");
    const std::string affine_trans = micro3d_name("AffineTrans");
    const std::string light = micro3d_name("Light");
    const std::string texture = micro3d_name("Texture");
    const std::string effect3d = micro3d_name("Effect3D");
    const std::string figure_layout = micro3d_name("FigureLayout");
    const std::string action_table = micro3d_name("ActionTable");
    const std::string figure = micro3d_name("Figure");
    const std::string graphics3d = micro3d_name("Graphics3D");

    if (name == vector3d) {
        return make_class(vector3d, "java/lang/Object", kOrdinary, {
            field(kPublic, "x", "I"),
            field(kPublic, "y", "I"),
            field(kPublic, "z", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(III)V"),
            method(kPublic, "<init>",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kFinal, "getX", "()I"),
            method(kPublic | kFinal, "getY", "()I"),
            method(kPublic | kFinal, "getZ", "()I"),
            method(kPublic | kFinal, "innerProduct",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)I"),
            method(kPublic | kStatic, "innerProduct",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;)I"),
            method(kPublic | kFinal, "outerProduct",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kStatic, "outerProduct",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;)"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;"),
            method(kPublic | kFinal, "set", "(III)V"),
            method(kPublic | kFinal, "set",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kFinal, "setX", "(I)V"),
            method(kPublic | kFinal, "setY", "(I)V"),
            method(kPublic | kFinal, "setZ", "(I)V"),
            method(kPublic | kFinal, "unit", "()V"),
        });
    }

    if (name == affine_trans) {
        return make_class(affine_trans, "java/lang/Object", kOrdinary, {
            field(kPublic, "m00", "I"), field(kPublic, "m01", "I"),
            field(kPublic, "m02", "I"), field(kPublic, "m03", "I"),
            field(kPublic, "m10", "I"), field(kPublic, "m11", "I"),
            field(kPublic, "m12", "I"), field(kPublic, "m13", "I"),
            field(kPublic, "m20", "I"), field(kPublic, "m21", "I"),
            field(kPublic, "m22", "I"), field(kPublic, "m23", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic, "<init>", "([I)V"),
            method(kPublic, "<init>", "([[I)V"),
            method(kPublic, "<init>", "([II)V"),
            method(kPublic, "<init>", "(IIIIIIIIIIII)V"),
            method(kPublic | kFinal, "get", "([I)V"),
            method(kPublic | kFinal, "get", "([II)V"),
            method(kPublic | kFinal, "lookAt",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kFinal, "mul",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "mul",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;"
                   "Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "multiply",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "multiply",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;"
                   "Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "rotationV",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;I)V"),
            method(kPublic | kFinal, "rotationX", "(I)V"),
            method(kPublic | kFinal, "rotationY", "(I)V"),
            method(kPublic | kFinal, "rotationZ", "(I)V"),
            method(kPublic | kFinal, "set",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "set", "([I)V"),
            method(kPublic | kFinal, "set", "([[I)V"),
            method(kPublic | kFinal, "set", "([II)V"),
            method(kPublic | kFinal, "set", "(IIIIIIIIIIII)V"),
            method(kPublic | kFinal, "setIdentity", "()V"),
            method(kPublic | kFinal, "setRotation",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;I)V"),
            method(kPublic | kFinal, "setRotationX", "(I)V"),
            method(kPublic | kFinal, "setRotationY", "(I)V"),
            method(kPublic | kFinal, "setRotationZ", "(I)V"),
            method(kPublic | kFinal, "setViewTrans",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kFinal, "transform",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;"),
            method(kPublic | kFinal, "transPoint",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)"
                   "Lcom/mascotcapsule/micro3d/v3/Vector3D;"),
        });
    }

    if (name == micro3d_name("Util3D")) {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary, {}, {
            method(kPublic | kStatic, "sqrt", "(I)I"),
            method(kPublic | kStatic, "sin", "(I)I"),
            method(kPublic | kStatic, "cos", "(I)I"),
        });
    }

    if (name == light) {
        return make_class(light, "java/lang/Object", kOrdinary, {
            field(kPrivate, "direction",
                  "Lcom/mascotcapsule/micro3d/v3/Vector3D;"),
            field(kPrivate, "dirIntensity", "I"),
            field(kPrivate, "ambIntensity", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;II)V"),
            method(kPublic | kFinal, "getAmbientIntensity", "()I"),
            method(kPublic | kFinal, "getAmbIntensity", "()I"),
            method(kPublic, "getDirection",
                   "()Lcom/mascotcapsule/micro3d/v3/Vector3D;"),
            method(kPublic | kFinal, "getDirIntensity", "()I"),
            method(kPublic | kFinal, "getParallelLightDirection",
                   "()Lcom/mascotcapsule/micro3d/v3/Vector3D;"),
            method(kPublic | kFinal, "getParallelLightIntensity", "()I"),
            method(kPublic | kFinal, "setAmbientIntensity", "(I)V"),
            method(kPublic | kFinal, "setAmbIntensity", "(I)V"),
            method(kPublic | kFinal, "setDirection",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kFinal, "setDirIntensity", "(I)V"),
            method(kPublic | kFinal, "setParallelLightDirection",
                   "(Lcom/mascotcapsule/micro3d/v3/Vector3D;)V"),
            method(kPublic | kFinal, "setParallelLightIntensity", "(I)V"),
        });
    }

    if (name == texture) {
        return make_class(texture, "java/lang/Object", kOrdinary, {
            field(kPrivate, "data", "[B"),
            field(kPrivate | kFinal, "isForModel", "Z"),
            field(kPrivate, "disposed", "Z"),
            field(kPrivate, "width", "I"),
            field(kPrivate, "height", "I"),
        }, {
            method(kPublic, "<init>", "([BZ)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Z)V"),
            method(kPublic | kFinal, "dispose", "()V"),
        });
    }

    if (name == effect3d) {
        return make_class(effect3d, "java/lang/Object", kOrdinary, {
            field(kPublic | kStatic | kFinal, "NORMAL_SHADING", "I"),
            field(kPublic | kStatic | kFinal, "TOON_SHADING", "I"),
            field(kPrivate, "light",
                  "Lcom/mascotcapsule/micro3d/v3/Light;"),
            field(kPrivate, "texture",
                  "Lcom/mascotcapsule/micro3d/v3/Texture;"),
            field(kPrivate, "shading", "I"),
            field(kPrivate, "toonHigh", "I"),
            field(kPrivate, "toonLow", "I"),
            field(kPrivate, "toonThreshold", "I"),
            field(kPrivate, "isTransparency", "Z"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>",
                   "(Lcom/mascotcapsule/micro3d/v3/Light;IZ"
                   "Lcom/mascotcapsule/micro3d/v3/Texture;)V"),
            method(kPublic | kFinal, "getLight",
                   "()Lcom/mascotcapsule/micro3d/v3/Light;"),
            method(kPublic | kFinal, "getShading", "()I"),
            method(kPublic | kFinal, "getShadingType", "()I"),
            method(kPublic | kFinal, "getSphereMap",
                   "()Lcom/mascotcapsule/micro3d/v3/Texture;"),
            method(kPublic | kFinal, "getSphereTexture",
                   "()Lcom/mascotcapsule/micro3d/v3/Texture;"),
            method(kPublic | kFinal, "getThreshold", "()I"),
            method(kPublic | kFinal, "getThresholdHigh", "()I"),
            method(kPublic | kFinal, "getThresholdLow", "()I"),
            method(kPublic | kFinal, "getToonHigh", "()I"),
            method(kPublic | kFinal, "getToonLow", "()I"),
            method(kPublic | kFinal, "getToonThreshold", "()I"),
            method(kPublic | kFinal, "isSemiTransparentEnabled", "()Z"),
            method(kPublic | kFinal, "isTransparency", "()Z"),
            method(kPublic | kFinal, "setLight",
                   "(Lcom/mascotcapsule/micro3d/v3/Light;)V"),
            method(kPublic | kFinal, "setSemiTransparentEnabled", "(Z)V"),
            method(kPublic | kFinal, "setShading", "(I)V"),
            method(kPublic | kFinal, "setShadingType", "(I)V"),
            method(kPublic | kFinal, "setSphereMap",
                   "(Lcom/mascotcapsule/micro3d/v3/Texture;)V"),
            method(kPublic | kFinal, "setSphereTexture",
                   "(Lcom/mascotcapsule/micro3d/v3/Texture;)V"),
            method(kPublic | kFinal, "setThreshold", "(III)V"),
            method(kPublic | kFinal, "setToonParams", "(III)V"),
            method(kPublic | kFinal, "setTransparency", "(Z)V"),
        });
    }

    if (name == figure_layout) {
        return make_class(figure_layout, "java/lang/Object", kOrdinary, {
            field(kPrivate, "affineArray",
                  "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;"),
            field(kPrivate, "affine",
                  "Lcom/mascotcapsule/micro3d/v3/AffineTrans;"),
            field(kPrivate, "scaleX", "I"), field(kPrivate, "scaleY", "I"),
            field(kPrivate, "centerX", "I"), field(kPrivate, "centerY", "I"),
            field(kPrivate, "parallelWidth", "I"),
            field(kPrivate, "parallelHeight", "I"),
            field(kPrivate, "near", "I"), field(kPrivate, "far", "I"),
            field(kPrivate, "angle", "I"),
            field(kPrivate, "perspectiveWidth", "I"),
            field(kPrivate, "perspectiveHeight", "I"),
            field(kPrivate, "projection", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;IIII)V"),
            method(kPublic | kFinal, "getAffineTrans",
                   "()Lcom/mascotcapsule/micro3d/v3/AffineTrans;"),
            method(kPublic | kFinal, "getCenterX", "()I"),
            method(kPublic | kFinal, "getCenterY", "()I"),
            method(kPublic | kFinal, "getParallelHeight", "()I"),
            method(kPublic | kFinal, "getParallelWidth", "()I"),
            method(kPublic | kFinal, "getScaleX", "()I"),
            method(kPublic | kFinal, "getScaleY", "()I"),
            method(kPublic | kFinal, "selectAffineTrans", "(I)V"),
            method(kPublic | kFinal, "setAffineTrans",
                   "(Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "setAffineTrans",
                   "([Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "setAffineTransArray",
                   "([Lcom/mascotcapsule/micro3d/v3/AffineTrans;)V"),
            method(kPublic | kFinal, "setCenter", "(II)V"),
            method(kPublic | kFinal, "setParallelSize", "(II)V"),
            method(kPublic | kFinal, "setPerspective", "(III)V"),
            method(kPublic | kFinal, "setPerspective", "(IIII)V"),
            method(kPublic | kFinal, "setScale", "(II)V"),
        });
    }

    if (name == action_table) {
        return make_class(action_table, "java/lang/Object", kOrdinary, {
            field(kPrivate, "data", "[B"),
            field(kPrivate, "actionFrames", "[I"),
            field(kPrivate, "disposed", "Z"),
        }, {
            method(kPublic, "<init>", "([B)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic | kFinal, "dispose", "()V"),
            method(kPublic | kFinal, "getNumAction", "()I"),
            method(kPublic | kFinal, "getNumActions", "()I"),
            method(kPublic | kFinal, "getNumFrame", "(I)I"),
            method(kPublic | kFinal, "getNumFrames", "(I)I"),
        });
    }

    if (name == figure) {
        return make_class(figure, "java/lang/Object", kOrdinary, {
            field(kPrivate, "data", "[B"),
            field(kPrivate, "textures",
                  "[Lcom/mascotcapsule/micro3d/v3/Texture;"),
            field(kPrivate, "textureIndex", "I"),
            field(kPrivate, "pattern", "I"),
            field(kPrivate, "postureTable",
                  "Lcom/mascotcapsule/micro3d/v3/ActionTable;"),
            field(kPrivate, "postureAction", "I"),
            field(kPrivate, "postureFrame", "I"),
            field(kPrivate, "numPatterns", "I"),
            field(kPrivate, "numTextures", "I"),
            field(kPrivate, "disposed", "Z"),
        }, {
            method(kPublic, "<init>", "([B)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic | kFinal, "dispose", "()V"),
            method(kPublic | kFinal, "getNumPattern", "()I"),
            method(kPublic | kFinal, "getNumTextures", "()I"),
            method(kPublic | kFinal, "getTexture",
                   "()Lcom/mascotcapsule/micro3d/v3/Texture;"),
            method(kPublic | kFinal, "selectTexture", "(I)V"),
            method(kPublic | kFinal, "setPattern", "(I)V"),
            method(kPublic | kFinal, "setPosture",
                   "(Lcom/mascotcapsule/micro3d/v3/ActionTable;II)V"),
            method(kPublic | kFinal, "setTexture",
                   "(Lcom/mascotcapsule/micro3d/v3/Texture;)V"),
            method(kPublic | kFinal, "setTexture",
                   "([Lcom/mascotcapsule/micro3d/v3/Texture;)V"),
        });
    }

    if (name == graphics3d) {
        return make_class(graphics3d, "java/lang/Object", kOrdinary, {
            field(kPublic | kStatic | kFinal, "COMMAND_AFFINE_INDEX", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_AMBIENT_LIGHT", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_ATTRIBUTE", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_CENTER", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_CLIP", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_DIRECTION_LIGHT", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_END", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_FLUSH", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_LIST_VERSION_1_0", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_NOP", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_PARALLEL_SCALE", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_PARALLEL_SIZE", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_PERSPECTIVE_FOV", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_PERSPECTIVE_WH", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_TEXTURE_INDEX", "I"),
            field(kPublic | kStatic | kFinal, "COMMAND_THRESHOLD", "I"),
            field(kPublic | kStatic | kFinal, "ENV_ATTR_LIGHTING", "I"),
            field(kPublic | kStatic | kFinal, "ENV_ATTR_SEMI_TRANSPARENT", "I"),
            field(kPublic | kStatic | kFinal, "ENV_ATTR_SPHERE_MAP", "I"),
            field(kPublic | kStatic | kFinal, "ENV_ATTR_TOON_SHADING", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_BLEND_ADD", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_BLEND_HALF", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_BLEND_NORMAL", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_BLEND_SUB", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_COLORKEY", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_LIGHTING", "I"),
            field(kPublic | kStatic | kFinal, "PATTR_SPHERE_MAP", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_COLOR_NONE", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_COLOR_PER_COMMAND", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_COLOR_PER_FACE", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_NORMAL_NONE", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_NORMAL_PER_FACE", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_NORMAL_PER_VERTEX", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_POINT_SPRITE_PARAMS_PER_CMD", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_POINT_SPRITE_PARAMS_PER_FACE", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_POINT_SPRITE_PARAMS_PER_VERTEX", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_TEXURE_COORD", "I"),
            field(kPublic | kStatic | kFinal, "PDATA_TEXURE_COORD_NONE", "I"),
            field(kPublic | kStatic | kFinal, "POINT_SPRITE_LOCAL_SIZE", "I"),
            field(kPublic | kStatic | kFinal, "POINT_SPRITE_NO_PERS", "I"),
            field(kPublic | kStatic | kFinal, "POINT_SPRITE_PERSPECTIVE", "I"),
            field(kPublic | kStatic | kFinal, "POINT_SPRITE_PIXEL_SIZE", "I"),
            field(kPublic | kStatic | kFinal, "PRIMITVE_LINES", "I"),
            field(kPublic | kStatic | kFinal, "PRIMITVE_POINTS", "I"),
            field(kPublic | kStatic | kFinal, "PRIMITVE_POINT_SPRITES", "I"),
            field(kPublic | kStatic | kFinal, "PRIMITVE_QUADS", "I"),
            field(kPublic | kStatic | kFinal, "PRIMITVE_TRIANGLES", "I"),
            field(kPrivate, "graphics", "Ljavax/microedition/lcdui/Graphics;"),
            field(kPrivate, "bound", "Z"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kFinal | kSynchronized, "bind",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
            method(kPublic | kFinal, "dispose", "()V"),
            method(kPublic | kFinal, "drawCommandList",
                   "([Lcom/mascotcapsule/micro3d/v3/Texture;II"
                   "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
                   "Lcom/mascotcapsule/micro3d/v3/Effect3D;[I)V"),
            method(kPublic | kFinal, "drawCommandList",
                   "(Lcom/mascotcapsule/micro3d/v3/Texture;II"
                   "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
                   "Lcom/mascotcapsule/micro3d/v3/Effect3D;[I)V"),
            method(kPublic | kFinal, "drawFigure",
                   "(Lcom/mascotcapsule/micro3d/v3/Figure;II"
                   "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
                   "Lcom/mascotcapsule/micro3d/v3/Effect3D;)V"),
            method(kPublic | kFinal, "flush", "()V"),
            method(kPublic | kFinal | kSynchronized, "release",
                   "(Ljavax/microedition/lcdui/Graphics;)V"),
            method(kPublic | kFinal, "renderFigure",
                   "(Lcom/mascotcapsule/micro3d/v3/Figure;II"
                   "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
                   "Lcom/mascotcapsule/micro3d/v3/Effect3D;)V"),
            method(kPublic | kFinal, "renderPrimitives",
                   "(Lcom/mascotcapsule/micro3d/v3/Texture;II"
                   "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
                   "Lcom/mascotcapsule/micro3d/v3/Effect3D;II[I[I[I[I)V"),
        });
    }

    return nullptr;
}

} // namespace

void register_micro3d_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_micro3d_class);
}

} // namespace phoneme::vm
