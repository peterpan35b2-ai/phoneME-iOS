#include "lfpport_ios_native.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kni.h>
#include <midpError.h>
#include <midpMalloc.h>
#include <midpUtilKni.h>
#include <pcsl_string.h>

#include <fbapp_export.h>
#include <lfpport_alert.h>
#include <lfpport_canvas.h>
#include <lfpport_choicegroup.h>
#include <lfpport_command.h>
#include <lfpport_customitem.h>
#include <lfpport_datefield.h>
#include <lfpport_export.h>
#include <lfpport_font.h>
#include <lfpport_form.h>
#include <lfpport_gauge.h>
#include <lfpport_imageitem.h>
#include <lfpport_item.h>
#include <lfpport_stringitem.h>
#include <lfpport_textfield.h>

#include "lfp_intern_registry.h"
#include "lfp_registry.h"

#define IOS_EVENT_QUEUE_INITIAL_CAPACITY 256
#define IOS_NODE_TEXT_CAPACITY 2048
#define IOS_LABEL_CAPACITY 512
#define IOS_EVENT_SCREEN_KIND_METADATA -1006
#define IOS_EVENT_SCREEN_MODE_METADATA -1007
#define IOS_CHOICE_IMAGE_INDEX_BITS 10
#define IOS_CHOICE_IMAGE_INDEX_MASK ((1 << IOS_CHOICE_IMAGE_INDEX_BITS) - 1)

typedef struct {
    char text[IOS_LABEL_CAPACITY];
    jboolean selected;
    int fontFace;
    int fontStyle;
    int fontSize;
    uint8_t* imageRGBA;
    int imageWidth;
    int imageHeight;
    uint64_t imageGeneration;
} IOSChoice;

typedef struct _IOSNode {
    int32_t id;
    int32_t parentId;
    MidpComponentType type;
    MidpItem* item;
    MidpDisplayable* displayable;
    int visible;
    int x;
    int y;
    int width;
    int height;
    int contentWidth;
    int contentHeight;
    int scrollPosition;
    int screenKind;
    int layout;
    int maxSize;
    int constraints;
    int caretPosition;
    int value;
    int maxValue;
    int interactive;
    int inputMode;
    int appearanceMode;
    int fitPolicy;
    int fontFace;
    int fontStyle;
    int fontSize;
    long dateValue;
    int textChanged;
    char label[IOS_LABEL_CAPACITY];
    char text[IOS_NODE_TEXT_CAPACITY];
    char detail[IOS_NODE_TEXT_CAPACITY];
    IOSChoice* choices;
    int choiceCount;
    uint8_t* imageRGBA;
    int imageWidth;
    int imageHeight;
    uint64_t imageGeneration;
    struct _IOSNode* next;
} IOSNode;

typedef struct {
    int face;
    int style;
    int size;
} IOSFont;

static pthread_mutex_t eventMutex = PTHREAD_MUTEX_INITIALIZER;
static PhoneMELCDUIEvent* eventQueue;
static int eventCapacity;
static int eventReadIndex;
static int eventWriteIndex;
static int eventCount;
static uint64_t eventGeneration;

static pthread_mutex_t nodeMutex = PTHREAD_MUTEX_INITIALIZER;
static IOSNode* firstNode;
static int formScrollPosition;
static int fullScreenMode;
static int32_t focusedItemId;

#define IOS_FONT_REGISTRY_CAPACITY 96
static pthread_mutex_t fontMutex = PTHREAD_MUTEX_INITIALIZER;
static IOSFont fontRegistry[IOS_FONT_REGISTRY_CAPACITY];
static int fontRegistryCount;

static void emit_choice(IOSNode* node, int index);

static int32_t choice_image_key(int32_t componentId, int index) {
    int64_t raw;

    if (componentId <= 0 ||
            componentId > (INT32_MAX >> IOS_CHOICE_IMAGE_INDEX_BITS) ||
            index < 0 || index >= IOS_CHOICE_IMAGE_INDEX_MASK) {
        return 0;
    }
    raw = ((int64_t)componentId << IOS_CHOICE_IMAGE_INDEX_BITS) |
        (int64_t)(index + 1);
    return (int32_t)-raw;
}

static int decode_choice_image_key(
        int32_t imageKey,
        int32_t* componentId,
        int* index) {
    int64_t raw;
    int encodedIndex;

    if (imageKey >= 0 || imageKey == INT32_MIN) {
        return 0;
    }
    raw = -(int64_t)imageKey;
    encodedIndex = (int)(raw & IOS_CHOICE_IMAGE_INDEX_MASK);
    if (encodedIndex == 0) {
        return 0;
    }
    if (componentId != NULL) {
        *componentId = (int32_t)(raw >> IOS_CHOICE_IMAGE_INDEX_BITS);
    }
    if (index != NULL) {
        *index = encodedIndex - 1;
    }
    return 1;
}

static void free_choice_image(IOSChoice* choice) {
    if (choice != NULL && choice->imageRGBA != NULL) {
        midpFree(choice->imageRGBA);
        choice->imageRGBA = NULL;
    }
    if (choice != NULL) {
        choice->imageWidth = 0;
        choice->imageHeight = 0;
        choice->imageGeneration++;
    }
}

static int64_t packed_font(int face, int style, int size) {
    return ((int64_t)(face & 0xffff)) |
        ((int64_t)(style & 0xffff) << 16) |
        ((int64_t)(size & 0xffff) << 32);
}

static int64_t packed_choice_font(const IOSChoice* choice) {
    if (choice == NULL) {
        return 0;
    }
    return packed_font(
        choice->fontFace,
        choice->fontStyle,
        choice->fontSize
    );
}

static uint8_t expand5(unsigned int value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(unsigned int value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

static void copy_utf8(char* destination, size_t capacity, const char* source) {
    if (capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static void copy_pcsl(
        char* destination,
        size_t capacity,
        const pcsl_string* source) {
    jsize convertedLength = 0;

    if (capacity == 0) {
        return;
    }
    destination[0] = '\0';
    if (source == NULL || pcsl_string_is_null(source)) {
        return;
    }

    if (pcsl_string_convert_to_utf8(
            source,
            (jbyte*)destination,
            (jint)(capacity - 1U),
            &convertedLength) == PCSL_STRING_OK) {
        if (convertedLength < 0) {
            convertedLength = 0;
        }
        if ((size_t)convertedLength >= capacity) {
            convertedLength = (jsize)(capacity - 1U);
        }
        destination[convertedLength] = '\0';
    }
}

static int ensure_event_capacity_locked(void) {
    PhoneMELCDUIEvent* replacement;
    int newCapacity;
    int index;

    if (eventCount < eventCapacity) {
        return 1;
    }

    if (eventCapacity == 0) {
        newCapacity = IOS_EVENT_QUEUE_INITIAL_CAPACITY;
    } else {
        if (eventCapacity > INT_MAX / 2) {
            return 0;
        }
        newCapacity = eventCapacity * 2;
    }

    replacement = (PhoneMELCDUIEvent*)malloc(
        sizeof(*replacement) * (size_t)newCapacity
    );
    if (replacement == NULL) {
        return 0;
    }

    for (index = 0; index < eventCount; ++index) {
        replacement[index] = eventQueue[
            (eventReadIndex + index) % eventCapacity
        ];
    }

    free(eventQueue);
    eventQueue = replacement;
    eventCapacity = newCapacity;
    eventReadIndex = 0;
    eventWriteIndex = eventCount;
    return 1;
}

static int should_log_event(int32_t kind) {
    static int logAll = -1;

    if (kind >= PHONEME_LCDUI_EVENT_RESET &&
            kind <= PHONEME_LCDUI_EVENT_SCREEN_DELETED) {
        return 1;
    }
    if (logAll < 0) {
        const char* value = getenv("PHONEME_LCDUI_LOG_ALL");
        logAll = value != NULL && value[0] != '\0' && value[0] != '0';
    }
    return logAll && kind <= PHONEME_LCDUI_EVENT_ITEM_FOCUSED;
}

static int event_metadata_key(int32_t arg3) {
    return arg3 < 0 ? arg3 : 0;
}

static int can_replace_last_event_locked(
        const PhoneMELCDUIEvent* previous,
        int32_t kind,
        int32_t componentId,
        int32_t index,
        int32_t arg3) {
    if (previous == NULL) {
        return 0;
    }

    switch (kind) {
    case PHONEME_LCDUI_EVENT_SCREEN_UPDATED:
    case PHONEME_LCDUI_EVENT_ITEM_UPDATED:
        return previous->kind == kind &&
            previous->component_id == componentId &&
            event_metadata_key(previous->arg3) == event_metadata_key(arg3);

    case PHONEME_LCDUI_EVENT_CHOICE_ELEMENT:
        return previous->kind == kind &&
            previous->component_id == componentId &&
            previous->index == index;

    case PHONEME_LCDUI_EVENT_ITEM_FOCUSED:
        /* Only the newest pending focus target is visually relevant. */
        return previous->kind == kind;

    default:
        return 0;
    }
}

static void emit_event_with_value64(
        int32_t kind,
        int32_t componentId,
        int32_t parentId,
        int32_t componentType,
        int32_t index,
        int32_t arg0,
        int32_t arg1,
        int32_t arg2,
        int32_t arg3,
        int64_t value64,
        const char* text,
        const char* detail) {
    PhoneMELCDUIEvent* event = NULL;
    int replacingLastEvent = 0;

    pthread_mutex_lock(&eventMutex);
    if (eventCount > 0) {
        int lastIndex = (eventWriteIndex + eventCapacity - 1) % eventCapacity;
        PhoneMELCDUIEvent* previous = &eventQueue[lastIndex];
        if (can_replace_last_event_locked(
                previous,
                kind,
                componentId,
                index,
                arg3)) {
            event = previous;
            replacingLastEvent = 1;
        }
    }

    if (event == NULL) {
        if (!ensure_event_capacity_locked()) {
            /*
             * Keep the runtime alive if the host is genuinely out of memory.
             * Normal LCDUI bursts grow the queue instead of discarding lifecycle
             * events such as SCREEN_SHOWN.
             */
            pthread_mutex_unlock(&eventMutex);
            return;
        }
        event = &eventQueue[eventWriteIndex];
    }
    memset(event, 0, sizeof(*event));
    event->kind = kind;
    event->component_id = componentId;
    event->parent_id = parentId;
    event->component_type = componentType;
    event->index = index;
    event->arg0 = arg0;
    event->arg1 = arg1;
    event->arg2 = arg2;
    event->arg3 = arg3;
    event->value64 = value64;
    event->generation = ++eventGeneration;
    copy_utf8(event->text, sizeof(event->text), text);
    copy_utf8(event->detail, sizeof(event->detail), detail);

    if (should_log_event(kind)) {
        fprintf(
            stderr,
            "PHONEME_LCDUI kind=%d id=%d parent=%d type=%d index=%d "
            "args=%d,%d,%d,%d value64=%lld text=%s detail=%s\n",
            kind,
            componentId,
            parentId,
            componentType,
            index,
            arg0,
            arg1,
            arg2,
            arg3,
            (long long)value64,
            event->text,
            event->detail
        );
        fflush(stderr);
    }

    if (!replacingLastEvent) {
        eventWriteIndex = (eventWriteIndex + 1) % eventCapacity;
        eventCount++;
    }
    pthread_mutex_unlock(&eventMutex);
}

static void emit_event(
        int32_t kind,
        int32_t componentId,
        int32_t parentId,
        int32_t componentType,
        int32_t index,
        int32_t arg0,
        int32_t arg1,
        int32_t arg2,
        int32_t arg3,
        const char* text,
        const char* detail) {
    emit_event_with_value64(
        kind,
        componentId,
        parentId,
        componentType,
        index,
        arg0,
        arg1,
        arg2,
        arg3,
        0,
        text,
        detail
    );
}

static void emit_screen_mode(const IOSNode* node) {
    if (node == NULL) {
        return;
    }
    emit_event(
        PHONEME_LCDUI_EVENT_SCREEN_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        fullScreenMode,
        0,
        0,
        IOS_EVENT_SCREEN_MODE_METADATA,
        node->label,
        node->detail
    );
}

static IOSNode* node_for_id(int32_t id) {
    IOSNode* node = firstNode;
    while (node != NULL) {
        if (node->id == id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

static IOSNode* create_node(
        MidpComponent* component,
        MidpDisplayable* displayable,
        MidpItem* item,
        int32_t parentId) {
    IOSNode* node = (IOSNode*)midpMalloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }

    memset(node, 0, sizeof(*node));
    node->id = MidpComponentToId(component);
    node->parentId = parentId;
    node->type = component->type;
    node->displayable = displayable;
    node->item = item;
    node->maxSize = 1024;
    node->maxValue = 100;
    node->width = fbapp_get_screen_width(0);
    node->height = 44;

    pthread_mutex_lock(&nodeMutex);
    node->next = firstNode;
    firstNode = node;
    pthread_mutex_unlock(&nodeMutex);
    return node;
}

static void free_node(IOSNode* node) {
    if (node == NULL) {
        return;
    }
    if (node->choices != NULL) {
        int index;
        for (index = 0; index < node->choiceCount; index++) {
            free_choice_image(&node->choices[index]);
        }
        midpFree(node->choices);
    }
    if (node->imageRGBA != NULL) {
        midpFree(node->imageRGBA);
    }
    midpFree(node);
}

static void clear_nodes(void) {
    IOSNode* node;

    pthread_mutex_lock(&nodeMutex);
    node = firstNode;
    firstNode = NULL;
    pthread_mutex_unlock(&nodeMutex);

    while (node != NULL) {
        IOSNode* next = node->next;
        free_node(node);
        node = next;
    }
}

static void delete_node(IOSNode* node) {
    IOSNode* previous = NULL;
    IOSNode* current;
    int found = 0;

    if (node == NULL) {
        return;
    }

    pthread_mutex_lock(&nodeMutex);
    current = firstNode;
    while (current != NULL) {
        if (current == node) {
            if (previous == NULL) {
                firstNode = current->next;
            } else {
                previous->next = current->next;
            }
            found = 1;
            break;
        }
        previous = current;
        current = current->next;
    }
    pthread_mutex_unlock(&nodeMutex);

    if (found) {
        free_node(node);
    }
}

static void emit_node(int32_t kind, const IOSNode* node) {
    if (node == NULL) {
        return;
    }
    emit_event(
        kind,
        node->id,
        node->parentId,
        node->type,
        -1,
        node->x,
        node->y,
        node->width,
        node->height,
        node->label,
        node->text
    );
}

static void emit_item_metadata(const IOSNode* node) {
    if (node == NULL || node->item == NULL) {
        return;
    }
    emit_event_with_value64(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        node->layout,
        node->appearanceMode,
        node->fitPolicy,
        -1005,
        packed_font(node->fontFace, node->fontStyle, node->fontSize),
        node->label,
        node->text
    );
}

static void emit_textfield_metadata(const IOSNode* node) {
    if (node == NULL || node->item == NULL) {
        return;
    }
    emit_event(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        node->maxSize,
        node->constraints,
        node->caretPosition,
        -1001,
        node->label,
        node->text
    );
}

static void focus_item_locked(IOSNode* node) {
    if (node == NULL || node->item == NULL || focusedItemId == node->id) {
        return;
    }
    focusedItemId = node->id;
    MidpFormFocusChanged(node);
    emit_event(
        PHONEME_LCDUI_EVENT_ITEM_FOCUSED,
        node->id,
        node->parentId,
        node->type,
        -1,
        0,
        0,
        0,
        0,
        node->label,
        node->text
    );
}

static MidpError screen_set_title(
        MidpDisplayable* displayable,
        const pcsl_string* title) {
    IOSNode* node = (IOSNode*)displayable->frame.widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    copy_pcsl(node->label, sizeof(node->label), title);
    emit_node(PHONEME_LCDUI_EVENT_SCREEN_UPDATED, node);
    return KNI_OK;
}

static MidpError screen_set_ticker(
        MidpDisplayable* displayable,
        const pcsl_string* ticker) {
    IOSNode* node = (IOSNode*)displayable->frame.widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    copy_pcsl(node->detail, sizeof(node->detail), ticker);
    emit_event(
        PHONEME_LCDUI_EVENT_SCREEN_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        node->contentWidth,
        node->contentHeight,
        node->scrollPosition,
        node->visible,
        node->label,
        node->detail
    );
    return KNI_OK;
}

static MidpError screen_show(MidpFrame* frame) {
    IOSNode* node = (IOSNode*)frame->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->visible = 1;
    emit_event(
        PHONEME_LCDUI_EVENT_SCREEN_SHOWN,
        node->id,
        0,
        node->type,
        -1,
        node->contentWidth,
        node->contentHeight,
        node->scrollPosition,
        0,
        node->label,
        node->detail
    );
    emit_screen_mode(node);
    return KNI_OK;
}

static MidpError screen_hide_delete(MidpFrame* frame, jboolean onExit) {
    IOSNode* node = (IOSNode*)frame->widgetPtr;
    (void)onExit;
    if (node == NULL) {
        return KNI_OK;
    }
    node->visible = 0;
    focusedItemId = 0;
    emit_node(PHONEME_LCDUI_EVENT_SCREEN_DELETED, node);
    frame->widgetPtr = NULL;
    delete_node(node);
    return KNI_OK;
}

static jboolean ignore_screen_event(
        MidpFrame* frame,
        PlatformEventPtr eventPtr) {
    (void)frame;
    (void)eventPtr;
    return KNI_FALSE;
}

static MidpError initialize_displayable(
        MidpDisplayable* displayable,
        const pcsl_string* title,
        const pcsl_string* ticker) {
    IOSNode* node = create_node(
        &displayable->frame.component,
        displayable,
        NULL,
        0
    );
    if (node == NULL) {
        return KNI_ENOMEM;
    }

    copy_pcsl(node->label, sizeof(node->label), title);
    copy_pcsl(node->detail, sizeof(node->detail), ticker);
    displayable->frame.widgetPtr = node;
    displayable->frame.show = screen_show;
    displayable->frame.hideAndDelete = screen_hide_delete;
    displayable->frame.handleEvent = ignore_screen_event;
    displayable->setTitle = screen_set_title;
    displayable->setTicker = screen_set_ticker;
    emit_event(
        PHONEME_LCDUI_EVENT_SCREEN_CREATED,
        node->id,
        0,
        node->type,
        -1,
        0,
        0,
        0,
        0,
        node->label,
        node->detail
    );
    return KNI_OK;
}

static int preferred_item_height(const IOSNode* node, int lockedWidth) {
    size_t textLength;
    int width = lockedWidth > 0 ? lockedWidth : fbapp_get_screen_width(0);
    int lines;

    if (node == NULL) {
        return 44;
    }
    switch (node->type) {
    case MIDP_TEXT_FIELD_TYPE:
        return 56;
    case MIDP_DATE_FIELD_TYPE:
        return 56;
    case MIDP_INTERACTIVE_GAUGE_TYPE:
    case MIDP_NON_INTERACTIVE_GAUGE_TYPE:
        return 52;
    case MIDP_POPUP_CHOICE_GROUP_TYPE:
        /* POPUP is a single collapsed control; its choices are not rows. */
        return 56;
    case MIDP_EXCLUSIVE_CHOICE_GROUP_TYPE:
    case MIDP_MULTIPLE_CHOICE_GROUP_TYPE:
    case MIDP_IMPLICIT_CHOICE_GROUP_TYPE:
        return node->choiceCount > 0 ? node->choiceCount * 44 + 24 : 44;
    default:
        break;
    }

    textLength = strlen(node->text) + strlen(node->label);
    lines = (int)(textLength / (size_t)(width > 40 ? width / 8 : 8)) + 1;
    if (lines > 6) {
        lines = 6;
    }
    return lines * 22 + 16;
}

static MidpError item_minimum_width(int* width, MidpItem* item) {
    (void)item;
    *width = 44;
    return KNI_OK;
}

static MidpError item_minimum_height(int* height, MidpItem* item) {
    *height = preferred_item_height((IOSNode*)item->widgetPtr, -1);
    return KNI_OK;
}

static MidpError item_preferred_width(
        int* width,
        MidpItem* item,
        int lockedHeight) {
    (void)item;
    (void)lockedHeight;
    *width = fbapp_get_screen_width(0);
    return KNI_OK;
}

static MidpError item_preferred_height(
        int* height,
        MidpItem* item,
        int lockedWidth) {
    *height = preferred_item_height((IOSNode*)item->widgetPtr, lockedWidth);
    return KNI_OK;
}

static MidpError item_set_label(
        MidpItem* item,
        const pcsl_string* label) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    copy_pcsl(node->label, sizeof(node->label), label);
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

static MidpError item_show(MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->visible = 1;
    emit_node(PHONEME_LCDUI_EVENT_ITEM_SHOWN, node);
    return KNI_OK;
}

static MidpError item_relocate(MidpItem* item, int x, int y) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->x = x;
    node->y = y;
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

static MidpError item_resize(MidpItem* item, int width, int height) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->width = width;
    node->height = height;
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

static MidpError item_hide(MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_OK;
    }
    node->visible = 0;
    emit_node(PHONEME_LCDUI_EVENT_ITEM_HIDDEN, node);
    return KNI_OK;
}

static MidpError item_destroy(MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_OK;
    }
    if (focusedItemId == node->id) {
        focusedItemId = 0;
    }
    emit_node(PHONEME_LCDUI_EVENT_ITEM_DELETED, node);
    item->widgetPtr = NULL;
    delete_node(node);
    return KNI_OK;
}

static jboolean ignore_item_event(
        MidpItem* item,
        PlatformEventPtr eventPtr) {
    (void)item;
    (void)eventPtr;
    return KNI_FALSE;
}

static MidpError initialize_item(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout) {
    IOSNode* node;
    int32_t parentId = owner == NULL
        ? 0
        : MidpComponentToId(&owner->frame.component);

    node = create_node(&item->component, NULL, item, parentId);
    if (node == NULL) {
        return KNI_ENOMEM;
    }

    item->ownerPtr = owner;
    item->layout = layout;
    node->layout = layout;
    item->widgetPtr = node;
    item->getMinimumWidth = item_minimum_width;
    item->getMinimumHeight = item_minimum_height;
    item->getPreferredWidth = item_preferred_width;
    item->getPreferredHeight = item_preferred_height;
    item->setLabel = item_set_label;
    item->show = item_show;
    item->relocate = item_relocate;
    item->resize = item_resize;
    item->hide = item_hide;
    item->destroy = item_destroy;
    item->handleEvent = ignore_item_event;

    copy_pcsl(node->label, sizeof(node->label), label);
    emit_node(PHONEME_LCDUI_EVENT_ITEM_CREATED, node);
    emit_item_metadata(node);
    return KNI_OK;
}

void phoneme_ios_lcdui_reset(void) {
    focusedItemId = 0;
    formScrollPosition = 0;
    fullScreenMode = 0;
    fbapp_set_fullscreen_mode(0, 0);
    clear_nodes();
    pthread_mutex_lock(&eventMutex);
    eventReadIndex = 0;
    eventWriteIndex = 0;
    eventCount = 0;
    pthread_mutex_unlock(&eventMutex);
    emit_event(PHONEME_LCDUI_EVENT_RESET, 0, 0, 0, -1, 0, 0, 0, 0, NULL, NULL);
}

int32_t phoneme_ios_lcdui_poll_event(PhoneMELCDUIEvent* eventOut) {
    if (eventOut == NULL) {
        return 0;
    }

    pthread_mutex_lock(&eventMutex);
    if (eventCount == 0) {
        pthread_mutex_unlock(&eventMutex);
        return 0;
    }
    *eventOut = eventQueue[eventReadIndex];
    eventReadIndex = (eventReadIndex + 1) % eventCapacity;
    eventCount--;
    pthread_mutex_unlock(&eventMutex);
    return 1;
}

static void capture_image(
        int32_t componentId,
        int choiceIndex,
        jobject imageData) {
    int width = 0;
    int height = 0;
    int pixelArrayLength = 0;
    int alphaArrayLength = 0;
    int pixelCount;
    int index;
    jfieldID widthField;
    jfieldID heightField;
    jfieldID pixelDataField;
    jfieldID alphaDataField;
    jbyte* pixelBytes = NULL;
    jbyte* alphaBytes = NULL;
    uint8_t* rgba = NULL;
    IOSNode* node = NULL;
    int32_t parentId = 0;
    int32_t componentType = 0;
    int32_t eventKind = PHONEME_LCDUI_EVENT_ITEM_UPDATED;
    uint64_t generation = 0;
    int choiceUpdated = 0;
    char label[IOS_LABEL_CAPACITY];
    char text[IOS_NODE_TEXT_CAPACITY];

    label[0] = '\0';
    text[0] = '\0';

    if (componentId <= 0) {
        return;
    }
    if (KNI_IsNullHandle(imageData) == KNI_TRUE) {
        pthread_mutex_lock(&nodeMutex);
        node = node_for_id(componentId);
        if (node != NULL && choiceIndex >= 0) {
            if (choiceIndex < node->choiceCount) {
                free_choice_image(&node->choices[choiceIndex]);
            } else {
                node = NULL;
            }
        } else if (node != NULL) {
            if (node->imageRGBA != NULL) {
                midpFree(node->imageRGBA);
                node->imageRGBA = NULL;
            }
            node->imageWidth = 0;
            node->imageHeight = 0;
            node->imageGeneration++;
            generation = node->imageGeneration;
            parentId = node->parentId;
            componentType = node->type;
            eventKind = node->item == NULL
                ? PHONEME_LCDUI_EVENT_SCREEN_UPDATED
                : PHONEME_LCDUI_EVENT_ITEM_UPDATED;
            copy_utf8(label, sizeof(label), node->label);
            copy_utf8(text, sizeof(text), node->text);
        }
        pthread_mutex_unlock(&nodeMutex);

        if (node != NULL && choiceIndex >= 0) {
            emit_choice(node, choiceIndex);
        } else if (node != NULL) {
            emit_event(
                eventKind,
                componentId,
                parentId,
                componentType,
                -1,
                0,
                0,
                (int32_t)(generation & 0x7fffffffU),
                -1004,
                label,
                text
            );
        }
        return;
    }

    KNI_StartHandles(3);
    KNI_DeclareHandle(imageClass);
    KNI_DeclareHandle(pixelData);
    KNI_DeclareHandle(alphaData);

    KNI_GetObjectClass(imageData, imageClass);
    widthField = KNI_GetFieldID(imageClass, "width", "I");
    heightField = KNI_GetFieldID(imageClass, "height", "I");
    pixelDataField = KNI_GetFieldID(imageClass, "pixelData", "[B");
    alphaDataField = KNI_GetFieldID(imageClass, "alphaData", "[B");

    width = KNI_GetIntField(imageData, widthField);
    height = KNI_GetIntField(imageData, heightField);
    KNI_GetObjectField(imageData, pixelDataField, pixelData);
    KNI_GetObjectField(imageData, alphaDataField, alphaData);

    if (width > 0 && height > 0 &&
            width <= INT_MAX / height &&
            width * height <= INT_MAX / 4 &&
            KNI_IsNullHandle(pixelData) != KNI_TRUE) {
        pixelCount = width * height;
        pixelArrayLength = KNI_GetArrayLength(pixelData);
        if (pixelArrayLength >= pixelCount * 2) {
            pixelBytes = (jbyte*)midpMalloc(
                (unsigned int)pixelCount * 2U);
            rgba = (uint8_t*)midpMalloc(
                (unsigned int)pixelCount * 4U);
            if (pixelBytes != NULL && rgba != NULL) {
                KNI_GetRawArrayRegion(
                    pixelData,
                    0,
                    pixelCount * 2,
                    pixelBytes
                );

                if (KNI_IsNullHandle(alphaData) != KNI_TRUE) {
                    alphaArrayLength = KNI_GetArrayLength(alphaData);
                    if (alphaArrayLength >= pixelCount) {
                        alphaBytes = (jbyte*)midpMalloc(
                            (unsigned int)pixelCount);
                        if (alphaBytes != NULL) {
                            KNI_GetRawArrayRegion(
                                alphaData,
                                0,
                                pixelCount,
                                alphaBytes
                            );
                        }
                    }
                }
            }
        }
    }

    KNI_EndHandles();

    if (pixelBytes == NULL || rgba == NULL) {
        if (pixelBytes != NULL) midpFree(pixelBytes);
        if (alphaBytes != NULL) midpFree(alphaBytes);
        if (rgba != NULL) midpFree(rgba);
        return;
    }

    pixelCount = width * height;
    for (index = 0; index < pixelCount; ++index) {
        unsigned int pixel =
            (unsigned int)(uint8_t)pixelBytes[index * 2] |
            ((unsigned int)(uint8_t)pixelBytes[index * 2 + 1] << 8);
        rgba[index * 4 + 0] = expand5((pixel >> 11) & 0x1fU);
        rgba[index * 4 + 1] = expand6((pixel >> 5) & 0x3fU);
        rgba[index * 4 + 2] = expand5(pixel & 0x1fU);
        rgba[index * 4 + 3] = alphaBytes == NULL
            ? 0xffU
            : (uint8_t)alphaBytes[index];
    }

    midpFree(pixelBytes);
    if (alphaBytes != NULL) midpFree(alphaBytes);

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    if (node != NULL && choiceIndex >= 0) {
        if (choiceIndex < node->choiceCount) {
            IOSChoice* choice = &node->choices[choiceIndex];
            if (choice->imageRGBA != NULL) {
                midpFree(choice->imageRGBA);
            }
            choice->imageRGBA = rgba;
            choice->imageWidth = width;
            choice->imageHeight = height;
            choice->imageGeneration++;
            generation = choice->imageGeneration;
            choiceUpdated = 1;
            rgba = NULL;
        } else {
            node = NULL;
        }
    } else if (node != NULL) {
        if (node->imageRGBA != NULL) {
            midpFree(node->imageRGBA);
        }
        node->imageRGBA = rgba;
        node->imageWidth = width;
        node->imageHeight = height;
        node->imageGeneration++;
        generation = node->imageGeneration;
        parentId = node->parentId;
        componentType = node->type;
        eventKind = node->item == NULL
            ? PHONEME_LCDUI_EVENT_SCREEN_UPDATED
            : PHONEME_LCDUI_EVENT_ITEM_UPDATED;
        copy_utf8(label, sizeof(label), node->label);
        copy_utf8(text, sizeof(text), node->text);
        rgba = NULL;
    }
    pthread_mutex_unlock(&nodeMutex);

    if (rgba != NULL) {
        midpFree(rgba);
    }

    if (node != NULL && choiceUpdated) {
        emit_choice(node, choiceIndex);
    } else if (node != NULL) {
        emit_event(
            eventKind,
            componentId,
            parentId,
            componentType,
            -1,
            width,
            height,
            (int32_t)(generation & 0x7fffffffU),
            -1004,
            label,
            text
        );
    }
}

void phoneme_ios_lcdui_capture_image(
        int32_t componentId,
        jobject imageData) {
    capture_image(componentId, -1, imageData);
}

void phoneme_ios_lcdui_capture_choice_image(
        int32_t componentId,
        int32_t choiceIndex,
        jobject imageData) {
    capture_image(componentId, choiceIndex, imageData);
}

int32_t phoneme_ios_lcdui_copy_image_rgba(
        int32_t componentId,
        uint8_t* destination,
        int32_t capacity,
        int32_t* width,
        int32_t* height,
        uint64_t* generation) {
    IOSNode* node;
    IOSChoice* choice = NULL;
    int32_t nodeId = componentId;
    int choiceIndex = -1;
    int32_t imageWidth = 0;
    int32_t imageHeight = 0;
    uint64_t imageGeneration = 0;
    uint8_t* imageRGBA = NULL;
    int32_t required = 0;

    if (componentId < 0 &&
            !decode_choice_image_key(componentId, &nodeId, &choiceIndex)) {
        nodeId = 0;
    }

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(nodeId);
    if (node != NULL && choiceIndex >= 0 && choiceIndex < node->choiceCount) {
        choice = &node->choices[choiceIndex];
        imageWidth = choice->imageWidth;
        imageHeight = choice->imageHeight;
        imageGeneration = choice->imageGeneration;
        imageRGBA = choice->imageRGBA;
    } else if (node != NULL && choiceIndex < 0) {
        imageWidth = node->imageWidth;
        imageHeight = node->imageHeight;
        imageGeneration = node->imageGeneration;
        imageRGBA = node->imageRGBA;
    }

    if (width != NULL) *width = imageWidth;
    if (height != NULL) *height = imageHeight;
    if (generation != NULL) *generation = imageGeneration;
    if (imageRGBA != NULL &&
            imageWidth > 0 && imageHeight > 0 &&
            imageWidth <= INT32_MAX / imageHeight &&
            imageWidth * imageHeight <= INT32_MAX / 4) {
        required = imageWidth * imageHeight * 4;
        if (destination != NULL && capacity >= required) {
            memcpy(destination, imageRGBA, (size_t)required);
        }
    }
    pthread_mutex_unlock(&nodeMutex);
    return required;
}

void phoneme_ios_lcdui_select_command(int32_t commandId) {
    if (commandId >= 0) {
        MidpCommandSelected(commandId);
    }
}

void phoneme_ios_lcdui_focus_item(int32_t componentId) {
    IOSNode* node;

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    focus_item_locked(node);
    pthread_mutex_unlock(&nodeMutex);
}

void phoneme_ios_lcdui_activate_item(int32_t componentId) {
    IOSNode* node;

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    if (node != NULL && node->item != NULL) {
        focus_item_locked(node);
        MidpFormItemPeerStateChangedByItem(node->item, -1);
    }
    pthread_mutex_unlock(&nodeMutex);
}

void phoneme_ios_lcdui_set_text(
        int32_t componentId,
        const char* utf8Text,
        int32_t caretPosition) {
    IOSNode* node;
    int resolvedCaret = caretPosition < 0 ? 0 : caretPosition;

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    if (node != NULL) {
        int changed;
        focus_item_locked(node);
        changed = strcmp(node->text, utf8Text == NULL ? "" : utf8Text) != 0 ||
            node->caretPosition != resolvedCaret;
        if (changed) {
            copy_utf8(node->text, sizeof(node->text), utf8Text);
            node->caretPosition = resolvedCaret;
            node->textChanged = 1;
            if (node->item != NULL) {
                /* Keep the native peer alive until the event captures its ID. */
                MidpFormItemPeerStateChangedByItem(node->item, 0);
            }
        }
    }
    pthread_mutex_unlock(&nodeMutex);
}

void phoneme_ios_lcdui_set_choice(
        int32_t componentId,
        int32_t elementIndex,
        int32_t selected) {
    IOSNode* node;
    int i;

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    if (node != NULL && elementIndex >= 0 && elementIndex < node->choiceCount) {
        int changed = 0;
        jboolean resolvedSelected = selected ? KNI_TRUE : KNI_FALSE;
        focus_item_locked(node);
        if (node->type != MIDP_MULTIPLE_CHOICE_GROUP_TYPE && selected) {
            for (i = 0; i < node->choiceCount; i++) {
                if (i != elementIndex && node->choices[i].selected) {
                    node->choices[i].selected = KNI_FALSE;
                    emit_choice(node, i);
                    changed = 1;
                }
            }
        }
        if (node->choices[elementIndex].selected != resolvedSelected) {
            node->choices[elementIndex].selected = resolvedSelected;
            emit_choice(node, elementIndex);
            changed = 1;
        }
        if (node->item != NULL &&
                (changed || node->type == MIDP_IMPLICIT_CHOICE_GROUP_TYPE)) {
            /* IMPLICIT List rows are actions even when already selected. */
            MidpFormItemPeerStateChangedByItem(node->item, elementIndex);
        }
    }
    pthread_mutex_unlock(&nodeMutex);
}

void phoneme_ios_lcdui_set_gauge(int32_t componentId, int32_t value) {
    IOSNode* node;

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    if (node != NULL) {
        focus_item_locked(node);
        if (value < 0) value = 0;
        if (value > node->maxValue) value = node->maxValue;
        if (node->value != value) {
            node->value = value;
            if (node->item != NULL) {
                MidpFormItemPeerStateChangedByItem(node->item, value);
            }
        }
    }
    pthread_mutex_unlock(&nodeMutex);
}

void phoneme_ios_lcdui_set_date(
        int32_t componentId,
        int64_t unixSeconds) {
    IOSNode* node;

    pthread_mutex_lock(&nodeMutex);
    node = node_for_id(componentId);
    if (node != NULL) {
        long resolvedValue = (long)unixSeconds;
        focus_item_locked(node);
        if (node->dateValue != resolvedValue) {
            node->dateValue = resolvedValue;
            if (node->item != NULL) {
                MidpFormItemPeerStateChangedByItem(node->item, 1);
            }
        }
    }
    pthread_mutex_unlock(&nodeMutex);
}

void phoneme_ios_lcdui_set_scroll_position(int32_t position) {
    int resolvedPosition = position < 0 ? 0 : position;
    if (formScrollPosition == resolvedPosition) {
        return;
    }
    formScrollPosition = resolvedPosition;
    if (MidpCurrentScreen != NULL && MidpCurrentScreen->widgetPtr != NULL) {
        MidpFormViewportChanged(MidpCurrentScreen->widgetPtr, formScrollPosition);
    }
}

void lfpport_refresh(int hardwareId, int x, int y, int w, int h) {
    fbapp_refresh(hardwareId, x, y, x + w, y + h);
}

void lfpport_set_fullscreen_mode(int hardwareId, jboolean mode) {
    IOSNode* currentNode = NULL;

    fullScreenMode = mode == KNI_TRUE ? 1 : 0;
    fbapp_set_fullscreen_mode(hardwareId, fullScreenMode);

    if (MidpCurrentScreen != NULL &&
            MidpCurrentScreen->component.type == MIDP_CANVAS_TYPE) {
        currentNode = (IOSNode*)MidpCurrentScreen->widgetPtr;
    }
    emit_screen_mode(currentNode);
}

jboolean lfpport_reverse_orientation(int hardwareId) {
    return fbapp_reverse_orientation(hardwareId);
}

void lfpport_handle_clamshell_event(void) {
    fbapp_handle_clamshell_event();
}

jboolean lfpport_get_reverse_orientation(int hardwareId) {
    return fbapp_get_reverse_orientation(hardwareId);
}

int lfpport_get_screen_width(int hardwareId) {
    return fbapp_get_screen_width(hardwareId);
}

int lfpport_get_screen_height(int hardwareId) {
    return fbapp_get_screen_height(hardwareId);
}

void lfpport_gained_foreground(int hardwareId) {
    (void)hardwareId;
}

void lfpport_ui_init(void) {
    /*
     * The traditional framebuffer ports initialize fbapp from their LFJ
     * bootstrap. The iOS port uses LFP directly, so it must own the same
     * lifecycle here. Without this call the Canvas exists, but the system
     * screen buffer remains 0x0 and every repaint is silently discarded.
     */
    fbapp_init();
    phoneme_ios_lcdui_reset();
}

void lfpport_ui_finalize(void) {
    emit_event(PHONEME_LCDUI_EVENT_RESET, 0, 0, 0, -1, 0, 0, 0, 0, NULL, NULL);
    fbapp_finalize();
}

jboolean lfpport_direct_flush(
        int hardwareId,
        const java_graphics* graphics,
        const java_imagedata* offscreenBuffer,
        int height) {
    (void)hardwareId;
    (void)graphics;
    (void)offscreenBuffer;
    (void)height;
    return KNI_FALSE;
}

char* lfpport_get_display_name(int hardwareId) {
    (void)hardwareId;
    return (char*)"iOS Display";
}

jboolean lfpport_is_display_primary(int hardwareId) {
    return hardwareId == 0 ? KNI_TRUE : KNI_FALSE;
}

jboolean lfpport_is_display_buildin(int hardwareId) {
    return hardwareId == 0 ? KNI_TRUE : KNI_FALSE;
}

jboolean lfpport_is_display_pen_supported(int hardwareId) {
    (void)hardwareId;
    return KNI_TRUE;
}

jboolean lfpport_is_display_pen_motion_supported(int hardwareId) {
    (void)hardwareId;
    return KNI_TRUE;
}

int lfpport_get_display_capabilities(int hardwareId) {
    (void)hardwareId;
    return 1   /* input events */
         | 2   /* commands */
         | 4   /* forms */
         | 8   /* ticker */
         | 16  /* title */
         | 32  /* alerts */
         | 64  /* lists */
         | 128;/* text boxes */
}

jint* lfpport_get_display_device_ids(jint* count) {
    static jint ids[] = { 0 };
    if (count != NULL) {
        *count = 1;
    }
    return ids;
}

void lfpport_display_device_state_changed(int hardwareId, int state) {
    (void)hardwareId;
    (void)state;
}

MidpError lfpport_get_font(
        PlatformFontPtr* fontPtr,
        int face,
        int style,
        int size) {
    int index;

    if (fontPtr == NULL) {
        return KNI_ENOMEM;
    }

    pthread_mutex_lock(&fontMutex);
    for (index = 0; index < fontRegistryCount; index++) {
        IOSFont* font = &fontRegistry[index];
        if (font->face == face && font->style == style && font->size == size) {
            *fontPtr = font;
            pthread_mutex_unlock(&fontMutex);
            return KNI_OK;
        }
    }

    if (fontRegistryCount >= IOS_FONT_REGISTRY_CAPACITY) {
        pthread_mutex_unlock(&fontMutex);
        return KNI_ENOMEM;
    }

    fontRegistry[fontRegistryCount].face = face;
    fontRegistry[fontRegistryCount].style = style;
    fontRegistry[fontRegistryCount].size = size;
    *fontPtr = &fontRegistry[fontRegistryCount];
    fontRegistryCount++;
    pthread_mutex_unlock(&fontMutex);
    return KNI_OK;
}

void lfpport_font_finalize(void) {
    pthread_mutex_lock(&fontMutex);
    fontRegistryCount = 0;
    pthread_mutex_unlock(&fontMutex);
}

MidpError lfpport_form_create(
        MidpDisplayable* form,
        const pcsl_string* title,
        const pcsl_string* ticker) {
    return initialize_displayable(form, title, ticker);
}

void phoneme_lfpport_form_set_screen_kind(
        MidpDisplayable* form,
        int screenKind) {
    IOSNode* node = form == NULL ? NULL : (IOSNode*)form->frame.widgetPtr;
    if (node == NULL) {
        return;
    }

    node->screenKind = screenKind;
    emit_event(
        PHONEME_LCDUI_EVENT_SCREEN_UPDATED,
        node->id,
        0,
        node->type,
        -1,
        screenKind,
        0,
        0,
        IOS_EVENT_SCREEN_KIND_METADATA,
        node->label,
        node->detail
    );
}

void phoneme_lfpport_item_set_layout(MidpItem* item, int layout) {
    IOSNode* node = item == NULL ? NULL : (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return;
    }

    item->layout = layout;
    node->layout = layout;
    emit_item_metadata(node);
}

MidpError lfpport_form_set_content_size(
        MidpDisplayable* form,
        int width,
        int height) {
    IOSNode* node = (IOSNode*)form->frame.widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->contentWidth = width;
    node->contentHeight = height;
    emit_event(
        PHONEME_LCDUI_EVENT_SCREEN_UPDATED,
        node->id,
        0,
        node->type,
        -1,
        width,
        height,
        node->scrollPosition,
        0,
        node->label,
        node->detail
    );
    return KNI_OK;
}

MidpError lfpport_form_set_current_item(MidpItem* item, int yOffset) {
    IOSNode* node = item == NULL ? NULL : (IOSNode*)item->widgetPtr;
    if (node != NULL) {
        focusedItemId = node->id;
        formScrollPosition = yOffset;
        emit_event(
            PHONEME_LCDUI_EVENT_ITEM_FOCUSED,
            node->id,
            node->parentId,
            node->type,
            -1,
            0,
            0,
            0,
            0,
            node->label,
            node->text
        );
        emit_event(
            PHONEME_LCDUI_EVENT_ITEM_UPDATED,
            node->id,
            node->parentId,
            node->type,
            -1,
            node->x,
            node->y,
            node->width,
            node->height,
            node->label,
            node->text
        );
    }
    return KNI_OK;
}

MidpError lfpport_form_get_scroll_position(int* position) {
    if (position != NULL) {
        *position = formScrollPosition;
    }
    return KNI_OK;
}

MidpError lfpport_form_set_scroll_position(int position) {
    formScrollPosition = position < 0 ? 0 : position;
    return KNI_OK;
}

MidpError lfpport_form_get_viewport_height(int* height) {
    if (height != NULL) {
        *height = fbapp_get_screen_height(0);
    }
    return KNI_OK;
}

MidpError lfpport_canvas_create(
        MidpDisplayable* canvas,
        const pcsl_string* title,
        const pcsl_string* ticker) {
    return initialize_displayable(canvas, title, ticker);
}

MidpError lfpport_alert_create(
        MidpDisplayable* alert,
        const pcsl_string* title,
        const pcsl_string* ticker,
        MidpComponentType alertType) {
    alert->frame.component.type = alertType;
    return initialize_displayable(alert, title, ticker);
}

MidpError lfpport_alert_set_contents(
        MidpDisplayable* alert,
        unsigned char* image,
        int* gaugeBounds,
        const pcsl_string* text) {
    IOSNode* node = (IOSNode*)alert->frame.widgetPtr;
    (void)image;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    copy_pcsl(node->text, sizeof(node->text), text);
    if (gaugeBounds != NULL) {
        node->value = gaugeBounds[0];
        node->maxValue = gaugeBounds[1];
    }
    emit_node(PHONEME_LCDUI_EVENT_SCREEN_UPDATED, node);
    return KNI_OK;
}

MidpError lfpport_alert_need_scrolling(
        jboolean* needScrolling,
        MidpDisplayable* alert) {
    IOSNode* node = (IOSNode*)alert->frame.widgetPtr;
    if (needScrolling != NULL) {
        *needScrolling = node != NULL && strlen(node->text) > 300
            ? KNI_TRUE
            : KNI_FALSE;
    }
    return KNI_OK;
}

static void emit_commands(MidpCommand* commands, int count) {
    int i;
    char shortLabel[IOS_LABEL_CAPACITY];
    char longLabel[IOS_LABEL_CAPACITY];

    emit_event(
        PHONEME_LCDUI_EVENT_COMMANDS_RESET,
        0,
        0,
        0,
        -1,
        count,
        0,
        0,
        0,
        NULL,
        NULL
    );
    for (i = 0; i < count; i++) {
        copy_pcsl(shortLabel, sizeof(shortLabel), &commands[i].shortLabel_str);
        copy_pcsl(longLabel, sizeof(longLabel), &commands[i].longLabel_str);
        emit_event(
            PHONEME_LCDUI_EVENT_COMMAND,
            (int32_t)commands[i].id,
            0,
            0,
            i,
            (int32_t)commands[i].type,
            commands[i].priority,
            commands[i].scope,
            0,
            shortLabel,
            longLabel
        );
    }
}

MidpError lfpport_alert_set_commands(
        MidpFrame* alert,
        MidpCommand* commands,
        int count) {
    (void)alert;
    emit_commands(commands, count);
    return KNI_OK;
}

static MidpError command_manager_show(MidpFrame* frame) {
    (void)frame;
    return KNI_OK;
}

static MidpError command_manager_hide(
        MidpFrame* frame,
        jboolean onExit) {
    (void)frame;
    (void)onExit;
    return KNI_OK;
}

static jboolean command_manager_event(
        MidpFrame* frame,
        PlatformEventPtr eventPtr) {
    (void)frame;
    (void)eventPtr;
    return KNI_FALSE;
}

MidpError cmdmanager_create(MidpFrame* manager) {
    memset(manager, 0, sizeof(*manager));
    manager->show = command_manager_show;
    manager->hideAndDelete = command_manager_hide;
    manager->handleEvent = command_manager_event;
    return KNI_OK;
}

MidpError cmdmanager_set_commands(
        MidpFrame* manager,
        MidpCommand* commands,
        int count) {
    (void)manager;
    emit_commands(commands, count);
    return KNI_OK;
}

MidpError lfpport_stringitem_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout,
        const pcsl_string* text,
        PlatformFontPtr font,
        int appearanceMode) {
    IOSNode* node;
    IOSFont* nativeFont = (IOSFont*)font;
    if (initialize_item(item, owner, label, layout) != KNI_OK) {
        return KNI_ENOMEM;
    }
    node = (IOSNode*)item->widgetPtr;
    node->appearanceMode = appearanceMode;
    if (nativeFont != NULL) {
        node->fontFace = nativeFont->face;
        node->fontStyle = nativeFont->style;
        node->fontSize = nativeFont->size;
    }
    copy_pcsl(node->text, sizeof(node->text), text);
    emit_item_metadata(node);
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

MidpError lfpport_stringitem_set_content(
        MidpItem* item,
        const pcsl_string* text,
        int appearanceMode) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    item->component.type = MIDP_PLAIN_STRING_ITEM_TYPE + appearanceMode;
    node->type = item->component.type;
    node->appearanceMode = appearanceMode;
    copy_pcsl(node->text, sizeof(node->text), text);
    emit_item_metadata(node);
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

MidpError lfpport_stringitem_set_font(
        MidpItem* item,
        PlatformFontPtr font) {
    IOSNode* node = item == NULL ? NULL : (IOSNode*)item->widgetPtr;
    IOSFont* nativeFont = (IOSFont*)font;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    if (nativeFont == NULL) {
        node->fontFace = 0;
        node->fontStyle = 0;
        node->fontSize = 0;
    } else {
        node->fontFace = nativeFont->face;
        node->fontStyle = nativeFont->style;
        node->fontSize = nativeFont->size;
    }
    emit_item_metadata(node);
    return KNI_OK;
}

MidpError lfpport_textfield_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout,
        const pcsl_string* text,
        int maxSize,
        int constraints,
        const pcsl_string* initialInputMode) {
    IOSNode* node;
    if (initialize_item(item, owner, label, layout) != KNI_OK) {
        return KNI_ENOMEM;
    }
    node = (IOSNode*)item->widgetPtr;
    node->maxSize = maxSize;
    node->constraints = constraints;
    copy_pcsl(node->text, sizeof(node->text), text);
    copy_pcsl(node->detail, sizeof(node->detail), initialInputMode);
    emit_textfield_metadata(node);
    return KNI_OK;
}

MidpError lfpport_textfield_set_string(
        MidpItem* item,
        const pcsl_string* text) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    copy_pcsl(node->text, sizeof(node->text), text);
    node->textChanged = 0;
    if (node->caretPosition > (int)strlen(node->text)) {
        node->caretPosition = (int)strlen(node->text);
    }
    emit_textfield_metadata(node);
    return KNI_OK;
}

MidpError lfpport_textfield_get_string(
        pcsl_string* text,
        jboolean* newChange,
        MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL || text == NULL || newChange == NULL) {
        return KNI_ENOMEM;
    }
    if (pcsl_string_convert_from_utf8(
            (const jbyte*)node->text,
            (jint)strlen(node->text),
            text) != PCSL_STRING_OK) {
        return KNI_ENOMEM;
    }
    *newChange = node->textChanged ? KNI_TRUE : KNI_FALSE;
    node->textChanged = 0;
    return KNI_OK;
}

MidpError lfpport_textfield_set_max_size(MidpItem* item, int maxSize) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->maxSize = maxSize;
    emit_textfield_metadata(node);
    return KNI_OK;
}

MidpError lfpport_textfield_get_caret_position(
        int* position,
        MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL || position == NULL) {
        return KNI_ENOMEM;
    }
    *position = node->caretPosition;
    return KNI_OK;
}

MidpError lfpport_textfield_set_constraints(
        MidpItem* item,
        int constraints) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->constraints = constraints;
    emit_textfield_metadata(node);
    return KNI_OK;
}

MidpError lfpport_gauge_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout,
        jboolean interactive,
        int maxValue,
        int initialValue) {
    IOSNode* node;
    if (initialize_item(item, owner, label, layout) != KNI_OK) {
        return KNI_ENOMEM;
    }
    node = (IOSNode*)item->widgetPtr;
    node->interactive = interactive;
    node->maxValue = maxValue;
    node->value = initialValue;
    emit_event(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        initialValue,
        maxValue,
        interactive,
        -1002,
        node->label,
        NULL
    );
    return KNI_OK;
}

MidpError lfpport_gauge_set_value(
        MidpItem* item,
        int value,
        int maxValue) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->value = value;
    node->maxValue = maxValue;
    emit_event(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        value,
        maxValue,
        node->interactive,
        -1002,
        node->label,
        NULL
    );
    return KNI_OK;
}

MidpError lfpport_gauge_get_value(int* value, MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL || value == NULL) {
        return KNI_ENOMEM;
    }
    *value = node->value;
    return KNI_OK;
}

MidpError lfpport_datefield_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout,
        int inputMode,
        long time,
        const pcsl_string* timezoneId) {
    IOSNode* node;
    if (initialize_item(item, owner, label, layout) != KNI_OK) {
        return KNI_ENOMEM;
    }
    node = (IOSNode*)item->widgetPtr;
    node->inputMode = inputMode;
    node->dateValue = time;
    copy_pcsl(node->detail, sizeof(node->detail), timezoneId);
    emit_event_with_value64(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        0,
        inputMode,
        0,
        -1003,
        (int64_t)time,
        node->label,
        node->detail
    );
    return KNI_OK;
}

MidpError lfpport_datefield_set_date(MidpItem* item, long time) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->dateValue = time;
    emit_event_with_value64(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        0,
        node->inputMode,
        0,
        -1003,
        (int64_t)time,
        node->label,
        node->detail
    );
    return KNI_OK;
}

MidpError lfpport_datefield_get_date(long* time, MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL || time == NULL) {
        return KNI_ENOMEM;
    }
    *time = node->dateValue;
    return KNI_OK;
}

MidpError lfpport_datefield_set_input_mode(MidpItem* item, int mode) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->inputMode = mode;
    emit_event_with_value64(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        0,
        mode,
        0,
        -1003,
        (int64_t)node->dateValue,
        node->label,
        node->detail
    );
    return KNI_OK;
}

static MidpError resize_choices(IOSNode* node, int newCount) {
    IOSChoice* resized;
    int copyCount;

    if (newCount < 0) {
        return KNI_ENOMEM;
    }
    if (newCount == 0) {
        if (node->choices != NULL) {
            int index;
            for (index = 0; index < node->choiceCount; index++) {
                free_choice_image(&node->choices[index]);
            }
            midpFree(node->choices);
        }
        node->choices = NULL;
        node->choiceCount = 0;
        return KNI_OK;
    }

    if ((size_t)newCount > (size_t)UINT_MAX / sizeof (*resized)) {
        return KNI_ENOMEM;
    }
    resized = (IOSChoice*)midpMalloc(
        (unsigned int)((size_t)newCount * sizeof (*resized)));
    if (resized == NULL) {
        return KNI_ENOMEM;
    }
    memset(resized, 0, sizeof(*resized) * (size_t)newCount);
    copyCount = node->choiceCount < newCount ? node->choiceCount : newCount;
    if (node->choices != NULL && copyCount > 0) {
        memcpy(resized, node->choices, sizeof(*resized) * (size_t)copyCount);
        midpFree(node->choices);
    }
    node->choices = resized;
    node->choiceCount = newCount;
    return KNI_OK;
}

static void emit_choice(IOSNode* node, int index) {
    if (node == NULL || index < 0 || index >= node->choiceCount) {
        return;
    }
    emit_event_with_value64(
        PHONEME_LCDUI_EVENT_CHOICE_ELEMENT,
        node->id,
        node->parentId,
        node->type,
        index,
        node->choices[index].selected,
        node->choiceCount,
        node->fitPolicy,
        choice_image_key(node->id, index),
        packed_choice_font(&node->choices[index]),
        node->choices[index].text,
        NULL
    );
}

MidpError lfpport_choicegroup_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout,
        MidpComponentType choiceType,
        MidpChoiceGroupElement* choices,
        int count,
        int selectedIndex,
        int fitPolicy) {
    IOSNode* node;
    int i;

    /*
     * choiceType is the public Choice value (1...4), while the item's
     * component type was already normalized by MidpNewItem to the native
     * zero-based enum. Keep that component type as the single source of
     * truth; assigning choiceType directly shifts IMPLICIT into POPUP.
     */
    (void)choiceType;
    if (initialize_item(item, owner, label, layout) != KNI_OK) {
        return KNI_ENOMEM;
    }
    node = (IOSNode*)item->widgetPtr;
    node->type = item->component.type;
    node->fitPolicy = fitPolicy;
    if (resize_choices(node, count) != KNI_OK) {
        return KNI_ENOMEM;
    }
    for (i = 0; i < count; i++) {
        copy_pcsl(node->choices[i].text, sizeof(node->choices[i].text), &choices[i].string);
        node->choices[i].selected = choices[i].selected;
        if (choices[i].font != NULL) {
            IOSFont* font = (IOSFont*)choices[i].font;
            node->choices[i].fontFace = font->face;
            node->choices[i].fontStyle = font->style;
            node->choices[i].fontSize = font->size;
        }
        if (i == selectedIndex && node->type != MIDP_MULTIPLE_CHOICE_GROUP_TYPE) {
            node->choices[i].selected = KNI_TRUE;
        }
        emit_choice(node, i);
    }
    return KNI_OK;
}

MidpError lfpport_choicegroup_insert(
        MidpItem* item,
        int elementIndex,
        MidpChoiceGroupElement element) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    int oldCount;
    int i;
    if (node == NULL || elementIndex < 0 || elementIndex > node->choiceCount) {
        return KNI_ENOMEM;
    }
    oldCount = node->choiceCount;
    if (resize_choices(node, oldCount + 1) != KNI_OK) {
        return KNI_ENOMEM;
    }
    for (i = oldCount; i > elementIndex; i--) {
        node->choices[i] = node->choices[i - 1];
    }
    memset(&node->choices[elementIndex], 0, sizeof(node->choices[elementIndex]));
    copy_pcsl(node->choices[elementIndex].text, sizeof(node->choices[elementIndex].text), &element.string);
    node->choices[elementIndex].selected = element.selected;
    if (element.font != NULL) {
        IOSFont* font = (IOSFont*)element.font;
        node->choices[elementIndex].fontFace = font->face;
        node->choices[elementIndex].fontStyle = font->style;
        node->choices[elementIndex].fontSize = font->size;
    }
    for (i = elementIndex; i < node->choiceCount; i++) {
        emit_choice(node, i);
    }
    return KNI_OK;
}

MidpError lfpport_choicegroup_delete(
        MidpItem* item,
        int elementIndex,
        int selectedIndex) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    int i;
    if (node == NULL || elementIndex < 0 || elementIndex >= node->choiceCount) {
        return KNI_ENOMEM;
    }
    free_choice_image(&node->choices[elementIndex]);
    for (i = elementIndex; i + 1 < node->choiceCount; i++) {
        node->choices[i] = node->choices[i + 1];
    }
    memset(&node->choices[node->choiceCount - 1], 0, sizeof(node->choices[0]));
    if (resize_choices(node, node->choiceCount - 1) != KNI_OK) {
        return KNI_ENOMEM;
    }
    if (selectedIndex >= 0 && selectedIndex < node->choiceCount) {
        node->choices[selectedIndex].selected = KNI_TRUE;
    }
    emit_event(
        PHONEME_LCDUI_EVENT_CHOICE_DELETED,
        node->id,
        node->parentId,
        node->type,
        elementIndex,
        node->choiceCount,
        selectedIndex,
        0,
        0,
        NULL,
        NULL
    );
    for (i = elementIndex; i < node->choiceCount; i++) {
        emit_choice(node, i);
    }
    return KNI_OK;
}

MidpError lfpport_choicegroup_delete_all(MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    resize_choices(node, 0);
    emit_event(
        PHONEME_LCDUI_EVENT_CHOICE_DELETED,
        node->id,
        node->parentId,
        node->type,
        -1,
        0,
        -1,
        0,
        0,
        NULL,
        NULL
    );
    return KNI_OK;
}

MidpError lfpport_choicegroup_set(
        MidpItem* item,
        int elementIndex,
        MidpChoiceGroupElement element) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL || elementIndex < 0 || elementIndex >= node->choiceCount) {
        return KNI_ENOMEM;
    }
    copy_pcsl(node->choices[elementIndex].text, sizeof(node->choices[elementIndex].text), &element.string);
    node->choices[elementIndex].selected = element.selected;
    emit_choice(node, elementIndex);
    return KNI_OK;
}

MidpError lfpport_choicegroup_set_selected_index(
        MidpItem* item,
        int elementIndex,
        jboolean selected) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    int i;
    if (node == NULL || elementIndex < 0 || elementIndex >= node->choiceCount) {
        return KNI_ENOMEM;
    }
    if (node->type != MIDP_MULTIPLE_CHOICE_GROUP_TYPE && selected) {
        for (i = 0; i < node->choiceCount; i++) {
            node->choices[i].selected = KNI_FALSE;
            emit_choice(node, i);
        }
    }
    node->choices[elementIndex].selected = selected;
    emit_choice(node, elementIndex);
    return KNI_OK;
}

MidpError lfpport_choicegroup_get_selected_index(
        int* elementIndex,
        MidpItem* item) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    int i;
    if (node == NULL || elementIndex == NULL) {
        return KNI_ENOMEM;
    }
    *elementIndex = -1;
    for (i = 0; i < node->choiceCount; i++) {
        if (node->choices[i].selected) {
            *elementIndex = i;
            break;
        }
    }
    return KNI_OK;
}

MidpError lfpport_choicegroup_set_selected_flags(
        MidpItem* item,
        jboolean* selectedArray,
        int count) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    int i;
    if (node == NULL || selectedArray == NULL) {
        return KNI_ENOMEM;
    }
    if (count > node->choiceCount) count = node->choiceCount;
    for (i = 0; i < count; i++) {
        node->choices[i].selected = selectedArray[i];
        emit_choice(node, i);
    }
    return KNI_OK;
}

MidpError lfpport_choicegroup_get_selected_flags(
        int* numSelected,
        MidpItem* item,
        jboolean* selectedArray,
        int count) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    int i;
    if (node == NULL || numSelected == NULL || selectedArray == NULL) {
        return KNI_ENOMEM;
    }
    *numSelected = 0;
    if (count > node->choiceCount) count = node->choiceCount;
    for (i = 0; i < count; i++) {
        selectedArray[i] = node->choices[i].selected;
        if (selectedArray[i]) (*numSelected)++;
    }
    return KNI_OK;
}

MidpError lfpport_choicegroup_is_selected(
        jboolean* selected,
        MidpItem* item,
        int elementIndex) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL || selected == NULL || elementIndex < 0 || elementIndex >= node->choiceCount) {
        return KNI_ENOMEM;
    }
    *selected = node->choices[elementIndex].selected;
    return KNI_OK;
}

MidpError lfpport_choicegroup_set_fit_policy(MidpItem* item, int fitPolicy) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    node->fitPolicy = fitPolicy;
    emit_item_metadata(node);
    return KNI_OK;
}

MidpError lfpport_choicegroup_set_font(
        MidpItem* item,
        int elementIndex,
        PlatformFontPtr font) {
    IOSNode* node = item == NULL ? NULL : (IOSNode*)item->widgetPtr;
    IOSFont* nativeFont = (IOSFont*)font;
    if (node == NULL || elementIndex < 0 || elementIndex >= node->choiceCount) {
        return KNI_ENOMEM;
    }
    if (nativeFont == NULL) {
        node->choices[elementIndex].fontFace = 0;
        node->choices[elementIndex].fontStyle = 0;
        node->choices[elementIndex].fontSize = 0;
    } else {
        node->choices[elementIndex].fontFace = nativeFont->face;
        node->choices[elementIndex].fontStyle = nativeFont->style;
        node->choices[elementIndex].fontSize = nativeFont->size;
    }
    emit_choice(node, elementIndex);
    return KNI_OK;
}

MidpError lfpport_choicegroup_dismiss_popup(void) {
    return KNI_OK;
}

MidpError lfpport_imageitem_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout,
        unsigned char* image,
        const pcsl_string* altText,
        int appearanceMode) {
    IOSNode* node;
    (void)image;
    if (initialize_item(item, owner, label, layout) != KNI_OK) {
        return KNI_ENOMEM;
    }
    node = (IOSNode*)item->widgetPtr;
    node->appearanceMode = appearanceMode;
    copy_pcsl(node->text, sizeof(node->text), altText);
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

MidpError lfpport_imageitem_set_content(
        MidpItem* item,
        unsigned char* image,
        const pcsl_string* altText,
        int appearanceMode) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    (void)image;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    item->component.type = MIDP_PLAIN_IMAGE_ITEM_TYPE + appearanceMode;
    node->type = item->component.type;
    node->appearanceMode = appearanceMode;
    copy_pcsl(node->text, sizeof(node->text), altText);
    emit_item_metadata(node);
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}

MidpError lfpport_customitem_create(
        MidpItem* item,
        MidpDisplayable* owner,
        const pcsl_string* label,
        int layout) {
    return initialize_item(item, owner, label, layout);
}

MidpError lfpport_customitem_refresh(
        MidpItem* item,
        int x,
        int y,
        int width,
        int height) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    emit_event(
        PHONEME_LCDUI_EVENT_ITEM_UPDATED,
        node->id,
        node->parentId,
        node->type,
        -1,
        x,
        y,
        width,
        height,
        node->label,
        node->text
    );
    return KNI_OK;
}

MidpError lfpport_customitem_get_label_width(
        int* widthRet,
        int width,
        MidpItem* item) {
    (void)item;
    if (widthRet != NULL) {
        *widthRet = width;
    }
    return KNI_OK;
}

MidpError lfpport_customitem_get_label_height(
        int width,
        int* heightRet,
        MidpItem* item) {
    (void)width;
    (void)item;
    if (heightRet != NULL) {
        *heightRet = 22;
    }
    return KNI_OK;
}

MidpError lfpport_customitem_get_item_pad(int* pad, MidpItem* item) {
    (void)item;
    if (pad != NULL) {
        *pad = 4;
    }
    return KNI_OK;
}

MidpError lfpport_customitem_set_content_buffer(
        MidpItem* item,
        unsigned char* image) {
    IOSNode* node = (IOSNode*)item->widgetPtr;
    (void)image;
    if (node == NULL) {
        return KNI_ENOMEM;
    }
    emit_node(PHONEME_LCDUI_EVENT_ITEM_UPDATED, node);
    return KNI_OK;
}
