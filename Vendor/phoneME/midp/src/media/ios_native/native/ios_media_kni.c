/* KNI shim between javax.microedition.media.IOSPlayer and the iOS app. */
#include <stdint.h>
#include <stdlib.h>
#include <kni.h>
#include <midpError.h>
#include <midpUtilKni.h>

extern int32_t phoneme_ios_media_create_data(
    const uint8_t* data,
    int32_t length,
    const char* content_type
);
extern int32_t phoneme_ios_media_create_locator(
    const char* locator,
    const char* content_type
);
extern int32_t phoneme_ios_media_start(int32_t handle);
extern int32_t phoneme_ios_media_stop(int32_t handle);
extern void phoneme_ios_media_close(int32_t handle);
extern void phoneme_ios_media_set_loop_count(int32_t handle, int32_t count);
extern void phoneme_ios_media_set_volume(int32_t handle, int32_t level);
extern void phoneme_ios_media_set_mute(int32_t handle, int32_t muted);
extern int64_t phoneme_ios_media_set_time(int32_t handle, int64_t microseconds);
extern int64_t phoneme_ios_media_get_time(int32_t handle);
extern int64_t phoneme_ios_media_get_duration(int32_t handle);
extern int32_t phoneme_ios_media_is_playing(int32_t handle);
extern int32_t phoneme_ios_media_has_ended(int32_t handle);
extern int32_t phoneme_ios_media_has_error(int32_t handle);
extern int32_t phoneme_ios_media_play_tone(
    int32_t note,
    int32_t duration_ms,
    int32_t volume
);

KNIEXPORT KNI_RETURNTYPE_INT
KNIDECL(javax_microedition_media_IOSPlayer_nCreateData) {
    int32_t result = 0;
    uint8_t* bytes = NULL;
    int32_t length = 0;

    KNI_StartHandles(2);
    KNI_DeclareHandle(data_handle);
    KNI_GetParameterAsObject(1, data_handle);

    if (!KNI_IsNullHandle(data_handle)) {
        length = (int32_t)KNI_GetArrayLength(data_handle);
        if (length > 0) {
            bytes = (uint8_t*)malloc((size_t)length);
            if (bytes != NULL) {
                KNI_GetRawArrayRegion(
                    data_handle,
                    0,
                    length,
                    (jbyte*)bytes
                );
            }
        }
    }

    GET_PARAMETER_AS_PCSL_STRING(2, content_type) {
        const char* type_utf8 =
            (const char*)pcsl_string_get_utf8_data(&content_type);
        if ((length == 0 || bytes != NULL) && type_utf8 != NULL) {
            result = phoneme_ios_media_create_data(
                bytes,
                length,
                type_utf8
            );
        }
        if (type_utf8 != NULL) {
            pcsl_string_release_utf8_data(
                (const jbyte*)type_utf8,
                &content_type
            );
        }
    } RELEASE_PCSL_STRING_PARAMETER

    free(bytes);
    KNI_EndHandles();
    KNI_ReturnInt(result);
}

KNIEXPORT KNI_RETURNTYPE_INT
KNIDECL(javax_microedition_media_IOSPlayer_nCreateLocator) {
    int32_t result = 0;

    KNI_StartHandles(2);
    GET_PARAMETER_AS_PCSL_STRING(1, locator) {
        GET_PARAMETER_AS_PCSL_STRING(2, content_type) {
            const char* locator_utf8 =
                (const char*)pcsl_string_get_utf8_data(&locator);
            const char* type_utf8 =
                (const char*)pcsl_string_get_utf8_data(&content_type);
            if (locator_utf8 != NULL) {
                result = phoneme_ios_media_create_locator(
                    locator_utf8,
                    type_utf8
                );
            }
            if (type_utf8 != NULL) {
                pcsl_string_release_utf8_data(
                    (const jbyte*)type_utf8,
                    &content_type
                );
            }
            if (locator_utf8 != NULL) {
                pcsl_string_release_utf8_data(
                    (const jbyte*)locator_utf8,
                    &locator
                );
            }
        } RELEASE_PCSL_STRING_PARAMETER
    } RELEASE_PCSL_STRING_PARAMETER
    KNI_EndHandles();

    KNI_ReturnInt(result);
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(javax_microedition_media_IOSPlayer_nStart) {
    KNI_ReturnBoolean(
        phoneme_ios_media_start(KNI_GetParameterAsInt(1)) != 0
    );
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(javax_microedition_media_IOSPlayer_nStop) {
    KNI_ReturnBoolean(
        phoneme_ios_media_stop(KNI_GetParameterAsInt(1)) != 0
    );
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(javax_microedition_media_IOSPlayer_nClose) {
    phoneme_ios_media_close(KNI_GetParameterAsInt(1));
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(javax_microedition_media_IOSPlayer_nSetLoopCount) {
    phoneme_ios_media_set_loop_count(
        KNI_GetParameterAsInt(1),
        KNI_GetParameterAsInt(2)
    );
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(javax_microedition_media_IOSPlayer_nSetVolume) {
    phoneme_ios_media_set_volume(
        KNI_GetParameterAsInt(1),
        KNI_GetParameterAsInt(2)
    );
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(javax_microedition_media_IOSPlayer_nSetMute) {
    phoneme_ios_media_set_mute(
        KNI_GetParameterAsInt(1),
        KNI_GetParameterAsBoolean(2) ? 1 : 0
    );
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(javax_microedition_media_IOSPlayer_nSetMediaTime) {
    KNI_ReturnLong(
        (jlong)phoneme_ios_media_set_time(
            KNI_GetParameterAsInt(1),
            (int64_t)KNI_GetParameterAsLong(2)
        )
    );
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(javax_microedition_media_IOSPlayer_nGetMediaTime) {
    KNI_ReturnLong(
        (jlong)phoneme_ios_media_get_time(KNI_GetParameterAsInt(1))
    );
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(javax_microedition_media_IOSPlayer_nGetDuration) {
    KNI_ReturnLong(
        (jlong)phoneme_ios_media_get_duration(KNI_GetParameterAsInt(1))
    );
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(javax_microedition_media_IOSPlayer_nIsPlaying) {
    KNI_ReturnBoolean(
        phoneme_ios_media_is_playing(KNI_GetParameterAsInt(1)) != 0
    );
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(javax_microedition_media_IOSPlayer_nHasEnded) {
    KNI_ReturnBoolean(
        phoneme_ios_media_has_ended(KNI_GetParameterAsInt(1)) != 0
    );
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(javax_microedition_media_IOSPlayer_nHasError) {
    KNI_ReturnBoolean(
        phoneme_ios_media_has_error(KNI_GetParameterAsInt(1)) != 0
    );
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(javax_microedition_media_IOSPlayer_nPlayTone) {
    KNI_ReturnBoolean(
        phoneme_ios_media_play_tone(
            KNI_GetParameterAsInt(1),
            KNI_GetParameterAsInt(2),
            KNI_GetParameterAsInt(3)
        ) != 0
    );
}
