#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <array>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_util_class(std::string_view name) {
    if (name == "java/util/TimerTask") {
        return make_class("java/util/TimerTask", "java/lang/Object",
                          kOrdinary | kAbstract, {}, {
            method(kProtected, "<init>", "()V"),
            method(kPublic | kAbstract, "run", "()V"),
            method(kPublic, "cancel", "()Z"),
            method(kPublic, "scheduledExecutionTime", "()J"),
        }, {"java/lang/Runnable"});
    }
    if (name == "java/util/Timer") {
        return make_class("java/util/Timer", "java/lang/Object", kOrdinary,
                          {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Z)V"),
            method(kPublic, "schedule", "(Ljava/util/TimerTask;J)V"),
            method(kPublic, "schedule", "(Ljava/util/TimerTask;JJ)V"),
            method(kPublic, "schedule",
                   "(Ljava/util/TimerTask;Ljava/util/Date;)V"),
            method(kPublic, "schedule",
                   "(Ljava/util/TimerTask;Ljava/util/Date;J)V"),
            method(kPublic, "scheduleAtFixedRate",
                   "(Ljava/util/TimerTask;JJ)V"),
            method(kPublic, "scheduleAtFixedRate",
                   "(Ljava/util/TimerTask;Ljava/util/Date;J)V"),
            method(kPublic, "cancel", "()V"),
        });
    }
    if (name == "java/util/Date") {
        return make_class("java/util/Date", "java/lang/Object", kOrdinary, {
            field(kPrivate, "fastTime", "J"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(J)V"),
            method(kPublic, "getTime", "()J"),
            method(kPublic, "setTime", "(J)V"),
            method(kPublic, "before", "(Ljava/util/Date;)Z"),
            method(kPublic, "after", "(Ljava/util/Date;)Z"),
            method(kPublic, "compareTo", "(Ljava/util/Date;)I"),
            method(kPublic, "compareTo", "(Ljava/lang/Object;)I"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/lang/Cloneable", "java/io/Serializable"});
    }
    if (name == "java/util/TimeZone") {
        return make_class("java/util/TimeZone", "java/lang/Object",
                          kOrdinary | kAbstract, {
            // Kept in the base layout so native operations work for every
            // concrete phoneME-compatible implementation.
            field(kPrivate, "id", "Ljava/lang/String;"),
            field(kPrivate, "rawOffset", "I"),
            field(kPrivate | kStatic, "defaultZone", "Ljava/util/TimeZone;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kAbstract, "getOffset", "(IIIIII)I"),
            method(kPublic | kAbstract, "getRawOffset", "()I"),
            method(kPublic, "setRawOffset", "(I)V"),
            method(kPublic | kAbstract, "useDaylightTime", "()Z"),
            method(kPublic, "getID", "()Ljava/lang/String;"),
            method(kPublic, "setID", "(Ljava/lang/String;)V"),
            method(kPublic, "hasSameRules", "(Ljava/util/TimeZone;)Z"),
            method(kPublic | kStatic, "getTimeZone",
                   "(Ljava/lang/String;)Ljava/util/TimeZone;"),
            method(kPublic | kStatic, "getDefault", "()Ljava/util/TimeZone;"),
            method(kPublic | kStatic, "setDefault", "(Ljava/util/TimeZone;)V"),
            method(kPublic | kStatic, "getAvailableIDs", "()[Ljava/lang/String;"),
        }, {"java/lang/Cloneable", "java/io/Serializable"});
    }
    if (name == "com/sun/cldc/util/j2me/TimeZoneImpl") {
        return make_class("com/sun/cldc/util/j2me/TimeZoneImpl",
                          "java/util/TimeZone", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "getOffset", "(IIIIII)I"),
            method(kPublic, "getRawOffset", "()I"),
            method(kPublic, "useDaylightTime", "()Z"),
            method(kPublic, "getID", "()Ljava/lang/String;"),
            method(kPublic | kSynchronized, "getInstance",
                   "(Ljava/lang/String;)Ljava/util/TimeZone;"),
            method(kPublic | kSynchronized, "getIDs",
                   "()[Ljava/lang/String;"),
        });
    }
    if (name == "java/util/Calendar") {
        return make_class("java/util/Calendar", "java/lang/Object",
                          kOrdinary | kAbstract, {
            field(kProtected, "time", "J"),
            field(kPrivate, "zone", "Ljava/util/TimeZone;"),
            field(kProtected, "fields", "[I"),
            field(kProtected, "isSet", "[Z"),
            field(kPublic | kStatic | kFinal, "YEAR", "I"),
            field(kPublic | kStatic | kFinal, "MONTH", "I"),
            field(kPublic | kStatic | kFinal, "DATE", "I"),
            field(kPublic | kStatic | kFinal, "DAY_OF_MONTH", "I"),
            field(kPublic | kStatic | kFinal, "DAY_OF_WEEK", "I"),
            field(kPublic | kStatic | kFinal, "AM_PM", "I"),
            field(kPublic | kStatic | kFinal, "HOUR", "I"),
            field(kPublic | kStatic | kFinal, "HOUR_OF_DAY", "I"),
            field(kPublic | kStatic | kFinal, "MINUTE", "I"),
            field(kPublic | kStatic | kFinal, "SECOND", "I"),
            field(kPublic | kStatic | kFinal, "MILLISECOND", "I"),
            field(kPublic | kStatic | kFinal, "SUNDAY", "I"),
            field(kPublic | kStatic | kFinal, "MONDAY", "I"),
            field(kPublic | kStatic | kFinal, "TUESDAY", "I"),
            field(kPublic | kStatic | kFinal, "WEDNESDAY", "I"),
            field(kPublic | kStatic | kFinal, "THURSDAY", "I"),
            field(kPublic | kStatic | kFinal, "FRIDAY", "I"),
            field(kPublic | kStatic | kFinal, "SATURDAY", "I"),
            field(kPublic | kStatic | kFinal, "JANUARY", "I"),
            field(kPublic | kStatic | kFinal, "FEBRUARY", "I"),
            field(kPublic | kStatic | kFinal, "MARCH", "I"),
            field(kPublic | kStatic | kFinal, "APRIL", "I"),
            field(kPublic | kStatic | kFinal, "MAY", "I"),
            field(kPublic | kStatic | kFinal, "JUNE", "I"),
            field(kPublic | kStatic | kFinal, "JULY", "I"),
            field(kPublic | kStatic | kFinal, "AUGUST", "I"),
            field(kPublic | kStatic | kFinal, "SEPTEMBER", "I"),
            field(kPublic | kStatic | kFinal, "OCTOBER", "I"),
            field(kPublic | kStatic | kFinal, "NOVEMBER", "I"),
            field(kPublic | kStatic | kFinal, "DECEMBER", "I"),
            field(kPublic | kStatic | kFinal, "AM", "I"),
            field(kPublic | kStatic | kFinal, "PM", "I"),
        }, {
            method(kProtected, "<init>", "()V"),
            method(kPublic | kStatic | kSynchronized, "getInstance",
                   "()Ljava/util/Calendar;"),
            method(kPublic | kStatic | kSynchronized, "getInstance",
                   "(Ljava/util/TimeZone;)Ljava/util/Calendar;"),
            method(kPublic | kFinal, "getTime", "()Ljava/util/Date;"),
            method(kPublic | kFinal, "setTime", "(Ljava/util/Date;)V"),
            method(kProtected, "getTimeInMillis", "()J"),
            method(kProtected, "setTimeInMillis", "(J)V"),
            method(kPublic | kFinal, "get", "(I)I"),
            method(kPublic | kFinal, "set", "(II)V"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "clear", "(I)V"),
            method(kPublic, "before", "(Ljava/lang/Object;)Z"),
            method(kPublic, "after", "(Ljava/lang/Object;)Z"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "setTimeZone", "(Ljava/util/TimeZone;)V"),
            method(kPublic, "getTimeZone", "()Ljava/util/TimeZone;"),
            method(kProtected | kAbstract, "computeFields", "()V"),
            method(kProtected | kAbstract, "computeTime", "()V"),
        }, {"java/lang/Cloneable", "java/io/Serializable"});
    }
    if (name == "java/util/GregorianCalendar") {
        return make_class("java/util/GregorianCalendar", "java/util/Calendar",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/util/TimeZone;)V"),
            method(kProtected, "computeFields", "()V"),
            method(kProtected, "computeTime", "()V"),
        });
    }
    if (name == "java/util/Objects") {
        return make_class("java/util/Objects", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic | kStatic, "requireNonNull",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
        });
    }
    if (name == "java/util/Enumeration") {
        return make_class("java/util/Enumeration", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "hasMoreElements", "()Z"),
            method(kPublic | kAbstract, "nextElement", "()Ljava/lang/Object;"),
        });
    }
    if (name == "java/util/ArrayEnumeration") {
        return make_class("java/util/ArrayEnumeration", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "values", "[Ljava/lang/Object;"),
            field(kPrivate, "index", "I"),
            field(kPrivate, "size", "I"),
        }, {
            method(kPublic, "hasMoreElements", "()Z"),
            method(kPublic, "nextElement", "()Ljava/lang/Object;"),
        }, {"java/util/Enumeration"});
    }
    if (name == "java/util/StringTokenizer") {
        return make_class("java/util/StringTokenizer", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate, "string", "Ljava/lang/String;"),
            field(kPrivate, "delimiters", "Ljava/lang/String;"),
            field(kPrivate, "position", "I"),
            field(kPrivate, "returnDelimiters", "Z"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;Z)V"),
            method(kPublic, "hasMoreTokens", "()Z"),
            method(kPublic, "nextToken", "()Ljava/lang/String;"),
            method(kPublic, "nextToken",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "hasMoreElements", "()Z"),
            method(kPublic, "nextElement", "()Ljava/lang/Object;"),
            method(kPublic, "countTokens", "()I"),
        }, {"java/util/Enumeration"});
    }
    if (name == "java/util/Vector") {
        return make_class("java/util/Vector", "java/lang/Object", kOrdinary, {
            field(kProtected, "elementData", "[Ljava/lang/Object;"),
            field(kProtected, "elementCount", "I"),
            field(kProtected, "capacityIncrement", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "<init>", "(II)V"),
            method(kPublic | kSynchronized, "size", "()I"),
            method(kPublic | kSynchronized, "capacity", "()I"),
            method(kPublic | kSynchronized, "ensureCapacity", "(I)V"),
            method(kPublic | kSynchronized, "trimToSize", "()V"),
            method(kPublic | kSynchronized, "setSize", "(I)V"),
            method(kPublic | kSynchronized, "isEmpty", "()Z"),
            method(kPublic | kSynchronized, "copyInto", "([Ljava/lang/Object;)V"),
            method(kPublic | kSynchronized, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic | kSynchronized, "indexOf", "(Ljava/lang/Object;)I"),
            method(kPublic | kSynchronized, "indexOf", "(Ljava/lang/Object;I)I"),
            method(kPublic | kSynchronized, "lastIndexOf", "(Ljava/lang/Object;)I"),
            method(kPublic | kSynchronized, "lastIndexOf", "(Ljava/lang/Object;I)I"),
            method(kPublic | kSynchronized, "elementAt", "(I)Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "firstElement", "()Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "lastElement", "()Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "setElementAt", "(Ljava/lang/Object;I)V"),
            method(kPublic | kSynchronized, "removeElementAt", "(I)V"),
            method(kPublic | kSynchronized, "insertElementAt", "(Ljava/lang/Object;I)V"),
            method(kPublic | kSynchronized, "addElement", "(Ljava/lang/Object;)V"),
            method(kPublic | kSynchronized, "removeElement", "(Ljava/lang/Object;)Z"),
            method(kPublic | kSynchronized, "removeAllElements", "()V"),
            method(kPublic | kSynchronized, "elements", "()Ljava/util/Enumeration;"),
            method(kPublic | kSynchronized, "toString", "()Ljava/lang/String;"),
        });
    }
    if (name == "java/util/Stack") {
        return make_class("java/util/Stack", "java/util/Vector", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "push", "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "pop", "()Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "peek", "()Ljava/lang/Object;"),
            method(kPublic, "empty", "()Z"),
            method(kPublic | kSynchronized, "search", "(Ljava/lang/Object;)I"),
        });
    }
    if (name == "java/util/Hashtable") {
        return make_class("java/util/Hashtable", "java/lang/Object", kOrdinary, {
            field(kPrivate, "keys", "[Ljava/lang/Object;"),
            field(kPrivate, "values", "[Ljava/lang/Object;"),
            field(kPrivate, "count", "I"),
            field(kPrivate, "hashes", "[I"),
            field(kPrivate, "buckets", "[I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "<init>", "(IF)V"),
            method(kPublic | kSynchronized, "size", "()I"),
            method(kPublic | kSynchronized, "isEmpty", "()Z"),
            method(kPublic | kSynchronized, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic | kSynchronized, "containsKey", "(Ljava/lang/Object;)Z"),
            method(kPublic | kSynchronized, "get",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "remove",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kSynchronized, "clear", "()V"),
            method(kPublic | kSynchronized, "keys", "()Ljava/util/Enumeration;"),
            method(kPublic | kSynchronized, "elements", "()Ljava/util/Enumeration;"),
            method(kProtected, "rehash", "()V"),
            method(kPublic | kSynchronized, "toString", "()Ljava/lang/String;"),
        });
    }
    if (name == "java/util/Random") {
        return make_class("java/util/Random", "java/lang/Object", kOrdinary, {
            field(kPrivate, "seed", "J"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(J)V"),
            method(kPublic | kSynchronized, "setSeed", "(J)V"),
            method(kProtected | kSynchronized, "next", "(I)I"),
            method(kPublic, "nextInt", "()I"),
            method(kPublic, "nextInt", "(I)I"),
            method(kPublic, "nextLong", "()J"),
            method(kPublic, "nextBoolean", "()Z"),
            method(kPublic, "nextFloat", "()F"),
            method(kPublic, "nextDouble", "()D"),
        });
    }

    struct Hierarchy final {
        const char* name;
        const char* super_name;
    };
    static constexpr std::array<Hierarchy, 2> hierarchy {{
        {"java/util/NoSuchElementException", "java/lang/RuntimeException"},
        {"java/util/EmptyStackException", "java/lang/RuntimeException"},
    }};
    for (const Hierarchy& entry : hierarchy) {
        if (name == entry.name) {
            return make_class(entry.name, entry.super_name, kOrdinary, {}, {
                method(kPublic, "<init>", "()V"),
                method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            });
        }
    }

    return nullptr;
}

} // namespace

void register_util_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_util_class);
}

} // namespace phoneme::vm
