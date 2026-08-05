#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

constexpr const char* kObject3D = "javax/microedition/m3g/Object3D";
constexpr const char* kTransformable =
    "javax/microedition/m3g/Transformable";
constexpr const char* kNode = "javax/microedition/m3g/Node";

[[nodiscard]] classfile::Method api_method(std::string name,
                                           std::string descriptor,
                                           u16 flags = kPublic) {
    return classfile::Method {
        .access_flags = flags,
        .name = std::move(name),
        .descriptor = std::move(descriptor),
        .code = std::nullopt,
    };
}

// Keep these builders split. Clang materializes the branch initializer
// temporaries in the function stack frame, and one monolithic M3G factory
// overflowed the smaller native Java-thread stack under ASan.
[[nodiscard]] ClassPtr m3g_base_class(std::string_view name) {
    if (name == kObject3D) {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kAbstract, {
            field(kPrivate, "userID", "I"),
            field(kPrivate, "userObject", "Ljava/lang/Object;"),
            field(kPrivate, "animationTracks", "[Ljavax/microedition/m3g/AnimationTrack;"),
            field(kPrivate, "animationTrackCount", "I"),
        }, {
            api_method("<init>", "()V", kProtected),
            api_method("animate", "(I)I"),
            api_method("duplicate", "()Ljavax/microedition/m3g/Object3D;"),
            api_method("find", "(I)Ljavax/microedition/m3g/Object3D;"),
            api_method("getReferences", "([Ljavax/microedition/m3g/Object3D;)I"),
            api_method("getUserID", "()I"),
            api_method("setUserID", "(I)V"),
            api_method("getUserObject", "()Ljava/lang/Object;"),
            api_method("setUserObject", "(Ljava/lang/Object;)V"),
            api_method("addAnimationTrack", "(Ljavax/microedition/m3g/AnimationTrack;)V"),
            api_method("removeAnimationTrack", "(Ljavax/microedition/m3g/AnimationTrack;)V"),
            api_method("getAnimationTrack", "(I)Ljavax/microedition/m3g/AnimationTrack;"),
            api_method("getAnimationTrackCount", "()I"),
        });
    }
    if (name == kTransformable) {
        return make_class(std::string(name), kObject3D,
                          kOrdinary | kAbstract, {
            field(kPrivate, "localTransform", "Ljavax/microedition/m3g/Transform;"),
            field(kPrivate, "genericTransform", "Ljavax/microedition/m3g/Transform;"),
            field(kPrivate, "translationX", "F"),
            field(kPrivate, "translationY", "F"),
            field(kPrivate, "translationZ", "F"),
            field(kPrivate, "scaleX", "F"),
            field(kPrivate, "scaleY", "F"),
            field(kPrivate, "scaleZ", "F"),
            field(kPrivate, "orientationX", "F"),
            field(kPrivate, "orientationY", "F"),
            field(kPrivate, "orientationZ", "F"),
            field(kPrivate, "orientationW", "F"),
        }, {
            api_method("<init>", "()V", kProtected),
            api_method("setTransform", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("getTransform", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("getCompositeTransform", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("setTranslation", "(FFF)V"),
            api_method("getTranslation", "([F)V"),
            api_method("translate", "(FFF)V"),
            api_method("setScale", "(FFF)V"),
            api_method("getScale", "([F)V"),
            api_method("scale", "(FFF)V"),
            api_method("setOrientation", "(FFFF)V"),
            api_method("getOrientation", "([F)V"),
            api_method("postRotate", "(FFFF)V"),
            api_method("preRotate", "(FFFF)V"),
        });
    }
    if (name == kNode) {
        return make_class(std::string(name), kTransformable,
                          kOrdinary | kAbstract, {
            field(kPrivate, "parent", "Ljavax/microedition/m3g/Node;"),
            field(kPrivate, "renderingEnabled", "Z"),
            field(kPrivate, "pickingEnabled", "Z"),
            field(kPrivate, "alphaFactor", "F"),
            field(kPrivate, "scope", "I"),
            field(kPrivate, "zTarget", "Ljavax/microedition/m3g/Node;"),
            field(kPrivate, "yTarget", "Ljavax/microedition/m3g/Node;"),
            field(kPrivate, "zReference", "I"),
            field(kPrivate, "yReference", "I"),
        }, {
            api_method("<init>", "()V", kProtected),
            api_method("getParent", "()Ljavax/microedition/m3g/Node;"),
            api_method("setRenderingEnable", "(Z)V"),
            api_method("isRenderingEnabled", "()Z"),
            api_method("setPickingEnable", "(Z)V"),
            api_method("isPickingEnabled", "()Z"),
            api_method("setAlphaFactor", "(F)V"),
            api_method("getAlphaFactor", "()F"),
            api_method("setScope", "(I)V"),
            api_method("getScope", "()I"),
            api_method("align", "(Ljavax/microedition/m3g/Node;)V"),
            api_method("setAlignment", "(Ljavax/microedition/m3g/Node;ILjavax/microedition/m3g/Node;I)V"),
            api_method("getAlignmentTarget", "(I)I"),
            api_method("getAlignmentReference", "(I)Ljavax/microedition/m3g/Node;"),
            api_method("getTransformTo", "(Ljavax/microedition/m3g/Node;Ljavax/microedition/m3g/Transform;)Z"),
        });
    }
    if (name == "javax/microedition/m3g/Group") {
        return make_class(std::string(name), kNode, kOrdinary, {
            field(kPrivate, "children", "[Ljavax/microedition/m3g/Node;"),
            field(kPrivate, "childCount", "I"),
        }, {
            api_method("<init>", "()V"),
            api_method("addChild", "(Ljavax/microedition/m3g/Node;)V"),
            api_method("removeChild", "(Ljavax/microedition/m3g/Node;)V"),
            api_method("getChild", "(I)Ljavax/microedition/m3g/Node;"),
            api_method("getChildCount", "()I"),
            api_method("pick", "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z"),
            api_method("pick", "(IFFFFLjavax/microedition/m3g/Camera;Ljavax/microedition/m3g/RayIntersection;)Z"),
            api_method("pick", "(IFFLjavax/microedition/m3g/Camera;Ljavax/microedition/m3g/RayIntersection;)Z"),
        });
    }
    if (name == "javax/microedition/m3g/World") {
        return make_class(std::string(name), "javax/microedition/m3g/Group",
                          kOrdinary, {
            field(kPrivate, "activeCamera", "Ljavax/microedition/m3g/Camera;"),
            field(kPrivate, "background", "Ljavax/microedition/m3g/Background;"),
        }, {
            api_method("<init>", "()V"),
            api_method("setActiveCamera", "(Ljavax/microedition/m3g/Camera;)V"),
            api_method("getActiveCamera", "()Ljavax/microedition/m3g/Camera;"),
            api_method("setBackground", "(Ljavax/microedition/m3g/Background;)V"),
            api_method("getBackground", "()Ljavax/microedition/m3g/Background;"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_transform_class(std::string_view name) {
    if (name == "javax/microedition/m3g/Camera") {
        return make_class(std::string(name), kNode, kOrdinary, {
            field(kPrivate, "projectionType", "I"),
            field(kPrivate, "projection", "[F"),
        }, {
            api_method("<init>", "()V"),
            api_method("setPerspective", "(FFFF)V"),
            api_method("setParallel", "(FFFF)V"),
            api_method("setGeneric", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("getProjection", "([F)I"),
            api_method("getProjection", "(Ljavax/microedition/m3g/Transform;)I"),
        });
    }
    if (name == "javax/microedition/m3g/Light") {
        return make_class(std::string(name), kNode, kOrdinary, {
            field(kPrivate, "mode", "I"),
            field(kPrivate, "intensity", "F"),
            field(kPrivate, "color", "I"),
            field(kPrivate, "spotAngle", "F"),
            field(kPrivate, "spotExponent", "F"),
            field(kPrivate, "attenuation", "[F"),
        }, {
            api_method("<init>", "()V"),
            api_method("setMode", "(I)V"),
            api_method("getMode", "()I"),
            api_method("setIntensity", "(F)V"),
            api_method("getIntensity", "()F"),
            api_method("setColor", "(I)V"),
            api_method("getColor", "()I"),
            api_method("setSpotAngle", "(F)V"),
            api_method("getSpotAngle", "()F"),
            api_method("setSpotExponent", "(F)V"),
            api_method("getSpotExponent", "()F"),
            api_method("setAttenuation", "(FFF)V"),
            api_method("getConstantAttenuation", "()F"),
            api_method("getLinearAttenuation", "()F"),
            api_method("getQuadraticAttenuation", "()F"),
        });
    }
    if (name == "javax/microedition/m3g/Transform") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "matrix", "[F"),
        }, {
            api_method("<init>", "()V"),
            api_method("<init>", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("setIdentity", "()V"),
            api_method("set", "([F)V"),
            api_method("set", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("get", "([F)V"),
            api_method("invert", "()V"),
            api_method("transpose", "()V"),
            api_method("postMultiply", "(Ljavax/microedition/m3g/Transform;)V"),
            api_method("postRotate", "(FFFF)V"),
            api_method("postRotateQuat", "(FFFF)V"),
            api_method("postScale", "(FFF)V"),
            api_method("postTranslate", "(FFF)V"),
            api_method("transform", "([F)V"),
            api_method("transform", "(Ljavax/microedition/m3g/VertexArray;[FZ)V"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_graphics_class(std::string_view name) {
    if (name == "javax/microedition/m3g/Graphics3D") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kStatic, "INSTANCE", "Ljavax/microedition/m3g/Graphics3D;"),
            field(kPrivate, "target", "Ljava/lang/Object;"),
            field(kPrivate, "camera", "Ljavax/microedition/m3g/Camera;"),
            field(kPrivate, "cameraTransform", "Ljavax/microedition/m3g/Transform;"),
            field(kPrivate, "viewportX", "I"),
            field(kPrivate, "viewportY", "I"),
            field(kPrivate, "viewportWidth", "I"),
            field(kPrivate, "viewportHeight", "I"),
            field(kPrivate, "depthNear", "F"),
            field(kPrivate, "depthFar", "F"),
            field(kPrivate, "hints", "I"),
            field(kPrivate, "depthBuffer", "Z"),
            field(kPrivate, "lights", "[Ljavax/microedition/m3g/Light;"),
            field(kPrivate, "lightTransforms", "[Ljavax/microedition/m3g/Transform;"),
            field(kPrivate, "lightCount", "I"),
        }, {
            api_method("<init>", "()V", kPrivate),
            api_method("getInstance", "()Ljavax/microedition/m3g/Graphics3D;", kPublic | kStatic),
            api_method("getProperties", "()Ljava/util/Hashtable;", kPublic | kStatic),
            api_method("bindTarget", "(Ljava/lang/Object;)V"),
            api_method("bindTarget", "(Ljava/lang/Object;ZI)V"),
            api_method("releaseTarget", "()V"),
            api_method("getTarget", "()Ljava/lang/Object;"),
            api_method("getHints", "()I"),
            api_method("isDepthBufferEnabled", "()Z"),
            api_method("clear", "(Ljavax/microedition/m3g/Background;)V"),
            api_method("render", "(Ljavax/microedition/m3g/World;)V"),
            api_method("render", "(Ljavax/microedition/m3g/Node;Ljavax/microedition/m3g/Transform;)V"),
            api_method("render", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Transform;)V"),
            api_method("render", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Transform;I)V"),
            api_method("setCamera", "(Ljavax/microedition/m3g/Camera;Ljavax/microedition/m3g/Transform;)V"),
            api_method("getCamera", "(Ljavax/microedition/m3g/Transform;)Ljavax/microedition/m3g/Camera;"),
            api_method("setViewport", "(IIII)V"),
            api_method("getViewportX", "()I"),
            api_method("getViewportY", "()I"),
            api_method("getViewportWidth", "()I"),
            api_method("getViewportHeight", "()I"),
            api_method("setDepthRange", "(FF)V"),
            api_method("getDepthRangeNear", "()F"),
            api_method("getDepthRangeFar", "()F"),
            api_method("addLight", "(Ljavax/microedition/m3g/Light;Ljavax/microedition/m3g/Transform;)I"),
            api_method("setLight", "(ILjavax/microedition/m3g/Light;Ljavax/microedition/m3g/Transform;)V"),
            api_method("getLight", "(ILjavax/microedition/m3g/Transform;)Ljavax/microedition/m3g/Light;"),
            api_method("getLightCount", "()I"),
            api_method("resetLights", "()V"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_appearance_class(std::string_view name) {
    if (name == "javax/microedition/m3g/Background") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "color", "I"),
            field(kPrivate, "image", "Ljavax/microedition/m3g/Image2D;"),
            field(kPrivate, "imageModeX", "I"),
            field(kPrivate, "imageModeY", "I"),
            field(kPrivate, "cropX", "I"),
            field(kPrivate, "cropY", "I"),
            field(kPrivate, "cropWidth", "I"),
            field(kPrivate, "cropHeight", "I"),
            field(kPrivate, "colorClear", "Z"),
            field(kPrivate, "depthClear", "Z"),
        }, {
            api_method("<init>", "()V"),
            api_method("setColor", "(I)V"), api_method("getColor", "()I"),
            api_method("setImage", "(Ljavax/microedition/m3g/Image2D;)V"),
            api_method("getImage", "()Ljavax/microedition/m3g/Image2D;"),
            api_method("setImageMode", "(II)V"),
            api_method("getImageModeX", "()I"), api_method("getImageModeY", "()I"),
            api_method("setCrop", "(IIII)V"),
            api_method("getCropX", "()I"), api_method("getCropY", "()I"),
            api_method("getCropWidth", "()I"), api_method("getCropHeight", "()I"),
            api_method("setColorClearEnable", "(Z)V"),
            api_method("isColorClearEnabled", "()Z"),
            api_method("setDepthClearEnable", "(Z)V"),
            api_method("isDepthClearEnabled", "()Z"),
        });
    }
    if (name == "javax/microedition/m3g/Appearance") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "layer", "I"),
            field(kPrivate, "compositingMode", "Ljavax/microedition/m3g/CompositingMode;"),
            field(kPrivate, "fog", "Ljavax/microedition/m3g/Fog;"),
            field(kPrivate, "polygonMode", "Ljavax/microedition/m3g/PolygonMode;"),
            field(kPrivate, "material", "Ljavax/microedition/m3g/Material;"),
            field(kPrivate, "textures", "[Ljavax/microedition/m3g/Texture2D;"),
        }, {
            api_method("<init>", "()V"),
            api_method("setLayer", "(I)V"), api_method("getLayer", "()I"),
            api_method("setCompositingMode", "(Ljavax/microedition/m3g/CompositingMode;)V"),
            api_method("getCompositingMode", "()Ljavax/microedition/m3g/CompositingMode;"),
            api_method("setFog", "(Ljavax/microedition/m3g/Fog;)V"),
            api_method("getFog", "()Ljavax/microedition/m3g/Fog;"),
            api_method("setPolygonMode", "(Ljavax/microedition/m3g/PolygonMode;)V"),
            api_method("getPolygonMode", "()Ljavax/microedition/m3g/PolygonMode;"),
            api_method("setMaterial", "(Ljavax/microedition/m3g/Material;)V"),
            api_method("getMaterial", "()Ljavax/microedition/m3g/Material;"),
            api_method("setTexture", "(ILjavax/microedition/m3g/Texture2D;)V"),
            api_method("getTexture", "(I)Ljavax/microedition/m3g/Texture2D;"),
        });
    }
    if (name == "javax/microedition/m3g/CompositingMode") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "blending", "I"),
            field(kPrivate, "alphaThreshold", "F"),
            field(kPrivate, "alphaWrite", "Z"),
            field(kPrivate, "colorWrite", "Z"),
            field(kPrivate, "depthWrite", "Z"),
            field(kPrivate, "depthTest", "Z"),
            field(kPrivate, "depthOffsetFactor", "F"),
            field(kPrivate, "depthOffsetUnits", "F"),
        }, {
            api_method("<init>", "()V"),
            api_method("setBlending", "(I)V"), api_method("getBlending", "()I"),
            api_method("setAlphaThreshold", "(F)V"), api_method("getAlphaThreshold", "()F"),
            api_method("setAlphaWriteEnable", "(Z)V"), api_method("isAlphaWriteEnabled", "()Z"),
            api_method("setColorWriteEnable", "(Z)V"), api_method("isColorWriteEnabled", "()Z"),
            api_method("setDepthWriteEnable", "(Z)V"), api_method("isDepthWriteEnabled", "()Z"),
            api_method("setDepthTestEnable", "(Z)V"), api_method("isDepthTestEnabled", "()Z"),
            api_method("setDepthOffset", "(FF)V"),
            api_method("getDepthOffsetFactor", "()F"), api_method("getDepthOffsetUnits", "()F"),
        });
    }
    if (name == "javax/microedition/m3g/PolygonMode") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "culling", "I"), field(kPrivate, "shading", "I"),
            field(kPrivate, "winding", "I"), field(kPrivate, "twoSided", "Z"),
            field(kPrivate, "localCameraLighting", "Z"),
            field(kPrivate, "perspectiveCorrection", "Z"),
        }, {
            api_method("<init>", "()V"),
            api_method("setCulling", "(I)V"), api_method("getCulling", "()I"),
            api_method("setShading", "(I)V"), api_method("getShading", "()I"),
            api_method("setWinding", "(I)V"), api_method("getWinding", "()I"),
            api_method("setTwoSidedLightingEnable", "(Z)V"), api_method("isTwoSidedLightingEnabled", "()Z"),
            api_method("setLocalCameraLightingEnable", "(Z)V"), api_method("isLocalCameraLightingEnabled", "()Z"),
            api_method("setPerspectiveCorrectionEnable", "(Z)V"), api_method("isPerspectiveCorrectionEnabled", "()Z"),
        });
    }
    if (name == "javax/microedition/m3g/Image2D") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "format", "I"), field(kPrivate, "width", "I"),
            field(kPrivate, "height", "I"), field(kPrivate, "mutable", "Z"),
            field(kPrivate, "source", "Ljava/lang/Object;"),
            field(kPrivate, "palette", "[B"),
        }, {
            api_method("<init>", "(ILjava/lang/Object;)V"),
            api_method("<init>", "(III)V"),
            api_method("<init>", "(III[B)V"),
            api_method("<init>", "(III[B[B)V"),
            api_method("getFormat", "()I"), api_method("getWidth", "()I"),
            api_method("getHeight", "()I"), api_method("isMutable", "()Z"),
            api_method("set", "(IIII[B)V"),
        });
    }
    if (name == "javax/microedition/m3g/Texture2D") {
        return make_class(std::string(name), kTransformable, kOrdinary, {
            field(kPrivate, "image", "Ljavax/microedition/m3g/Image2D;"),
            field(kPrivate, "blendColor", "I"), field(kPrivate, "blending", "I"),
            field(kPrivate, "levelFilter", "I"), field(kPrivate, "imageFilter", "I"),
            field(kPrivate, "wrapS", "I"), field(kPrivate, "wrapT", "I"),
        }, {
            api_method("<init>", "(Ljavax/microedition/m3g/Image2D;)V"),
            api_method("setImage", "(Ljavax/microedition/m3g/Image2D;)V"),
            api_method("getImage", "()Ljavax/microedition/m3g/Image2D;"),
            api_method("setBlendColor", "(I)V"), api_method("getBlendColor", "()I"),
            api_method("setBlending", "(I)V"), api_method("getBlending", "()I"),
            api_method("setFiltering", "(II)V"),
            api_method("getLevelFilter", "()I"), api_method("getImageFilter", "()I"),
            api_method("setWrapping", "(II)V"),
            api_method("getWrappingS", "()I"), api_method("getWrappingT", "()I"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_geometry_class(std::string_view name) {
    if (name == "javax/microedition/m3g/VertexArray") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "vertexCount", "I"), field(kPrivate, "componentCount", "I"),
            field(kPrivate, "componentSize", "I"), field(kPrivate, "data", "Ljava/lang/Object;"),
        }, {
            api_method("<init>", "(III)V"),
            api_method("getVertexCount", "()I"), api_method("getComponentCount", "()I"),
            api_method("getComponentType", "()I"),
            api_method("set", "(II[B)V"), api_method("set", "(II[S)V"),
            api_method("get", "(II[B)V"), api_method("get", "(II[S)V"),
        });
    }
    if (name == "javax/microedition/m3g/VertexBuffer") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "vertexCount", "I"), field(kPrivate, "defaultColor", "I"),
            field(kPrivate, "positions", "Ljavax/microedition/m3g/VertexArray;"),
            field(kPrivate, "positionScale", "F"), field(kPrivate, "positionBias", "[F"),
            field(kPrivate, "normals", "Ljavax/microedition/m3g/VertexArray;"),
            field(kPrivate, "colors", "Ljavax/microedition/m3g/VertexArray;"),
            field(kPrivate, "texCoords", "[Ljavax/microedition/m3g/VertexArray;"),
            field(kPrivate, "texScales", "[F"), field(kPrivate, "texBiases", "[[F"),
        }, {
            api_method("<init>", "()V"), api_method("getVertexCount", "()I"),
            api_method("setPositions", "(Ljavax/microedition/m3g/VertexArray;F[F)V"),
            api_method("getPositions", "([F)Ljavax/microedition/m3g/VertexArray;"),
            api_method("setNormals", "(Ljavax/microedition/m3g/VertexArray;)V"),
            api_method("getNormals", "()Ljavax/microedition/m3g/VertexArray;"),
            api_method("setColors", "(Ljavax/microedition/m3g/VertexArray;)V"),
            api_method("getColors", "()Ljavax/microedition/m3g/VertexArray;"),
            api_method("setTexCoords", "(ILjavax/microedition/m3g/VertexArray;F[F)V"),
            api_method("getTexCoords", "(I[F)Ljavax/microedition/m3g/VertexArray;"),
            api_method("setDefaultColor", "(I)V"), api_method("getDefaultColor", "()I"),
        });
    }
    if (name == "javax/microedition/m3g/IndexBuffer") {
        return make_class(std::string(name), kObject3D, kOrdinary | kAbstract, {
            field(kPrivate, "indices", "[I"),
            field(kPrivate, "stripLengths", "[I"),
        }, {
            api_method("<init>", "()V", kProtected),
            api_method("getIndexCount", "()I"), api_method("getIndices", "([I)V"),
        });
    }
    if (name == "javax/microedition/m3g/TriangleStripArray") {
        return make_class(std::string(name), "javax/microedition/m3g/IndexBuffer", kOrdinary, {}, {
            api_method("<init>", "(I[I)V"), api_method("<init>", "([I[I)V"),
        });
    }
    if (name == "javax/microedition/m3g/Mesh") {
        return make_class(std::string(name), kNode, kOrdinary, {
            field(kPrivate, "vertexBuffer", "Ljavax/microedition/m3g/VertexBuffer;"),
            field(kPrivate, "indexBuffers", "[Ljavax/microedition/m3g/IndexBuffer;"),
            field(kPrivate, "appearances", "[Ljavax/microedition/m3g/Appearance;"),
        }, {
            api_method("<init>", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;)V"),
            api_method("<init>", "(Ljavax/microedition/m3g/VertexBuffer;[Ljavax/microedition/m3g/IndexBuffer;[Ljavax/microedition/m3g/Appearance;)V"),
            api_method("getVertexBuffer", "()Ljavax/microedition/m3g/VertexBuffer;"),
            api_method("getIndexBuffer", "(I)Ljavax/microedition/m3g/IndexBuffer;"),
            api_method("getAppearance", "(I)Ljavax/microedition/m3g/Appearance;"),
            api_method("setAppearance", "(ILjavax/microedition/m3g/Appearance;)V"),
            api_method("getSubmeshCount", "()I"),
        });
    }
    if (name == "javax/microedition/m3g/Sprite3D") {
        return make_class(std::string(name), kNode, kOrdinary, {
            field(kPrivate, "scaled", "Z"), field(kPrivate, "image", "Ljavax/microedition/m3g/Image2D;"),
            field(kPrivate, "appearance", "Ljavax/microedition/m3g/Appearance;"),
            field(kPrivate, "cropX", "I"), field(kPrivate, "cropY", "I"),
            field(kPrivate, "cropWidth", "I"), field(kPrivate, "cropHeight", "I"),
            field(kPrivate, "flipX", "Z"), field(kPrivate, "flipY", "Z"),
        }, {
            api_method("<init>", "(ZLjavax/microedition/m3g/Image2D;Ljavax/microedition/m3g/Appearance;)V"),
            api_method("isScaled", "()Z"), api_method("setImage", "(Ljavax/microedition/m3g/Image2D;)V"),
            api_method("getImage", "()Ljavax/microedition/m3g/Image2D;"),
            api_method("setAppearance", "(Ljavax/microedition/m3g/Appearance;)V"),
            api_method("getAppearance", "()Ljavax/microedition/m3g/Appearance;"),
            api_method("setCrop", "(IIII)V"), api_method("getCropX", "()I"),
            api_method("getCropY", "()I"), api_method("getCropWidth", "()I"),
            api_method("getCropHeight", "()I"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_material_class(std::string_view name) {
    if (name == "javax/microedition/m3g/Material") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "ambient", "I"), field(kPrivate, "diffuse", "I"),
            field(kPrivate, "emissive", "I"), field(kPrivate, "specular", "I"),
            field(kPrivate, "shininess", "F"), field(kPrivate, "vertexColorTracking", "Z"),
        }, {
            api_method("<init>", "()V"), api_method("setColor", "(II)V"),
            api_method("getColor", "(I)I"), api_method("setShininess", "(F)V"),
            api_method("getShininess", "()F"), api_method("setVertexColorTrackingEnable", "(Z)V"),
            api_method("isVertexColorTrackingEnabled", "()Z"),
        });
    }
    if (name == "javax/microedition/m3g/Fog") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "mode", "I"), field(kPrivate, "color", "I"),
            field(kPrivate, "density", "F"), field(kPrivate, "nearDistance", "F"),
            field(kPrivate, "farDistance", "F"),
        }, {
            api_method("<init>", "()V"), api_method("setMode", "(I)V"),
            api_method("getMode", "()I"), api_method("setColor", "(I)V"),
            api_method("getColor", "()I"), api_method("setDensity", "(F)V"),
            api_method("getDensity", "()F"), api_method("setLinear", "(FF)V"),
            api_method("getNearDistance", "()F"), api_method("getFarDistance", "()F"),
        });
    }
    if (name == "javax/microedition/m3g/RayIntersection") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "intersected", "Ljavax/microedition/m3g/Node;"),
            field(kPrivate, "distance", "F"),
            field(kPrivate, "submeshIndex", "I"),
            field(kPrivate, "textureS", "[F"),
            field(kPrivate, "textureT", "[F"),
            field(kPrivate, "normal", "[F"),
            field(kPrivate, "ray", "[F"),
        }, {
            api_method("<init>", "()V"),
            api_method("getIntersected", "()Ljavax/microedition/m3g/Node;"),
            api_method("getDistance", "()F"), api_method("getSubmeshIndex", "()I"),
            api_method("getTextureS", "(I)F"), api_method("getTextureT", "(I)F"),
            api_method("getNormalX", "()F"), api_method("getNormalY", "()F"),
            api_method("getNormalZ", "()F"), api_method("getRay", "([F)V"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_animation_class(std::string_view name) {
    if (name == "javax/microedition/m3g/Loader") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary | kFinal, {}, {
            api_method("load", "(Ljava/lang/String;)[Ljavax/microedition/m3g/Object3D;", kPublic | kStatic),
            api_method("load", "([BI)[Ljavax/microedition/m3g/Object3D;", kPublic | kStatic),
        });
    }
    if (name == "javax/microedition/m3g/AnimationController") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "activeStart", "I"),
            field(kPrivate, "activeEnd", "I"),
            field(kPrivate, "speed", "F"),
            field(kPrivate, "weight", "F"),
            field(kPrivate, "refSequenceTime", "F"),
            field(kPrivate, "refWorldTime", "I"),
        }, {
            api_method("<init>", "()V"), api_method("setActiveInterval", "(II)V"),
            api_method("getActiveIntervalStart", "()I"), api_method("getActiveIntervalEnd", "()I"),
            api_method("setSpeed", "(FI)V"), api_method("getSpeed", "()F"),
            api_method("getRefWorldTime", "()I"), api_method("setPosition", "(FI)V"),
            api_method("getPosition", "(I)F"), api_method("setWeight", "(F)V"),
            api_method("getWeight", "()F"),
        });
    }
    if (name == "javax/microedition/m3g/AnimationTrack") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "sequence", "Ljavax/microedition/m3g/KeyframeSequence;"),
            field(kPrivate, "controller", "Ljavax/microedition/m3g/AnimationController;"),
            field(kPrivate, "property", "I"),
        }, {
            api_method("<init>", "(Ljavax/microedition/m3g/KeyframeSequence;I)V"),
            api_method("getKeyframeSequence", "()Ljavax/microedition/m3g/KeyframeSequence;"),
            api_method("setController", "(Ljavax/microedition/m3g/AnimationController;)V"),
            api_method("getController", "()Ljavax/microedition/m3g/AnimationController;"),
            api_method("getTargetProperty", "()I"),
        });
    }
    if (name == "javax/microedition/m3g/KeyframeSequence") {
        return make_class(std::string(name), kObject3D, kOrdinary, {
            field(kPrivate, "keyframeCount", "I"),
            field(kPrivate, "componentCount", "I"),
            field(kPrivate, "interpolationType", "I"),
            field(kPrivate, "validFirst", "I"),
            field(kPrivate, "validLast", "I"),
            field(kPrivate, "duration", "I"),
            field(kPrivate, "repeatMode", "I"),
            field(kPrivate, "times", "[I"),
            field(kPrivate, "values", "[F"),
        }, {
            api_method("<init>", "(III)V"), api_method("setKeyframe", "(II[F)V"),
            api_method("getKeyframe", "(I[F)I"), api_method("getKeyframeCount", "()I"),
            api_method("getComponentCount", "()I"), api_method("getInterpolationType", "()I"),
            api_method("setValidRange", "(II)V"), api_method("getValidRangeFirst", "()I"),
            api_method("getValidRangeLast", "()I"), api_method("setDuration", "(I)V"),
            api_method("getDuration", "()I"), api_method("setRepeatMode", "(I)V"),
            api_method("getRepeatMode", "()I"),
        });
    }
    if (name == "javax/microedition/m3g/SkinnedMesh") {
        return make_class(std::string(name), "javax/microedition/m3g/Mesh", kOrdinary, {
            field(kPrivate, "skeleton", "Ljavax/microedition/m3g/Group;"),
            field(kPrivate, "bones", "[Ljavax/microedition/m3g/Node;"),
            field(kPrivate, "boneFirstVertices", "[I"),
            field(kPrivate, "boneVertexCounts", "[I"),
            field(kPrivate, "boneWeights", "[I"),
            field(kPrivate, "boneTransforms", "[[F"),
        }, {
            api_method("<init>", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Group;)V"),
            api_method("<init>", "(Ljavax/microedition/m3g/VertexBuffer;[Ljavax/microedition/m3g/IndexBuffer;[Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Group;)V"),
            api_method("addTransform", "(Ljavax/microedition/m3g/Node;III)V"),
            api_method("getBoneTransform", "(Ljavax/microedition/m3g/Node;Ljavax/microedition/m3g/Transform;)V"),
            api_method("getBoneVertices", "(Ljavax/microedition/m3g/Node;[I[F)I"),
            api_method("getSkeleton", "()Ljavax/microedition/m3g/Group;"),
        });
    }
    if (name == "javax/microedition/m3g/MorphingMesh") {
        return make_class(std::string(name), "javax/microedition/m3g/Mesh", kOrdinary, {
            field(kPrivate, "morphTargets", "[Ljavax/microedition/m3g/VertexBuffer;"),
            field(kPrivate, "weights", "[F"),
        }, {
            api_method("<init>", "(Ljavax/microedition/m3g/VertexBuffer;[Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;)V"),
            api_method("<init>", "(Ljavax/microedition/m3g/VertexBuffer;[Ljavax/microedition/m3g/VertexBuffer;[Ljavax/microedition/m3g/IndexBuffer;[Ljavax/microedition/m3g/Appearance;)V"),
            api_method("setWeights", "([F)V"), api_method("getWeights", "([F)V"),
            api_method("getMorphTarget", "(I)Ljavax/microedition/m3g/VertexBuffer;"),
            api_method("getMorphTargetCount", "()I"),
        });
    }
    return {};
}

[[nodiscard]] ClassPtr m3g_class(std::string_view name) {
    if (auto value = m3g_base_class(name)) return value;
    if (auto value = m3g_transform_class(name)) return value;
    if (auto value = m3g_graphics_class(name)) return value;
    if (auto value = m3g_appearance_class(name)) return value;
    if (auto value = m3g_geometry_class(name)) return value;
    if (auto value = m3g_material_class(name)) return value;
    return m3g_animation_class(name);
}

} // namespace

void register_m3g_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(&m3g_class);
}

} // namespace phoneme::vm
