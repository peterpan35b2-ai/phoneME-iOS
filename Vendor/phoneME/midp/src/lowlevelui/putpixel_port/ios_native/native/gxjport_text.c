/*
 * Pixel-accurate Canvas text renderer for the iOS phoneME port.
 *
 * The primary renderer uses the bundled font.bin atlas. It is intentionally
 * kept at its original 13-pixel height because classic J2ME games commonly
 * target 176x208, 240x320, or similarly small Canvas resolutions. CoreText is
 * used only when a string contains a glyph that font.bin does not provide.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kni.h>
#include <gxapi_constants.h>
#include <gxj_putpixel.h>

#include "font_bin_data.h"

#define FONT_BIN_MAX_GLYPHS 256
#define FONT_BIN_ASCII_COUNT 128
#define FONT_BIN_ASCENT 11
#define FONT_BIN_DESCENT 2
#define FONT_BIN_LEADING 0
#define FONT_BIN_EXPECTED_HEIGHT 13

#define CORETEXT_FALLBACK_POINT_SIZE 10.0
#define CORETEXT_FALLBACK_BASELINE 2.0
#define CORETEXT_FALLBACK_PADDING 1

#define INVALID_GLYPH_INDEX (-1)

typedef struct {
    const uint8_t *bytes;
    size_t byteCount;
    const uint8_t *widths;
    const uint8_t *bitmap;
    jchar codepoints[FONT_BIN_MAX_GLYPHS];
    uint16_t xOffsets[FONT_BIN_MAX_GLYPHS];
    int16_t asciiIndices[FONT_BIN_ASCII_COUNT];
    int glyphCount;
    int height;
    int atlasWidth;
    int valid;
} PhoneMEFontBin;

static PhoneMEFontBin gFontBin;
static pthread_once_t gFontBinOnce = PTHREAD_ONCE_INIT;

static int decode_utf8_codepoint(
        const uint8_t *bytes,
        size_t limit,
        size_t *cursor,
        uint32_t *codepoint) {
    uint8_t first;
    uint32_t value;
    int continuationCount;
    int continuationIndex;

    if (bytes == NULL || cursor == NULL || codepoint == NULL ||
            *cursor >= limit) {
        return 0;
    }

    first = bytes[(*cursor)++];
    if (first < 0x80) {
        *codepoint = first;
        return 1;
    }

    if ((first & 0xe0) == 0xc0) {
        value = first & 0x1f;
        continuationCount = 1;
    } else if ((first & 0xf0) == 0xe0) {
        value = first & 0x0f;
        continuationCount = 2;
    } else if ((first & 0xf8) == 0xf0) {
        value = first & 0x07;
        continuationCount = 3;
    } else {
        return 0;
    }

    if (*cursor + (size_t)continuationCount > limit) {
        return 0;
    }

    for (continuationIndex = 0;
            continuationIndex < continuationCount;
            continuationIndex++) {
        uint8_t continuation = bytes[(*cursor)++];
        if ((continuation & 0xc0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (uint32_t)(continuation & 0x3f);
    }

    *codepoint = value;
    return 1;
}

static void initialise_font_bin(void) {
    size_t charsetByteCount;
    size_t charsetStart = 2;
    size_t charsetEnd;
    size_t cursor;
    size_t widthsOffset;
    size_t bitmapOffset;
    size_t expectedBitmapBytes;
    int glyphCount = 0;
    int atlasWidth = 0;
    int index;

    memset(&gFontBin, 0, sizeof(gFontBin));
    for (index = 0; index < FONT_BIN_ASCII_COUNT; index++) {
        gFontBin.asciiIndices[index] = INVALID_GLYPH_INDEX;
    }

    gFontBin.bytes = (const uint8_t *)kPhoneMEFontBinData;
    gFontBin.byteCount = (size_t)kPhoneMEFontBinDataLength;
    if (gFontBin.byteCount < 4) {
        return;
    }

    charsetByteCount =
        ((size_t)gFontBin.bytes[0] << 8) | (size_t)gFontBin.bytes[1];
    charsetEnd = charsetStart + charsetByteCount;
    if (charsetEnd >= gFontBin.byteCount) {
        return;
    }

    cursor = charsetStart;
    while (cursor < charsetEnd) {
        uint32_t codepoint;

        if (glyphCount >= FONT_BIN_MAX_GLYPHS ||
                !decode_utf8_codepoint(
                    gFontBin.bytes,
                    charsetEnd,
                    &cursor,
                    &codepoint) ||
                codepoint > 0xffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return;
        }

        gFontBin.codepoints[glyphCount++] = (jchar)codepoint;
    }

    if (glyphCount <= 0 || cursor != charsetEnd) {
        return;
    }

    gFontBin.height = gFontBin.bytes[charsetEnd];
    if (gFontBin.height != FONT_BIN_EXPECTED_HEIGHT) {
        return;
    }

    widthsOffset = charsetEnd + 1;
    bitmapOffset = widthsOffset + (size_t)glyphCount;
    if (bitmapOffset > gFontBin.byteCount) {
        return;
    }

    gFontBin.widths = gFontBin.bytes + widthsOffset;
    for (index = 0; index < glyphCount; index++) {
        int width = gFontBin.widths[index];
        uint16_t codepoint = (uint16_t)gFontBin.codepoints[index];

        if (width <= 0 || atlasWidth > UINT16_MAX - width) {
            return;
        }

        gFontBin.xOffsets[index] = (uint16_t)atlasWidth;
        atlasWidth += width;

        if (codepoint < FONT_BIN_ASCII_COUNT) {
            gFontBin.asciiIndices[codepoint] = (int16_t)index;
        }
    }

    expectedBitmapBytes =
        (((size_t)atlasWidth * (size_t)gFontBin.height) + 7) / 8;
    if (bitmapOffset + expectedBitmapBytes > gFontBin.byteCount) {
        return;
    }

    gFontBin.bitmap = gFontBin.bytes + bitmapOffset;
    gFontBin.glyphCount = glyphCount;
    gFontBin.atlasWidth = atlasWidth;
    gFontBin.valid = 1;
}

static const PhoneMEFontBin *font_bin(void) {
    pthread_once(&gFontBinOnce, initialise_font_bin);
    return &gFontBin;
}

static int lookup_glyph(const PhoneMEFontBin *font, jchar character) {
    uint16_t codepoint = (uint16_t)character;
    int index;

    if (font == NULL || !font->valid) {
        return INVALID_GLYPH_INDEX;
    }

    if (codepoint < FONT_BIN_ASCII_COUNT) {
        return font->asciiIndices[codepoint];
    }

    for (index = 0; index < font->glyphCount; index++) {
        if ((uint16_t)font->codepoints[index] == codepoint) {
            return index;
        }
    }

    return INVALID_GLYPH_INDEX;
}

static int style_horizontal_padding(int style) {
    int padding = 0;

    if ((style & STYLE_BOLD) != 0) {
        padding += 1;
    }
    if ((style & STYLE_ITALIC) != 0) {
        padding += 2;
    }

    return padding;
}

static int glyph_advance(
        const PhoneMEFontBin *font,
        int glyphIndex,
        int style) {
    return (int)font->widths[glyphIndex] + style_horizontal_padding(style);
}

static int bitmap_text_width(
        const PhoneMEFontBin *font,
        int style,
        const jchar *characters,
        int count) {
    int width = 0;
    int index;

    if (font == NULL || !font->valid || characters == NULL || count <= 0) {
        return -1;
    }

    for (index = 0; index < count; index++) {
        int glyphIndex = lookup_glyph(font, characters[index]);
        int advance;

        if (glyphIndex == INVALID_GLYPH_INDEX) {
            return -1;
        }

        advance = glyph_advance(font, glyphIndex, style);
        if (width > INT_MAX - advance) {
            return -1;
        }
        width += advance;
    }

    return width;
}

static int bitmap_pixel_is_set(
        const PhoneMEFontBin *font,
        int glyphIndex,
        int glyphX,
        int glyphY) {
    size_t bitIndex =
        ((size_t)glyphY * (size_t)font->atlasWidth) +
        (size_t)font->xOffsets[glyphIndex] +
        (size_t)glyphX;

    return (font->bitmap[bitIndex >> 3] >> (bitIndex & 7)) & 1;
}

static void put_rgb565_pixel(
        gxj_screen_buffer *destination,
        int destinationX,
        int destinationY,
        int clipX1,
        int clipY1,
        int clipX2,
        int clipY2,
        gxj_pixel_type pixel) {
    gxj_pixel_type *destinationPixel;

    if (destinationX < clipX1 || destinationX >= clipX2 ||
            destinationY < clipY1 || destinationY >= clipY2) {
        return;
    }

    destinationPixel = destination->pixelData +
        ((size_t)destinationY * (size_t)destination->width) +
        (size_t)destinationX;
    *destinationPixel = pixel;
}

static int draw_bitmap_text(
        int pixel,
        const jshort *clip,
        gxj_screen_buffer *destination,
        int style,
        int x,
        int y,
        const jchar *characters,
        int count) {
    const PhoneMEFontBin *font = font_bin();
    int totalWidth;
    int clipX1;
    int clipY1;
    int clipX2;
    int clipY2;
    int penX = x;
    int characterIndex;
    gxj_pixel_type pixel565;

    if (font == NULL || !font->valid || destination == NULL ||
            destination->pixelData == NULL || clip == NULL ||
            characters == NULL || count <= 0 ||
            destination->width <= 0 || destination->height <= 0) {
        return KNI_FALSE;
    }

    totalWidth = bitmap_text_width(font, style, characters, count);
    if (totalWidth < 0) {
        return KNI_FALSE;
    }

    clipX1 = clip[0] < 0 ? 0 : clip[0];
    clipY1 = clip[1] < 0 ? 0 : clip[1];
    clipX2 = clip[2] > destination->width ? destination->width : clip[2];
    clipY2 = clip[3] > destination->height ? destination->height : clip[3];
    if (clipX1 >= clipX2 || clipY1 >= clipY2) {
        return KNI_TRUE;
    }

    pixel565 = GXJ_RGB24TORGB16(pixel);

    for (characterIndex = 0; characterIndex < count; characterIndex++) {
        int glyphIndex = lookup_glyph(font, characters[characterIndex]);
        int glyphWidth = font->widths[glyphIndex];
        int glyphY;

        for (glyphY = 0; glyphY < font->height; glyphY++) {
            int italicShift = (style & STYLE_ITALIC) != 0
                ? (font->height - 1 - glyphY) / 6
                : 0;
            int glyphX;

            for (glyphX = 0; glyphX < glyphWidth; glyphX++) {
                int destinationX;
                int destinationY;

                if (!bitmap_pixel_is_set(
                        font,
                        glyphIndex,
                        glyphX,
                        glyphY)) {
                    continue;
                }

                destinationX = penX + glyphX + italicShift;
                destinationY = y + glyphY;
                put_rgb565_pixel(
                    destination,
                    destinationX,
                    destinationY,
                    clipX1,
                    clipY1,
                    clipX2,
                    clipY2,
                    pixel565
                );

                if ((style & STYLE_BOLD) != 0) {
                    put_rgb565_pixel(
                        destination,
                        destinationX + 1,
                        destinationY,
                        clipX1,
                        clipY1,
                        clipX2,
                        clipY2,
                        pixel565
                    );
                }
            }
        }

        penX += glyph_advance(font, glyphIndex, style);
    }

    if ((style & STYLE_UNDERLINED) != 0 && totalWidth > 0) {
        int underlineY = y + font->height - 1;
        int underlineX;

        for (underlineX = x; underlineX < x + totalWidth; underlineX++) {
            put_rgb565_pixel(
                destination,
                underlineX,
                underlineY,
                clipX1,
                clipY1,
                clipX2,
                clipY2,
                pixel565
            );
        }
    }

    return KNI_TRUE;
}

static CTFontRef create_coretext_font(int face, int style) {
    CTFontUIFontType fontType = face == FACE_MONOSPACE
        ? kCTFontUIFontUserFixedPitch
        : kCTFontUIFontSystem;
    CTFontRef font = CTFontCreateUIFontForLanguage(
        fontType,
        CORETEXT_FALLBACK_POINT_SIZE,
        NULL
    );

    if (font == NULL) {
        font = CTFontCreateWithName(
            CFSTR("Helvetica"),
            CORETEXT_FALLBACK_POINT_SIZE,
            NULL
        );
    }

    if (font != NULL && (style & (STYLE_BOLD | STYLE_ITALIC)) != 0) {
        CTFontSymbolicTraits traits = 0;
        CTFontRef styledFont;

        if ((style & STYLE_BOLD) != 0) {
            traits |= kCTFontBoldTrait;
        }
        if ((style & STYLE_ITALIC) != 0) {
            traits |= kCTFontItalicTrait;
        }

        styledFont = CTFontCreateCopyWithSymbolicTraits(
            font,
            CORETEXT_FALLBACK_POINT_SIZE,
            NULL,
            traits,
            traits
        );
        if (styledFont != NULL) {
            CFRelease(font);
            font = styledFont;
        }
    }

    return font;
}

static CTLineRef create_coretext_line(
        const jchar *characters,
        int count,
        CTFontRef font) {
    CFStringRef string;
    CFDictionaryRef attributes;
    CFAttributedStringRef attributedString;
    CTLineRef line;
    const void *keys[2];
    const void *values[2];

    if (characters == NULL || count <= 0 || font == NULL) {
        return NULL;
    }

    string = CFStringCreateWithCharacters(
        kCFAllocatorDefault,
        (const UniChar *)characters,
        (CFIndex)count
    );
    if (string == NULL) {
        return NULL;
    }

    keys[0] = kCTFontAttributeName;
    values[0] = font;
    keys[1] = kCTForegroundColorFromContextAttributeName;
    values[1] = kCFBooleanTrue;
    attributes = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        2,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    if (attributes == NULL) {
        CFRelease(string);
        return NULL;
    }

    attributedString = CFAttributedStringCreate(
        kCFAllocatorDefault,
        string,
        attributes
    );
    CFRelease(attributes);
    CFRelease(string);
    if (attributedString == NULL) {
        return NULL;
    }

    line = CTLineCreateWithAttributedString(attributedString);
    CFRelease(attributedString);
    return line;
}

static int coretext_line_width(CTLineRef line) {
    double width;

    if (line == NULL) {
        return -1;
    }

    width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    if (!isfinite(width) || width < 0.0 || width > (double)(INT_MAX - 1)) {
        return -1;
    }

    return width > 0.0 ? (int)ceil(width) : 1;
}

static gxj_pixel_type blend_rgb565(
        gxj_pixel_type destination,
        jint sourceRGB,
        unsigned int alpha) {
    unsigned int sourceRed;
    unsigned int sourceGreen;
    unsigned int sourceBlue;
    unsigned int destinationRed;
    unsigned int destinationGreen;
    unsigned int destinationBlue;
    unsigned int inverseAlpha;
    unsigned int red;
    unsigned int green;
    unsigned int blue;

    if (alpha >= 255) {
        return GXJ_RGB24TORGB16(sourceRGB);
    }

    sourceRed = ((unsigned int)sourceRGB >> 16) & 0xff;
    sourceGreen = ((unsigned int)sourceRGB >> 8) & 0xff;
    sourceBlue = (unsigned int)sourceRGB & 0xff;

    destinationRed = GXJ_GET_RED_FROM_PIXEL(destination);
    destinationGreen = GXJ_GET_GREEN_FROM_PIXEL(destination);
    destinationBlue = GXJ_GET_BLUE_FROM_PIXEL(destination);

    inverseAlpha = 255 - alpha;
    red = (sourceRed * alpha + destinationRed * inverseAlpha + 127) / 255;
    green = (sourceGreen * alpha + destinationGreen * inverseAlpha + 127) / 255;
    blue = (sourceBlue * alpha + destinationBlue * inverseAlpha + 127) / 255;

    return GXJ_RGB24TORGB16((red << 16) | (green << 8) | blue);
}

static int draw_coretext_fallback(
        int pixel,
        const jshort *clip,
        gxj_screen_buffer *destination,
        int face,
        int style,
        int x,
        int y,
        const jchar *characters,
        int count) {
    CTFontRef font;
    CTLineRef line;
    CGRect inkBounds;
    double advance;
    int minimumX;
    int maximumX;
    int maskWidth;
    int maskHeight = FONT_BIN_EXPECTED_HEIGHT;
    int destinationOriginX;
    size_t maskLength;
    uint8_t *mask;
    CGColorSpaceRef colorSpace;
    CGContextRef context;
    int clipX1;
    int clipY1;
    int clipX2;
    int clipY2;
    int sourceY;

    if (destination == NULL || destination->pixelData == NULL ||
            destination->width <= 0 || destination->height <= 0 ||
            clip == NULL || characters == NULL || count <= 0) {
        return KNI_FALSE;
    }

    font = create_coretext_font(face, style);
    if (font == NULL) {
        return KNI_FALSE;
    }

    line = create_coretext_line(characters, count, font);
    CFRelease(font);
    if (line == NULL) {
        return KNI_FALSE;
    }

    advance = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    inkBounds = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
    if (!isfinite(advance) || !isfinite(CGRectGetMinX(inkBounds)) ||
            !isfinite(CGRectGetMaxX(inkBounds))) {
        CFRelease(line);
        return KNI_FALSE;
    }

    minimumX = (int)floor(fmin(0.0, CGRectGetMinX(inkBounds)));
    maximumX = (int)ceil(fmax(advance, CGRectGetMaxX(inkBounds)));
    if (maximumX < minimumX ||
            maximumX - minimumX > INT_MAX -
                (CORETEXT_FALLBACK_PADDING * 2)) {
        CFRelease(line);
        return KNI_FALSE;
    }

    maskWidth = maximumX - minimumX + (CORETEXT_FALLBACK_PADDING * 2);
    if (maskWidth <= 0) {
        maskWidth = 1;
    }

    if ((size_t)maskWidth > SIZE_MAX / (size_t)maskHeight) {
        CFRelease(line);
        return KNI_FALSE;
    }

    maskLength = (size_t)maskWidth * (size_t)maskHeight;
    mask = (uint8_t *)calloc(maskLength, sizeof(uint8_t));
    if (mask == NULL) {
        CFRelease(line);
        return KNI_FALSE;
    }

    colorSpace = CGColorSpaceCreateDeviceGray();
    if (colorSpace == NULL) {
        free(mask);
        CFRelease(line);
        return KNI_FALSE;
    }

    context = CGBitmapContextCreate(
        mask,
        (size_t)maskWidth,
        (size_t)maskHeight,
        8,
        (size_t)maskWidth,
        colorSpace,
        (CGBitmapInfo)kCGImageAlphaNone
    );
    CGColorSpaceRelease(colorSpace);
    if (context == NULL) {
        free(mask);
        CFRelease(line);
        return KNI_FALSE;
    }

    CGContextSetAllowsAntialiasing(context, true);
    CGContextSetShouldAntialias(context, true);
    CGContextSetShouldSmoothFonts(context, true);
    CGContextSetGrayFillColor(context, 1.0, 1.0);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetTextPosition(
        context,
        (CGFloat)(CORETEXT_FALLBACK_PADDING - minimumX),
        CORETEXT_FALLBACK_BASELINE
    );
    CTLineDraw(line, context);

    if ((style & STYLE_UNDERLINED) != 0 && advance > 0.0) {
        CGContextFillRect(
            context,
            CGRectMake(
                (CGFloat)(CORETEXT_FALLBACK_PADDING - minimumX),
                0.0,
                (CGFloat)advance,
                1.0
            )
        );
    }

    CGContextRelease(context);
    CFRelease(line);

    clipX1 = clip[0] < 0 ? 0 : clip[0];
    clipY1 = clip[1] < 0 ? 0 : clip[1];
    clipX2 = clip[2] > destination->width ? destination->width : clip[2];
    clipY2 = clip[3] > destination->height ? destination->height : clip[3];
    destinationOriginX =
        x + minimumX - CORETEXT_FALLBACK_PADDING;

    if (clipX1 < clipX2 && clipY1 < clipY2) {
        for (sourceY = 0; sourceY < maskHeight; sourceY++) {
            int destinationY = y + sourceY;
            int sourceX;

            if (destinationY < clipY1 || destinationY >= clipY2) {
                continue;
            }

            for (sourceX = 0; sourceX < maskWidth; sourceX++) {
                unsigned int alpha = mask[
                    (size_t)sourceY * (size_t)maskWidth + (size_t)sourceX
                ];
                int destinationX;
                gxj_pixel_type *destinationPixel;

                if (alpha == 0) {
                    continue;
                }

                destinationX = destinationOriginX + sourceX;
                if (destinationX < clipX1 || destinationX >= clipX2) {
                    continue;
                }

                destinationPixel = destination->pixelData +
                    ((size_t)destinationY * (size_t)destination->width) +
                    (size_t)destinationX;
                *destinationPixel = blend_rgb565(
                    *destinationPixel,
                    pixel,
                    alpha
                );
            }
        }
    }

    free(mask);
    return KNI_TRUE;
}

int gxjport_draw_chars(
        int pixel,
        const jshort *clip,
        void *dst,
        int dotted,
        int face,
        int style,
        int size,
        int x,
        int y,
        int anchor,
        const jchar *chararray,
        int n) {
    gxj_screen_buffer *destination = (gxj_screen_buffer *)dst;

    (void)dotted;
    (void)size;
    (void)anchor;

    if (bitmap_text_width(font_bin(), style, chararray, n) >= 0) {
        return draw_bitmap_text(
            pixel,
            clip,
            destination,
            style,
            x,
            y,
            chararray,
            n
        );
    }

    return draw_coretext_fallback(
        pixel,
        clip,
        destination,
        face,
        style,
        x,
        y,
        chararray,
        n
    );
}

int gxjport_get_font_info(
        int face,
        int style,
        int size,
        int *ascent,
        int *descent,
        int *leading) {
    (void)face;
    (void)style;
    (void)size;

    if (ascent == NULL || descent == NULL || leading == NULL) {
        return KNI_FALSE;
    }

    *ascent = FONT_BIN_ASCENT;
    *descent = FONT_BIN_DESCENT;
    *leading = FONT_BIN_LEADING;
    return KNI_TRUE;
}

int gxjport_get_chars_width(
        int face,
        int style,
        int size,
        const jchar *charArray,
        int n) {
    int width;
    CTFontRef font;
    CTLineRef line;

    (void)size;

    width = bitmap_text_width(font_bin(), style, charArray, n);
    if (width >= 0) {
        return width;
    }

    font = create_coretext_font(face, style);
    if (font == NULL) {
        return -1;
    }

    line = create_coretext_line(charArray, n, font);
    CFRelease(font);
    if (line == NULL) {
        return -1;
    }

    width = coretext_line_width(line);
    CFRelease(line);
    return width;
}
