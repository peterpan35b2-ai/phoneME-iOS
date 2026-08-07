#include "phoneme/graphics/ImageDecoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#include "phoneme/graphics/Color.hpp"
#include "phoneme/graphics/PngDecoder.hpp"

namespace phoneme::graphics {
namespace {

constexpr std::array<u8, 8> kPngSignature {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
};

[[nodiscard]] bool has_prefix(std::span<const u8> bytes,
                              std::span<const u8> prefix) noexcept {
    return bytes.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

[[nodiscard]] bool is_jpeg(std::span<const u8> bytes) noexcept {
    return bytes.size() >= 3U && bytes[0] == 0xFFU &&
           bytes[1] == 0xD8U && bytes[2] == 0xFFU;
}

[[nodiscard]] bool is_gif(std::span<const u8> bytes) noexcept {
    constexpr std::array<u8, 6> kGif87a {'G', 'I', 'F', '8', '7', 'a'};
    constexpr std::array<u8, 6> kGif89a {'G', 'I', 'F', '8', '9', 'a'};
    return has_prefix(bytes, kGif87a) || has_prefix(bytes, kGif89a);
}

#if defined(__APPLE__)
[[nodiscard]] Result<i32> image_property_dimension(
    CFDictionaryRef properties,
    CFStringRef key,
    std::string_view name) {
    if (properties == nullptr) {
        return fail(ErrorCode::malformed_archive,
                    "ImageIO did not return image properties");
    }
    const auto value = static_cast<CFTypeRef>(
        CFDictionaryGetValue(properties, key));
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return fail(ErrorCode::malformed_archive,
                    "ImageIO image " + std::string(name) + " is missing");
    }
    i64 dimension = 0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(value),
                          kCFNumberSInt64Type,
                          &dimension) ||
        dimension <= 0 ||
        dimension > std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "ImageIO image " + std::string(name) +
                        " is outside the supported range");
    }
    return static_cast<i32>(dimension);
}

[[nodiscard]] u8 unpremultiply(u8 component, u8 alpha_value) noexcept {
    if (alpha_value == 0U) return 0U;
    if (alpha_value == 255U) return component;
    const u32 expanded = static_cast<u32>(component) * 255U +
                         static_cast<u32>(alpha_value) / 2U;
    return static_cast<u8>(std::min<u32>(255U,
        expanded / static_cast<u32>(alpha_value)));
}

[[nodiscard]] Result<Image> decode_apple_raster(std::span<const u8> bytes) {
    if (bytes.empty()) {
        return fail(ErrorCode::malformed_archive, "image data is empty");
    }
    if (bytes.size() > static_cast<usize>(
            std::numeric_limits<CFIndex>::max())) {
        return fail(ErrorCode::overflow,
                    "compressed image data exceeds ImageIO address space");
    }

    CFDataRef data = CFDataCreate(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(bytes.data()),
        static_cast<CFIndex>(bytes.size()));
    if (data == nullptr) {
        return fail(ErrorCode::overflow,
                    "ImageIO compressed data allocation failed");
    }
    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (source == nullptr || CGImageSourceGetCount(source) == 0U) {
        if (source != nullptr) CFRelease(source);
        return fail(ErrorCode::malformed_archive,
                    "ImageIO could not parse JPEG/GIF data");
    }

    CFDictionaryRef properties =
        CGImageSourceCopyPropertiesAtIndex(source, 0U, nullptr);
    auto width = image_property_dimension(
        properties, kCGImagePropertyPixelWidth, "width");
    auto height = image_property_dimension(
        properties, kCGImagePropertyPixelHeight, "height");
    if (properties != nullptr) CFRelease(properties);
    if (!width || !height) {
        CFRelease(source);
        return !width ? std::unexpected(width.error())
                      : std::unexpected(height.error());
    }
    auto count = validated_pixel_count(*width, *height);
    if (!count) {
        CFRelease(source);
        return std::unexpected(count.error());
    }

    CGImageRef decoded = CGImageSourceCreateImageAtIndex(source, 0U, nullptr);
    CFRelease(source);
    if (decoded == nullptr) {
        return fail(ErrorCode::malformed_archive,
                    "ImageIO failed to decode JPEG/GIF pixels");
    }
    const usize decoded_width = static_cast<usize>(CGImageGetWidth(decoded));
    const usize decoded_height = static_cast<usize>(CGImageGetHeight(decoded));
    if (decoded_width != static_cast<usize>(*width) ||
        decoded_height != static_cast<usize>(*height)) {
        CGImageRelease(decoded);
        return fail(ErrorCode::malformed_archive,
                    "ImageIO decoded dimensions conflict with metadata");
    }
    if (*count > std::numeric_limits<usize>::max() / 4U) {
        CGImageRelease(decoded);
        return fail(ErrorCode::overflow,
                    "decoded image byte count overflows size_t");
    }

    const usize row_bytes = static_cast<usize>(*width) * 4U;
    std::vector<u8> rgba(*count * 4U, 0U);
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    if (color_space == nullptr) {
        CGImageRelease(decoded);
        return fail(ErrorCode::overflow,
                    "ImageIO RGB color space allocation failed");
    }
    CGContextRef context = CGBitmapContextCreate(
        rgba.data(),
        static_cast<usize>(*width),
        static_cast<usize>(*height),
        8U,
        row_bytes,
        color_space,
        static_cast<CGBitmapInfo>(
            static_cast<u32>(kCGImageAlphaPremultipliedLast) |
            static_cast<u32>(kCGBitmapByteOrder32Big)));
    CGColorSpaceRelease(color_space);
    if (context == nullptr) {
        CGImageRelease(decoded);
        return fail(ErrorCode::overflow,
                    "ImageIO bitmap context allocation failed");
    }

    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(
        context,
        CGRectMake(0.0, 0.0,
                   static_cast<CGFloat>(*width),
                   static_cast<CGFloat>(*height)),
        decoded);
    CGContextRelease(context);
    CGImageRelease(decoded);

    std::vector<Pixel> pixels;
    pixels.reserve(*count);
    for (usize index = 0U; index < *count; ++index) {
        const usize offset = index * 4U;
        const u8 alpha_value = rgba[offset + 3U];
        pixels.push_back(argb(
            alpha_value,
            unpremultiply(rgba[offset], alpha_value),
            unpremultiply(rgba[offset + 1U], alpha_value),
            unpremultiply(rgba[offset + 2U], alpha_value)));
    }
    return Image::create_immutable_owned(
        *width, *height, std::move(pixels));
}
#endif

} // namespace

Result<Image> decode_image(std::span<const u8> bytes) {
    if (has_prefix(bytes, kPngSignature)) return decode_png(bytes);
#if defined(__APPLE__)
    if (is_jpeg(bytes) || is_gif(bytes)) return decode_apple_raster(bytes);
    return fail(ErrorCode::unsupported_feature,
                "unsupported LCDUI image format; expected PNG, JPEG or GIF");
#else
    if (is_jpeg(bytes) || is_gif(bytes)) {
        return fail(ErrorCode::unsupported_feature,
                    "JPEG/GIF decoding is unavailable on this platform");
    }
    return fail(ErrorCode::unsupported_feature,
                "unsupported LCDUI image format; expected PNG");
#endif
}

} // namespace phoneme::graphics
