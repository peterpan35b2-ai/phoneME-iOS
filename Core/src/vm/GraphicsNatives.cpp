#include "GraphicsNatives.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "GraphicsNativeSupport.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

using namespace graphics_native;

constexpr usize kFontFaceField = 0;
constexpr usize kFontStyleField = 1;
constexpr usize kFontSizeField = 2;

struct BoundGraphics final {
    graphics::GraphicsContext* context {nullptr};
    graphics::Image* target {nullptr};
};

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) {
        std::terminate();
    }
}

[[nodiscard]] Result<graphics::Image*> image_payload(Machine& machine,
                                                     ObjectRef image) {
    auto payload = machine.graphics().image(image.bits);
    if (!payload) return graphics_error(payload.error());
    return *payload;
}

[[nodiscard]] Result<BoundGraphics> bound_graphics(
    Machine& machine,
    std::span<const Value> arguments,
    std::string_view operation) {
    auto graphics_object = receiver(arguments, operation);
    if (!graphics_object) return std::unexpected(graphics_object.error());
    auto context = machine.graphics().context(graphics_object->bits);
    if (!context) return graphics_error(context.error());
    auto target = machine.graphics().image((*context)->target_key);
    if (!target) return graphics_error(target.error());
    return BoundGraphics {.context = *context, .target = *target};
}

[[nodiscard]] Result<i32> object_int_field(Machine& machine,
                                           ObjectRef object,
                                           usize index) {
    auto value = machine.heap().field(object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<graphics::Font> font_payload(Machine& machine,
                                                  ObjectRef font) {
    if (font.is_null()) return graphics::Font::default_font();
    auto class_name = machine.heap().class_name(font);
    if (!class_name || *class_name != "javax/microedition/lcdui/Font") {
        return fail_java("java/lang/IllegalArgumentException",
                         "object is not a javax.microedition.lcdui.Font");
    }
    auto face = object_int_field(machine, font, kFontFaceField);
    auto style = object_int_field(machine, font, kFontStyleField);
    auto size = object_int_field(machine, font, kFontSizeField);
    if (!face) return std::unexpected(face.error());
    if (!style) return std::unexpected(style.error());
    if (!size) return std::unexpected(size.error());
    auto result = graphics::Font::create(*face, *style, *size);
    if (!result) return graphics_error(result.error());
    return *result;
}

[[nodiscard]] Result<ObjectRef> create_font_object(Machine& machine,
                                                   const graphics::Font& font) {
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/lcdui/Font");
    if (!object) return std::unexpected(object.error());
    auto face = machine.heap().set_field(
        *object, kFontFaceField, Value::from_int(font.face()));
    auto style = machine.heap().set_field(
        *object, kFontStyleField, Value::from_int(font.style()));
    auto size = machine.heap().set_field(
        *object, kFontSizeField, Value::from_int(font.size()));
    if (!face) return std::unexpected(face.error());
    if (!style) return std::unexpected(style.error());
    if (!size) return std::unexpected(size.error());
    return *object;
}

[[nodiscard]] Result<std::vector<char32_t>> string_characters(
    Machine& machine,
    ObjectRef string,
    std::string_view operation) {
    auto text = string_text(machine, string, operation);
    if (!text) return std::unexpected(text.error());
    return utf32_text(*text);
}

[[nodiscard]] Result<std::vector<char32_t>> substring_characters(
    Machine& machine,
    ObjectRef string,
    i32 offset,
    i32 length,
    std::string_view operation) {
    auto text = string_text(machine, string, operation);
    if (!text) return std::unexpected(text.error());
    if (offset < 0 || length < 0 ||
        static_cast<usize>(offset) > text->size() ||
        static_cast<usize>(length) >
            text->size() - static_cast<usize>(offset)) {
        return fail_java("java/lang/StringIndexOutOfBoundsException",
                         std::string(operation) + " substring is outside the String");
    }
    return utf32_text(std::u16string_view(*text).substr(
        static_cast<usize>(offset), static_cast<usize>(length)));
}

[[nodiscard]] Result<std::vector<char32_t>> char_array_characters(
    Machine& machine,
    ObjectRef array,
    i32 offset,
    i32 length,
    std::string_view operation) {
    if (array.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " char[] is null");
    }
    if (offset < 0 || length < 0) {
        return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                         std::string(operation) + " char[] slice is negative");
    }
    auto class_name = machine.heap().class_name(array);
    auto array_length = machine.heap().array_length(array);
    if (!class_name || !array_length || *class_name != "[C") {
        return fail_java("java/lang/IllegalArgumentException",
                         std::string(operation) + " expects char[]");
    }
    if (static_cast<usize>(offset) > *array_length ||
        static_cast<usize>(length) >
            *array_length - static_cast<usize>(offset)) {
        return fail_java("java/lang/ArrayIndexOutOfBoundsException",
                         std::string(operation) + " char[] slice is outside the array");
    }
    std::u16string text;
    text.reserve(static_cast<usize>(length));
    for (i32 index = 0; index < length; ++index) {
        auto value = machine.heap().element(
            array, static_cast<usize>(offset + index));
        if (!value) return std::unexpected(value.error());
        auto character = value->as_int();
        if (!character) return std::unexpected(character.error());
        text.push_back(static_cast<char16_t>(static_cast<u16>(*character)));
    }
    return utf32_text(text);
}

[[nodiscard]] Result<std::optional<Value>> status_result(Status status) {
    if (!status) return graphics_error(status.error());
    return std::optional<Value> {};
}

[[nodiscard]] Result<std::optional<Value>> font_boolean(bool value) {
    return std::optional<Value>(Value::from_int(value ? 1 : 0));
}

} // namespace

void register_graphics_natives(NativeMethodRegistry& registry) {
    constexpr const char* graphics_owner =
        "javax/microedition/lcdui/Graphics";
    constexpr const char* font_owner = "javax/microedition/lcdui/Font";

    add(registry, graphics_owner, "setColor", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.setColor");
            auto rgb = int_argument(arguments, 1U, "Graphics.setColor");
            if (!bound) return std::unexpected(bound.error());
            if (!rgb) return std::unexpected(rgb.error());
            bound->context->color = 0xFF000000U |
                (static_cast<u32>(*rgb) & 0x00FFFFFFU);
            return std::optional<Value> {};
        });

    add(registry, graphics_owner, "setColor", "(III)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.setColor");
            auto red = int_argument(arguments, 1U, "Graphics.setColor");
            auto green = int_argument(arguments, 2U, "Graphics.setColor");
            auto blue = int_argument(arguments, 3U, "Graphics.setColor");
            if (!bound) return std::unexpected(bound.error());
            if (!red) return std::unexpected(red.error());
            if (!green) return std::unexpected(green.error());
            if (!blue) return std::unexpected(blue.error());
            if (*red < 0 || *red > 255 || *green < 0 || *green > 255 ||
                *blue < 0 || *blue > 255) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Graphics color components must be 0..255");
            }
            bound->context->color = graphics::argb(
                255U,
                static_cast<u8>(*red),
                static_cast<u8>(*green),
                static_cast<u8>(*blue));
            return std::optional<Value> {};
        });

    add(registry, graphics_owner, "getColor", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.getColor");
            if (!bound) return std::unexpected(bound.error());
            return std::optional<Value>(Value::from_int(static_cast<i32>(
                bound->context->color & 0x00FFFFFFU)));
        });

    const auto component_getter = [&registry](const char* name,
                                              auto getter) {
        add(registry, graphics_owner, name, "()I",
            [getter, name](Machine& machine,
                           std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto bound = bound_graphics(machine, arguments, name);
                if (!bound) return std::unexpected(bound.error());
                return std::optional<Value>(Value::from_int(
                    getter(bound->context->color)));
            });
    };
    component_getter("getRedComponent", graphics::red);
    component_getter("getGreenComponent", graphics::green);
    component_getter("getBlueComponent", graphics::blue);

    add(registry, graphics_owner, "setGrayScale", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.setGrayScale");
            auto value = int_argument(arguments, 1U,
                                      "Graphics.setGrayScale");
            if (!bound) return std::unexpected(bound.error());
            if (!value) return std::unexpected(value.error());
            if (*value < 0 || *value > 255) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "gray scale must be 0..255");
            }
            const u8 component = static_cast<u8>(*value);
            bound->context->color = graphics::argb(
                255U, component, component, component);
            return std::optional<Value> {};
        });

    add(registry, graphics_owner, "getGrayScale", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.getGrayScale");
            if (!bound) return std::unexpected(bound.error());
            const u32 gray = (static_cast<u32>(graphics::red(
                                  bound->context->color)) +
                              graphics::green(bound->context->color) +
                              graphics::blue(bound->context->color)) /
                             3U;
            return std::optional<Value>(Value::from_int(
                static_cast<i32>(gray)));
        });

    add(registry, graphics_owner, "setStrokeStyle", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.setStrokeStyle");
            auto style = int_argument(arguments, 1U,
                                      "Graphics.setStrokeStyle");
            if (!bound) return std::unexpected(bound.error());
            if (!style) return std::unexpected(style.error());
            if (*style != graphics::stroke_solid &&
                *style != graphics::stroke_dotted) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid Graphics stroke style");
            }
            bound->context->stroke_style = *style;
            return std::optional<Value> {};
        });

    add(registry, graphics_owner, "getStrokeStyle", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.getStrokeStyle");
            if (!bound) return std::unexpected(bound.error());
            return std::optional<Value>(Value::from_int(
                bound->context->stroke_style));
        });

    add(registry, graphics_owner, "translate", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.translate");
            auto x = int_argument(arguments, 1U, "Graphics.translate");
            auto y = int_argument(arguments, 2U, "Graphics.translate");
            if (!bound) return std::unexpected(bound.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            graphics::translate(*bound->context, *x, *y);
            return std::optional<Value> {};
        });

    add(registry, graphics_owner, "getTranslateX", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.getTranslateX");
            if (!bound) return std::unexpected(bound.error());
            return std::optional<Value>(Value::from_int(
                bound->context->translate_x));
        });
    add(registry, graphics_owner, "getTranslateY", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.getTranslateY");
            if (!bound) return std::unexpected(bound.error());
            return std::optional<Value>(Value::from_int(
                bound->context->translate_y));
        });

    const auto clip_operation = [&registry](const char* name, bool replace) {
        add(registry, graphics_owner, name, "(IIII)V",
            [replace, name](Machine& machine,
                            std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto bound = bound_graphics(machine, arguments, name);
                auto x = int_argument(arguments, 1U, name);
                auto y = int_argument(arguments, 2U, name);
                auto width = int_argument(arguments, 3U, name);
                auto height = int_argument(arguments, 4U, name);
                if (!bound) return std::unexpected(bound.error());
                if (!x) return std::unexpected(x.error());
                if (!y) return std::unexpected(y.error());
                if (!width) return std::unexpected(width.error());
                if (!height) return std::unexpected(height.error());
                return status_result(replace
                    ? graphics::set_clip(*bound->context,
                                         *bound->target,
                                         *x, *y, *width, *height)
                    : graphics::clip_rect(*bound->context,
                                          *bound->target,
                                          *x, *y, *width, *height));
            });
    };
    clip_operation("setClip", true);
    clip_operation("clipRect", false);

    const auto clip_getter = [&registry](const char* method_name,
                                         auto getter) {
        add(registry, graphics_owner, method_name, "()I",
            [getter, method_name](Machine& machine,
                                  std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto bound = bound_graphics(machine, arguments, method_name);
                if (!bound) return std::unexpected(bound.error());
                return std::optional<Value>(Value::from_int(
                    getter(*bound->context)));
            });
    };
    clip_getter("getClipX", [](const graphics::GraphicsContext& value) {
        return value.clip.x - value.translate_x;
    });
    clip_getter("getClipY", [](const graphics::GraphicsContext& value) {
        return value.clip.y - value.translate_y;
    });
    clip_getter("getClipWidth", [](const graphics::GraphicsContext& value) {
        return value.clip.width;
    });
    clip_getter("getClipHeight", [](const graphics::GraphicsContext& value) {
        return value.clip.height;
    });

    add(registry, graphics_owner, "setFont",
        "(Ljavax/microedition/lcdui/Font;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.setFont");
            auto font_reference = reference_argument(
                arguments, 1U, "Graphics.setFont", true);
            if (!bound) return std::unexpected(bound.error());
            if (!font_reference) return std::unexpected(font_reference.error());
            auto font = font_payload(machine, *font_reference);
            if (!font) return std::unexpected(font.error());
            bound->context->font = *font;
            return std::optional<Value> {};
        });

    add(registry, graphics_owner, "getFont",
        "()Ljavax/microedition/lcdui/Font;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.getFont");
            if (!bound) return std::unexpected(bound.error());
            auto font = create_font_object(machine, bound->context->font);
            if (!font) return std::unexpected(font.error());
            return std::optional<Value>(Value::from_reference(*font));
        });

    add(registry, graphics_owner, "drawLine", "(IIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawLine");
            auto x1 = int_argument(arguments, 1U, "Graphics.drawLine");
            auto y1 = int_argument(arguments, 2U, "Graphics.drawLine");
            auto x2 = int_argument(arguments, 3U, "Graphics.drawLine");
            auto y2 = int_argument(arguments, 4U, "Graphics.drawLine");
            if (!bound) return std::unexpected(bound.error());
            if (!x1) return std::unexpected(x1.error());
            if (!y1) return std::unexpected(y1.error());
            if (!x2) return std::unexpected(x2.error());
            if (!y2) return std::unexpected(y2.error());
            return status_result(graphics::draw_line(
                *bound->target, *bound->context, *x1, *y1, *x2, *y2));
        });

    const auto rectangle_operation = [&registry](const char* name,
                                                  bool fill) {
        add(registry, graphics_owner, name, "(IIII)V",
            [fill, name](Machine& machine,
                         std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto bound = bound_graphics(machine, arguments, name);
                auto x = int_argument(arguments, 1U, name);
                auto y = int_argument(arguments, 2U, name);
                auto width = int_argument(arguments, 3U, name);
                auto height = int_argument(arguments, 4U, name);
                if (!bound) return std::unexpected(bound.error());
                if (!x) return std::unexpected(x.error());
                if (!y) return std::unexpected(y.error());
                if (!width) return std::unexpected(width.error());
                if (!height) return std::unexpected(height.error());
                return status_result(fill
                    ? graphics::fill_rect(*bound->target,
                                          *bound->context,
                                          *x, *y, *width, *height)
                    : graphics::draw_rect(*bound->target,
                                          *bound->context,
                                          *x, *y, *width, *height));
            });
    };
    rectangle_operation("fillRect", true);
    rectangle_operation("drawRect", false);

    const auto round_rectangle_operation = [&registry](const char* name,
                                                        bool fill) {
        add(registry, graphics_owner, name, "(IIIIII)V",
            [fill, name](Machine& machine,
                         std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto bound = bound_graphics(machine, arguments, name);
                std::array<Result<i32>, 6> values {
                    int_argument(arguments, 1U, name),
                    int_argument(arguments, 2U, name),
                    int_argument(arguments, 3U, name),
                    int_argument(arguments, 4U, name),
                    int_argument(arguments, 5U, name),
                    int_argument(arguments, 6U, name),
                };
                if (!bound) return std::unexpected(bound.error());
                for (const auto& value : values) {
                    if (!value) return std::unexpected(value.error());
                }
                return status_result(graphics::draw_round_rect(
                    *bound->target, *bound->context,
                    *values[0], *values[1], *values[2], *values[3],
                    *values[4], *values[5], fill));
            });
    };
    round_rectangle_operation("drawRoundRect", false);
    round_rectangle_operation("fillRoundRect", true);

    const auto arc_operation = [&registry](const char* name, bool fill) {
        add(registry, graphics_owner, name, "(IIIIII)V",
            [fill, name](Machine& machine,
                         std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto bound = bound_graphics(machine, arguments, name);
                std::array<Result<i32>, 6> values {
                    int_argument(arguments, 1U, name),
                    int_argument(arguments, 2U, name),
                    int_argument(arguments, 3U, name),
                    int_argument(arguments, 4U, name),
                    int_argument(arguments, 5U, name),
                    int_argument(arguments, 6U, name),
                };
                if (!bound) return std::unexpected(bound.error());
                for (const auto& value : values) {
                    if (!value) return std::unexpected(value.error());
                }
                return status_result(graphics::draw_arc(
                    *bound->target, *bound->context,
                    *values[0], *values[1], *values[2], *values[3],
                    *values[4], *values[5], fill));
            });
    };
    arc_operation("drawArc", false);
    arc_operation("fillArc", true);

    add(registry, graphics_owner, "fillTriangle", "(IIIIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.fillTriangle");
            std::array<Result<i32>, 6> values {
                int_argument(arguments, 1U, "Graphics.fillTriangle"),
                int_argument(arguments, 2U, "Graphics.fillTriangle"),
                int_argument(arguments, 3U, "Graphics.fillTriangle"),
                int_argument(arguments, 4U, "Graphics.fillTriangle"),
                int_argument(arguments, 5U, "Graphics.fillTriangle"),
                int_argument(arguments, 6U, "Graphics.fillTriangle"),
            };
            if (!bound) return std::unexpected(bound.error());
            for (const auto& value : values) {
                if (!value) return std::unexpected(value.error());
            }
            return status_result(graphics::fill_triangle(
                *bound->target, *bound->context,
                *values[0], *values[1], *values[2], *values[3],
                *values[4], *values[5]));
        });

    add(registry, graphics_owner, "drawImage",
        "(Ljavax/microedition/lcdui/Image;III)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawImage");
            auto source_reference = reference_argument(
                arguments, 1U, "Graphics.drawImage");
            auto x = int_argument(arguments, 2U, "Graphics.drawImage");
            auto y = int_argument(arguments, 3U, "Graphics.drawImage");
            auto anchor = int_argument(arguments, 4U, "Graphics.drawImage");
            if (!bound) return std::unexpected(bound.error());
            if (!source_reference) return std::unexpected(source_reference.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!anchor) return std::unexpected(anchor.error());
            auto source = image_payload(machine, *source_reference);
            if (!source) return std::unexpected(source.error());
            return status_result(graphics::draw_image(
                *bound->target, *bound->context, **source,
                *x, *y, *anchor));
        });

    add(registry, graphics_owner, "drawRegion",
        "(Ljavax/microedition/lcdui/Image;IIIIIIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawRegion");
            auto source_reference = reference_argument(
                arguments, 1U, "Graphics.drawRegion");
            std::array<Result<i32>, 8> values {
                int_argument(arguments, 2U, "Graphics.drawRegion"),
                int_argument(arguments, 3U, "Graphics.drawRegion"),
                int_argument(arguments, 4U, "Graphics.drawRegion"),
                int_argument(arguments, 5U, "Graphics.drawRegion"),
                int_argument(arguments, 6U, "Graphics.drawRegion"),
                int_argument(arguments, 7U, "Graphics.drawRegion"),
                int_argument(arguments, 8U, "Graphics.drawRegion"),
                int_argument(arguments, 9U, "Graphics.drawRegion"),
            };
            if (!bound) return std::unexpected(bound.error());
            if (!source_reference) return std::unexpected(source_reference.error());
            for (const auto& value : values) {
                if (!value) return std::unexpected(value.error());
            }
            auto transform = graphics::transform_from_int(*values[4]);
            if (!transform) return graphics_error(transform.error());
            auto source = image_payload(machine, *source_reference);
            if (!source) return std::unexpected(source.error());
            return status_result(graphics::draw_region(
                *bound->target, *bound->context, **source,
                *values[0], *values[1], *values[2], *values[3],
                *transform, *values[5], *values[6], *values[7]));
        });

    add(registry, graphics_owner, "copyArea", "(IIIIIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.copyArea");
            std::array<Result<i32>, 7> values {
                int_argument(arguments, 1U, "Graphics.copyArea"),
                int_argument(arguments, 2U, "Graphics.copyArea"),
                int_argument(arguments, 3U, "Graphics.copyArea"),
                int_argument(arguments, 4U, "Graphics.copyArea"),
                int_argument(arguments, 5U, "Graphics.copyArea"),
                int_argument(arguments, 6U, "Graphics.copyArea"),
                int_argument(arguments, 7U, "Graphics.copyArea"),
            };
            if (!bound) return std::unexpected(bound.error());
            for (const auto& value : values) {
                if (!value) return std::unexpected(value.error());
            }
            return status_result(graphics::copy_area(
                *bound->target, *bound->context,
                *values[0], *values[1], *values[2], *values[3],
                *values[4], *values[5], *values[6]));
        });

    add(registry, graphics_owner, "drawRGB", "([IIIIIIIZ)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawRGB");
            auto rgb_reference = reference_argument(
                arguments, 1U, "Graphics.drawRGB");
            std::array<Result<i32>, 7> values {
                int_argument(arguments, 2U, "Graphics.drawRGB"),
                int_argument(arguments, 3U, "Graphics.drawRGB"),
                int_argument(arguments, 4U, "Graphics.drawRGB"),
                int_argument(arguments, 5U, "Graphics.drawRGB"),
                int_argument(arguments, 6U, "Graphics.drawRGB"),
                int_argument(arguments, 7U, "Graphics.drawRGB"),
                int_argument(arguments, 8U, "Graphics.drawRGB"),
            };
            if (!bound) return std::unexpected(bound.error());
            if (!rgb_reference) return std::unexpected(rgb_reference.error());
            for (const auto& value : values) {
                if (!value) return std::unexpected(value.error());
            }
            auto pixels = int_array(machine, *rgb_reference,
                                    "Graphics.drawRGB");
            if (!pixels) return std::unexpected(pixels.error());
            return status_result(graphics::draw_rgb(
                *bound->target, *bound->context, *pixels,
                *values[0], *values[1], *values[2], *values[3],
                *values[4], *values[5], *values[6] != 0));
        });

    add(registry, graphics_owner, "drawString",
        "(Ljava/lang/String;III)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawString");
            auto string = reference_argument(arguments, 1U,
                                             "Graphics.drawString");
            auto x = int_argument(arguments, 2U, "Graphics.drawString");
            auto y = int_argument(arguments, 3U, "Graphics.drawString");
            auto anchor = int_argument(arguments, 4U,
                                       "Graphics.drawString");
            if (!bound) return std::unexpected(bound.error());
            if (!string) return std::unexpected(string.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!anchor) return std::unexpected(anchor.error());
            auto text = string_characters(machine, *string,
                                          "Graphics.drawString");
            if (!text) return std::unexpected(text.error());
            return status_result(graphics::draw_text(
                *bound->target, *bound->context, *text,
                *x, *y, *anchor));
        });

    add(registry, graphics_owner, "drawSubstring",
        "(Ljava/lang/String;IIIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawSubstring");
            auto string = reference_argument(arguments, 1U,
                                             "Graphics.drawSubstring");
            std::array<Result<i32>, 5> values {
                int_argument(arguments, 2U, "Graphics.drawSubstring"),
                int_argument(arguments, 3U, "Graphics.drawSubstring"),
                int_argument(arguments, 4U, "Graphics.drawSubstring"),
                int_argument(arguments, 5U, "Graphics.drawSubstring"),
                int_argument(arguments, 6U, "Graphics.drawSubstring"),
            };
            if (!bound) return std::unexpected(bound.error());
            if (!string) return std::unexpected(string.error());
            for (const auto& value : values) {
                if (!value) return std::unexpected(value.error());
            }
            auto text = substring_characters(
                machine, *string, *values[0], *values[1],
                "Graphics.drawSubstring");
            if (!text) return std::unexpected(text.error());
            return status_result(graphics::draw_text(
                *bound->target, *bound->context, *text,
                *values[2], *values[3], *values[4]));
        });

    add(registry, graphics_owner, "drawChar", "(CIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawChar");
            auto character = int_argument(arguments, 1U,
                                          "Graphics.drawChar");
            auto x = int_argument(arguments, 2U, "Graphics.drawChar");
            auto y = int_argument(arguments, 3U, "Graphics.drawChar");
            auto anchor = int_argument(arguments, 4U,
                                       "Graphics.drawChar");
            if (!bound) return std::unexpected(bound.error());
            if (!character) return std::unexpected(character.error());
            if (!x) return std::unexpected(x.error());
            if (!y) return std::unexpected(y.error());
            if (!anchor) return std::unexpected(anchor.error());
            const char32_t text[] {
                static_cast<char32_t>(static_cast<u16>(*character)),
            };
            return status_result(graphics::draw_text(
                *bound->target, *bound->context, text,
                *x, *y, *anchor));
        });

    add(registry, graphics_owner, "drawChars", "([CIIIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto bound = bound_graphics(machine, arguments,
                                        "Graphics.drawChars");
            auto data = reference_argument(arguments, 1U,
                                           "Graphics.drawChars");
            std::array<Result<i32>, 5> values {
                int_argument(arguments, 2U, "Graphics.drawChars"),
                int_argument(arguments, 3U, "Graphics.drawChars"),
                int_argument(arguments, 4U, "Graphics.drawChars"),
                int_argument(arguments, 5U, "Graphics.drawChars"),
                int_argument(arguments, 6U, "Graphics.drawChars"),
            };
            if (!bound) return std::unexpected(bound.error());
            if (!data) return std::unexpected(data.error());
            for (const auto& value : values) {
                if (!value) return std::unexpected(value.error());
            }
            auto text = char_array_characters(
                machine, *data, *values[0], *values[1],
                "Graphics.drawChars");
            if (!text) return std::unexpected(text.error());
            return status_result(graphics::draw_text(
                *bound->target, *bound->context, *text,
                *values[2], *values[3], *values[4]));
        });

    add(registry, font_owner, "getDefaultFont",
        "()Ljavax/microedition/lcdui/Font;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty()) {
                return fail(ErrorCode::invalid_argument,
                            "Font.getDefaultFont expects no arguments");
            }
            auto object = create_font_object(
                machine, graphics::Font::default_font());
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_reference(*object));
        });

    add(registry, font_owner, "getFont",
        "(III)Ljavax/microedition/lcdui/Font;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto face = int_argument(arguments, 0U, "Font.getFont");
            auto style = int_argument(arguments, 1U, "Font.getFont");
            auto size = int_argument(arguments, 2U, "Font.getFont");
            if (!face) return std::unexpected(face.error());
            if (!style) return std::unexpected(style.error());
            if (!size) return std::unexpected(size.error());
            auto font = graphics::Font::create(*face, *style, *size);
            if (!font) return graphics_error(font.error());
            auto object = create_font_object(machine, *font);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value>(Value::from_reference(*object));
        });

    const auto font_int_getter = [&registry](const char* name, auto getter) {
        add(registry, font_owner, name, "()I",
            [getter, name](Machine& machine,
                           std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, name);
                if (!object) return std::unexpected(object.error());
                auto font = font_payload(machine, *object);
                if (!font) return std::unexpected(font.error());
                return std::optional<Value>(Value::from_int(getter(*font)));
            });
    };
    font_int_getter("getFace", [](const graphics::Font& font) {
        return font.face();
    });
    font_int_getter("getStyle", [](const graphics::Font& font) {
        return font.style();
    });
    font_int_getter("getSize", [](const graphics::Font& font) {
        return font.size();
    });
    font_int_getter("getHeight", [](const graphics::Font& font) {
        return font.height();
    });
    font_int_getter("getBaselinePosition", [](const graphics::Font& font) {
        return font.baseline();
    });

    const auto font_flag_getter = [&registry](const char* name, auto getter) {
        add(registry, font_owner, name, "()Z",
            [getter, name](Machine& machine,
                           std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, name);
                if (!object) return std::unexpected(object.error());
                auto font = font_payload(machine, *object);
                if (!font) return std::unexpected(font.error());
                return font_boolean(getter(*font));
            });
    };
    font_flag_getter("isPlain", [](const graphics::Font& font) {
        return font.is_plain();
    });
    font_flag_getter("isBold", [](const graphics::Font& font) {
        return font.is_bold();
    });
    font_flag_getter("isItalic", [](const graphics::Font& font) {
        return font.is_italic();
    });
    font_flag_getter("isUnderlined", [](const graphics::Font& font) {
        return font.is_underlined();
    });

    add(registry, font_owner, "charWidth", "(C)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Font.charWidth");
            auto character = int_argument(arguments, 1U, "Font.charWidth");
            if (!object) return std::unexpected(object.error());
            if (!character) return std::unexpected(character.error());
            auto font = font_payload(machine, *object);
            if (!font) return std::unexpected(font.error());
            return std::optional<Value>(Value::from_int(font->char_width(
                static_cast<char32_t>(static_cast<u16>(*character)))));
        });

    add(registry, font_owner, "charsWidth", "([CII)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Font.charsWidth");
            auto data = reference_argument(arguments, 1U, "Font.charsWidth");
            auto offset = int_argument(arguments, 2U, "Font.charsWidth");
            auto length = int_argument(arguments, 3U, "Font.charsWidth");
            if (!object) return std::unexpected(object.error());
            if (!data) return std::unexpected(data.error());
            if (!offset) return std::unexpected(offset.error());
            if (!length) return std::unexpected(length.error());
            auto font = font_payload(machine, *object);
            if (!font) return std::unexpected(font.error());
            auto text = char_array_characters(machine, *data, *offset, *length,
                                              "Font.charsWidth");
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_int(
                font->chars_width(*text)));
        });

    add(registry, font_owner, "stringWidth", "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Font.stringWidth");
            auto string = reference_argument(arguments, 1U,
                                             "Font.stringWidth");
            if (!object) return std::unexpected(object.error());
            if (!string) return std::unexpected(string.error());
            auto font = font_payload(machine, *object);
            if (!font) return std::unexpected(font.error());
            auto text = string_characters(machine, *string,
                                          "Font.stringWidth");
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_int(
                font->chars_width(*text)));
        });

    add(registry, font_owner, "substringWidth",
        "(Ljava/lang/String;II)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Font.substringWidth");
            auto string = reference_argument(arguments, 1U,
                                             "Font.substringWidth");
            auto offset = int_argument(arguments, 2U,
                                       "Font.substringWidth");
            auto length = int_argument(arguments, 3U,
                                       "Font.substringWidth");
            if (!object) return std::unexpected(object.error());
            if (!string) return std::unexpected(string.error());
            if (!offset) return std::unexpected(offset.error());
            if (!length) return std::unexpected(length.error());
            auto font = font_payload(machine, *object);
            if (!font) return std::unexpected(font.error());
            auto text = substring_characters(machine, *string, *offset, *length,
                                             "Font.substringWidth");
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_int(
                font->chars_width(*text)));
        });
}

} // namespace phoneme::vm
