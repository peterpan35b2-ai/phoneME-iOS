#pragma once

#include <span>

#include "phoneme/graphics/Font.hpp"
#include "phoneme/graphics/Image.hpp"

namespace phoneme::graphics {

constexpr i32 anchor_hcenter = 1;
constexpr i32 anchor_vcenter = 2;
constexpr i32 anchor_left = 4;
constexpr i32 anchor_right = 8;
constexpr i32 anchor_top = 16;
constexpr i32 anchor_bottom = 32;
constexpr i32 anchor_baseline = 64;

constexpr i32 stroke_solid = 0;
constexpr i32 stroke_dotted = 1;

struct Rect final {
    i32 x {0};
    i32 y {0};
    i32 width {0};
    i32 height {0};
};

struct GraphicsContext final {
    u64 target_key {0};
    bool display_target {false};
    bool rendering_enabled {true};
    Pixel color {0xFF000000U};
    i32 translate_x {0};
    i32 translate_y {0};
    Rect clip {};
    i32 stroke_style {stroke_solid};
    Font font {Font::default_font()};
};

[[nodiscard]] Rect intersect(Rect left, Rect right) noexcept;
[[nodiscard]] bool empty(Rect rectangle) noexcept;
[[nodiscard]] Rect target_bounds(const Image& image) noexcept;

[[nodiscard]] Status set_clip(GraphicsContext& context,
                              const Image& target,
                              i32 x,
                              i32 y,
                              i32 width,
                              i32 height);
[[nodiscard]] Status clip_rect(GraphicsContext& context,
                               const Image& target,
                               i32 x,
                               i32 y,
                               i32 width,
                               i32 height);
void translate(GraphicsContext& context, i32 x, i32 y) noexcept;

[[nodiscard]] Status draw_line(Image& target,
                               const GraphicsContext& context,
                               i32 x1,
                               i32 y1,
                               i32 x2,
                               i32 y2);
[[nodiscard]] Status fill_rect(Image& target,
                               const GraphicsContext& context,
                               i32 x,
                               i32 y,
                               i32 width,
                               i32 height);
[[nodiscard]] Status clear_rect(Image& target,
                                const GraphicsContext& context,
                                i32 x,
                                i32 y,
                                i32 width,
                                i32 height);
[[nodiscard]] Status draw_rect(Image& target,
                               const GraphicsContext& context,
                               i32 x,
                               i32 y,
                               i32 width,
                               i32 height);
[[nodiscard]] Status draw_round_rect(Image& target,
                                     const GraphicsContext& context,
                                     i32 x,
                                     i32 y,
                                     i32 width,
                                     i32 height,
                                     i32 arc_width,
                                     i32 arc_height,
                                     bool fill);
[[nodiscard]] Status draw_arc(Image& target,
                              const GraphicsContext& context,
                              i32 x,
                              i32 y,
                              i32 width,
                              i32 height,
                              i32 start_angle,
                              i32 arc_angle,
                              bool fill);
[[nodiscard]] Status fill_triangle(Image& target,
                                   const GraphicsContext& context,
                                   i32 x1,
                                   i32 y1,
                                   i32 x2,
                                   i32 y2,
                                   i32 x3,
                                   i32 y3);
[[nodiscard]] Status draw_image(Image& target,
                                const GraphicsContext& context,
                                const Image& source,
                                i32 x,
                                i32 y,
                                i32 anchor);
[[nodiscard]] Status draw_region(Image& target,
                                 const GraphicsContext& context,
                                 const Image& source,
                                 i32 source_x,
                                 i32 source_y,
                                 i32 width,
                                 i32 height,
                                 Transform transform,
                                 i32 x,
                                 i32 y,
                                 i32 anchor);
[[nodiscard]] Status copy_area(Image& target,
                               const GraphicsContext& context,
                               i32 source_x,
                               i32 source_y,
                               i32 width,
                               i32 height,
                               i32 x,
                               i32 y,
                               i32 anchor);
[[nodiscard]] Status draw_rgb(Image& target,
                              const GraphicsContext& context,
                              std::span<const Pixel> pixels,
                              i32 offset,
                              i32 scan_length,
                              i32 x,
                              i32 y,
                              i32 width,
                              i32 height,
                              bool process_alpha);
[[nodiscard]] Status draw_text(Image& target,
                               const GraphicsContext& context,
                               std::span<const char32_t> text,
                               i32 x,
                               i32 y,
                               i32 anchor);

[[nodiscard]] Result<Rect> anchored_rect(i32 x,
                                         i32 y,
                                         i32 width,
                                         i32 height,
                                         i32 anchor,
                                         bool text,
                                         i32 baseline = 0);

} // namespace phoneme::graphics
