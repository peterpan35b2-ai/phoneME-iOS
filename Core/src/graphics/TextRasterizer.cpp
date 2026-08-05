#include "phoneme/graphics/TextRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "PhoneMEFontData.hpp"
#include "phoneme/graphics/Graphics.hpp"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

namespace phoneme::graphics {
namespace {

constexpr usize kMaximumTextCodePoints = 262'144U;
constexpr usize kMaximumFontGlyphs = 256U;
constexpr usize kAsciiGlyphCount = 128U;
constexpr i32 kPhoneMEFontAscent = 11;
constexpr i32 kPhoneMEFontDescent = 3;
constexpr i32 kPhoneMEFontHeight =
    kPhoneMEFontAscent + kPhoneMEFontDescent;
constexpr i32 kEmbeddedFontHeight = 13;
constexpr i32 kInvalidGlyphIndex = -1;

struct PhoneMEFontBin final {
    const u8* widths {nullptr};
    const u8* bitmap {nullptr};
    std::array<char32_t, kMaximumFontGlyphs> codepoints {};
    std::array<u16, kMaximumFontGlyphs> x_offsets {};
    std::array<i32, kAsciiGlyphCount> ascii_indices {};
    i32 glyph_count {0};
    i32 height {0};
    i32 atlas_width {0};
    bool valid {false};
};

[[nodiscard]] bool decode_utf8_codepoint(std::span<const u8> bytes,
                                         usize limit,
                                         usize& cursor,
                                         u32& codepoint) noexcept {
    if (cursor >= limit) {
        return false;
    }

    const u8 first = bytes[cursor++];
    if (first < 0x80U) {
        codepoint = first;
        return true;
    }

    u32 value = 0U;
    usize continuation_count = 0U;
    if ((first & 0xE0U) == 0xC0U) {
        value = first & 0x1FU;
        continuation_count = 1U;
    } else if ((first & 0xF0U) == 0xE0U) {
        value = first & 0x0FU;
        continuation_count = 2U;
    } else if ((first & 0xF8U) == 0xF0U) {
        value = first & 0x07U;
        continuation_count = 3U;
    } else {
        return false;
    }

    if (continuation_count > limit - cursor) {
        return false;
    }
    for (usize index = 0U; index < continuation_count; ++index) {
        const u8 continuation = bytes[cursor++];
        if ((continuation & 0xC0U) != 0x80U) {
            return false;
        }
        value = (value << 6U) | static_cast<u32>(continuation & 0x3FU);
    }

    codepoint = value;
    return true;
}

[[nodiscard]] PhoneMEFontBin parse_font_bin() noexcept {
    PhoneMEFontBin font;
    font.ascii_indices.fill(kInvalidGlyphIndex);

    constexpr usize data_length = detail::phone_me_font_bin_data_length;
    static_assert(sizeof(detail::phone_me_font_bin_data) == data_length + 1U);
    const std::span<const u8> bytes(detail::phone_me_font_bin_data,
                                    data_length);
    if (bytes.size() < 4U) {
        return font;
    }

    const usize charset_length =
        (static_cast<usize>(bytes[0]) << 8U) |
        static_cast<usize>(bytes[1]);
    constexpr usize charset_start = 2U;
    if (charset_length > bytes.size() - charset_start) {
        return font;
    }
    const usize charset_end = charset_start + charset_length;
    if (charset_end >= bytes.size()) {
        return font;
    }

    usize cursor = charset_start;
    usize glyph_count = 0U;
    while (cursor < charset_end) {
        u32 codepoint = 0U;
        if (glyph_count >= kMaximumFontGlyphs ||
            !decode_utf8_codepoint(bytes, charset_end, cursor, codepoint) ||
            codepoint > 0xFFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return font;
        }
        font.codepoints[glyph_count++] = static_cast<char32_t>(codepoint);
    }
    if (glyph_count == 0U || cursor != charset_end) {
        return font;
    }

    font.height = bytes[charset_end];
    if (font.height != kEmbeddedFontHeight) {
        return font;
    }

    const usize widths_offset = charset_end + 1U;
    if (glyph_count > bytes.size() - widths_offset) {
        return font;
    }
    const usize bitmap_offset = widths_offset + glyph_count;
    font.widths = bytes.data() + widths_offset;

    i32 atlas_width = 0;
    for (usize index = 0U; index < glyph_count; ++index) {
        const i32 width = font.widths[index];
        if (width <= 0 ||
            atlas_width > static_cast<i32>(
                std::numeric_limits<u16>::max()) - width) {
            return PhoneMEFontBin {};
        }
        font.x_offsets[index] = static_cast<u16>(atlas_width);
        atlas_width += width;

        const u32 codepoint = static_cast<u32>(font.codepoints[index]);
        if (codepoint < kAsciiGlyphCount) {
            font.ascii_indices[codepoint] = static_cast<i32>(index);
        }
    }

    const usize bitmap_bits =
        static_cast<usize>(atlas_width) * static_cast<usize>(font.height);
    const usize bitmap_bytes = (bitmap_bits + 7U) / 8U;
    if (bitmap_offset > bytes.size() ||
        bitmap_bytes > bytes.size() - bitmap_offset) {
        return PhoneMEFontBin {};
    }

    font.bitmap = bytes.data() + bitmap_offset;
    font.glyph_count = static_cast<i32>(glyph_count);
    font.atlas_width = atlas_width;
    font.valid = true;
    return font;
}

[[nodiscard]] const PhoneMEFontBin& phone_me_font() noexcept {
    static const PhoneMEFontBin font = parse_font_bin();
    return font;
}

[[nodiscard]] i32 lookup_glyph(const PhoneMEFontBin& font,
                               char32_t character) noexcept {
    if (!font.valid) {
        return kInvalidGlyphIndex;
    }
    const u32 codepoint = static_cast<u32>(character);
    if (codepoint < kAsciiGlyphCount) {
        return font.ascii_indices[codepoint];
    }
    for (i32 index = 0; index < font.glyph_count; ++index) {
        if (font.codepoints[static_cast<usize>(index)] == character) {
            return index;
        }
    }
    return kInvalidGlyphIndex;
}

[[nodiscard]] i32 style_horizontal_padding(const Font& font) noexcept {
    i32 padding = 0;
    if (font.is_bold()) {
        ++padding;
    }
    if (font.is_italic()) {
        padding += 2;
    }
    return padding;
}

[[nodiscard]] i32 glyph_advance(const PhoneMEFontBin& font,
                                i32 glyph_index,
                                const Font& logical_font) noexcept {
    return static_cast<i32>(font.widths[static_cast<usize>(glyph_index)]) +
           style_horizontal_padding(logical_font);
}

[[nodiscard]] std::optional<i32> bitmap_text_width(
    const Font& logical_font,
    std::span<const char32_t> text) noexcept {
    if (text.empty()) {
        return 0;
    }
    const PhoneMEFontBin& font = phone_me_font();
    if (!font.valid) {
        return std::nullopt;
    }

    i32 width = 0;
    for (const char32_t character : text) {
        const i32 glyph_index = lookup_glyph(font, character);
        if (glyph_index == kInvalidGlyphIndex) {
            return std::nullopt;
        }
        const i32 advance = glyph_advance(font, glyph_index, logical_font);
        if (width > std::numeric_limits<i32>::max() - advance) {
            return std::nullopt;
        }
        width += advance;
    }
    return width;
}

[[nodiscard]] bool bitmap_pixel_is_set(const PhoneMEFontBin& font,
                                       i32 glyph_index,
                                       i32 glyph_x,
                                       i32 glyph_y) noexcept {
    const usize bit_index =
        static_cast<usize>(glyph_y) *
            static_cast<usize>(font.atlas_width) +
        static_cast<usize>(font.x_offsets[static_cast<usize>(glyph_index)]) +
        static_cast<usize>(glyph_x);
    return ((font.bitmap[bit_index >> 3U] >> (bit_index & 7U)) & 1U) != 0U;
}

[[nodiscard]] Status draw_bitmap_text(Image& target,
                                      const Font& logical_font,
                                      std::span<const char32_t> text,
                                      i32 x,
                                      i32 top,
                                      Pixel color,
                                      const Rect& clip) {
    const PhoneMEFontBin& font = phone_me_font();
    if (!font.valid) {
        return fail(ErrorCode::unsupported_feature,
                    "phoneME bitmap font is unavailable");
    }

    const Rect visible_clip = intersect(clip, target_bounds(target));
    if (visible_clip.width <= 0 || visible_clip.height <= 0) {
        return {};
    }
    const i64 clip_right = static_cast<i64>(visible_clip.x) +
                           visible_clip.width;
    const i64 clip_bottom = static_cast<i64>(visible_clip.y) +
                            visible_clip.height;

    i64 pen_x = x;
    for (const char32_t character : text) {
        const i32 glyph_index = lookup_glyph(font, character);
        if (glyph_index == kInvalidGlyphIndex) {
            return fail(ErrorCode::unsupported_feature,
                        "phoneME bitmap font does not contain the glyph");
        }
        const i32 glyph_width =
            font.widths[static_cast<usize>(glyph_index)];
        for (i32 glyph_y = 0; glyph_y < font.height; ++glyph_y) {
            const i64 destination_y = static_cast<i64>(top) + glyph_y;
            if (destination_y < visible_clip.y ||
                destination_y >= clip_bottom) {
                continue;
            }
            const i32 italic_shift = logical_font.is_italic()
                ? (font.height - 1 - glyph_y) / 6
                : 0;
            for (i32 glyph_x = 0; glyph_x < glyph_width; ++glyph_x) {
                if (!bitmap_pixel_is_set(font,
                                         glyph_index,
                                         glyph_x,
                                         glyph_y)) {
                    continue;
                }
                const i64 destination_x = pen_x + glyph_x + italic_shift;
                if (destination_x < visible_clip.x ||
                    destination_x >= clip_right) {
                    continue;
                }
                auto stored = target.set_pixel(static_cast<i32>(destination_x),
                                               static_cast<i32>(destination_y),
                                               color,
                                               true);
                if (!stored) {
                    return stored;
                }
                if (logical_font.is_bold() &&
                    destination_x + 1 < clip_right) {
                    stored = target.set_pixel(
                        static_cast<i32>(destination_x + 1),
                        static_cast<i32>(destination_y),
                        color,
                        true);
                    if (!stored) {
                        return stored;
                    }
                }
            }
        }
        pen_x += glyph_advance(font, glyph_index, logical_font);
    }
    return {};
}

#if defined(__APPLE__)

constexpr CGFloat kCoreTextFallbackPointSize = 10.0;
constexpr CGFloat kCoreTextFallbackBaselineFromBottom = 2.0;
constexpr i32 kCoreTextFallbackPadding = 1;

struct CachedFont final {
    CTFontRef font {nullptr};

    explicit CachedFont(CTFontRef value) noexcept : font(value) {}

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
           ((font.style() & (style_bold | style_italic)) << 8);
}

[[nodiscard]] std::shared_ptr<const CachedFont> create_font(
    const Font& font) {
    const CTFontUIFontType font_type =
        font.face() == static_cast<i32>(FontFace::monospace)
        ? kCTFontUIFontUserFixedPitch
        : kCTFontUIFontSystem;
    CTFontRef base = CTFontCreateUIFontForLanguage(
        font_type, kCoreTextFallbackPointSize, nullptr);
    if (base == nullptr) {
        base = CTFontCreateWithName(CFSTR("Helvetica"),
                                    kCoreTextFallbackPointSize,
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
            base,
            kCoreTextFallbackPointSize,
            nullptr,
            traits,
            traits);
        if (styled != nullptr) {
            CFRelease(base);
            base = styled;
        }
    }
    return std::make_shared<CachedFont>(base);
}

[[nodiscard]] std::shared_ptr<const CachedFont> cached_font(
    const Font& font) {
    static std::mutex mutex;
    static std::unordered_map<i32, std::shared_ptr<const CachedFont>> cache;

    const i32 key = font_cache_key(font);
    std::scoped_lock lock(mutex);
    const auto iterator = cache.find(key);
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

[[nodiscard]] bool contains_combining_mark(
    std::span<const char32_t> text) noexcept {
    for (const char32_t character : text) {
        const u32 value = static_cast<u32>(character);
        if ((value >= 0x0300U && value <= 0x036FU) ||
            (value >= 0x1AB0U && value <= 0x1AFFU) ||
            (value >= 0x1DC0U && value <= 0x1DFFU) ||
            (value >= 0x20D0U && value <= 0x20FFU) ||
            (value >= 0xFE20U && value <= 0xFE2FU)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::vector<char32_t>> normalize_nfc_if_needed(
    std::span<const char32_t> text) {
    if (!contains_combining_mark(text)) {
        return std::nullopt;
    }

    const std::vector<UniChar> characters = utf16_text(text);
    CFMutableStringRef string = CFStringCreateMutable(kCFAllocatorDefault, 0);
    if (string == nullptr) {
        return std::nullopt;
    }
    if (!characters.empty()) {
        CFStringAppendCharacters(string,
                                 characters.data(),
                                 static_cast<CFIndex>(characters.size()));
    }
    CFStringNormalize(string, kCFStringNormalizationFormC);

    const CFIndex length = CFStringGetLength(string);
    if (length < 0 ||
        static_cast<unsigned long long>(length) >
            static_cast<unsigned long long>(kMaximumTextCodePoints * 2U)) {
        CFRelease(string);
        return std::nullopt;
    }

    std::vector<UniChar> normalized_utf16(static_cast<usize>(length));
    if (length > 0) {
        CFStringGetCharacters(string,
                              CFRangeMake(0, length),
                              normalized_utf16.data());
    }
    CFRelease(string);

    std::vector<char32_t> output;
    output.reserve(normalized_utf16.size());
    for (usize index = 0U; index < normalized_utf16.size(); ++index) {
        const u32 first = normalized_utf16[index];
        if (first >= 0xD800U && first <= 0xDBFFU &&
            index + 1U < normalized_utf16.size()) {
            const u32 second = normalized_utf16[index + 1U];
            if (second >= 0xDC00U && second <= 0xDFFFU) {
                output.push_back(static_cast<char32_t>(
                    0x10000U + ((first - 0xD800U) << 10U) +
                    (second - 0xDC00U)));
                ++index;
                continue;
            }
        }
        if (first >= 0xD800U && first <= 0xDFFFU) {
            output.push_back(U'\uFFFD');
        } else {
            output.push_back(static_cast<char32_t>(first));
        }
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

[[nodiscard]] std::optional<i32> core_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept {
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
        width > static_cast<double>(std::numeric_limits<i32>::max() - 1)) {
        return std::nullopt;
    }
    return width > 0.0 ? static_cast<i32>(std::ceil(width)) : 1;
}

[[nodiscard]] Status draw_core_text_fallback(
    Image& target,
    const Font& font,
    std::span<const char32_t> text,
    i32 x,
    i32 top,
    Pixel color,
    const Rect& clip) {
    auto resource = cached_font(font);
    if (!resource) {
        return fail(ErrorCode::unsupported_feature,
                    "CoreText fallback font could not be created");
    }
    CTLineRef line = create_line(resource->font, text);
    if (line == nullptr) {
        return fail(ErrorCode::internal_error,
                    "failed to create CoreText fallback line");
    }

    const double advance = CTLineGetTypographicBounds(line,
                                                      nullptr,
                                                      nullptr,
                                                      nullptr);
    const CGRect ink_bounds = CTLineGetBoundsWithOptions(
        line, kCTLineBoundsUseGlyphPathBounds);
    const double ink_min_x = CGRectGetMinX(ink_bounds);
    const double ink_max_x = CGRectGetMaxX(ink_bounds);
    if (!std::isfinite(advance) || !std::isfinite(ink_min_x) ||
        !std::isfinite(ink_max_x)) {
        CFRelease(line);
        return fail(ErrorCode::internal_error,
                    "CoreText fallback returned invalid glyph bounds");
    }

    const double minimum_x_value = std::floor(std::min(0.0, ink_min_x));
    const double maximum_x_value = std::ceil(std::max(advance, ink_max_x));
    if (minimum_x_value < static_cast<double>(std::numeric_limits<i32>::min()) ||
        maximum_x_value > static_cast<double>(std::numeric_limits<i32>::max())) {
        CFRelease(line);
        return fail(ErrorCode::overflow,
                    "CoreText fallback glyph bounds overflow");
    }
    const i32 minimum_x = static_cast<i32>(minimum_x_value);
    const i32 maximum_x = static_cast<i32>(maximum_x_value);
    if (maximum_x < minimum_x) {
        CFRelease(line);
        return fail(ErrorCode::internal_error,
                    "CoreText fallback glyph bounds are inverted");
    }

    const i64 full_mask_width =
        static_cast<i64>(maximum_x) - minimum_x +
        kCoreTextFallbackPadding * 2LL;
    if (full_mask_width <= 0 ||
        full_mask_width > static_cast<i64>(std::numeric_limits<i32>::max())) {
        CFRelease(line);
        return fail(ErrorCode::overflow,
                    "CoreText fallback mask width overflows");
    }

    const Rect target_clip = intersect(clip, target_bounds(target));
    const i64 full_left = static_cast<i64>(x) + minimum_x -
                          kCoreTextFallbackPadding;
    const i64 full_right = full_left + full_mask_width;
    const i64 full_top = top;
    const i64 full_bottom = full_top + kPhoneMEFontHeight;
    const i64 clip_right = static_cast<i64>(target_clip.x) +
                           target_clip.width;
    const i64 clip_bottom = static_cast<i64>(target_clip.y) +
                            target_clip.height;
    const i64 visible_left = std::max<i64>(target_clip.x, full_left);
    const i64 visible_right = std::min<i64>(clip_right, full_right);
    const i64 visible_top = std::max<i64>(target_clip.y, full_top);
    const i64 visible_bottom = std::min<i64>(clip_bottom, full_bottom);
    if (visible_right <= visible_left || visible_bottom <= visible_top) {
        CFRelease(line);
        return {};
    }

    const i32 mask_width = static_cast<i32>(visible_right - visible_left);
    const usize width_value = static_cast<usize>(mask_width);
    constexpr usize height_value = static_cast<usize>(kPhoneMEFontHeight);
    if (width_value > std::numeric_limits<usize>::max() / height_value) {
        CFRelease(line);
        return fail(ErrorCode::overflow,
                    "CoreText fallback mask size overflows");
    }
    std::vector<u8> mask(width_value * height_value, 0U);

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceGray();
    if (color_space == nullptr) {
        CFRelease(line);
        return fail(ErrorCode::internal_error,
                    "failed to create CoreText fallback color space");
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
        CFRelease(line);
        return fail(ErrorCode::internal_error,
                    "failed to create CoreText fallback bitmap context");
    }

    const i64 source_x_offset = visible_left - full_left;
    CGContextSetAllowsAntialiasing(context, true);
    CGContextSetShouldAntialias(context, true);
    CGContextSetShouldSmoothFonts(context, true);
    CGContextSetGrayFillColor(context, 1.0, 1.0);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetTextPosition(
        context,
        static_cast<CGFloat>(kCoreTextFallbackPadding - minimum_x) -
            static_cast<CGFloat>(source_x_offset),
        kCoreTextFallbackBaselineFromBottom);
    CTLineDraw(line, context);
    CGContextRelease(context);
    CFRelease(line);

    const u32 base_alpha = alpha(color);
    for (i32 source_y = 0; source_y < kPhoneMEFontHeight; ++source_y) {
        // This is the same row mapping used by the phoneME C iOS port. The
        // fallback mask and Canvas framebuffer both expose row zero at top.
        const i64 destination_y = static_cast<i64>(top) + source_y;
        if (destination_y < visible_top || destination_y >= visible_bottom) {
            continue;
        }
        for (i32 source_x = 0; source_x < mask_width; ++source_x) {
            const u8 coverage = mask[
                static_cast<usize>(source_y) * width_value +
                static_cast<usize>(source_x)];
            if (coverage == 0U) {
                continue;
            }
            const i32 destination_x = static_cast<i32>(visible_left + source_x);
            const u8 source_alpha = static_cast<u8>(
                (base_alpha * coverage + 127U) / 255U);
            const Pixel source = argb(source_alpha,
                                      red(color),
                                      green(color),
                                      blue(color));
            auto stored = target.set_pixel(destination_x,
                                           static_cast<i32>(destination_y),
                                           source,
                                           true);
            if (!stored) {
                return stored;
            }
        }
    }
    return {};
}

[[nodiscard]] std::optional<i32> mixed_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept {
    i64 total_width = 0;
    usize run_start = 0U;
    while (run_start < text.size()) {
        const bool use_bitmap =
            lookup_glyph(phone_me_font(), text[run_start]) !=
            kInvalidGlyphIndex;
        usize run_end = run_start + 1U;
        while (run_end < text.size() &&
               (lookup_glyph(phone_me_font(), text[run_end]) !=
                kInvalidGlyphIndex) == use_bitmap) {
            ++run_end;
        }

        const auto run = text.subspan(run_start, run_end - run_start);
        const std::optional<i32> run_width = use_bitmap
            ? bitmap_text_width(font, run)
            : core_text_width(font, run);
        if (!run_width || *run_width < 0 ||
            total_width > std::numeric_limits<i32>::max() - *run_width) {
            return std::nullopt;
        }
        total_width += *run_width;
        run_start = run_end;
    }
    return static_cast<i32>(total_width);
}

[[nodiscard]] Status draw_mixed_text(Image& target,
                                     const Font& font,
                                     std::span<const char32_t> text,
                                     i32 x,
                                     i32 top,
                                     Pixel color,
                                     const Rect& clip) {
    i64 pen_x = x;
    usize run_start = 0U;
    while (run_start < text.size()) {
        const bool use_bitmap =
            lookup_glyph(phone_me_font(), text[run_start]) !=
            kInvalidGlyphIndex;
        usize run_end = run_start + 1U;
        while (run_end < text.size() &&
               (lookup_glyph(phone_me_font(), text[run_end]) !=
                kInvalidGlyphIndex) == use_bitmap) {
            ++run_end;
        }

        const auto run = text.subspan(run_start, run_end - run_start);
        const std::optional<i32> run_width = use_bitmap
            ? bitmap_text_width(font, run)
            : core_text_width(font, run);
        if (!run_width) {
            return fail(ErrorCode::unsupported_feature,
                        "text run could not be measured");
        }
        if (pen_x > std::numeric_limits<i32>::max()) {
            return {};
        }

        const Status status = use_bitmap
            ? draw_bitmap_text(target,
                               font,
                               run,
                               static_cast<i32>(pen_x),
                               top,
                               color,
                               clip)
            : draw_core_text_fallback(target,
                                      font,
                                      run,
                                      static_cast<i32>(pen_x),
                                      top,
                                      color,
                                      clip);
        if (!status) {
            return status;
        }
        pen_x += *run_width;
        run_start = run_end;
    }
    return {};
}

#endif

} // namespace

std::optional<PlatformFontMetrics> platform_font_metrics(
    const Font& font) noexcept {
    static_cast<void>(font);
    if (!phone_me_font().valid) {
        return std::nullopt;
    }
    return PlatformFontMetrics {
        .height = kPhoneMEFontHeight,
        .baseline = kPhoneMEFontAscent,
    };
}

std::optional<i32> platform_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept {
    if (text.empty()) {
        return 0;
    }
    if (text.size() > kMaximumTextCodePoints) {
        return std::nullopt;
    }
#if defined(__APPLE__)
    auto normalized = normalize_nfc_if_needed(text);
    if (normalized) {
        text = *normalized;
    }
    return mixed_text_width(font, text);
#else
    return bitmap_text_width(font, text);
#endif
}

Status draw_platform_text(Image& target,
                          const Font& font,
                          std::span<const char32_t> text,
                          i32 x,
                          i32 top,
                          Pixel color,
                          const Rect& clip) {
    if (!target.is_mutable()) {
        return fail(ErrorCode::invalid_state,
                    "cannot draw text into an immutable image");
    }
    if (text.empty() || alpha(color) == 0U) {
        return {};
    }
    if (text.size() > kMaximumTextCodePoints) {
        return fail(ErrorCode::overflow,
                    "text input exceeds the bounded glyph budget");
    }
#if defined(__APPLE__)
    auto normalized = normalize_nfc_if_needed(text);
    if (normalized) {
        text = *normalized;
    }
    return draw_mixed_text(target, font, text, x, top, color, clip);
#else
    if (bitmap_text_width(font, text).has_value()) {
        return draw_bitmap_text(target, font, text, x, top, color, clip);
    }
    return fail(ErrorCode::unsupported_feature,
                "platform text fallback is unavailable");
#endif
}

} // namespace phoneme::graphics
