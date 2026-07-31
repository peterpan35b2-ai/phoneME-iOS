#include <stddef.h>
#include <stdint.h>
#include <kni.h>
#include <sni.h>
#include <midpError.h>
#include <imgapi_image.h>
#include <gxj_putpixel.h>
#include "imgj_rgb.h"

extern int phoneme_ios_device_set_backlight(int mode);
extern int phoneme_ios_device_flash_lights(int64_t duration_ms);
extern int phoneme_ios_device_start_vibrate(int frequency, int64_t duration_ms);
extern int phoneme_ios_device_stop_vibrate(void);

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_nokia_mid_ui_DirectGraphicsImpl_nGetPixels) {
    jint offset = KNI_GetParameterAsInt(3);
    jint scanlength = KNI_GetParameterAsInt(4);
    jint x = KNI_GetParameterAsInt(5);
    jint y = KNI_GetParameterAsInt(6);
    jint width = KNI_GetParameterAsInt(7);
    jint height = KNI_GetParameterAsInt(8);
    jint trans_x = 0;
    jint trans_y = 0;
    jint array_length = 0;
    jint has_image = KNI_FALSE;
    jint valid = KNI_TRUE;
    jint *target = NULL;
    jfieldID field;
    img_native_error_codes error = IMG_NATIVE_IMAGE_NO_ERROR;

    KNI_StartHandles(6);
    KNI_DeclareHandle(graphics);
    KNI_DeclareHandle(pixels);
    KNI_DeclareHandle(graphics_class);
    KNI_DeclareHandle(image);
    KNI_DeclareHandle(image_class);
    KNI_DeclareHandle(image_data);

    KNI_GetParameterAsObject(1, graphics);
    KNI_GetParameterAsObject(2, pixels);

    if (KNI_IsNullHandle(graphics) || KNI_IsNullHandle(pixels) ||
            width < 0 || height < 0) {
        valid = KNI_FALSE;
    }

    if (valid) {
        jlong last_row = height > 0
                ? (jlong)(height - 1) * (jlong)scanlength
                : 0;
        jlong minimum_index = (jlong)offset +
                (last_row < 0 ? last_row : 0);
        jlong maximum_index = (jlong)offset +
                (last_row > 0 ? last_row : 0) + (jlong)width;
        array_length = KNI_GetArrayLength(pixels);
        if (minimum_index < 0 || maximum_index > array_length) {
            valid = KNI_FALSE;
        }
    }

    if (valid) {
        KNI_GetObjectClass(graphics, graphics_class);
        field = KNI_GetFieldID(graphics_class, "transX", "I");
        if (field != 0) {
            trans_x = KNI_GetIntField(graphics, field);
        }
        field = KNI_GetFieldID(graphics_class, "transY", "I");
        if (field != 0) {
            trans_y = KNI_GetIntField(graphics, field);
        }
        field = KNI_GetFieldID(
                graphics_class,
                "img",
                "Ljavax/microedition/lcdui/Image;"
        );
        if (field != 0) {
            KNI_GetObjectField(graphics, field, image);
        }

        x += trans_x;
        y += trans_y;
        has_image = !KNI_IsNullHandle(image);

        if (has_image) {
            KNI_GetObjectClass(image, image_class);
            field = KNI_GetFieldID(
                    image_class,
                    "imageData",
                    "Ljavax/microedition/lcdui/ImageData;"
            );
            if (field != 0) {
                KNI_GetObjectField(image, field, image_data);
            }
            if (KNI_IsNullHandle(image_data)) {
                valid = KNI_FALSE;
            }
        }
    }

    if (valid && width > 0 && height > 0) {
        SNI_BEGIN_RAW_POINTERS;
        target = JavaIntArray(pixels);

        if (has_image) {
            java_imagedata *source = IMGAPI_GET_IMAGEDATA_PTR(image_data);
            if (source == NULL || x < 0 || y < 0 ||
                    x + width > source->width ||
                    y + height > source->height) {
                error = IMG_NATIVE_IMAGE_DECODING_ERROR;
            } else {
                imgj_get_argb(
                        source,
                        target,
                        offset,
                        scanlength,
                        x,
                        y,
                        width,
                        height,
                        &error
                );
            }
        } else {
            gxj_screen_buffer *screen = &gxj_system_screen_buffer;
            if (screen->pixelData == NULL || x < 0 || y < 0 ||
                    x + width > screen->width ||
                    y + height > screen->height) {
                error = IMG_NATIVE_IMAGE_DECODING_ERROR;
            } else {
                jint row;
                for (row = 0; row < height; row++) {
                    jint column;
                    gxj_pixel_type *source = screen->pixelData +
                            (y + row) * screen->width + x;
                    jint *destination = target + offset + row * scanlength;
                    for (column = 0; column < width; column++) {
                        gxj_pixel_type pixel = source[column];
                        jint red = GXJ_GET_RED_FROM_PIXEL(pixel);
                        jint green = GXJ_GET_GREEN_FROM_PIXEL(pixel);
                        jint blue = GXJ_GET_BLUE_FROM_PIXEL(pixel);
                        red |= red >> 5;
                        green |= green >> 6;
                        blue |= blue >> 5;
                        destination[column] = (jint)0xff000000 |
                                (red << 16) | (green << 8) | blue;
                    }
                }
            }
        }
        SNI_END_RAW_POINTERS;
    }

    if (!valid || error != IMG_NATIVE_IMAGE_NO_ERROR) {
        KNI_ThrowNew(
                midpIllegalArgumentException,
                "Pixel region is outside the graphics surface"
        );
    }

    KNI_EndHandles();
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_nokia_mid_ui_DeviceControl_nSetLights) {
    jint level = KNI_GetParameterAsInt(1);
    phoneme_ios_device_set_backlight(level > 0 ? 1 : 0);
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_nokia_mid_ui_DeviceControl_nFlashLights) {
    phoneme_ios_device_flash_lights(
            (int64_t)KNI_GetParameterAsLong(1));
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_nokia_mid_ui_DeviceControl_nStartVibra) {
    phoneme_ios_device_start_vibrate(
            KNI_GetParameterAsInt(1),
            (int64_t)KNI_GetParameterAsLong(2));
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_nokia_mid_ui_DeviceControl_nStopVibra) {
    phoneme_ios_device_stop_vibrate();
    KNI_ReturnVoid();
}
