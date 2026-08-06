#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "phoneme/graphics/Color.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm::micro3d::software {

struct Vec3 final {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

struct Mat34 final {
    std::array<float, 12> values {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
};

struct TexCoord final {
    float u {0.0F};
    float v {0.0F};
};

struct Polygon final {
    std::array<u32, 3> indices {};
    std::array<TexCoord, 3> texcoords {};
    std::array<graphics::Pixel, 3> colors {
        0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU,
    };
    u32 pattern_mask {0U};
    i32 texture_index {-1};
    u8 blend_mode {0U};
    bool textured {false};
    bool double_sided {false};
    bool lighting {false};
    bool sphere_map {false};
    bool color_key {false};
};

struct Bone final {
    usize vertex_count {0U};
    i32 parent {-1};
    Mat34 transform {};
};

struct Model final {
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Polygon> polygons;
    std::vector<Bone> bones;
    i32 patterns {1};
    i32 textures {0};
};

struct Texture final {
    i32 width {0};
    i32 height {0};
    std::vector<graphics::Pixel> pixels;
};

struct KeyVec3 final {
    i32 frame {0};
    Vec3 value {};
};

struct KeyScalar final {
    i32 frame {0};
    float value {0.0F};
};

struct BoneAction final {
    i32 type {1};
    Mat34 fixed {};
    std::vector<KeyVec3> translate;
    std::vector<KeyVec3> scale;
    std::vector<KeyVec3> rotate;
    std::vector<KeyScalar> roll;
};

struct DynamicPattern final {
    i32 frame {0};
    i32 pattern {0};
};

struct Action final {
    i32 keyframes {0};
    std::vector<BoneAction> bones;
    std::vector<DynamicPattern> dynamic_patterns;
};

struct ActionTable final {
    std::vector<Action> actions;
};

struct Pose final {
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
};

[[nodiscard]] Result<std::shared_ptr<const Model>> decode_model(
    std::span<const u8> bytes);
[[nodiscard]] Result<std::shared_ptr<const ActionTable>> decode_actions(
    std::span<const u8> bytes);
[[nodiscard]] Result<std::shared_ptr<const Texture>> decode_texture(
    std::span<const u8> bytes);

[[nodiscard]] Status cache_model(Machine& machine, ObjectRef object,
                                 std::span<const u8> bytes);
[[nodiscard]] Status cache_actions(Machine& machine, ObjectRef object,
                                   std::span<const u8> bytes);
[[nodiscard]] Status cache_texture(Machine& machine, ObjectRef object,
                                   std::span<const u8> bytes);
[[nodiscard]] Result<std::shared_ptr<const Model>> cached_model(
    Machine& machine, ObjectRef object);
[[nodiscard]] Result<std::shared_ptr<const ActionTable>> cached_actions(
    Machine& machine, ObjectRef object);
[[nodiscard]] Result<std::shared_ptr<const Texture>> cached_texture(
    Machine& machine, ObjectRef object);
void erase_resource(Machine& machine, ObjectRef object) noexcept;

[[nodiscard]] Result<Pose> pose_model(const Model& model,
                                      const ActionTable* actions,
                                      i32 action,
                                      i32 frame);
[[nodiscard]] i32 dynamic_pattern(const ActionTable& actions,
                                  i32 action,
                                  i32 frame,
                                  i32 fallback) noexcept;

} // namespace phoneme::vm::micro3d::software
