#include "M3gNativeModules.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

constexpr const char* kImage2D = "javax/microedition/m3g/Image2D";
constexpr const char* kTexture2D = "javax/microedition/m3g/Texture2D";
constexpr const char* kVertexArray = "javax/microedition/m3g/VertexArray";
constexpr const char* kVertexBuffer = "javax/microedition/m3g/VertexBuffer";
constexpr const char* kIndexBuffer = "javax/microedition/m3g/IndexBuffer";
constexpr const char* kTriangleStrip = "javax/microedition/m3g/TriangleStripArray";
constexpr const char* kMesh = "javax/microedition/m3g/Mesh";
constexpr const char* kSprite3D = "javax/microedition/m3g/Sprite3D";

void register_read_only_int(NativeMethodRegistry& registry,
                            const char* owner,
                            const char* field,
                            const char* getter,
                            const char* descriptor = "I") {
    add(registry, owner, getter, std::string("()") + descriptor,
        [owner, field, descriptor, getter](Machine& machine,
                                   std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, getter);
            if (!object) return std::unexpected(object.error());
            auto value = object_field(machine, *object, owner, field, descriptor);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
}

void register_image2d(NativeMethodRegistry& registry) {
    add(registry, kImage2D, "<init>", "(ILjava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Image2D.<init>");
            auto format = int_argument(arguments, 1U, "Image2D.<init>");
            auto source = reference_argument(arguments, 2U,
                                             "Image2D.<init>", false);
            if (!object) return std::unexpected(object.error());
            if (!format) return std::unexpected(format.error());
            if (!source) return std::unexpected(source.error());

            i32 width = 0;
            i32 height = 0;
            auto source_class = machine.heap().class_name(*source);
            if (!source_class) return std::unexpected(source_class.error());
            if (*source_class == "javax/microedition/lcdui/Image") {
                auto width_location = field_location(machine,
                    "javax/microedition/lcdui/Image", "width", "I");
                auto height_location = field_location(machine,
                    "javax/microedition/lcdui/Image", "height", "I");
                if (!width_location) return std::unexpected(width_location.error());
                if (!height_location) return std::unexpected(height_location.error());
                auto width_value = machine.heap().field(*source,
                                                        width_location->index);
                auto height_value = machine.heap().field(*source,
                                                         height_location->index);
                if (!width_value) return std::unexpected(width_value.error());
                if (!height_value) return std::unexpected(height_value.error());
                auto parsed_width = width_value->as_int();
                auto parsed_height = height_value->as_int();
                if (!parsed_width) return std::unexpected(parsed_width.error());
                if (!parsed_height) return std::unexpected(parsed_height.error());
                width = *parsed_width;
                height = *parsed_height;
            }

            auto format_stored = set_int_field(machine, *object, kImage2D,
                                               "format", *format);
            auto width_stored = set_int_field(machine, *object, kImage2D,
                                              "width", width);
            auto height_stored = set_int_field(machine, *object, kImage2D,
                                               "height", height);
            auto source_stored = set_reference_field(machine, *object, kImage2D,
                "source", "Ljava/lang/Object;", *source);
            if (!format_stored) return std::unexpected(format_stored.error());
            if (!width_stored) return std::unexpected(width_stored.error());
            if (!height_stored) return std::unexpected(height_stored.error());
            if (!source_stored) return std::unexpected(source_stored.error());
            return std::optional<Value> {};
        });

    const auto dimensions_constructor = [&registry](const char* descriptor,
                                                    bool mutable_image) {
        add(registry, kImage2D, "<init>", descriptor,
            [mutable_image](Machine& machine,
                            std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Image2D.<init>");
                auto format = int_argument(arguments, 1U, "Image2D.<init>");
                auto width = int_argument(arguments, 2U, "Image2D.<init>");
                auto height = int_argument(arguments, 3U, "Image2D.<init>");
                if (!object) return std::unexpected(object.error());
                if (!format) return std::unexpected(format.error());
                if (!width) return std::unexpected(width.error());
                if (!height) return std::unexpected(height.error());
                if (*width <= 0 || *height <= 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Image2D dimensions must be positive");
                }
                auto format_stored = set_int_field(machine, *object, kImage2D,
                                                   "format", *format);
                auto width_stored = set_int_field(machine, *object, kImage2D,
                                                  "width", *width);
                auto height_stored = set_int_field(machine, *object, kImage2D,
                                                   "height", *height);
                auto mutable_stored = set_int_field(machine, *object, kImage2D,
                    "mutable", mutable_image ? 1 : 0, "Z");
                if (!format_stored) return std::unexpected(format_stored.error());
                if (!width_stored) return std::unexpected(width_stored.error());
                if (!height_stored) return std::unexpected(height_stored.error());
                if (!mutable_stored) return std::unexpected(mutable_stored.error());
                return std::optional<Value> {};
            });
    };
    dimensions_constructor("(III)V", true);
    dimensions_constructor("(III[B)V", false);
    dimensions_constructor("(III[B[B)V", false);

    register_read_only_int(registry, kImage2D, "format", "getFormat");
    register_read_only_int(registry, kImage2D, "width", "getWidth");
    register_read_only_int(registry, kImage2D, "height", "getHeight");
    register_read_only_int(registry, kImage2D, "mutable", "isMutable", "Z");
    add(registry, kImage2D, "set", "(III[B)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Image2D.set");
            if (!object) return std::unexpected(object.error());
            auto mutable_value = int_field(machine, *object, kImage2D,
                                           "mutable", "Z");
            if (!mutable_value) return std::unexpected(mutable_value.error());
            if (*mutable_value == 0) {
                return fail_java("java/lang/IllegalStateException",
                                 "Image2D is immutable");
            }
            return std::optional<Value> {};
        });
}

void register_texture2d(NativeMethodRegistry& registry) {
    add(registry, kTexture2D, "<init>",
        "(Ljavax/microedition/m3g/Image2D;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Texture2D.<init>");
            auto image = reference_argument(arguments, 1U,
                                            "Texture2D.<init>", false);
            if (!object) return std::unexpected(object.error());
            if (!image) return std::unexpected(image.error());
            auto initialized = initialize_transformable(machine, *object);
            if (!initialized) return std::unexpected(initialized.error());
            auto image_stored = set_reference_field(machine, *object, kTexture2D,
                "image", "Ljavax/microedition/m3g/Image2D;", *image);
            auto blending = set_int_field(machine, *object, kTexture2D,
                                          "blending", 228);
            auto level_filter = set_int_field(machine, *object, kTexture2D,
                                              "levelFilter", 208);
            auto image_filter = set_int_field(machine, *object, kTexture2D,
                                              "imageFilter", 210);
            auto wrap_s = set_int_field(machine, *object, kTexture2D,
                                        "wrapS", 240);
            auto wrap_t = set_int_field(machine, *object, kTexture2D,
                                        "wrapT", 240);
            if (!image_stored) return std::unexpected(image_stored.error());
            if (!blending) return std::unexpected(blending.error());
            if (!level_filter) return std::unexpected(level_filter.error());
            if (!image_filter) return std::unexpected(image_filter.error());
            if (!wrap_s) return std::unexpected(wrap_s.error());
            if (!wrap_t) return std::unexpected(wrap_t.error());
            return std::optional<Value> {};
        });
    register_reference_property(registry, kTexture2D, kTexture2D, "image",
        "Ljavax/microedition/m3g/Image2D;", "setImage", "getImage");
    register_int_property(registry, kTexture2D, kTexture2D, "blendColor",
                          "setBlendColor", "getBlendColor");
    register_int_property(registry, kTexture2D, kTexture2D, "blending",
                          "setBlending", "getBlending");
    add(registry, kTexture2D, "setFiltering", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Texture2D.setFiltering");
            auto level = int_argument(arguments, 1U, "Texture2D.setFiltering");
            auto image = int_argument(arguments, 2U, "Texture2D.setFiltering");
            if (!object) return std::unexpected(object.error());
            if (!level) return std::unexpected(level.error());
            if (!image) return std::unexpected(image.error());
            auto first = set_int_field(machine, *object, kTexture2D,
                                       "levelFilter", *level);
            auto second = set_int_field(machine, *object, kTexture2D,
                                        "imageFilter", *image);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    register_read_only_int(registry, kTexture2D, "levelFilter",
                           "getLevelFilter");
    register_read_only_int(registry, kTexture2D, "imageFilter",
                           "getImageFilter");
    add(registry, kTexture2D, "setWrapping", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Texture2D.setWrapping");
            auto s = int_argument(arguments, 1U, "Texture2D.setWrapping");
            auto t = int_argument(arguments, 2U, "Texture2D.setWrapping");
            if (!object) return std::unexpected(object.error());
            if (!s) return std::unexpected(s.error());
            if (!t) return std::unexpected(t.error());
            auto first = set_int_field(machine, *object, kTexture2D, "wrapS", *s);
            auto second = set_int_field(machine, *object, kTexture2D, "wrapT", *t);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    register_read_only_int(registry, kTexture2D, "wrapS", "getWrappingS");
    register_read_only_int(registry, kTexture2D, "wrapT", "getWrappingT");
}

void register_vertex_array(NativeMethodRegistry& registry) {
    add(registry, kVertexArray, "<init>", "(III)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "VertexArray.<init>");
            auto vertices = int_argument(arguments, 1U, "VertexArray.<init>");
            auto components = int_argument(arguments, 2U, "VertexArray.<init>");
            auto component_size = int_argument(arguments, 3U,
                                               "VertexArray.<init>");
            if (!object) return std::unexpected(object.error());
            if (!vertices) return std::unexpected(vertices.error());
            if (!components) return std::unexpected(components.error());
            if (!component_size) return std::unexpected(component_size.error());
            if (*vertices <= 0 || *components < 2 || *components > 4 ||
                (*component_size != 1 && *component_size != 2)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "VertexArray dimensions are invalid");
            }
            const usize element_count = static_cast<usize>(*vertices) *
                                        static_cast<usize>(*components);
            auto data = allocate_array(machine,
                *component_size == 1 ? "[B" : "[S",
                element_count, Value::from_int(0));
            if (!data) return std::unexpected(data.error());
            auto vertex_stored = set_int_field(machine, *object, kVertexArray,
                                               "vertexCount", *vertices);
            auto component_stored = set_int_field(machine, *object, kVertexArray,
                                                  "componentCount", *components);
            auto size_stored = set_int_field(machine, *object, kVertexArray,
                                             "componentSize", *component_size);
            auto data_stored = set_reference_field(machine, *object, kVertexArray,
                "data", "Ljava/lang/Object;", *data);
            if (!vertex_stored) return std::unexpected(vertex_stored.error());
            if (!component_stored) return std::unexpected(component_stored.error());
            if (!size_stored) return std::unexpected(size_stored.error());
            if (!data_stored) return std::unexpected(data_stored.error());
            return std::optional<Value> {};
        });
    register_read_only_int(registry, kVertexArray, "vertexCount",
                           "getVertexCount");
    register_read_only_int(registry, kVertexArray, "componentCount",
                           "getComponentCount");
    register_read_only_int(registry, kVertexArray, "componentSize",
                           "getComponentType");

    const auto register_copy = [&registry](const char* method,
                                           const char* descriptor,
                                           bool into_vertex_array) {
        add(registry, kVertexArray, method, descriptor,
            [into_vertex_array](Machine& machine,
                                std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "VertexArray copy");
                auto first_vertex = int_argument(arguments, 1U,
                                                 "VertexArray copy");
                auto vertex_count = int_argument(arguments, 2U,
                                                 "VertexArray copy");
                auto array = reference_argument(arguments, 3U,
                                                "VertexArray copy", false);
                if (!object) return std::unexpected(object.error());
                if (!first_vertex) return std::unexpected(first_vertex.error());
                if (!vertex_count) return std::unexpected(vertex_count.error());
                if (!array) return std::unexpected(array.error());
                auto total_vertices = int_field(machine, *object, kVertexArray,
                                                "vertexCount");
                auto components = int_field(machine, *object, kVertexArray,
                                            "componentCount");
                auto data = reference_field(machine, *object, kVertexArray,
                                            "data", "Ljava/lang/Object;");
                if (!total_vertices) return std::unexpected(total_vertices.error());
                if (!components) return std::unexpected(components.error());
                if (!data) return std::unexpected(data.error());
                if (*first_vertex < 0 || *vertex_count < 0 ||
                    *first_vertex > *total_vertices - *vertex_count) {
                    return fail_java("java/lang/IndexOutOfBoundsException",
                                     "VertexArray range is invalid");
                }
                const usize count = static_cast<usize>(*vertex_count) *
                                    static_cast<usize>(*components);
                auto external_length = machine.heap().array_length(*array);
                if (!external_length) return std::unexpected(external_length.error());
                if (*external_length < count) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "VertexArray external array is too short");
                }
                const usize offset = static_cast<usize>(*first_vertex) *
                                     static_cast<usize>(*components);
                for (usize index = 0; index < count; ++index) {
                    auto value = into_vertex_array
                        ? machine.heap().element(*array, index)
                        : machine.heap().element(*data, offset + index);
                    if (!value) return std::unexpected(value.error());
                    auto stored = into_vertex_array
                        ? machine.heap().set_element(*data, offset + index, *value)
                        : machine.heap().set_element(*array, index, *value);
                    if (!stored) return std::unexpected(stored.error());
                }
                return std::optional<Value> {};
            });
    };
    register_copy("set", "(II[B)V", true);
    register_copy("set", "(II[S)V", true);
    register_copy("get", "(II[B)V", false);
    register_copy("get", "(II[S)V", false);
}

void register_vertex_buffer(NativeMethodRegistry& registry) {
    add(registry, kVertexBuffer, "<init>", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
    register_read_only_int(registry, kVertexBuffer, "vertexCount",
                           "getVertexCount");
    register_int_property(registry, kVertexBuffer, kVertexBuffer,
                          "defaultColor", "setDefaultColor", "getDefaultColor");
    register_reference_property(registry, kVertexBuffer, kVertexBuffer,
        "normals", "Ljavax/microedition/m3g/VertexArray;",
        "setNormals", "getNormals");
    register_reference_property(registry, kVertexBuffer, kVertexBuffer,
        "colors", "Ljavax/microedition/m3g/VertexArray;",
        "setColors", "getColors");
    add(registry, kVertexBuffer, "setPositions",
        "(Ljavax/microedition/m3g/VertexArray;F[F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "VertexBuffer.setPositions");
            auto positions = reference_argument(arguments, 1U,
                                                "VertexBuffer.setPositions");
            auto scale = float_argument(arguments, 2U,
                                        "VertexBuffer.setPositions");
            auto bias = reference_argument(arguments, 3U,
                                           "VertexBuffer.setPositions");
            if (!object) return std::unexpected(object.error());
            if (!positions) return std::unexpected(positions.error());
            if (!scale) return std::unexpected(scale.error());
            if (!bias) return std::unexpected(bias.error());
            auto positions_stored = set_reference_field(machine, *object,
                kVertexBuffer, "positions",
                "Ljavax/microedition/m3g/VertexArray;", *positions);
            auto scale_stored = set_float_field(machine, *object, kVertexBuffer,
                                                "positionScale", *scale);
            auto bias_stored = set_reference_field(machine, *object,
                kVertexBuffer, "positionBias", "[F", *bias);
            if (!positions_stored) return std::unexpected(positions_stored.error());
            if (!scale_stored) return std::unexpected(scale_stored.error());
            if (!bias_stored) return std::unexpected(bias_stored.error());
            if (!positions->is_null()) {
                auto count = int_field(machine, *positions, kVertexArray,
                                       "vertexCount");
                if (!count) return std::unexpected(count.error());
                auto count_stored = set_int_field(machine, *object,
                                                  kVertexBuffer,
                                                  "vertexCount", *count);
                if (!count_stored) return std::unexpected(count_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kVertexBuffer, "getPositions",
        "([F)Ljavax/microedition/m3g/VertexArray;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "VertexBuffer.getPositions");
            auto destination = reference_argument(arguments, 1U,
                                                  "VertexBuffer.getPositions");
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto scale = float_field(machine, *object, kVertexBuffer,
                                     "positionScale");
            auto bias = reference_field(machine, *object, kVertexBuffer,
                                        "positionBias", "[F");
            auto positions = reference_field(machine, *object, kVertexBuffer,
                "positions", "Ljavax/microedition/m3g/VertexArray;");
            if (!scale) return std::unexpected(scale.error());
            if (!bias) return std::unexpected(bias.error());
            if (!positions) return std::unexpected(positions.error());
            if (!destination->is_null()) {
                std::array<float, 4> values {*scale, 0.0F, 0.0F, 0.0F};
                if (!bias->is_null()) {
                    auto bias_values = read_float_array(machine, *bias,
                                                        "VertexBuffer.getPositions");
                    if (!bias_values) return std::unexpected(bias_values.error());
                    for (usize index = 0;
                         index < std::min<usize>(3U, bias_values->size());
                         ++index) {
                        values[index + 1U] = (*bias_values)[index];
                    }
                }
                auto stored = write_float_array(machine, *destination, values);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_reference(*positions));
        });
    add(registry, kVertexBuffer, "setTexCoords",
        "(ILjavax/microedition/m3g/VertexArray;F[F)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
    add(registry, kVertexBuffer, "getTexCoords",
        "(I[F)Ljavax/microedition/m3g/VertexArray;",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_reference({}));
        });
}

void register_index_buffers(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kIndexBuffer, "()V");
    add(registry, kIndexBuffer, "getIndexCount", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "IndexBuffer.getIndexCount");
            if (!object) return std::unexpected(object.error());
            auto indices = reference_field(machine, *object, kIndexBuffer,
                                            "indices", "[I");
            if (!indices) return std::unexpected(indices.error());
            if (indices->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto length = machine.heap().array_length(*indices);
            if (!length) return std::unexpected(length.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(*length)));
        });
    add(registry, kIndexBuffer, "getIndices", "([I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "IndexBuffer.getIndices");
            auto destination = reference_argument(arguments, 1U,
                                                  "IndexBuffer.getIndices", false);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto indices = reference_field(machine, *object, kIndexBuffer,
                                            "indices", "[I");
            if (!indices) return std::unexpected(indices.error());
            if (indices->is_null()) return std::optional<Value> {};
            auto count = machine.heap().array_length(*indices);
            auto destination_length = machine.heap().array_length(*destination);
            if (!count) return std::unexpected(count.error());
            if (!destination_length) return std::unexpected(destination_length.error());
            if (*destination_length < *count) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "IndexBuffer destination is too short");
            }
            for (usize index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*indices, index);
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(*destination, index, *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });

    const auto register_triangle = [&registry](const char* descriptor,
                                               bool explicit_indices) {
        add(registry, kTriangleStrip, "<init>", descriptor,
            [explicit_indices](Machine& machine,
                               std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "TriangleStripArray.<init>");
                auto source = reference_argument(arguments,
                    explicit_indices ? 1U : 2U,
                    "TriangleStripArray.<init>", false);
                if (!object) return std::unexpected(object.error());
                if (!source) return std::unexpected(source.error());
                auto source_length = machine.heap().array_length(*source);
                if (!source_length) return std::unexpected(source_length.error());
                usize count = 0U;
                if (explicit_indices) {
                    count = *source_length;
                } else {
                    for (usize index = 0; index < *source_length; ++index) {
                        auto value = machine.heap().element(*source, index);
                        if (!value) return std::unexpected(value.error());
                        auto length = value->as_int();
                        if (!length) return std::unexpected(length.error());
                        if (*length < 3) {
                            return fail_java("java/lang/IllegalArgumentException",
                                             "Triangle strip is too short");
                        }
                        count += static_cast<usize>(*length);
                    }
                }
                auto indices = allocate_array(machine, "[I", count,
                                              Value::from_int(0));
                if (!indices) return std::unexpected(indices.error());
                if (explicit_indices) {
                    for (usize index = 0; index < count; ++index) {
                        auto value = machine.heap().element(*source, index);
                        if (!value) return std::unexpected(value.error());
                        auto stored = machine.heap().set_element(*indices,
                                                                  index, *value);
                        if (!stored) return std::unexpected(stored.error());
                    }
                } else {
                    auto first = int_argument(arguments, 1U,
                                              "TriangleStripArray.<init>");
                    if (!first) return std::unexpected(first.error());
                    for (usize index = 0; index < count; ++index) {
                        auto stored = machine.heap().set_element(
                            *indices, index,
                            Value::from_int(*first + static_cast<i32>(index)));
                        if (!stored) return std::unexpected(stored.error());
                    }
                }
                auto stored = set_reference_field(machine, *object,
                    kIndexBuffer, "indices", "[I", *indices);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    register_triangle("(I[I)V", false);
    register_triangle("([I[I)V", true);
}

void register_mesh(NativeMethodRegistry& registry) {
    const auto register_constructor = [&registry](const char* descriptor,
                                                  bool arrays) {
        add(registry, kMesh, "<init>", descriptor,
            [arrays](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Mesh.<init>");
                auto vertices = reference_argument(arguments, 1U,
                                                   "Mesh.<init>", false);
                auto indices = reference_argument(arguments, 2U,
                                                  "Mesh.<init>", false);
                auto appearances = reference_argument(arguments, 3U,
                                                      "Mesh.<init>");
                if (!object) return std::unexpected(object.error());
                if (!vertices) return std::unexpected(vertices.error());
                if (!indices) return std::unexpected(indices.error());
                if (!appearances) return std::unexpected(appearances.error());
                auto initialized = initialize_node(machine, *object);
                if (!initialized) return std::unexpected(initialized.error());

                ObjectRef index_array = *indices;
                ObjectRef appearance_array = *appearances;
                if (!arrays) {
                    auto index_wrapper = allocate_array(machine,
                        "[Ljavax/microedition/m3g/IndexBuffer;", 1U,
                        Value::from_reference({}));
                    auto appearance_wrapper = allocate_array(machine,
                        "[Ljavax/microedition/m3g/Appearance;", 1U,
                        Value::from_reference({}));
                    if (!index_wrapper) return std::unexpected(index_wrapper.error());
                    if (!appearance_wrapper) {
                        return std::unexpected(appearance_wrapper.error());
                    }
                    auto index_stored = machine.heap().set_element(
                        *index_wrapper, 0U, Value::from_reference(*indices));
                    auto appearance_stored = machine.heap().set_element(
                        *appearance_wrapper, 0U,
                        Value::from_reference(*appearances));
                    if (!index_stored) return std::unexpected(index_stored.error());
                    if (!appearance_stored) {
                        return std::unexpected(appearance_stored.error());
                    }
                    index_array = *index_wrapper;
                    appearance_array = *appearance_wrapper;
                }
                auto vertices_stored = set_reference_field(machine, *object,
                    kMesh, "vertexBuffer",
                    "Ljavax/microedition/m3g/VertexBuffer;", *vertices);
                auto indices_stored = set_reference_field(machine, *object,
                    kMesh, "indexBuffers",
                    "[Ljavax/microedition/m3g/IndexBuffer;", index_array);
                auto appearances_stored = set_reference_field(machine, *object,
                    kMesh, "appearances",
                    "[Ljavax/microedition/m3g/Appearance;", appearance_array);
                if (!vertices_stored) return std::unexpected(vertices_stored.error());
                if (!indices_stored) return std::unexpected(indices_stored.error());
                if (!appearances_stored) {
                    return std::unexpected(appearances_stored.error());
                }
                return std::optional<Value> {};
            });
    };
    register_constructor(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;)V", false);
    register_constructor(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/IndexBuffer;"
        "[Ljavax/microedition/m3g/Appearance;)V", true);

    register_reference_property(registry, kMesh, kMesh, "vertexBuffer",
        "Ljavax/microedition/m3g/VertexBuffer;",
        "__setVertexBuffer", "getVertexBuffer");
    add(registry, kMesh, "getSubmeshCount", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Mesh.getSubmeshCount");
            if (!object) return std::unexpected(object.error());
            auto indices = reference_field(machine, *object, kMesh,
                "indexBuffers", "[Ljavax/microedition/m3g/IndexBuffer;");
            if (!indices) return std::unexpected(indices.error());
            auto length = machine.heap().array_length(*indices);
            if (!length) return std::unexpected(length.error());
            return std::optional<Value>(
                Value::from_int(static_cast<i32>(*length)));
        });
    const auto register_array_getter = [&registry](const char* method,
                                                   const char* field,
                                                   const char* array_descriptor,
                                                   const char* result_descriptor) {
        add(registry, kMesh, method,
            std::string("(I)") + result_descriptor,
            [field, array_descriptor](Machine& machine,
                                      std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Mesh getter");
                auto index = int_argument(arguments, 1U, "Mesh getter");
                if (!object) return std::unexpected(object.error());
                if (!index) return std::unexpected(index.error());
                auto array = reference_field(machine, *object, kMesh,
                                             field, array_descriptor);
                if (!array) return std::unexpected(array.error());
                auto length = machine.heap().array_length(*array);
                if (!length) return std::unexpected(length.error());
                if (*index < 0 || static_cast<usize>(*index) >= *length) {
                    return fail_java("java/lang/IndexOutOfBoundsException",
                                     "Mesh submesh index is out of range");
                }
                auto value = machine.heap().element(*array,
                                                    static_cast<usize>(*index));
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(*value);
            });
    };
    register_array_getter("getIndexBuffer", "indexBuffers",
        "[Ljavax/microedition/m3g/IndexBuffer;",
        "Ljavax/microedition/m3g/IndexBuffer;");
    register_array_getter("getAppearance", "appearances",
        "[Ljavax/microedition/m3g/Appearance;",
        "Ljavax/microedition/m3g/Appearance;");
    add(registry, kMesh, "setAppearance",
        "(ILjavax/microedition/m3g/Appearance;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Mesh.setAppearance");
            auto index = int_argument(arguments, 1U, "Mesh.setAppearance");
            auto appearance = reference_argument(arguments, 2U,
                                                 "Mesh.setAppearance");
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            if (!appearance) return std::unexpected(appearance.error());
            auto array = reference_field(machine, *object, kMesh,
                "appearances", "[Ljavax/microedition/m3g/Appearance;");
            if (!array) return std::unexpected(array.error());
            auto length = machine.heap().array_length(*array);
            if (!length) return std::unexpected(length.error());
            if (*index < 0 || static_cast<usize>(*index) >= *length) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Mesh appearance index is out of range");
            }
            auto stored = machine.heap().set_element(
                *array, static_cast<usize>(*index),
                Value::from_reference(*appearance));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
}

void register_sprite(NativeMethodRegistry& registry) {
    add(registry, kSprite3D, "<init>",
        "(ZLjavax/microedition/m3g/Image2D;"
        "Ljavax/microedition/m3g/Appearance;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Sprite3D.<init>");
            auto scaled = int_argument(arguments, 1U, "Sprite3D.<init>");
            auto image = reference_argument(arguments, 2U,
                                            "Sprite3D.<init>", false);
            auto appearance = reference_argument(arguments, 3U,
                                                 "Sprite3D.<init>");
            if (!object) return std::unexpected(object.error());
            if (!scaled) return std::unexpected(scaled.error());
            if (!image) return std::unexpected(image.error());
            if (!appearance) return std::unexpected(appearance.error());
            auto initialized = initialize_node(machine, *object);
            if (!initialized) return std::unexpected(initialized.error());
            auto scaled_stored = set_int_field(machine, *object, kSprite3D,
                                               "scaled", *scaled, "Z");
            auto image_stored = set_reference_field(machine, *object, kSprite3D,
                "image", "Ljavax/microedition/m3g/Image2D;", *image);
            auto appearance_stored = set_reference_field(machine, *object,
                kSprite3D, "appearance",
                "Ljavax/microedition/m3g/Appearance;", *appearance);
            if (!scaled_stored) return std::unexpected(scaled_stored.error());
            if (!image_stored) return std::unexpected(image_stored.error());
            if (!appearance_stored) {
                return std::unexpected(appearance_stored.error());
            }
            return std::optional<Value> {};
        });
    register_read_only_int(registry, kSprite3D, "scaled", "isScaled", "Z");
    register_reference_property(registry, kSprite3D, kSprite3D, "image",
        "Ljavax/microedition/m3g/Image2D;", "setImage", "getImage");
    register_reference_property(registry, kSprite3D, kSprite3D, "appearance",
        "Ljavax/microedition/m3g/Appearance;",
        "setAppearance", "getAppearance");
    add(registry, kSprite3D, "setCrop", "(IIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Sprite3D.setCrop");
            if (!object) return std::unexpected(object.error());
            const std::array<const char*, 4> fields {
                "cropX", "cropY", "cropWidth", "cropHeight",
            };
            for (usize index = 0; index < fields.size(); ++index) {
                auto value = int_argument(arguments, index + 1U,
                                          "Sprite3D.setCrop");
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(machine, *object, kSprite3D,
                                            fields[index], *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    register_read_only_int(registry, kSprite3D, "cropX", "getCropX");
    register_read_only_int(registry, kSprite3D, "cropY", "getCropY");
    register_read_only_int(registry, kSprite3D, "cropWidth", "getCropWidth");
    register_read_only_int(registry, kSprite3D, "cropHeight", "getCropHeight");
}

void register_special_meshes(NativeMethodRegistry& registry) {
    constexpr const char* skinned = "javax/microedition/m3g/SkinnedMesh";
    register_noop_constructor(registry, skinned,
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;"
        "Ljavax/microedition/m3g/Group;)V", initialize_node);
    register_noop_constructor(registry, skinned,
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/IndexBuffer;"
        "[Ljavax/microedition/m3g/Appearance;"
        "Ljavax/microedition/m3g/Group;)V", initialize_node);
    add(registry, skinned, "addTransform",
        "(Ljavax/microedition/m3g/Node;III)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
    add(registry, skinned, "getBoneTransform",
        "(Ljavax/microedition/m3g/Node;"
        "Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto destination = reference_argument(arguments, 2U,
                                                  "SkinnedMesh.getBoneTransform",
                                                  false);
            if (!destination) return std::unexpected(destination.error());
            auto stored = set_transform_matrix(machine, *destination,
                                               identity_matrix());
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, skinned, "getBoneVertices",
        "(Ljavax/microedition/m3g/Node;[I[F)I",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, skinned, "getSkeleton",
        "()Ljavax/microedition/m3g/Group;",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_reference({}));
        });

    constexpr const char* morphing = "javax/microedition/m3g/MorphingMesh";
    register_noop_constructor(registry, morphing,
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;)V", initialize_node);
    register_noop_constructor(registry, morphing,
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/IndexBuffer;"
        "[Ljavax/microedition/m3g/Appearance;)V", initialize_node);
    for (const char* method : {"setWeights", "getWeights"}) {
        add(registry, morphing, method, "([F)V",
            [](Machine&, std::span<const Value>)
                -> Result<std::optional<Value>> {
                return std::optional<Value> {};
            });
    }
    add(registry, morphing, "getMorphTargetCount", "()I",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, morphing, "getMorphTarget",
        "(I)Ljavax/microedition/m3g/VertexBuffer;",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_reference({}));
        });
}

} // namespace

void register_m3g_geometry_natives(NativeMethodRegistry& registry) {
    register_image2d(registry);
    register_texture2d(registry);
    register_vertex_array(registry);
    register_vertex_buffer(registry);
    register_index_buffers(registry);
    register_mesh(registry);
    register_sprite(registry);
    register_special_meshes(registry);
}

} // namespace phoneme::vm
