#include "Micro3dNativeModules.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Micro3dNativeSupport.hpp"
#include "Micro3dSoftware.hpp"
#include "PhoneMEMetal3D.h"
#include "phoneme/graphics/Graphics.hpp"

namespace {
std::atomic<PhoneMEMetal3DRasterizer> g_phoneME_metal3d_rasterizer {nullptr};
}

extern "C" void phoneme_metal3d_set_rasterizer(
    PhoneMEMetal3DRasterizer rasterizer) {
    g_phoneME_metal3d_rasterizer.store(rasterizer,
                                       std::memory_order_release);
}

extern "C" PhoneMEMetal3DRasterizer phoneme_metal3d_get_rasterizer(void) {
    return g_phoneME_metal3d_rasterizer.load(std::memory_order_acquire);
}

namespace phoneme::vm {
namespace micro3d {
namespace {

using software::Mat34;
using software::TexCoord;
using software::Texture;
using software::Vec3;

constexpr u32 kCommandAffineIndex = 0x87000000U;
constexpr u32 kCommandAmbientLight = 0xA0000000U;
constexpr u32 kCommandAttribute = 0x83000000U;
constexpr u32 kCommandCenter = 0x85000000U;
constexpr u32 kCommandClip = 0x84000000U;
constexpr u32 kCommandDirectionLight = 0xA1000000U;
constexpr u32 kCommandEnd = 0x80000000U;
constexpr u32 kCommandFlush = 0x82000000U;
constexpr u32 kCommandListVersion = 0xFE000001U;
constexpr u32 kCommandNop = 0x81000000U;
constexpr u32 kCommandParallelScale = 0x90000000U;
constexpr u32 kCommandParallelSize = 0x91000000U;
constexpr u32 kCommandPerspectiveFov = 0x92000000U;
constexpr u32 kCommandPerspectiveWh = 0x93000000U;
constexpr u32 kCommandTextureIndex = 0x86000000U;
constexpr u32 kCommandThreshold = 0xAF000000U;

constexpr u32 kPrimitivePoints = 0x01000000U;
constexpr u32 kPrimitiveLines = 0x02000000U;
constexpr u32 kPrimitiveTriangles = 0x03000000U;
constexpr u32 kPrimitiveQuads = 0x04000000U;
constexpr u32 kPrimitivePointSprites = 0x05000000U;
constexpr u32 kPrimitiveMask = 0x07000000U;
constexpr u32 kNormalMask = 0x00000300U;
constexpr u32 kNormalPerFace = 0x00000200U;
constexpr u32 kNormalPerVertex = 0x00000300U;
constexpr u32 kColorMask = 0x00000C00U;
constexpr u32 kColorPerCommand = 0x00000400U;
constexpr u32 kColorPerFace = 0x00000800U;
constexpr u32 kColorPerVertex = 0x00000C00U;
constexpr u32 kTexcoordMask = 0x00003000U;
constexpr u32 kTexcoordPerVertex = 0x00003000U;
constexpr u32 kPointParamsPerCommand = 0x00001000U;
constexpr u32 kAttributeLighting = 1U;
constexpr u32 kAttributeSphereMap = 2U;
constexpr u32 kAttributeToon = 4U;
constexpr u32 kAttributeSemiTransparent = 8U;
constexpr u32 kPrimitiveLighting = 1U;
constexpr u32 kPrimitiveSphereMap = 2U;
constexpr u32 kPrimitiveColorKey = 16U;
constexpr u32 kPrimitiveBlendMask = 96U;
constexpr float kFixedScale = 1.0F / 4096.0F;

struct RenderKey final {
    const Machine* machine {nullptr};
    u64 graphics3d {0U};
    friend bool operator==(const RenderKey&, const RenderKey&) noexcept = default;
};

struct RenderKeyHash final {
    [[nodiscard]] usize operator()(const RenderKey& key) const noexcept {
        const auto pointer = reinterpret_cast<std::uintptr_t>(key.machine);
        return std::hash<std::uintptr_t> {}(pointer) ^
            (std::hash<u64> {}(key.graphics3d) << 1U);
    }
};

struct Projection final {
    u32 type {kCommandParallelScale};
    i32 scale_x {512};
    i32 scale_y {512};
    i32 parallel_width {0};
    i32 parallel_height {0};
    i32 near_value {1};
    i32 far_value {32767};
    i32 angle {512};
    i32 perspective_width {0};
    i32 perspective_height {0};
    float center_x {0.0F};
    float center_y {0.0F};
};

struct EffectState final {
    bool lighting {false};
    bool sphere_map {false};
    bool toon {false};
    bool semi_transparent {true};
    float ambient {0.0F};
    float directional {1.0F};
    Vec3 light_direction {0.0F, 0.0F, -1.0F};
    float toon_threshold {0.5F};
    float toon_high {1.0F};
    float toon_low {0.0F};
    std::shared_ptr<const Texture> sphere_texture;
};

struct BatchState final {
    Mat34 view {};
    Projection projection {};
    EffectState effect {};
    graphics::Rect clip {};
    std::vector<std::shared_ptr<const Texture>> textures;
    i32 texture_index {0};
};

struct Vertex final {
    Vec3 position {};
    Vec3 normal {0.0F, 0.0F, 1.0F};
    graphics::Pixel color {0xFFFFFFFFU};
    TexCoord texcoord {};
};

struct Triangle final {
    std::array<Vertex, 3> vertices {};
    std::shared_ptr<const Texture> texture;
    u8 blend {0U};
    bool double_sided {false};
    bool lighting {false};
    bool sphere_map {false};
    bool color_key {false};
};

struct Line final {
    std::array<Vertex, 2> vertices {};
    u8 blend {0U};
};

struct Point final {
    Vertex vertex {};
    u8 blend {0U};
};

struct Batch final {
    BatchState state {};
    std::vector<Triangle> triangles;
    std::vector<Line> lines;
    std::vector<Point> points;
};

struct Queue final {
    std::vector<Batch> batches;
    std::vector<float> depth;
    i32 depth_width {0};
    i32 depth_height {0};
};

struct Target final {
    graphics::Image* image {nullptr};
    graphics::GraphicsContext context {};
    graphics::Rect clip {};
};

struct ScreenVertex final {
    float x {0.0F};
    float y {0.0F};
    float depth {1.0F};
    float inverse_w {1.0F};
    Vec3 normal_over_w {};
    std::array<float, 4> color_over_w {};
    float u_over_w {0.0F};
    float v_over_w {0.0F};
    bool visible {false};
};

std::mutex g_render_mutex;
std::unordered_map<RenderKey, Queue, RenderKeyHash> g_queues;

[[nodiscard]] RenderKey render_key(Machine& machine,
                                   ObjectRef graphics3d) noexcept {
    return RenderKey {&machine, graphics3d.bits};
}

[[nodiscard]] float clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] Vec3 normalize(Vec3 value,
                             Vec3 fallback = {0.0F, 0.0F, 1.0F}) noexcept {
    const float length = std::sqrt(value.x * value.x + value.y * value.y +
                                   value.z * value.z);
    if (!std::isfinite(length) || length <= 1.0e-8F) return fallback;
    const float inverse = 1.0F / length;
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

[[nodiscard]] Vec3 transform_point(const Mat34& matrix,
                                   Vec3 point) noexcept {
    const auto& m = matrix.values;
    return {
        point.x * m[0] + point.y * m[1] + point.z * m[2] + m[3],
        point.x * m[4] + point.y * m[5] + point.z * m[6] + m[7],
        point.x * m[8] + point.y * m[9] + point.z * m[10] + m[11],
    };
}

[[nodiscard]] Vec3 transform_normal(const Mat34& matrix,
                                    Vec3 normal) noexcept {
    const auto& m = matrix.values;
    return normalize({
        normal.x * m[0] + normal.y * m[1] + normal.z * m[2],
        normal.x * m[4] + normal.y * m[5] + normal.z * m[6],
        normal.x * m[8] + normal.y * m[9] + normal.z * m[10],
    });
}

[[nodiscard]] Mat34 affine_matrix(const AffineValue& affine) noexcept {
    Mat34 result;
    for (usize index = 0U; index < 12U; ++index) {
        const bool translation = index == 3U || index == 7U || index == 11U;
        result.values[index] = static_cast<float>(affine.m[index]) *
            (translation ? 1.0F : kFixedScale);
    }
    return result;
}

[[nodiscard]] Status require_bound(Machine& machine,
                                   ObjectRef self,
                                   std::string_view operation) {
    auto bound = int_field(machine, self, kGraphics3D, "bound", "Z");
    if (!bound) return std::unexpected(bound.error());
    if (*bound == 0) {
        return fail_java("java/lang/IllegalStateException",
                         std::string(operation) + ": no target is bound");
    }
    return {};
}

[[nodiscard]] Result<Target> bound_target(Machine& machine,
                                          ObjectRef graphics3d) {
    auto graphics = reference_field(
        machine, graphics3d, kGraphics3D, "graphics",
        "Ljavax/microedition/lcdui/Graphics;");
    if (!graphics) return std::unexpected(graphics.error());
    if (graphics->is_null()) {
        return fail_java("java/lang/IllegalStateException",
                         "Micro3D target is not bound");
    }
    auto context = machine.graphics().context(graphics->bits);
    if (!context) return std::unexpected(context.error());
    auto image = machine.graphics().image((*context)->target_key);
    if (!image) return std::unexpected(image.error());
    graphics::Rect bounds = graphics::target_bounds(**image);
    graphics::Rect clip = graphics::intersect((*context)->clip, bounds);
    return Target {.image = *image, .context = **context, .clip = clip};
}

[[nodiscard]] Result<std::shared_ptr<const Texture>> optional_texture(
    Machine& machine, ObjectRef texture) {
    if (texture.is_null()) return std::shared_ptr<const Texture> {};
    auto live = require_not_disposed(machine, texture, kTexture, "Texture");
    if (!live) return std::unexpected(live.error());
    return software::cached_texture(machine, texture);
}

[[nodiscard]] Result<std::vector<std::shared_ptr<const Texture>>>
texture_array(Machine& machine, ObjectRef array) {
    std::vector<std::shared_ptr<const Texture>> result;
    if (array.is_null()) return result;
    auto length = machine.heap().array_length(array);
    if (!length) return std::unexpected(length.error());
    const usize count = std::min(*length, usize {16U});
    result.reserve(count);
    for (usize index = 0U; index < count; ++index) {
        auto value = machine.heap().element(array, index);
        if (!value) return std::unexpected(value.error());
        auto reference = value->as_reference();
        if (!reference) return std::unexpected(reference.error());
        auto texture = optional_texture(machine, *reference);
        if (!texture) return std::unexpected(texture.error());
        result.push_back(std::move(*texture));
    }
    return result;
}

[[nodiscard]] Result<BatchState> read_state(Machine& machine,
                                            ObjectRef graphics3d,
                                            ObjectRef layout,
                                            ObjectRef effect,
                                            i32 x,
                                            i32 y,
                                            ObjectRef textures,
                                            bool single_texture) {
    BatchState state;
    auto target = bound_target(machine, graphics3d);
    if (!target) return std::unexpected(target.error());
    state.clip = target->clip;

    auto affine = reference_field(
        machine, layout, kFigureLayout, "affine",
        "Lcom/mascotcapsule/micro3d/v3/AffineTrans;");
    if (!affine) return std::unexpected(affine.error());
    auto affine_value = read_affine(machine, *affine, "FigureLayout");
    if (!affine_value) return std::unexpected(affine_value.error());
    state.view = affine_matrix(*affine_value);

    auto center_x = int_field(machine, layout, kFigureLayout, "centerX");
    auto center_y = int_field(machine, layout, kFigureLayout, "centerY");
    auto projection = int_field(machine, layout, kFigureLayout, "projection");
    auto scale_x = int_field(machine, layout, kFigureLayout, "scaleX");
    auto scale_y = int_field(machine, layout, kFigureLayout, "scaleY");
    auto parallel_width = int_field(machine, layout, kFigureLayout,
                                    "parallelWidth");
    auto parallel_height = int_field(machine, layout, kFigureLayout,
                                     "parallelHeight");
    auto near_value = int_field(machine, layout, kFigureLayout, "near");
    auto far_value = int_field(machine, layout, kFigureLayout, "far");
    auto angle = int_field(machine, layout, kFigureLayout, "angle");
    auto perspective_width = int_field(machine, layout, kFigureLayout,
                                       "perspectiveWidth");
    auto perspective_height = int_field(machine, layout, kFigureLayout,
                                        "perspectiveHeight");
    if (!center_x) return std::unexpected(center_x.error());
    if (!center_y) return std::unexpected(center_y.error());
    if (!projection) return std::unexpected(projection.error());
    if (!scale_x) return std::unexpected(scale_x.error());
    if (!scale_y) return std::unexpected(scale_y.error());
    if (!parallel_width) return std::unexpected(parallel_width.error());
    if (!parallel_height) return std::unexpected(parallel_height.error());
    if (!near_value) return std::unexpected(near_value.error());
    if (!far_value) return std::unexpected(far_value.error());
    if (!angle) return std::unexpected(angle.error());
    if (!perspective_width) return std::unexpected(perspective_width.error());
    if (!perspective_height) return std::unexpected(perspective_height.error());
    state.projection = {
        .type = std::bit_cast<u32>(*projection),
        .scale_x = *scale_x,
        .scale_y = *scale_y,
        .parallel_width = *parallel_width,
        .parallel_height = *parallel_height,
        .near_value = *near_value,
        .far_value = *far_value,
        .angle = *angle,
        .perspective_width = *perspective_width,
        .perspective_height = *perspective_height,
        .center_x = static_cast<float>(*center_x + x +
                                       target->context.translate_x),
        .center_y = static_cast<float>(*center_y + y +
                                       target->context.translate_y),
    };

    auto light = reference_field(machine, effect, kEffect3D, "light",
                                 "Lcom/mascotcapsule/micro3d/v3/Light;");
    auto sphere = reference_field(machine, effect, kEffect3D, "texture",
                                  "Lcom/mascotcapsule/micro3d/v3/Texture;");
    auto shading = int_field(machine, effect, kEffect3D, "shading");
    auto transparent = int_field(machine, effect, kEffect3D,
                                 "isTransparency", "Z");
    auto threshold = int_field(machine, effect, kEffect3D, "toonThreshold");
    auto high = int_field(machine, effect, kEffect3D, "toonHigh");
    auto low = int_field(machine, effect, kEffect3D, "toonLow");
    if (!light) return std::unexpected(light.error());
    if (!sphere) return std::unexpected(sphere.error());
    if (!shading) return std::unexpected(shading.error());
    if (!transparent) return std::unexpected(transparent.error());
    if (!threshold) return std::unexpected(threshold.error());
    if (!high) return std::unexpected(high.error());
    if (!low) return std::unexpected(low.error());
    state.effect.semi_transparent = *transparent != 0;
    state.effect.toon = *shading == 1;
    state.effect.toon_threshold = clamp01(static_cast<float>(*threshold) / 255.0F);
    state.effect.toon_high = clamp01(static_cast<float>(*high) / 255.0F);
    state.effect.toon_low = clamp01(static_cast<float>(*low) / 255.0F);
    if (!light->is_null()) {
        auto ambient = int_field(machine, *light, kLight, "ambIntensity");
        auto directional = int_field(machine, *light, kLight, "dirIntensity");
        auto direction = reference_field(
            machine, *light, kLight, "direction",
            "Lcom/mascotcapsule/micro3d/v3/Vector3D;");
        if (!ambient) return std::unexpected(ambient.error());
        if (!directional) return std::unexpected(directional.error());
        if (!direction) return std::unexpected(direction.error());
        auto vector = read_vector(machine, *direction, "Light direction");
        if (!vector) return std::unexpected(vector.error());
        state.effect.lighting = true;
        state.effect.ambient = clamp01(static_cast<float>(*ambient) / 4096.0F);
        state.effect.directional = std::clamp(
            static_cast<float>(*directional) / 4096.0F, 0.0F, 4.0F);
        state.effect.light_direction = normalize({
            -static_cast<float>(vector->x),
            -static_cast<float>(vector->y),
            -static_cast<float>(vector->z),
        });
    }
    if (!sphere->is_null()) {
        auto decoded = optional_texture(machine, *sphere);
        if (!decoded) return std::unexpected(decoded.error());
        state.effect.sphere_map = true;
        state.effect.sphere_texture = std::move(*decoded);
    }

    if (!textures.is_null()) {
        if (single_texture) {
            auto decoded = optional_texture(machine, textures);
            if (!decoded) return std::unexpected(decoded.error());
            if (*decoded) state.textures.push_back(std::move(*decoded));
        } else {
            auto decoded = texture_array(machine, textures);
            if (!decoded) return std::unexpected(decoded.error());
            state.textures = std::move(*decoded);
        }
    }
    return state;
}

[[nodiscard]] graphics::Pixel color_from_int(i32 value) noexcept {
    return graphics::argb(255U,
        static_cast<u8>((static_cast<u32>(value) >> 16U) & 0xFFU),
        static_cast<u8>((static_cast<u32>(value) >> 8U) & 0xFFU),
        static_cast<u8>(static_cast<u32>(value) & 0xFFU));
}

[[nodiscard]] Result<Batch> figure_batch(Machine& machine,
                                         ObjectRef graphics3d,
                                         ObjectRef figure,
                                         ObjectRef layout,
                                         ObjectRef effect,
                                         i32 x,
                                         i32 y,
                                         bool selected_texture_only) {
    auto model = software::cached_model(machine, figure);
    if (!model) return std::unexpected(model.error());
    auto posture_table = reference_field(
        machine, figure, kFigure, "postureTable",
        "Lcom/mascotcapsule/micro3d/v3/ActionTable;");
    auto posture_action = int_field(machine, figure, kFigure, "postureAction");
    auto posture_frame = int_field(machine, figure, kFigure, "postureFrame");
    auto pattern = int_field(machine, figure, kFigure, "pattern");
    auto textures = reference_field(
        machine, figure, kFigure, "textures",
        "[Lcom/mascotcapsule/micro3d/v3/Texture;");
    auto selected = int_field(machine, figure, kFigure, "textureIndex");
    if (!posture_table) return std::unexpected(posture_table.error());
    if (!posture_action) return std::unexpected(posture_action.error());
    if (!posture_frame) return std::unexpected(posture_frame.error());
    if (!pattern) return std::unexpected(pattern.error());
    if (!textures) return std::unexpected(textures.error());
    if (!selected) return std::unexpected(selected.error());

    const software::ActionTable* action_table = nullptr;
    std::shared_ptr<const software::ActionTable> action_owner;
    if (!posture_table->is_null()) {
        auto actions = software::cached_actions(machine, *posture_table);
        if (!actions) return std::unexpected(actions.error());
        action_owner = std::move(*actions);
        action_table = action_owner.get();
    }
    auto pose = software::pose_model(**model, action_table,
                                     *posture_action, *posture_frame);
    if (!pose) return std::unexpected(pose.error());

    ObjectRef state_textures = *textures;
    bool single = false;
    ObjectRef selected_texture {};
    if (selected_texture_only && !textures->is_null() && *selected >= 0) {
        auto length = machine.heap().array_length(*textures);
        if (!length) return std::unexpected(length.error());
        if (static_cast<usize>(*selected) < *length) {
            auto value = machine.heap().element(
                *textures, static_cast<usize>(*selected));
            if (!value) return std::unexpected(value.error());
            auto reference = value->as_reference();
            if (!reference) return std::unexpected(reference.error());
            selected_texture = *reference;
            state_textures = selected_texture;
            single = true;
        }
    }
    auto state = read_state(machine, graphics3d, layout, effect, x, y,
                            state_textures, single);
    if (!state) return std::unexpected(state.error());
    Batch batch {.state = std::move(*state)};
    batch.triangles.reserve((*model)->polygons.size());
    for (const software::Polygon& polygon : (*model)->polygons) {
        if ((polygon.pattern_mask & static_cast<u32>(*pattern)) !=
            polygon.pattern_mask) {
            continue;
        }
        Triangle triangle;
        triangle.blend = polygon.blend_mode;
        triangle.double_sided = polygon.double_sided;
        triangle.lighting = polygon.lighting;
        triangle.sphere_map = polygon.sphere_map;
        triangle.color_key = polygon.color_key;
        if (polygon.textured) {
            i32 texture_index = selected_texture_only ? 0 : polygon.texture_index;
            if (texture_index >= 0 &&
                static_cast<usize>(texture_index) < batch.state.textures.size()) {
                triangle.texture = batch.state.textures[
                    static_cast<usize>(texture_index)];
            }
            if (!triangle.texture) continue;
        }
        bool valid = true;
        for (usize index = 0U; index < 3U; ++index) {
            const usize vertex_index = polygon.indices[index];
            if (vertex_index >= pose->vertices.size()) {
                valid = false;
                break;
            }
            triangle.vertices[index].position = pose->vertices[vertex_index];
            if (vertex_index < pose->normals.size()) {
                triangle.vertices[index].normal = pose->normals[vertex_index];
            }
            triangle.vertices[index].color = polygon.colors[index];
            triangle.vertices[index].texcoord = polygon.texcoords[index];
        }
        if (valid) batch.triangles.push_back(std::move(triangle));
    }
    return batch;
}

[[nodiscard]] Vec3 array_vec3(std::span<const i32> values,
                              usize offset) noexcept {
    return {
        static_cast<float>(values[offset]),
        static_cast<float>(values[offset + 1U]),
        static_cast<float>(values[offset + 2U]),
    };
}

[[nodiscard]] Result<Batch> primitive_batch(
    BatchState state,
    u32 command,
    i32 count,
    std::span<const i32> coordinates,
    std::span<const i32> normals,
    std::span<const i32> texcoords,
    std::span<const i32> colors) {
    if (count <= 0 || count >= 256) {
        return fail_java("java/lang/IllegalArgumentException",
                         "invalid Micro3D primitive count");
    }
    const u32 type = command & kPrimitiveMask;
    const usize primitive_count = static_cast<usize>(count);
    usize vertices_per_primitive = 0U;
    switch (type) {
    case kPrimitivePoints: vertices_per_primitive = 1U; break;
    case kPrimitiveLines: vertices_per_primitive = 2U; break;
    case kPrimitiveTriangles: vertices_per_primitive = 3U; break;
    case kPrimitiveQuads: vertices_per_primitive = 4U; break;
    case kPrimitivePointSprites: vertices_per_primitive = 1U; break;
    default:
        return fail_java("java/lang/IllegalArgumentException",
                         "unsupported Micro3D primitive type");
    }
    if (coordinates.size() < primitive_count * vertices_per_primitive * 3U) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Micro3D vertex array is truncated");
    }
    const u32 normal_mode = command & kNormalMask;
    const u32 color_mode = command & kColorMask;
    const u32 texcoord_mode = command & kTexcoordMask;
    const u8 blend = static_cast<u8>((command & kPrimitiveBlendMask) >> 4U);
    Batch batch {.state = std::move(state)};

    const auto vertex_color = [&](usize primitive, usize vertex) {
        if (color_mode == kColorPerCommand && !colors.empty()) {
            return color_from_int(colors[0]);
        }
        if (color_mode == kColorPerFace && primitive < colors.size()) {
            return color_from_int(colors[primitive]);
        }
        const usize index = primitive * vertices_per_primitive + vertex;
        if (color_mode == kColorPerVertex && index < colors.size()) {
            return color_from_int(colors[index]);
        }
        return graphics::Pixel {0xFFFFFFFFU};
    };
    const auto vertex_normal = [&](usize primitive, usize vertex) {
        if (normal_mode == kNormalPerFace) {
            const usize offset = primitive * 3U;
            if (offset + 3U <= normals.size()) return array_vec3(normals, offset);
        }
        if (normal_mode == kNormalPerVertex) {
            const usize offset =
                (primitive * vertices_per_primitive + vertex) * 3U;
            if (offset + 3U <= normals.size()) return array_vec3(normals, offset);
        }
        return Vec3 {0.0F, 0.0F, 1.0F};
    };
    const auto make_vertex = [&](usize primitive, usize vertex) {
        const usize position_offset =
            (primitive * vertices_per_primitive + vertex) * 3U;
        Vertex result;
        result.position = array_vec3(coordinates, position_offset);
        result.normal = vertex_normal(primitive, vertex);
        result.color = vertex_color(primitive, vertex);
        if (texcoord_mode == kTexcoordPerVertex &&
            type != kPrimitivePointSprites) {
            const usize offset =
                (primitive * vertices_per_primitive + vertex) * 2U;
            if (offset + 2U <= texcoords.size()) {
                result.texcoord = {
                    static_cast<float>(texcoords[offset]),
                    static_cast<float>(texcoords[offset + 1U]),
                };
            }
        }
        return result;
    };

    if (type == kPrimitivePoints) {
        batch.points.reserve(primitive_count);
        for (usize primitive = 0U; primitive < primitive_count; ++primitive) {
            batch.points.push_back({make_vertex(primitive, 0U), blend});
        }
        return batch;
    }
    if (type == kPrimitiveLines) {
        batch.lines.reserve(primitive_count);
        for (usize primitive = 0U; primitive < primitive_count; ++primitive) {
            batch.lines.push_back({{
                make_vertex(primitive, 0U), make_vertex(primitive, 1U)}, blend});
        }
        return batch;
    }
    if (type == kPrimitivePointSprites) {
        if (batch.state.textures.empty()) return batch;
        const auto texture = batch.state.textures[
            std::min<usize>(static_cast<usize>(std::max(
                batch.state.texture_index, 0)), batch.state.textures.size() - 1U)];
        if (!texture || texcoord_mode == 0U) return batch;
        usize parameter_offset = 0U;
        std::array<i32, 8> retained {};
        for (usize primitive = 0U; primitive < primitive_count; ++primitive) {
            if (texcoord_mode != kPointParamsPerCommand || primitive == 0U) {
                if (parameter_offset + 8U > texcoords.size()) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "point sprite parameters are truncated");
                }
                std::copy_n(texcoords.begin() +
                            static_cast<isize>(parameter_offset), 8U,
                            retained.begin());
                parameter_offset += 8U;
            }
            const float half_width = static_cast<float>(retained[0]) * 0.5F;
            const float half_height = static_cast<float>(retained[1]) * 0.5F;
            const float radians = static_cast<float>(retained[2]) *
                                  std::numbers::pi_v<float> / 2048.0F;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            const Vec3 center = make_vertex(primitive, 0U).position;
            const auto corner = [&](float x_value, float y_value) {
                return Vec3 {
                    center.x + x_value * cosine - y_value * sine,
                    center.y + x_value * sine + y_value * cosine,
                    center.z,
                };
            };
            std::array<Vertex, 4> vertices {};
            vertices[0].position = corner(-half_width, -half_height);
            vertices[1].position = corner(half_width, -half_height);
            vertices[2].position = corner(-half_width, half_height);
            vertices[3].position = corner(half_width, half_height);
            const float u0 = static_cast<float>(retained[3]);
            const float v0 = static_cast<float>(retained[4]);
            const float u1 = static_cast<float>(retained[5] - 1);
            const float v1 = static_cast<float>(retained[6] - 1);
            vertices[0].texcoord = {u0, v0};
            vertices[1].texcoord = {u1, v0};
            vertices[2].texcoord = {u0, v1};
            vertices[3].texcoord = {u1, v1};
            Triangle first;
            first.vertices = {vertices[0], vertices[1], vertices[2]};
            first.texture = texture;
            first.blend = blend;
            first.double_sided = true;
            first.color_key = (command & kPrimitiveColorKey) != 0U;
            Triangle second = first;
            second.vertices = {vertices[2], vertices[1], vertices[3]};
            batch.triangles.push_back(std::move(first));
            batch.triangles.push_back(std::move(second));
        }
        return batch;
    }

    batch.triangles.reserve(type == kPrimitiveQuads
        ? primitive_count * 2U : primitive_count);
    for (usize primitive = 0U; primitive < primitive_count; ++primitive) {
        Triangle first;
        first.vertices = {make_vertex(primitive, 0U),
                          make_vertex(primitive, 1U),
                          make_vertex(primitive, 2U)};
        first.blend = blend;
        first.double_sided = true;
        first.lighting = (command & kPrimitiveLighting) != 0U;
        first.sphere_map = (command & kPrimitiveSphereMap) != 0U;
        first.color_key = (command & kPrimitiveColorKey) != 0U;
        if (texcoord_mode == kTexcoordPerVertex &&
            !batch.state.textures.empty()) {
            const usize texture_index = std::min<usize>(
                static_cast<usize>(std::max(batch.state.texture_index, 0)),
                batch.state.textures.size() - 1U);
            first.texture = batch.state.textures[texture_index];
        }
        batch.triangles.push_back(first);
        if (type == kPrimitiveQuads) {
            Triangle second = first;
            second.vertices = {make_vertex(primitive, 2U),
                               make_vertex(primitive, 1U),
                               make_vertex(primitive, 3U)};
            batch.triangles.push_back(std::move(second));
        }
    }
    return batch;
}

[[nodiscard]] bool project(const BatchState& state,
                           const graphics::Image& target,
                           const Vertex& input,
                           ScreenVertex& output) noexcept {
    const Vec3 view = transform_point(state.view, input.position);
    const Vec3 normal = transform_normal(state.view, input.normal);
    const Projection& projection = state.projection;
    float x = 0.0F;
    float y = 0.0F;
    float depth = 0.5F;
    float w = 1.0F;
    if (projection.type == kCommandParallelScale) {
        x = projection.center_x + view.x *
            static_cast<float>(projection.scale_x) * kFixedScale;
        y = projection.center_y - view.y *
            static_cast<float>(projection.scale_y) * kFixedScale;
        depth = clamp01((view.z + 32768.0F) / 65536.0F);
    } else if (projection.type == kCommandParallelSize) {
        if (projection.parallel_width <= 0 || projection.parallel_height <= 0) {
            return false;
        }
        x = projection.center_x + view.x *
            static_cast<float>(target.width()) /
            static_cast<float>(projection.parallel_width);
        y = projection.center_y - view.y *
            static_cast<float>(target.height()) /
            static_cast<float>(projection.parallel_height);
        depth = clamp01((view.z + 32768.0F) / 65536.0F);
    } else {
        const float near_value = static_cast<float>(projection.near_value);
        const float far_value = static_cast<float>(projection.far_value);
        if (view.z < near_value || view.z > far_value || view.z <= 1.0e-6F ||
            far_value <= near_value) {
            return false;
        }
        w = view.z;
        float scale_x = 0.0F;
        float scale_y = 0.0F;
        if (projection.type == kCommandPerspectiveFov) {
            const float radians = std::clamp(
                static_cast<float>(projection.angle), 2.0F, 2046.0F) *
                std::numbers::pi_v<float> / 2048.0F;
            scale_x = 1.0F / std::tan(radians);
            scale_y = scale_x * static_cast<float>(target.width()) /
                                  static_cast<float>(target.height());
            x = projection.center_x + scale_x * view.x / view.z *
                static_cast<float>(target.width()) * 0.5F;
            y = projection.center_y - scale_y * view.y / view.z *
                static_cast<float>(target.height()) * 0.5F;
        } else if (projection.type == kCommandPerspectiveWh) {
            const float width = static_cast<float>(projection.perspective_width) *
                                kFixedScale;
            const float height = static_cast<float>(projection.perspective_height) *
                                 kFixedScale;
            if (width == 0.0F || height == 0.0F) return false;
            x = projection.center_x + near_value * view.x / view.z *
                static_cast<float>(target.width()) / width;
            y = projection.center_y - near_value * view.y / view.z *
                static_cast<float>(target.height()) / height;
        } else {
            return false;
        }
        depth = clamp01((view.z - near_value) / (far_value - near_value));
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth)) {
        return false;
    }
    const float inverse_w = 1.0F / w;
    output.x = x;
    output.y = y;
    output.depth = depth;
    output.inverse_w = inverse_w;
    output.normal_over_w = {normal.x * inverse_w,
                            normal.y * inverse_w,
                            normal.z * inverse_w};
    output.color_over_w = {
        static_cast<float>(graphics::alpha(input.color)) / 255.0F * inverse_w,
        static_cast<float>(graphics::red(input.color)) / 255.0F * inverse_w,
        static_cast<float>(graphics::green(input.color)) / 255.0F * inverse_w,
        static_cast<float>(graphics::blue(input.color)) / 255.0F * inverse_w,
    };
    output.u_over_w = input.texcoord.u * inverse_w;
    output.v_over_w = input.texcoord.v * inverse_w;
    output.visible = true;
    return true;
}

[[nodiscard]] graphics::Pixel sample_texture(const Texture& texture,
                                             float u,
                                             float v) noexcept {
    if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty()) {
        return 0U;
    }
    const i32 x = std::clamp(static_cast<i32>(std::floor(u)),
                             0, texture.width - 1);
    const i32 y = std::clamp(static_cast<i32>(std::floor(v)),
                             0, texture.height - 1);
    const usize index = static_cast<usize>(y) *
                        static_cast<usize>(texture.width) +
                        static_cast<usize>(x);
    return index < texture.pixels.size() ? texture.pixels[index] : 0U;
}

[[nodiscard]] graphics::Pixel modulate(graphics::Pixel left,
                                       graphics::Pixel right) noexcept {
    const auto channel = [](u8 a, u8 b) {
        return static_cast<u8>((static_cast<u32>(a) * b + 127U) / 255U);
    };
    return graphics::argb(channel(graphics::alpha(left), graphics::alpha(right)),
                          channel(graphics::red(left), graphics::red(right)),
                          channel(graphics::green(left), graphics::green(right)),
                          channel(graphics::blue(left), graphics::blue(right)));
}

[[nodiscard]] graphics::Pixel shade(graphics::Pixel color,
                                    Vec3 normal,
                                    bool lighting,
                                    bool sphere_map,
                                    const EffectState& effect) noexcept {
    float light = 1.0F;
    const Vec3 normalized = normalize(normal);
    if (lighting && effect.lighting) {
        light = std::min(1.0F, effect.ambient + effect.directional *
            std::max(0.0F, normalized.x * effect.light_direction.x +
                           normalized.y * effect.light_direction.y +
                           normalized.z * effect.light_direction.z));
        if (effect.toon) {
            light = light < effect.toon_threshold
                ? effect.toon_low : effect.toon_high;
        }
    }
    float red = static_cast<float>(graphics::red(color)) * light;
    float green = static_cast<float>(graphics::green(color)) * light;
    float blue = static_cast<float>(graphics::blue(color)) * light;
    if (sphere_map && effect.sphere_map && effect.sphere_texture) {
        const Texture& texture = *effect.sphere_texture;
        const float u = normalized.x / 128.0F + 32.0F;
        const float v = normalized.y / 128.0F + 32.0F;
        const graphics::Pixel reflected = sample_texture(texture, u, v);
        red += static_cast<float>(graphics::red(reflected));
        green += static_cast<float>(graphics::green(reflected));
        blue += static_cast<float>(graphics::blue(reflected));
    }
    return graphics::argb(graphics::alpha(color),
        static_cast<u8>(std::clamp(red, 0.0F, 255.0F)),
        static_cast<u8>(std::clamp(green, 0.0F, 255.0F)),
        static_cast<u8>(std::clamp(blue, 0.0F, 255.0F)));
}

[[nodiscard]] graphics::Pixel blend(graphics::Pixel source,
                                    graphics::Pixel destination,
                                    u8 mode) noexcept {
    const auto half = [](u8 source_value, u8 destination_value) {
        return static_cast<u8>((static_cast<u32>(source_value) +
                                destination_value + 1U) / 2U);
    };
    const auto add = [](u8 source_value, u8 destination_value) {
        return static_cast<u8>(std::min<u32>(
            255U, static_cast<u32>(source_value) + destination_value));
    };
    const auto subtract = [](u8 source_value, u8 destination_value) {
        return static_cast<u8>(destination_value > source_value
            ? destination_value - source_value : 0U);
    };
    if (mode == 2U) {
        return graphics::argb(255U,
            half(graphics::red(source), graphics::red(destination)),
            half(graphics::green(source), graphics::green(destination)),
            half(graphics::blue(source), graphics::blue(destination)));
    }
    if (mode == 4U) {
        return graphics::argb(255U,
            add(graphics::red(source), graphics::red(destination)),
            add(graphics::green(source), graphics::green(destination)),
            add(graphics::blue(source), graphics::blue(destination)));
    }
    if (mode == 6U) {
        return graphics::argb(graphics::alpha(destination),
            subtract(graphics::red(source), graphics::red(destination)),
            subtract(graphics::green(source), graphics::green(destination)),
            subtract(graphics::blue(source), graphics::blue(destination)));
    }
    return graphics::opaque(source);
}

[[nodiscard]] float edge(float ax, float ay, float bx, float by,
                         float px, float py) noexcept {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void ensure_depth(Queue& queue, const graphics::Image& image) {
    if (queue.depth_width == image.width() &&
        queue.depth_height == image.height() &&
        queue.depth.size() == static_cast<usize>(image.width()) *
                              static_cast<usize>(image.height())) {
        return;
    }
    queue.depth_width = image.width();
    queue.depth_height = image.height();
    queue.depth.assign(static_cast<usize>(image.width()) *
                       static_cast<usize>(image.height()), 1.0F);
}

void clear_depth(Queue& queue) noexcept {
    std::fill(queue.depth.begin(), queue.depth.end(), 1.0F);
}

void rasterize_triangle(graphics::Image& image,
                        const graphics::Rect& clip,
                        Queue& queue,
                        const BatchState& state,
                        const Triangle& triangle,
                        bool transparent_pass) {
    const bool transparent = triangle.blend != 0U;
    if (transparent != transparent_pass) return;
    if (transparent && !state.effect.semi_transparent) return;
    std::array<ScreenVertex, 3> vertices {};
    for (usize index = 0U; index < 3U; ++index) {
        if (!project(state, image, triangle.vertices[index], vertices[index])) {
            return;
        }
    }
    const float area = edge(vertices[0].x, vertices[0].y,
                            vertices[1].x, vertices[1].y,
                            vertices[2].x, vertices[2].y);
    if (!std::isfinite(area) || std::abs(area) < 1.0e-6F) return;
    if (!triangle.double_sided && area >= 0.0F) return;
    const i32 minimum_x = std::max(clip.x, static_cast<i32>(std::floor(
        std::min({vertices[0].x, vertices[1].x, vertices[2].x}))));
    const i32 maximum_x = std::min(clip.x + clip.width - 1,
        static_cast<i32>(std::ceil(
            std::max({vertices[0].x, vertices[1].x, vertices[2].x}))));
    const i32 minimum_y = std::max(clip.y, static_cast<i32>(std::floor(
        std::min({vertices[0].y, vertices[1].y, vertices[2].y}))));
    const i32 maximum_y = std::min(clip.y + clip.height - 1,
        static_cast<i32>(std::ceil(
            std::max({vertices[0].y, vertices[1].y, vertices[2].y}))));
    if (minimum_x > maximum_x || minimum_y > maximum_y) return;
    auto pixels = image.mutable_pixels();
    for (i32 y = minimum_y; y <= maximum_y; ++y) {
        for (i32 x = minimum_x; x <= maximum_x; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            const float w0 = edge(vertices[1].x, vertices[1].y,
                                  vertices[2].x, vertices[2].y, px, py);
            const float w1 = edge(vertices[2].x, vertices[2].y,
                                  vertices[0].x, vertices[0].y, px, py);
            const float w2 = edge(vertices[0].x, vertices[0].y,
                                  vertices[1].x, vertices[1].y, px, py);
            const bool inside = area > 0.0F
                ? w0 >= 0.0F && w1 >= 0.0F && w2 >= 0.0F
                : w0 <= 0.0F && w1 <= 0.0F && w2 <= 0.0F;
            if (!inside) continue;
            const float b0 = w0 / area;
            const float b1 = w1 / area;
            const float b2 = w2 / area;
            const float depth = b0 * vertices[0].depth +
                                b1 * vertices[1].depth +
                                b2 * vertices[2].depth;
            const usize pixel_index = static_cast<usize>(y) *
                                      static_cast<usize>(image.width()) +
                                      static_cast<usize>(x);
            if (pixel_index >= queue.depth.size() ||
                depth > queue.depth[pixel_index] + 1.0e-6F) {
                continue;
            }
            const float denominator = b0 * vertices[0].inverse_w +
                                      b1 * vertices[1].inverse_w +
                                      b2 * vertices[2].inverse_w;
            if (std::abs(denominator) <= 1.0e-8F) continue;
            const auto interpolate = [&](usize channel) {
                return (b0 * vertices[0].color_over_w[channel] +
                        b1 * vertices[1].color_over_w[channel] +
                        b2 * vertices[2].color_over_w[channel]) / denominator;
            };
            graphics::Pixel source = graphics::argb(
                static_cast<u8>(clamp01(interpolate(0U)) * 255.0F),
                static_cast<u8>(clamp01(interpolate(1U)) * 255.0F),
                static_cast<u8>(clamp01(interpolate(2U)) * 255.0F),
                static_cast<u8>(clamp01(interpolate(3U)) * 255.0F));
            if (triangle.texture) {
                const float u = (b0 * vertices[0].u_over_w +
                                 b1 * vertices[1].u_over_w +
                                 b2 * vertices[2].u_over_w) / denominator;
                const float v = (b0 * vertices[0].v_over_w +
                                 b1 * vertices[1].v_over_w +
                                 b2 * vertices[2].v_over_w) / denominator;
                const graphics::Pixel texture = sample_texture(*triangle.texture,
                                                                u, v);
                if (triangle.color_key && graphics::alpha(texture) < 128U) {
                    continue;
                }
                source = modulate(source, texture);
                source = graphics::opaque(source);
            }
            Vec3 normal {
                (b0 * vertices[0].normal_over_w.x +
                 b1 * vertices[1].normal_over_w.x +
                 b2 * vertices[2].normal_over_w.x) / denominator,
                (b0 * vertices[0].normal_over_w.y +
                 b1 * vertices[1].normal_over_w.y +
                 b2 * vertices[2].normal_over_w.y) / denominator,
                (b0 * vertices[0].normal_over_w.z +
                 b1 * vertices[1].normal_over_w.z +
                 b2 * vertices[2].normal_over_w.z) / denominator,
            };
            source = shade(source, normal, triangle.lighting,
                           triangle.sphere_map, state.effect);
            pixels[pixel_index] = blend(source, pixels[pixel_index],
                                        triangle.blend);
            if (!transparent) queue.depth[pixel_index] = depth;
        }
    }
}

void rasterize_line(graphics::Image& image,
                    const graphics::Rect& clip,
                    Queue& queue,
                    const BatchState& state,
                    const Line& line,
                    bool transparent_pass) {
    const bool transparent = line.blend != 0U;
    if (transparent != transparent_pass ||
        (transparent && !state.effect.semi_transparent)) {
        return;
    }
    ScreenVertex first;
    ScreenVertex second;
    if (!project(state, image, line.vertices[0], first) ||
        !project(state, image, line.vertices[1], second)) {
        return;
    }
    const float dx = second.x - first.x;
    const float dy = second.y - first.y;
    const i32 steps = std::max(1, static_cast<i32>(std::ceil(
        std::max(std::abs(dx), std::abs(dy)))));
    auto pixels = image.mutable_pixels();
    for (i32 step = 0; step <= steps; ++step) {
        const float amount = static_cast<float>(step) / static_cast<float>(steps);
        const i32 x = static_cast<i32>(std::lround(first.x + dx * amount));
        const i32 y = static_cast<i32>(std::lround(first.y + dy * amount));
        if (x < clip.x || y < clip.y || x >= clip.x + clip.width ||
            y >= clip.y + clip.height) {
            continue;
        }
        const float depth = first.depth + (second.depth - first.depth) * amount;
        const usize index = static_cast<usize>(y) *
                            static_cast<usize>(image.width()) +
                            static_cast<usize>(x);
        if (index >= queue.depth.size() || depth > queue.depth[index]) continue;
        const auto channel = [&](auto accessor) {
            const float value = static_cast<float>(accessor(line.vertices[0].color)) +
                (static_cast<float>(accessor(line.vertices[1].color)) -
                 static_cast<float>(accessor(line.vertices[0].color))) * amount;
            return static_cast<u8>(std::clamp(value, 0.0F, 255.0F));
        };
        const graphics::Pixel source = graphics::argb(255U,
            channel(graphics::red), channel(graphics::green),
            channel(graphics::blue));
        pixels[index] = blend(source, pixels[index], line.blend);
        if (!transparent) queue.depth[index] = depth;
    }
}

void rasterize_point(graphics::Image& image,
                     const graphics::Rect& clip,
                     Queue& queue,
                     const BatchState& state,
                     const Point& point,
                     bool transparent_pass) {
    const bool transparent = point.blend != 0U;
    if (transparent != transparent_pass ||
        (transparent && !state.effect.semi_transparent)) {
        return;
    }
    ScreenVertex value;
    if (!project(state, image, point.vertex, value)) return;
    const i32 x = static_cast<i32>(std::lround(value.x));
    const i32 y = static_cast<i32>(std::lround(value.y));
    if (x < clip.x || y < clip.y || x >= clip.x + clip.width ||
        y >= clip.y + clip.height) {
        return;
    }
    const usize index = static_cast<usize>(y) *
                        static_cast<usize>(image.width()) +
                        static_cast<usize>(x);
    if (index >= queue.depth.size() || value.depth > queue.depth[index]) return;
    auto pixels = image.mutable_pixels();
    pixels[index] = blend(graphics::opaque(point.vertex.color), pixels[index],
                          point.blend);
    if (!transparent) queue.depth[index] = value.depth;
}

[[nodiscard]] i32 metal_texture_index(
    const std::shared_ptr<const Texture>& texture,
    std::vector<PhoneMEMetal3DTextureSource>& sources,
    std::unordered_map<const Texture*, i32>& indices) {
    if (!texture || texture->width <= 0 || texture->height <= 0 ||
        texture->pixels.empty()) {
        return -1;
    }
    if (const auto found = indices.find(texture.get()); found != indices.end()) {
        return found->second;
    }
    if (sources.size() >= static_cast<usize>(std::numeric_limits<i32>::max())) {
        return -1;
    }
    const i32 index = static_cast<i32>(sources.size());
    sources.push_back(PhoneMEMetal3DTextureSource {
        .pixels = texture->pixels.data(),
        .width = texture->width,
        .height = texture->height,
    });
    indices.emplace(texture.get(), index);
    return index;
}

[[nodiscard]] std::optional<PhoneMEMetal3DTriangle> prepare_metal_triangle(
    const graphics::Image& image,
    const graphics::Rect& clip,
    const BatchState& state,
    const Triangle& triangle,
    bool transparent_pass,
    std::vector<PhoneMEMetal3DTextureSource>& textures,
    std::unordered_map<const Texture*, i32>& texture_indices,
    usize& workload) {
    const bool transparent = triangle.blend != 0U;
    if (transparent != transparent_pass ||
        (transparent && !state.effect.semi_transparent)) {
        return std::nullopt;
    }
    std::array<ScreenVertex, 3> vertices {};
    for (usize index = 0U; index < vertices.size(); ++index) {
        if (!project(state, image, triangle.vertices[index], vertices[index])) {
            return std::nullopt;
        }
    }
    const float area = edge(vertices[0].x, vertices[0].y,
                            vertices[1].x, vertices[1].y,
                            vertices[2].x, vertices[2].y);
    if (!std::isfinite(area) || std::abs(area) < 1.0e-6F ||
        (!triangle.double_sided && area >= 0.0F)) {
        return std::nullopt;
    }
    const i32 minimum_x = std::max(clip.x, static_cast<i32>(std::floor(
        std::min({vertices[0].x, vertices[1].x, vertices[2].x}))));
    const i32 maximum_x = std::min(clip.x + clip.width - 1,
        static_cast<i32>(std::ceil(
            std::max({vertices[0].x, vertices[1].x, vertices[2].x}))));
    const i32 minimum_y = std::max(clip.y, static_cast<i32>(std::floor(
        std::min({vertices[0].y, vertices[1].y, vertices[2].y}))));
    const i32 maximum_y = std::min(clip.y + clip.height - 1,
        static_cast<i32>(std::ceil(
            std::max({vertices[0].y, vertices[1].y, vertices[2].y}))));
    if (minimum_x > maximum_x || minimum_y > maximum_y) return std::nullopt;

    PhoneMEMetal3DTriangle result {};
    for (usize index = 0U; index < vertices.size(); ++index) {
        const ScreenVertex& source = vertices[index];
        result.vertices[index] = PhoneMEMetal3DVertex {
            .x = source.x,
            .y = source.y,
            .depth = source.depth,
            .inverse_w = source.inverse_w,
            .normal_x_over_w = source.normal_over_w.x,
            .normal_y_over_w = source.normal_over_w.y,
            .normal_z_over_w = source.normal_over_w.z,
            .u_over_w = source.u_over_w,
            .v_over_w = source.v_over_w,
            .alpha_over_w = source.color_over_w[0U],
            .red_over_w = source.color_over_w[1U],
            .green_over_w = source.color_over_w[2U],
            .blue_over_w = source.color_over_w[3U],
        };
    }
    result.minimum_x = minimum_x;
    result.minimum_y = minimum_y;
    result.maximum_x = maximum_x;
    result.maximum_y = maximum_y;
    result.texture_index = metal_texture_index(
        triangle.texture, textures, texture_indices);
    result.sphere_texture_index = metal_texture_index(
        state.effect.sphere_texture, textures, texture_indices);
    result.blend_mode = triangle.blend;
    if (triangle.lighting && state.effect.lighting) {
        result.flags |= PHONEME_METAL3D_LIGHTING;
    }
    if (triangle.sphere_map && state.effect.sphere_map &&
        result.sphere_texture_index >= 0) {
        result.flags |= PHONEME_METAL3D_SPHERE_MAP;
    }
    if (triangle.color_key) result.flags |= PHONEME_METAL3D_COLOR_KEY;
    if (state.effect.toon &&
        (result.flags & PHONEME_METAL3D_LIGHTING) != 0U) {
        result.flags |= PHONEME_METAL3D_TOON;
    }
    result.ambient = state.effect.ambient;
    result.directional = state.effect.directional;
    result.light_x = state.effect.light_direction.x;
    result.light_y = state.effect.light_direction.y;
    result.light_z = state.effect.light_direction.z;
    result.toon_threshold = state.effect.toon_threshold;
    result.toon_high = state.effect.toon_high;
    result.toon_low = state.effect.toon_low;
    const usize width = static_cast<usize>(maximum_x - minimum_x + 1);
    const usize height = static_cast<usize>(maximum_y - minimum_y + 1);
    if (width <= std::numeric_limits<usize>::max() / height) {
        workload += width * height;
    } else {
        workload = std::numeric_limits<usize>::max();
    }
    return result;
}

[[nodiscard]] bool try_metal_triangles(Target& target,
                                       Queue& queue,
                                       const Batch& batch,
                                       bool transparent_pass) {
#if defined(__APPLE__)
    const PhoneMEMetal3DRasterizer rasterizer =
        phoneme_metal3d_get_rasterizer();
    if (rasterizer == nullptr) return false;
    const graphics::Rect clip = graphics::intersect(target.clip, batch.state.clip);
    if (graphics::empty(clip)) return false;
    std::vector<PhoneMEMetal3DTriangle> triangles;
    std::vector<PhoneMEMetal3DTextureSource> textures;
    std::unordered_map<const Texture*, i32> texture_indices;
    triangles.reserve(batch.triangles.size());
    usize workload = 0U;
    for (const Triangle& triangle : batch.triangles) {
        auto prepared = prepare_metal_triangle(
            *target.image, clip, batch.state, triangle, transparent_pass,
            textures, texture_indices, workload);
        if (prepared) triangles.push_back(*prepared);
    }
    if (triangles.empty() ||
        (triangles.size() < 4U && workload < 2'048U)) {
        return false;
    }
    auto pixels = target.image->mutable_pixels();
    return rasterizer(
        pixels.data(), queue.depth.data(), target.image->width(),
        target.image->height(), triangles.data(), triangles.size(),
        textures.data(), textures.size());
#else
    static_cast<void>(target);
    static_cast<void>(queue);
    static_cast<void>(batch);
    static_cast<void>(transparent_pass);
    return false;
#endif
}

void render_batch(Target& target,
                  Queue& queue,
                  const Batch& batch,
                  bool transparent_pass) {
    const graphics::Rect clip = graphics::intersect(target.clip, batch.state.clip);
    if (graphics::empty(clip)) return;
    const bool rendered_triangles = try_metal_triangles(
        target, queue, batch, transparent_pass);
    if (!rendered_triangles) {
        for (const Triangle& triangle : batch.triangles) {
            rasterize_triangle(*target.image, clip, queue, batch.state,
                               triangle, transparent_pass);
        }
    }
    for (const Line& line : batch.lines) {
        rasterize_line(*target.image, clip, queue, batch.state,
                       line, transparent_pass);
    }
    for (const Point& point : batch.points) {
        rasterize_point(*target.image, clip, queue, batch.state,
                        point, transparent_pass);
    }
}

[[nodiscard]] Status render_batches(Machine& machine,
                                    ObjectRef graphics3d,
                                    std::span<const Batch> batches,
                                    bool clear_after) {
    auto target = bound_target(machine, graphics3d);
    if (!target) return std::unexpected(target.error());
    if (!target->context.rendering_enabled || graphics::empty(target->clip)) {
        return {};
    }
    Queue& queue = g_queues[render_key(machine, graphics3d)];
    ensure_depth(queue, *target->image);
    for (bool transparent : {false, true}) {
        for (const Batch& batch : batches) {
            render_batch(*target, queue, batch, transparent);
        }
    }
    target->image->mark_dirty_region(target->clip.x, target->clip.y,
                                     target->clip.width, target->clip.height);
    if (clear_after) clear_depth(queue);
    return {};
}

[[nodiscard]] Status flush_queue(Machine& machine,
                                 ObjectRef graphics3d) {
    std::scoped_lock lock(g_render_mutex);
    Queue& queue = g_queues[render_key(machine, graphics3d)];
    if (queue.batches.empty()) {
        clear_depth(queue);
        return {};
    }
    auto rendered = render_batches(machine, graphics3d, queue.batches, true);
    if (!rendered) return rendered;
    queue.batches.clear();
    return {};
}

void clear_queue(Machine& machine, ObjectRef graphics3d) noexcept {
    std::scoped_lock lock(g_render_mutex);
    g_queues.erase(render_key(machine, graphics3d));
}

[[nodiscard]] Status enqueue(Machine& machine,
                             ObjectRef graphics3d,
                             Batch batch) {
    std::scoped_lock lock(g_render_mutex);
    g_queues[render_key(machine, graphics3d)].batches.push_back(
        std::move(batch));
    return {};
}

[[nodiscard]] Result<std::vector<Mat34>> affine_array(Machine& machine,
                                                      ObjectRef layout) {
    std::vector<Mat34> result;
    auto array = reference_field(
        machine, layout, kFigureLayout, "affineArray",
        "[Lcom/mascotcapsule/micro3d/v3/AffineTrans;");
    if (!array) return std::unexpected(array.error());
    if (array->is_null()) return result;
    auto length = machine.heap().array_length(*array);
    if (!length) return std::unexpected(length.error());
    result.reserve(*length);
    for (usize index = 0U; index < *length; ++index) {
        auto value = machine.heap().element(*array, index);
        if (!value) return std::unexpected(value.error());
        auto reference = value->as_reference();
        if (!reference) return std::unexpected(reference.error());
        auto affine = read_affine(machine, *reference, "FigureLayout affine array");
        if (!affine) return std::unexpected(affine.error());
        result.push_back(affine_matrix(*affine));
    }
    return result;
}

[[nodiscard]] Status process_command_list(
    Machine& machine,
    ObjectRef graphics3d,
    BatchState state,
    ObjectRef layout,
    std::span<const i32> commands) {
    if (commands.empty() || std::bit_cast<u32>(commands[0]) !=
                            kCommandListVersion) {
        return fail_java("java/lang/IllegalArgumentException",
                         "unsupported Micro3D command list version");
    }
    auto transforms = affine_array(machine, layout);
    if (!transforms) return std::unexpected(transforms.error());
    usize offset = 1U;
    const auto require = [&](usize count) {
        return count <= commands.size() && offset <= commands.size() - count;
    };
    while (offset < commands.size()) {
        const u32 command = std::bit_cast<u32>(commands[offset++]);
        const u32 opcode = command & 0xFF000000U;
        if (opcode == kCommandEnd) {
            return flush_queue(machine, graphics3d);
        }
        if (opcode == kCommandAffineIndex) {
            const usize index = static_cast<usize>(command & 0x00FFFFFFU);
            if (index >= transforms->size()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "command list affine index is invalid");
            }
            state.view = (*transforms)[index];
            continue;
        }
        if (opcode == kCommandAmbientLight) {
            if (!require(1U)) break;
            state.effect.lighting = true;
            state.effect.ambient = clamp01(
                static_cast<float>(commands[offset++]) / 4096.0F);
            continue;
        }
        if (opcode == kCommandAttribute) {
            const u32 attributes = command & 0x00FFFFFFU;
            state.effect.lighting = (attributes & kAttributeLighting) != 0U;
            state.effect.sphere_map = (attributes & kAttributeSphereMap) != 0U;
            state.effect.toon = (attributes & kAttributeToon) != 0U;
            state.effect.semi_transparent =
                (attributes & kAttributeSemiTransparent) != 0U;
            continue;
        }
        if (opcode == kCommandCenter) {
            if (!require(2U)) break;
            state.projection.center_x = static_cast<float>(commands[offset++]);
            state.projection.center_y = static_cast<float>(commands[offset++]);
            continue;
        }
        if (opcode == kCommandClip) {
            if (!require(4U)) break;
            graphics::Rect requested {
                commands[offset], commands[offset + 1U],
                commands[offset + 2U], commands[offset + 3U]};
            offset += 4U;
            state.clip = graphics::intersect(state.clip, requested);
            continue;
        }
        if (opcode == kCommandDirectionLight) {
            if (!require(4U)) break;
            state.effect.lighting = true;
            state.effect.light_direction = normalize({
                -static_cast<float>(commands[offset]),
                -static_cast<float>(commands[offset + 1U]),
                -static_cast<float>(commands[offset + 2U]),
            });
            state.effect.directional = std::clamp(
                static_cast<float>(commands[offset + 3U]) / 4096.0F,
                0.0F, 4.0F);
            offset += 4U;
            continue;
        }
        if (opcode == kCommandFlush) {
            auto flushed = flush_queue(machine, graphics3d);
            if (!flushed) return flushed;
            continue;
        }
        if (opcode == kCommandNop) {
            const usize count = static_cast<usize>(command & 0x00FFFFFFU);
            if (!require(count)) break;
            offset += count;
            continue;
        }
        if (opcode == kCommandParallelScale) {
            if (!require(2U)) break;
            state.projection.type = kCommandParallelScale;
            state.projection.scale_x = commands[offset++];
            state.projection.scale_y = commands[offset++];
            continue;
        }
        if (opcode == kCommandParallelSize) {
            if (!require(2U)) break;
            state.projection.type = kCommandParallelSize;
            state.projection.parallel_width = commands[offset++];
            state.projection.parallel_height = commands[offset++];
            continue;
        }
        if (opcode == kCommandPerspectiveFov) {
            if (!require(3U)) break;
            state.projection.type = kCommandPerspectiveFov;
            state.projection.near_value = commands[offset++];
            state.projection.far_value = commands[offset++];
            state.projection.angle = commands[offset++];
            continue;
        }
        if (opcode == kCommandPerspectiveWh) {
            if (!require(4U)) break;
            state.projection.type = kCommandPerspectiveWh;
            state.projection.near_value = commands[offset++];
            state.projection.far_value = commands[offset++];
            state.projection.perspective_width = commands[offset++];
            state.projection.perspective_height = commands[offset++];
            continue;
        }
        if (opcode == kCommandTextureIndex) {
            const i32 index = static_cast<i32>(command & 0x00FFFFFFU);
            if (index < 16) state.texture_index = index;
            continue;
        }
        if (opcode == kCommandThreshold) {
            if (!require(3U)) break;
            state.effect.toon = true;
            state.effect.toon_threshold = clamp01(
                static_cast<float>(commands[offset++]) / 255.0F);
            state.effect.toon_high = clamp01(
                static_cast<float>(commands[offset++]) / 255.0F);
            state.effect.toon_low = clamp01(
                static_cast<float>(commands[offset++]) / 255.0F);
            continue;
        }

        const u32 primitive_type = command & kPrimitiveMask;
        const usize primitive_count = static_cast<usize>((command >> 16U) & 0xFFU);
        constexpr std::array<usize, 6> sizes {0U, 1U, 2U, 3U, 4U, 1U};
        const usize type_index = static_cast<usize>(primitive_type >> 24U);
        if (primitive_type == 0U || type_index >= sizes.size() ||
            primitive_count == 0U) {
            return fail_java("java/lang/IllegalArgumentException",
                             "invalid primitive in Micro3D command list");
        }
        const usize vertices_per_primitive = sizes[type_index];
        const usize coordinate_count = primitive_count *
                                       vertices_per_primitive * 3U;
        if (!require(coordinate_count)) break;
        const usize coordinate_offset = offset;
        offset += coordinate_count;
        usize normal_count = 0U;
        if ((command & kNormalMask) == kNormalPerFace) {
            normal_count = primitive_count * 3U;
        } else if ((command & kNormalMask) == kNormalPerVertex) {
            normal_count = coordinate_count;
        }
        if (!require(normal_count)) break;
        const usize normal_offset = offset;
        offset += normal_count;
        usize texcoord_count = 0U;
        if (primitive_type == kPrimitivePointSprites) {
            const u32 mode = command & kTexcoordMask;
            texcoord_count = mode == kPointParamsPerCommand
                ? 8U : (mode == 0U ? 0U : primitive_count * 8U);
        } else if ((command & kTexcoordMask) == kTexcoordPerVertex) {
            texcoord_count = primitive_count * vertices_per_primitive * 2U;
        }
        if (!require(texcoord_count)) break;
        const usize texcoord_offset = offset;
        offset += texcoord_count;
        usize color_count = 0U;
        if ((command & kColorMask) == kColorPerCommand) {
            color_count = 1U;
        } else if ((command & kColorMask) == kColorPerFace) {
            color_count = primitive_count;
        } else if ((command & kColorMask) == kColorPerVertex) {
            color_count = primitive_count * vertices_per_primitive;
        }
        if (!require(color_count)) break;
        const usize color_offset = offset;
        offset += color_count;
        auto batch = primitive_batch(
            state, command, static_cast<i32>(primitive_count),
            commands.subspan(coordinate_offset, coordinate_count),
            commands.subspan(normal_offset, normal_count),
            commands.subspan(texcoord_offset, texcoord_count),
            commands.subspan(color_offset, color_count));
        if (!batch) return std::unexpected(batch.error());
        auto queued = enqueue(machine, graphics3d, std::move(*batch));
        if (!queued) return queued;
    }
    return fail_java("java/lang/IllegalArgumentException",
                     "truncated Micro3D command list");
}

void register_lifecycle(NativeMethodRegistry& registry) {
    m3g::add(registry, kGraphics3D, "<init>", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.<init>");
            if (!self) return std::unexpected(self.error());
            auto stored = set_int_field(machine, *self, kGraphics3D,
                                        "bound", 0, "Z");
            if (!stored) return std::unexpected(stored.error());
            clear_queue(machine, *self);
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "bind",
             "(Ljavax/microedition/lcdui/Graphics;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.bind");
            auto graphics = m3g::reference_argument(args, 1U,
                                                     "Graphics3D.bind", false);
            if (!self) return std::unexpected(self.error());
            if (!graphics) return std::unexpected(graphics.error());
            auto bound = int_field(machine, *self, kGraphics3D, "bound", "Z");
            if (!bound) return std::unexpected(bound.error());
            if (*bound != 0) {
                return fail_java("java/lang/IllegalStateException",
                                 "Micro3D target is already bound");
            }
            auto context = machine.graphics().context(graphics->bits);
            if (!context) return std::unexpected(context.error());
            auto stored_graphics = set_reference_field(
                machine, *self, kGraphics3D, "graphics",
                "Ljavax/microedition/lcdui/Graphics;", *graphics);
            auto stored_bound = set_int_field(machine, *self, kGraphics3D,
                                              "bound", 1, "Z");
            if (!stored_graphics) return std::unexpected(stored_graphics.error());
            if (!stored_bound) return std::unexpected(stored_bound.error());
            clear_queue(machine, *self);
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "release",
             "(Ljavax/microedition/lcdui/Graphics;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.release");
            auto graphics = m3g::reference_argument(args, 1U,
                                                     "Graphics3D.release", false);
            if (!self) return std::unexpected(self.error());
            if (!graphics) return std::unexpected(graphics.error());
            auto current = reference_field(
                machine, *self, kGraphics3D, "graphics",
                "Ljavax/microedition/lcdui/Graphics;");
            if (!current) return std::unexpected(current.error());
            if (!current->is_null() && *current != *graphics) {
                clear_queue(machine, *self);
            } else {
                auto flushed = flush_queue(machine, *self);
                if (!flushed) return std::unexpected(flushed.error());
            }
            auto stored_graphics = set_reference_field(
                machine, *self, kGraphics3D, "graphics",
                "Ljavax/microedition/lcdui/Graphics;", {});
            auto stored_bound = set_int_field(machine, *self, kGraphics3D,
                                              "bound", 0, "Z");
            if (!stored_graphics) return std::unexpected(stored_graphics.error());
            if (!stored_bound) return std::unexpected(stored_bound.error());
            clear_queue(machine, *self);
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "dispose", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.dispose");
            if (!self) return std::unexpected(self.error());
            clear_queue(machine, *self);
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "flush", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.flush");
            if (!self) return std::unexpected(self.error());
            auto bound = require_bound(machine, *self, "Graphics3D.flush");
            if (!bound) return std::unexpected(bound.error());
            auto flushed = flush_queue(machine, *self);
            if (!flushed) return std::unexpected(flushed.error());
            return void_result();
        });
}

void register_figure_rendering(NativeMethodRegistry& registry) {
    constexpr const char* descriptor =
        "(Lcom/mascotcapsule/micro3d/v3/Figure;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;)V";
    m3g::add(registry, kGraphics3D, "drawFigure", descriptor,
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.drawFigure");
            auto figure = m3g::reference_argument(args, 1U,
                                                   "Graphics3D.drawFigure", false);
            auto x = m3g::int_argument(args, 2U, "Graphics3D.drawFigure");
            auto y = m3g::int_argument(args, 3U, "Graphics3D.drawFigure");
            auto layout = m3g::reference_argument(args, 4U,
                                                   "Graphics3D.drawFigure", false);
            auto effect = m3g::reference_argument(args, 5U,
                                                   "Graphics3D.drawFigure", false);
            if (!self) return std::unexpected(self.error());
            if (!figure) return std::unexpected(figure.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!layout) return std::unexpected(layout.error());
            if (!effect) return std::unexpected(effect.error());
            auto bound = require_bound(machine, *self, "Graphics3D.drawFigure");
            if (!bound) return std::unexpected(bound.error());
            auto live = require_not_disposed(machine, *figure, kFigure, "Figure");
            if (!live) return std::unexpected(live.error());
            auto batch = figure_batch(machine, *self, *figure, *layout, *effect,
                                      *x, *y, true);
            if (!batch) return std::unexpected(batch.error());
            {
                std::scoped_lock lock(g_render_mutex);
                Queue& queue = g_queues[render_key(machine, *self)];
                queue.batches.push_back(std::move(*batch));
                auto rendered = render_batches(machine, *self,
                                                queue.batches, true);
                if (!rendered) return std::unexpected(rendered.error());
                queue.batches.clear();
            }
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "renderFigure", descriptor,
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.renderFigure");
            auto figure = m3g::reference_argument(args, 1U,
                                                   "Graphics3D.renderFigure", false);
            auto x = m3g::int_argument(args, 2U, "Graphics3D.renderFigure");
            auto y = m3g::int_argument(args, 3U, "Graphics3D.renderFigure");
            auto layout = m3g::reference_argument(args, 4U,
                                                   "Graphics3D.renderFigure", false);
            auto effect = m3g::reference_argument(args, 5U,
                                                   "Graphics3D.renderFigure", false);
            if (!self) return std::unexpected(self.error());
            if (!figure) return std::unexpected(figure.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!layout) return std::unexpected(layout.error());
            if (!effect) return std::unexpected(effect.error());
            auto bound = require_bound(machine, *self, "Graphics3D.renderFigure");
            if (!bound) return std::unexpected(bound.error());
            auto live = require_not_disposed(machine, *figure, kFigure, "Figure");
            if (!live) return std::unexpected(live.error());
            auto batch = figure_batch(machine, *self, *figure, *layout, *effect,
                                      *x, *y, false);
            if (!batch) return std::unexpected(batch.error());
            auto queued = enqueue(machine, *self, std::move(*batch));
            if (!queued) return std::unexpected(queued.error());
            return void_result();
        });
}

void register_command_rendering(NativeMethodRegistry& registry) {
    const auto render = [](Machine& machine,
                           std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "Graphics3D.drawCommandList");
        auto textures = m3g::reference_argument(args, 1U,
                                                 "Graphics3D.drawCommandList", true);
        auto x = m3g::int_argument(args, 2U, "Graphics3D.drawCommandList");
        auto y = m3g::int_argument(args, 3U, "Graphics3D.drawCommandList");
        auto layout = m3g::reference_argument(args, 4U,
                                               "Graphics3D.drawCommandList", false);
        auto effect = m3g::reference_argument(args, 5U,
                                               "Graphics3D.drawCommandList", false);
        auto commands = m3g::reference_argument(args, 6U,
                                                 "Graphics3D.drawCommandList", false);
        if (!self) return std::unexpected(self.error());
        if (!textures) return std::unexpected(textures.error());
        if (!x) return std::unexpected(x.error());
        if (!y) return std::unexpected(y.error());
        if (!layout) return std::unexpected(layout.error());
        if (!effect) return std::unexpected(effect.error());
        if (!commands) return std::unexpected(commands.error());
        auto bound = require_bound(machine, *self, "Graphics3D.drawCommandList");
        if (!bound) return std::unexpected(bound.error());
        auto class_name = textures->is_null()
            ? Result<std::string>(std::string {})
            : machine.heap().class_name(*textures);
        if (!class_name) return std::unexpected(class_name.error());
        const bool single = *class_name == kTexture;
        auto state = read_state(machine, *self, *layout, *effect, *x, *y,
                                *textures, single);
        if (!state) return std::unexpected(state.error());
        auto values = read_int_array(machine, *commands,
                                     "Graphics3D.drawCommandList");
        if (!values) return std::unexpected(values.error());
        auto processed = process_command_list(machine, *self, std::move(*state),
                                              *layout, *values);
        if (!processed) return std::unexpected(processed.error());
        return void_result();
    };
    m3g::add(registry, kGraphics3D, "drawCommandList",
        "([Lcom/mascotcapsule/micro3d/v3/Texture;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;[I)V", render);
    m3g::add(registry, kGraphics3D, "drawCommandList",
        "(Lcom/mascotcapsule/micro3d/v3/Texture;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;[I)V", render);
}

void register_primitive_rendering(NativeMethodRegistry& registry) {
    m3g::add(registry, kGraphics3D, "renderPrimitives",
        "(Lcom/mascotcapsule/micro3d/v3/Texture;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;II[I[I[I[I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.renderPrimitives");
            auto texture = m3g::reference_argument(args, 1U,
                                                    "Graphics3D.renderPrimitives", true);
            auto x = m3g::int_argument(args, 2U, "Graphics3D.renderPrimitives");
            auto y = m3g::int_argument(args, 3U, "Graphics3D.renderPrimitives");
            auto layout = m3g::reference_argument(args, 4U,
                                                   "Graphics3D.renderPrimitives", false);
            auto effect = m3g::reference_argument(args, 5U,
                                                   "Graphics3D.renderPrimitives", false);
            auto command = m3g::int_argument(args, 6U,
                                              "Graphics3D.renderPrimitives");
            auto count = m3g::int_argument(args, 7U,
                                            "Graphics3D.renderPrimitives");
            if (!self) return std::unexpected(self.error());
            if (!texture) return std::unexpected(texture.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!layout) return std::unexpected(layout.error());
            if (!effect) return std::unexpected(effect.error());
            if (!command) return std::unexpected(command.error());
            if (!count) return std::unexpected(count.error());
            std::array<std::vector<i32>, 4> arrays;
            for (usize index = 0U; index < arrays.size(); ++index) {
                auto array = m3g::reference_argument(
                    args, 8U + index, "Graphics3D.renderPrimitives", false);
                if (!array) return std::unexpected(array.error());
                auto values = read_int_array(machine, *array,
                                              "Graphics3D.renderPrimitives");
                if (!values) return std::unexpected(values.error());
                arrays[index] = std::move(*values);
            }
            auto bound = require_bound(machine, *self,
                                       "Graphics3D.renderPrimitives");
            if (!bound) return std::unexpected(bound.error());
            if (*command < 0 || *count <= 0 || *count >= 256) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid Micro3D primitive command");
            }
            auto state = read_state(machine, *self, *layout, *effect,
                                    *x, *y, *texture, true);
            if (!state) return std::unexpected(state.error());
            const u32 encoded = std::bit_cast<u32>(*command) |
                                (static_cast<u32>(*count) << 16U);
            auto batch = primitive_batch(std::move(*state), encoded, *count,
                                         arrays[0], arrays[1], arrays[2],
                                         arrays[3]);
            if (!batch) return std::unexpected(batch.error());
            auto queued = enqueue(machine, *self, std::move(*batch));
            if (!queued) return std::unexpected(queued.error());
            return void_result();
        });
}

} // namespace
} // namespace micro3d

void register_micro3d_render_natives(NativeMethodRegistry& registry) {
    micro3d::register_lifecycle(registry);
    micro3d::register_figure_rendering(registry);
    micro3d::register_command_rendering(registry);
    micro3d::register_primitive_rendering(registry);
}

} // namespace phoneme::vm
