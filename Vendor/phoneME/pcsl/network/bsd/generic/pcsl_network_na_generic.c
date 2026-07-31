/*
 *  
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
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
 * This file implements the notification adapter functions for a
 * generic BSD platform.
 */

#include <stdint.h>

#include <pcsl_network_generic.h>
#include <pcsl_network_na.h>
#include <pcsl_memory.h>

/** Note: We are guaranteed by Protocol Java code that there are no 2
 *  threads performing the same action on the socket, i.e. only one
 *  thread can be registered for reading, so well for writing.
 */

#define SD_RECV   0
#define SD_SEND   1

/** List of socket handles registered for read/write checks. */
static SocketHandle* rootSocketHandle = NULL;

/** List of all allocated handles, including handles not currently blocked. */
static SocketHandle* rootAllSocketHandle = NULL;

/** Next small integer token safe to store in MIDP's 32-bit Java fields. */
static int nextSocketHandleId = 1;

/** We need this method to unblock threads waiting for socket being destroyed. */
extern void NotifySocketStatusChanged(long handle, int waitingFor);

/** Resolve a public 32-bit token to its native SocketHandle structure. */
static SocketHandle* socketHandleFromToken(void* handle) {
    int id = (int)(intptr_t)handle;
    SocketHandle* current;

    if (id <= 0) {
        return NULL;
    }

    for (current = rootAllSocketHandle;
         current != NULL;
         current = current->all_next) {
        if (current->id == id) {
            return current;
        }
    }

    return NULL;
}

/** Search for handle instance in the registered list. */
static SocketHandle** getSocketHandleReference(SocketHandle* handle) {
    SocketHandle** ptr;
    for (ptr = &rootSocketHandle; *ptr != NULL; ptr = &((*ptr)->next)) {
        if (*ptr == handle) {
            return ptr;
        }
    }
    return NULL;
}

/** Add handle to the registered list. */
static void addSocketHandle(SocketHandle* handle) {
    SocketHandle** ptr = getSocketHandleReference(handle);
    if (ptr == NULL) {
        handle->next = rootSocketHandle;
        rootSocketHandle = handle;
    }
}

/** Remove handle from the registered list. */
static void removeSocketHandle(SocketHandle* handle) {
    SocketHandle** ptr = getSocketHandleReference(handle);
    if (ptr != NULL) {
        *ptr = handle->next;
        handle->next = NULL;
    }
}

/** Add handle to the allocated-handle registry. */
static void addAllSocketHandle(SocketHandle* handle) {
    handle->all_next = rootAllSocketHandle;
    rootAllSocketHandle = handle;
}

/** Remove handle from the allocated-handle registry. */
static void removeAllSocketHandle(SocketHandle* handle) {
    SocketHandle** ptr;

    for (ptr = &rootAllSocketHandle;
         *ptr != NULL;
         ptr = &((*ptr)->all_next)) {
        if (*ptr == handle) {
            *ptr = handle->all_next;
            handle->all_next = NULL;
            return;
        }
    }
}

/**
 * See pcsl_network_generic.h for definition.
 */
const SocketHandle* GetRegisteredSocketHandles() {
    return (const SocketHandle*)rootSocketHandle;
}

/**
 * See pcsl_network_na.h for definition.
 */
void* na_create(int fd) {
    SocketHandle* handle =
        (SocketHandle*)pcsl_mem_malloc(sizeof(SocketHandle));

    if (handle == NULL) {
        return NULL;
    }

    if (nextSocketHandleId <= 0) {
        nextSocketHandleId = 1;
    }

    handle->id = nextSocketHandleId++;
    handle->fd = fd;
    handle->check_flags = 0;
    handle->status = PCSL_NET_SUCCESS;
    handle->next = NULL;
    handle->all_next = NULL;
    addAllSocketHandle(handle);

    return (void*)(intptr_t)handle->id;
}

/**
 * See pcsl_network_na.h for definition.
 */
int na_get_fd(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);
    return sh == NULL ? -1 : sh->fd;
}

/**
 * See pcsl_network_na.h for definition.
 */
void na_register_for_read(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);
    if (sh != NULL) {
        sh->check_flags |= CHECK_READ;
        addSocketHandle(sh);
    }
}

/**
 * See pcsl_network_na.h for definition.
 */
void na_register_for_write(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);
    if (sh != NULL) {
        sh->check_flags |= CHECK_WRITE;
        addSocketHandle(sh);
    }
}

/**
 * See pcsl_network_na.h for definition.
 */
void na_unregister_for_read(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);
    if (sh != NULL) {
        sh->check_flags &= ~CHECK_READ;
        if (sh->check_flags == 0) {
            removeSocketHandle(sh);
        }
    }
}

/**
 * See pcsl_network_na.h for definition.
 */
void na_unregister_for_write(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);
    if (sh != NULL) {
        sh->check_flags &= ~CHECK_WRITE;
        if (sh->check_flags == 0) {
            removeSocketHandle(sh);
        }
    }
}

/**
 * See pcsl_network_na.h for definition.
 */
int na_get_status(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);
    return sh == NULL ? PCSL_NET_INTERRUPTED : sh->status;
}

/**
 * See pcsl_network_na.h for definition.
 */
void na_destroy(void* handle) {
    SocketHandle* sh = socketHandleFromToken(handle);

    if (sh == NULL) {
        return;
    }

    removeSocketHandle(sh);

    /* Still registered readers/writers should be unblocked. */
    if (sh->check_flags != 0) {
        sh->status = PCSL_NET_INTERRUPTED;
        if (sh->check_flags & CHECK_READ) {
            NotifySocketStatusChanged((long)sh->id, SD_RECV);
        }
        if (sh->check_flags & CHECK_WRITE) {
            NotifySocketStatusChanged((long)sh->id, SD_SEND);
        }
    }

    removeAllSocketHandle(sh);
    pcsl_mem_free(sh);
}
