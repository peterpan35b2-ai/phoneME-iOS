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
 * Implementation of UI Component Registry.
 */

#include <kni.h>
#include <pthread.h>
#include <string.h>

#include "lfp_intern_registry.h"

#include <lfpport_displayable.h>
#include <lfpport_item.h>
#include <midpError.h>
#include <midpMalloc.h>
#include <midpString.h>
#if PHONEME_IOS_NATIVE
#include <midpServices.h>
#endif

/**
 * Global variable pointing to current visible screen. The legacy ports have
 * one process-wide display. The iOS MVM host runs several MIDlet isolates in
 * one process, so each isolate needs its own current-screen slot.
 */
#if PHONEME_IOS_NATIVE
#define MIDP_IOS_CURRENT_SCREEN_SLOTS 128

typedef struct {
    int isolateId;
    MidpFrame* screen;
} MidpIOSCurrentScreenSlot;

static MidpIOSCurrentScreenSlot
    MidpCurrentScreenSlots[MIDP_IOS_CURRENT_SCREEN_SLOTS];
static pthread_mutex_t MidpCurrentScreenMutex = PTHREAD_MUTEX_INITIALIZER;

static MidpIOSCurrentScreenSlot* currentScreenSlotLocked(
        int isolateId,
        int create) {
    MidpIOSCurrentScreenSlot* empty = NULL;
    int index;

    for (index = 0; index < MIDP_IOS_CURRENT_SCREEN_SLOTS; ++index) {
        MidpIOSCurrentScreenSlot* slot = &MidpCurrentScreenSlots[index];
        if (slot->isolateId == isolateId && isolateId != 0) {
            return slot;
        }
        if (empty == NULL && slot->isolateId == 0) {
            empty = slot;
        }
    }
    if (create && isolateId > 0 && empty != NULL) {
        empty->isolateId = isolateId;
        empty->screen = NULL;
        return empty;
    }
    return NULL;
}

static MidpFrame** currentScreenRefForIsolate(int isolateId) {
    static MidpFrame* nullScreen;
    MidpIOSCurrentScreenSlot* slot;

    pthread_mutex_lock(&MidpCurrentScreenMutex);
    slot = currentScreenSlotLocked(isolateId, 1);
    pthread_mutex_unlock(&MidpCurrentScreenMutex);
    return slot == NULL ? &nullScreen : &slot->screen;
}

static void releaseCurrentScreenSlot(int isolateId) {
    MidpIOSCurrentScreenSlot* slot;
    pthread_mutex_lock(&MidpCurrentScreenMutex);
    slot = currentScreenSlotLocked(isolateId, 0);
    if (slot != NULL) {
        slot->screen = NULL;
        slot->isolateId = 0;
    }
    pthread_mutex_unlock(&MidpCurrentScreenMutex);
}

MidpFrame** MidpCurrentScreenRef(void) {
    return currentScreenRefForIsolate(getCurrentIsolateId());
}

MidpFrame* MidpCurrentScreenForIsolate(int isolateId) {
    MidpIOSCurrentScreenSlot* slot;
    MidpFrame* result = NULL;
    pthread_mutex_lock(&MidpCurrentScreenMutex);
    slot = currentScreenSlotLocked(isolateId, 0);
    if (slot != NULL) {
        result = slot->screen;
    }
    pthread_mutex_unlock(&MidpCurrentScreenMutex);
    return result;
}
#else
MidpFrame* MidpCurrentScreen;
#endif

/**
 * Beginning of a linked list of all MidpFrame structures.
 */
static MidpFrame* MidpFirstScreen;

/**
 * Beginning of a linked list of all MidpItem structures without owner screen.
 */
static MidpItem* MidpFirstOrphanItem;

typedef struct _MidpNativeHandleEntry {
    jint id;
    MidpComponent* component;
#if PHONEME_IOS_NATIVE
    int isolateId;
#endif
    struct _MidpNativeHandleEntry* next;
} MidpNativeHandleEntry;

static MidpNativeHandleEntry* MidpFirstNativeHandle;
static jint MidpNextNativeHandle = 1;
#if PHONEME_IOS_NATIVE
static pthread_mutex_t MidpNativeHandleMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t MidpComponentListMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static jint registerComponent(MidpComponent* component) {
    MidpNativeHandleEntry* entry;

    if (component == NULL) {
        return 0;
    }

    entry = (MidpNativeHandleEntry*)midpMalloc(sizeof(*entry));
    if (entry == NULL) {
        return 0;
    }

#if PHONEME_IOS_NATIVE
    pthread_mutex_lock(&MidpNativeHandleMutex);
#endif
    if (MidpNextNativeHandle <= 0) {
        MidpNextNativeHandle = 1;
    }

    entry->id = MidpNextNativeHandle++;
    entry->component = component;
#if PHONEME_IOS_NATIVE
    entry->isolateId = getCurrentIsolateId();
#endif
    entry->next = MidpFirstNativeHandle;
    MidpFirstNativeHandle = entry;
#if PHONEME_IOS_NATIVE
    pthread_mutex_unlock(&MidpNativeHandleMutex);
#endif
    return entry->id;
}

static void unregisterComponent(const MidpComponent* component) {
    MidpNativeHandleEntry* previous = NULL;
    MidpNativeHandleEntry* entry;
#if PHONEME_IOS_NATIVE
    int isolateId = 0;
    int releaseScreenSlot = 0;
    pthread_mutex_lock(&MidpNativeHandleMutex);
#endif
    entry = MidpFirstNativeHandle;

    while (entry != NULL) {
        if (entry->component == component) {
#if PHONEME_IOS_NATIVE
            MidpNativeHandleEntry* remaining;
            isolateId = entry->isolateId;
#endif
            if (previous == NULL) {
                MidpFirstNativeHandle = entry->next;
            } else {
                previous->next = entry->next;
            }
            midpFree(entry);
#if PHONEME_IOS_NATIVE
            remaining = MidpFirstNativeHandle;
            while (remaining != NULL && remaining->isolateId != isolateId) {
                remaining = remaining->next;
            }
            releaseScreenSlot = remaining == NULL;
#endif
            break;
        }
        previous = entry;
        entry = entry->next;
    }
#if PHONEME_IOS_NATIVE
    pthread_mutex_unlock(&MidpNativeHandleMutex);
    if (releaseScreenSlot) {
        releaseCurrentScreenSlot(isolateId);
    }
#endif
}

jint MidpComponentToId(const MidpComponent* componentPtr) {
    MidpNativeHandleEntry* entry;
    jint result = 0;
#if PHONEME_IOS_NATIVE
    pthread_mutex_lock(&MidpNativeHandleMutex);
#endif
    entry = MidpFirstNativeHandle;
    while (entry != NULL) {
        if (entry->component == componentPtr) {
            result = entry->id;
            break;
        }
        entry = entry->next;
    }
#if PHONEME_IOS_NATIVE
    pthread_mutex_unlock(&MidpNativeHandleMutex);
#endif
    return result;
}

#if PHONEME_IOS_NATIVE
int MidpComponentIsolateId(const MidpComponent* componentPtr) {
    MidpNativeHandleEntry* entry;
    int result = 0;
    pthread_mutex_lock(&MidpNativeHandleMutex);
    entry = MidpFirstNativeHandle;
    while (entry != NULL) {
        if (entry->component == componentPtr) {
            result = entry->isolateId;
            break;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&MidpNativeHandleMutex);
    return result;
}
#endif

static MidpComponent* componentFromId(jint nativeId) {
    MidpNativeHandleEntry* entry;
    MidpComponent* result = NULL;

    if (nativeId <= 0) {
        return NULL;
    }

#if PHONEME_IOS_NATIVE
    pthread_mutex_lock(&MidpNativeHandleMutex);
#endif
    entry = MidpFirstNativeHandle;
    while (entry != NULL) {
        if (entry->id == nativeId) {
            result = entry->component;
            break;
        }
        entry = entry->next;
    }
#if PHONEME_IOS_NATIVE
    pthread_mutex_unlock(&MidpNativeHandleMutex);
#endif
    return result;
}

MidpDisplayable* MidpDisplayableFromId(jint nativeId) {
    MidpComponent* component = componentFromId(nativeId);
    if (component == NULL || component->type < MIDP_NULL_ALERT_TYPE) {
        return NULL;
    }
    return (MidpDisplayable*)component;
}

MidpItem* MidpItemFromId(jint nativeId) {
    MidpComponent* component = componentFromId(nativeId);
    if (component == NULL || component->type >= MIDP_NULL_ALERT_TYPE) {
        return NULL;
    }
    return (MidpItem*)component;
}

/**
 * Create a component resource structure for a Displayable.
 * The MidpComponent structure portion will be initialized as:
 * <ul>
 *	<li>type = type argument</li>
 *	<li>modelVersion = 0</li>
 *	<li>next = NULL</li>
 *	<li>child = NULL</li>
 * </ul>
 *
 * The rest of MidpDisplayable structure remains un-initialized. Platform
 * specific layer should populate these remaining data fields before use.
 * 
 *
 * @param type component type of new resource
 * @return pointer to the newly created MidpDisplayable structure,
 * 	   null if failed.
 */
MidpDisplayable* MidpNewDisplayable(MidpComponentType type) {
    MidpDisplayable* p = (MidpDisplayable *)midpMalloc(sizeof(MidpDisplayable));
    
    if (p) {
        p->frame.component.type = type;
        p->frame.component.modelVersion = 0;
        p->frame.component.next = NULL;
        p->frame.component.child = NULL;

        /*
        * The rest of the structure is not yet initialized.
        * If MidpDeleteDisplayable() is called on this pointer before
        * they are initialized, this flag will help preventing calling
        * hideAndDelete() unnecessarily
        */
        p->frame.widgetPtr = NULL;

        if (registerComponent(&p->frame.component) == 0) {
            midpFree(p);
            p = NULL;
        } else {
#if PHONEME_IOS_NATIVE
            pthread_mutex_lock(&MidpComponentListMutex);
#endif
            p->frame.component.next = (MidpComponent *)MidpFirstScreen;
            MidpFirstScreen = (MidpFrame *)p;
#if PHONEME_IOS_NATIVE
            pthread_mutex_unlock(&MidpComponentListMutex);
#endif
        }
    }

    return p;
}

/**
 * Delete all native resource of a Displayable.
 * Resources that will be freed are:
 * <ul>
 * 	<li>For each child Item: its platform dependent resource
 * 	<li>For each child Item: MidpItem structure
 * 	<li>Platform dependent resource
 * 	<li>MidpDisplayable structure
 * </ul>
 *
 * @param displayablePtr pointer to the MidpDisplayable structure
 */
void MidpDeleteDisplayable(MidpDisplayable *displayablePtr) {
    MidpComponent *p, *c;

    if (displayablePtr == NULL) {
        return;
    }

    /* If this displayable is current screen, clear only its owner's slot. */
#if PHONEME_IOS_NATIVE
    {
        int isolateId = MidpComponentIsolateId(
            &displayablePtr->frame.component);
        MidpFrame** currentScreen = currentScreenRefForIsolate(isolateId);
        if (*currentScreen == &displayablePtr->frame) {
            *currentScreen = NULL;
        }
    }
#else
    if (MidpCurrentScreen == &displayablePtr->frame) {
        MidpCurrentScreen = NULL;
    }
#endif

    /* First Delete all children */
    while (displayablePtr->frame.component.child != NULL) {
        MidpDeleteItem((MidpItem *)displayablePtr->frame.component.child);
    }

    /* Then detach this displayable from the process registry. */
#if PHONEME_IOS_NATIVE
    pthread_mutex_lock(&MidpComponentListMutex);
#endif
    if (MidpFirstScreen == (MidpFrame *)displayablePtr ||
        MidpFirstScreen == NULL) {
        MidpFirstScreen = (MidpFrame *)displayablePtr->frame.component.next;
    } else {
        p = (MidpComponent *)MidpFirstScreen;
        c = p->next;
        while (c != NULL) {
            if (c == (MidpComponent *)displayablePtr) {
                p->next = c->next;
                break;
            } else {
                p = c;
                c = c->next;
            }
        }
    }
#if PHONEME_IOS_NATIVE
    pthread_mutex_unlock(&MidpComponentListMutex);
#endif

    /* Next destroy platform dependent resource */
    if (displayablePtr->frame.widgetPtr) {
        displayablePtr->frame.hideAndDelete(&displayablePtr->frame, KNI_FALSE);
    }
    
    /* Last free the structure and its Java-visible native handle. */
    unregisterComponent(&displayablePtr->frame.component);
    midpFree(displayablePtr);
}

/**
 * Create a component resource structure for an Item.
 * The MidpComponent structure portion will be initialized as:
 * <ul>
 *	<li>type = type argument</li>
 *	<li>modelVersion = 0</li>
 *	<li>next = NULL</li>
 *	<li>child = NULL</li>
 * </ul>
 *
 * The rest of MidpItem structure remains un-initialized. Platform
 * specific layer should populate these remaining data fields before use.
 *
 * @param ownerPtr owner screen pointer, null if no owner
 * @param type component type of new resource
 * @return pointer to the newly created MidpItem structure,
 * 	   null if failed.
 */
MidpItem* MidpNewItem(MidpDisplayable *ownerPtr, MidpComponentType type) {
    MidpItem *p = (MidpItem *)midpMalloc(sizeof(MidpItem));

    if (p) {
	p->component.type = type;
	p->component.modelVersion = 0;
	p->component.child = NULL;

        p->component.next = NULL;
	p->ownerPtr = ownerPtr;

	/*
	 * The rest of the structure is not yet initialized.
	 * If MidpDeleteItem() is called on this pointer before
	 * they are initialized, this flag will help preventing calling
	 * destroy() unnecessarily
	 */
	p->widgetPtr = NULL;

        if (registerComponent(&p->component) == 0) {
            midpFree(p);
            p = NULL;
        } else if (ownerPtr == NULL) {
#if PHONEME_IOS_NATIVE
            pthread_mutex_lock(&MidpComponentListMutex);
#endif
            p->component.next = (MidpComponent *)MidpFirstOrphanItem;
            MidpFirstOrphanItem = p;
#if PHONEME_IOS_NATIVE
            pthread_mutex_unlock(&MidpComponentListMutex);
#endif
        } else {
            p->component.next = ownerPtr->frame.component.child;
            ownerPtr->frame.component.child = (MidpComponent *)p;
        }
    }

    return p;
}

/**
 * Delete all native resource of an Item.
 * Resources that will be freed are:
 * <ul>
 * 	<li>Platform dependent resource
 * 	<li>MidpItem structure
 * </ul>
 *
 * @param itemPtr pointer to the MidpItem structure
 */
void MidpDeleteItem(MidpItem *itemPtr) {
    MidpComponent *p, *c;
    
    if (itemPtr == NULL) {
        return;
    }

    /* First detach this item from its owner's children list */
    if (itemPtr->ownerPtr == NULL) {
#if PHONEME_IOS_NATIVE
        pthread_mutex_lock(&MidpComponentListMutex);
#endif
        p = (MidpComponent *)MidpFirstOrphanItem;
        if (p == (MidpComponent *)itemPtr || p == NULL) {
            MidpFirstOrphanItem = (MidpItem *)itemPtr->component.next;
            c = NULL;
        } else {
            c = p->next;
        }
    } else {
        p = itemPtr->ownerPtr->frame.component.child;
        if (p == (MidpComponent *)itemPtr || p == NULL) {
            itemPtr->ownerPtr->frame.component.child = itemPtr->component.next;
            c = NULL;
        } else {
            c = p->next;
        }
    }

    while (c != NULL) {
        if (c == (MidpComponent *)itemPtr) {
            p->next = c->next;
            break;
        } else {
            p = c;
            c = c->next;
        }
    }
#if PHONEME_IOS_NATIVE
    if (itemPtr->ownerPtr == NULL) {
        pthread_mutex_unlock(&MidpComponentListMutex);
    }
#endif

    /* Then free all platform dependent resource */
    if (itemPtr->widgetPtr) {
        itemPtr->destroy(itemPtr);
    }

    /* Last free the MidpItem structure and its Java-visible handle. */
    unregisterComponent(&itemPtr->component);
    midpFree(itemPtr);
}

/**
 * Map a platform widget pointer to its Item's structure pointer.
 * @param owner owner screen's structure pointer, null to search for orphan item
 * @param itemWidgetPtr platform widget pointer to be used as key
 * @return MidpItem* of its Item, null if not found.
 */
MidpItem* MidpFindItem(MidpDisplayable *ownerPtr,
		       PlatformItemWidgetPtr itemWidgetPtr) {
    MidpComponent *c;

    if (itemWidgetPtr == NULL) {
	return NULL;
    }

#if PHONEME_IOS_NATIVE
    if (ownerPtr == NULL) {
        pthread_mutex_lock(&MidpComponentListMutex);
    }
#endif
    c = (ownerPtr == NULL) ? (MidpComponent *)MidpFirstOrphanItem
			   : ownerPtr->frame.component.child;

    while (c != NULL) {
	if (((MidpItem *)c)->widgetPtr == itemWidgetPtr) {
            MidpItem* result = (MidpItem *)c;
#if PHONEME_IOS_NATIVE
            if (ownerPtr == NULL) {
                pthread_mutex_unlock(&MidpComponentListMutex);
            }
#endif
	    return result;
	} else {
	    c = c->next;
	}
    }
#if PHONEME_IOS_NATIVE
    if (ownerPtr == NULL) {
        pthread_mutex_unlock(&MidpComponentListMutex);
    }
#endif

    return NULL; /* no match */
}

/**
 * Delete all MIDP components when VM is exiting.
 */
void MidpDeleteAllComponents() {
    for (;;) {
        MidpItem* orphan;
#if PHONEME_IOS_NATIVE
        pthread_mutex_lock(&MidpComponentListMutex);
#endif
        orphan = MidpFirstOrphanItem;
#if PHONEME_IOS_NATIVE
        pthread_mutex_unlock(&MidpComponentListMutex);
#endif
        if (orphan == NULL) {
            break;
        }
        MidpDeleteItem(orphan);
    }
#if PHONEME_IOS_NATIVE
    pthread_mutex_lock(&MidpCurrentScreenMutex);
    memset(MidpCurrentScreenSlots, 0, sizeof(MidpCurrentScreenSlots));
    pthread_mutex_unlock(&MidpCurrentScreenMutex);
#endif
}
