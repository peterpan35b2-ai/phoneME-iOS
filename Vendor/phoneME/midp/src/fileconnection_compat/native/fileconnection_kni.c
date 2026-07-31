#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <kni.h>
#include <sni.h>
#include <midpError.h>
#include <midpUtilKni.h>

static char* fc_copy_utf8(const pcsl_string* value) {
    const jbyte* raw = pcsl_string_get_utf8_data(value);
    char* result = NULL;
    if (raw != NULL) {
        size_t length = strlen((const char*)raw);
        result = (char*)malloc(length + 1U);
        if (result != NULL) {
            memcpy(result, raw, length + 1U);
        }
        pcsl_string_release_utf8_data(raw, value);
    }
    return result;
}

static char* fc_get_parameter_utf8(int parameter_index) {
    char* result = NULL;
    pcsl_string value = PCSL_STRING_NULL;

    KNI_StartHandles(1);
    KNI_DeclareHandle(string_handle);
    KNI_GetParameterAsObject(parameter_index, string_handle);
    if (!KNI_IsNullHandle(string_handle) &&
            midp_jstring_to_pcsl_string(string_handle, &value) ==
                    PCSL_STRING_OK) {
        result = fc_copy_utf8(&value);
        pcsl_string_free(&value);
    }
    KNI_EndHandles();
    return result;
}

static int fc_mkdir_p(const char* path) {
    char* copy;
    char* cursor;
    size_t length;

    if (path == NULL || path[0] == '\0') return -1;
    length = strlen(path);
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) return -1;
    memcpy(copy, path, length + 1U);

    if (length > 1U && copy[length - 1U] == '/') {
        copy[length - 1U] = '\0';
    }
    for (cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
                free(copy);
                return -1;
            }
            *cursor = '/';
        }
    }
    if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
        free(copy);
        return -1;
    }
    free(copy);
    return 0;
}

static int fc_write_all_fd(int descriptor, const uint8_t* data, size_t length) {
    size_t written = 0;
    while (written < length) {
        ssize_t count = write(descriptor, data + written, length - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (count == 0) return -1;
        written += (size_t)count;
    }
    return 0;
}

static int64_t fc_directory_size(const char* path, int recursive, int* failed) {
    DIR* directory;
    struct dirent* entry;
    int64_t total = 0;

    directory = opendir(path);
    if (directory == NULL) {
        *failed = 1;
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        char* child;
        size_t path_length;
        size_t name_length;
        struct stat info;
        if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        path_length = strlen(path);
        name_length = strlen(entry->d_name);
        child = (char*)malloc(path_length + name_length + 2U);
        if (child == NULL) {
            *failed = 1;
            break;
        }
        memcpy(child, path, path_length);
        if (path_length > 0 && path[path_length - 1] != '/') {
            child[path_length++] = '/';
        }
        memcpy(child + path_length, entry->d_name, name_length + 1U);
        if (lstat(child, &info) != 0) {
            free(child);
            *failed = 1;
            break;
        }
        if (S_ISDIR(info.st_mode)) {
            if (recursive) {
                total += fc_directory_size(child, 1, failed);
            }
        } else if (S_ISREG(info.st_mode)) {
            total += (int64_t)info.st_size;
        }
        free(child);
        if (*failed) break;
    }
    closedir(directory);
    return total;
}

static void fc_throw_io(const char* message) {
    KNI_ThrowNew(midpIOException, message);
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nEnsureDirectory) {
    char* path = fc_get_parameter_utf8(1);
    if (path != NULL) {
        (void)fc_mkdir_p(path);
    }
    free(path);
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nExists) {
    char* path = fc_get_parameter_utf8(1);
    struct stat info;
    jboolean result = path != NULL && lstat(path, &info) == 0
            ? KNI_TRUE : KNI_FALSE;
    free(path);
    KNI_ReturnBoolean(result);
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nIsDirectory) {
    char* path = fc_get_parameter_utf8(1);
    struct stat info;
    jboolean result = path != NULL && lstat(path, &info) == 0 &&
            S_ISDIR(info.st_mode) ? KNI_TRUE : KNI_FALSE;
    free(path);
    KNI_ReturnBoolean(result);
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nSize) {
    char* path = fc_get_parameter_utf8(1);
    struct stat info;
    jlong result = path != NULL && lstat(path, &info) == 0
            ? (jlong)info.st_size : 0;
    free(path);
    KNI_ReturnLong(result);
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nLastModified) {
    char* path = fc_get_parameter_utf8(1);
    struct stat info;
    jlong result = 0;
    if (path != NULL && lstat(path, &info) == 0) {
#if defined(__APPLE__)
        result = (jlong)info.st_mtimespec.tv_sec * 1000LL +
                (jlong)(info.st_mtimespec.tv_nsec / 1000000L);
#else
        result = (jlong)info.st_mtime * 1000LL;
#endif
    }
    free(path);
    KNI_ReturnLong(result);
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nCanRead) {
    char* path = fc_get_parameter_utf8(1);
    jboolean result = path != NULL && access(path, R_OK) == 0
            ? KNI_TRUE : KNI_FALSE;
    free(path);
    KNI_ReturnBoolean(result);
}

KNIEXPORT KNI_RETURNTYPE_BOOLEAN
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nCanWrite) {
    char* path = fc_get_parameter_utf8(1);
    jboolean result = path != NULL && access(path, W_OK) == 0
            ? KNI_TRUE : KNI_FALSE;
    free(path);
    KNI_ReturnBoolean(result);
}

static void fc_set_permission(int readable) {
    char* path = fc_get_parameter_utf8(1);
    jboolean enabled = KNI_GetParameterAsBoolean(2);
    struct stat info;
    int failed = 0;
    if (path == NULL || lstat(path, &info) != 0) {
        failed = 1;
    } else {
        mode_t mask = readable
                ? (S_IRUSR | S_IRGRP | S_IROTH)
                : (S_IWUSR | S_IWGRP | S_IWOTH);
        mode_t mode = enabled ? (info.st_mode | mask) : (info.st_mode & ~mask);
        if (chmod(path, mode) != 0) failed = 1;
    }
    free(path);
    if (failed) fc_throw_io("Unable to change file permissions");
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nSetReadable) {
    fc_set_permission(1);
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nSetWritable) {
    fc_set_permission(0);
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nCreateFile) {
    char* path = fc_get_parameter_utf8(1);
    int descriptor = path == NULL ? -1 :
            open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    int failed = descriptor < 0;
    if (descriptor >= 0) close(descriptor);
    free(path);
    if (failed) fc_throw_io("Unable to create file");
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nMkdir) {
    char* path = fc_get_parameter_utf8(1);
    int failed = path == NULL || mkdir(path, 0755) != 0;
    free(path);
    if (failed) fc_throw_io("Unable to create directory");
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nDelete) {
    char* path = fc_get_parameter_utf8(1);
    struct stat info;
    int failed = 0;
    if (path == NULL || lstat(path, &info) != 0) {
        failed = 1;
    } else if (S_ISDIR(info.st_mode)) {
        failed = rmdir(path) != 0;
    } else {
        failed = unlink(path) != 0;
    }
    free(path);
    if (failed) fc_throw_io("Unable to delete file or directory");
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nRename) {
    char* old_path = fc_get_parameter_utf8(1);
    char* new_path = fc_get_parameter_utf8(2);
    int failed = old_path == NULL || new_path == NULL ||
            rename(old_path, new_path) != 0;
    free(old_path);
    free(new_path);
    if (failed) fc_throw_io("Unable to rename file");
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nTruncate) {
    char* path = fc_get_parameter_utf8(1);
    jlong size = KNI_GetParameterAsLong(2);
    int failed = path == NULL || size < 0 ||
            truncate(path, (off_t)size) != 0;
    free(path);
    if (failed) fc_throw_io("Unable to truncate file");
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_OBJECT
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nReadAll) {
    uint8_t* bytes = NULL;
    size_t length = 0;
    int failed = 0;
    char* path = fc_get_parameter_utf8(1);
    int descriptor = path == NULL ? -1 : open(path, O_RDONLY);

    if (descriptor < 0) {
        failed = 1;
    } else {
        struct stat info;
        if (fstat(descriptor, &info) != 0 || info.st_size < 0 ||
                info.st_size > INT_MAX) {
            failed = 1;
        } else {
            length = (size_t)info.st_size;
            bytes = length == 0 ? NULL : (uint8_t*)malloc(length);
            if (length > 0 && bytes == NULL) {
                failed = 1;
            } else {
                size_t read_total = 0;
                while (read_total < length) {
                    ssize_t count = read(descriptor, bytes + read_total,
                            length - read_total);
                    if (count < 0 && errno == EINTR) continue;
                    if (count <= 0) {
                        failed = 1;
                        break;
                    }
                    read_total += (size_t)count;
                }
            }
        }
        close(descriptor);
    }
    free(path);

    KNI_StartHandles(1);
    KNI_DeclareHandle(result);

    if (!failed) {
        SNI_NewArray(SNI_BYTE_ARRAY, (jint)length, result);
        if (KNI_IsNullHandle(result)) {
            KNI_ThrowNew(midpOutOfMemoryError, NULL);
        } else if (length > 0) {
            KNI_SetRawArrayRegion(result, 0, (jint)length, (jbyte*)bytes);
        }
    } else {
        fc_throw_io("Unable to read file");
    }
    free(bytes);
    KNI_EndHandlesAndReturnObject(result);
}

KNIEXPORT KNI_RETURNTYPE_VOID
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nWriteAll) {
    int failed = 0;
    uint8_t* bytes = NULL;
    jint length = 0;
    char* path = fc_get_parameter_utf8(1);

    KNI_StartHandles(1);
    KNI_DeclareHandle(data);
    KNI_GetParameterAsObject(2, data);
    if (KNI_IsNullHandle(data)) {
        failed = 1;
    } else {
        length = KNI_GetArrayLength(data);
        if (length > 0) {
            bytes = (uint8_t*)malloc((size_t)length);
            if (bytes == NULL) {
                failed = 1;
            } else {
                KNI_GetRawArrayRegion(data, 0, length, (jbyte*)bytes);
            }
        }
    }

    if (!failed) {
        int descriptor = path == NULL ? -1 :
                open(path, O_WRONLY | O_TRUNC);
        if (descriptor < 0 ||
                (length > 0 && fc_write_all_fd(descriptor, bytes,
                        (size_t)length) != 0)) {
            failed = 1;
        }
        if (descriptor >= 0) {
            if (fsync(descriptor) != 0) failed = 1;
            close(descriptor);
        }
    }

    free(path);
    free(bytes);
    if (failed) fc_throw_io("Unable to write file");
    KNI_EndHandles();
    KNI_ReturnVoid();
}

KNIEXPORT KNI_RETURNTYPE_OBJECT
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nList) {
    uint8_t* buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;
    int failed = 0;
    char* path = fc_get_parameter_utf8(1);
    DIR* directory = path == NULL ? NULL : opendir(path);

    if (directory == NULL) {
        failed = 1;
    } else {
        struct dirent* entry;
        while ((entry = readdir(directory)) != NULL) {
                size_t name_length;
                int directory_entry = 0;
                char* child;
                size_t path_length;
                struct stat info;
                size_t required;
                if (strcmp(entry->d_name, ".") == 0 ||
                        strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                name_length = strlen(entry->d_name);
                path_length = strlen(path);
                child = (char*)malloc(path_length + name_length + 2U);
                if (child == NULL) {
                    failed = 1;
                    break;
                }
                memcpy(child, path, path_length);
                if (path_length > 0 && path[path_length - 1] != '/') {
                    child[path_length++] = '/';
                }
                memcpy(child + path_length, entry->d_name, name_length + 1U);
                if (lstat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
                    directory_entry = 1;
                }
                free(child);

                required = used + name_length + (directory_entry ? 1U : 0U) + 1U;
                if (required > capacity) {
                    size_t new_capacity = capacity == 0 ? 256U : capacity * 2U;
                    uint8_t* expanded;
                    while (new_capacity < required) new_capacity *= 2U;
                    expanded = (uint8_t*)realloc(buffer, new_capacity);
                    if (expanded == NULL) {
                        failed = 1;
                        break;
                    }
                    buffer = expanded;
                    capacity = new_capacity;
                }
                memcpy(buffer + used, entry->d_name, name_length);
                used += name_length;
                if (directory_entry) buffer[used++] = '/';
                buffer[used++] = '\0';
        }
        closedir(directory);
    }
    free(path);

    KNI_StartHandles(1);
    KNI_DeclareHandle(result);

    if (!failed) {
        SNI_NewArray(SNI_BYTE_ARRAY, (jint)used, result);
        if (KNI_IsNullHandle(result)) {
            KNI_ThrowNew(midpOutOfMemoryError, NULL);
        } else if (used > 0) {
            KNI_SetRawArrayRegion(result, 0, (jint)used, (jbyte*)buffer);
        }
    } else {
        fc_throw_io("Unable to list directory");
    }
    free(buffer);
    KNI_EndHandlesAndReturnObject(result);
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nDirectorySize) {
    jlong result = 0;
    int failed = 0;
    jboolean recursive = KNI_GetParameterAsBoolean(2);
    char* path = fc_get_parameter_utf8(1);
    if (path == NULL) {
        failed = 1;
    } else {
        result = (jlong)fc_directory_size(path, recursive != 0, &failed);
    }
    free(path);
    if (failed) fc_throw_io("Unable to calculate directory size");
    KNI_ReturnLong(result);
}

KNIEXPORT KNI_RETURNTYPE_LONG
KNIDECL(com_sun_midp_io_j2me_file_Protocol_nSpace) {
    jlong result = 0;
    jint kind = KNI_GetParameterAsInt(2);
    char* path = fc_get_parameter_utf8(1);
    struct statvfs info;
    if (path != NULL && statvfs(path, &info) == 0) {
        uint64_t total = (uint64_t)info.f_blocks *
                (uint64_t)info.f_frsize;
        uint64_t available = (uint64_t)info.f_bavail *
                (uint64_t)info.f_frsize;
        uint64_t value = kind == 0 ? total :
                (kind == 1 ? available : total - available);
        result = value > INT64_MAX ? (jlong)INT64_MAX : (jlong)value;
    }
    free(path);
    KNI_ReturnLong(result);
}
