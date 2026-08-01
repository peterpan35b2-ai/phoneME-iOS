/* KNI shim between the MIDP HttpsConnection implementation and iOS. */
#include <stdint.h>
#include <stdlib.h>

#include <kni.h>
#include <midpError.h>
#include <midpUtilKni.h>
#include <sni.h>

extern int32_t phoneme_ios_https_execute(
    const char* url,
    const char* method,
    const char* headers,
    const uint8_t* body,
    int32_t body_length,
    int32_t timeout_ms
);
extern int32_t phoneme_ios_https_get_status_code(int32_t handle);
extern int32_t phoneme_ios_https_copy_string(
    int32_t handle,
    int32_t field,
    char* destination,
    int32_t capacity
);
extern int32_t phoneme_ios_https_copy_body(
    int32_t handle,
    uint8_t* destination,
    int32_t capacity
);
extern int64_t phoneme_ios_https_get_long(int32_t handle, int32_t field);
extern void phoneme_ios_https_close(int32_t handle);

KNIEXPORT KNI_RETURNTYPE_INT
KNIDECL(com_sun_midp_io_j2me_https_Protocol_nExecute) {
    int32_t result = 0;
    int32_t timeout_ms = KNI_GetParameterAsInt(5);
    int32_t body_length = 0;
    uint8_t* body = NULL;

    KNI_StartHandles(4);
    KNI_DeclareHandle(body_handle);
    KNI_GetParameterAsObject(4, body_handle);
    if (!KNI_IsNullHandle(body_handle)) {
        body_length = (int32_t)KNI_GetArrayLength(body_handle);
        if (body_length > 0) {
            body = (uint8_t*)malloc((size_t)body_length);
            if (body != NULL) {
                KNI_GetRawArrayRegion(
                    body_handle,
                    0,
                    body_length,
                    (jbyte*)body
                );
            }
        }
    }

    if (body_length == 0 || body != NULL) {
        GET_PARAMETER_AS_PCSL_STRING(1, url_string) {
            GET_PARAMETER_AS_PCSL_STRING(2, method_string) {
                GET_PARAMETER_AS_PCSL_STRING(3, headers_string) {
                    const char* url = (const char*)
                        pcsl_string_get_utf8_data(&url_string);
                    const char* method = (const char*)
                        pcsl_string_get_utf8_data(&method_string);
                    const char* headers = (const char*)
                        pcsl_string_get_utf8_data(&headers_string);

                    if (url != NULL && method != NULL && headers != NULL) {
                        result = phoneme_ios_https_execute(
                            url,
                            method,
                            headers,
                            body,
                            body_length,
                            timeout_ms
                        );
                    }

                    if (headers != NULL) {
                        pcsl_string_release_utf8_data(
                            (const jbyte*)headers,
                            &headers_string
                        );
                    }
                    if (method != NULL) {
                        pcsl_string_release_utf8_data(
                            (const jbyte*)method,
                            &method_string
                        );
                    }
                    if (url != NULL) {
                        pcsl_string_release_utf8_data(
                            (const jbyte*)url,
                            &url_string
                        );
                    }
                } RELEASE_PCSL_STRING_PARAMETER
            } RELEASE_PCSL_STRING_PARAMETER
        } RELEASE_PCSL_STRING_PARAMETER
    }

    free(body);
    KNI_EndHandles();
    KNI_ReturnInt(result);
}

KNIEXPORT KNI_RETURNTYPE_INT
KNIDECL(com_sun_midp_io_j2me_https_Protocol_nGetStatusCode) {
    KNI_ReturnInt(
        phoneme_ios_https_get_status_code(KNI_GetParameterAsInt(1))
    );
}

KNIEXPORT KNI_RETURNTYPE_OBJECT
KNIDECL(com_sun_midp_io_j2me_https_Protocol_nGetString) {
    int32_t handle = KNI_GetParameterAsInt(1);
    int32_t field = KNI_GetParameterAsInt(2);
    int32_t length = phoneme_ios_https_copy_string(
        handle, field, NULL, 0);
    char* value = NULL;

    KNI_StartHandles(1);
    KNI_DeclareHandle(result);

    if (length >= 0) {
        value = (char*)malloc((size_t)length + 1U);
        if (value == NULL) {
            KNI_ThrowNew(midpOutOfMemoryError, NULL);
        } else if (phoneme_ios_https_copy_string(
                handle, field, value, length + 1) < 0) {
            free(value);
            value = NULL;
        } else {
            KNI_NewStringUTF(value, result);
            free(value);
            value = NULL;
        }
    }

    free(value);
    KNI_EndHandlesAndReturnObject(result);
}

KNIEXPORT KNI_RETURNTYPE_OBJECT
KNIDECL(com_sun_midp_io_j2me_https_Protocol_nGetBody) {
    int32_t handle = KNI_GetParameterAsInt(1);
    int32_t length = phoneme_ios_https_copy_body(handle, NULL, 0);
    uint8_t* body = NULL;

    KNI_StartHandles(1);
    KNI_DeclareHandle(result);

    if (length >= 0) {
        if (length > 0) {
            body = (uint8_t*)malloc((size_t)length);
        }
        if (length > 0 && body == NULL) {
            KNI_ThrowNew(midpOutOfMemoryError, NULL);
        } else {
            SNI_NewArray(SNI_BYTE_ARRAY, length, result);
            if (KNI_IsNullHandle(result)) {
                KNI_ThrowNew(midpOutOfMemoryError, NULL);
            } else if (length > 0 &&
                    phoneme_ios_https_copy_body(
                        handle, body, length) == length) {
                KNI_SetRawArrayRegion(
                    result, 0, length, (jbyte*)body);
            }
        }
    }

    free(body);
    KNI_EndHandlesAndReturnObject(result);
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(com_sun_midp_io_j2me_https_Protocol_nGetLong) {
    KNI_ReturnLong((jlong)phoneme_ios_https_get_long(
        KNI_GetParameterAsInt(1),
        KNI_GetParameterAsInt(2)
    ));
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_https_Protocol_nClose) {
    phoneme_ios_https_close(KNI_GetParameterAsInt(1));
    KNI_ReturnVoid();
}
