/*
 * Embedded phoneME preverifier bridge for phoneME-iOS.
 */

#include "phoneme_preverify.h"

#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "oobj.h"
#include "path.h"
#include "sys_api.h"
#include "tree.h"

/* Globals historically owned by the standalone preverifier's main.c. */
int processedfile = 0;
int errorCode = 0;
bool_t no_native_methods = FALSE;
bool_t no_floating_point = FALSE;
bool_t no_finalizers = FALSE;
bool_t tmpDirExists = FALSE;
char tmp_dir[32];

extern char* output_dir;
extern bool_t stack_map_on;
extern bool_t inline_jsr_on;
extern struct StrIDhash* nameTypeHash;
extern struct StrIDhash* stringHash;

/* WriteClass() uses this short, deterministic output path in embedded mode.
 * The original class name may contain a ZIP component longer than NAME_MAX,
 * which is legal inside a JAR but cannot be materialized as an iOS file. */
const char* phoneme_preverify_output_path = 0;

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Required by the legacy class loader; the standalone tool defined this in
 * main.c, which is intentionally not linked into the iOS application. */
int OpenCode(char* filename, char* short_filename, char* directory, struct stat* status)
{
    int descriptor;
    (void)short_filename;
    (void)directory;

    if (filename == NULL || status == NULL) {
        return -2;
    }
    descriptor = open(filename, O_RDONLY | O_BINARY);
    if (descriptor < 0 || fstat(descriptor, status) < 0) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return -2;
    }
    return descriptor;
}

static pthread_mutex_t phoneme_preverify_mutex = PTHREAD_MUTEX_INITIALIZER;
static jmp_buf phoneme_preverify_abort_environment;
static volatile int phoneme_preverify_abort_active;
static volatile int phoneme_preverify_abort_status;

/* All legacy sources are compiled with exit=phoneme_preverify_abort. */
__attribute__((noreturn))
void phoneme_preverify_abort(int status)
{
    phoneme_preverify_abort_status = status == 0 ? 1 : status;
    if (phoneme_preverify_abort_active) {
        longjmp(phoneme_preverify_abort_environment, 1);
    }
    abort();
}

static void release_loaded_classes(void)
{
    while (nbinclasses > 0) {
        ClassClass* cb = binclasses[nbinclasses - 1];
        nbinclasses--;
        if (cb != 0) {
            void* class_object = cb->obj;
            FreeClass(cb);
            if (class_object != 0) {
                sysFree(class_object);
            }
            sysFree(cb);
        }
    }

    if (binclasses != 0) {
        sysFree(binclasses);
    }
    binclasses = 0;
    sizebinclasses = 0;

    if (nameTypeHash != 0) {
        Str2IDFree(&nameTypeHash);
    }
    if (stringHash != 0) {
        Str2IDFree(&stringHash);
    }
}

static void reset_verifier_flags(void)
{
    phoneme_preverify_abort_active = 0;
    phoneme_preverify_abort_status = 0;
    errorCode = 0;
    processedfile = 0;
    tmpDirExists = FALSE;
    stack_map_on = TRUE;
    inline_jsr_on = TRUE;
    no_native_methods = FALSE;
    no_floating_point = FALSE;
    no_finalizers = FALSE;
    memset(tmp_dir, 0, sizeof(tmp_dir));
}

static void reset_verifier_state(void)
{
    release_loaded_classes();
    phoneme_preverify_reset_classpath();
    reset_verifier_flags();
}

static void abandon_failed_verifier_state(void)
{
    /*
     * VerifyFile() was designed for a short-lived command line process. A
     * failed dependency may leave ClassClass objects linked to half-created
     * peers, so recursively freeing that graph can hang or crash. Detach the
     * globals and intentionally leak this one import pass; future passes start
     * from a clean state and the iOS process remains usable.
     */
    binclasses = 0;
    nbinclasses = 0;
    sizebinclasses = 0;
    nameTypeHash = 0;
    stringHash = 0;
    phoneme_preverify_abandon_classpath();
    reset_verifier_flags();
}

static int has_class_suffix(const char* value, size_t length)
{
    static const char suffix[] = ".class";
    const size_t suffix_length = sizeof(suffix) - 1U;
    return length > suffix_length
        && memcmp(value + length - suffix_length, suffix, suffix_length) == 0;
}

static int is_safe_class_entry(const char* value, size_t length)
{
    const char* component = value;
    const char* cursor = value;
    const char* end = value + length;

    if (length == 0 || value[0] == '/' || value[0] == '\\') {
        return 0;
    }

    while (cursor < end) {
        if (*cursor == '\\') {
            return 0;
        }
        if (*cursor == '/') {
            size_t component_length = (size_t)(cursor - component);
            if (component_length == 0
                    || (component_length == 1 && component[0] == '.')
                    || (component_length == 2
                        && component[0] == '.' && component[1] == '.')) {
                return 0;
            }
            component = cursor + 1;
        }
        cursor++;
    }

    {
        size_t component_length = (size_t)(end - component);
        if (component_length == 0
                || (component_length == 1 && component[0] == '.')
                || (component_length == 2
                    && component[0] == '.' && component[1] == '.')) {
            return 0;
        }
    }
    return 1;
}

static void trim_line_ending(char* line)
{
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

int phoneme_preverify_jar_classes(
        const char* runtime_classes_path,
        const char* jar_path,
        const char* class_list_path,
        const char* output_directory,
        PhoneMEPreverifyResult* result_out)
{
    PhoneMEPreverifyResult result;
    FILE* class_list = 0;
    char* class_path = 0;
    char* previous_class_path = 0;
    char* line = 0;
    size_t line_capacity = 0;
    char* output_path = 0;
    size_t output_path_capacity = 0;
    unsigned long line_index = 0;
    int return_code = 0;

    memset(&result, 0, sizeof(result));
    if (result_out != 0) {
        memset(result_out, 0, sizeof(*result_out));
    }

    if (runtime_classes_path == 0 || jar_path == 0 || class_list_path == 0
            || output_directory == 0 || runtime_classes_path[0] == '\0'
            || jar_path[0] == '\0' || class_list_path[0] == '\0'
            || output_directory[0] == '\0') {
        return -1;
    }

    class_list = fopen(class_list_path, "rb");
    if (class_list == 0) {
        return -2;
    }

    pthread_mutex_lock(&phoneme_preverify_mutex);
    reset_verifier_state();

    {
        const char* current = getenv("CLASSPATH");
        if (current != 0) {
            previous_class_path = strdup(current);
        }
    }

    {
        size_t runtime_length = strlen(runtime_classes_path);
        size_t jar_length = strlen(jar_path);
        if (runtime_length > ((size_t)-1) - jar_length - 2U) {
            return_code = -3;
            goto finished;
        }
        class_path = (char*)malloc(runtime_length + jar_length + 2U);
        if (class_path == 0) {
            return_code = -3;
            goto finished;
        }
        memcpy(class_path, runtime_classes_path, runtime_length);
        class_path[runtime_length] = ':';
        memcpy(class_path + runtime_length + 1U, jar_path, jar_length + 1U);
    }

    if (setenv("CLASSPATH", class_path, 1) != 0) {
        return_code = -4;
        goto finished;
    }

    output_dir = (char*)output_directory;
    {
        size_t output_directory_length = strlen(output_directory);
        if (output_directory_length > ((size_t)-1) - 32U) {
            return_code = -3;
            goto finished;
        }
        output_path_capacity = output_directory_length + 32U;
    }
    output_path = (char*)malloc(output_path_capacity);
    if (output_path == 0) {
        return_code = -3;
        goto finished;
    }

    while (getline(&line, &line_capacity, class_list) >= 0) {
        size_t length;
        unsigned long current_line_index = line_index++;
        int output_length;

        trim_line_ending(line);
        length = strlen(line);

        if (!has_class_suffix(line, length)) {
            continue;
        }
        if (!is_safe_class_entry(line, length)) {
            result.skipped++;
            continue;
        }

        output_length = snprintf(
            output_path,
            output_path_capacity,
            "%s/%08lx.class",
            output_directory,
            current_line_index
        );
        if (output_length < 0
                || (size_t)output_length >= output_path_capacity) {
            return_code = -3;
            break;
        }

        line[length - 6U] = '\0';
        result.attempted++;
        errorCode = 0;
        phoneme_preverify_abort_status = 0;
        phoneme_preverify_output_path = output_path;
        phoneme_preverify_abort_active = 1;

        if (setjmp(phoneme_preverify_abort_environment) == 0) {
            VerifyFile(line);
            phoneme_preverify_abort_active = 0;
            phoneme_preverify_output_path = 0;
            if (errorCode == 0) {
                result.succeeded++;
            } else {
                /*
                 * The legacy verifier treats a failed class as process-fatal.
                 * Its dependency cache is not resumable after this point.
                 * Keep all successfully generated outputs and end the pass;
                 * continuing would dereference partially initialized classes.
                 */
                result.failed++;
                break;
            }
        } else {
            phoneme_preverify_abort_active = 0;
            phoneme_preverify_output_path = 0;
            result.failed++;
            errorCode = 0;
            break;
        }
    }

finished:
    phoneme_preverify_abort_active = 0;
    phoneme_preverify_output_path = 0;
    output_dir = (char*)"output";
    if (result.failed > 0) {
        abandon_failed_verifier_state();
    } else {
        reset_verifier_state();
    }

    if (previous_class_path != 0) {
        (void)setenv("CLASSPATH", previous_class_path, 1);
    } else {
        (void)unsetenv("CLASSPATH");
    }

    free(previous_class_path);
    free(class_path);
    free(output_path);
    free(line);
    pthread_mutex_unlock(&phoneme_preverify_mutex);
    fclose(class_list);

    if (result_out != 0) {
        *result_out = result;
    }
    return return_code;
}
