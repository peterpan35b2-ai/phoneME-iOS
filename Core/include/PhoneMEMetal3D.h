#ifndef PHONEME_METAL3D_H
#define PHONEME_METAL3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PhoneMEMetal3DVertex {
    float x;
    float y;
    float depth;
    float inverse_w;
    float normal_x_over_w;
    float normal_y_over_w;
    float normal_z_over_w;
    float u_over_w;
    float v_over_w;
    float alpha_over_w;
    float red_over_w;
    float green_over_w;
    float blue_over_w;
} PhoneMEMetal3DVertex;

typedef struct PhoneMEMetal3DTextureSource {
    const uint32_t *pixels;
    int32_t width;
    int32_t height;
} PhoneMEMetal3DTextureSource;

typedef struct PhoneMEMetal3DTriangle {
    PhoneMEMetal3DVertex vertices[3];
    int32_t minimum_x;
    int32_t minimum_y;
    int32_t maximum_x;
    int32_t maximum_y;
    int32_t texture_index;
    int32_t sphere_texture_index;
    uint32_t blend_mode;
    uint32_t flags;
    float ambient;
    float directional;
    float light_x;
    float light_y;
    float light_z;
    float toon_threshold;
    float toon_high;
    float toon_low;
} PhoneMEMetal3DTriangle;

enum {
    PHONEME_METAL3D_LIGHTING = 1U << 0U,
    PHONEME_METAL3D_SPHERE_MAP = 1U << 1U,
    PHONEME_METAL3D_COLOR_KEY = 1U << 2U,
    PHONEME_METAL3D_TOON = 1U << 3U,
};

typedef bool (*PhoneMEMetal3DRasterizer)(
    uint32_t *pixels,
    float *depth,
    int32_t width,
    int32_t height,
    const PhoneMEMetal3DTriangle *triangles,
    size_t triangle_count,
    const PhoneMEMetal3DTextureSource *textures,
    size_t texture_count);

void phoneme_metal3d_set_rasterizer(PhoneMEMetal3DRasterizer rasterizer);
PhoneMEMetal3DRasterizer phoneme_metal3d_get_rasterizer(void);

bool phoneme_metal3d_rasterize(
    uint32_t *pixels,
    float *depth,
    int32_t width,
    int32_t height,
    const PhoneMEMetal3DTriangle *triangles,
    size_t triangle_count,
    const PhoneMEMetal3DTextureSource *textures,
    size_t texture_count);

#ifdef __cplusplus
}
#endif

#endif
