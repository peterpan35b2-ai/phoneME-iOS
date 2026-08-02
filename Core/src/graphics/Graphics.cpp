#include "phoneme/graphics/Graphics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <vector>

#include "phoneme/graphics/TextRasterizer.hpp"

namespace phoneme::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] i32 saturated_add(i32 left, i32 right) noexcept {
    const i64 value = static_cast<i64>(left) + static_cast<i64>(right);
    return static_cast<i32>(std::clamp<i64>(
        value,
        std::numeric_limits<i32>::min(),
        std::numeric_limits<i32>::max()));
}

[[nodiscard]] bool contains(Rect rectangle, i32 x, i32 y) noexcept {
    if (rectangle.width <= 0 || rectangle.height <= 0) {
        return false;
    }
    const i64 right = static_cast<i64>(rectangle.x) + rectangle.width;
    const i64 bottom = static_cast<i64>(rectangle.y) + rectangle.height;
    return static_cast<i64>(x) >= rectangle.x &&
           static_cast<i64>(y) >= rectangle.y &&
           static_cast<i64>(x) < right &&
           static_cast<i64>(y) < bottom;
}

[[nodiscard]] Status require_mutable(const Image& image) {
    if (!image.is_mutable()) {
        return fail(ErrorCode::invalid_state,
                    "graphics target image is immutable");
    }
    return {};
}

[[nodiscard]] Status put_pixel(Image& target,
                               const GraphicsContext& context,
                               i32 x,
                               i32 y,
                               Pixel pixel,
                               bool blend = true) {
    if (!contains(context.clip, x, y)) {
        return {};
    }
    return target.set_pixel(x, y, pixel, blend);
}

[[nodiscard]] Status draw_line_absolute(Image& target,
                                        const GraphicsContext& context,
                                        i32 x1,
                                        i32 y1,
                                        i32 x2,
                                        i32 y2) {
    i64 current_x = x1;
    i64 current_y = y1;
    const i64 destination_x = x2;
    const i64 destination_y = y2;
    const i64 delta_x = std::abs(destination_x - current_x);
    const i64 step_x = current_x < destination_x ? 1 : -1;
    const i64 delta_y = -std::abs(destination_y - current_y);
    const i64 step_y = current_y < destination_y ? 1 : -1;
    i64 error = delta_x + delta_y;
    usize sample = 0;

    while (true) {
        if (context.stroke_style == stroke_solid || (sample & 1U) == 0U) {
            auto stored = put_pixel(target,
                                    context,
                                    static_cast<i32>(current_x),
                                    static_cast<i32>(current_y),
                                    context.color);
            if (!stored) {
                return stored;
            }
        }
        if (current_x == destination_x && current_y == destination_y) {
            break;
        }
        const i64 doubled = error * 2;
        if (doubled >= delta_y) {
            error += delta_y;
            current_x += step_x;
        }
        if (doubled <= delta_x) {
            error += delta_x;
            current_y += step_y;
        }
        ++sample;
    }
    return {};
}

[[nodiscard]] bool rounded_contains(i32 local_x,
                                    i32 local_y,
                                    i32 width,
                                    i32 height,
                                    i32 radius_x,
                                    i32 radius_y) noexcept {
    if (local_x < 0 || local_y < 0 || local_x >= width || local_y >= height) {
        return false;
    }
    if (radius_x <= 0 || radius_y <= 0 ||
        (local_x >= radius_x && local_x < width - radius_x) ||
        (local_y >= radius_y && local_y < height - radius_y)) {
        return true;
    }
    const double center_x = local_x < radius_x
        ? static_cast<double>(radius_x) - 0.5
        : static_cast<double>(width - radius_x) - 0.5;
    const double center_y = local_y < radius_y
        ? static_cast<double>(radius_y) - 0.5
        : static_cast<double>(height - radius_y) - 0.5;
    const double normalized_x =
        (static_cast<double>(local_x) + 0.5 - center_x) /
        static_cast<double>(radius_x);
    const double normalized_y =
        (static_cast<double>(local_y) + 0.5 - center_y) /
        static_cast<double>(radius_y);
    return normalized_x * normalized_x + normalized_y * normalized_y <= 1.0;
}

struct Glyph final {
    char character;
    std::array<u8, 5> columns;
};

constexpr std::array<Glyph, 36> kGlyphs {{
    {'0', {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}},
    {'1', {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}},
    {'2', {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}},
    {'3', {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}},
    {'4', {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}},
    {'5', {0x27U, 0x45U, 0x45U, 0x45U, 0x39U}},
    {'6', {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U}},
    {'7', {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}},
    {'8', {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}},
    {'9', {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}},
    {'A', {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}},
    {'B', {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U}},
    {'C', {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U}},
    {'D', {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}},
    {'E', {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}},
    {'F', {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}},
    {'G', {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU}},
    {'H', {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU}},
    {'I', {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U}},
    {'J', {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U}},
    {'K', {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U}},
    {'L', {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}},
    {'M', {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU}},
    {'N', {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU}},
    {'O', {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}},
    {'P', {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}},
    {'Q', {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU}},
    {'R', {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}},
    {'S', {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}},
    {'T', {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}},
    {'U', {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}},
    {'V', {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU}},
    {'W', {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU}},
    {'X', {0x63U, 0x14U, 0x08U, 0x14U, 0x63U}},
    {'Y', {0x07U, 0x08U, 0x70U, 0x08U, 0x07U}},
    {'Z', {0x61U, 0x51U, 0x49U, 0x45U, 0x43U}},
}};

[[nodiscard]] std::array<u8, 5> glyph_columns(char32_t character) noexcept {
    if (character >= U'a' && character <= U'z') {
        character -= U'a' - U'A';
    }
    if (character <= 0x7FU) {
        const char ascii = static_cast<char>(character);
        for (const Glyph& glyph : kGlyphs) {
            if (glyph.character == ascii) {
                return glyph.columns;
            }
        }
        switch (ascii) {
        case ' ':
            return {};
        case '.':
            return {0x00U, 0x60U, 0x60U, 0x00U, 0x00U};
        case ',':
            return {0x00U, 0x80U, 0x60U, 0x00U, 0x00U};
        case ':':
            return {0x00U, 0x36U, 0x36U, 0x00U, 0x00U};
        case '-':
            return {0x08U, 0x08U, 0x08U, 0x08U, 0x08U};
        case '_':
            return {0x40U, 0x40U, 0x40U, 0x40U, 0x40U};
        case '/':
            return {0x20U, 0x10U, 0x08U, 0x04U, 0x02U};
        case '\\':
            return {0x02U, 0x04U, 0x08U, 0x10U, 0x20U};
        case '!':
            return {0x00U, 0x00U, 0x5FU, 0x00U, 0x00U};
        case '?':
            return {0x02U, 0x01U, 0x51U, 0x09U, 0x06U};
        case '(':
            return {0x00U, 0x1CU, 0x22U, 0x41U, 0x00U};
        case ')':
            return {0x00U, 0x41U, 0x22U, 0x1CU, 0x00U};
        default:
            break;
        }
    }
    const u8 seed = static_cast<u8>((character ^ (character >> 8U)) & 0x7FU);
    return {0x7FU,
            static_cast<u8>(0x41U | (seed & 0x1CU)),
            static_cast<u8>(0x41U |
                            ((static_cast<u32>(seed) << 1U) & 0x1CU)),
            static_cast<u8>(0x41U | ((seed >> 1U) & 0x1CU)),
            0x7FU};
}

[[nodiscard]] Status draw_glyph(Image& target,
                                const GraphicsContext& context,
                                char32_t character,
                                i32 cell_x,
                                i32 top) {
    if (character == U' ' || character == U'\t') {
        return {};
    }
    const auto columns = glyph_columns(character);
    const i32 scale = context.font.height() >= 20 ? 2 : 1;
    const i32 glyph_width = 5 * scale;
    const i32 glyph_height = 7 * scale;
    const i32 cell_width = context.font.char_width(character);
    const i32 origin_x = cell_x + std::max(0, (cell_width - glyph_width) / 2);
    const i32 origin_y = top + context.font.baseline() - glyph_height;

    for (i32 column = 0; column < 5; ++column) {
        for (i32 row = 0; row < 7; ++row) {
            if ((columns[static_cast<usize>(column)] & (1U << row)) == 0U) {
                continue;
            }
            const i32 italic_shift = context.font.is_italic()
                ? (6 - row) / 3
                : 0;
            for (i32 scale_y = 0; scale_y < scale; ++scale_y) {
                for (i32 scale_x = 0; scale_x < scale; ++scale_x) {
                    const i32 pixel_x = origin_x + column * scale + scale_x +
                                        italic_shift;
                    const i32 pixel_y = origin_y + row * scale + scale_y;
                    auto stored = put_pixel(target,
                                            context,
                                            pixel_x,
                                            pixel_y,
                                            context.color);
                    if (!stored) {
                        return stored;
                    }
                    if (context.font.is_bold()) {
                        stored = put_pixel(target,
                                           context,
                                           pixel_x + 1,
                                           pixel_y,
                                           context.color);
                        if (!stored) {
                            return stored;
                        }
                    }
                }
            }
        }
    }
    return {};
}

} // namespace

Rect intersect(Rect left_rectangle, Rect right_rectangle) noexcept {
    const i64 left = std::max<i64>(left_rectangle.x, right_rectangle.x);
    const i64 top = std::max<i64>(left_rectangle.y, right_rectangle.y);
    const i64 right = std::min<i64>(
        static_cast<i64>(left_rectangle.x) +
            std::max(0, left_rectangle.width),
        static_cast<i64>(right_rectangle.x) +
            std::max(0, right_rectangle.width));
    const i64 bottom = std::min<i64>(
        static_cast<i64>(left_rectangle.y) +
            std::max(0, left_rectangle.height),
        static_cast<i64>(right_rectangle.y) +
            std::max(0, right_rectangle.height));
    if (right <= left || bottom <= top) {
        return Rect {.x = static_cast<i32>(std::clamp<i64>(
                         left,
                         std::numeric_limits<i32>::min(),
                         std::numeric_limits<i32>::max())),
                     .y = static_cast<i32>(std::clamp<i64>(
                         top,
                         std::numeric_limits<i32>::min(),
                         std::numeric_limits<i32>::max())),
                     .width = 0,
                     .height = 0};
    }
    return Rect {
        .x = static_cast<i32>(left),
        .y = static_cast<i32>(top),
        .width = static_cast<i32>(std::min<i64>(
            right - left, std::numeric_limits<i32>::max())),
        .height = static_cast<i32>(std::min<i64>(
            bottom - top, std::numeric_limits<i32>::max())),
    };
}

bool empty(Rect rectangle) noexcept {
    return rectangle.width <= 0 || rectangle.height <= 0;
}

Rect target_bounds(const Image& image) noexcept {
    return Rect {.x = 0,
                 .y = 0,
                 .width = image.width(),
                 .height = image.height()};
}

Status set_clip(GraphicsContext& context,
                const Image& target,
                i32 x,
                i32 y,
                i32 width,
                i32 height) {
    context.clip = intersect(
        Rect {.x = saturated_add(x, context.translate_x),
              .y = saturated_add(y, context.translate_y),
              .width = std::max(0, width),
              .height = std::max(0, height)},
        target_bounds(target));
    return {};
}

Status clip_rect(GraphicsContext& context,
                 const Image& target,
                 i32 x,
                 i32 y,
                 i32 width,
                 i32 height) {
    context.clip = intersect(
        context.clip,
        intersect(Rect {.x = saturated_add(x, context.translate_x),
                        .y = saturated_add(y, context.translate_y),
                        .width = std::max(0, width),
                        .height = std::max(0, height)},
                  target_bounds(target)));
    return {};
}

void translate(GraphicsContext& context, i32 x, i32 y) noexcept {
    context.translate_x = saturated_add(context.translate_x, x);
    context.translate_y = saturated_add(context.translate_y, y);
}

Result<Rect> anchored_rect(i32 x,
                           i32 y,
                           i32 width,
                           i32 height,
                           i32 anchor,
                           bool text,
                           i32 baseline) {
    if (width < 0 || height < 0) {
        return fail(ErrorCode::invalid_argument,
                    "anchored dimensions cannot be negative");
    }
    if (anchor == 0) {
        anchor = anchor_left | anchor_top;
    }
    const i32 horizontal = anchor &
        (anchor_left | anchor_right | anchor_hcenter);
    const i32 vertical_mask = text
        ? (anchor_top | anchor_bottom | anchor_vcenter | anchor_baseline)
        : (anchor_top | anchor_bottom | anchor_vcenter);
    const i32 vertical = anchor & vertical_mask;
    const i32 allowed = (anchor_left | anchor_right | anchor_hcenter) |
                        vertical_mask;
    const auto one_bit = [](i32 value) noexcept {
        return value != 0 && (value & (value - 1)) == 0;
    };
    if (!one_bit(horizontal) || !one_bit(vertical) ||
        (anchor & ~allowed) != 0) {
        return fail(ErrorCode::invalid_argument,
                    "invalid or conflicting Graphics anchor");
    }

    i32 left = x;
    if (horizontal == anchor_right) {
        left = saturated_add(x, -width);
    } else if (horizontal == anchor_hcenter) {
        left = saturated_add(x, -(width / 2));
    }

    i32 top = y;
    if (vertical == anchor_bottom) {
        top = saturated_add(y, -height);
    } else if (vertical == anchor_vcenter) {
        top = saturated_add(y, -(height / 2));
    } else if (vertical == anchor_baseline) {
        top = saturated_add(y, -baseline);
    }
    return Rect {.x = left, .y = top, .width = width, .height = height};
}

Status draw_line(Image& target,
                 const GraphicsContext& context,
                 i32 x1,
                 i32 y1,
                 i32 x2,
                 i32 y2) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) {
        return mutable_target;
    }
    return draw_line_absolute(target,
                              context,
                              saturated_add(x1, context.translate_x),
                              saturated_add(y1, context.translate_y),
                              saturated_add(x2, context.translate_x),
                              saturated_add(y2, context.translate_y));
}

Status fill_rect(Image& target,
                 const GraphicsContext& context,
                 i32 x,
                 i32 y,
                 i32 width,
                 i32 height) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) {
        return mutable_target;
    }
    if (width <= 0 || height <= 0) {
        return {};
    }
    const Rect fill = intersect(
        Rect {.x = saturated_add(x, context.translate_x),
              .y = saturated_add(y, context.translate_y),
              .width = width,
              .height = height},
        context.clip);
    for (i32 row = 0; row < fill.height; ++row) {
        for (i32 column = 0; column < fill.width; ++column) {
            auto stored = target.set_pixel(fill.x + column,
                                           fill.y + row,
                                           context.color,
                                           true);
            if (!stored) {
                return stored;
            }
        }
    }
    return {};
}

Status draw_rect(Image& target,
                 const GraphicsContext& context,
                 i32 x,
                 i32 y,
                 i32 width,
                 i32 height) {
    if (width < 0 || height < 0) {
        return {};
    }
    auto status = draw_line(target, context, x, y, x + width, y);
    if (!status) return status;
    status = draw_line(target, context, x, y, x, y + height);
    if (!status) return status;
    status = draw_line(target, context, x + width, y,
                       x + width, y + height);
    if (!status) return status;
    return draw_line(target, context, x, y + height,
                     x + width, y + height);
}

Status draw_round_rect(Image& target,
                       const GraphicsContext& context,
                       i32 x,
                       i32 y,
                       i32 width,
                       i32 height,
                       i32 arc_width,
                       i32 arc_height,
                       bool fill) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) return mutable_target;
    if (width <= 0 || height <= 0) return {};
    const i32 radius_x = std::clamp(std::abs(arc_width) / 2, 0, width / 2);
    const i32 radius_y = std::clamp(std::abs(arc_height) / 2, 0, height / 2);
    const i32 absolute_x = saturated_add(x, context.translate_x);
    const i32 absolute_y = saturated_add(y, context.translate_y);
    for (i32 local_y = 0; local_y < height; ++local_y) {
        for (i32 local_x = 0; local_x < width; ++local_x) {
            if (!rounded_contains(local_x, local_y, width, height,
                                  radius_x, radius_y)) {
                continue;
            }
            if (!fill) {
                const bool inner = width > 2 && height > 2 &&
                    rounded_contains(local_x - 1,
                                     local_y - 1,
                                     width - 2,
                                     height - 2,
                                     std::max(0, radius_x - 1),
                                     std::max(0, radius_y - 1));
                if (inner) continue;
            }
            auto stored = put_pixel(target,
                                    context,
                                    absolute_x + local_x,
                                    absolute_y + local_y,
                                    context.color);
            if (!stored) return stored;
        }
    }
    return {};
}

Status draw_arc(Image& target,
                const GraphicsContext& context,
                i32 x,
                i32 y,
                i32 width,
                i32 height,
                i32 start_angle,
                i32 arc_angle,
                bool fill) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) return mutable_target;
    if (width <= 0 || height <= 0 || arc_angle == 0) return {};
    const i32 absolute_x = saturated_add(x, context.translate_x);
    const i32 absolute_y = saturated_add(y, context.translate_y);
    const double center_x = static_cast<double>(absolute_x) +
                            static_cast<double>(width - 1) / 2.0;
    const double center_y = static_cast<double>(absolute_y) +
                            static_cast<double>(height - 1) / 2.0;
    const double radius_x = static_cast<double>(width - 1) / 2.0;
    const double radius_y = static_cast<double>(height - 1) / 2.0;
    const i32 clamped_arc = std::clamp(arc_angle, -360, 360);
    const i32 steps = std::max(1,
        static_cast<i32>(std::ceil(
            std::abs(static_cast<double>(clamped_arc)) *
            std::max(width, height) / 90.0)));
    i32 previous_x = static_cast<i32>(std::lround(
        center_x + radius_x * std::cos(start_angle * kPi / 180.0)));
    i32 previous_y = static_cast<i32>(std::lround(
        center_y - radius_y * std::sin(start_angle * kPi / 180.0)));
    for (i32 step = 0; step <= steps; ++step) {
        const double progress = static_cast<double>(step) / steps;
        const double angle_value =
            (static_cast<double>(start_angle) + clamped_arc * progress) *
            kPi / 180.0;
        const i32 current_x = static_cast<i32>(std::lround(
            center_x + radius_x * std::cos(angle_value)));
        const i32 current_y = static_cast<i32>(std::lround(
            center_y - radius_y * std::sin(angle_value)));
        auto status = draw_line_absolute(target,
                                         context,
                                         previous_x,
                                         previous_y,
                                         current_x,
                                         current_y);
        if (!status) return status;
        if (fill) {
            status = draw_line_absolute(target,
                                        context,
                                        static_cast<i32>(std::lround(center_x)),
                                        static_cast<i32>(std::lround(center_y)),
                                        current_x,
                                        current_y);
            if (!status) return status;
        }
        previous_x = current_x;
        previous_y = current_y;
    }
    return {};
}

Status fill_triangle(Image& target,
                     const GraphicsContext& context,
                     i32 x1,
                     i32 y1,
                     i32 x2,
                     i32 y2,
                     i32 x3,
                     i32 y3) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) return mutable_target;
    x1 = saturated_add(x1, context.translate_x);
    y1 = saturated_add(y1, context.translate_y);
    x2 = saturated_add(x2, context.translate_x);
    y2 = saturated_add(y2, context.translate_y);
    x3 = saturated_add(x3, context.translate_x);
    y3 = saturated_add(y3, context.translate_y);
    const Rect bounds = intersect(
        Rect {.x = std::min({x1, x2, x3}),
              .y = std::min({y1, y2, y3}),
              .width = std::max({x1, x2, x3}) - std::min({x1, x2, x3}) + 1,
              .height = std::max({y1, y2, y3}) - std::min({y1, y2, y3}) + 1},
        context.clip);
    const auto edge = [](i32 ax, i32 ay, i32 bx, i32 by,
                         i32 px, i32 py) -> i64 {
        return (static_cast<i64>(px) - ax) *
                   (static_cast<i64>(by) - ay) -
               (static_cast<i64>(py) - ay) *
                   (static_cast<i64>(bx) - ax);
    };
    const i64 area = edge(x1, y1, x2, y2, x3, y3);
    if (area == 0) {
        auto status = draw_line_absolute(target, context, x1, y1, x2, y2);
        if (!status) return status;
        return draw_line_absolute(target, context, x2, y2, x3, y3);
    }
    for (i32 y_value = bounds.y;
         y_value < bounds.y + bounds.height;
         ++y_value) {
        for (i32 x_value = bounds.x;
             x_value < bounds.x + bounds.width;
             ++x_value) {
            const i64 first = edge(x1, y1, x2, y2, x_value, y_value);
            const i64 second = edge(x2, y2, x3, y3, x_value, y_value);
            const i64 third = edge(x3, y3, x1, y1, x_value, y_value);
            const bool has_negative = first < 0 || second < 0 || third < 0;
            const bool has_positive = first > 0 || second > 0 || third > 0;
            if (has_negative && has_positive) continue;
            auto stored = target.set_pixel(x_value,
                                           y_value,
                                           context.color,
                                           true);
            if (!stored) return stored;
        }
    }
    return {};
}

Status draw_image(Image& target,
                  const GraphicsContext& context,
                  const Image& source,
                  i32 x,
                  i32 y,
                  i32 anchor) {
    auto placement = anchored_rect(saturated_add(x, context.translate_x),
                                   saturated_add(y, context.translate_y),
                                   source.width(),
                                   source.height(),
                                   anchor,
                                   false);
    if (!placement) return std::unexpected(placement.error());
    auto mutable_target = require_mutable(target);
    if (!mutable_target) return mutable_target;
    const std::vector<Pixel> source_pixels(source.pixels().begin(),
                                           source.pixels().end());
    for (i32 row = 0; row < source.height(); ++row) {
        for (i32 column = 0; column < source.width(); ++column) {
            const Pixel pixel_value =
                source_pixels[static_cast<usize>(row) *
                                  static_cast<usize>(source.width()) +
                              static_cast<usize>(column)];
            auto stored = put_pixel(target,
                                    context,
                                    placement->x + column,
                                    placement->y + row,
                                    pixel_value,
                                    true);
            if (!stored) return stored;
        }
    }
    return {};
}

Status draw_region(Image& target,
                   const GraphicsContext& context,
                   const Image& source,
                   i32 source_x,
                   i32 source_y,
                   i32 width,
                   i32 height,
                   Transform transform_value,
                   i32 x,
                   i32 y,
                   i32 anchor) {
    auto region = Image::transformed_region(source,
                                            source_x,
                                            source_y,
                                            width,
                                            height,
                                            transform_value);
    if (!region) return std::unexpected(region.error());
    return draw_image(target, context, *region, x, y, anchor);
}

Status copy_area(Image& target,
                 const GraphicsContext& context,
                 i32 source_x,
                 i32 source_y,
                 i32 width,
                 i32 height,
                 i32 x,
                 i32 y,
                 i32 anchor) {
    const i32 absolute_source_x = saturated_add(source_x, context.translate_x);
    const i32 absolute_source_y = saturated_add(source_y, context.translate_y);
    auto region = Image::transformed_region(target,
                                            absolute_source_x,
                                            absolute_source_y,
                                            width,
                                            height,
                                            Transform::none);
    if (!region) return std::unexpected(region.error());
    return draw_image(target, context, *region, x, y, anchor);
}

Status draw_rgb(Image& target,
                const GraphicsContext& context,
                std::span<const Pixel> pixels,
                i32 offset,
                i32 scan_length,
                i32 x,
                i32 y,
                i32 width,
                i32 height,
                bool process_alpha) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) return mutable_target;
    if (width < 0 || height < 0) {
        return fail(ErrorCode::invalid_argument,
                    "drawRGB dimensions cannot be negative");
    }
    if (width == 0 || height == 0) return {};
    if (scan_length == 0) {
        return fail(ErrorCode::invalid_argument,
                    "drawRGB scanlength cannot be zero");
    }
    const i64 first = offset;
    const i64 last_row = static_cast<i64>(offset) +
                         static_cast<i64>(height - 1) * scan_length;
    const i64 minimum = std::min(first, last_row);
    const i64 maximum = std::max(first, last_row) + width - 1;
    if (minimum < 0 || maximum < 0 ||
        static_cast<u64>(maximum) >= pixels.size()) {
        return fail(ErrorCode::out_of_range,
                    "drawRGB source slice exceeds int[]");
    }
    const i32 absolute_x = saturated_add(x, context.translate_x);
    const i32 absolute_y = saturated_add(y, context.translate_y);
    for (i32 row = 0; row < height; ++row) {
        const i64 row_start = static_cast<i64>(offset) +
                              static_cast<i64>(row) * scan_length;
        for (i32 column = 0; column < width; ++column) {
            Pixel pixel_value = pixels[static_cast<usize>(
                row_start + column)];
            if (!process_alpha) pixel_value = opaque(pixel_value);
            auto stored = put_pixel(target,
                                    context,
                                    absolute_x + column,
                                    absolute_y + row,
                                    pixel_value,
                                    process_alpha);
            if (!stored) return stored;
        }
    }
    return {};
}

Status draw_text(Image& target,
                 const GraphicsContext& context,
                 std::span<const char32_t> text,
                 i32 x,
                 i32 y,
                 i32 anchor) {
    auto mutable_target = require_mutable(target);
    if (!mutable_target) return mutable_target;
    const i32 width = context.font.chars_width(text);
    auto placement = anchored_rect(saturated_add(x, context.translate_x),
                                   saturated_add(y, context.translate_y),
                                   width,
                                   context.font.height(),
                                   anchor,
                                   true,
                                   context.font.baseline());
    if (!placement) return std::unexpected(placement.error());
    auto platform = draw_platform_text(target,
                                       context.font,
                                       text,
                                       placement->x,
                                       placement->y,
                                       context.color,
                                       context.clip);
    if (platform) {
        if (context.font.is_underlined() && width > 0) {
            return draw_line_absolute(
                target,
                context,
                placement->x,
                placement->y + context.font.baseline() + 1,
                placement->x + width - 1,
                placement->y + context.font.baseline() + 1);
        }
        return {};
    }
    i32 cursor = placement->x;
    for (char32_t character : text) {
        auto status = draw_glyph(target,
                                 context,
                                 character,
                                 cursor,
                                 placement->y);
        if (!status) return status;
        cursor = saturated_add(cursor, context.font.char_width(character));
    }
    if (context.font.is_underlined() && width > 0) {
        return draw_line_absolute(target,
                                  context,
                                  placement->x,
                                  placement->y + context.font.baseline() + 1,
                                  placement->x + width - 1,
                                  placement->y + context.font.baseline() + 1);
    }
    return {};
}

} // namespace phoneme::graphics
