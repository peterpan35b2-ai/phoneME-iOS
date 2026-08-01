/*
 *
 *
 * Copyright  1990-2008 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

/**
 * @file
 *
 * @ingroup AMS
 *
 * This is reference implementation of the Suite Storage Listeners API.
 * It allows to register/unregister callbacks that will be notified
 * when the certain operation on a midlet suite is performed.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pcsl_memory.h>
#include <midpInit.h>
#include <suitestore_intern.h>
#include <suitestore_icon_cache.h>

/**
 * Maximal number of free entries allowed in the file containing the cache until
 * it is compacted (i.e. rewritten without free entries).
 */
#define MAX_FREE_ENTRIES 10

/**
 * A number of additional free entries that will be allocated in
 * g_pIconCache array to avoid memory reallocations when a new
 * icon is added into the cache.
 */
#define RESERVED_CACHE_ENTRIES_NUM 10

/**
 * An array of IconCache structures representing
 * the icon cache in the memory.
 */
static IconCache* g_pIconCache = NULL;

/** A flag indicating if the icon cache is loaded from file into memory. */
static int g_iconsLoaded = 0;

/** Number of currently occupied entries in the g_pIconCache array. */
static int g_numberOfIcons = 0;

/** Number of entries currently allocated in the g_pIconCache array. */
static int g_numberOfEntries = 0;

#define ADJUST_POS_IN_BUF(pos, bufferLen, n) \
    pos += n; \
    bufferLen -= n;

/**
 * Search for a structure containing the cached suite's icon(s)
 * by the suite's ID.
 *
 * @param suiteId unique ID of the midlet suite
 *
 * @return pointer to the IconCache structure containing
 * the cached suite's icon(s) or NULL if the it was not found
 */
static IconCache*
get_icon_cache_for_suite(SuiteIdType suiteId) {
    IconCache* pData;
    int i;

    if (!g_iconsLoaded) {
        MIDPError status = midp_load_suites_icons();
        if (status != ALL_OK) {
            return NULL;
        }
    }

    pData = g_pIconCache;

    /* search the given suite Id in the array of cache entries */
    for (i = 0; i < g_numberOfIcons; i++) {
        if (g_pIconCache[i].suiteId == suiteId) {
            return &g_pIconCache[i];
        }
    }

    return NULL;
}

/**
 * Initializes the icons cache.
 *
 * @return status code: ALL_OK if no errors,
 *         OUT_OF_MEMORY if malloc failed
 *         IO_ERROR if an IO_ERROR
 */
MIDPError midp_load_suites_icons() {
    int i;
    int iconIndex = 0;
    long bufferLen, pos, alignedOffset;
    char* buffer = NULL;
    char* pszError = NULL;
    unsigned char* pIconBytes;
    jchar* iconNameChars = NULL;
    size_t entryLength;
    size_t iconNameBytes;
    pcsl_string_status rc;
    pcsl_string iconsCacheFile;
    IconCache *pIconsData, *pData;
    IconCacheHeader cacheFileHeader;
    IconCacheEntry nextCacheEntry;
    int numOfIcons = 0, numOfEntries = 0;
    MIDPError status;

    if (g_iconsLoaded) {
        return ALL_OK;
    }

    if (midpInit(LIST_LEVEL) != 0) {
        return OUT_OF_MEMORY;
    }

    /* get a full path to the _suites.dat */
    rc = pcsl_string_cat(storage_get_root(INTERNAL_STORAGE_ID),
                         &ICON_CACHE_FILENAME, &iconsCacheFile);
    if (rc != PCSL_STRING_OK) {
        return OUT_OF_MEMORY;
    }

    /* read the file */
    status = read_file(&pszError, &iconsCacheFile, &buffer, &bufferLen);
    pcsl_string_free(&iconsCacheFile);
    storageFreeError(pszError);

    if (status == NOT_FOUND || (status == ALL_OK && bufferLen == 0)) {
        /* _icons.dat is absent or empty, it's a normal situation */
        g_pIconCache  = NULL;
        g_iconsLoaded = 1;
        return ALL_OK;
    }

    if (status == ALL_OK && bufferLen < (long)sizeof (IconCacheHeader)) {
        status = IO_ERROR; /* _icons.dat is corrupted */
    }
    if (status != ALL_OK) {
        pcsl_mem_free(buffer);
        return status;
    }

    /* parse its contents */
    pos = 0;

    /* checking the file header */
    memcpy(&cacheFileHeader, buffer, sizeof (cacheFileHeader));
    if (cacheFileHeader.magic   != ICON_CACHE_MAGIC ||
        cacheFileHeader.version != ICON_CACHE_VERSION ||
        cacheFileHeader.numberOfEntries < 0 ||
        cacheFileHeader.numberOfFreeEntries < 0 ||
        cacheFileHeader.numberOfFreeEntries >
            cacheFileHeader.numberOfEntries) {
        pcsl_mem_free(buffer);
        return IO_ERROR;
    }

    numOfIcons = cacheFileHeader.numberOfEntries -
                 cacheFileHeader.numberOfFreeEntries;
    numOfEntries = numOfIcons + RESERVED_CACHE_ENTRIES_NUM;

    ADJUST_POS_IN_BUF(pos, bufferLen, sizeof(IconCacheHeader));

    if ((size_t)numOfEntries > SIZE_MAX / sizeof (IconCache)) {
        pcsl_mem_free(buffer);
        return OUT_OF_MEMORY;
    }
    pIconsData = (IconCache*)pcsl_mem_malloc(
        (size_t)numOfEntries * sizeof (IconCache));
    if (pIconsData == NULL) {
        pcsl_mem_free(buffer);
        return OUT_OF_MEMORY;
    }

    /* iterating through the cache entries */
    memset(pIconsData, 0, (size_t)numOfEntries * sizeof (IconCache));

    for (i = 0; i < cacheFileHeader.numberOfEntries; i++) {
        if ((long)sizeof (IconCacheEntry) > bufferLen) {
            status = IO_ERROR;
            break;
        }

        memcpy(&nextCacheEntry, &buffer[pos], sizeof (nextCacheEntry));
        if (nextCacheEntry.nameLength < 0 ||
                (nextCacheEntry.nameLength & 1) != 0 ||
                nextCacheEntry.imageDataLength < 0) {
            status = IO_ERROR;
            break;
        }

        entryLength = sizeof (IconCacheEntry) +
            (size_t)nextCacheEntry.nameLength +
            (size_t)nextCacheEntry.imageDataLength;
        if (entryLength > (size_t)LONG_MAX ||
                entryLength > (size_t)bufferLen) {
            status = IO_ERROR;
            break;
        }
        alignedOffset = SUITESTORE_ALIGN_4((long)entryLength);
        if (alignedOffset < 0 || alignedOffset > bufferLen) {
            status = IO_ERROR;
            break;
        }

        if (nextCacheEntry.isFree) {
            ADJUST_POS_IN_BUF(pos, bufferLen, alignedOffset);
            continue;
        }
        if (iconIndex >= numOfIcons) {
            status = IO_ERROR;
            break;
        }

        pData = &pIconsData[iconIndex];
        pData->suiteId = nextCacheEntry.suiteId;
        pData->numberOfCachedImages = 1;

        pData->pInfo[0].isFree = 0;
        pData->pInfo[0].entryOffsetInFile = (uint64_t)pos;
        pData->pInfo[0].imageDataLength = nextCacheEntry.imageDataLength;
        if (nextCacheEntry.imageDataLength > 0) {
            pData->pInfo[0].pImageData =
                (unsigned char*)pcsl_mem_malloc(
                    (size_t)nextCacheEntry.imageDataLength);
            if (pData->pInfo[0].pImageData == NULL) {
                status = OUT_OF_MEMORY;
                break;
            }
        }

        iconNameBytes = (size_t)nextCacheEntry.nameLength;
        pIconBytes = (unsigned char*)&buffer[pos] + sizeof (IconCacheEntry) +
            iconNameBytes;
        if (nextCacheEntry.imageDataLength > 0) {
            memcpy(pData->pInfo[0].pImageData, pIconBytes,
                   (size_t)nextCacheEntry.imageDataLength);
        }

        if (iconNameBytes == 0U) {
            pData->pInfo[0].imageName = PCSL_STRING_EMPTY;
        } else {
            iconNameChars = (jchar*)pcsl_mem_malloc(iconNameBytes);
            if (iconNameChars == NULL) {
                status = OUT_OF_MEMORY;
                break;
            }
            memcpy(iconNameChars,
                   &buffer[pos] + sizeof (IconCacheEntry),
                   iconNameBytes);
            rc = pcsl_string_convert_from_utf16(
                iconNameChars,
                (jsize)(iconNameBytes / sizeof (jchar)),
                &pData->pInfo[0].imageName);
            pcsl_mem_free(iconNameChars);
            iconNameChars = NULL;
            if (rc != PCSL_STRING_OK) {
                status = OUT_OF_MEMORY;
                break;
            }
        }

        iconIndex++;
        ADJUST_POS_IN_BUF(pos, bufferLen, alignedOffset);
    } /* end for (numOfEntries) */

    if (status == ALL_OK && iconIndex != numOfIcons) {
        status = IO_ERROR;
    }

    if (status == ALL_OK) {
        g_numberOfIcons = numOfIcons;
        g_numberOfEntries = numOfEntries;
        g_pIconCache = pIconsData;
        g_iconsLoaded = 1;

        if (cacheFileHeader.numberOfFreeEntries > MAX_FREE_ENTRIES) {
            (void)midp_compact_icons();
        }
    } else {
        for (i = 0; i <= iconIndex && i < numOfEntries; i++) {
            pcsl_mem_free(pIconsData[i].pInfo[0].pImageData);
            pcsl_string_free(&pIconsData[i].pInfo[0].imageName);
        }
        pcsl_mem_free(pIconsData);
    }

    pcsl_mem_free(buffer);

    return status;
}

/**
 * Writes the cache contents into the icon cache file.
 *
 * @param pIconCache currently unused; may be useful to optimize
 *                   the writing of the file
 *
 * @return status code: ALL_OK if no errors,
 *         OUT_OF_MEMORY if malloc failed
 *         IO_ERROR if an IO_ERROR
 */
static MIDPError store_suites_icons(const IconCache* pIconCache) {
    MIDPError status = ALL_OK;
    int i;
    int activeEntries = 0;
    int convertedLen;
    long bufferLen;
    long pos;
    size_t totalSize;
    size_t entrySize;
    size_t nameBytes;
    jsize nameLength;
    char* buffer = NULL;
    char *pszError = NULL;
    jchar* nameBuffer = NULL;
    pcsl_string_status rc;
    pcsl_string iconsCacheFile;
    const IconCache *pData;
    IconCacheHeader cacheFileHeader;
    IconCacheEntry cacheEntry;

    if (pIconCache == NULL) {
        return BAD_PARAMS;
    }

    rc = pcsl_string_cat(storage_get_root(INTERNAL_STORAGE_ID),
                         &ICON_CACHE_FILENAME, &iconsCacheFile);
    if (rc != PCSL_STRING_OK) {
        return OUT_OF_MEMORY;
    }

    if (!g_numberOfIcons) {
        status = write_file(&pszError, &iconsCacheFile, buffer, 0);
        storageFreeError(pszError);
        pcsl_string_free(&iconsCacheFile);
        return status;
    }

    totalSize = sizeof (IconCacheHeader);
    for (i = 0; i < g_numberOfIcons; i++) {
        pData = &pIconCache[i];
        if (pData->pInfo[0].isFree) {
            continue;
        }
        if (pData->pInfo[0].imageDataLength < 0) {
            status = IO_ERROR;
            break;
        }

        nameLength = pcsl_string_utf16_length(&pData->pInfo[0].imageName);
        if (nameLength < 0 ||
                (size_t)nameLength > (size_t)INT_MAX / sizeof (jchar)) {
            status = IO_ERROR;
            break;
        }
        nameBytes = (size_t)nameLength * sizeof (jchar);
        entrySize = sizeof (IconCacheEntry) + nameBytes +
                    (size_t)pData->pInfo[0].imageDataLength;
        if (entrySize > SIZE_MAX - 3U) {
            status = OUT_OF_MEMORY;
            break;
        }
        entrySize = (entrySize + 3U) & ~(size_t)3U;
        if (totalSize > SIZE_MAX - entrySize) {
            status = OUT_OF_MEMORY;
            break;
        }
        totalSize += entrySize;
        activeEntries++;
    }

    if (status != ALL_OK || totalSize > (size_t)LONG_MAX) {
        pcsl_string_free(&iconsCacheFile);
        return status == ALL_OK ? OUT_OF_MEMORY : status;
    }

    bufferLen = (long)totalSize;
    buffer = (char*)pcsl_mem_malloc(totalSize);
    if (buffer == NULL) {
        pcsl_string_free(&iconsCacheFile);
        return OUT_OF_MEMORY;
    }
    memset(buffer, 0, totalSize);

    cacheFileHeader.magic = ICON_CACHE_MAGIC;
    cacheFileHeader.version = ICON_CACHE_VERSION;
    cacheFileHeader.numberOfEntries = activeEntries;
    cacheFileHeader.numberOfFreeEntries = 0;
    memcpy(buffer, &cacheFileHeader, sizeof (cacheFileHeader));
    pos = (long)sizeof (cacheFileHeader);

    for (i = 0; i < g_numberOfIcons; i++) {
        pData = &pIconCache[i];
        if (pData->pInfo[0].isFree) {
            continue;
        }

        nameLength = pcsl_string_utf16_length(&pData->pInfo[0].imageName);
        nameBytes = (size_t)nameLength * sizeof (jchar);
        if (nameBytes > 0U) {
            nameBuffer = (jchar*)pcsl_mem_malloc(nameBytes);
            if (nameBuffer == NULL) {
                status = OUT_OF_MEMORY;
                break;
            }
            rc = pcsl_string_convert_to_utf16(&pData->pInfo[0].imageName,
                                               nameBuffer, nameLength,
                                               &convertedLen);
            if (rc != PCSL_STRING_OK || convertedLen != nameLength) {
                pcsl_mem_free(nameBuffer);
                nameBuffer = NULL;
                status = IO_ERROR;
                break;
            }
        }

        cacheEntry.isFree = 0;
        cacheEntry.suiteId = pData->suiteId;
        cacheEntry.imageDataLength = pData->pInfo[0].imageDataLength;
        cacheEntry.nameLength = (jint)nameBytes;
        entrySize = sizeof (cacheEntry) + nameBytes +
                    (size_t)cacheEntry.imageDataLength;
        entrySize = (entrySize + 3U) & ~(size_t)3U;
        if ((size_t)pos > totalSize || entrySize > totalSize - (size_t)pos) {
            pcsl_mem_free(nameBuffer);
            nameBuffer = NULL;
            status = IO_ERROR;
            break;
        }

        memcpy(&buffer[pos], &cacheEntry, sizeof (cacheEntry));
        if (nameBytes > 0U) {
            memcpy(&buffer[pos] + sizeof (cacheEntry), nameBuffer, nameBytes);
            pcsl_mem_free(nameBuffer);
            nameBuffer = NULL;
        }
        if (cacheEntry.imageDataLength > 0) {
            memcpy(&buffer[pos] + sizeof (cacheEntry) + nameBytes,
                   pData->pInfo[0].pImageData,
                   (size_t)cacheEntry.imageDataLength);
        }
        pos += (long)entrySize;
    }

    if (status == ALL_OK) {
        status = write_file(&pszError, &iconsCacheFile, buffer, pos);
        storageFreeError(pszError);
    }

    pcsl_mem_free(nameBuffer);
    pcsl_mem_free(buffer);
    pcsl_string_free(&iconsCacheFile);

    return status;
}

#undef ADJUST_POS_IN_BUF

/**
 * Frees the memory allocated for icons cache.
 */
void midp_free_suites_icons() {
    int i, n;

    if (g_iconsLoaded && g_numberOfEntries > 0 && g_pIconCache != NULL) {
        /* for each suite whose icons are in cache */
        for (i = 0; i < g_numberOfIcons; i++) {
            /* for each cached icon of this suite (currently 1) */
            for (n = 0; n < g_pIconCache[i].numberOfCachedImages; n++) {
                CachedImageInfo* pEntry = &g_pIconCache[i].pInfo[n];

                if (pEntry->isFree) {
                    continue;
                }

                if (pEntry->pImageData != NULL) {
                    pcsl_mem_free(pEntry->pImageData);
                }

                pcsl_string_free(&pEntry->imageName);
            }
        }

        pcsl_mem_free(g_pIconCache);
    }

    g_pIconCache      = NULL;
    g_iconsLoaded     = 0;
    g_numberOfIcons   = 0;
    g_numberOfEntries = 0;
}

/**
 * Retrieves image bytes of the icon with the given name belonging
 * to the given suite.
 *
 * @param suiteId ID of the suite which the icon belongs to
 * @param pIconName the icon's name
 * @param ppImageData   [out] pointer to a place where the pointer to the
 *                            area inside the cache where the icon's
 *                            bytes are located will be saved
 * @param pImageDataLen [out] pointer to a place where the length of the
 *                            retrieved data will be saved
 *
 * @return status code (ALL_OK if successful)
 */
MIDPError
midp_get_suite_icon(SuiteIdType suiteId, const pcsl_string* pIconName,
                    unsigned char** ppImageData, int* pImageDataLen) {
    IconCache* pIconCache;
    int i;

    if (pIconName == NULL || ppImageData == NULL || pImageDataLen == NULL ||
            suiteId == UNUSED_SUITE_ID) {
        return BAD_PARAMS;
    }

    pIconCache = get_icon_cache_for_suite(suiteId);
    if (pIconCache == NULL) {
        return NOT_FOUND;
    }

    /* iterate through the icons cache */
    for (i = 0; i < pIconCache->numberOfCachedImages; i++) {
        if (pcsl_string_equals(pIconName, &(pIconCache->pInfo[i].imageName))) {
            *pImageDataLen = pIconCache->pInfo[i].imageDataLength;
            *ppImageData = pIconCache->pInfo[i].pImageData;
            return ALL_OK;
        }
    }

    /* icon not found */
    return NOT_FOUND;
}

/**
 * Adds a new icon with the given name belonging to the given suite
 * to the cache.
 * If an icon with the same name belonging to the same suite exists,
 * it will be overwritten.
 *
 * @param suiteId ID of the suite which the icon belongs to
 * @param pIconName the icon's name
 *        Note: this function (not the caller) is responsible for allocating
 *              the memory needed to store a copy of the icon's name.
 * @param pImageData pointer to the array containing the icon's bytes
 *        Note: after calling this function the control over the memory
 *              occupied by pImageData is given to midp_add_suite_icon().
 * @param imageDataLen size of data given in pImageData
 *
 * @return status code (ALL_OK if successful)
 */
MIDPError
midp_add_suite_icon(SuiteIdType suiteId, const pcsl_string* pIconName,
                    unsigned char* pImageData, int imageDataLen) {
    MIDPError status = ALL_OK;
    pcsl_string_status res;

    if (pIconName == NULL || pImageData == NULL || imageDataLen == 0 ||
            suiteId == UNUSED_SUITE_ID) {
        return BAD_PARAMS;
    }

    do {
        IconCache* pIconCache = get_icon_cache_for_suite(suiteId);

        if (pIconCache == NULL) {
            /* try to find a free entry */
            int n;
            for (n = 0; n < g_numberOfIcons; n++) {
                if (g_pIconCache[n].pInfo[0].isFree) {
                    pIconCache = &g_pIconCache[n];
                    break;
                }
            }

            if (pIconCache == NULL) {
                /* there are no free entries in the cache */
                if (g_numberOfEntries <= g_numberOfIcons + 1) {
                    /* the cache is too small - add more entries */
                    int numOfEntries = g_numberOfIcons + RESERVED_CACHE_ENTRIES_NUM;
                    IconCache *pIconsData = (IconCache*) pcsl_mem_malloc(
                        sizeof(IconCache) * numOfEntries);
                    if (pIconsData == NULL) {
                        status = OUT_OF_MEMORY;
                        break;
                    }

                    if (g_numberOfEntries > 0) {
                        memcpy((char*)pIconsData, (char*)g_pIconCache,
                            g_numberOfEntries * sizeof(IconCache));
                        pcsl_mem_free(g_pIconCache);
                    }

                    g_pIconCache = pIconsData;
                }

                pIconCache = &g_pIconCache[g_numberOfIcons];
                g_numberOfIcons++;
            }
        } else {
            /* cache entry for this suite already exists, free it first */
            pcsl_string_free(&pIconCache->pInfo[0].imageName);
            pcsl_mem_free(pIconCache->pInfo[0].pImageData);
        }

        /* until the entry is filled, consider it as free */
        pIconCache->pInfo[0].isFree = 1;

        pIconCache->suiteId = suiteId;
        pIconCache->numberOfCachedImages = 1;

        res = pcsl_string_dup(pIconName, &pIconCache->pInfo[0].imageName);
        if (res != PCSL_STRING_OK) {
            status = OUT_OF_MEMORY;
            break;
        }
        pIconCache->pInfo[0].isFree = 0;
        pIconCache->pInfo[0].entryOffsetInFile = (unsigned long)-1;
        pIconCache->pInfo[0].imageDataLength = imageDataLen;
        pIconCache->pInfo[0].pImageData = pImageData;

        status = store_suites_icons(pIconCache);
    } while (0);

    return status;
}

/**
 * Removes all icons belonging to the given suite from the cache.
 *
 * @param suiteId ID of the suite whose icons should be removed
 *
 * @return status code (ALL_OK if successful)
 */
MIDPError midp_remove_suite_icons(SuiteIdType suiteId) {
    IconCache* pIconCache = get_icon_cache_for_suite(suiteId);

    if (pIconCache != NULL) {
        pcsl_string_free(&pIconCache->pInfo[0].imageName);
        pcsl_mem_free(pIconCache->pInfo[0].pImageData);
        pIconCache->pInfo[0].pImageData = NULL;
        pIconCache->pInfo[0].isFree = 1;
    }

    return store_suites_icons(pIconCache);
}

/**
 * Compacts the storage with the cached icons.
 *
 * IMPL_NOTE: currently does nothing because the whole file
 *            containing the cached icons is overwritten, so
 *            there are no free entries in it.
 *
 * @return status code (ALL_OK if successful)
 */
MIDPError midp_compact_icons() {
    return ALL_OK;
}
