#include "Micro3dSoftware.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace phoneme::vm::micro3d::software {
namespace {

constexpr float kFixedScale = 1.0F / 4096.0F;
constexpr float kAngleScale = std::numbers::pi_v<float> / 2048.0F;
constexpr std::array<i32, 4> kPackedSizes {8, 10, 13, 16};
constexpr std::array<i32, 8> kNormalPool {0, 0, 4096, 0, 0, -4096, 0, 0};
constexpr usize kMaximumVertices = 21'845U;
constexpr usize kMaximumPolygons = 262'144U;
constexpr usize kMaximumBones = 4'096U;
constexpr usize kMaximumActions = 4'096U;
constexpr usize kMaximumKeys = 1'048'576U;

[[nodiscard]] std::unexpected<Error> malformed(std::string message) {
    return fail_java("java/lang/IllegalArgumentException", std::move(message));
}

class Cursor final {
public:
    explicit Cursor(std::span<const u8> data) noexcept : data_(data) {}

    [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] usize position() const noexcept { return position_; }
    [[nodiscard]] usize remaining() const noexcept {
        return position_ <= data_.size() ? data_.size() - position_ : 0U;
    }

    [[nodiscard]] u8 read_u8(std::string_view what) {
        if (!require(1U, what)) return 0U;
        return data_[position_++];
    }

    [[nodiscard]] i8 read_i8(std::string_view what) {
        return static_cast<i8>(read_u8(what));
    }

    [[nodiscard]] u16 read_u16(std::string_view what) {
        if (!require(2U, what)) return 0U;
        const u16 value = static_cast<u16>(data_[position_]) |
            static_cast<u16>(static_cast<u16>(data_[position_ + 1U]) << 8U);
        position_ += 2U;
        return value;
    }

    [[nodiscard]] i16 read_i16(std::string_view what) {
        return static_cast<i16>(read_u16(what));
    }

    [[nodiscard]] u32 read_u32(std::string_view what) {
        if (!require(4U, what)) return 0U;
        const u32 value = static_cast<u32>(data_[position_]) |
            (static_cast<u32>(data_[position_ + 1U]) << 8U) |
            (static_cast<u32>(data_[position_ + 2U]) << 16U) |
            (static_cast<u32>(data_[position_ + 3U]) << 24U);
        position_ += 4U;
        return value;
    }

    [[nodiscard]] i32 read_i32(std::string_view what) {
        return static_cast<i32>(read_u32(what));
    }

    [[nodiscard]] u32 read_bits(i32 size, std::string_view what) {
        if (size < 0 || size > 25) {
            set_error(std::string(what) + " uses an invalid bit width");
            return 0U;
        }
        if (size == 0) return 0U;
        while (cached_bits_ < size) {
            const u32 next = read_u8(what);
            if (!ok()) return 0U;
            bit_cache_ |= next << static_cast<u32>(cached_bits_);
            cached_bits_ += 8;
        }
        const u32 mask = (1U << static_cast<u32>(size)) - 1U;
        const u32 value = bit_cache_ & mask;
        bit_cache_ >>= static_cast<u32>(size);
        cached_bits_ -= size;
        return value;
    }

    [[nodiscard]] i32 read_signed_bits(i32 size, std::string_view what) {
        const u32 value = read_bits(size, what);
        if (!ok() || size == 0) return 0;
        const u32 sign = 1U << static_cast<u32>(size - 1);
        const u32 extended = (value ^ sign) - sign;
        return static_cast<i32>(extended);
    }

    void clear_bits() noexcept {
        bit_cache_ = 0U;
        cached_bits_ = 0;
    }

    void skip(usize count, std::string_view what) {
        if (!require(count, what)) return;
        position_ += count;
    }

private:
    [[nodiscard]] bool require(usize count, std::string_view what) {
        if (!ok()) return false;
        if (count > data_.size() || position_ > data_.size() - count) {
            set_error("truncated " + std::string(what));
            return false;
        }
        return true;
    }

    void set_error(std::string value) {
        if (error_.empty()) error_ = std::move(value);
    }

    std::span<const u8> data_;
    usize position_ {0U};
    u32 bit_cache_ {0U};
    i32 cached_bits_ {0};
    std::string error_;
};

[[nodiscard]] bool valid_count_product(usize left, usize right,
                                       usize limit) noexcept {
    return left == 0U || right <= limit / left;
}

[[nodiscard]] u32 pattern_bit(usize index) noexcept {
    if (index == 0U) return 0U;
    return 1U << static_cast<u32>(index & 31U);
}

[[nodiscard]] Vec3 normalize(Vec3 value,
                             Vec3 fallback = {0.0F, 0.0F, 1.0F}) noexcept {
    const float length = std::sqrt(value.x * value.x + value.y * value.y +
                                   value.z * value.z);
    if (!std::isfinite(length) || length <= 1.0e-8F) return fallback;
    const float inverse = 1.0F / length;
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

[[nodiscard]] Mat34 multiply(const Mat34& left,
                             const Mat34& right) noexcept {
    Mat34 result;
    const auto& l = left.values;
    const auto& r = right.values;
    auto& m = result.values;
    m[0] = l[0] * r[0] + l[1] * r[4] + l[2] * r[8];
    m[1] = l[0] * r[1] + l[1] * r[5] + l[2] * r[9];
    m[2] = l[0] * r[2] + l[1] * r[6] + l[2] * r[10];
    m[3] = l[0] * r[3] + l[1] * r[7] + l[2] * r[11] + l[3];
    m[4] = l[4] * r[0] + l[5] * r[4] + l[6] * r[8];
    m[5] = l[4] * r[1] + l[5] * r[5] + l[6] * r[9];
    m[6] = l[4] * r[2] + l[5] * r[6] + l[6] * r[10];
    m[7] = l[4] * r[3] + l[5] * r[7] + l[6] * r[11] + l[7];
    m[8] = l[8] * r[0] + l[9] * r[4] + l[10] * r[8];
    m[9] = l[8] * r[1] + l[9] * r[5] + l[10] * r[9];
    m[10] = l[8] * r[2] + l[9] * r[6] + l[10] * r[10];
    m[11] = l[8] * r[3] + l[9] * r[7] + l[10] * r[11] + l[11];
    return result;
}

[[nodiscard]] Vec3 transform_point(const Mat34& matrix,
                                   Vec3 value) noexcept {
    const auto& m = matrix.values;
    return {
        value.x * m[0] + value.y * m[1] + value.z * m[2] + m[3],
        value.x * m[4] + value.y * m[5] + value.z * m[6] + m[7],
        value.x * m[8] + value.y * m[9] + value.z * m[10] + m[11],
    };
}

[[nodiscard]] Vec3 transform_normal(const Mat34& matrix,
                                    Vec3 value) noexcept {
    const auto& m = matrix.values;
    return normalize({
        value.x * m[0] + value.y * m[1] + value.z * m[2],
        value.x * m[4] + value.y * m[5] + value.z * m[6],
        value.x * m[8] + value.y * m[9] + value.z * m[10],
    });
}

void set_rotation_direction(Mat34& matrix, Vec3 direction) noexcept {
    Vec3 value = normalize(direction);
    const float xx = value.x * value.x;
    const float yy = value.y * value.y;
    auto& m = matrix.values;
    if (xx > 0.0F || yy > 0.0F) {
        const float a = (1.0F - value.z) / (yy + xx);
        const float b = -value.x * value.y * a;
        m[0] = value.z + yy * a;
        m[1] = b;
        m[2] = value.x;
        m[4] = b;
        m[5] = value.z + xx * a;
        m[6] = value.y;
        m[8] = -value.x;
        m[9] = -value.y;
    } else {
        m[0] = 1.0F;
        m[1] = 0.0F;
        m[2] = 0.0F;
        m[4] = 0.0F;
        m[5] = value.z;
        m[6] = 0.0F;
        m[8] = 0.0F;
        m[9] = 0.0F;
    }
    m[10] = value.z;
}

void apply_roll(Mat34& matrix, float angle) noexcept {
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    auto& m = matrix.values;
    const float m00 = m[0];
    const float m01 = m[1];
    const float m10 = m[4];
    const float m11 = m[5];
    const float m20 = m[8];
    const float m21 = m[9];
    m[0] = m00 * cosine + m01 * sine;
    m[1] = m01 * cosine - m00 * sine;
    m[4] = m10 * cosine + m11 * sine;
    m[5] = m11 * cosine - m10 * sine;
    m[8] = m20 * cosine + m21 * sine;
    m[9] = m21 * cosine - m20 * sine;
}

[[nodiscard]] Vec3 sample(const std::vector<KeyVec3>& keys,
                          float frame,
                          Vec3 fallback) noexcept {
    if (keys.empty()) return fallback;
    if (frame <= static_cast<float>(keys.front().frame)) return keys.front().value;
    if (frame >= static_cast<float>(keys.back().frame)) return keys.back().value;
    const auto next = std::upper_bound(
        keys.begin(), keys.end(), frame,
        [](float value, const KeyVec3& key) {
            return value < static_cast<float>(key.frame);
        });
    if (next == keys.begin()) return next->value;
    const auto previous = std::prev(next);
    const float span = static_cast<float>(next->frame - previous->frame);
    if (span <= 0.0F) return previous->value;
    const float amount = (frame - static_cast<float>(previous->frame)) / span;
    return {
        previous->value.x + (next->value.x - previous->value.x) * amount,
        previous->value.y + (next->value.y - previous->value.y) * amount,
        previous->value.z + (next->value.z - previous->value.z) * amount,
    };
}

[[nodiscard]] float sample(const std::vector<KeyScalar>& keys,
                           float frame,
                           float fallback) noexcept {
    if (keys.empty()) return fallback;
    if (frame <= static_cast<float>(keys.front().frame)) return keys.front().value;
    if (frame >= static_cast<float>(keys.back().frame)) return keys.back().value;
    const auto next = std::upper_bound(
        keys.begin(), keys.end(), frame,
        [](float value, const KeyScalar& key) {
            return value < static_cast<float>(key.frame);
        });
    if (next == keys.begin()) return next->value;
    const auto previous = std::prev(next);
    const float span = static_cast<float>(next->frame - previous->frame);
    if (span <= 0.0F) return previous->value;
    const float amount = (frame - static_cast<float>(previous->frame)) / span;
    return previous->value + (next->value - previous->value) * amount;
}

[[nodiscard]] Mat34 evaluate(const BoneAction& action,
                             i32 fixed_frame) noexcept {
    if (action.type == 0) return action.fixed;
    Mat34 matrix;
    if (action.type == 1) return matrix;
    const float frame = static_cast<float>(std::max(fixed_frame, 0)) / 65536.0F;
    if (action.type == 2 || action.type == 6) {
        const Vec3 translation = sample(action.translate, frame, {});
        matrix.values[3] = translation.x;
        matrix.values[7] = translation.y;
        matrix.values[11] = translation.z;
    } else if (action.type == 3 && !action.translate.empty()) {
        const Vec3 translation = action.translate.front().value;
        matrix.values[3] = translation.x;
        matrix.values[7] = translation.y;
        matrix.values[11] = translation.z;
    }
    if (action.type >= 2 && action.type <= 6) {
        set_rotation_direction(matrix,
            sample(action.rotate, frame, {0.0F, 0.0F, 1.0F}));
    }
    if (action.type == 2 || action.type == 4 || action.type == 6) {
        apply_roll(matrix, sample(action.roll, frame, 0.0F));
    } else if (action.type == 3 && !action.roll.empty()) {
        apply_roll(matrix, action.roll.front().value);
    }
    if (action.type == 2) {
        const Vec3 scale = sample(action.scale, frame, {1.0F, 1.0F, 1.0F});
        matrix.values[0] *= scale.x;
        matrix.values[1] *= scale.y;
        matrix.values[2] *= scale.z;
        matrix.values[4] *= scale.x;
        matrix.values[5] *= scale.y;
        matrix.values[6] *= scale.z;
        matrix.values[8] *= scale.x;
        matrix.values[9] *= scale.y;
        matrix.values[10] *= scale.z;
    }
    return matrix;
}

[[nodiscard]] graphics::Pixel rgb(u8 red, u8 green, u8 blue) noexcept {
    return graphics::argb(255U, red, green, blue);
}

void apply_material(Polygon& polygon, i32 material) noexcept {
    polygon.blend_mode = static_cast<u8>(material & 6);
    polygon.color_key = (material & 1) != 0;
    polygon.double_sided = (material & 16) != 0;
    polygon.lighting = (material & 32) != 0;
    polygon.sphere_map = (material & 64) != 0;
}

[[nodiscard]] Result<std::vector<Vec3>> read_vertices(Cursor& cursor,
                                                       i32 format,
                                                       usize count) {
    std::vector<Vec3> vertices;
    vertices.reserve(count);
    if (format == 1) {
        for (usize index = 0U; index < count; ++index) {
            vertices.push_back({
                static_cast<float>(cursor.read_i16("MBAC vertex x")),
                static_cast<float>(cursor.read_i16("MBAC vertex y")),
                static_cast<float>(cursor.read_i16("MBAC vertex z")),
            });
        }
    } else if (format == 2) {
        while (vertices.size() < count && cursor.ok()) {
            const u32 chunk = cursor.read_bits(8, "MBAC vertex chunk");
            const usize group = static_cast<usize>((chunk & 0x3FU) + 1U);
            const i32 size = kPackedSizes[static_cast<usize>(chunk >> 6U)];
            if (group > count - vertices.size()) {
                return malformed("MBAC vertex chunk exceeds the declared count");
            }
            for (usize index = 0U; index < group; ++index) {
                vertices.push_back({
                    static_cast<float>(cursor.read_signed_bits(size, "MBAC vertex x")),
                    static_cast<float>(cursor.read_signed_bits(size, "MBAC vertex y")),
                    static_cast<float>(cursor.read_signed_bits(size, "MBAC vertex z")),
                });
            }
        }
    } else {
        return malformed("unsupported MBAC vertex format");
    }
    if (!cursor.ok()) return malformed(cursor.error());
    cursor.clear_bits();
    return vertices;
}

[[nodiscard]] Result<std::vector<Vec3>> read_normals(Cursor& cursor,
                                                      i32 format,
                                                      usize count) {
    std::vector<Vec3> normals;
    if (format == 0) return normals;
    normals.reserve(count);
    if (format == 1) {
        for (usize index = 0U; index < count; ++index) {
            normals.push_back({
                static_cast<float>(cursor.read_i16("MBAC normal x")),
                static_cast<float>(cursor.read_i16("MBAC normal y")),
                static_cast<float>(cursor.read_i16("MBAC normal z")),
            });
        }
    } else if (format == 2) {
        for (usize index = 0U; index < count; ++index) {
            i32 x = static_cast<i32>(cursor.read_bits(7, "MBAC normal x"));
            i32 y = 0;
            i32 z = 0;
            if (x == 64) {
                const usize type = static_cast<usize>(
                    cursor.read_bits(3, "MBAC pooled normal"));
                if (type > 5U) return malformed("invalid MBAC pooled normal");
                z = kNormalPool[type];
                y = kNormalPool[type + 1U];
                x = kNormalPool[type + 2U];
            } else {
                x = static_cast<i32>(static_cast<i8>(x << 1U)) * 64;
                const u32 encoded_y = cursor.read_bits(7, "MBAC normal y");
                y = static_cast<i32>(static_cast<i8>(encoded_y << 1U)) * 64;
                const bool negative = cursor.read_bits(1, "MBAC normal sign") != 0U;
                const i64 square = 16'777'216LL -
                    static_cast<i64>(x) * static_cast<i64>(x) -
                    static_cast<i64>(y) * static_cast<i64>(y);
                z = square > 0 ? static_cast<i32>(std::lround(
                    std::sqrt(static_cast<double>(square)))) : 0;
                if (negative) z = -z;
            }
            normals.push_back({static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(z)});
        }
    } else {
        return malformed("unsupported MBAC normal format");
    }
    if (!cursor.ok()) return malformed(cursor.error());
    cursor.clear_bits();
    return normals;
}

struct PatternCounts final {
    usize triangles {0U};
    usize quads {0U};
};

using PatternTable = std::vector<std::vector<PatternCounts>>;

[[nodiscard]] Result<std::vector<Polygon>> read_color_polygons(
    Cursor& cursor, usize vertex_count, usize color_count,
    usize triangle_count, usize quad_count) {
    const i32 material_bits = static_cast<i32>(cursor.read_u8("MBAC color material width"));
    const i32 vertex_bits = static_cast<i32>(cursor.read_u8("MBAC color vertex width"));
    const i32 color_bits = static_cast<i32>(cursor.read_u8("MBAC color width"));
    const i32 color_id_bits = static_cast<i32>(cursor.read_u8("MBAC color ID width"));
    static_cast<void>(cursor.read_u8("MBAC color reserved byte"));
    if (material_bits > 25 || vertex_bits > 25 || color_bits > 25 ||
        color_id_bits > 25) {
        return malformed("invalid MBAC color polygon bit width");
    }
    std::vector<graphics::Pixel> palette;
    palette.reserve(color_count);
    for (usize index = 0U; index < color_count; ++index) {
        const u8 red = static_cast<u8>(cursor.read_bits(color_bits, "MBAC palette red"));
        const u8 green = static_cast<u8>(cursor.read_bits(color_bits, "MBAC palette green"));
        const u8 blue = static_cast<u8>(cursor.read_bits(color_bits, "MBAC palette blue"));
        palette.push_back(rgb(red, green, blue));
    }
    std::vector<Polygon> polygons;
    polygons.reserve(triangle_count + quad_count * 2U);
    const auto read_polygon = [&](bool quad) -> Status {
        const i32 material = static_cast<i32>(
            cursor.read_bits(material_bits, "MBAC color material")) << 1;
        if ((material & 0xFC09) != 0) {
            return malformed("unsupported MBAC color material flags");
        }
        std::array<u32, 4> indices {};
        const usize count = quad ? 4U : 3U;
        for (usize index = 0U; index < count; ++index) {
            indices[index] = cursor.read_bits(vertex_bits, "MBAC color vertex index");
            if (indices[index] >= vertex_count) {
                return malformed("MBAC color polygon references an invalid vertex");
            }
        }
        const usize color_index = static_cast<usize>(
            cursor.read_bits(color_id_bits, "MBAC color ID"));
        if (color_index >= palette.size()) {
            return malformed("MBAC color polygon references an invalid palette entry");
        }
        Polygon first;
        first.indices = {indices[0], indices[1], indices[2]};
        first.colors.fill(palette[color_index]);
        apply_material(first, material);
        polygons.push_back(first);
        if (quad) {
            Polygon second = first;
            second.indices = {indices[2], indices[1], indices[3]};
            polygons.push_back(second);
        }
        return {};
    };
    for (usize index = 0U; index < triangle_count; ++index) {
        auto status = read_polygon(false);
        if (!status) return std::unexpected(status.error());
    }
    for (usize index = 0U; index < quad_count; ++index) {
        auto status = read_polygon(true);
        if (!status) return std::unexpected(status.error());
    }
    if (!cursor.ok()) return malformed(cursor.error());
    return polygons;
}

[[nodiscard]] Result<std::vector<Polygon>> read_textured_polygons(
    Cursor& cursor, i32 format, usize vertex_count,
    usize triangle_count, usize quad_count) {
    std::vector<Polygon> polygons;
    polygons.reserve(triangle_count + quad_count * 2U);
    i32 material_bits = 16;
    i32 vertex_bits = 16;
    i32 uv_bits = 8;
    if (format == 2) {
        material_bits = static_cast<i32>(cursor.read_u8("MBAC material width"));
        vertex_bits = static_cast<i32>(cursor.read_u8("MBAC vertex index width"));
        uv_bits = 7;
    } else if (format == 3) {
        material_bits = static_cast<i32>(cursor.read_bits(8, "MBAC material width"));
        vertex_bits = static_cast<i32>(cursor.read_bits(8, "MBAC vertex index width"));
        uv_bits = static_cast<i32>(cursor.read_bits(8, "MBAC UV width"));
        static_cast<void>(cursor.read_bits(8, "MBAC textured reserved byte"));
    } else if (format != 1) {
        return malformed("unsupported MBAC polygon format");
    }
    if (material_bits > 25 || vertex_bits > 25 || uv_bits > 25) {
        return malformed("invalid MBAC textured polygon bit width");
    }
    const auto read_value = [&](i32 bits, std::string_view what) -> u32 {
        return format == 1 ? static_cast<u32>(cursor.read_u16(what))
                           : cursor.read_bits(bits, what);
    };
    const auto read_uv = [&](std::string_view what) -> float {
        if (format == 1) return static_cast<float>(
            static_cast<u8>(cursor.read_i8(what)));
        return static_cast<float>(cursor.read_bits(uv_bits, what));
    };
    const auto read_polygon = [&](bool quad) -> Status {
        i32 material = static_cast<i32>(read_value(material_bits,
                                                   "MBAC textured material"));
        if (format == 1) {
            if ((!quad && (material & 0xFFF9) != 0) ||
                (quad && ((material & 0xFFF8) != 0 || (material & 1) == 0))) {
                return malformed("unsupported MBAC v1 textured material flags");
            }
            material = (material & 4) << 2 | (material & 2) >> 1;
        } else if ((format == 2 && (material & 0xFF88) != 0) ||
                   (format == 3 && (material & 0xFC08) != 0)) {
            return malformed("unsupported MBAC textured material flags");
        }
        std::array<u32, 4> indices {};
        std::array<TexCoord, 4> texcoords {};
        const usize count = quad ? 4U : 3U;
        for (usize index = 0U; index < count; ++index) {
            indices[index] = read_value(vertex_bits, "MBAC textured vertex index");
            if (indices[index] >= vertex_count) {
                return malformed("MBAC textured polygon references an invalid vertex");
            }
        }
        for (usize index = 0U; index < count; ++index) {
            texcoords[index] = {read_uv("MBAC texture u"), read_uv("MBAC texture v")};
        }
        Polygon first;
        first.textured = true;
        first.indices = {indices[0], indices[1], indices[2]};
        first.texcoords = {texcoords[0], texcoords[1], texcoords[2]};
        apply_material(first, material);
        polygons.push_back(first);
        if (quad) {
            Polygon second = first;
            second.indices = {indices[2], indices[1], indices[3]};
            second.texcoords = {texcoords[2], texcoords[1], texcoords[3]};
            polygons.push_back(second);
        }
        return {};
    };
    for (usize index = 0U; index < triangle_count; ++index) {
        auto status = read_polygon(false);
        if (!status) return std::unexpected(status.error());
    }
    for (usize index = 0U; index < quad_count; ++index) {
        auto status = read_polygon(true);
        if (!status) return std::unexpected(status.error());
    }
    if (!cursor.ok()) return malformed(cursor.error());
    return polygons;
}

[[nodiscard]] Result<std::vector<KeyVec3>> read_vec3_keys(
    Cursor& cursor, bool scaled, std::string_view what) {
    const usize count = cursor.read_u16(what);
    if (count > kMaximumKeys) return malformed("too many MTRA vector keys");
    std::vector<KeyVec3> keys;
    keys.reserve(count);
    for (usize index = 0U; index < count; ++index) {
        const i32 frame = static_cast<i32>(cursor.read_u16(what));
        const float factor = scaled ? kFixedScale : 1.0F;
        keys.push_back({frame, {
            static_cast<float>(cursor.read_i16(what)) * factor,
            static_cast<float>(cursor.read_i16(what)) * factor,
            static_cast<float>(cursor.read_i16(what)) * factor,
        }});
    }
    if (!cursor.ok()) return malformed(cursor.error());
    return keys;
}

[[nodiscard]] Result<std::vector<KeyScalar>> read_roll_keys(
    Cursor& cursor, std::string_view what) {
    const usize count = cursor.read_u16(what);
    if (count > kMaximumKeys) return malformed("too many MTRA roll keys");
    std::vector<KeyScalar> keys;
    keys.reserve(count);
    for (usize index = 0U; index < count; ++index) {
        keys.push_back({
            static_cast<i32>(cursor.read_u16(what)),
            static_cast<float>(cursor.read_i16(what)) * kAngleScale,
        });
    }
    if (!cursor.ok()) return malformed(cursor.error());
    return keys;
}

[[nodiscard]] Result<BoneAction> read_bone_action(Cursor& cursor) {
    BoneAction action;
    action.type = static_cast<i32>(cursor.read_u8("MTRA bone action type"));
    if (action.type < 0 || action.type > 6) {
        return malformed("unsupported MTRA bone action type");
    }
    if (action.type == 0) {
        for (usize index = 0U; index < 12U; ++index) {
            const float factor = (index == 3U || index == 7U || index == 11U)
                ? 1.0F : kFixedScale;
            action.fixed.values[index] =
                static_cast<float>(cursor.read_i16("MTRA fixed matrix")) * factor;
        }
    } else if (action.type == 2) {
        auto translate = read_vec3_keys(cursor, false, "MTRA translation");
        auto scale = read_vec3_keys(cursor, true, "MTRA scale");
        auto rotate = read_vec3_keys(cursor, false, "MTRA rotation");
        auto roll = read_roll_keys(cursor, "MTRA roll");
        if (!translate) return std::unexpected(translate.error());
        if (!scale) return std::unexpected(scale.error());
        if (!rotate) return std::unexpected(rotate.error());
        if (!roll) return std::unexpected(roll.error());
        action.translate = std::move(*translate);
        action.scale = std::move(*scale);
        action.rotate = std::move(*rotate);
        action.roll = std::move(*roll);
    } else if (action.type == 3) {
        action.translate.push_back({0, {
            static_cast<float>(cursor.read_i16("MTRA fixed translation")),
            static_cast<float>(cursor.read_i16("MTRA fixed translation")),
            static_cast<float>(cursor.read_i16("MTRA fixed translation")),
        }});
        auto rotate = read_vec3_keys(cursor, false, "MTRA rotation");
        if (!rotate) return std::unexpected(rotate.error());
        action.rotate = std::move(*rotate);
        action.roll.push_back({0, static_cast<float>(
            cursor.read_i16("MTRA fixed roll")) * kAngleScale});
    } else if (action.type == 4) {
        auto rotate = read_vec3_keys(cursor, false, "MTRA rotation");
        auto roll = read_roll_keys(cursor, "MTRA roll");
        if (!rotate) return std::unexpected(rotate.error());
        if (!roll) return std::unexpected(roll.error());
        action.rotate = std::move(*rotate);
        action.roll = std::move(*roll);
    } else if (action.type == 5) {
        auto rotate = read_vec3_keys(cursor, false, "MTRA rotation");
        if (!rotate) return std::unexpected(rotate.error());
        action.rotate = std::move(*rotate);
    } else if (action.type == 6) {
        auto translate = read_vec3_keys(cursor, false, "MTRA translation");
        auto rotate = read_vec3_keys(cursor, false, "MTRA rotation");
        auto roll = read_roll_keys(cursor, "MTRA roll");
        if (!translate) return std::unexpected(translate.error());
        if (!rotate) return std::unexpected(rotate.error());
        if (!roll) return std::unexpected(roll.error());
        action.translate = std::move(*translate);
        action.rotate = std::move(*rotate);
        action.roll = std::move(*roll);
    }
    if (!cursor.ok()) return malformed(cursor.error());
    return action;
}

struct ResourceKey final {
    const Machine* machine {nullptr};
    u64 object {0U};
    friend bool operator==(const ResourceKey&, const ResourceKey&) noexcept = default;
};

struct ResourceKeyHash final {
    [[nodiscard]] usize operator()(const ResourceKey& key) const noexcept {
        const auto pointer = reinterpret_cast<std::uintptr_t>(key.machine);
        return std::hash<std::uintptr_t> {}(pointer) ^
            (std::hash<u64> {}(key.object) << 1U);
    }
};

std::mutex g_cache_mutex;
std::unordered_map<ResourceKey, std::shared_ptr<const Model>, ResourceKeyHash>
    g_models;
std::unordered_map<ResourceKey, std::shared_ptr<const ActionTable>, ResourceKeyHash>
    g_actions;
std::unordered_map<ResourceKey, std::shared_ptr<const Texture>, ResourceKeyHash>
    g_textures;

} // namespace

Result<std::shared_ptr<const Model>> decode_model(std::span<const u8> bytes) {
    Cursor cursor(bytes);
    if (cursor.read_u8("MBAC signature") != static_cast<u8>('M') ||
        cursor.read_u8("MBAC signature") != static_cast<u8>('B')) {
        return malformed("not an MBAC resource");
    }
    const i32 version = static_cast<i32>(cursor.read_u8("MBAC version"));
    if (cursor.read_u8("MBAC version terminator") != 0U ||
        version < 2 || version > 5) {
        return malformed("unsupported MBAC version");
    }
    i32 vertex_format = 1;
    i32 normal_format = 0;
    i32 polygon_format = 1;
    i32 bone_format = 1;
    if (version > 3) {
        vertex_format = static_cast<i32>(cursor.read_u8("MBAC vertex format"));
        normal_format = static_cast<i32>(cursor.read_u8("MBAC normal format"));
        polygon_format = static_cast<i32>(cursor.read_u8("MBAC polygon format"));
        bone_format = static_cast<i32>(cursor.read_u8("MBAC bone format"));
    }
    if (bone_format != 1) return malformed("unsupported MBAC bone format");

    const usize vertex_count = cursor.read_u16("MBAC vertex count");
    const usize textured_triangles = cursor.read_u16("MBAC textured triangle count");
    const usize textured_quads = cursor.read_u16("MBAC textured quad count");
    const usize bone_count = cursor.read_u16("MBAC bone count");
    usize color_triangles = 0U;
    usize color_quads = 0U;
    usize texture_count = 1U;
    usize pattern_count = 1U;
    usize color_count = 0U;
    if (polygon_format >= 3) {
        color_triangles = cursor.read_u16("MBAC color triangle count");
        color_quads = cursor.read_u16("MBAC color quad count");
        texture_count = cursor.read_u16("MBAC texture count");
        pattern_count = cursor.read_u16("MBAC pattern count");
        color_count = cursor.read_u16("MBAC palette count");
    }
    const usize polygon_count = textured_triangles + textured_quads * 2U +
        color_triangles + color_quads * 2U;
    if (!cursor.ok()) return malformed(cursor.error());
    if (vertex_count > kMaximumVertices || bone_count > kMaximumBones ||
        texture_count > 16U || pattern_count == 0U || pattern_count > 33U ||
        color_count > 256U || polygon_count > kMaximumPolygons) {
        return malformed("MBAC declared counts exceed safety limits");
    }

    PatternTable patterns(pattern_count,
        std::vector<PatternCounts>(texture_count + 1U));
    if (version == 5) {
        for (usize pattern = 0U; pattern < pattern_count; ++pattern) {
            for (usize group = 0U; group <= texture_count; ++group) {
                patterns[pattern][group].triangles =
                    cursor.read_u16("MBAC pattern triangle count");
                patterns[pattern][group].quads =
                    cursor.read_u16("MBAC pattern quad count");
            }
        }
    } else {
        patterns[0][0] = {color_triangles, color_quads};
        if (texture_count > 0U) {
            patterns[0][1] = {textured_triangles, textured_quads};
        }
    }

    auto vertices = read_vertices(cursor, vertex_format, vertex_count);
    if (!vertices) return std::unexpected(vertices.error());
    auto normals = read_normals(cursor, normal_format, vertex_count);
    if (!normals) return std::unexpected(normals.error());
    auto color_polygons = color_triangles + color_quads > 0U
        ? read_color_polygons(cursor, vertex_count, color_count,
                              color_triangles, color_quads)
        : Result<std::vector<Polygon>>(std::vector<Polygon> {});
    if (!color_polygons) return std::unexpected(color_polygons.error());
    auto textured_polygons = textured_triangles + textured_quads > 0U
        ? read_textured_polygons(cursor, polygon_format, vertex_count,
                                 textured_triangles, textured_quads)
        : Result<std::vector<Polygon>>(std::vector<Polygon> {});
    if (!textured_polygons) return std::unexpected(textured_polygons.error());
    cursor.clear_bits();

    usize color_triangle_offset = 0U;
    usize color_quad_offset = color_triangles;
    usize textured_triangle_offset = 0U;
    usize textured_quad_offset = textured_triangles;
    for (usize pattern = 0U; pattern < pattern_count; ++pattern) {
        const u32 mask = pattern_bit(pattern);
        const auto& color_group = patterns[pattern][0];
        for (usize index = 0U; index < color_group.triangles; ++index) {
            if (color_triangle_offset >= color_triangles) {
                return malformed("MBAC pattern color triangle counts overflow");
            }
            (*color_polygons)[color_triangle_offset++].pattern_mask = mask;
        }
        for (usize index = 0U; index < color_group.quads; ++index) {
            if (color_quad_offset + 1U >= color_polygons->size()) {
                return malformed("MBAC pattern color quad counts overflow");
            }
            (*color_polygons)[color_quad_offset].pattern_mask = mask;
            (*color_polygons)[color_quad_offset + 1U].pattern_mask = mask;
            color_quad_offset += 2U;
        }
        for (usize texture = 0U; texture < texture_count; ++texture) {
            const auto& group = patterns[pattern][texture + 1U];
            for (usize index = 0U; index < group.triangles; ++index) {
                if (textured_triangle_offset >= textured_triangles) {
                    return malformed("MBAC pattern textured triangle counts overflow");
                }
                auto& polygon = (*textured_polygons)[textured_triangle_offset++];
                polygon.pattern_mask = mask;
                polygon.texture_index = static_cast<i32>(texture);
            }
            for (usize index = 0U; index < group.quads; ++index) {
                if (textured_quad_offset + 1U >= textured_polygons->size()) {
                    return malformed("MBAC pattern textured quad counts overflow");
                }
                auto& first = (*textured_polygons)[textured_quad_offset];
                auto& second = (*textured_polygons)[textured_quad_offset + 1U];
                first.pattern_mask = mask;
                second.pattern_mask = mask;
                first.texture_index = static_cast<i32>(texture);
                second.texture_index = static_cast<i32>(texture);
                textured_quad_offset += 2U;
            }
        }
    }
    if (color_triangle_offset != color_triangles ||
        color_quad_offset != color_polygons->size() ||
        textured_triangle_offset != textured_triangles ||
        textured_quad_offset != textured_polygons->size()) {
        return malformed("MBAC pattern counts do not cover all polygons");
    }

    std::vector<Bone> bones;
    bones.reserve(bone_count);
    usize assigned_vertices = 0U;
    for (usize bone = 0U; bone < bone_count; ++bone) {
        Bone value;
        value.vertex_count = cursor.read_u16("MBAC bone vertex count");
        value.parent = cursor.read_i16("MBAC bone parent");
        if (value.parent < -1 ||
            (value.parent >= 0 && static_cast<usize>(value.parent) >= bone)) {
            return malformed("invalid MBAC bone parent");
        }
        for (usize index = 0U; index < 12U; ++index) {
            const float factor = index == 3U || index == 7U || index == 11U
                ? 1.0F : kFixedScale;
            value.transform.values[index] =
                static_cast<float>(cursor.read_i16("MBAC bone matrix")) * factor;
        }
        if (value.vertex_count > vertex_count - assigned_vertices) {
            return malformed("MBAC bone vertex ranges overflow");
        }
        assigned_vertices += value.vertex_count;
        bones.push_back(value);
    }
    if (!cursor.ok()) return malformed(cursor.error());
    if (assigned_vertices != vertex_count) {
        return malformed("MBAC bone ranges do not cover all vertices");
    }

    auto model = std::make_shared<Model>();
    model->vertices = std::move(*vertices);
    model->normals = std::move(*normals);
    model->bones = std::move(bones);
    model->patterns = static_cast<i32>(pattern_count);
    model->textures = static_cast<i32>(texture_count);
    model->polygons.reserve(color_polygons->size() + textured_polygons->size());
    model->polygons.insert(model->polygons.end(),
                           textured_polygons->begin(), textured_polygons->end());
    model->polygons.insert(model->polygons.end(),
                           color_polygons->begin(), color_polygons->end());
    return std::shared_ptr<const Model>(std::move(model));
}

Result<std::shared_ptr<const ActionTable>> decode_actions(
    std::span<const u8> bytes) {
    Cursor cursor(bytes);
    if (cursor.read_u8("MTRA signature") != static_cast<u8>('M') ||
        cursor.read_u8("MTRA signature") != static_cast<u8>('T')) {
        return malformed("not an MTRA resource");
    }
    const i32 version = static_cast<i32>(cursor.read_u8("MTRA version"));
    if (cursor.read_u8("MTRA version terminator") != 0U ||
        version < 2 || version > 5) {
        return malformed("unsupported MTRA version");
    }
    const usize action_count = cursor.read_u16("MTRA action count");
    const usize bone_count = cursor.read_u16("MTRA bone count");
    if (action_count > kMaximumActions || bone_count > kMaximumBones ||
        !valid_count_product(action_count, bone_count, kMaximumKeys)) {
        return malformed("MTRA declared counts exceed safety limits");
    }
    for (usize index = 0U; index < 8U; ++index) {
        static_cast<void>(cursor.read_u16("MTRA transform type count"));
    }
    static_cast<void>(cursor.read_u32("MTRA data size"));
    auto table = std::make_shared<ActionTable>();
    table->actions.reserve(action_count);
    for (usize action_index = 0U; action_index < action_count; ++action_index) {
        Action action;
        action.keyframes = static_cast<i32>(cursor.read_u16("MTRA keyframe count"));
        action.bones.reserve(bone_count);
        for (usize bone = 0U; bone < bone_count; ++bone) {
            auto value = read_bone_action(cursor);
            if (!value) return std::unexpected(value.error());
            action.bones.push_back(std::move(*value));
        }
        if (version >= 5) {
            const usize count = cursor.read_u16("MTRA dynamic pattern count");
            if (count > kMaximumKeys) {
                return malformed("too many MTRA dynamic pattern keys");
            }
            action.dynamic_patterns.reserve(count);
            for (usize index = 0U; index < count; ++index) {
                action.dynamic_patterns.push_back({
                    static_cast<i32>(cursor.read_u16("MTRA dynamic pattern frame")),
                    cursor.read_i32("MTRA dynamic pattern value"),
                });
            }
        }
        table->actions.push_back(std::move(action));
    }
    if (!cursor.ok()) return malformed(cursor.error());
    return std::shared_ptr<const ActionTable>(std::move(table));
}

Result<std::shared_ptr<const Texture>> decode_texture(
    std::span<const u8> bytes) {
    if (bytes.size() < 26U || bytes[0] != static_cast<u8>('B') ||
        bytes[1] != static_cast<u8>('M')) {
        return malformed("not an indexed BMP texture");
    }
    const auto read_u16_at = [&](usize offset) -> std::optional<u16> {
        if (offset + 2U > bytes.size()) return std::nullopt;
        return static_cast<u16>(bytes[offset]) |
            static_cast<u16>(static_cast<u16>(bytes[offset + 1U]) << 8U);
    };
    const auto read_u32_at = [&](usize offset) -> std::optional<u32> {
        if (offset + 4U > bytes.size()) return std::nullopt;
        return static_cast<u32>(bytes[offset]) |
            (static_cast<u32>(bytes[offset + 1U]) << 8U) |
            (static_cast<u32>(bytes[offset + 2U]) << 16U) |
            (static_cast<u32>(bytes[offset + 3U]) << 24U);
    };
    auto raster_value = read_u32_at(10U);
    auto dib_value = read_u32_at(14U);
    if (!raster_value || !dib_value) return malformed("truncated BMP header");
    usize raster_offset = static_cast<usize>(*raster_value);
    const usize dib_size = static_cast<usize>(*dib_value);
    i32 width = 0;
    i32 height = 0;
    usize colors = 256U;
    bool bottom_up = true;
    usize palette_entry_size = 4U;
    if (dib_size == 12U) {
        auto width_value = read_u16_at(18U);
        auto height_value = read_u16_at(20U);
        auto bpp = read_u16_at(24U);
        if (!width_value || !height_value || !bpp || *bpp != 8U) {
            return malformed("unsupported OS/2 BMP texture");
        }
        width = static_cast<i32>(*width_value);
        height = static_cast<i32>(*height_value);
        palette_entry_size = 3U;
    } else if (dib_size >= 40U) {
        auto width_value = read_u32_at(18U);
        auto height_value = read_u32_at(22U);
        auto bpp = read_u16_at(28U);
        auto compression = read_u32_at(30U);
        auto colors_value = read_u32_at(46U);
        if (!width_value || !height_value || !bpp || !compression ||
            !colors_value || *bpp != 8U || *compression != 0U) {
            return malformed("unsupported Windows BMP texture");
        }
        width = static_cast<i32>(*width_value);
        const i32 signed_height = static_cast<i32>(*height_value);
        bottom_up = signed_height >= 0;
        if (signed_height == std::numeric_limits<i32>::min()) {
            return malformed("invalid BMP texture height");
        }
        height = std::abs(signed_height);
        colors = *colors_value == 0U ? 256U
                                    : static_cast<usize>(*colors_value);
    } else {
        return malformed("unsupported BMP DIB header");
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        colors == 0U || colors > 256U) {
        return malformed("BMP texture dimensions or palette are invalid");
    }
    const usize palette_offset = 14U + dib_size;
    if (!valid_count_product(colors, palette_entry_size, bytes.size()) ||
        palette_offset > bytes.size() - colors * palette_entry_size) {
        return malformed("truncated BMP palette");
    }
    const usize minimum_raster = palette_offset + colors * palette_entry_size;
    raster_offset = std::max(raster_offset, minimum_raster);
    const usize row_width = static_cast<usize>(width);
    const usize stride = (row_width + 3U) & ~usize {3U};
    const usize row_count = static_cast<usize>(height);
    if (!valid_count_product(stride, row_count, bytes.size()) ||
        raster_offset > bytes.size() - stride * row_count) {
        return malformed("truncated BMP raster");
    }
    auto texture = std::make_shared<Texture>();
    texture->width = width;
    texture->height = height;
    texture->pixels.resize(row_width * row_count);
    for (usize y = 0U; y < row_count; ++y) {
        const usize source_y = bottom_up ? row_count - 1U - y : y;
        const usize source = raster_offset + source_y * stride;
        for (usize x = 0U; x < row_width; ++x) {
            const usize palette_index = static_cast<usize>(bytes[source + x]);
            if (palette_index >= colors) {
                return malformed("BMP raster references an invalid palette entry");
            }
            const usize entry = palette_offset + palette_index * palette_entry_size;
            const u8 blue = bytes[entry];
            const u8 green = bytes[entry + 1U];
            const u8 red = bytes[entry + 2U];
            texture->pixels[y * row_width + x] = graphics::argb(
                palette_index == 0U ? 0U : 255U, red, green, blue);
        }
    }
    return std::shared_ptr<const Texture>(std::move(texture));
}

Status cache_model(Machine& machine, ObjectRef object,
                   std::span<const u8> bytes) {
    auto decoded = decode_model(bytes);
    if (!decoded) return std::unexpected(decoded.error());
    std::scoped_lock lock(g_cache_mutex);
    g_models[ResourceKey {&machine, object.bits}] = std::move(*decoded);
    return {};
}

Status cache_actions(Machine& machine, ObjectRef object,
                     std::span<const u8> bytes) {
    auto decoded = decode_actions(bytes);
    if (!decoded) return std::unexpected(decoded.error());
    std::scoped_lock lock(g_cache_mutex);
    g_actions[ResourceKey {&machine, object.bits}] = std::move(*decoded);
    return {};
}

Status cache_texture(Machine& machine, ObjectRef object,
                     std::span<const u8> bytes) {
    auto decoded = decode_texture(bytes);
    if (!decoded) return std::unexpected(decoded.error());
    std::scoped_lock lock(g_cache_mutex);
    g_textures[ResourceKey {&machine, object.bits}] = std::move(*decoded);
    return {};
}

Result<std::shared_ptr<const Model>> cached_model(Machine& machine,
                                                  ObjectRef object) {
    std::scoped_lock lock(g_cache_mutex);
    const auto found = g_models.find(ResourceKey {&machine, object.bits});
    if (found == g_models.end()) {
        return fail(ErrorCode::invalid_state, "Micro3D model cache is missing");
    }
    return found->second;
}

Result<std::shared_ptr<const ActionTable>> cached_actions(Machine& machine,
                                                          ObjectRef object) {
    std::scoped_lock lock(g_cache_mutex);
    const auto found = g_actions.find(ResourceKey {&machine, object.bits});
    if (found == g_actions.end()) {
        return fail(ErrorCode::invalid_state, "Micro3D action cache is missing");
    }
    return found->second;
}

Result<std::shared_ptr<const Texture>> cached_texture(Machine& machine,
                                                      ObjectRef object) {
    std::scoped_lock lock(g_cache_mutex);
    const auto found = g_textures.find(ResourceKey {&machine, object.bits});
    if (found == g_textures.end()) {
        return fail(ErrorCode::invalid_state, "Micro3D texture cache is missing");
    }
    return found->second;
}

void erase_resource(Machine& machine, ObjectRef object) noexcept {
    std::scoped_lock lock(g_cache_mutex);
    const ResourceKey key {&machine, object.bits};
    g_models.erase(key);
    g_actions.erase(key);
    g_textures.erase(key);
}

Result<Pose> pose_model(const Model& model,
                        const ActionTable* actions,
                        i32 action,
                        i32 frame) {
    Pose pose {.vertices = model.vertices, .normals = model.normals};
    if (model.bones.empty()) return pose;
    const Action* selected = nullptr;
    if (actions != nullptr) {
        if (action < 0 || static_cast<usize>(action) >= actions->actions.size()) {
            return fail_java("java/lang/IllegalArgumentException",
                             "invalid Micro3D action index");
        }
        selected = &actions->actions[static_cast<usize>(action)];
        if (selected->bones.size() != model.bones.size()) {
            return fail_java("java/lang/IllegalArgumentException",
                             "MTRA bone count does not match MBAC model");
        }
    }
    std::vector<Mat34> transforms(model.bones.size());
    for (usize index = 0U; index < model.bones.size(); ++index) {
        const Bone& bone = model.bones[index];
        Mat34 combined = bone.transform;
        if (bone.parent >= 0) {
            combined = multiply(transforms[static_cast<usize>(bone.parent)],
                                combined);
        }
        if (selected != nullptr) {
            combined = multiply(combined, evaluate(selected->bones[index], frame));
        }
        transforms[index] = combined;
    }
    usize vertex = 0U;
    for (usize bone_index = 0U; bone_index < model.bones.size(); ++bone_index) {
        const Bone& bone = model.bones[bone_index];
        const Mat34& transform = transforms[bone_index];
        for (usize index = 0U; index < bone.vertex_count; ++index, ++vertex) {
            if (vertex >= pose.vertices.size()) {
                return fail(ErrorCode::invalid_state,
                            "Micro3D bone ranges exceed model vertices");
            }
            pose.vertices[vertex] = transform_point(transform, model.vertices[vertex]);
            if (vertex < pose.normals.size()) {
                pose.normals[vertex] = transform_normal(transform,
                                                        model.normals[vertex]);
            }
        }
    }
    return pose;
}

i32 dynamic_pattern(const ActionTable& actions,
                    i32 action,
                    i32 frame,
                    i32 fallback) noexcept {
    if (action < 0 || static_cast<usize>(action) >= actions.actions.size()) {
        return fallback;
    }
    const i32 integral_frame = std::max(frame, 0) >> 16;
    i32 result = fallback;
    for (const DynamicPattern& key :
         actions.actions[static_cast<usize>(action)].dynamic_patterns) {
        if (key.frame > integral_frame) break;
        result = key.pattern;
    }
    return result;
}

} // namespace phoneme::vm::micro3d::software
