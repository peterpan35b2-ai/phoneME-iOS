#include "M3gLoader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

#include "M3gNativeSupport.hpp"
#include "phoneme/graphics/Image.hpp"
#include "phoneme/vm/NativeRootScope.hpp"

namespace phoneme::vm::m3g {
namespace {

constexpr std::array<u8, 12> kM3gSignature {
    0xABU, 0x4AU, 0x53U, 0x52U, 0x31U, 0x38U,
    0x34U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
};
constexpr usize kMaximumM3gBytes = 64U * 1024U * 1024U;

[[nodiscard]] std::string utf8_from_utf16(std::u16string_view text) {
    std::string output;
    output.reserve(text.size());
    for (usize index = 0U; index < text.size(); ++index) {
        u32 code_point = static_cast<u16>(text[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < text.size()) {
            const u32 low = static_cast<u16>(text[index + 1U]);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                             (low - 0xDC00U);
                ++index;
            }
        }
        if (code_point <= 0x7FU) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(
                0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            output.push_back(static_cast<char>(
                0x80U | ((code_point >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(
                0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
    }
    return output;
}

class Cursor final {
public:
    explicit Cursor(std::span<const u8> bytes) : bytes_(bytes) {}

    [[nodiscard]] usize position() const noexcept { return position_; }
    [[nodiscard]] usize remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] Result<u8> read_u8(std::string_view what) {
        if (remaining() < 1U) return std::unexpected(truncated(what));
        return bytes_[position_++];
    }

    [[nodiscard]] Result<u16> read_u16(std::string_view what) {
        if (remaining() < 2U) return std::unexpected(truncated(what));
        const u16 result = static_cast<u16>(bytes_[position_]) |
            static_cast<u16>(static_cast<u16>(bytes_[position_ + 1U]) << 8U);
        position_ += 2U;
        return result;
    }

    [[nodiscard]] Result<u32> read_u32(std::string_view what) {
        if (remaining() < 4U) return std::unexpected(truncated(what));
        const u32 result = static_cast<u32>(bytes_[position_]) |
            (static_cast<u32>(bytes_[position_ + 1U]) << 8U) |
            (static_cast<u32>(bytes_[position_ + 2U]) << 16U) |
            (static_cast<u32>(bytes_[position_ + 3U]) << 24U);
        position_ += 4U;
        return result;
    }

    [[nodiscard]] Result<float> read_float(std::string_view what) {
        auto bits = read_u32(what);
        if (!bits) return std::unexpected(bits.error());
        return std::bit_cast<float>(*bits);
    }

    [[nodiscard]] Result<std::span<const u8>> read_bytes(
        usize count,
        std::string_view what) {
        if (count > remaining()) return std::unexpected(truncated(what));
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    [[nodiscard]] Status skip(usize count, std::string_view what) {
        auto bytes = read_bytes(count, what);
        if (!bytes) return std::unexpected(bytes.error());
        return {};
    }

private:
    [[nodiscard]] static Error truncated(std::string_view what) {
        return Error::make(ErrorCode::malformed_class,
                           "truncated M3G " + std::string(what));
    }

    std::span<const u8> bytes_;
    usize position_ {0};
};

struct ParsedSkinTransform final {
    u32 bone {0U};
    u32 first_vertex {0U};
    u32 vertex_count {0U};
    i32 weight {0};
};

struct ParsedObject final {
    u8 type {0};
    std::vector<u8> payload;
    u32 user_id {0};
    std::vector<std::pair<u32, std::vector<u8>>> user_parameters;
    std::vector<u32> animation_tracks;
    u32 animation_sequence {0};
    u32 animation_controller {0};
    i32 animation_property {0};
    i32 animation_active_start {std::numeric_limits<i32>::min()};
    i32 animation_active_end {std::numeric_limits<i32>::max()};
    i32 animation_reference_world_time {0};
    float animation_speed {1.0F};
    float animation_weight {1.0F};
    float animation_reference_sequence_time {0.0F};
    i32 keyframe_count {0};
    i32 keyframe_component_count {0};
    i32 keyframe_interpolation {176};
    i32 keyframe_repeat_mode {192};
    i32 keyframe_duration {0};
    i32 keyframe_valid_first {0};
    i32 keyframe_valid_last {0};
    std::vector<i32> keyframe_times;
    std::vector<float> keyframe_values;
    std::vector<u32> references;
    std::vector<u32> children;
    std::vector<std::pair<u32, u32>> submeshes;
    std::vector<u32> textures;
    u32 vertex_buffer {0};
    u32 image {0};
    u32 appearance {0};
    u32 compositing_mode {0};
    u32 fog {0};
    u32 polygon_mode {0};
    u32 material {0};
    u32 active_camera {0};
    u32 background {0};
    u32 skeleton {0};
    std::vector<u32> morph_targets;
    std::vector<float> morph_weights;
    std::vector<ParsedSkinTransform> skin_transforms;
    u32 positions {0};
    u32 normals {0};
    u32 colors {0};
    std::vector<u32> texcoords;
    std::array<float, 3> translation {0.0F, 0.0F, 0.0F};
    std::array<float, 3> scale {1.0F, 1.0F, 1.0F};
    std::array<float, 4> orientation {0.0F, 0.0F, 0.0F, 1.0F};
    Matrix generic_transform {identity_matrix()};
    i32 projection_type {0};
    std::vector<float> projection;
    i32 layer {0};
    i32 color {0};
    i32 secondary_color {0};
    i32 tertiary_color {0};
    i32 quaternary_color {0};
    i32 mode {0};
    i32 secondary_mode {0};
    i32 tertiary_mode {0};
    i32 quaternary_mode {0};
    float scalar {0.0F};
    float secondary_scalar {0.0F};
    float tertiary_scalar {0.0F};
    std::array<float, 3> vector3 {0.0F, 0.0F, 0.0F};
    bool flag {false};
    bool secondary_flag {false};
    bool tertiary_flag {false};
    bool quaternary_flag {false};
    i32 crop_x {0};
    i32 crop_y {0};
    i32 crop_width {0};
    i32 crop_height {0};
    std::vector<i32> indices;
    std::vector<i32> strip_lengths;
    i32 vertex_count {0};
    i32 component_count {0};
    i32 component_size {0};
    std::vector<i32> vertex_values;
    i32 default_color {static_cast<i32>(0xFFFFFFFFU)};
    float position_scale {1.0F};
    std::array<float, 3> position_bias {0.0F, 0.0F, 0.0F};
    std::vector<float> texcoord_scales;
    std::vector<std::array<float, 3>> texcoord_biases;
    u8 image_format {99U};
    u32 width {1U};
    u32 height {1U};
    bool mutable_image {false};
    std::vector<u8> palette;
    std::vector<u8> pixels;
    bool scaled {false};
    bool rendering_enabled {true};
    bool picking_enabled {true};
    float alpha_factor {1.0F};
    i32 scope {-1};
    std::string external_uri;
    ObjectRef java_object {};
};

[[nodiscard]] Status io_failure(std::string message) {
    return fail_java("java/io/IOException", std::move(message));
}

[[nodiscard]] Result<std::vector<u8>> decompress_section(
    u8 compression,
    std::span<const u8> payload,
    usize uncompressed_length) {
    if (uncompressed_length > kMaximumM3gBytes) {
        return fail_java("java/io/IOException",
                         "M3G section exceeds the 64 MiB safety limit");
    }
    if (compression == 0U) {
        if (payload.size() != uncompressed_length) {
            return fail_java("java/io/IOException",
                             "M3G uncompressed section length is inconsistent");
        }
        return std::vector<u8>(payload.begin(), payload.end());
    }
    if (compression != 1U) {
        return fail_java("java/io/IOException",
                         "unsupported M3G compression scheme");
    }
    std::vector<u8> output(uncompressed_length);
    uLongf output_length = static_cast<uLongf>(output.size());
    const int status = ::uncompress(
        reinterpret_cast<Bytef*>(output.data()), &output_length,
        reinterpret_cast<const Bytef*>(payload.data()),
        static_cast<uLong>(payload.size()));
    if (status != Z_OK || output_length != output.size()) {
        return fail_java("java/io/IOException",
                         "M3G zlib section could not be decompressed");
    }
    return output;
}

[[nodiscard]] Status parse_sections(
    std::span<const u8> file,
    std::vector<ParsedObject>& objects) {
    if (file.size() < kM3gSignature.size() ||
        !std::equal(kM3gSignature.begin(), kM3gSignature.end(),
                    file.begin())) {
        return io_failure("invalid M3G file signature");
    }

    usize offset = kM3gSignature.size();
    usize total_uncompressed = 0U;
    while (offset < file.size()) {
        if (file.size() - offset < 13U) {
            return io_failure("truncated M3G section header");
        }
        Cursor header(file.subspan(offset));
        auto compression = header.read_u8("compression scheme");
        auto total_length = header.read_u32("section length");
        auto uncompressed_length = header.read_u32("uncompressed length");
        if (!compression) return std::unexpected(compression.error());
        if (!total_length) return std::unexpected(total_length.error());
        if (!uncompressed_length) {
            return std::unexpected(uncompressed_length.error());
        }
        if (*total_length < 13U || *total_length > file.size() - offset) {
            return io_failure("M3G section length is outside the file");
        }
        const usize payload_length = static_cast<usize>(*total_length) - 13U;
        const auto payload = file.subspan(offset + 9U, payload_length);
        auto decompressed = decompress_section(
            *compression, payload, static_cast<usize>(*uncompressed_length));
        if (!decompressed) return std::unexpected(decompressed.error());

        const usize crc_offset = offset + static_cast<usize>(*total_length) - 4U;
        const u32 expected_crc = static_cast<u32>(file[crc_offset]) |
            (static_cast<u32>(file[crc_offset + 1U]) << 8U) |
            (static_cast<u32>(file[crc_offset + 2U]) << 16U) |
            (static_cast<u32>(file[crc_offset + 3U]) << 24U);
        uLong actual_checksum = ::adler32(0L, Z_NULL, 0);
        actual_checksum = ::adler32(
            actual_checksum,
            reinterpret_cast<const Bytef*>(file.data() + offset),
            static_cast<uInt>(static_cast<usize>(*total_length) - 4U));
        if (static_cast<u32>(actual_checksum) != expected_crc) {
            return io_failure("M3G section checksum mismatch");
        }
        if (decompressed->size() > kMaximumM3gBytes - total_uncompressed) {
            return io_failure("M3G aggregate data exceeds the safety limit");
        }
        total_uncompressed += decompressed->size();

        Cursor section(*decompressed);
        while (section.remaining() != 0U) {
            auto type = section.read_u8("object type");
            auto length = section.read_u32("object length");
            if (!type) return std::unexpected(type.error());
            if (!length) return std::unexpected(length.error());
            auto payload_bytes = section.read_bytes(
                static_cast<usize>(*length), "object payload");
            if (!payload_bytes) return std::unexpected(payload_bytes.error());
            objects.push_back(ParsedObject {
                .type = *type,
                .payload = std::vector<u8>(payload_bytes->begin(),
                                           payload_bytes->end()),
            });
        }
        offset += static_cast<usize>(*total_length);
    }
    if (objects.empty() || objects.front().type != 0U) {
        return io_failure("M3G header object is missing");
    }
    return {};
}

[[nodiscard]] Status add_reference(ParsedObject& object, u32 reference) {
    if (reference != 0U) object.references.push_back(reference);
    return {};
}

[[nodiscard]] Status parse_object3d(Cursor& cursor, ParsedObject& object) {
    auto user_id = cursor.read_u32("Object3D user ID");
    auto track_count = cursor.read_u32("Object3D animation track count");
    if (!user_id) return std::unexpected(user_id.error());
    if (!track_count) return std::unexpected(track_count.error());
    object.user_id = *user_id;
    if (*track_count > 1'000'000U) {
        return io_failure("M3G animation track count is unreasonable");
    }
    for (u32 index = 0; index < *track_count; ++index) {
        auto reference = cursor.read_u32("Object3D animation track reference");
        if (!reference) return std::unexpected(reference.error());
        object.animation_tracks.push_back(*reference);
        (void)add_reference(object, *reference);
    }
    auto parameter_count = cursor.read_u32("Object3D user parameter count");
    if (!parameter_count) return std::unexpected(parameter_count.error());
    if (*parameter_count > 1'000'000U) {
        return io_failure("M3G user parameter count is unreasonable");
    }
    std::unordered_set<u32> parameter_ids;
    for (u32 index = 0; index < *parameter_count; ++index) {
        auto id = cursor.read_u32("Object3D user parameter ID");
        auto length = cursor.read_u32("Object3D user parameter length");
        if (!id) return std::unexpected(id.error());
        if (!length) return std::unexpected(length.error());
        if (!parameter_ids.insert(*id).second) {
            return io_failure("duplicate Object3D user parameter ID");
        }
        auto bytes = cursor.read_bytes(
            static_cast<usize>(*length), "Object3D user parameter data");
        if (!bytes) return std::unexpected(bytes.error());
        object.user_parameters.emplace_back(
            *id, std::vector<u8>(bytes->begin(), bytes->end()));
    }
    return {};
}

[[nodiscard]] Status parse_transformable(Cursor& cursor,
                                         ParsedObject& object) {
    auto base = parse_object3d(cursor, object);
    if (!base) return base;
    auto has_components = cursor.read_u8("Transformable component flag");
    if (!has_components) return std::unexpected(has_components.error());
    if (*has_components > 1U) {
        return io_failure("invalid Transformable component flag");
    }
    if (*has_components != 0U) {
        for (float& value : object.translation) {
            auto parsed = cursor.read_float("Transformable translation");
            if (!parsed) return std::unexpected(parsed.error());
            value = *parsed;
        }
        for (float& value : object.scale) {
            auto parsed = cursor.read_float("Transformable scale");
            if (!parsed) return std::unexpected(parsed.error());
            value = *parsed;
        }
        for (float& value : object.orientation) {
            auto parsed = cursor.read_float("Transformable orientation");
            if (!parsed) return std::unexpected(parsed.error());
            value = *parsed;
        }
        auto orientation = axis_angle_quaternion(
            object.orientation[0U], object.orientation[1U],
            object.orientation[2U], object.orientation[3U]);
        if (!orientation) return std::unexpected(orientation.error());
    }
    auto has_matrix = cursor.read_u8("Transformable matrix flag");
    if (!has_matrix) return std::unexpected(has_matrix.error());
    if (*has_matrix > 1U) {
        return io_failure("invalid Transformable matrix flag");
    }
    if (*has_matrix != 0U) {
        for (float& value : object.generic_transform) {
            auto parsed = cursor.read_float("Transformable matrix");
            if (!parsed) return std::unexpected(parsed.error());
            value = *parsed;
        }
    }
    return {};
}

[[nodiscard]] Status parse_node(Cursor& cursor, ParsedObject& object) {
    auto base = parse_transformable(cursor, object);
    if (!base) return base;
    auto rendering = cursor.read_u8("Node rendering flag");
    auto picking = cursor.read_u8("Node picking flag");
    auto alpha = cursor.read_u8("Node alpha factor");
    auto scope = cursor.read_u32("Node scope");
    auto has_alignment = cursor.read_u8("Node alignment flag");
    if (!rendering) return std::unexpected(rendering.error());
    if (!picking) return std::unexpected(picking.error());
    if (!alpha) return std::unexpected(alpha.error());
    if (!scope) return std::unexpected(scope.error());
    if (!has_alignment) return std::unexpected(has_alignment.error());
    object.rendering_enabled = *rendering != 0U;
    object.picking_enabled = *picking != 0U;
    object.alpha_factor = static_cast<float>(*alpha) / 255.0F;
    object.scope = static_cast<i32>(*scope);
    if (*has_alignment != 0U) {
        auto z_target = cursor.read_u32("Node z alignment target");
        auto y_target = cursor.read_u32("Node y alignment target");
        auto z_reference = cursor.read_u8("Node z alignment reference");
        auto y_reference = cursor.read_u8("Node y alignment reference");
        if (!z_target) return std::unexpected(z_target.error());
        if (!y_target) return std::unexpected(y_target.error());
        if (!z_reference) return std::unexpected(z_reference.error());
        if (!y_reference) return std::unexpected(y_reference.error());
        (void)add_reference(object, *z_target);
        (void)add_reference(object, *y_target);
    }
    return {};
}

[[nodiscard]] Status parse_group(Cursor& cursor, ParsedObject& object) {
    auto base = parse_node(cursor, object);
    if (!base) return base;
    auto child_count = cursor.read_u32("Group child count");
    if (!child_count) return std::unexpected(child_count.error());
    if (*child_count > 1'000'000U) {
        return io_failure("M3G Group child count is unreasonable");
    }
    for (u32 index = 0; index < *child_count; ++index) {
        auto child = cursor.read_u32("Group child reference");
        if (!child) return std::unexpected(child.error());
        object.children.push_back(*child);
        (void)add_reference(object, *child);
    }
    return {};
}

[[nodiscard]] Status parse_mesh(Cursor& cursor, ParsedObject& object) {
    auto base = parse_node(cursor, object);
    if (!base) return base;
    auto vertex_buffer = cursor.read_u32("Mesh vertex buffer reference");
    auto submesh_count = cursor.read_u32("Mesh submesh count");
    if (!vertex_buffer) return std::unexpected(vertex_buffer.error());
    if (!submesh_count) return std::unexpected(submesh_count.error());
    object.vertex_buffer = *vertex_buffer;
    (void)add_reference(object, *vertex_buffer);
    if (*submesh_count > 1'000'000U) {
        return io_failure("M3G Mesh submesh count is unreasonable");
    }
    for (u32 index = 0; index < *submesh_count; ++index) {
        auto index_buffer = cursor.read_u32("Mesh index buffer reference");
        auto appearance = cursor.read_u32("Mesh appearance reference");
        if (!index_buffer) return std::unexpected(index_buffer.error());
        if (!appearance) return std::unexpected(appearance.error());
        object.submeshes.emplace_back(*index_buffer, *appearance);
        (void)add_reference(object, *index_buffer);
        (void)add_reference(object, *appearance);
    }
    return {};
}

[[nodiscard]] Result<i32> read_rgb(Cursor& cursor,
                                   std::string_view operation) {
    auto bytes = cursor.read_bytes(3U, operation);
    if (!bytes) return std::unexpected(bytes.error());
    const u32 value = (static_cast<u32>((*bytes)[0U]) << 16U) |
                      (static_cast<u32>((*bytes)[1U]) << 8U) |
                      static_cast<u32>((*bytes)[2U]);
    return static_cast<i32>(value);
}

[[nodiscard]] Result<i32> read_argb(Cursor& cursor,
                                    std::string_view operation) {
    auto bytes = cursor.read_bytes(4U, operation);
    if (!bytes) return std::unexpected(bytes.error());
    const u32 value = (static_cast<u32>((*bytes)[3U]) << 24U) |
                      (static_cast<u32>((*bytes)[0U]) << 16U) |
                      (static_cast<u32>((*bytes)[1U]) << 8U) |
                      static_cast<u32>((*bytes)[2U]);
    return static_cast<i32>(value);
}

[[nodiscard]] Status parse_triangle_strip(Cursor& cursor,
                                          ParsedObject& object) {
    auto base = parse_object3d(cursor, object);
    if (!base) return base;
    auto encoding = cursor.read_u8("TriangleStripArray encoding");
    if (!encoding) return std::unexpected(encoding.error());

    i32 first_index = 0;
    bool implicit = false;
    switch (*encoding) {
    case 0U: {
        auto value = cursor.read_u32("TriangleStripArray first index");
        if (!value) return std::unexpected(value.error());
        first_index = static_cast<i32>(*value);
        implicit = true;
        break;
    }
    case 1U: {
        auto value = cursor.read_u8("TriangleStripArray first index");
        if (!value) return std::unexpected(value.error());
        first_index = static_cast<i32>(*value);
        implicit = true;
        break;
    }
    case 2U: {
        auto value = cursor.read_u16("TriangleStripArray first index");
        if (!value) return std::unexpected(value.error());
        first_index = static_cast<i32>(*value);
        implicit = true;
        break;
    }
    case 128U:
    case 129U:
    case 130U: {
        auto count = cursor.read_u32("TriangleStripArray index count");
        if (!count) return std::unexpected(count.error());
        if (*count > 16'000'000U) {
            return io_failure("TriangleStripArray index count is unreasonable");
        }
        object.indices.reserve(static_cast<usize>(*count));
        for (u32 index = 0U; index < *count; ++index) {
            if (*encoding == 128U) {
                auto value = cursor.read_u32("TriangleStripArray index");
                if (!value) return std::unexpected(value.error());
                object.indices.push_back(static_cast<i32>(*value));
            } else if (*encoding == 129U) {
                auto value = cursor.read_u8("TriangleStripArray index");
                if (!value) return std::unexpected(value.error());
                object.indices.push_back(static_cast<i32>(*value));
            } else {
                auto value = cursor.read_u16("TriangleStripArray index");
                if (!value) return std::unexpected(value.error());
                object.indices.push_back(static_cast<i32>(*value));
            }
        }
        break;
    }
    default:
        return io_failure("unsupported TriangleStripArray encoding");
    }

    auto strip_count = cursor.read_u32("TriangleStripArray strip count");
    if (!strip_count) return std::unexpected(strip_count.error());
    if (*strip_count == 0U || *strip_count > 4'000'000U) {
        return io_failure("TriangleStripArray strip count is invalid");
    }
    usize total_indices = 0U;
    object.strip_lengths.reserve(static_cast<usize>(*strip_count));
    for (u32 index = 0U; index < *strip_count; ++index) {
        auto length = cursor.read_u32("TriangleStripArray strip length");
        if (!length) return std::unexpected(length.error());
        if (*length < 3U || *length > 16'000'000U - total_indices) {
            return io_failure("TriangleStripArray strip length is invalid");
        }
        object.strip_lengths.push_back(static_cast<i32>(*length));
        total_indices += static_cast<usize>(*length);
    }
    if (implicit) {
        object.indices.reserve(total_indices);
        for (usize index = 0U; index < total_indices; ++index) {
            if (index > static_cast<usize>(std::numeric_limits<i32>::max()) ||
                first_index > std::numeric_limits<i32>::max() -
                    static_cast<i32>(index)) {
                return io_failure("TriangleStripArray implicit indices overflow");
            }
            object.indices.push_back(first_index + static_cast<i32>(index));
        }
    } else if (object.indices.size() != total_indices) {
        return io_failure("TriangleStripArray strip lengths do not match indices");
    }
    return {};
}

[[nodiscard]] Status parse_vertex_array(Cursor& cursor,
                                        ParsedObject& object) {
    auto base = parse_object3d(cursor, object);
    if (!base) return base;
    auto component_size = cursor.read_u8("VertexArray component size");
    auto component_count = cursor.read_u8("VertexArray component count");
    auto encoding = cursor.read_u8("VertexArray encoding");
    auto vertex_count = cursor.read_u16("VertexArray vertex count");
    if (!component_size) return std::unexpected(component_size.error());
    if (!component_count) return std::unexpected(component_count.error());
    if (!encoding) return std::unexpected(encoding.error());
    if (!vertex_count) return std::unexpected(vertex_count.error());
    if ((*component_size != 1U && *component_size != 2U) ||
        *component_count < 2U || *component_count > 4U ||
        *vertex_count == 0U || *encoding > 1U) {
        return io_failure("VertexArray metadata is invalid");
    }
    object.component_size = static_cast<i32>(*component_size);
    object.component_count = static_cast<i32>(*component_count);
    object.vertex_count = static_cast<i32>(*vertex_count);
    const usize value_count = static_cast<usize>(*vertex_count) *
                              static_cast<usize>(*component_count);
    object.vertex_values.resize(value_count);
    std::array<i32, 4> previous {};
    for (usize vertex = 0U; vertex < static_cast<usize>(*vertex_count); ++vertex) {
        for (usize component = 0U;
             component < static_cast<usize>(*component_count); ++component) {
            i32 value = 0;
            if (*component_size == 1U) {
                auto raw = cursor.read_u8("VertexArray byte component");
                if (!raw) return std::unexpected(raw.error());
                value = static_cast<i32>(static_cast<std::int8_t>(*raw));
            } else {
                auto raw = cursor.read_u16("VertexArray short component");
                if (!raw) return std::unexpected(raw.error());
                value = static_cast<i32>(static_cast<std::int16_t>(*raw));
            }
            if (*encoding != 0U) {
                value = *component_size == 1U
                    ? static_cast<i32>(static_cast<std::int8_t>(
                        previous[component] + value))
                    : static_cast<i32>(static_cast<std::int16_t>(
                        previous[component] + value));
            }
            previous[component] = value;
            object.vertex_values[
                vertex * static_cast<usize>(*component_count) + component] = value;
        }
    }
    return {};
}

[[nodiscard]] Status parse_serialized_object(ParsedObject& object) {
    if (object.type == 0U) return {};
    if (object.type == 255U) {
        if (object.payload.size() < 2U || object.payload.back() != 0U) {
            return io_failure("M3G external reference URI is not terminated");
        }
        const auto terminator = std::find(object.payload.begin(),
                                          object.payload.end(), 0U);
        if (terminator != object.payload.end() - 1 ||
            terminator == object.payload.begin()) {
            return io_failure("M3G external reference URI is invalid");
        }
        object.external_uri.assign(
            reinterpret_cast<const char*>(object.payload.data()),
            object.payload.size() - 1U);
        return {};
    }
    Cursor cursor(object.payload);
    Status parsed;
    switch (object.type) {
    case 1U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto speed = cursor.read_float("AnimationController speed");
        auto weight = cursor.read_float("AnimationController weight");
        auto active_start = cursor.read_u32("AnimationController active start");
        auto active_end = cursor.read_u32("AnimationController active end");
        auto reference_sequence_time = cursor.read_float(
            "AnimationController reference sequence time");
        auto reference_world_time = cursor.read_u32(
            "AnimationController reference world time");
        if (!speed) return std::unexpected(speed.error());
        if (!weight) return std::unexpected(weight.error());
        if (!active_start) return std::unexpected(active_start.error());
        if (!active_end) return std::unexpected(active_end.error());
        if (!reference_sequence_time) {
            return std::unexpected(reference_sequence_time.error());
        }
        if (!reference_world_time) {
            return std::unexpected(reference_world_time.error());
        }
        if (!std::isfinite(*speed) || !std::isfinite(*weight) ||
            *weight < 0.0F || *weight > 1.0F ||
            !std::isfinite(*reference_sequence_time)) {
            return io_failure("AnimationController state is invalid");
        }
        object.animation_speed = *speed;
        object.animation_weight = *weight;
        object.animation_active_start = static_cast<i32>(*active_start);
        object.animation_active_end = static_cast<i32>(*active_end);
        object.animation_reference_sequence_time = *reference_sequence_time;
        object.animation_reference_world_time =
            static_cast<i32>(*reference_world_time);
        if (object.animation_active_start > object.animation_active_end) {
            return io_failure("AnimationController active interval is reversed");
        }
        break;
    }
    case 2U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto sequence = cursor.read_u32("AnimationTrack sequence reference");
        auto controller = cursor.read_u32("AnimationTrack controller reference");
        auto property = cursor.read_u32("AnimationTrack property");
        if (!sequence) return std::unexpected(sequence.error());
        if (!controller) return std::unexpected(controller.error());
        if (!property) return std::unexpected(property.error());
        object.animation_sequence = *sequence;
        object.animation_controller = *controller;
        object.animation_property = static_cast<i32>(*property);
        (void)add_reference(object, *sequence);
        (void)add_reference(object, *controller);
        break;
    }
    case 3U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto layer = cursor.read_u8("Appearance layer");
        auto compositing = cursor.read_u32("Appearance compositing reference");
        auto fog = cursor.read_u32("Appearance fog reference");
        auto polygon = cursor.read_u32("Appearance polygon reference");
        auto material = cursor.read_u32("Appearance material reference");
        auto count = cursor.read_u32("Appearance texture count");
        if (!layer) return std::unexpected(layer.error());
        if (!compositing) return std::unexpected(compositing.error());
        if (!fog) return std::unexpected(fog.error());
        if (!polygon) return std::unexpected(polygon.error());
        if (!material) return std::unexpected(material.error());
        if (!count) return std::unexpected(count.error());
        object.layer = static_cast<i32>(static_cast<std::int8_t>(*layer));
        object.compositing_mode = *compositing;
        object.fog = *fog;
        object.polygon_mode = *polygon;
        object.material = *material;
        for (u32 index = 0; index < *count; ++index) {
            auto texture = cursor.read_u32("Appearance texture reference");
            if (!texture) return std::unexpected(texture.error());
            object.textures.push_back(*texture);
            (void)add_reference(object, *texture);
        }
        (void)add_reference(object, *compositing);
        (void)add_reference(object, *fog);
        (void)add_reference(object, *polygon);
        (void)add_reference(object, *material);
        break;
    }
    case 4U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto color = read_argb(cursor, "Background color");
        auto image = cursor.read_u32("Background image reference");
        auto mode_x = cursor.read_u8("Background image mode X");
        auto mode_y = cursor.read_u8("Background image mode Y");
        auto crop_x = cursor.read_u32("Background crop X");
        auto crop_y = cursor.read_u32("Background crop Y");
        auto crop_width = cursor.read_u32("Background crop width");
        auto crop_height = cursor.read_u32("Background crop height");
        auto color_clear = cursor.read_u8("Background color clear flag");
        auto depth_clear = cursor.read_u8("Background depth clear flag");
        if (!color) return std::unexpected(color.error());
        if (!image) return std::unexpected(image.error());
        if (!mode_x) return std::unexpected(mode_x.error());
        if (!mode_y) return std::unexpected(mode_y.error());
        if (!crop_x) return std::unexpected(crop_x.error());
        if (!crop_y) return std::unexpected(crop_y.error());
        if (!crop_width) return std::unexpected(crop_width.error());
        if (!crop_height) return std::unexpected(crop_height.error());
        if (!color_clear) return std::unexpected(color_clear.error());
        if (!depth_clear) return std::unexpected(depth_clear.error());
        if (*color_clear > 1U || *depth_clear > 1U) {
            return io_failure("Background clear flags are invalid");
        }
        object.color = *color;
        object.image = *image;
        object.mode = static_cast<i32>(*mode_x);
        object.secondary_mode = static_cast<i32>(*mode_y);
        object.crop_x = static_cast<i32>(*crop_x);
        object.crop_y = static_cast<i32>(*crop_y);
        object.crop_width = static_cast<i32>(*crop_width);
        object.crop_height = static_cast<i32>(*crop_height);
        object.flag = *color_clear != 0U;
        object.secondary_flag = *depth_clear != 0U;
        (void)add_reference(object, *image);
        break;
    }
    case 5U: {
        parsed = parse_node(cursor, object);
        if (!parsed) break;
        auto projection_type = cursor.read_u8("Camera projection type");
        if (!projection_type) return std::unexpected(projection_type.error());
        object.projection_type = static_cast<i32>(*projection_type);
        const usize count = *projection_type == 52U ? 16U : 4U;
        if (*projection_type != 48U && *projection_type != 50U &&
            *projection_type != 52U) {
            return io_failure("Camera projection type is invalid");
        }
        object.projection.resize(count);
        for (float& value : object.projection) {
            auto parsed_value = cursor.read_float("Camera projection value");
            if (!parsed_value) return std::unexpected(parsed_value.error());
            value = *parsed_value;
        }
        break;
    }
    case 6U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto depth_test = cursor.read_u8("CompositingMode depth test");
        auto depth_write = cursor.read_u8("CompositingMode depth write");
        auto color_write = cursor.read_u8("CompositingMode color write");
        auto alpha_write = cursor.read_u8("CompositingMode alpha write");
        auto blending = cursor.read_u8("CompositingMode blending");
        auto alpha_threshold = cursor.read_u8("CompositingMode alpha threshold");
        auto depth_factor = cursor.read_float("CompositingMode depth factor");
        auto depth_units = cursor.read_float("CompositingMode depth units");
        if (!depth_test) return std::unexpected(depth_test.error());
        if (!depth_write) return std::unexpected(depth_write.error());
        if (!color_write) return std::unexpected(color_write.error());
        if (!alpha_write) return std::unexpected(alpha_write.error());
        if (!blending) return std::unexpected(blending.error());
        if (!alpha_threshold) return std::unexpected(alpha_threshold.error());
        if (!depth_factor) return std::unexpected(depth_factor.error());
        if (!depth_units) return std::unexpected(depth_units.error());
        if (*depth_test > 1U || *depth_write > 1U ||
            *color_write > 1U || *alpha_write > 1U) {
            return io_failure("CompositingMode boolean is invalid");
        }
        object.flag = *depth_test != 0U;
        object.secondary_flag = *depth_write != 0U;
        object.tertiary_flag = *color_write != 0U;
        object.quaternary_flag = *alpha_write != 0U;
        object.mode = static_cast<i32>(*blending);
        object.scalar = static_cast<float>(*alpha_threshold) / 255.0F;
        object.secondary_scalar = *depth_factor;
        object.tertiary_scalar = *depth_units;
        break;
    }
    case 7U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto color = read_rgb(cursor, "Fog color");
        auto mode = cursor.read_u8("Fog mode");
        if (!color) return std::unexpected(color.error());
        if (!mode) return std::unexpected(mode.error());
        object.color = *color;
        object.mode = static_cast<i32>(*mode);
        if (*mode == 80U) {
            auto density = cursor.read_float("Fog density");
            if (!density) return std::unexpected(density.error());
            object.scalar = *density;
        } else if (*mode == 81U) {
            auto near_value = cursor.read_float("Fog near distance");
            auto far_value = cursor.read_float("Fog far distance");
            if (!near_value) return std::unexpected(near_value.error());
            if (!far_value) return std::unexpected(far_value.error());
            object.scalar = *near_value;
            object.secondary_scalar = *far_value;
        } else {
            return io_failure("Fog mode is invalid");
        }
        break;
    }
    case 8U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto culling = cursor.read_u8("PolygonMode culling");
        auto shading = cursor.read_u8("PolygonMode shading");
        auto winding = cursor.read_u8("PolygonMode winding");
        auto two_sided = cursor.read_u8("PolygonMode two-sided lighting");
        auto local_camera = cursor.read_u8("PolygonMode local camera lighting");
        auto perspective = cursor.read_u8("PolygonMode perspective correction");
        if (!culling) return std::unexpected(culling.error());
        if (!shading) return std::unexpected(shading.error());
        if (!winding) return std::unexpected(winding.error());
        if (!two_sided) return std::unexpected(two_sided.error());
        if (!local_camera) return std::unexpected(local_camera.error());
        if (!perspective) return std::unexpected(perspective.error());
        if (*two_sided > 1U || *local_camera > 1U || *perspective > 1U) {
            return io_failure("PolygonMode boolean is invalid");
        }
        object.mode = static_cast<i32>(*culling);
        object.secondary_mode = static_cast<i32>(*shading);
        object.tertiary_mode = static_cast<i32>(*winding);
        object.flag = *two_sided != 0U;
        object.secondary_flag = *local_camera != 0U;
        object.tertiary_flag = *perspective != 0U;
        break;
    }
    case 10U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto format = cursor.read_u8("Image2D format");
        auto mutable_image = cursor.read_u8("Image2D mutable flag");
        auto width = cursor.read_u32("Image2D width");
        auto height = cursor.read_u32("Image2D height");
        if (!format) return std::unexpected(format.error());
        if (!mutable_image) return std::unexpected(mutable_image.error());
        if (!width) return std::unexpected(width.error());
        if (!height) return std::unexpected(height.error());
        if (*mutable_image > 1U || *width == 0U || *height == 0U) {
            return io_failure("Image2D metadata is invalid");
        }
        object.image_format = *format;
        object.mutable_image = *mutable_image != 0U;
        object.width = *width;
        object.height = *height;
        if (!object.mutable_image) {
            auto palette_length = cursor.read_u32("Image2D palette length");
            if (!palette_length) return std::unexpected(palette_length.error());
            auto palette = cursor.read_bytes(
                static_cast<usize>(*palette_length), "Image2D palette");
            if (!palette) return std::unexpected(palette.error());
            object.palette.assign(palette->begin(), palette->end());
            auto pixel_length = cursor.read_u32("Image2D pixel length");
            if (!pixel_length) return std::unexpected(pixel_length.error());
            auto pixels = cursor.read_bytes(
                static_cast<usize>(*pixel_length), "Image2D pixels");
            if (!pixels) return std::unexpected(pixels.error());
            object.pixels.assign(pixels->begin(), pixels->end());
        }
        break;
    }
    case 11U:
        parsed = parse_triangle_strip(cursor, object);
        break;
    case 12U: {
        parsed = parse_node(cursor, object);
        if (!parsed) break;
        for (float& value : object.vector3) {
            auto attenuation = cursor.read_float("Light attenuation");
            if (!attenuation) return std::unexpected(attenuation.error());
            value = *attenuation;
        }
        auto color = read_rgb(cursor, "Light color");
        auto mode = cursor.read_u8("Light mode");
        auto intensity = cursor.read_float("Light intensity");
        auto spot_angle = cursor.read_float("Light spot angle");
        auto spot_exponent = cursor.read_float("Light spot exponent");
        if (!color) return std::unexpected(color.error());
        if (!mode) return std::unexpected(mode.error());
        if (!intensity) return std::unexpected(intensity.error());
        if (!spot_angle) return std::unexpected(spot_angle.error());
        if (!spot_exponent) return std::unexpected(spot_exponent.error());
        object.color = *color;
        object.mode = static_cast<i32>(*mode);
        object.scalar = *intensity;
        object.secondary_scalar = *spot_angle;
        object.tertiary_scalar = *spot_exponent;
        break;
    }
    case 13U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto ambient = read_rgb(cursor, "Material ambient color");
        auto diffuse = read_argb(cursor, "Material diffuse color");
        auto emissive = read_rgb(cursor, "Material emissive color");
        auto specular = read_rgb(cursor, "Material specular color");
        auto shininess = cursor.read_float("Material shininess");
        auto tracking = cursor.read_u8("Material vertex color tracking");
        if (!ambient) return std::unexpected(ambient.error());
        if (!diffuse) return std::unexpected(diffuse.error());
        if (!emissive) return std::unexpected(emissive.error());
        if (!specular) return std::unexpected(specular.error());
        if (!shininess) return std::unexpected(shininess.error());
        if (!tracking) return std::unexpected(tracking.error());
        if (*tracking > 1U) {
            return io_failure("Material tracking flag is invalid");
        }
        object.color = *ambient;
        object.secondary_color = *diffuse;
        object.tertiary_color = *emissive;
        object.quaternary_color = *specular;
        object.scalar = *shininess;
        object.flag = *tracking != 0U;
        break;
    }
    case 9U:
        parsed = parse_group(cursor, object);
        break;
    case 14U:
        parsed = parse_mesh(cursor, object);
        break;
    case 15U: {
        parsed = parse_mesh(cursor, object);
        if (!parsed) break;
        auto target_count = cursor.read_u32("MorphingMesh target count");
        if (!target_count) return std::unexpected(target_count.error());
        if (*target_count == 0U || *target_count > 1'000'000U) {
            return io_failure("MorphingMesh target count is invalid");
        }
        object.morph_targets.reserve(*target_count);
        object.morph_weights.reserve(*target_count);
        for (u32 index = 0; index < *target_count; ++index) {
            auto target = cursor.read_u32("MorphingMesh target reference");
            auto weight = cursor.read_float("MorphingMesh target weight");
            if (!target) return std::unexpected(target.error());
            if (!weight) return std::unexpected(weight.error());
            if (!std::isfinite(*weight)) {
                return io_failure("MorphingMesh target weight is not finite");
            }
            object.morph_targets.push_back(*target);
            object.morph_weights.push_back(*weight);
            (void)add_reference(object, *target);
        }
        break;
    }
    case 16U: {
        parsed = parse_mesh(cursor, object);
        if (!parsed) break;
        auto skeleton = cursor.read_u32("SkinnedMesh skeleton reference");
        auto transform_count = cursor.read_u32(
            "SkinnedMesh transform reference count");
        if (!skeleton) return std::unexpected(skeleton.error());
        if (!transform_count) return std::unexpected(transform_count.error());
        if (*transform_count > 1'000'000U) {
            return io_failure("SkinnedMesh transform count is unreasonable");
        }
        object.skeleton = *skeleton;
        (void)add_reference(object, *skeleton);
        object.skin_transforms.reserve(*transform_count);
        for (u32 index = 0U; index < *transform_count; ++index) {
            auto bone = cursor.read_u32("SkinnedMesh bone reference");
            auto first_vertex = cursor.read_u32("SkinnedMesh first vertex");
            auto vertex_count = cursor.read_u32("SkinnedMesh vertex count");
            auto weight = cursor.read_u32("SkinnedMesh weight");
            if (!bone) return std::unexpected(bone.error());
            if (!first_vertex) return std::unexpected(first_vertex.error());
            if (!vertex_count) return std::unexpected(vertex_count.error());
            if (!weight) return std::unexpected(weight.error());
            object.skin_transforms.push_back(ParsedSkinTransform {
                .bone = *bone,
                .first_vertex = *first_vertex,
                .vertex_count = *vertex_count,
                .weight = static_cast<i32>(*weight),
            });
            (void)add_reference(object, *bone);
        }
        break;
    }
    case 17U: {
        parsed = parse_transformable(cursor, object);
        if (!parsed) break;
        auto image = cursor.read_u32("Texture2D image reference");
        auto blend_color = read_rgb(cursor, "Texture2D blend color");
        auto blending = cursor.read_u8("Texture2D blending");
        auto wrap_s = cursor.read_u8("Texture2D wrap S");
        auto wrap_t = cursor.read_u8("Texture2D wrap T");
        auto level_filter = cursor.read_u8("Texture2D level filter");
        auto image_filter = cursor.read_u8("Texture2D image filter");
        if (!image) return std::unexpected(image.error());
        if (!blend_color) return std::unexpected(blend_color.error());
        if (!blending) return std::unexpected(blending.error());
        if (!wrap_s) return std::unexpected(wrap_s.error());
        if (!wrap_t) return std::unexpected(wrap_t.error());
        if (!level_filter) return std::unexpected(level_filter.error());
        if (!image_filter) return std::unexpected(image_filter.error());
        object.image = *image;
        object.color = *blend_color;
        object.mode = static_cast<i32>(*blending);
        object.secondary_mode = static_cast<i32>(*wrap_s);
        object.tertiary_mode = static_cast<i32>(*wrap_t);
        object.quaternary_mode = static_cast<i32>(*level_filter);
        object.layer = static_cast<i32>(*image_filter);
        (void)add_reference(object, *image);
        break;
    }
    case 18U: {
        parsed = parse_node(cursor, object);
        if (!parsed) break;
        auto image = cursor.read_u32("Sprite3D image reference");
        auto appearance = cursor.read_u32("Sprite3D appearance reference");
        auto scaled = cursor.read_u8("Sprite3D scaled flag");
        auto crop_x = cursor.read_u32("Sprite3D crop X");
        auto crop_y = cursor.read_u32("Sprite3D crop Y");
        auto crop_width = cursor.read_u32("Sprite3D crop width");
        auto crop_height = cursor.read_u32("Sprite3D crop height");
        if (!image) return std::unexpected(image.error());
        if (!appearance) return std::unexpected(appearance.error());
        if (!scaled) return std::unexpected(scaled.error());
        if (!crop_x) return std::unexpected(crop_x.error());
        if (!crop_y) return std::unexpected(crop_y.error());
        if (!crop_width) return std::unexpected(crop_width.error());
        if (!crop_height) return std::unexpected(crop_height.error());
        if (*scaled > 1U) return io_failure("invalid Sprite3D scaled flag");
        const i32 signed_width = static_cast<i32>(*crop_width);
        const i32 signed_height = static_cast<i32>(*crop_height);
        if (signed_width == std::numeric_limits<i32>::min() ||
            signed_height == std::numeric_limits<i32>::min()) {
            return io_failure("Sprite3D crop dimensions are out of range");
        }
        object.scaled = *scaled != 0U;
        object.image = *image;
        object.appearance = *appearance;
        object.crop_x = static_cast<i32>(*crop_x);
        object.crop_y = static_cast<i32>(*crop_y);
        object.crop_width = signed_width < 0 ? -signed_width : signed_width;
        object.crop_height = signed_height < 0 ? -signed_height : signed_height;
        object.flag = signed_width < 0;
        object.secondary_flag = signed_height < 0;
        (void)add_reference(object, *image);
        (void)add_reference(object, *appearance);
        break;
    }
    case 19U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto interpolation = cursor.read_u8("KeyframeSequence interpolation");
        auto repeat_mode = cursor.read_u8("KeyframeSequence repeat mode");
        auto encoding = cursor.read_u8("KeyframeSequence encoding");
        auto duration = cursor.read_u32("KeyframeSequence duration");
        auto valid_first = cursor.read_u32("KeyframeSequence valid first");
        auto valid_last = cursor.read_u32("KeyframeSequence valid last");
        auto component_count = cursor.read_u32("KeyframeSequence component count");
        auto keyframe_count = cursor.read_u32("KeyframeSequence keyframe count");
        if (!interpolation) return std::unexpected(interpolation.error());
        if (!repeat_mode) return std::unexpected(repeat_mode.error());
        if (!encoding) return std::unexpected(encoding.error());
        if (!duration) return std::unexpected(duration.error());
        if (!valid_first) return std::unexpected(valid_first.error());
        if (!valid_last) return std::unexpected(valid_last.error());
        if (!component_count) return std::unexpected(component_count.error());
        if (!keyframe_count) return std::unexpected(keyframe_count.error());
        if (*interpolation < 176U || *interpolation > 180U ||
            (*repeat_mode != 192U && *repeat_mode != 193U) ||
            *encoding > 2U || *duration == 0U ||
            *component_count == 0U || *keyframe_count == 0U ||
            *valid_first > *valid_last || *valid_last >= *keyframe_count ||
            *component_count > 1'000'000U || *keyframe_count > 1'000'000U) {
            return io_failure("KeyframeSequence metadata is invalid");
        }
        const u64 value_count = static_cast<u64>(*component_count) *
                                static_cast<u64>(*keyframe_count);
        if (value_count > static_cast<u64>(kMaximumM3gBytes / sizeof(float))) {
            return io_failure("KeyframeSequence data is too large");
        }
        object.keyframe_interpolation = static_cast<i32>(*interpolation);
        object.keyframe_repeat_mode = static_cast<i32>(*repeat_mode);
        object.keyframe_duration = static_cast<i32>(*duration);
        object.keyframe_valid_first = static_cast<i32>(*valid_first);
        object.keyframe_valid_last = static_cast<i32>(*valid_last);
        object.keyframe_component_count = static_cast<i32>(*component_count);
        object.keyframe_count = static_cast<i32>(*keyframe_count);
        object.keyframe_times.resize(static_cast<usize>(*keyframe_count));
        object.keyframe_values.resize(static_cast<usize>(value_count));

        std::vector<float> bias(static_cast<usize>(*component_count), 0.0F);
        std::vector<float> scale(static_cast<usize>(*component_count), 1.0F);
        if (*encoding != 0U) {
            for (float& value : bias) {
                auto parsed_value = cursor.read_float("KeyframeSequence bias");
                if (!parsed_value) return std::unexpected(parsed_value.error());
                value = *parsed_value;
            }
            for (float& value : scale) {
                auto parsed_value = cursor.read_float("KeyframeSequence scale");
                if (!parsed_value) return std::unexpected(parsed_value.error());
                value = *parsed_value;
            }
        }
        for (u32 keyframe = 0U; keyframe < *keyframe_count; ++keyframe) {
            auto time = cursor.read_u32("KeyframeSequence keyframe time");
            if (!time) return std::unexpected(time.error());
            object.keyframe_times[static_cast<usize>(keyframe)] =
                static_cast<i32>(*time);
            for (u32 component = 0U; component < *component_count; ++component) {
                float value = 0.0F;
                if (*encoding == 0U) {
                    auto parsed_value = cursor.read_float(
                        "KeyframeSequence keyframe value");
                    if (!parsed_value) {
                        return std::unexpected(parsed_value.error());
                    }
                    value = *parsed_value;
                } else if (*encoding == 1U) {
                    auto raw = cursor.read_u8("KeyframeSequence byte value");
                    if (!raw) return std::unexpected(raw.error());
                    value = bias[static_cast<usize>(component)] +
                        scale[static_cast<usize>(component)] *
                        (static_cast<float>(*raw) / 255.0F);
                } else {
                    auto raw = cursor.read_u16("KeyframeSequence short value");
                    if (!raw) return std::unexpected(raw.error());
                    value = bias[static_cast<usize>(component)] +
                        scale[static_cast<usize>(component)] *
                        (static_cast<float>(*raw) / 65535.0F);
                }
                object.keyframe_values[
                    static_cast<usize>(keyframe) *
                        static_cast<usize>(*component_count) +
                    static_cast<usize>(component)] = value;
            }
        }
        break;
    }
    case 20U:
        parsed = parse_vertex_array(cursor, object);
        break;
    case 21U: {
        parsed = parse_object3d(cursor, object);
        if (!parsed) break;
        auto default_color = read_argb(cursor, "VertexBuffer default color");
        auto positions = cursor.read_u32("VertexBuffer positions reference");
        if (!default_color) return std::unexpected(default_color.error());
        if (!positions) return std::unexpected(positions.error());
        object.default_color = *default_color;
        object.positions = *positions;
        (void)add_reference(object, *positions);
        for (float& value : object.position_bias) {
            auto bias = cursor.read_float("VertexBuffer position bias");
            if (!bias) return std::unexpected(bias.error());
            value = *bias;
        }
        auto position_scale = cursor.read_float("VertexBuffer position scale");
        if (!position_scale) return std::unexpected(position_scale.error());
        object.position_scale = *position_scale;
        auto normals = cursor.read_u32("VertexBuffer normals reference");
        auto colors = cursor.read_u32("VertexBuffer colors reference");
        auto texcoord_count = cursor.read_u32("VertexBuffer texcoord count");
        if (!normals) return std::unexpected(normals.error());
        if (!colors) return std::unexpected(colors.error());
        if (!texcoord_count) return std::unexpected(texcoord_count.error());
        if (*texcoord_count > 256U) {
            return io_failure("VertexBuffer texcoord count is unreasonable");
        }
        object.normals = *normals;
        object.colors = *colors;
        (void)add_reference(object, *normals);
        (void)add_reference(object, *colors);
        object.texcoords.reserve(static_cast<usize>(*texcoord_count));
        object.texcoord_scales.reserve(static_cast<usize>(*texcoord_count));
        object.texcoord_biases.reserve(static_cast<usize>(*texcoord_count));
        for (u32 index = 0U; index < *texcoord_count; ++index) {
            auto texcoord = cursor.read_u32("VertexBuffer texcoord reference");
            if (!texcoord) return std::unexpected(texcoord.error());
            std::array<float, 3> bias {};
            for (float& value : bias) {
                auto parsed_bias = cursor.read_float(
                    "VertexBuffer texcoord bias");
                if (!parsed_bias) return std::unexpected(parsed_bias.error());
                value = *parsed_bias;
            }
            auto scale = cursor.read_float("VertexBuffer texcoord scale");
            if (!scale) return std::unexpected(scale.error());
            object.texcoords.push_back(*texcoord);
            object.texcoord_biases.push_back(bias);
            object.texcoord_scales.push_back(*scale);
            (void)add_reference(object, *texcoord);
        }
        break;
    }
    case 22U: {
        parsed = parse_group(cursor, object);
        if (!parsed) break;
        auto camera = cursor.read_u32("World camera reference");
        auto background = cursor.read_u32("World background reference");
        if (!camera) return std::unexpected(camera.error());
        if (!background) return std::unexpected(background.error());
        object.active_camera = *camera;
        object.background = *background;
        (void)add_reference(object, *camera);
        (void)add_reference(object, *background);
        break;
    }
    default:
        return io_failure("unsupported M3G object type " +
                          std::to_string(object.type));
    }
    if (!parsed) return parsed;
    return {};
}

[[nodiscard]] std::string_view class_for_type(u8 type) {
    switch (type) {
    case 1U: return kAnimationController;
    case 2U: return kAnimationTrack;
    case 3U: return kAppearance;
    case 4U: return kBackground;
    case 5U: return kCamera;
    case 6U: return kCompositingMode;
    case 7U: return kFog;
    case 8U: return kPolygonMode;
    case 9U: return kGroup;
    case 10U: return kImage2D;
    case 11U: return kTriangleStripArray;
    case 12U: return kLight;
    case 13U: return kMaterial;
    case 14U: return kMesh;
    case 15U: return "javax/microedition/m3g/MorphingMesh";
    case 16U: return "javax/microedition/m3g/SkinnedMesh";
    case 17U: return kTexture2D;
    case 18U: return kSprite3D;
    case 19U: return kKeyframeSequence;
    case 20U: return kVertexArray;
    case 21U: return kVertexBuffer;
    case 22U: return kWorld;
    default: return {};
    }
}

[[nodiscard]] Status initialize_loaded_object(
    Machine& machine,
    ParsedObject& object) {
    const std::string_view class_name = class_for_type(object.type);
    if (class_name.empty()) return {};
    auto allocated = allocate_instance(machine, class_name);
    if (!allocated) return std::unexpected(allocated.error());
    object.java_object = *allocated;

    Status initialized;
    switch (object.type) {
    case 9U:
    case 22U:
        initialized = initialize_group(machine, *allocated);
        break;
    case 5U:
    case 12U:
    case 14U:
    case 15U:
    case 16U:
    case 18U:
        initialized = initialize_node(machine, *allocated);
        break;
    case 17U:
        initialized = initialize_transformable(machine, *allocated);
        break;
    default:
        initialized = initialize_object3d(machine, *allocated);
        break;
    }
    if (!initialized) return initialized;

    auto user_id = set_int_field(machine, *allocated, kObject3D,
                                 "userID", static_cast<i32>(object.user_id));
    if (!user_id) return user_id;
    if (object.type == 5U || object.type == 9U || object.type == 12U ||
        object.type == 14U || object.type == 15U || object.type == 16U ||
        object.type == 18U || object.type == 22U) {
        auto rendering = set_int_field(machine, *allocated, kNode,
            "renderingEnabled", object.rendering_enabled ? 1 : 0, "Z");
        auto picking = set_int_field(machine, *allocated, kNode,
            "pickingEnabled", object.picking_enabled ? 1 : 0, "Z");
        auto alpha = set_float_field(machine, *allocated, kNode,
                                     "alphaFactor", object.alpha_factor);
        auto scope = set_int_field(machine, *allocated, kNode,
                                   "scope", object.scope);
        if (!rendering) return rendering;
        if (!picking) return picking;
        if (!alpha) return alpha;
        if (!scope) return scope;
    }
    const bool transformable = object.type == 5U || object.type == 9U ||
        object.type == 12U || object.type == 14U || object.type == 15U ||
        object.type == 16U || object.type == 17U || object.type == 18U ||
        object.type == 22U;
    if (transformable) {
        auto orientation = axis_angle_quaternion(
            object.orientation[0U], object.orientation[1U],
            object.orientation[2U], object.orientation[3U]);
        if (!orientation) return std::unexpected(orientation.error());
        auto generic = generic_transform(machine, *allocated);
        if (!generic) return std::unexpected(generic.error());
        auto generic_stored = set_transform_matrix(
            machine, *generic, object.generic_transform);
        if (!generic_stored) return generic_stored;
        const std::array<Status, 6> component_stored {
            set_float_field(machine, *allocated, kTransformable,
                            "translationX", object.translation[0U]),
            set_float_field(machine, *allocated, kTransformable,
                            "translationY", object.translation[1U]),
            set_float_field(machine, *allocated, kTransformable,
                            "translationZ", object.translation[2U]),
            set_float_field(machine, *allocated, kTransformable,
                            "scaleX", object.scale[0U]),
            set_float_field(machine, *allocated, kTransformable,
                            "scaleY", object.scale[1U]),
            set_float_field(machine, *allocated, kTransformable,
                            "scaleZ", object.scale[2U]),
        };
        for (const Status& status : component_stored) {
            if (!status) return status;
        }
        auto orientation_stored = set_transformable_quaternion(
            machine, *allocated, *orientation);
        if (!orientation_stored) return orientation_stored;
        auto rebuilt = rebuild_transformable_matrix(machine, *allocated);
        if (!rebuilt) return rebuilt;
    }

    if (object.type == 1U) {
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kAnimationController,
                          "activeStart", object.animation_active_start),
            set_int_field(machine, *allocated, kAnimationController,
                          "activeEnd", object.animation_active_end),
            set_float_field(machine, *allocated, kAnimationController,
                            "speed", object.animation_speed),
            set_float_field(machine, *allocated, kAnimationController,
                            "weight", object.animation_weight),
            set_float_field(machine, *allocated, kAnimationController,
                            "refSequenceTime",
                            object.animation_reference_sequence_time),
            set_int_field(machine, *allocated, kAnimationController,
                          "refWorldTime",
                          object.animation_reference_world_time),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 3U) {
        auto layer = set_int_field(machine, *allocated, kAppearance,
                                   "layer", object.layer);
        if (!layer) return layer;
    } else if (object.type == 4U) {
        const std::array<Status, 9> stored {
            set_int_field(machine, *allocated, kBackground,
                          "color", object.color),
            set_int_field(machine, *allocated, kBackground,
                          "imageModeX", object.mode),
            set_int_field(machine, *allocated, kBackground,
                          "imageModeY", object.secondary_mode),
            set_int_field(machine, *allocated, kBackground,
                          "cropX", object.crop_x),
            set_int_field(machine, *allocated, kBackground,
                          "cropY", object.crop_y),
            set_int_field(machine, *allocated, kBackground,
                          "cropWidth", object.crop_width),
            set_int_field(machine, *allocated, kBackground,
                          "cropHeight", object.crop_height),
            set_int_field(machine, *allocated, kBackground,
                          "colorClear", object.flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kBackground,
                          "depthClear", object.secondary_flag ? 1 : 0, "Z"),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 5U) {
        auto projection = allocate_array(
            machine, "[F", object.projection.size(), Value::from_float(0.0F));
        if (!projection) return std::unexpected(projection.error());
        auto root = machine.pin_native_root(*projection);
        if (!root) return std::unexpected(root.error());
        for (usize index = 0U; index < object.projection.size(); ++index) {
            auto stored = machine.heap().set_element(
                *projection, index, Value::from_float(object.projection[index]));
            if (!stored) return stored;
        }
        auto type = set_int_field(machine, *allocated, kCamera,
                                  "projectionType", object.projection_type);
        auto values = set_reference_field(machine, *allocated, kCamera,
                                           "projection", "[F", *projection);
        if (!type) return type;
        if (!values) return values;
    } else if (object.type == 6U) {
        const std::array<Status, 8> stored {
            set_int_field(machine, *allocated, kCompositingMode,
                          "depthTest", object.flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kCompositingMode,
                          "depthWrite", object.secondary_flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kCompositingMode,
                          "colorWrite", object.tertiary_flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kCompositingMode,
                          "alphaWrite", object.quaternary_flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kCompositingMode,
                          "blending", object.mode),
            set_float_field(machine, *allocated, kCompositingMode,
                            "alphaThreshold", object.scalar),
            set_float_field(machine, *allocated, kCompositingMode,
                            "depthOffsetFactor", object.secondary_scalar),
            set_float_field(machine, *allocated, kCompositingMode,
                            "depthOffsetUnits", object.tertiary_scalar),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 7U) {
        const std::array<Status, 5> stored {
            set_int_field(machine, *allocated, kFog, "mode", object.mode),
            set_float_field(machine, *allocated, kFog,
                            "density", object.scalar),
            set_float_field(machine, *allocated, kFog,
                            "nearDistance", object.scalar),
            set_float_field(machine, *allocated, kFog,
                            "farDistance", object.secondary_scalar),
            set_int_field(machine, *allocated, kFog, "color", object.color),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 8U) {
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kPolygonMode,
                          "culling", object.mode),
            set_int_field(machine, *allocated, kPolygonMode,
                          "shading", object.secondary_mode),
            set_int_field(machine, *allocated, kPolygonMode,
                          "winding", object.tertiary_mode),
            set_int_field(machine, *allocated, kPolygonMode,
                          "twoSided", object.flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kPolygonMode,
                          "localCameraLighting",
                          object.secondary_flag ? 1 : 0, "Z"),
            set_int_field(machine, *allocated, kPolygonMode,
                          "perspectiveCorrection",
                          object.tertiary_flag ? 1 : 0, "Z"),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 10U) {
        auto source = allocate_array(
            machine, "[B", object.mutable_image
                ? static_cast<usize>(object.width) *
                    static_cast<usize>(object.height) *
                    (object.image_format == 96U || object.image_format == 97U
                        ? 1U : object.image_format == 98U ? 2U
                        : object.image_format == 99U ? 3U : 4U)
                : object.pixels.size(),
            Value::from_int(0));
        auto palette = allocate_array(machine, "[B", object.palette.size(),
                                      Value::from_int(0));
        if (!source) return std::unexpected(source.error());
        if (!palette) return std::unexpected(palette.error());
        auto source_root = machine.pin_native_root(*source);
        auto palette_root = machine.pin_native_root(*palette);
        if (!source_root) return std::unexpected(source_root.error());
        if (!palette_root) return std::unexpected(palette_root.error());
        for (usize index = 0U; index < object.pixels.size(); ++index) {
            auto stored = machine.heap().set_element(
                *source, index, Value::from_int(object.pixels[index]));
            if (!stored) return stored;
        }
        for (usize index = 0U; index < object.palette.size(); ++index) {
            auto stored = machine.heap().set_element(
                *palette, index, Value::from_int(object.palette[index]));
            if (!stored) return stored;
        }
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kImage2D,
                          "format", object.image_format),
            set_int_field(machine, *allocated, kImage2D,
                          "width", static_cast<i32>(object.width)),
            set_int_field(machine, *allocated, kImage2D,
                          "height", static_cast<i32>(object.height)),
            set_int_field(machine, *allocated, kImage2D,
                          "mutable", object.mutable_image ? 1 : 0, "Z"),
            set_reference_field(machine, *allocated, kImage2D,
                                "source", "Ljava/lang/Object;", *source),
            set_reference_field(machine, *allocated, kImage2D,
                                "palette", "[B", *palette),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
        if (object.mutable_image) {
            auto image = graphics::Image::create_mutable(
                static_cast<i32>(object.width),
                static_cast<i32>(object.height));
            if (!image) return std::unexpected(image.error());
            auto attached = machine.graphics().attach_image(
                allocated->bits, std::move(*image));
            if (!attached) return attached;
        }
    } else if (object.type == 11U) {
        auto indices = allocate_array(machine, "[I", object.indices.size(),
                                      Value::from_int(0));
        auto lengths = allocate_array(machine, "[I", object.strip_lengths.size(),
                                      Value::from_int(0));
        if (!indices) return std::unexpected(indices.error());
        if (!lengths) return std::unexpected(lengths.error());
        auto indices_root = machine.pin_native_root(*indices);
        auto lengths_root = machine.pin_native_root(*lengths);
        if (!indices_root) return std::unexpected(indices_root.error());
        if (!lengths_root) return std::unexpected(lengths_root.error());
        for (usize index = 0U; index < object.indices.size(); ++index) {
            auto stored = machine.heap().set_element(
                *indices, index, Value::from_int(object.indices[index]));
            if (!stored) return stored;
        }
        for (usize index = 0U; index < object.strip_lengths.size(); ++index) {
            auto stored = machine.heap().set_element(
                *lengths, index, Value::from_int(object.strip_lengths[index]));
            if (!stored) return stored;
        }
        auto first = set_reference_field(machine, *allocated, kIndexBuffer,
                                         "indices", "[I", *indices);
        auto second = set_reference_field(machine, *allocated, kIndexBuffer,
                                          "stripLengths", "[I", *lengths);
        if (!first) return first;
        if (!second) return second;
    } else if (object.type == 12U) {
        auto attenuation = allocate_array(machine, "[F", 3U,
                                          Value::from_float(0.0F));
        if (!attenuation) return std::unexpected(attenuation.error());
        auto root = machine.pin_native_root(*attenuation);
        if (!root) return std::unexpected(root.error());
        for (usize index = 0U; index < object.vector3.size(); ++index) {
            auto stored = machine.heap().set_element(
                *attenuation, index, Value::from_float(object.vector3[index]));
            if (!stored) return stored;
        }
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kLight,
                          "mode", object.mode),
            set_float_field(machine, *allocated, kLight,
                            "intensity", object.scalar),
            set_int_field(machine, *allocated, kLight,
                          "color", object.color),
            set_float_field(machine, *allocated, kLight,
                            "spotAngle", object.secondary_scalar),
            set_float_field(machine, *allocated, kLight,
                            "spotExponent", object.tertiary_scalar),
            set_reference_field(machine, *allocated, kLight,
                                "attenuation", "[F", *attenuation),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 13U) {
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kMaterial,
                          "ambient", object.color),
            set_int_field(machine, *allocated, kMaterial,
                          "diffuse", object.secondary_color),
            set_int_field(machine, *allocated, kMaterial,
                          "emissive", object.tertiary_color),
            set_int_field(machine, *allocated, kMaterial,
                          "specular", object.quaternary_color),
            set_float_field(machine, *allocated, kMaterial,
                            "shininess", object.scalar),
            set_int_field(machine, *allocated, kMaterial,
                          "vertexColorTracking", object.flag ? 1 : 0, "Z"),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 17U) {
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kTexture2D,
                          "blendColor", object.color),
            set_int_field(machine, *allocated, kTexture2D,
                          "blending", object.mode),
            set_int_field(machine, *allocated, kTexture2D,
                          "wrapS", object.secondary_mode),
            set_int_field(machine, *allocated, kTexture2D,
                          "wrapT", object.tertiary_mode),
            set_int_field(machine, *allocated, kTexture2D,
                          "levelFilter", object.quaternary_mode),
            set_int_field(machine, *allocated, kTexture2D,
                          "imageFilter", object.layer),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 19U) {
        auto times = allocate_array(machine, "[I", object.keyframe_times.size(),
                                    Value::from_int(0));
        auto values = allocate_array(machine, "[F", object.keyframe_values.size(),
                                     Value::from_float(0.0F));
        if (!times) return std::unexpected(times.error());
        if (!values) return std::unexpected(values.error());
        auto times_root = machine.pin_native_root(*times);
        auto values_root = machine.pin_native_root(*values);
        if (!times_root) return std::unexpected(times_root.error());
        if (!values_root) return std::unexpected(values_root.error());
        for (usize index = 0U; index < object.keyframe_times.size(); ++index) {
            auto stored = machine.heap().set_element(
                *times, index, Value::from_int(object.keyframe_times[index]));
            if (!stored) return stored;
        }
        for (usize index = 0U; index < object.keyframe_values.size(); ++index) {
            auto stored = machine.heap().set_element(
                *values, index, Value::from_float(object.keyframe_values[index]));
            if (!stored) return stored;
        }
        const std::array<Status, 9> stored {
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "keyframeCount", object.keyframe_count),
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "componentCount", object.keyframe_component_count),
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "interpolationType", object.keyframe_interpolation),
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "validFirst", object.keyframe_valid_first),
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "validLast", object.keyframe_valid_last),
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "duration", object.keyframe_duration),
            set_int_field(machine, *allocated, kKeyframeSequence,
                          "repeatMode", object.keyframe_repeat_mode),
            set_reference_field(machine, *allocated, kKeyframeSequence,
                                "times", "[I", *times),
            set_reference_field(machine, *allocated, kKeyframeSequence,
                                "values", "[F", *values),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 20U) {
        auto data = allocate_array(
            machine, object.component_size == 1 ? "[B" : "[S",
            object.vertex_values.size(), Value::from_int(0));
        if (!data) return std::unexpected(data.error());
        auto root = machine.pin_native_root(*data);
        if (!root) return std::unexpected(root.error());
        for (usize index = 0U; index < object.vertex_values.size(); ++index) {
            auto stored = machine.heap().set_element(
                *data, index, Value::from_int(object.vertex_values[index]));
            if (!stored) return stored;
        }
        const std::array<Status, 4> stored {
            set_int_field(machine, *allocated, kVertexArray,
                          "vertexCount", object.vertex_count),
            set_int_field(machine, *allocated, kVertexArray,
                          "componentCount", object.component_count),
            set_int_field(machine, *allocated, kVertexArray,
                          "componentSize", object.component_size),
            set_reference_field(machine, *allocated, kVertexArray,
                                "data", "Ljava/lang/Object;", *data),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    } else if (object.type == 21U) {
        auto texcoords = allocate_array(
            machine, "[Ljavax/microedition/m3g/VertexArray;",
            object.texcoords.size(), Value::from_reference({}));
        auto scales = allocate_array(machine, "[F", object.texcoords.size(),
                                     Value::from_float(1.0F));
        auto biases = allocate_array(machine, "[[F", object.texcoords.size(),
                                     Value::from_reference({}));
        auto position_bias = allocate_array(machine, "[F", 3U,
                                            Value::from_float(0.0F));
        if (!texcoords) return std::unexpected(texcoords.error());
        if (!scales) return std::unexpected(scales.error());
        if (!biases) return std::unexpected(biases.error());
        if (!position_bias) return std::unexpected(position_bias.error());
        auto texcoords_root = machine.pin_native_root(*texcoords);
        auto scales_root = machine.pin_native_root(*scales);
        auto biases_root = machine.pin_native_root(*biases);
        auto position_root = machine.pin_native_root(*position_bias);
        if (!texcoords_root) return std::unexpected(texcoords_root.error());
        if (!scales_root) return std::unexpected(scales_root.error());
        if (!biases_root) return std::unexpected(biases_root.error());
        if (!position_root) return std::unexpected(position_root.error());
        for (usize index = 0U; index < 3U; ++index) {
            auto stored = machine.heap().set_element(
                *position_bias, index,
                Value::from_float(object.position_bias[index]));
            if (!stored) return stored;
        }
        const std::array<Status, 6> stored {
            set_int_field(machine, *allocated, kVertexBuffer,
                          "vertexCount", object.vertex_count),
            set_int_field(machine, *allocated, kVertexBuffer,
                          "defaultColor", object.default_color),
            set_float_field(machine, *allocated, kVertexBuffer,
                            "positionScale", object.position_scale),
            set_reference_field(machine, *allocated, kVertexBuffer,
                                "positionBias", "[F", *position_bias),
            set_reference_field(machine, *allocated, kVertexBuffer,
                "texScales", "[F", *scales),
            set_reference_field(machine, *allocated, kVertexBuffer,
                "texBiases", "[[F", *biases),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    }
    return {};
}

[[nodiscard]] Result<ObjectRef> referenced_object(
    std::span<const ParsedObject> objects,
    u32 index,
    std::string_view operation) {
    if (index == 0U) return ObjectRef {};
    if (index > objects.size() || objects[index - 1U].java_object.is_null()) {
        std::string message = std::string(operation) + " references object " +
            std::to_string(index) + " of " +
            std::to_string(objects.size());
        if (index != 0U && index <= objects.size()) {
            message += " (file type " +
                std::to_string(objects[index - 1U].type) +
                " has no runtime object)";
        }
        return fail_java("java/io/IOException", std::move(message));
    }
    return objects[index - 1U].java_object;
}

[[nodiscard]] Result<ObjectRef> reference_array(
    Machine& machine,
    std::string class_name,
    std::span<const ParsedObject> objects,
    std::span<const u32> references,
    std::string_view operation,
    usize minimum_length = 0U) {
    auto array = allocate_array(
        machine, std::move(class_name),
        std::max(references.size(), minimum_length),
        Value::from_reference({}));
    if (!array) return std::unexpected(array.error());
    for (usize index = 0; index < references.size(); ++index) {
        auto object = referenced_object(objects, references[index], operation);
        if (!object) return std::unexpected(object.error());
        auto stored = machine.heap().set_element(
            *array, index, Value::from_reference(*object));
        if (!stored) return std::unexpected(stored.error());
    }
    return *array;
}

[[nodiscard]] Result<ObjectRef> user_parameter_table(
    Machine& machine,
    const ParsedObject& object) {
    auto table = allocate_instance(machine, "java/util/Hashtable");
    if (!table) return std::unexpected(table.error());
    auto table_root = machine.pin_native_root(*table);
    if (!table_root) return std::unexpected(table_root.error());
    auto initialized = machine.invoke_instance(
        *table, "java/util/Hashtable", "<init>", "()V");
    if (!initialized) return std::unexpected(initialized.error());
    if (!initialized->completed_normally()) {
        return fail(ErrorCode::java_exception,
                    "M3G user parameter table initialization failed");
    }
    for (const auto& [id, bytes] : object.user_parameters) {
        const Value id_argument = Value::from_int(static_cast<i32>(id));
        auto boxed = machine.invoke_static(
            "java/lang/Integer", "valueOf", "(I)Ljava/lang/Integer;",
            std::span<const Value>(&id_argument, 1U));
        if (!boxed) return std::unexpected(boxed.error());
        if (!boxed->completed_normally() ||
            !boxed->return_value.has_value()) {
            return fail(ErrorCode::java_exception,
                        "M3G user parameter ID boxing failed");
        }
        auto key = boxed->return_value->as_reference();
        if (!key) return std::unexpected(key.error());
        auto data = allocate_array(machine, "[B", bytes.size(),
                                   Value::from_int(0));
        if (!data) return std::unexpected(data.error());
        auto key_root = machine.pin_native_root(*key);
        auto data_root = machine.pin_native_root(*data);
        if (!key_root) return std::unexpected(key_root.error());
        if (!data_root) return std::unexpected(data_root.error());
        auto written = machine.heap().write_byte_array(*data, 0U, bytes);
        if (!written) return std::unexpected(written.error());
        const std::array<Value, 2> put_arguments {
            Value::from_reference(*key), Value::from_reference(*data),
        };
        auto inserted = machine.invoke_instance(
            *table, "java/util/Hashtable", "put",
            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
            put_arguments);
        if (!inserted) return std::unexpected(inserted.error());
        if (!inserted->completed_normally()) {
            return fail(ErrorCode::java_exception,
                        "M3G user parameter insertion failed");
        }
    }
    return *table;
}

[[nodiscard]] Status link_group_hierarchy(
    Machine& machine,
    std::span<ParsedObject> objects,
    ParsedObject& object) {
    if (object.java_object.is_null() ||
        (object.type != 9U && object.type != 22U)) {
        return {};
    }
    const auto all = std::span<const ParsedObject>(objects.data(), objects.size());
    auto children = reference_array(
        machine, "[Ljavax/microedition/m3g/Node;", all,
        object.children, "Group children");
    if (!children) return std::unexpected(children.error());
    auto children_stored = set_reference_field(
        machine, object.java_object, kGroup, "children",
        "[Ljavax/microedition/m3g/Node;", *children);
    auto count_stored = set_int_field(
        machine, object.java_object, kGroup, "childCount",
        static_cast<i32>(object.children.size()));
    if (!children_stored) return children_stored;
    if (!count_stored) return count_stored;
    for (u32 child_index : object.children) {
        auto child = referenced_object(all, child_index, "Group child");
        if (!child) return std::unexpected(child.error());
        auto parent = set_reference_field(
            machine, *child, kNode, "parent",
            "Ljavax/microedition/m3g/Node;", object.java_object);
        if (!parent) return parent;
    }
    return {};
}

[[nodiscard]] Status link_loaded_object(
    Machine& machine,
    std::span<ParsedObject> objects,
    ParsedObject& object) {
    if (object.java_object.is_null()) return {};
    const auto all = std::span<const ParsedObject>(objects.data(), objects.size());

    auto animation_tracks = reference_array(
        machine, "[Ljavax/microedition/m3g/AnimationTrack;", all,
        object.animation_tracks, "Object3D animation tracks");
    if (!animation_tracks) return std::unexpected(animation_tracks.error());
    auto tracks_stored = set_reference_field(
        machine, object.java_object, kObject3D, "animationTracks",
        "[Ljavax/microedition/m3g/AnimationTrack;", *animation_tracks);
    auto track_count_stored = set_int_field(
        machine, object.java_object, kObject3D, "animationTrackCount",
        static_cast<i32>(object.animation_tracks.size()));
    if (!tracks_stored) return tracks_stored;
    if (!track_count_stored) return track_count_stored;
    if (!object.user_parameters.empty()) {
        auto table = user_parameter_table(machine, object);
        if (!table) return std::unexpected(table.error());
        auto stored = set_reference_field(
            machine, object.java_object, kObject3D, "userObject",
            "Ljava/lang/Object;", *table);
        if (!stored) return stored;
    }

    if (object.type == 2U) {
        auto sequence = referenced_object(all, object.animation_sequence,
                                          "AnimationTrack sequence");
        auto controller = referenced_object(all, object.animation_controller,
                                            "AnimationTrack controller");
        if (!sequence) return std::unexpected(sequence.error());
        if (!controller) return std::unexpected(controller.error());
        auto sequence_stored = set_reference_field(
            machine, object.java_object, kAnimationTrack, "sequence",
            "Ljavax/microedition/m3g/KeyframeSequence;", *sequence);
        auto controller_stored = set_reference_field(
            machine, object.java_object, kAnimationTrack, "controller",
            "Ljavax/microedition/m3g/AnimationController;", *controller);
        auto property_stored = set_int_field(
            machine, object.java_object, kAnimationTrack, "property",
            object.animation_property);
        if (!sequence_stored) return sequence_stored;
        if (!controller_stored) return controller_stored;
        if (!property_stored) return property_stored;
    }

    auto hierarchy_linked = link_group_hierarchy(machine, objects, object);
    if (!hierarchy_linked) return hierarchy_linked;

    if (object.type == 22U) {
        auto camera = referenced_object(all, object.active_camera, "World camera");
        auto background = referenced_object(all, object.background,
                                            "World background");
        if (!camera) return std::unexpected(camera.error());
        if (!background) return std::unexpected(background.error());
        auto camera_stored = set_reference_field(
            machine, object.java_object, kWorld, "activeCamera",
            "Ljavax/microedition/m3g/Camera;", *camera);
        auto background_stored = set_reference_field(
            machine, object.java_object, kWorld, "background",
            "Ljavax/microedition/m3g/Background;", *background);
        if (!camera_stored) return camera_stored;
        if (!background_stored) return background_stored;
    }

    if (object.type == 4U) {
        auto image = referenced_object(all, object.image, "Background image");
        if (!image) return std::unexpected(image.error());
        auto stored = set_reference_field(
            machine, object.java_object, kBackground, "image",
            "Ljavax/microedition/m3g/Image2D;", *image);
        if (!stored) return stored;
    }

    if (object.type == 3U) {
        auto textures = reference_array(
            machine, "[Ljavax/microedition/m3g/Texture2D;", all,
            object.textures, "Appearance textures", 8U);
        if (!textures) return std::unexpected(textures.error());
        auto textures_stored = set_reference_field(
            machine, object.java_object, kAppearance, "textures",
            "[Ljavax/microedition/m3g/Texture2D;", *textures);
        if (!textures_stored) return textures_stored;
        for (const auto& [reference, name, descriptor] :
             std::array<std::tuple<u32, const char*, const char*>, 4> {{
                 {object.compositing_mode, "compositingMode",
                  "Ljavax/microedition/m3g/CompositingMode;"},
                 {object.fog, "fog", "Ljavax/microedition/m3g/Fog;"},
                 {object.polygon_mode, "polygonMode",
                  "Ljavax/microedition/m3g/PolygonMode;"},
                 {object.material, "material",
                  "Ljavax/microedition/m3g/Material;"},
             }}) {
            auto target = referenced_object(all, reference, "Appearance state");
            if (!target) return std::unexpected(target.error());
            auto stored = set_reference_field(machine, object.java_object,
                                               kAppearance, name, descriptor,
                                               *target);
            if (!stored) return stored;
        }
    }

    if (object.type == 17U) {
        auto image = referenced_object(all, object.image, "Texture2D image");
        if (!image) return std::unexpected(image.error());
        auto stored = set_reference_field(
            machine, object.java_object, kTexture2D, "image",
            "Ljavax/microedition/m3g/Image2D;", *image);
        if (!stored) return stored;
    }

    if (object.type == 14U || object.type == 15U || object.type == 16U) {
        auto vertex_buffer = referenced_object(all, object.vertex_buffer,
                                               "Mesh vertex buffer");
        if (!vertex_buffer) return std::unexpected(vertex_buffer.error());
        auto vertex_stored = set_reference_field(
            machine, object.java_object, kMesh, "vertexBuffer",
            "Ljavax/microedition/m3g/VertexBuffer;", *vertex_buffer);
        if (!vertex_stored) return vertex_stored;
        std::vector<u32> indices;
        std::vector<u32> appearances;
        indices.reserve(object.submeshes.size());
        appearances.reserve(object.submeshes.size());
        for (const auto& [index, appearance] : object.submeshes) {
            indices.push_back(index);
            appearances.push_back(appearance);
        }
        auto index_array = reference_array(
            machine, "[Ljavax/microedition/m3g/IndexBuffer;", all,
            indices, "Mesh index buffers");
        auto appearance_array = reference_array(
            machine, "[Ljavax/microedition/m3g/Appearance;", all,
            appearances, "Mesh appearances");
        if (!index_array) return std::unexpected(index_array.error());
        if (!appearance_array) return std::unexpected(appearance_array.error());
        auto indices_stored = set_reference_field(
            machine, object.java_object, kMesh, "indexBuffers",
            "[Ljavax/microedition/m3g/IndexBuffer;", *index_array);
        auto appearances_stored = set_reference_field(
            machine, object.java_object, kMesh, "appearances",
            "[Ljavax/microedition/m3g/Appearance;", *appearance_array);
        if (!indices_stored) return indices_stored;
        if (!appearances_stored) return appearances_stored;
    }

    if (object.type == 15U) {
        auto targets = reference_array(
            machine, "[Ljavax/microedition/m3g/VertexBuffer;", all,
            object.morph_targets, "MorphingMesh targets");
        auto weights = float_array(machine, object.morph_weights);
        if (!targets) return std::unexpected(targets.error());
        if (!weights) return std::unexpected(weights.error());
        auto targets_stored = set_reference_field(
            machine, object.java_object,
            "javax/microedition/m3g/MorphingMesh", "morphTargets",
            "[Ljavax/microedition/m3g/VertexBuffer;", *targets);
        auto weights_stored = set_reference_field(
            machine, object.java_object,
            "javax/microedition/m3g/MorphingMesh", "weights",
            "[F", *weights);
        if (!targets_stored) return targets_stored;
        if (!weights_stored) return weights_stored;
    }

    if (object.type == 16U) {
        auto skeleton = referenced_object(all, object.skeleton,
                                          "SkinnedMesh skeleton");
        if (!skeleton) return std::unexpected(skeleton.error());
        auto skeleton_stored = set_reference_field(
            machine, object.java_object,
            "javax/microedition/m3g/SkinnedMesh", "skeleton",
            "Ljavax/microedition/m3g/Group;", *skeleton);
        if (!skeleton_stored) return skeleton_stored;
        for (const ParsedSkinTransform& influence : object.skin_transforms) {
            auto bone = referenced_object(all, influence.bone,
                                          "SkinnedMesh bone");
            if (!bone) return std::unexpected(bone.error());
            const std::array<Value, 4> arguments {
                Value::from_reference(*bone),
                Value::from_int(influence.weight),
                Value::from_int(static_cast<i32>(influence.first_vertex)),
                Value::from_int(static_cast<i32>(influence.vertex_count)),
            };
            auto invoked = machine.invoke_instance(
                object.java_object,
                "javax/microedition/m3g/SkinnedMesh", "addTransform",
                "(Ljavax/microedition/m3g/Node;III)V", arguments);
            if (!invoked) return std::unexpected(invoked.error());
            if (!invoked->completed_normally()) {
                if (!invoked->throwable.has_value()) {
                    return fail(ErrorCode::internal_error,
                                "SkinnedMesh addTransform failed without a throwable");
                }
                auto class_name = machine.heap().class_name(*invoked->throwable);
                if (!class_name) return std::unexpected(class_name.error());
                std::string detail =
                    "SkinnedMesh transform reference is invalid"
                    " (weight=" + std::to_string(influence.weight) +
                    ", firstVertex=" +
                    std::to_string(influence.first_vertex) +
                    ", vertexCount=" +
                    std::to_string(influence.vertex_count) + ")";
                auto message_field = machine.heap().field(*invoked->throwable, 0U);
                if (message_field) {
                    auto message = message_field->as_reference();
                    if (message && !message->is_null()) {
                        auto text = machine.heap().string_value(*message);
                        if (text && !text->empty()) {
                            detail += ": " + utf8_from_utf16(*text);
                        }
                    }
                }
                return fail_java(*class_name, std::move(detail));
            }
        }
    }

    if (object.type == 18U) {
        auto image = referenced_object(all, object.image, "Sprite3D image");
        auto appearance = referenced_object(all, object.appearance,
                                            "Sprite3D appearance");
        if (!image) return std::unexpected(image.error());
        if (!appearance) return std::unexpected(appearance.error());
        const std::array<Status, 9> stored {
            set_int_field(machine, object.java_object, kSprite3D,
                          "scaled", object.scaled ? 1 : 0, "Z"),
            set_reference_field(machine, object.java_object, kSprite3D, "image",
                "Ljavax/microedition/m3g/Image2D;", *image),
            set_reference_field(machine, object.java_object, kSprite3D,
                "appearance", "Ljavax/microedition/m3g/Appearance;", *appearance),
            set_int_field(machine, object.java_object, kSprite3D,
                          "cropX", object.crop_x),
            set_int_field(machine, object.java_object, kSprite3D,
                          "cropY", object.crop_y),
            set_int_field(machine, object.java_object, kSprite3D,
                          "cropWidth", object.crop_width),
            set_int_field(machine, object.java_object, kSprite3D,
                          "cropHeight", object.crop_height),
            set_int_field(machine, object.java_object, kSprite3D,
                          "flipX", object.flag ? 1 : 0, "Z"),
            set_int_field(machine, object.java_object, kSprite3D,
                          "flipY", object.secondary_flag ? 1 : 0, "Z"),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    }

    if (object.type == 21U) {
        ObjectRef positions {};
        for (const auto& [reference, field] :
             std::array<std::pair<u32, const char*>, 3> {{
                 {object.positions, "positions"},
                 {object.normals, "normals"},
                 {object.colors, "colors"},
             }}) {
            auto target = referenced_object(all, reference, "VertexBuffer array");
            if (!target) return std::unexpected(target.error());
            if (std::string_view(field) == "positions") positions = *target;
            auto stored = set_reference_field(
                machine, object.java_object, kVertexBuffer, field,
                "Ljavax/microedition/m3g/VertexArray;", *target);
            if (!stored) return stored;
        }
        if (!positions.is_null()) {
            auto count = int_field(machine, positions, kVertexArray,
                                   "vertexCount");
            if (!count) return std::unexpected(count.error());
            auto stored = set_int_field(machine, object.java_object,
                                        kVertexBuffer, "vertexCount", *count);
            if (!stored) return stored;
        }

        auto texcoords = reference_array(
            machine, "[Ljavax/microedition/m3g/VertexArray;", all,
            object.texcoords, "VertexBuffer texcoords");
        auto scales = allocate_array(machine, "[F", object.texcoord_scales.size(),
                                     Value::from_float(1.0F));
        auto biases = allocate_array(machine, "[[F", object.texcoord_biases.size(),
                                     Value::from_reference({}));
        if (!texcoords) return std::unexpected(texcoords.error());
        if (!scales) return std::unexpected(scales.error());
        if (!biases) return std::unexpected(biases.error());
        auto texcoords_root = machine.pin_native_root(*texcoords);
        auto scales_root = machine.pin_native_root(*scales);
        auto biases_root = machine.pin_native_root(*biases);
        if (!texcoords_root) return std::unexpected(texcoords_root.error());
        if (!scales_root) return std::unexpected(scales_root.error());
        if (!biases_root) return std::unexpected(biases_root.error());
        for (usize index = 0U; index < object.texcoord_scales.size(); ++index) {
            auto scale_stored = machine.heap().set_element(
                *scales, index, Value::from_float(object.texcoord_scales[index]));
            if (!scale_stored) return scale_stored;
            auto bias = allocate_array(machine, "[F", 3U,
                                       Value::from_float(0.0F));
            if (!bias) return std::unexpected(bias.error());
            auto bias_root = machine.pin_native_root(*bias);
            if (!bias_root) return std::unexpected(bias_root.error());
            for (usize component = 0U; component < 3U; ++component) {
                auto stored = machine.heap().set_element(
                    *bias, component,
                    Value::from_float(object.texcoord_biases[index][component]));
                if (!stored) return stored;
            }
            auto bias_stored = machine.heap().set_element(
                *biases, index, Value::from_reference(*bias));
            if (!bias_stored) return bias_stored;
        }
        const std::array<Status, 3> stored {
            set_reference_field(machine, object.java_object, kVertexBuffer,
                "texCoords", "[Ljavax/microedition/m3g/VertexArray;", *texcoords),
            set_reference_field(machine, object.java_object, kVertexBuffer,
                "texScales", "[F", *scales),
            set_reference_field(machine, object.java_object, kVertexBuffer,
                "texBiases", "[[F", *biases),
        };
        for (const Status& status : stored) {
            if (!status) return status;
        }
    }
    return {};
}

} // namespace

Result<ObjectRef> load_m3g(
    Machine& machine,
    std::span<const u8> bytes,
    const ExternalReferenceResolver& resolve_external) {
    std::vector<ParsedObject> objects;
    auto sections = parse_sections(bytes, objects);
    if (!sections) return std::unexpected(sections.error());
    for (ParsedObject& object : objects) {
        auto parsed = parse_serialized_object(object);
        if (!parsed) return std::unexpected(parsed.error());
    }

    std::vector<NativeRootScope> pinned;
    pinned.reserve(objects.size() + 1U);
    for (ParsedObject& object : objects) {
        if (object.type == 0U) continue;
        if (object.type == 255U) {
            if (!resolve_external) {
                return fail_java(
                    "java/io/IOException",
                    "M3G file contains an external reference without a resolver");
            }
            auto resolved = resolve_external(object.external_uri);
            if (!resolved) return std::unexpected(resolved.error());
            if (resolved->is_null()) {
                return fail_java("java/io/IOException",
                                 "M3G external reference resolved to null");
            }
            object.java_object = *resolved;
        } else {
            auto initialized = initialize_loaded_object(machine, object);
            if (!initialized) return std::unexpected(initialized.error());
        }
        auto root = machine.pin_native_root(object.java_object);
        if (!root) return std::unexpected(root.error());
        pinned.push_back(std::move(*root));
    }
    // Parent links must exist for the whole scene graph before a SkinnedMesh
    // validates that each bone belongs to its skeleton. Serialized files are
    // free to place the SkinnedMesh before the Group objects it references.
    for (ParsedObject& object : objects) {
        auto hierarchy_linked = link_group_hierarchy(machine, objects, object);
        if (!hierarchy_linked) {
            return std::unexpected(hierarchy_linked.error());
        }
    }
    for (ParsedObject& object : objects) {
        auto linked = link_loaded_object(machine, objects, object);
        if (!linked) return std::unexpected(linked.error());
    }

    std::unordered_set<u32> referenced;
    for (const ParsedObject& object : objects) {
        for (u32 reference : object.references) {
            if (reference != 0U) referenced.insert(reference);
        }
    }
    std::vector<ObjectRef> roots;
    for (usize index = 0; index < objects.size(); ++index) {
        const ParsedObject& object = objects[index];
        const u32 serialized_index = static_cast<u32>(index + 1U);
        if (!object.java_object.is_null() &&
            !referenced.contains(serialized_index)) {
            roots.push_back(object.java_object);
        }
    }
    if (roots.empty()) {
        for (const ParsedObject& object : objects) {
            if (!object.java_object.is_null()) roots.push_back(object.java_object);
        }
    }
    auto result = allocate_array(machine,
        "[Ljavax/microedition/m3g/Object3D;", roots.size(),
        Value::from_reference({}));
    if (!result) return std::unexpected(result.error());
    auto result_root = machine.pin_native_root(*result);
    if (!result_root) return std::unexpected(result_root.error());
    for (usize index = 0; index < roots.size(); ++index) {
        auto stored = machine.heap().set_element(
            *result, index, Value::from_reference(roots[index]));
        if (!stored) return std::unexpected(stored.error());
    }
    return *result;
}

} // namespace phoneme::vm::m3g
