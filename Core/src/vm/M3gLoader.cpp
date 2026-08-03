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
#include "phoneme/vm/NativeRootScope.hpp"

namespace phoneme::vm::m3g {
namespace {

constexpr std::array<u8, 12> kM3gSignature {
    0xABU, 0x4AU, 0x53U, 0x52U, 0x31U, 0x38U,
    0x34U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
};
constexpr usize kMaximumM3gBytes = 64U * 1024U * 1024U;

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

struct ParsedObject final {
    u8 type {0};
    std::vector<u8> payload;
    u32 user_id {0};
    std::vector<u32> animation_tracks;
    u32 animation_sequence {0};
    u32 animation_controller {0};
    i32 animation_property {0};
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
    u32 positions {0};
    u32 normals {0};
    u32 colors {0};
    std::vector<u32> texcoords;
    u8 image_format {99U};
    u32 width {1U};
    u32 height {1U};
    bool scaled {false};
    bool rendering_enabled {true};
    bool picking_enabled {true};
    float alpha_factor {1.0F};
    i32 scope {-1};
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
    for (u32 index = 0; index < *parameter_count; ++index) {
        auto ignored_id = cursor.read_u32("Object3D user parameter ID");
        auto length = cursor.read_u32("Object3D user parameter length");
        if (!ignored_id) return std::unexpected(ignored_id.error());
        if (!length) return std::unexpected(length.error());
        auto skipped = cursor.skip(static_cast<usize>(*length),
                                   "Object3D user parameter data");
        if (!skipped) return skipped;
    }
    return {};
}

[[nodiscard]] Status parse_transformable(Cursor& cursor,
                                         ParsedObject& object) {
    auto base = parse_object3d(cursor, object);
    if (!base) return base;
    auto has_components = cursor.read_u8("Transformable component flag");
    if (!has_components) return std::unexpected(has_components.error());
    if (*has_components != 0U) {
        auto skipped = cursor.skip(10U * sizeof(float),
                                   "Transformable components");
        if (!skipped) return skipped;
    }
    auto has_matrix = cursor.read_u8("Transformable matrix flag");
    if (!has_matrix) return std::unexpected(has_matrix.error());
    if (*has_matrix != 0U) {
        auto skipped = cursor.skip(16U * sizeof(float),
                                   "Transformable matrix");
        if (!skipped) return skipped;
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

[[nodiscard]] Status parse_serialized_object(ParsedObject& object) {
    if (object.type == 0U || object.type == 255U) return {};
    Cursor cursor(object.payload);
    Status parsed;
    switch (object.type) {
    case 1U:
    case 6U:
    case 7U:
    case 8U:
    case 10U:
    case 11U:
    case 13U:
    case 19U:
    case 20U:
    case 21U:
        parsed = parse_object3d(cursor, object);
        break;
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
        auto color = cursor.read_u32("Background color");
        auto image = cursor.read_u32("Background image reference");
        if (!color) return std::unexpected(color.error());
        if (!image) return std::unexpected(image.error());
        object.image = *image;
        (void)add_reference(object, *image);
        break;
    }
    case 5U:
    case 12U:
        parsed = parse_node(cursor, object);
        break;
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
        for (u32 index = 0; index < *target_count; ++index) {
            auto target = cursor.read_u32("MorphingMesh target reference");
            if (!target) return std::unexpected(target.error());
            (void)add_reference(object, *target);
        }
        break;
    }
    case 16U: {
        parsed = parse_mesh(cursor, object);
        if (!parsed) break;
        auto skeleton = cursor.read_u32("SkinnedMesh skeleton reference");
        if (!skeleton) return std::unexpected(skeleton.error());
        (void)add_reference(object, *skeleton);
        break;
    }
    case 17U: {
        parsed = parse_transformable(cursor, object);
        if (!parsed) break;
        auto image = cursor.read_u32("Texture2D image reference");
        if (!image) return std::unexpected(image.error());
        object.image = *image;
        (void)add_reference(object, *image);
        break;
    }
    case 18U: {
        parsed = parse_node(cursor, object);
        if (!parsed) break;
        auto scaled = cursor.read_u8("Sprite3D scaled flag");
        auto image = cursor.read_u32("Sprite3D image reference");
        auto appearance = cursor.read_u32("Sprite3D appearance reference");
        if (!scaled) return std::unexpected(scaled.error());
        if (!image) return std::unexpected(image.error());
        if (!appearance) return std::unexpected(appearance.error());
        object.scaled = *scaled != 0U;
        object.image = *image;
        object.appearance = *appearance;
        (void)add_reference(object, *image);
        (void)add_reference(object, *appearance);
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

    if (object.type == 10U) {
        auto format = cursor.read_u8("Image2D format");
        auto mutable_image = cursor.read_u8("Image2D mutable flag");
        auto width = cursor.read_u32("Image2D width");
        auto height = cursor.read_u32("Image2D height");
        if (!format) return std::unexpected(format.error());
        if (!mutable_image) return std::unexpected(mutable_image.error());
        if (!width) return std::unexpected(width.error());
        if (!height) return std::unexpected(height.error());
        object.image_format = *format;
        object.width = std::max<u32>(*width, 1U);
        object.height = std::max<u32>(*height, 1U);
    } else if (object.type == 21U) {
        auto default_color = cursor.read_u32("VertexBuffer default color");
        auto positions = cursor.read_u32("VertexBuffer positions reference");
        if (!default_color) return std::unexpected(default_color.error());
        if (!positions) return std::unexpected(positions.error());
        object.positions = *positions;
        (void)add_reference(object, *positions);
        if (*positions != 0U) {
            auto skipped = cursor.skip(sizeof(float) * 4U,
                                       "VertexBuffer position scale and bias");
            if (!skipped) return skipped;
        }
        auto normals = cursor.read_u32("VertexBuffer normals reference");
        auto colors = cursor.read_u32("VertexBuffer colors reference");
        auto texcoord_count = cursor.read_u32("VertexBuffer texcoord count");
        if (!normals) return std::unexpected(normals.error());
        if (!colors) return std::unexpected(colors.error());
        if (!texcoord_count) return std::unexpected(texcoord_count.error());
        object.normals = *normals;
        object.colors = *colors;
        (void)add_reference(object, *normals);
        (void)add_reference(object, *colors);
        for (u32 index = 0; index < *texcoord_count; ++index) {
            auto texcoord = cursor.read_u32("VertexBuffer texcoord reference");
            if (!texcoord) return std::unexpected(texcoord.error());
            object.texcoords.push_back(*texcoord);
            (void)add_reference(object, *texcoord);
            if (*texcoord != 0U) {
                auto skipped = cursor.skip(sizeof(float) * 4U,
                                           "VertexBuffer texcoord scale and bias");
                if (!skipped) return skipped;
            }
        }
    }
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
    if (object.type == 10U) {
        auto format = set_int_field(machine, *allocated, kImage2D,
                                    "format", object.image_format);
        auto width = set_int_field(machine, *allocated, kImage2D,
                                   "width", static_cast<i32>(object.width));
        auto height = set_int_field(machine, *allocated, kImage2D,
                                    "height", static_cast<i32>(object.height));
        auto immutable = set_int_field(machine, *allocated, kImage2D,
                                       "mutable", 0, "Z");
        if (!format) return format;
        if (!width) return width;
        if (!height) return height;
        if (!immutable) return immutable;
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
    std::string_view operation) {
    auto array = allocate_array(machine, std::move(class_name),
                                references.size(), Value::from_reference({}));
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

    if (object.type == 9U || object.type == 22U) {
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
    }

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

    if (object.type == 3U) {
        auto textures = reference_array(
            machine, "[Ljavax/microedition/m3g/Texture2D;", all,
            object.textures, "Appearance textures");
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
        for (const auto& [field, value] :
             std::array<std::pair<const char*, i32>, 5> {{
                 {"blending", 228}, {"levelFilter", 208},
                 {"imageFilter", 209}, {"wrapS", 240}, {"wrapT", 240},
             }}) {
            auto value_stored = set_int_field(
                machine, object.java_object, kTexture2D, field, value);
            if (!value_stored) return value_stored;
        }
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

    if (object.type == 18U) {
        auto image = referenced_object(all, object.image, "Sprite3D image");
        auto appearance = referenced_object(all, object.appearance,
                                            "Sprite3D appearance");
        if (!image) return std::unexpected(image.error());
        if (!appearance) return std::unexpected(appearance.error());
        auto scaled = set_int_field(machine, object.java_object, kSprite3D,
                                    "scaled", object.scaled ? 1 : 0, "Z");
        auto image_stored = set_reference_field(
            machine, object.java_object, kSprite3D, "image",
            "Ljavax/microedition/m3g/Image2D;", *image);
        auto appearance_stored = set_reference_field(
            machine, object.java_object, kSprite3D, "appearance",
            "Ljavax/microedition/m3g/Appearance;", *appearance);
        if (!scaled) return scaled;
        if (!image_stored) return image_stored;
        if (!appearance_stored) return appearance_stored;
    }

    if (object.type == 21U) {
        for (const auto& [reference, field] :
             std::array<std::pair<u32, const char*>, 3> {{
                 {object.positions, "positions"},
                 {object.normals, "normals"},
                 {object.colors, "colors"},
             }}) {
            auto target = referenced_object(all, reference, "VertexBuffer array");
            if (!target) return std::unexpected(target.error());
            auto stored = set_reference_field(
                machine, object.java_object, kVertexBuffer, field,
                "Ljavax/microedition/m3g/VertexArray;", *target);
            if (!stored) return stored;
        }
        auto texcoords = reference_array(
            machine, "[Ljavax/microedition/m3g/VertexArray;", all,
            object.texcoords, "VertexBuffer texcoords");
        if (!texcoords) return std::unexpected(texcoords.error());
        auto stored = set_reference_field(
            machine, object.java_object, kVertexBuffer, "texCoords",
            "[Ljavax/microedition/m3g/VertexArray;", *texcoords);
        if (!stored) return stored;
    }
    return {};
}

} // namespace

Result<ObjectRef> load_m3g(Machine& machine, std::span<const u8> bytes) {
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
        if (object.type == 0U || object.type == 255U) continue;
        auto initialized = initialize_loaded_object(machine, object);
        if (!initialized) return std::unexpected(initialized.error());
        auto root = machine.pin_native_root(object.java_object);
        if (!root) return std::unexpected(root.error());
        pinned.push_back(std::move(*root));
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
