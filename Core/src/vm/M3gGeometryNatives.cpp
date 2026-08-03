#include "M3gNativeModules.hpp"

#include <algorithm>
#include <array>
#include <limits>
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

[[nodiscard]] Result<usize> image_bytes_per_pixel(i32 format) {
    switch (format) {
    case 96: // ALPHA
    case 97: // LUMINANCE
        return 1U;
    case 98: // LUMINANCE_ALPHA
        return 2U;
    case 99: // RGB
        return 3U;
    case 100: // RGBA
        return 4U;
    default:
        return fail_java("java/lang/IllegalArgumentException",
                         "unsupported Image2D format");
    }
}

[[nodiscard]] Result<ObjectRef> copy_array(Machine& machine,
                                           ObjectRef source,
                                           std::string class_name,
                                           Value initial) {
    auto length = machine.heap().array_length(source);
    if (!length) return std::unexpected(length.error());
    auto destination = allocate_array(machine, std::move(class_name),
                                      *length, initial);
    if (!destination) return std::unexpected(destination.error());
    for (usize index = 0; index < *length; ++index) {
        auto value = machine.heap().element(source, index);
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(*destination, index, *value);
        if (!stored) return std::unexpected(stored.error());
    }
    return *destination;
}

[[nodiscard]] Result<ObjectRef> append_array_value(
    Machine& machine,
    ObjectRef existing,
    std::string class_name,
    Value initial,
    Value appended) {
    usize old_length = 0U;
    if (!existing.is_null()) {
        auto length = machine.heap().array_length(existing);
        if (!length) return std::unexpected(length.error());
        old_length = *length;
    }
    if (old_length == std::numeric_limits<usize>::max()) {
        return fail_java("java/lang/OutOfMemoryError",
                         "M3G state array is too large");
    }
    auto result = allocate_array(machine, std::move(class_name),
                                 old_length + 1U, initial);
    if (!result) return std::unexpected(result.error());
    for (usize index = 0; index < old_length; ++index) {
        auto value = machine.heap().element(existing, index);
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(*result, index, *value);
        if (!stored) return std::unexpected(stored.error());
    }
    auto stored = machine.heap().set_element(*result, old_length, appended);
    if (!stored) return std::unexpected(stored.error());
    return *result;
}

[[nodiscard]] Result<usize> checked_image_byte_count(i32 width,
                                                     i32 height,
                                                     usize components) {
    if (width <= 0 || height <= 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Image2D dimensions must be positive");
    }
    const u64 count = static_cast<u64>(width) *
                      static_cast<u64>(height) *
                      static_cast<u64>(components);
    if (count > static_cast<u64>(std::numeric_limits<usize>::max())) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Image2D dimensions are too large");
    }
    return static_cast<usize>(count);
}

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
                auto components = image_bytes_per_pixel(*format);
                if (!components) return std::unexpected(components.error());
                auto full_size = checked_image_byte_count(*width, *height,
                                                          *components);
                if (!full_size) return std::unexpected(full_size.error());

                ObjectRef source {};
                ObjectRef palette {};
                if (mutable_image) {
                    auto allocated = allocate_array(machine, "[B", *full_size,
                                                    Value::from_int(0));
                    if (!allocated) return std::unexpected(allocated.error());
                    source = *allocated;
                } else {
                    auto pixels = reference_argument(arguments, 4U,
                                                     "Image2D.<init>", false);
                    if (!pixels) return std::unexpected(pixels.error());
                    auto pixel_length = machine.heap().array_length(*pixels);
                    if (!pixel_length) {
                        return std::unexpected(pixel_length.error());
                    }
                    const bool indexed = arguments.size() == 6U;
                    const usize required = indexed
                        ? static_cast<usize>(*width) * static_cast<usize>(*height)
                        : *full_size;
                    if (*pixel_length < required) {
                        return fail_java("java/lang/IllegalArgumentException",
                                         "Image2D pixel array is too short");
                    }
                    auto copied = copy_array(machine, *pixels, "[B",
                                             Value::from_int(0));
                    if (!copied) return std::unexpected(copied.error());
                    source = *copied;
                    if (indexed) {
                        auto palette_argument = reference_argument(
                            arguments, 5U, "Image2D.<init>", false);
                        if (!palette_argument) {
                            return std::unexpected(palette_argument.error());
                        }
                        auto palette_length =
                            machine.heap().array_length(*palette_argument);
                        if (!palette_length) {
                            return std::unexpected(palette_length.error());
                        }
                        if (*palette_length == 0U ||
                            (*palette_length % *components) != 0U ||
                            (*palette_length / *components) > 256U) {
                            return fail_java(
                                "java/lang/IllegalArgumentException",
                                "Image2D palette size is invalid");
                        }
                        auto copied_palette = copy_array(
                            machine, *palette_argument, "[B",
                            Value::from_int(0));
                        if (!copied_palette) {
                            return std::unexpected(copied_palette.error());
                        }
                        palette = *copied_palette;
                    }
                }

                const std::array<Status, 6> stored {
                    set_int_field(machine, *object, kImage2D,
                                  "format", *format),
                    set_int_field(machine, *object, kImage2D,
                                  "width", *width),
                    set_int_field(machine, *object, kImage2D,
                                  "height", *height),
                    set_int_field(machine, *object, kImage2D,
                                  "mutable", mutable_image ? 1 : 0, "Z"),
                    set_reference_field(machine, *object, kImage2D,
                                        "source", "Ljava/lang/Object;", source),
                    set_reference_field(machine, *object, kImage2D,
                                        "palette", "[B", palette),
                };
                for (const Status& status : stored) {
                    if (!status) return std::unexpected(status.error());
                }
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
    add(registry, kImage2D, "set", "(IIII[B)V",
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
            auto x = int_argument(arguments, 1U, "Image2D.set");
            auto y = int_argument(arguments, 2U, "Image2D.set");
            auto width = int_argument(arguments, 3U, "Image2D.set");
            auto height = int_argument(arguments, 4U, "Image2D.set");
            auto pixels = reference_argument(arguments, 5U,
                                             "Image2D.set", false);
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!width) return std::unexpected(width.error());
            if (!height) return std::unexpected(height.error());
            if (!pixels) return std::unexpected(pixels.error());

            auto image_width = int_field(machine, *object, kImage2D, "width");
            auto image_height = int_field(machine, *object, kImage2D, "height");
            auto format = int_field(machine, *object, kImage2D, "format");
            if (!image_width) return std::unexpected(image_width.error());
            if (!image_height) return std::unexpected(image_height.error());
            if (!format) return std::unexpected(format.error());
            if (*x < 0 || *y < 0 || *width <= 0 || *height <= 0 ||
                static_cast<i64>(*x) + static_cast<i64>(*width) >
                    static_cast<i64>(*image_width) ||
                static_cast<i64>(*y) + static_cast<i64>(*height) >
                    static_cast<i64>(*image_height)) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Image2D update region is out of bounds");
            }
            auto components = image_bytes_per_pixel(*format);
            if (!components) return std::unexpected(components.error());
            auto update_size = checked_image_byte_count(*width, *height,
                                                        *components);
            if (!update_size) return std::unexpected(update_size.error());
            auto pixel_length = machine.heap().array_length(*pixels);
            if (!pixel_length) return std::unexpected(pixel_length.error());
            if (*pixel_length < *update_size) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Image2D update array is too short");
            }

            auto source = reference_field(machine, *object, kImage2D,
                                           "source", "Ljava/lang/Object;");
            if (!source) return std::unexpected(source.error());
            if (source->is_null()) {
                auto full_size = checked_image_byte_count(
                    *image_width, *image_height, *components);
                if (!full_size) return std::unexpected(full_size.error());
                auto allocated = allocate_array(machine, "[B", *full_size,
                                                Value::from_int(0));
                if (!allocated) return std::unexpected(allocated.error());
                *source = *allocated;
                auto stored = set_reference_field(
                    machine, *object, kImage2D, "source",
                    "Ljava/lang/Object;", *source);
                if (!stored) return std::unexpected(stored.error());
            }
            auto source_length = machine.heap().array_length(*source);
            if (!source_length) return std::unexpected(source_length.error());
            auto full_size = checked_image_byte_count(
                *image_width, *image_height, *components);
            if (!full_size) return std::unexpected(full_size.error());
            if (*source_length < *full_size) {
                return fail(ErrorCode::invalid_state,
                            "Image2D backing array is truncated");
            }
            for (i32 row = 0; row < *height; ++row) {
                for (i32 column = 0; column < *width; ++column) {
                    for (usize component = 0; component < *components;
                         ++component) {
                        const usize input_index =
                            (static_cast<usize>(row) *
                                 static_cast<usize>(*width) +
                             static_cast<usize>(column)) * *components +
                            component;
                        const usize output_index =
                            ((static_cast<usize>(*y + row) *
                                  static_cast<usize>(*image_width)) +
                             static_cast<usize>(*x + column)) * *components +
                            component;
                        auto value = machine.heap().element(*pixels,
                                                            input_index);
                        if (!value) return std::unexpected(value.error());
                        auto stored = machine.heap().set_element(
                            *source, output_index, *value);
                        if (!stored) return std::unexpected(stored.error());
                    }
                }
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

[[nodiscard]] Status initialize_mesh_geometry(
    Machine& machine,
    ObjectRef object,
    ObjectRef vertices,
    ObjectRef indices,
    ObjectRef appearances,
    bool arrays) {
    auto initialized = initialize_node(machine, object);
    if (!initialized) return initialized;

    ObjectRef index_array = indices;
    ObjectRef appearance_array = appearances;
    if (!arrays) {
        auto index_wrapper = allocate_array(
            machine, "[Ljavax/microedition/m3g/IndexBuffer;", 1U,
            Value::from_reference({}));
        auto appearance_wrapper = allocate_array(
            machine, "[Ljavax/microedition/m3g/Appearance;", 1U,
            Value::from_reference({}));
        if (!index_wrapper) return std::unexpected(index_wrapper.error());
        if (!appearance_wrapper) {
            return std::unexpected(appearance_wrapper.error());
        }
        auto index_stored = machine.heap().set_element(
            *index_wrapper, 0U, Value::from_reference(indices));
        auto appearance_stored = machine.heap().set_element(
            *appearance_wrapper, 0U, Value::from_reference(appearances));
        if (!index_stored) return index_stored;
        if (!appearance_stored) return appearance_stored;
        index_array = *index_wrapper;
        appearance_array = *appearance_wrapper;
    } else {
        auto index_count = machine.heap().array_length(indices);
        if (!index_count) return std::unexpected(index_count.error());
        if (appearances.is_null()) {
            auto allocated = allocate_array(
                machine, "[Ljavax/microedition/m3g/Appearance;", *index_count,
                Value::from_reference({}));
            if (!allocated) return std::unexpected(allocated.error());
            appearance_array = *allocated;
        } else {
            auto appearance_count = machine.heap().array_length(appearances);
            if (!appearance_count) {
                return std::unexpected(appearance_count.error());
            }
            if (*appearance_count != *index_count) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Mesh appearance count does not match submeshes");
            }
        }
    }

    const std::array<Status, 3> stored {
        set_reference_field(machine, object, kMesh, "vertexBuffer",
            "Ljavax/microedition/m3g/VertexBuffer;", vertices),
        set_reference_field(machine, object, kMesh, "indexBuffers",
            "[Ljavax/microedition/m3g/IndexBuffer;", index_array),
        set_reference_field(machine, object, kMesh, "appearances",
            "[Ljavax/microedition/m3g/Appearance;", appearance_array),
    };
    for (const Status& status : stored) {
        if (!status) return status;
    }
    return {};
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
    const auto register_skinned_constructor =
        [&registry](const char* descriptor, bool arrays) {
            add(registry, skinned, "<init>", descriptor,
                [arrays](Machine& machine,
                                  std::span<const Value> arguments)
                    -> Result<std::optional<Value>> {
                    auto object = receiver(arguments, "SkinnedMesh.<init>");
                    auto vertices = reference_argument(
                        arguments, 1U, "SkinnedMesh.<init>", false);
                    auto indices = reference_argument(
                        arguments, 2U, "SkinnedMesh.<init>", false);
                    auto appearances = reference_argument(
                        arguments, 3U, "SkinnedMesh.<init>");
                    auto skeleton = reference_argument(
                        arguments, 4U, "SkinnedMesh.<init>", false);
                    if (!object) return std::unexpected(object.error());
                    if (!vertices) return std::unexpected(vertices.error());
                    if (!indices) return std::unexpected(indices.error());
                    if (!appearances) return std::unexpected(appearances.error());
                    if (!skeleton) return std::unexpected(skeleton.error());
                    auto initialized = initialize_mesh_geometry(
                        machine, *object, *vertices, *indices, *appearances,
                        arrays);
                    if (!initialized) return std::unexpected(initialized.error());
                    auto stored = set_reference_field(
                        machine, *object, skinned, "skeleton",
                        "Ljavax/microedition/m3g/Group;", *skeleton);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value> {};
                });
        };
    register_skinned_constructor(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;"
        "Ljavax/microedition/m3g/Group;)V", false);
    register_skinned_constructor(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/IndexBuffer;"
        "[Ljavax/microedition/m3g/Appearance;"
        "Ljavax/microedition/m3g/Group;)V", true);
    add(registry, skinned, "addTransform",
        "(Ljavax/microedition/m3g/Node;III)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "SkinnedMesh.addTransform");
            auto bone = reference_argument(arguments, 1U,
                                           "SkinnedMesh.addTransform", false);
            auto weight = int_argument(arguments, 2U,
                                       "SkinnedMesh.addTransform");
            auto first_vertex = int_argument(arguments, 3U,
                                             "SkinnedMesh.addTransform");
            auto vertex_count = int_argument(arguments, 4U,
                                             "SkinnedMesh.addTransform");
            if (!object) return std::unexpected(object.error());
            if (!bone) return std::unexpected(bone.error());
            if (!weight) return std::unexpected(weight.error());
            if (!first_vertex) return std::unexpected(first_vertex.error());
            if (!vertex_count) return std::unexpected(vertex_count.error());
            if (*weight <= 0 || *weight > 255 || *first_vertex < 0 ||
                *vertex_count <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "SkinnedMesh transform range is invalid");
            }

            auto bones = reference_field(
                machine, *object, skinned, "bones",
                "[Ljavax/microedition/m3g/Node;");
            auto first_vertices = reference_field(
                machine, *object, skinned, "boneFirstVertices", "[I");
            auto vertex_counts = reference_field(
                machine, *object, skinned, "boneVertexCounts", "[I");
            auto weights = reference_field(
                machine, *object, skinned, "boneWeights", "[I");
            if (!bones) return std::unexpected(bones.error());
            if (!first_vertices) return std::unexpected(first_vertices.error());
            if (!vertex_counts) return std::unexpected(vertex_counts.error());
            if (!weights) return std::unexpected(weights.error());

            auto next_bones = append_array_value(
                machine, *bones, "[Ljavax/microedition/m3g/Node;",
                Value::from_reference({}), Value::from_reference(*bone));
            auto next_first = append_array_value(
                machine, *first_vertices, "[I", Value::from_int(0),
                Value::from_int(*first_vertex));
            auto next_counts = append_array_value(
                machine, *vertex_counts, "[I", Value::from_int(0),
                Value::from_int(*vertex_count));
            auto next_weights = append_array_value(
                machine, *weights, "[I", Value::from_int(0),
                Value::from_int(*weight));
            if (!next_bones) return std::unexpected(next_bones.error());
            if (!next_first) return std::unexpected(next_first.error());
            if (!next_counts) return std::unexpected(next_counts.error());
            if (!next_weights) return std::unexpected(next_weights.error());
            const std::array<Status, 4> stored {
                set_reference_field(machine, *object, skinned, "bones",
                    "[Ljavax/microedition/m3g/Node;", *next_bones),
                set_reference_field(machine, *object, skinned,
                    "boneFirstVertices", "[I", *next_first),
                set_reference_field(machine, *object, skinned,
                    "boneVertexCounts", "[I", *next_counts),
                set_reference_field(machine, *object, skinned,
                    "boneWeights", "[I", *next_weights),
            };
            for (const Status& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
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
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "SkinnedMesh.getBoneVertices");
            auto bone = reference_argument(arguments, 1U,
                                           "SkinnedMesh.getBoneVertices", false);
            auto destination_indices = reference_argument(
                arguments, 2U, "SkinnedMesh.getBoneVertices");
            auto destination_weights = reference_argument(
                arguments, 3U, "SkinnedMesh.getBoneVertices");
            if (!object) return std::unexpected(object.error());
            if (!bone) return std::unexpected(bone.error());
            if (!destination_indices) {
                return std::unexpected(destination_indices.error());
            }
            if (!destination_weights) {
                return std::unexpected(destination_weights.error());
            }
            auto bones = reference_field(
                machine, *object, skinned, "bones",
                "[Ljavax/microedition/m3g/Node;");
            auto first_vertices = reference_field(
                machine, *object, skinned, "boneFirstVertices", "[I");
            auto vertex_counts = reference_field(
                machine, *object, skinned, "boneVertexCounts", "[I");
            auto weights = reference_field(
                machine, *object, skinned, "boneWeights", "[I");
            if (!bones) return std::unexpected(bones.error());
            if (!first_vertices) return std::unexpected(first_vertices.error());
            if (!vertex_counts) return std::unexpected(vertex_counts.error());
            if (!weights) return std::unexpected(weights.error());
            if (bones->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto transform_count = machine.heap().array_length(*bones);
            if (!transform_count) {
                return std::unexpected(transform_count.error());
            }
            usize result_count = 0U;
            for (usize index = 0; index < *transform_count; ++index) {
                auto stored_bone = machine.heap().element(*bones, index);
                auto count_value = machine.heap().element(*vertex_counts, index);
                if (!stored_bone) return std::unexpected(stored_bone.error());
                if (!count_value) return std::unexpected(count_value.error());
                auto stored_reference = stored_bone->as_reference();
                auto count = count_value->as_int();
                if (!stored_reference) {
                    return std::unexpected(stored_reference.error());
                }
                if (!count) return std::unexpected(count.error());
                if (*stored_reference == *bone) {
                    result_count += static_cast<usize>(*count);
                }
            }
            if (!destination_indices->is_null()) {
                auto length = machine.heap().array_length(*destination_indices);
                if (!length) return std::unexpected(length.error());
                if (*length < result_count) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "bone vertex destination is too short");
                }
            }
            if (!destination_weights->is_null()) {
                auto length = machine.heap().array_length(*destination_weights);
                if (!length) return std::unexpected(length.error());
                if (*length < result_count) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "bone weight destination is too short");
                }
            }
            usize output = 0U;
            for (usize index = 0; index < *transform_count; ++index) {
                auto stored_bone = machine.heap().element(*bones, index);
                auto first_value = machine.heap().element(*first_vertices, index);
                auto count_value = machine.heap().element(*vertex_counts, index);
                auto weight_value = machine.heap().element(*weights, index);
                if (!stored_bone) return std::unexpected(stored_bone.error());
                if (!first_value) return std::unexpected(first_value.error());
                if (!count_value) return std::unexpected(count_value.error());
                if (!weight_value) return std::unexpected(weight_value.error());
                auto stored_reference = stored_bone->as_reference();
                auto first = first_value->as_int();
                auto count = count_value->as_int();
                auto weight = weight_value->as_int();
                if (!stored_reference) {
                    return std::unexpected(stored_reference.error());
                }
                if (!first) return std::unexpected(first.error());
                if (!count) return std::unexpected(count.error());
                if (!weight) return std::unexpected(weight.error());
                if (*stored_reference != *bone) continue;
                for (i32 offset = 0; offset < *count; ++offset) {
                    if (!destination_indices->is_null()) {
                        auto stored = machine.heap().set_element(
                            *destination_indices, output,
                            Value::from_int(*first + offset));
                        if (!stored) return std::unexpected(stored.error());
                    }
                    if (!destination_weights->is_null()) {
                        auto stored = machine.heap().set_element(
                            *destination_weights, output,
                            Value::from_float(static_cast<float>(*weight) /
                                              255.0F));
                        if (!stored) return std::unexpected(stored.error());
                    }
                    ++output;
                }
            }
            if (result_count > static_cast<usize>(std::numeric_limits<i32>::max())) {
                return fail_java("java/lang/IllegalStateException",
                                 "bone vertex result is too large");
            }
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(result_count)));
        });
    add(registry, skinned, "getSkeleton",
        "()Ljavax/microedition/m3g/Group;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "SkinnedMesh.getSkeleton");
            if (!object) return std::unexpected(object.error());
            auto skeleton = reference_field(
                machine, *object, skinned, "skeleton",
                "Ljavax/microedition/m3g/Group;");
            if (!skeleton) return std::unexpected(skeleton.error());
            return std::optional<Value>(Value::from_reference(*skeleton));
        });

    constexpr const char* morphing = "javax/microedition/m3g/MorphingMesh";
    const auto register_morphing_constructor =
        [&registry](const char* descriptor, bool arrays) {
            add(registry, morphing, "<init>", descriptor,
                [arrays](Machine& machine,
                                   std::span<const Value> arguments)
                    -> Result<std::optional<Value>> {
                    auto object = receiver(arguments, "MorphingMesh.<init>");
                    auto vertices = reference_argument(
                        arguments, 1U, "MorphingMesh.<init>", false);
                    auto targets = reference_argument(
                        arguments, 2U, "MorphingMesh.<init>", false);
                    auto indices = reference_argument(
                        arguments, 3U, "MorphingMesh.<init>", false);
                    auto appearances = reference_argument(
                        arguments, 4U, "MorphingMesh.<init>");
                    if (!object) return std::unexpected(object.error());
                    if (!vertices) return std::unexpected(vertices.error());
                    if (!targets) return std::unexpected(targets.error());
                    if (!indices) return std::unexpected(indices.error());
                    if (!appearances) return std::unexpected(appearances.error());
                    auto target_count = machine.heap().array_length(*targets);
                    if (!target_count) {
                        return std::unexpected(target_count.error());
                    }
                    if (*target_count == 0U) {
                        return fail_java("java/lang/IllegalArgumentException",
                                         "MorphingMesh requires a morph target");
                    }
                    auto initialized = initialize_mesh_geometry(
                        machine, *object, *vertices, *indices, *appearances,
                        arrays);
                    if (!initialized) return std::unexpected(initialized.error());
                    auto copied_targets = copy_array(
                        machine, *targets,
                        "[Ljavax/microedition/m3g/VertexBuffer;",
                        Value::from_reference({}));
                    auto weights = allocate_array(
                        machine, "[F", *target_count,
                        Value::from_float(0.0F));
                    if (!copied_targets) {
                        return std::unexpected(copied_targets.error());
                    }
                    if (!weights) return std::unexpected(weights.error());
                    auto targets_stored = set_reference_field(
                        machine, *object, morphing, "morphTargets",
                        "[Ljavax/microedition/m3g/VertexBuffer;",
                        *copied_targets);
                    auto weights_stored = set_reference_field(
                        machine, *object, morphing, "weights", "[F", *weights);
                    if (!targets_stored) {
                        return std::unexpected(targets_stored.error());
                    }
                    if (!weights_stored) {
                        return std::unexpected(weights_stored.error());
                    }
                    return std::optional<Value> {};
                });
        };
    register_morphing_constructor(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/VertexBuffer;"
        "Ljavax/microedition/m3g/IndexBuffer;"
        "Ljavax/microedition/m3g/Appearance;)V", false);
    register_morphing_constructor(
        "(Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/VertexBuffer;"
        "[Ljavax/microedition/m3g/IndexBuffer;"
        "[Ljavax/microedition/m3g/Appearance;)V", true);
    add(registry, morphing, "setWeights", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "MorphingMesh.setWeights");
            auto source = reference_argument(arguments, 1U,
                                             "MorphingMesh.setWeights", false);
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            auto targets = reference_field(
                machine, *object, morphing, "morphTargets",
                "[Ljavax/microedition/m3g/VertexBuffer;");
            auto weights = reference_field(
                machine, *object, morphing, "weights", "[F");
            if (!targets) return std::unexpected(targets.error());
            if (!weights) return std::unexpected(weights.error());
            auto count = machine.heap().array_length(*targets);
            auto source_length = machine.heap().array_length(*source);
            if (!count) return std::unexpected(count.error());
            if (!source_length) return std::unexpected(source_length.error());
            if (*source_length < *count) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "MorphingMesh weight array is too short");
            }
            if (weights->is_null()) {
                auto allocated = allocate_array(machine, "[F", *count,
                                                Value::from_float(0.0F));
                if (!allocated) return std::unexpected(allocated.error());
                *weights = *allocated;
                auto stored = set_reference_field(
                    machine, *object, morphing, "weights", "[F", *weights);
                if (!stored) return std::unexpected(stored.error());
            }
            for (usize index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*source, index);
                if (!value) return std::unexpected(value.error());
                auto parsed = value->as_float();
                if (!parsed) return std::unexpected(parsed.error());
                auto stored = machine.heap().set_element(
                    *weights, index, Value::from_float(*parsed));
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, morphing, "getWeights", "([F)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "MorphingMesh.getWeights");
            auto destination = reference_argument(
                arguments, 1U, "MorphingMesh.getWeights", false);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto weights = reference_field(
                machine, *object, morphing, "weights", "[F");
            if (!weights) return std::unexpected(weights.error());
            auto count = machine.heap().array_length(*weights);
            auto destination_length = machine.heap().array_length(*destination);
            if (!count) return std::unexpected(count.error());
            if (!destination_length) {
                return std::unexpected(destination_length.error());
            }
            if (*destination_length < *count) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "MorphingMesh destination is too short");
            }
            for (usize index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*weights, index);
                if (!value) return std::unexpected(value.error());
                auto stored = machine.heap().set_element(
                    *destination, index, *value);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, morphing, "getMorphTargetCount", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments,
                                   "MorphingMesh.getMorphTargetCount");
            if (!object) return std::unexpected(object.error());
            auto targets = reference_field(
                machine, *object, morphing, "morphTargets",
                "[Ljavax/microedition/m3g/VertexBuffer;");
            if (!targets) return std::unexpected(targets.error());
            auto count = machine.heap().array_length(*targets);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(*count)));
        });
    add(registry, morphing, "getMorphTarget",
        "(I)Ljavax/microedition/m3g/VertexBuffer;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "MorphingMesh.getMorphTarget");
            auto index = int_argument(arguments, 1U,
                                      "MorphingMesh.getMorphTarget");
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            auto targets = reference_field(
                machine, *object, morphing, "morphTargets",
                "[Ljavax/microedition/m3g/VertexBuffer;");
            if (!targets) return std::unexpected(targets.error());
            auto count = machine.heap().array_length(*targets);
            if (!count) return std::unexpected(count.error());
            if (*index < 0 || static_cast<usize>(*index) >= *count) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "morph target index is out of range");
            }
            auto target = machine.heap().element(
                *targets, static_cast<usize>(*index));
            if (!target) return std::unexpected(target.error());
            return std::optional<Value>(*target);
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
