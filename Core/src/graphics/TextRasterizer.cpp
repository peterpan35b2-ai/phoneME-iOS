#include "phoneme/graphics/TextRasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "phoneme/graphics/Graphics.hpp"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

namespace phoneme::graphics {
namespace {

#if defined(__APPLE__)

constexpr usize kMaximumTextCodePoints = 262'144U;

struct CachedFont final {
    CTFontRef font {nullptr};
    PlatformFontMetrics metrics {};

    CachedFont(CTFontRef value, PlatformFontMetrics measured) noexcept
        : font(value), metrics(measured) {}

    ~CachedFont() {
        if (font != nullptr) {
            CFRelease(font);
        }
    }

    CachedFont(const CachedFont&) = delete;
    CachedFont& operator=(const CachedFont&) = delete;
};

[[nodiscard]] i32 font_cache_key(const Font& font) noexcept {
    return (font.face() & 0xFF) |
           ((font.style() & 0xFF) << 8) |
           ((font.size() & 0xFF) << 16);
}

[[nodiscard]] CGFloat point_size(const Font& font) noexcept {
    switch (static_cast<FontSize>(font.size())) {
    case FontSize::small:
        return 12.0;
    case FontSize::large:
        return 20.0;
    case FontSize::medium:
        return 16.0;
    }
    return 16.0;
}

[[nodiscard]] std::shared_ptr<const CachedFont> create_font(
    const Font& font) {
    const CGFloat size = point_size(font);
    CTFontRef base = nullptr;
    if (font.face() == static_cast<i32>(FontFace::monospace)) {
        base = CTFontCreateWithName(CFSTR("Menlo"), size, nullptr);
    } else {
        base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem,
                                             size,
                                             nullptr);
    }
    if (base == nullptr) {
        return {};
    }

    CTFontSymbolicTraits traits = 0U;
    if (font.is_bold()) {
        traits |= kCTFontBoldTrait;
    }
    if (font.is_italic()) {
        traits |= kCTFontItalicTrait;
    }
    if (traits != 0U) {
        CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(
            base, size, nullptr, traits, traits);
        if (styled != nullptr) {
            CFRelease(base);
            base = styled;
        }
    }

    const CGFloat ascent = CTFontGetAscent(base);
    const CGFloat descent = CTFontGetDescent(base);
    const CGFloat leading = CTFontGetLeading(base);
    const i32 height = std::max(
        1,
        static_cast<i32>(std::ceil(ascent + descent + leading)));
    const i32 baseline = std::clamp(
        static_cast<i32>(std::ceil(ascent)), 1, height);
    return std::make_shared<CachedFont>(
        base,
        PlatformFontMetrics {.height = height, .baseline = baseline});
}

[[nodiscard]] std::shared_ptr<const CachedFont> cached_font(
    const Font& font) {
    static std::mutex mutex;
    static std::unordered_map<i32, std::shared_ptr<const CachedFont>> cache;

    const i32 key = font_cache_key(font);
    std::scoped_lock lock(mutex);
    auto iterator = cache.find(key);
    if (iterator != cache.end()) {
        return iterator->second;
    }
    auto created = create_font(font);
    if (created) {
        cache.emplace(key, created);
    }
    return created;
}

[[nodiscard]] std::vector<UniChar> utf16_text(
    std::span<const char32_t> text) {
    std::vector<UniChar> output;
    output.reserve(text.size());
    for (const char32_t character : text) {
        u32 value = static_cast<u32>(character);
        if (value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) {
            value = 0xFFFDU;
        }
        if (value <= 0xFFFFU) {
            output.push_back(static_cast<UniChar>(value));
            continue;
        }
        value -= 0x10000U;
        output.push_back(static_cast<UniChar>(
            0xD800U | ((value >> 10U) & 0x3FFU)));
        output.push_back(static_cast<UniChar>(
            0xDC00U | (value & 0x3FFU)));
    }
    return output;
}

[[nodiscard]] CTLineRef create_line(CTFontRef font,
                                    std::span<const char32_t> text) {
    const std::vector<UniChar> characters = utf16_text(text);
    CFStringRef string = CFStringCreateWithCharacters(
        kCFAllocatorDefault,
        characters.empty() ? nullptr : characters.data(),
        static_cast<CFIndex>(characters.size()));
    if (string == nullptr) {
        return nullptr;
    }
    CFMutableAttributedStringRef attributed =
        CFAttributedStringCreateMutable(kCFAllocatorDefault, 0);
    if (attributed == nullptr) {
        CFRelease(string);
        return nullptr;
    }
    CFAttributedStringReplaceString(attributed,
                                    CFRangeMake(0, 0),
                                    string);
    const CFRange range = CFRangeMake(0, CFStringGetLength(string));
    CFAttributedStringSetAttribute(attributed,
                                   range,
                                   kCTFontAttributeName,
                                   font);
    CFAttributedStringSetAttribute(
        attributed,
        range,
        kCTForegroundColorFromContextAttributeName,
        kCFBooleanTrue);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CFRelease(attributed);
    CFRelease(string);
    return line;
}

#endif

} // namespace

std::optional<PlatformFontMetrics> platform_font_metrics(
    const Font& font) noexcept {
#if defined(__APPLE__)
    auto resource = cached_font(font);
    if (resource) {
        return resource->metrics;
    }
#else
    static_cast<void>(font);
#endif
    return std::nullopt;
}

std::optional<i32> platform_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept {
#if defined(__APPLE__)
    if (text.empty()) {
        return 0;
    }
    if (text.size() > kMaximumTextCodePoints) {
        return std::nullopt;
    }
    auto resource = cached_font(font);
    if (!resource) {
        return std::nullopt;
    }
    CTLineRef line = create_line(resource->font, text);
    if (line == nullptr) {
        return std::nullopt;
    }
    const double width = CTLineGetTypographicBounds(line,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr);
    CFRelease(line);
    if (!std::isfinite(width) || width < 0.0 ||
        width > static_cast<double>(std::numeric_limits<i32>::max())) {
        return std::nullopt;
    }
    return static_cast<i32>(std::ceil(width));
#else
    static_cast<void>(font);
    static_cast<void>(text);
    return std::nullopt;
#endif
}

Status draw_platform_text(Image& target,
                          const Font& font,
                          std::span<const char32_t> text,
                          i32 x,
                          i32 top,
                          Pixel color,
                          const Rect& clip) {
#if defined(__APPLE__)
    if (!target.is_mutable()) {
        return fail(ErrorCode::invalid_state,
                    "cannot draw text into an immutable image");
    }
    if (text.empty() || alpha(color) == 0U) {
        return {};
    }
    if (text.size() > kMaximumTextCodePoints) {
        return fail(ErrorCode::overflow,
                    "CoreText input exceeds the bounded text budget");
    }
    auto resource = cached_font(font);
    auto measured_width = platform_text_width(font, text);
    if (!resource || !measured_width) {
        return fail(ErrorCode::unsupported_feature,
                    "CoreText font could not be created");
    }
    const i32 height = resource->metrics.height;
    const i32 padding = std::max(2, height / 4);
    const i64 clip_left = std::max<i64>(0, clip.x);
    const i64 clip_top = std::max<i64>(0, clip.y);
    const i64 clip_right = std::min<i64>(
        target.width(),
        static_cast<i64>(clip.x) + std::max(0, clip.width));
    const i64 clip_bottom = std::min<i64>(
        target.height(),
        static_cast<i64>(clip.y) + std::max(0, clip.height));
    const i64 glyph_left = static_cast<i64>(x) - padding;
    const i64 glyph_right = static_cast<i64>(x) + *measured_width + padding;
    const i64 glyph_top = top;
    const i64 glyph_bottom = static_cast<i64>(top) + height;
    const i64 visible_left = std::max(clip_left, glyph_left);
    const i64 visible_right = std::min(clip_right, glyph_right);
    const i64 visible_top = std::max(clip_top, glyph_top);
    const i64 visible_bottom = std::min(clip_bottom, glyph_bottom);
    if (visible_right <= visible_left || visible_bottom <= visible_top) {
        return {};
    }
    const i32 mask_width = static_cast<i32>(visible_right - visible_left);
    const usize width_value = static_cast<usize>(mask_width);
    const usize height_value = static_cast<usize>(height);
    if (height_value != 0U &&
        width_value > std::numeric_limits<usize>::max() / height_value) {
        return fail(ErrorCode::overflow,
                    "CoreText glyph mask size overflows");
    }
    std::vector<u8> mask(width_value * height_value, 0U);
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceGray();
    if (color_space == nullptr) {
        return fail(ErrorCode::internal_error,
                    "failed to create CoreText gray color space");
    }
    CGContextRef context = CGBitmapContextCreate(
        mask.data(),
        width_value,
        height_value,
        8U,
        width_value,
        color_space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNone));
    CGColorSpaceRelease(color_space);
    if (context == nullptr) {
        return fail(ErrorCode::internal_error,
                    "failed to create CoreText glyph context");
    }
    CTLineRef line = create_line(resource->font, text);
    if (line == nullptr) {
        CGContextRelease(context);
        return fail(ErrorCode::internal_error,
                    "failed to create CoreText line");
    }

    CGContextSetAllowsAntialiasing(context, true);
    CGContextSetShouldAntialias(context, true);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetGrayFillColor(context, 1.0, 1.0);
    const CGFloat baseline_from_bottom = static_cast<CGFloat>(
        height - resource->metrics.baseline);
    CGContextSetTextPosition(
        context,
        static_cast<CGFloat>(static_cast<i64>(x) - visible_left),
        baseline_from_bottom);
    CTLineDraw(line, context);
    CFRelease(line);
    CGContextRelease(context);

    const u32 base_alpha = alpha(color);
    for (i32 memory_y = 0; memory_y < height; ++memory_y) {
        const i32 destination_y = top + (height - 1 - memory_y);
        if (destination_y < visible_top || destination_y >= visible_bottom) {
            continue;
        }
        for (i32 mask_x = 0; mask_x < mask_width; ++mask_x) {
            const u8 coverage = mask[
                static_cast<usize>(memory_y) * width_value +
                static_cast<usize>(mask_x)];
            if (coverage == 0U) {
                continue;
            }
            const i32 destination_x = static_cast<i32>(
                visible_left + mask_x);
            const u8 source_alpha = static_cast<u8>(
                (base_alpha * coverage + 127U) / 255U);
            const Pixel source = argb(source_alpha,
                                      red(color),
                                      green(color),
                                      blue(color));
            auto stored = target.set_pixel(destination_x,
                                           destination_y,
                                           source,
                                           true);
            if (!stored) {
                return stored;
            }
        }
    }
    return {};
#else
    static_cast<void>(target);
    static_cast<void>(font);
    static_cast<void>(text);
    static_cast<void>(x);
    static_cast<void>(top);
    static_cast<void>(color);
    static_cast<void>(clip);
    return fail(ErrorCode::unsupported_feature,
                "platform text rasterizer is unavailable");
#endif
}

} // namespace phoneme::graphics
