#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "PhoneMEMetal3D.h"

namespace {

struct MetalTextureDescriptor final {
    uint32_t offset;
    int32_t width;
    int32_t height;
};

struct MetalRasterParameters final {
    int32_t width;
    int32_t height;
    uint32_t texture_count;
    uint32_t triangle_count;
    int32_t origin_x;
    int32_t origin_y;
    uint32_t reserved_0;
    uint32_t reserved_1;
};

static_assert(sizeof(PhoneMEMetal3DVertex) == 13U * sizeof(float));
static_assert(sizeof(MetalTextureDescriptor) == 3U * sizeof(uint32_t));
static_assert(sizeof(MetalRasterParameters) == 8U * sizeof(uint32_t));

NSString *const kPhoneMEMetal3DShader = @R"METAL(
#include <metal_stdlib>
using namespace metal;

struct RasterVertex {
    float x;
    float y;
    float depth;
    float inverseW;
    float normalXOverW;
    float normalYOverW;
    float normalZOverW;
    float uOverW;
    float vOverW;
    float alphaOverW;
    float redOverW;
    float greenOverW;
    float blueOverW;
};

struct RasterTriangle {
    RasterVertex vertices[3];
    int minimumX;
    int minimumY;
    int maximumX;
    int maximumY;
    int textureIndex;
    int sphereTextureIndex;
    uint blendMode;
    uint flags;
    float ambient;
    float directional;
    float lightX;
    float lightY;
    float lightZ;
    float toonThreshold;
    float toonHigh;
    float toonLow;
};

struct TextureDescriptor {
    uint offset;
    int width;
    int height;
};

struct RasterParameters {
    int width;
    int height;
    uint textureCount;
    uint triangleCount;
    int originX;
    int originY;
    uint reserved0;
    uint reserved1;
};

constant uint kLighting = 1u << 0u;
constant uint kSphereMap = 1u << 1u;
constant uint kColorKey = 1u << 2u;
constant uint kToon = 1u << 3u;

inline float edgeValue(float ax, float ay, float bx, float by,
                       float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

inline uint alphaOf(uint value) { return (value >> 24u) & 255u; }
inline uint redOf(uint value) { return (value >> 16u) & 255u; }
inline uint greenOf(uint value) { return (value >> 8u) & 255u; }
inline uint blueOf(uint value) { return value & 255u; }

inline uint argb(uint alpha, uint red, uint green, uint blue) {
    return ((alpha & 255u) << 24u) | ((red & 255u) << 16u) |
           ((green & 255u) << 8u) | (blue & 255u);
}

inline uint opaque(uint value) { return value | 0xFF000000u; }

inline uint textureSample(device const uint *texturePixels,
                          constant TextureDescriptor *textures,
                          uint textureCount,
                          int textureIndex,
                          float u,
                          float v) {
    if (textureIndex < 0 || uint(textureIndex) >= textureCount) return 0u;
    TextureDescriptor descriptor = textures[textureIndex];
    if (descriptor.width <= 0 || descriptor.height <= 0) return 0u;
    int x = clamp(int(floor(u)), 0, descriptor.width - 1);
    int y = clamp(int(floor(v)), 0, descriptor.height - 1);
    uint index = descriptor.offset + uint(y * descriptor.width + x);
    return texturePixels[index];
}

inline uint modulate(uint left, uint right) {
    uint alpha = (alphaOf(left) * alphaOf(right) + 127u) / 255u;
    uint red = (redOf(left) * redOf(right) + 127u) / 255u;
    uint green = (greenOf(left) * greenOf(right) + 127u) / 255u;
    uint blue = (blueOf(left) * blueOf(right) + 127u) / 255u;
    return argb(alpha, red, green, blue);
}

inline uint blendPixel(uint source, uint destination, uint mode) {
    if (mode == 2u) {
        return argb(255u,
            (redOf(source) + redOf(destination) + 1u) / 2u,
            (greenOf(source) + greenOf(destination) + 1u) / 2u,
            (blueOf(source) + blueOf(destination) + 1u) / 2u);
    }
    if (mode == 4u) {
        return argb(255u,
            min(255u, redOf(source) + redOf(destination)),
            min(255u, greenOf(source) + greenOf(destination)),
            min(255u, blueOf(source) + blueOf(destination)));
    }
    if (mode == 6u) {
        return argb(alphaOf(destination),
            redOf(destination) > redOf(source)
                ? redOf(destination) - redOf(source) : 0u,
            greenOf(destination) > greenOf(source)
                ? greenOf(destination) - greenOf(source) : 0u,
            blueOf(destination) > blueOf(source)
                ? blueOf(destination) - blueOf(source) : 0u);
    }
    return opaque(source);
}

kernel void phoneMEMicro3DRaster(
    device uint *pixels [[buffer(0)]],
    device float *depthBuffer [[buffer(1)]],
    device const RasterTriangle *triangles [[buffer(2)]],
    device const uint *texturePixels [[buffer(3)]],
    constant TextureDescriptor *textures [[buffer(4)]],
    constant RasterParameters &parameters [[buffer(5)]],
    uint2 gid [[thread_position_in_grid]]) {
    int x = parameters.originX + int(gid.x);
    int y = parameters.originY + int(gid.y);
    if (x < 0 || y < 0 || x >= parameters.width || y >= parameters.height) {
        return;
    }

    uint pixelIndex = uint(y * parameters.width + x);
    uint currentPixel = pixels[pixelIndex];
    float currentDepth = depthBuffer[pixelIndex];
    float sampleX = float(x) + 0.5f;
    float sampleY = float(y) + 0.5f;

    for (uint triangleIndex = 0u;
         triangleIndex < parameters.triangleCount;
         ++triangleIndex) {
        device const RasterTriangle &triangle = triangles[triangleIndex];
        if (x < triangle.minimumX || x > triangle.maximumX ||
            y < triangle.minimumY || y > triangle.maximumY) {
            continue;
        }

        RasterVertex first = triangle.vertices[0];
        RasterVertex second = triangle.vertices[1];
        RasterVertex third = triangle.vertices[2];
        float area = edgeValue(first.x, first.y, second.x, second.y,
                               third.x, third.y);
        if (!isfinite(area) || abs(area) < 1.0e-6f) continue;

        float firstEdge = edgeValue(second.x, second.y, third.x, third.y,
                                    sampleX, sampleY);
        float secondEdge = edgeValue(third.x, third.y, first.x, first.y,
                                     sampleX, sampleY);
        float thirdEdge = edgeValue(first.x, first.y, second.x, second.y,
                                    sampleX, sampleY);
        bool inside = area > 0.0f
            ? firstEdge >= 0.0f && secondEdge >= 0.0f && thirdEdge >= 0.0f
            : firstEdge <= 0.0f && secondEdge <= 0.0f && thirdEdge <= 0.0f;
        if (!inside) continue;

        float firstWeight = firstEdge / area;
        float secondWeight = secondEdge / area;
        float thirdWeight = thirdEdge / area;
        float pixelDepth = firstWeight * first.depth +
                           secondWeight * second.depth +
                           thirdWeight * third.depth;
        if (pixelDepth > currentDepth + 1.0e-6f) continue;

        float denominator = firstWeight * first.inverseW +
                            secondWeight * second.inverseW +
                            thirdWeight * third.inverseW;
        if (abs(denominator) <= 1.0e-8f) continue;

        float alpha = (firstWeight * first.alphaOverW +
                       secondWeight * second.alphaOverW +
                       thirdWeight * third.alphaOverW) / denominator;
        float red = (firstWeight * first.redOverW +
                     secondWeight * second.redOverW +
                     thirdWeight * third.redOverW) / denominator;
        float green = (firstWeight * first.greenOverW +
                       secondWeight * second.greenOverW +
                       thirdWeight * third.greenOverW) / denominator;
        float blue = (firstWeight * first.blueOverW +
                      secondWeight * second.blueOverW +
                      thirdWeight * third.blueOverW) / denominator;
        uint source = argb(
            uint(clamp(alpha, 0.0f, 1.0f) * 255.0f),
            uint(clamp(red, 0.0f, 1.0f) * 255.0f),
            uint(clamp(green, 0.0f, 1.0f) * 255.0f),
            uint(clamp(blue, 0.0f, 1.0f) * 255.0f));

        if (triangle.textureIndex >= 0) {
            float u = (firstWeight * first.uOverW +
                       secondWeight * second.uOverW +
                       thirdWeight * third.uOverW) / denominator;
            float v = (firstWeight * first.vOverW +
                       secondWeight * second.vOverW +
                       thirdWeight * third.vOverW) / denominator;
            uint texture = textureSample(texturePixels, textures,
                                         parameters.textureCount,
                                         triangle.textureIndex, u, v);
            if ((triangle.flags & kColorKey) != 0u &&
                alphaOf(texture) < 128u) {
                continue;
            }
            source = opaque(modulate(source, texture));
        }

        float3 normal = float3(
            (firstWeight * first.normalXOverW +
             secondWeight * second.normalXOverW +
             thirdWeight * third.normalXOverW) / denominator,
            (firstWeight * first.normalYOverW +
             secondWeight * second.normalYOverW +
             thirdWeight * third.normalYOverW) / denominator,
            (firstWeight * first.normalZOverW +
             secondWeight * second.normalZOverW +
             thirdWeight * third.normalZOverW) / denominator);
        float normalLength = length(normal);
        normal = normalLength > 1.0e-8f
            ? normal / normalLength : float3(0.0f, 0.0f, 1.0f);

        float light = 1.0f;
        if ((triangle.flags & kLighting) != 0u) {
            float directional = max(0.0f, dot(
                normal,
                float3(triangle.lightX, triangle.lightY, triangle.lightZ)));
            light = min(1.0f,
                triangle.ambient + triangle.directional * directional);
            if ((triangle.flags & kToon) != 0u) {
                light = light < triangle.toonThreshold
                    ? triangle.toonLow : triangle.toonHigh;
            }
        }

        float shadedRed = float(redOf(source)) * light;
        float shadedGreen = float(greenOf(source)) * light;
        float shadedBlue = float(blueOf(source)) * light;
        if ((triangle.flags & kSphereMap) != 0u &&
            triangle.sphereTextureIndex >= 0) {
            uint reflected = textureSample(
                texturePixels, textures, parameters.textureCount,
                triangle.sphereTextureIndex,
                normal.x / 128.0f + 32.0f,
                normal.y / 128.0f + 32.0f);
            shadedRed += float(redOf(reflected));
            shadedGreen += float(greenOf(reflected));
            shadedBlue += float(blueOf(reflected));
        }
        source = argb(alphaOf(source),
            uint(clamp(shadedRed, 0.0f, 255.0f)),
            uint(clamp(shadedGreen, 0.0f, 255.0f)),
            uint(clamp(shadedBlue, 0.0f, 255.0f)));

        currentPixel = blendPixel(source, currentPixel, triangle.blendMode);
        if (triangle.blendMode == 0u) currentDepth = pixelDepth;
    }

    pixels[pixelIndex] = currentPixel;
    depthBuffer[pixelIndex] = currentDepth;
}
)METAL";

} // namespace

@interface PhoneMEMetal3DContext : NSObject
@property(nonatomic, strong, readonly) id<MTLDevice> device;
@property(nonatomic, strong, readonly) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong, readonly) id<MTLComputePipelineState> pipeline;
+ (nullable instancetype)sharedContext;
- (nullable instancetype)initWithDevice:(id<MTLDevice>)device;
@end

@implementation PhoneMEMetal3DContext

+ (nullable instancetype)sharedContext {
    static PhoneMEMetal3DContext *context;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device != nil) {
            context = [[PhoneMEMetal3DContext alloc] initWithDevice:device];
        }
    });
    return context;
}

- (nullable instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self == nil) return nil;
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:kPhoneMEMetal3DShader
                                                   options:nil
                                                     error:&error];
    id<MTLFunction> function = [library newFunctionWithName:@"phoneMEMicro3DRaster"];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLComputePipelineState> pipeline = function == nil
        ? nil : [device newComputePipelineStateWithFunction:function error:&error];
    if (library == nil || function == nil || queue == nil || pipeline == nil) {
        return nil;
    }
    _device = device;
    _commandQueue = queue;
    _pipeline = pipeline;
    return self;
}

@end

namespace {

bool multiplyFits(size_t left, size_t right, size_t limit) noexcept {
    return left == 0U || right <= limit / left;
}

} // namespace

extern "C" bool phoneme_metal3d_rasterize(
    uint32_t *pixels,
    float *depth,
    int32_t width,
    int32_t height,
    const PhoneMEMetal3DTriangle *triangles,
    size_t triangle_count,
    const PhoneMEMetal3DTextureSource *textures,
    size_t texture_count) {
    @autoreleasepool {
        if (pixels == nullptr || depth == nullptr || width <= 0 || height <= 0 ||
            triangles == nullptr || triangle_count == 0U ||
            !multiplyFits(static_cast<size_t>(width),
                          static_cast<size_t>(height),
                          std::numeric_limits<size_t>::max())) {
            return false;
        }
        const size_t pixelCount = static_cast<size_t>(width) *
                                  static_cast<size_t>(height);
        if (!multiplyFits(pixelCount, sizeof(uint32_t),
                          std::numeric_limits<size_t>::max()) ||
            !multiplyFits(pixelCount, sizeof(float),
                          std::numeric_limits<size_t>::max()) ||
            !multiplyFits(triangle_count, sizeof(PhoneMEMetal3DTriangle),
                          std::numeric_limits<size_t>::max())) {
            return false;
        }

        PhoneMEMetal3DContext *context =
            [PhoneMEMetal3DContext sharedContext];
        if (context == nil) return false;

        std::vector<uint32_t> packedTexturePixels;
        std::vector<MetalTextureDescriptor> packedTextures;
        packedTextures.reserve(texture_count);
        for (size_t index = 0U; index < texture_count; ++index) {
            const PhoneMEMetal3DTextureSource &source = textures[index];
            if (source.pixels == nullptr || source.width <= 0 ||
                source.height <= 0 ||
                !multiplyFits(static_cast<size_t>(source.width),
                              static_cast<size_t>(source.height),
                              std::numeric_limits<size_t>::max())) {
                return false;
            }
            const size_t count = static_cast<size_t>(source.width) *
                                 static_cast<size_t>(source.height);
            if (packedTexturePixels.size() >
                    static_cast<size_t>(std::numeric_limits<uint32_t>::max()) ||
                count > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) -
                        packedTexturePixels.size()) {
                return false;
            }
            packedTextures.push_back(MetalTextureDescriptor {
                .offset = static_cast<uint32_t>(packedTexturePixels.size()),
                .width = source.width,
                .height = source.height,
            });
            packedTexturePixels.insert(packedTexturePixels.end(),
                                       source.pixels, source.pixels + count);
        }
        if (packedTexturePixels.empty()) packedTexturePixels.push_back(0U);
        if (packedTextures.empty()) {
            packedTextures.push_back(MetalTextureDescriptor {0U, 0, 0});
        }

        const size_t pixelBytes = pixelCount * sizeof(uint32_t);
        const size_t depthBytes = pixelCount * sizeof(float);
        id<MTLBuffer> pixelBuffer = [context.device newBufferWithBytes:pixels
                                                               length:pixelBytes
                                                              options:MTLResourceStorageModeShared];
        id<MTLBuffer> depthBuffer = [context.device newBufferWithBytes:depth
                                                               length:depthBytes
                                                              options:MTLResourceStorageModeShared];
        id<MTLBuffer> triangleBuffer = [context.device newBufferWithBytes:triangles
                                                                  length:triangle_count * sizeof(PhoneMEMetal3DTriangle)
                                                                 options:MTLResourceStorageModeShared];
        id<MTLBuffer> texturePixelBuffer = [context.device newBufferWithBytes:packedTexturePixels.data()
                                                                      length:packedTexturePixels.size() * sizeof(uint32_t)
                                                                     options:MTLResourceStorageModeShared];
        id<MTLBuffer> textureDescriptorBuffer = [context.device newBufferWithBytes:packedTextures.data()
                                                                           length:packedTextures.size() * sizeof(MetalTextureDescriptor)
                                                                          options:MTLResourceStorageModeShared];
        if (pixelBuffer == nil || depthBuffer == nil || triangleBuffer == nil ||
            texturePixelBuffer == nil || textureDescriptorBuffer == nil) {
            return false;
        }

        id<MTLCommandBuffer> commandBuffer = [context.commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> encoder =
            [commandBuffer computeCommandEncoder];
        if (commandBuffer == nil || encoder == nil) return false;
        [encoder setComputePipelineState:context.pipeline];
        [encoder setBuffer:pixelBuffer offset:0 atIndex:0];
        [encoder setBuffer:depthBuffer offset:0 atIndex:1];
        [encoder setBuffer:texturePixelBuffer offset:0 atIndex:3];
        [encoder setBuffer:textureDescriptorBuffer offset:0 atIndex:4];
        MetalRasterParameters parameters {
            .width = width,
            .height = height,
            .texture_count = static_cast<uint32_t>(texture_count),
            .triangle_count = 1U,
            .origin_x = 0,
            .origin_y = 0,
            .reserved_0 = 0U,
            .reserved_1 = 0U,
        };

        const NSUInteger executionWidth = std::max<NSUInteger>(
            1U, std::min<NSUInteger>(8U, context.pipeline.threadExecutionWidth));
        const NSUInteger executionHeight = std::max<NSUInteger>(
            1U, std::min<NSUInteger>(8U,
                context.pipeline.maxTotalThreadsPerThreadgroup / executionWidth));
        const MTLSize threadsPerGroup = MTLSizeMake(
            executionWidth, executionHeight, 1U);
        for (size_t index = 0U; index < triangle_count; ++index) {
            const PhoneMEMetal3DTriangle &triangle = triangles[index];
            if (triangle.maximum_x < triangle.minimum_x ||
                triangle.maximum_y < triangle.minimum_y) {
                continue;
            }
            const NSUInteger regionWidth = static_cast<NSUInteger>(
                triangle.maximum_x - triangle.minimum_x + 1);
            const NSUInteger regionHeight = static_cast<NSUInteger>(
                triangle.maximum_y - triangle.minimum_y + 1);
            parameters.origin_x = triangle.minimum_x;
            parameters.origin_y = triangle.minimum_y;
            [encoder setBuffer:triangleBuffer
                        offset:index * sizeof(PhoneMEMetal3DTriangle)
                       atIndex:2];
            [encoder setBytes:&parameters length:sizeof(parameters) atIndex:5];
            [encoder dispatchThreads:MTLSizeMake(regionWidth, regionHeight, 1U)
               threadsPerThreadgroup:threadsPerGroup];
            if (index + 1U < triangle_count) {
                [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
            }
        }
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status != MTLCommandBufferStatusCompleted ||
            commandBuffer.error != nil) {
            return false;
        }
        std::memcpy(pixels, pixelBuffer.contents, pixelBytes);
        std::memcpy(depth, depthBuffer.contents, depthBytes);
        return true;
    }
}

__attribute__((constructor)) static void PhoneMEInstallMetal3DRasterizer(void) {
    phoneme_metal3d_set_rasterizer(&phoneme_metal3d_rasterize);
}
