/*
 * Embedded phoneME preverifier bridge for phoneME-iOS.
 *
 * The historical tool is a process-oriented command line program. This API
 * keeps its class verifier in-process so imported merged/obfuscated MIDlet
 * launchers can have CLDC StackMap attributes regenerated on iOS.
 */

#ifndef PHONEME_PREVERIFY_H
#define PHONEME_PREVERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PhoneMEPreverifyResult {
    int attempted;
    int succeeded;
    int failed;
    int skipped;
} PhoneMEPreverifyResult;

/*
 * class_list_path must contain one UTF-8 JAR entry path per line. Only
 * entries ending in .class are processed. Successful classes are written
 * below output_directory using their original JAR-relative paths.
 *
 * Returns 0 when the pass completed, even if individual classes failed.
 * Negative values report invalid arguments or an unrecoverable setup error.
 */
int phoneme_preverify_jar_classes(
    const char* runtime_classes_path,
    const char* jar_path,
    const char* class_list_path,
    const char* output_directory,
    PhoneMEPreverifyResult* result_out
);

#ifdef __cplusplus
}
#endif

#endif /* PHONEME_PREVERIFY_H */
