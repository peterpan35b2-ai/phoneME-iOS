#include "phoneme/graphics/TextRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "PhoneMEFontData.hpp"
#include "phoneme/graphics/Graphics.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

#if defined(PHONEME_WEB)
#include <emscripten.h>
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

[[nodiscard]] inline bool composite_text_pixel(Pixel source,
                                               Pixel& destination) noexcept {
    const u8 source_alpha = alpha(source);
    if (source_alpha == 0U) return false;
    const Pixel composited = source_alpha == 255U
        ? rgb565_roundtrip(source)
        : rgb565_roundtrip(source_over(source, destination));
    if (composited == destination) return false;
    destination = composited;
    return true;
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
    auto target_pixels = target.mutable_pixels();
    const usize target_stride = static_cast<usize>(target.width());
    bool changed = false;
    i32 dirty_left = target.width();
    i32 dirty_top = target.height();
    i32 dirty_right = -1;
    i32 dirty_bottom = -1;
    const auto store_pixel = [&](i32 destination_x,
                                 i32 destination_y) noexcept {
        Pixel& destination = target_pixels[
            static_cast<usize>(destination_y) * target_stride +
            static_cast<usize>(destination_x)];
        if (!composite_text_pixel(color, destination)) return;
        changed = true;
        dirty_left = std::min(dirty_left, destination_x);
        dirty_top = std::min(dirty_top, destination_y);
        dirty_right = std::max(dirty_right, destination_x);
        dirty_bottom = std::max(dirty_bottom, destination_y);
    };

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
                store_pixel(static_cast<i32>(destination_x),
                            static_cast<i32>(destination_y));
                if (logical_font.is_bold() &&
                    destination_x + 1 < clip_right) {
                    store_pixel(static_cast<i32>(destination_x + 1),
                                static_cast<i32>(destination_y));
                }
            }
        }
        pen_x += glyph_advance(font, glyph_index, logical_font);
    }
    if (changed) {
        target.mark_dirty_region(dirty_left,
                                 dirty_top,
                                 dirty_right - dirty_left + 1,
                                 dirty_bottom - dirty_top + 1);
    }
    return {};
}

// Composites an 8-bit coverage mask into the target image. Mask row zero is
// the top row; (destination_left, destination_top) is the mask's top-left
// corner in target coordinates. Pixels outside the clip and the target are
// skipped. Shared by the CoreText and browser-canvas fallback rasterizers.
[[nodiscard]] Status composite_text_mask(Image& target,
                                         std::span<const u8> mask,
                                         i32 mask_width,
                                         i64 destination_left,
                                         i64 destination_top,
                                         Pixel color,
                                         const Rect& clip) {
    if (mask_width <= 0 ||
        mask.size() % static_cast<usize>(mask_width) != 0U) {
        return fail(ErrorCode::internal_error,
                    "text fallback mask dimensions are invalid");
    }
    const Rect target_clip = intersect(clip, target_bounds(target));
    const i64 mask_height = static_cast<i64>(mask.size()) /
                            static_cast<i64>(mask_width);
    const i64 clip_right = static_cast<i64>(target_clip.x) +
                           target_clip.width;
    const i64 clip_bottom = static_cast<i64>(target_clip.y) +
                            target_clip.height;
    const i64 visible_left = std::max<i64>(target_clip.x, destination_left);
    const i64 visible_right =
        std::min<i64>(clip_right,
                      destination_left + static_cast<i64>(mask_width));
    const i64 visible_top = std::max<i64>(target_clip.y, destination_top);
    const i64 visible_bottom =
        std::min<i64>(clip_bottom, destination_top + mask_height);
    if (visible_right <= visible_left || visible_bottom <= visible_top) {
        return {};
    }

    const u32 base_alpha = alpha(color);
    auto target_pixels = target.mutable_pixels();
    const usize target_stride = static_cast<usize>(target.width());
    bool changed = false;
    i32 dirty_left = target.width();
    i32 dirty_top = target.height();
    i32 dirty_right = -1;
    i32 dirty_bottom = -1;
    for (i64 row = visible_top; row < visible_bottom; ++row) {
        const u8* mask_row = mask.data() +
            static_cast<usize>(row - destination_top) *
                static_cast<usize>(mask_width);
        for (i64 column = visible_left; column < visible_right; ++column) {
            const u8 coverage =
                mask_row[static_cast<usize>(column - destination_left)];
            if (coverage == 0U) {
                continue;
            }
            const u8 source_alpha = static_cast<u8>(
                (base_alpha * coverage + 127U) / 255U);
            const Pixel source = argb(source_alpha,
                                      red(color),
                                      green(color),
                                      blue(color));
            const i32 destination_x = static_cast<i32>(column);
            const i32 destination_y = static_cast<i32>(row);
            Pixel& destination = target_pixels[
                static_cast<usize>(destination_y) * target_stride +
                static_cast<usize>(destination_x)];
            if (composite_text_pixel(source, destination)) {
                changed = true;
                dirty_left = std::min(dirty_left, destination_x);
                dirty_top = std::min(dirty_top, destination_y);
                dirty_right = std::max(dirty_right, destination_x);
                dirty_bottom = std::max(dirty_bottom, destination_y);
            }
        }
    }
    if (changed) {
        target.mark_dirty_region(dirty_left,
                                 dirty_top,
                                 dirty_right - dirty_left + 1,
                                 dirty_bottom - dirty_top + 1);
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

[[nodiscard]] i32 font_cache_key(const Font& font) noexcept;

constexpr usize kCoreTextMaskCacheMaximumBytes = 512U * 1024U;
constexpr usize kCoreTextMaskCacheMaximumEntries = 256U;
constexpr usize kCoreTextMaskCacheMaximumCodePoints = 64U;
constexpr i32 kCoreTextMaskCacheMaximumWidth = 2'048;

struct CoreTextMaskKey final {
    i32 font_key {0};
    std::u32string text;

    [[nodiscard]] bool operator==(const CoreTextMaskKey& other) const noexcept {
        return font_key == other.font_key && text == other.text;
    }
};

struct CoreTextMaskKeyHash final {
    [[nodiscard]] usize operator()(const CoreTextMaskKey& key) const noexcept {
        usize hash = static_cast<usize>(static_cast<u32>(key.font_key)) +
                     0x9E3779B9U;
        for (char32_t character : key.text) {
            hash ^= static_cast<usize>(character) + 0x9E3779B9U +
                    (hash << 6U) + (hash >> 2U);
        }
        return hash;
    }
};

struct CachedCoreTextMask final {
    i32 minimum_x {0};
    i32 width {0};
    std::vector<u8> mask;
};

struct CoreTextMaskCache final {
    std::mutex mutex;
    std::unordered_map<CoreTextMaskKey,
                       std::shared_ptr<const CachedCoreTextMask>,
                       CoreTextMaskKeyHash> entries;
    std::deque<CoreTextMaskKey> order;
    usize bytes {0U};
};

[[nodiscard]] CoreTextMaskCache& core_text_mask_cache() noexcept {
    static CoreTextMaskCache cache;
    return cache;
}

[[nodiscard]] std::shared_ptr<const CachedCoreTextMask>
cached_core_text_mask(const Font& font,
                      std::span<const char32_t> text) {
    if (text.size() > kCoreTextMaskCacheMaximumCodePoints) return {};
    CoreTextMaskKey key {
        .font_key = font_cache_key(font),
        .text = std::u32string(text.begin(), text.end()),
    };
    auto& cache = core_text_mask_cache();
    std::scoped_lock lock(cache.mutex);
    const auto found = cache.entries.find(key);
    if (found == cache.entries.end()) {
        vm::PerformanceCounters::record_core_text_cache(false);
        return {};
    }
    vm::PerformanceCounters::record_core_text_cache(true);
    return found->second;
}

void cache_core_text_mask(const Font& font,
                          std::span<const char32_t> text,
                          std::shared_ptr<const CachedCoreTextMask> value) {
    if (!value || text.size() > kCoreTextMaskCacheMaximumCodePoints ||
        value->width > kCoreTextMaskCacheMaximumWidth ||
        value->mask.size() > kCoreTextMaskCacheMaximumBytes) {
        return;
    }
    CoreTextMaskKey key {
        .font_key = font_cache_key(font),
        .text = std::u32string(text.begin(), text.end()),
    };
    auto& cache = core_text_mask_cache();
    std::scoped_lock lock(cache.mutex);
    if (cache.entries.contains(key)) return;
    while ((!cache.order.empty()) &&
           (cache.entries.size() >= kCoreTextMaskCacheMaximumEntries ||
            cache.bytes + value->mask.size() >
                kCoreTextMaskCacheMaximumBytes)) {
        CoreTextMaskKey oldest = std::move(cache.order.front());
        cache.order.pop_front();
        const auto found = cache.entries.find(oldest);
        if (found == cache.entries.end()) continue;
        cache.bytes -= found->second->mask.size();
        cache.entries.erase(found);
        vm::PerformanceCounters::record_core_text_cache_eviction();
    }
    cache.bytes += value->mask.size();
    cache.order.push_back(key);
    cache.entries.emplace(std::move(key), std::move(value));
    vm::PerformanceCounters::observe_core_text_cache_bytes(cache.bytes);
}

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
    if (auto cached = cached_core_text_mask(font, text)) {
        return composite_text_mask(
            target,
            cached->mask,
            cached->width,
            static_cast<i64>(x) + cached->minimum_x -
                kCoreTextFallbackPadding,
            top,
            color,
            clip);
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

    const bool cacheable_mask =
        text.size() <= kCoreTextMaskCacheMaximumCodePoints &&
        full_mask_width <= kCoreTextMaskCacheMaximumWidth;
    if (cacheable_mask) {
        const i32 mask_width = static_cast<i32>(full_mask_width);
        const usize width_value = static_cast<usize>(mask_width);
        constexpr usize height_value = static_cast<usize>(kPhoneMEFontHeight);
        if (width_value <= std::numeric_limits<usize>::max() / height_value) {
            auto cached = std::make_shared<CachedCoreTextMask>();
            cached->minimum_x = minimum_x;
            cached->width = mask_width;
            cached->mask.assign(width_value * height_value, 0U);

            CGColorSpaceRef color_space = CGColorSpaceCreateDeviceGray();
            if (color_space != nullptr) {
                CGContextRef context = CGBitmapContextCreate(
                    cached->mask.data(),
                    width_value,
                    height_value,
                    8U,
                    width_value,
                    color_space,
                    static_cast<CGBitmapInfo>(kCGImageAlphaNone));
                CGColorSpaceRelease(color_space);
                if (context != nullptr) {
                    CGContextSetAllowsAntialiasing(context, true);
                    CGContextSetShouldAntialias(context, true);
                    CGContextSetShouldSmoothFonts(context, true);
                    CGContextSetGrayFillColor(context, 1.0, 1.0);
                    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
                    CGContextSetTextPosition(
                        context,
                        static_cast<CGFloat>(
                            kCoreTextFallbackPadding - minimum_x),
                        kCoreTextFallbackBaselineFromBottom);
                    CTLineDraw(line, context);
                    CGContextRelease(context);
                    CFRelease(line);
                    cache_core_text_mask(font, text, cached);
                    return composite_text_mask(
                        target,
                        cached->mask,
                        cached->width,
                        static_cast<i64>(x) + minimum_x -
                            kCoreTextFallbackPadding,
                        top,
                        color,
                        clip);
                }
            }
        }
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

    return composite_text_mask(target,
                               mask,
                               mask_width,
                               visible_left,
                               top,
                               color,
                               clip);
}

#endif

#if defined(PHONEME_WEB)

// Browser fallback rasterizer. The bundled phoneME bitmap font only covers
// Latin (at most kMaximumFontGlyphs glyphs), so every other script — CJK,
// Vietnamese, Cyrillic, ... — is measured and rasterized with the worker's
// canvas 2D font stack, mirroring the iOS CoreText fallback. If OffscreenCanvas
// is unavailable the bridge reports failure and callers keep the bitmap-only
// behavior.
constexpr i32 kWebFontPixelSize = 13;
constexpr i32 kWebFontBaseline = kPhoneMEFontAscent;

[[nodiscard]] std::string utf8_text(std::span<const char32_t> text) {
    std::string output;
    output.reserve(text.size());
    for (const char32_t character : text) {
        u32 value = static_cast<u32>(character);
        if (value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) {
            value = 0xFFFDU;
        }
        if (value < 0x80U) {
            output.push_back(static_cast<char>(value));
        } else if (value < 0x800U) {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else if (value < 0x10000U) {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(
                0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            output.push_back(static_cast<char>(
                0x80U | ((value >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(
                0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }
    return output;
}

[[nodiscard]] std::string web_css_font(const Font& font) {
    std::string css;
    if (font.is_bold()) {
        css += "bold ";
    }
    if (font.is_italic()) {
        css += "italic ";
    }
    css += std::to_string(kWebFontPixelSize);
    css += "px ";
    css += font.face() == static_cast<i32>(FontFace::monospace)
        ? "monospace"
        : "system-ui";
    return css;
}

[[nodiscard]] i32 web_measure_text(std::string_view text,
                                   std::string_view css_font) noexcept {
    const std::string text_bytes(text);
    const std::string font_bytes(css_font);
    return EM_ASM_INT({
        if (typeof OffscreenCanvas === 'undefined') return -1;
        const global = globalThis;
        let bridge = global.__phoneMETextBridge;
        if (!bridge) {
            bridge = global.__phoneMETextBridge = {
                context: new OffscreenCanvas(1, 1)
                    .getContext('2d', { willReadFrequently: true })
            };
        }
        bridge.context.font = UTF8ToString($1);
        const width = bridge.context.measureText(UTF8ToString($0)).width;
        return Number.isFinite(width) && width >= 0 ? Math.ceil(width) : -1;
    }, text_bytes.c_str(), font_bytes.c_str());
}

[[nodiscard]] bool web_rasterize_text(std::string_view text,
                                      std::string_view css_font,
                                      u8* mask,
                                      i32 mask_width,
                                      i32 mask_height,
                                      double origin_x) noexcept {
    const std::string text_bytes(text);
    const std::string font_bytes(css_font);
    return EM_ASM_INT({
        const bridge = globalThis.__phoneMETextBridge;
        if (!bridge || $3 <= 0 || $4 <= 0) return 0;
        const context = bridge.context;
        const canvas = context.canvas;
        if ($3 > canvas.width) canvas.width = $3;
        if ($4 > canvas.height) canvas.height = $4;
        context.clearRect(0, 0, $3, $4);
        context.font = UTF8ToString($1);
        context.fillStyle = '#fff';
        context.textBaseline = 'alphabetic';
        context.fillText(UTF8ToString($0), $5, $6);
        const pixels = context.getImageData(0, 0, $3, $4).data;
        const total = $3 * $4;
        for (let index = 0; index < total; ++index) {
            HEAPU8[$2 + index] = pixels[index * 4 + 3];
        }
        return 1;
    }, text_bytes.c_str(), font_bytes.c_str(),
       reinterpret_cast<intptr_t>(mask), mask_width, mask_height, origin_x,
       static_cast<double>(kWebFontBaseline)) != 0;
}

[[nodiscard]] std::optional<i32> web_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept {
    if (text.empty()) {
        return 0;
    }
    const i32 width = web_measure_text(utf8_text(text), web_css_font(font));
    if (width < 0) {
        return std::nullopt;
    }
    return width;
}

[[nodiscard]] Status draw_web_text(Image& target,
                                   const Font& font,
                                   std::span<const char32_t> text,
                                   i32 x,
                                   i32 top,
                                   Pixel color,
                                   const Rect& clip) {
    const std::string utf8 = utf8_text(text);
    const std::string css_font = web_css_font(font);
    const i32 advance = web_measure_text(utf8, css_font);
    if (advance < 0) {
        return fail(ErrorCode::unsupported_feature,
                    "browser text bridge is unavailable");
    }

    constexpr i32 kWebTextPadding = 1;
    const i64 full_left = static_cast<i64>(x) - kWebTextPadding;
    const i64 full_right =
        static_cast<i64>(x) + advance + kWebTextPadding;
    const i64 full_bottom =
        static_cast<i64>(top) + kPhoneMEFontHeight;

    const Rect target_clip = intersect(clip, target_bounds(target));
    const i64 clip_right =
        static_cast<i64>(target_clip.x) + target_clip.width;
    const i64 clip_bottom =
        static_cast<i64>(target_clip.y) + target_clip.height;
    const i64 visible_left =
        std::max<i64>(target_clip.x, full_left);
    const i64 visible_right = std::min<i64>(clip_right, full_right);
    const i64 visible_top = std::max<i64>(target_clip.y, top);
    const i64 visible_bottom =
        std::min<i64>(clip_bottom, full_bottom);
    if (visible_right <= visible_left || visible_bottom <= visible_top) {
        return {};
    }

    const i32 mask_width =
        static_cast<i32>(visible_right - visible_left);
    const usize width_value = static_cast<usize>(mask_width);
    constexpr usize height_value = static_cast<usize>(kPhoneMEFontHeight);
    if (width_value >
        std::numeric_limits<usize>::max() / height_value) {
        return fail(ErrorCode::overflow,
                    "browser text mask size overflows");
    }
    std::vector<u8> mask(width_value * height_value, 0U);

    // Mask column zero is visible_left; the run's pen starts at x.
    const double origin_x = static_cast<double>(x - visible_left);
    if (!web_rasterize_text(utf8,
                            css_font,
                            mask.data(),
                            mask_width,
                            kPhoneMEFontHeight,
                            origin_x)) {
        return fail(ErrorCode::unsupported_feature,
                    "browser text rasterization failed");
    }

    return composite_text_mask(target,
                               mask,
                               mask_width,
                               visible_left,
                               top,
                               color,
                               clip);
}

#endif

#if defined(__APPLE__) || defined(PHONEME_WEB)

[[nodiscard]] std::optional<i32> fallback_text_width(
    const Font& font,
    std::span<const char32_t> text) noexcept {
#if defined(__APPLE__)
    return core_text_width(font, text);
#else
    return web_text_width(font, text);
#endif
}

[[nodiscard]] Status draw_text_fallback(Image& target,
                                        const Font& font,
                                        std::span<const char32_t> text,
                                        i32 x,
                                        i32 top,
                                        Pixel color,
                                        const Rect& clip) {
#if defined(__APPLE__)
    return draw_core_text_fallback(target, font, text, x, top, color, clip);
#else
    return draw_web_text(target, font, text, x, top, color, clip);
#endif
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
            : fallback_text_width(font, run);
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
            : fallback_text_width(font, run);
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
            : draw_text_fallback(target,
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
#if defined(__APPLE__) || defined(PHONEME_WEB)
#if defined(__APPLE__)
    auto normalized = normalize_nfc_if_needed(text);
    if (normalized) {
        text = *normalized;
    }
#endif
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
#if defined(__APPLE__) || defined(PHONEME_WEB)
#if defined(__APPLE__)
    auto normalized = normalize_nfc_if_needed(text);
    if (normalized) {
        text = *normalized;
    }
#endif
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
