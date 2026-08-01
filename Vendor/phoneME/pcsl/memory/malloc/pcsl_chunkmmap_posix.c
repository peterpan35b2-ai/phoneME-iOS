/*
 * Copyright 1990-2008 Sun Microsystems, Inc. All Rights Reserved.
 *
 * POSIX mmap-backed PCSL chunks for the embedded phoneME VM.
 *
 * The address range up to max_size is reserved with PROT_NONE, while only
 * initial_size is committed for read/write access. Growing and shrinking the
 * logical CLDC heap therefore changes resident memory without moving the heap.
 */

#include <pcsl_memory.h>
#include <pcsl_print.h>

#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

typedef struct _ChunkInfo {
    void* address;
    void* mapping_address;
    size_t current_size;
    size_t maximum_size;
    size_t mapping_size;
    struct _ChunkInfo* next;
} ChunkInfo;

static ChunkInfo* chunks = NULL;

static size_t system_page_size(void) {
    long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? (size_t)value : (size_t)4096;
}

static size_t align_up_size(size_t value, size_t alignment) {
    size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

static uintptr_t align_up_address(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1U;
    return (value + mask) & ~mask;
}

static ChunkInfo* find_chunk(void* address) {
    ChunkInfo* chunk;
    for (chunk = chunks; chunk != NULL; chunk = chunk->next) {
        if (chunk->address == address) {
            return chunk;
        }
    }
    return NULL;
}

static int set_access(void* address, size_t size, int protection) {
    if (size == 0) {
        return 0;
    }
    return mprotect(address, size, protection);
}

void* pcsl_mem_allocate_chunk(unsigned int initial_size,
                              unsigned int max_size,
                              unsigned int alignment) {
    const size_t page_size = system_page_size();
    size_t requested_alignment = alignment == 0 ? page_size : (size_t)alignment;
    size_t effective_alignment;
    size_t committed_size;
    size_t maximum_size;
    size_t mapping_size;
    void* mapping;
    void* address;
    ChunkInfo* info;

    if ((requested_alignment & (requested_alignment - 1U)) != 0) {
        return NULL;
    }

    effective_alignment = requested_alignment > page_size
        ? requested_alignment
        : page_size;
    committed_size = align_up_size((size_t)initial_size, page_size);
    maximum_size = align_up_size((size_t)max_size, page_size);
    if (maximum_size == 0 || committed_size > maximum_size) {
        return NULL;
    }

    if (maximum_size > SIZE_MAX - effective_alignment) {
        return NULL;
    }
    mapping_size = maximum_size + effective_alignment;
    mapping = mmap(
        NULL,
        mapping_size,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );
    if (mapping == MAP_FAILED) {
        return NULL;
    }

    address = (void*)align_up_address(
        (uintptr_t)mapping,
        effective_alignment
    );
    if (set_access(address, committed_size, PROT_READ | PROT_WRITE) != 0) {
        munmap(mapping, mapping_size);
        return NULL;
    }

    info = (ChunkInfo*)pcsl_mem_malloc(sizeof(ChunkInfo));
    if (info == NULL) {
        munmap(mapping, mapping_size);
        return NULL;
    }

    info->address = address;
    info->mapping_address = mapping;
    info->current_size = committed_size;
    info->maximum_size = maximum_size;
    info->mapping_size = mapping_size;
    info->next = chunks;
    chunks = info;
    return address;
}

unsigned int pcsl_mem_adjust_chunk(void* chunk_ptr, unsigned int new_size) {
    const size_t page_size = system_page_size();
    ChunkInfo* info = find_chunk(chunk_ptr);
    size_t requested_size;
    size_t previous_size;
    uint8_t* bytes;

    if (info == NULL) {
        return 0;
    }

    requested_size = align_up_size((size_t)new_size, page_size);
    if (requested_size > info->maximum_size) {
        return 0;
    }

    previous_size = info->current_size;
    if (requested_size == previous_size) {
        return (unsigned int)previous_size;
    }

    bytes = (uint8_t*)info->address;
    if (requested_size > previous_size) {
        if (set_access(
                bytes + previous_size,
                requested_size - previous_size,
                PROT_READ | PROT_WRITE) != 0) {
            return 0;
        }
    } else {
        size_t released_size = previous_size - requested_size;
#if defined(MADV_DONTNEED)
        (void)madvise(bytes + requested_size, released_size, MADV_DONTNEED);
#endif
        if (set_access(
                bytes + requested_size,
                released_size,
                PROT_NONE) != 0) {
            return 0;
        }
    }

    info->current_size = requested_size;
    return (unsigned int)previous_size;
}

void pcsl_mem_free_chunk(void* chunk_ptr) {
    ChunkInfo* info = chunks;
    ChunkInfo* previous = NULL;

    while (info != NULL) {
        if (info->address == chunk_ptr) {
            if (previous == NULL) {
                chunks = info->next;
            } else {
                previous->next = info->next;
            }
            munmap(info->mapping_address, info->mapping_size);
            pcsl_mem_free(info);
            return;
        }
        previous = info;
        info = info->next;
    }
}
