#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_jdk8_compat_class(std::string_view name) {
    if (name == "java/lang/Enum") {
        return make_class("java/lang/Enum", "java/lang/Object",
                          kPublic | kAbstract, {
            field(kPrivate | kFinal, "name", "Ljava/lang/String;"),
            field(kPrivate | kFinal, "ordinal", "I"),
        }, {
            method(kProtected, "<init>", "(Ljava/lang/String;I)V"),
            method(kPublic | kFinal, "name", "()Ljava/lang/String;"),
            method(kPublic | kFinal, "ordinal", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
            method(kPublic | kFinal, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic | kFinal, "hashCode", "()I"),
            method(kPublic | kFinal, "compareTo", "(Ljava/lang/Enum;)I"),
            method(kPublic | kFinal, "compareTo", "(Ljava/lang/Object;)I"),
            method(kPublic | kFinal, "getDeclaringClass", "()Ljava/lang/Class;"),
            method(kPublic | kStatic, "valueOf",
                   "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;"),
        }, {"java/lang/Comparable", "java/io/Serializable"});
    }

    if (name == "java/util/function/Function") {
        return make_class("java/util/function/Function", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "apply",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
        });
    }
    if (name == "java/util/function/BiFunction") {
        return make_class("java/util/function/BiFunction", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "apply",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
        });
    }
    if (name == "java/util/function/Consumer") {
        return make_class("java/util/function/Consumer", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "accept", "(Ljava/lang/Object;)V"),
        });
    }
    if (name == "java/util/function/Predicate") {
        return make_class("java/util/function/Predicate", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "test", "(Ljava/lang/Object;)Z"),
        });
    }
    if (name == "java/util/function/IntPredicate") {
        return make_class("java/util/function/IntPredicate", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "test", "(I)Z"),
        });
    }
    if (name == "java/util/function/IntSupplier") {
        return make_class("java/util/function/IntSupplier", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "getAsInt", "()I"),
        });
    }
    if (name == "java/util/function/IntUnaryOperator") {
        return make_class("java/util/function/IntUnaryOperator",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "applyAsInt", "(I)I"),
            method(kPublic | kStatic, "identity",
                   "()Ljava/util/function/IntUnaryOperator;"),
        });
    }
    if (name == "java/util/function/ToIntFunction") {
        return make_class("java/util/function/ToIntFunction", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "applyAsInt", "(Ljava/lang/Object;)I"),
        });
    }
    if (name == "java/util/function/NativeIntIdentity") {
        return make_class("java/util/function/NativeIntIdentity",
                          "java/lang/Object", kOrdinary | kFinal, {}, {
            method(kPublic, "applyAsInt", "(I)I"),
        }, {"java/util/function/IntUnaryOperator"});
    }

    if (name == "java/util/Comparator") {
        return make_class("java/util/Comparator", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "compare",
                   "(Ljava/lang/Object;Ljava/lang/Object;)I"),
            method(kPublic, "reversed", "()Ljava/util/Comparator;"),
            method(kPublic, "thenComparing",
                   "(Ljava/util/Comparator;)Ljava/util/Comparator;"),
            method(kPublic, "thenComparing",
                   "(Ljava/util/function/Function;)Ljava/util/Comparator;"),
            method(kPublic, "thenComparingInt",
                   "(Ljava/util/function/ToIntFunction;)Ljava/util/Comparator;"),
            method(kPublic | kStatic, "comparingInt",
                   "(Ljava/util/function/ToIntFunction;)Ljava/util/Comparator;"),
        });
    }
    if (name == "java/util/NativeComparator") {
        return make_class("java/util/NativeComparator", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "kind", "I"),
            field(kPrivate, "first", "Ljava/lang/Object;"),
            field(kPrivate, "second", "Ljava/lang/Object;"),
        }, {
            method(kPublic, "compare",
                   "(Ljava/lang/Object;Ljava/lang/Object;)I"),
        }, {"java/util/Comparator"});
    }

    if (name == "java/util/Map$Entry") {
        return make_class("java/util/Map$Entry", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "getKey", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "getValue", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "setValue",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kAbstract, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "hashCode", "()I"),
        });
    }
    if (name == "java/util/NativeMapEntry") {
        return make_class("java/util/NativeMapEntry", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "owner", "Ljava/util/Map;"),
            field(kPrivate, "key", "Ljava/lang/Object;"),
        }, {
            method(kPublic, "getKey", "()Ljava/lang/Object;"),
            method(kPublic, "getValue", "()Ljava/lang/Object;"),
            method(kPublic, "setValue",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/Map$Entry"});
    }

    if (name == "java/util/LinkedHashMap" ||
        name == "java/util/IdentityHashMap" ||
        name == "java/util/WeakHashMap") {
        const std::string class_name(name);
        return make_class(class_name, "java/lang/Object", kOrdinary, {
            field(kPrivate, "keys", "[Ljava/lang/Object;"),
            field(kPrivate, "values", "[Ljava/lang/Object;"),
            field(kPrivate, "size", "I"),
            field(kPrivate, "hashes", "[I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "<init>", "(IF)V"),
            method(kPublic, "<init>", "(Ljava/util/Map;)V"),
            method(kPublic, "size", "()I"),
            method(kPublic, "isEmpty", "()Z"),
            method(kPublic, "containsKey", "(Ljava/lang/Object;)Z"),
            method(kPublic, "containsValue", "(Ljava/lang/Object;)Z"),
            method(kPublic, "get", "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "getOrDefault",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "putIfAbsent",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "computeIfAbsent",
                   "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;"),
            method(kPublic, "merge",
                   "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;"),
            method(kPublic, "remove",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "putAll", "(Ljava/util/Map;)V"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "keySet", "()Ljava/util/Set;"),
            method(kPublic, "values", "()Ljava/util/Collection;"),
            method(kPublic, "entrySet", "()Ljava/util/Set;"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/Map", "java/lang/Cloneable", "java/io/Serializable"});
    }

    if (name == "java/util/LinkedHashSet") {
        return make_class("java/util/LinkedHashSet", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate, "map", "Ljava/util/HashMap;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(I)V"),
            method(kPublic, "<init>", "(Ljava/util/Collection;)V"),
            method(kPublic, "size", "()I"),
            method(kPublic, "isEmpty", "()Z"),
            method(kPublic, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic, "iterator", "()Ljava/util/Iterator;"),
            method(kPublic, "toArray", "()[Ljava/lang/Object;"),
            method(kPublic, "toArray",
                   "([Ljava/lang/Object;)[Ljava/lang/Object;"),
            method(kPublic, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic, "remove", "(Ljava/lang/Object;)Z"),
            method(kPublic, "containsAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "addAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "removeAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "retainAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/Set", "java/lang/Cloneable", "java/io/Serializable"});
    }

    if (name == "java/util/LinkedList") {
        return make_class("java/util/LinkedList", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate, "elementData", "[Ljava/lang/Object;"),
            field(kPrivate, "size", "I"),
            field(kPrivate, "capacityIncrement", "I"),
            field(kPrivate, "mutationMode", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/util/Collection;)V"),
            method(kPublic, "size", "()I"),
            method(kPublic, "isEmpty", "()Z"),
            method(kPublic, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic, "get", "(I)Ljava/lang/Object;"),
            method(kPublic, "set", "(ILjava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic, "add", "(ILjava/lang/Object;)V"),
            method(kPublic, "addFirst", "(Ljava/lang/Object;)V"),
            method(kPublic, "addLast", "(Ljava/lang/Object;)V"),
            method(kPublic, "remove", "(I)Ljava/lang/Object;"),
            method(kPublic, "remove", "(Ljava/lang/Object;)Z"),
            method(kPublic, "removeFirst", "()Ljava/lang/Object;"),
            method(kPublic, "removeLast", "()Ljava/lang/Object;"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "indexOf", "(Ljava/lang/Object;)I"),
            method(kPublic, "lastIndexOf", "(Ljava/lang/Object;)I"),
            method(kPublic, "iterator", "()Ljava/util/Iterator;"),
            method(kPublic, "toArray", "()[Ljava/lang/Object;"),
            method(kPublic, "toArray",
                   "([Ljava/lang/Object;)[Ljava/lang/Object;"),
            method(kPublic, "addAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/List", "java/util/Deque", "java/lang/Cloneable",
             "java/io/Serializable"});
    }

    if (name == "java/util/EnumMap") {
        return make_class("java/util/EnumMap", "java/lang/Object", kOrdinary, {
            field(kPrivate, "keys", "[Ljava/lang/Object;"),
            field(kPrivate, "values", "[Ljava/lang/Object;"),
            field(kPrivate, "size", "I"),
            field(kPrivate, "hashes", "[I"),
            field(kPrivate, "keyType", "Ljava/lang/Class;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/Class;)V"),
            method(kPublic, "size", "()I"),
            method(kPublic, "isEmpty", "()Z"),
            method(kPublic, "containsKey", "(Ljava/lang/Object;)Z"),
            method(kPublic, "containsValue", "(Ljava/lang/Object;)Z"),
            method(kPublic, "get", "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "getOrDefault",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "put",
                   "(Ljava/lang/Enum;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "remove",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "putAll", "(Ljava/util/Map;)V"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "keySet", "()Ljava/util/Set;"),
            method(kPublic, "values", "()Ljava/util/Collection;"),
            method(kPublic, "entrySet", "()Ljava/util/Set;"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/Map", "java/lang/Cloneable", "java/io/Serializable"});
    }

    if (name == "java/util/EnumSet") {
        return make_class("java/util/EnumSet", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate, "map", "Ljava/util/HashMap;"),
            field(kPrivate, "elementType", "Ljava/lang/Class;"),
        }, {
            method(kPrivate, "<init>", "(Ljava/lang/Class;)V"),
            method(kPublic | kStatic, "noneOf",
                   "(Ljava/lang/Class;)Ljava/util/EnumSet;"),
            method(kPublic | kStatic, "copyOf",
                   "(Ljava/util/EnumSet;)Ljava/util/EnumSet;"),
            method(kPublic | kStatic, "copyOf",
                   "(Ljava/util/Collection;)Ljava/util/EnumSet;"),
            method(kPublic, "size", "()I"),
            method(kPublic, "isEmpty", "()Z"),
            method(kPublic, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic, "iterator", "()Ljava/util/Iterator;"),
            method(kPublic, "toArray", "()[Ljava/lang/Object;"),
            method(kPublic, "toArray",
                   "([Ljava/lang/Object;)[Ljava/lang/Object;"),
            method(kPublic, "add", "(Ljava/lang/Enum;)Z"),
            method(kPublic, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic, "remove", "(Ljava/lang/Object;)Z"),
            method(kPublic, "addAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/Set", "java/lang/Cloneable", "java/io/Serializable"});
    }

    if (name == "java/util/TreeSet") {
        return make_class("java/util/TreeSet", "java/lang/Object", kOrdinary, {
            field(kPrivate, "values", "Ljava/util/ArrayList;"),
            field(kPrivate, "comparator", "Ljava/util/Comparator;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/util/Collection;)V"),
            method(kPublic, "<init>", "(Ljava/util/Comparator;)V"),
            method(kPublic, "size", "()I"),
            method(kPublic, "isEmpty", "()Z"),
            method(kPublic, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic, "iterator", "()Ljava/util/Iterator;"),
            method(kPublic, "toArray", "()[Ljava/lang/Object;"),
            method(kPublic, "toArray",
                   "([Ljava/lang/Object;)[Ljava/lang/Object;"),
            method(kPublic, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic, "remove", "(Ljava/lang/Object;)Z"),
            method(kPublic, "containsAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "addAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "removeAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "retainAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic, "hashCode", "()I"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/util/Set", "java/lang/Cloneable", "java/io/Serializable"});
    }

    if (name == "java/util/Properties") {
        return make_class("java/util/Properties", "java/util/HashMap",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "setProperty",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;"),
            method(kPublic, "getProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "getProperty",
                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "load", "(Ljava/io/Reader;)V"),
            method(kPublic, "load", "(Ljava/io/InputStream;)V"),
            method(kPublic, "store",
                   "(Ljava/io/OutputStream;Ljava/lang/String;)V"),
            method(kPublic, "stringPropertyNames", "()Ljava/util/Set;"),
            method(kPublic, "propertyNames", "()Ljava/util/Enumeration;"),
            method(kPublic, "equals", "(Ljava/lang/Object;)Z"),
        });
    }

    if (name == "java/nio/ByteBuffer") {
        return make_class("java/nio/ByteBuffer", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "data", "[B"),
            field(kPrivate | kFinal, "limit", "I"),
            field(kPrivate, "position", "I"),
        }, {
            method(kPrivate, "<init>", "([BII)V"),
            method(kPublic | kStatic, "allocate", "(I)Ljava/nio/ByteBuffer;"),
            method(kPublic | kStatic, "wrap", "([B)Ljava/nio/ByteBuffer;"),
            method(kPublic | kStatic, "wrap", "([BII)Ljava/nio/ByteBuffer;"),
            method(kPublic, "put", "(B)Ljava/nio/ByteBuffer;"),
            method(kPublic, "put", "([B)Ljava/nio/ByteBuffer;"),
            method(kPublic, "putInt", "(I)Ljava/nio/ByteBuffer;"),
            method(kPublic, "get", "()B"),
            method(kPublic, "get", "([B)Ljava/nio/ByteBuffer;"),
            method(kPublic, "getInt", "()I"),
            method(kPublic, "array", "()[B"),
            method(kPublic, "position", "()I"),
            method(kPublic, "position", "(I)Ljava/nio/ByteBuffer;"),
            method(kPublic, "remaining", "()I"),
        });
    }

    if (name == "java/math/BigDecimal") {
        return make_class("java/math/BigDecimal", "java/lang/Number",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "unscaled", "J"),
            field(kPrivate | kFinal, "scale", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic | kStatic, "valueOf", "(J)Ljava/math/BigDecimal;"),
            method(kPublic, "movePointRight", "(I)Ljava/math/BigDecimal;"),
            method(kPublic, "intValueExact", "()I"),
            method(kPublic, "compareTo", "(Ljava/math/BigDecimal;)I"),
            method(kPublic, "compareTo", "(Ljava/lang/Object;)I"),
            method(kPublic, "intValue", "()I"),
            method(kPublic, "longValue", "()J"),
            method(kPublic, "floatValue", "()F"),
            method(kPublic, "doubleValue", "()D"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        }, {"java/lang/Comparable", "java/io/Serializable"});
    }

    if (name == "java/security/SecureRandom") {
        return make_class("java/security/SecureRandom", "java/util/Random",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "nextLong", "()J"),
            method(kPublic, "nextBytes", "([B)V"),
        });
    }

    if (name == "java/util/stream/Stream") {
        return make_class("java/util/stream/Stream", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "values", "[Ljava/lang/Object;"),
        }, {
            method(kPrivate, "<init>", "([Ljava/lang/Object;)V"),
            method(kPublic, "mapToInt",
                   "(Ljava/util/function/ToIntFunction;)Ljava/util/stream/IntStream;"),
        });
    }
    if (name == "java/util/stream/IntStream") {
        return make_class("java/util/stream/IntStream", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "values", "[I"),
        }, {
            method(kPrivate, "<init>", "([I)V"),
            method(kPublic, "toArray", "()[I"),
        });
    }

    if (name == "java/util/regex/PatternSyntaxException") {
        return make_class("java/util/regex/PatternSyntaxException",
                          "java/lang/IllegalArgumentException", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/util/regex/Pattern") {
        return make_class("java/util/regex/Pattern", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "source", "Ljava/lang/String;"),
            field(kPrivate | kFinal, "flags", "I"),
        }, {
            method(kPrivate, "<init>", "(Ljava/lang/String;I)V"),
            method(kPublic | kStatic, "compile",
                   "(Ljava/lang/String;)Ljava/util/regex/Pattern;"),
            method(kPublic | kStatic, "compile",
                   "(Ljava/lang/String;I)Ljava/util/regex/Pattern;"),
            method(kPublic | kStatic, "matches",
                   "(Ljava/lang/String;Ljava/lang/CharSequence;)Z"),
            method(kPublic, "matcher",
                   "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;"),
        });
    }
    if (name == "java/util/regex/Matcher") {
        return make_class("java/util/regex/Matcher", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "pattern", "Ljava/util/regex/Pattern;"),
            field(kPrivate | kFinal, "input", "Ljava/lang/String;"),
            field(kPrivate, "searchFrom", "I"),
            field(kPrivate, "groups", "[Ljava/lang/String;"),
            field(kPrivate, "matched", "Z"),
        }, {
            method(kPrivate, "<init>",
                   "(Ljava/util/regex/Pattern;Ljava/lang/String;)V"),
            method(kPublic, "find", "()Z"),
            method(kPublic, "matches", "()Z"),
            method(kPublic, "group", "()Ljava/lang/String;"),
            method(kPublic, "group", "(I)Ljava/lang/String;"),
        });
    }

    if (name == "java/util/zip/ZipException") {
        return make_class("java/util/zip/ZipException",
                          "java/io/IOException", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/util/zip/DataFormatException") {
        return make_class("java/util/zip/DataFormatException",
                          "java/lang/Exception", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/util/zip/Inflater") {
        return make_class("java/util/zip/Inflater", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate, "input", "[B"),
            field(kPrivate, "output", "[B"),
            field(kPrivate, "offset", "I"),
            field(kPrivate, "finished", "Z"),
            field(kPrivate, "needsInput", "Z"),
            field(kPrivate, "needsDictionary", "Z"),
            field(kPrivate, "ended", "Z"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "setInput", "([B)V"),
            method(kPublic, "inflate", "([B)I"),
            method(kPublic, "finished", "()Z"),
            method(kPublic, "needsInput", "()Z"),
            method(kPublic, "needsDictionary", "()Z"),
            method(kPublic, "end", "()V"),
        });
    }
    if (name == "java/util/zip/GZIPInputStream") {
        return make_class("java/util/zip/GZIPInputStream",
                          "java/io/InputStream", kOrdinary, {
            field(kPrivate, "data", "[B"),
            field(kPrivate, "position", "I"),
            field(kPrivate, "closed", "Z"),
        }, {
            method(kPublic, "<init>", "(Ljava/io/InputStream;)V"),
            method(kPublic, "read", "()I"),
            method(kPublic, "read", "([BII)I"),
            method(kPublic, "close", "()V"),
        });
    }
    if (name == "java/util/zip/GZIPOutputStream") {
        return make_class("java/util/zip/GZIPOutputStream",
                          "java/io/OutputStream", kOrdinary, {
            field(kPrivate | kFinal, "out", "Ljava/io/OutputStream;"),
            field(kPrivate, "data", "[B"),
            field(kPrivate, "size", "I"),
            field(kPrivate, "closed", "Z"),
        }, {
            method(kPublic, "<init>", "(Ljava/io/OutputStream;)V"),
            method(kPublic, "write", "(I)V"),
            method(kPublic, "write", "([BII)V"),
            method(kPublic, "close", "()V"),
        });
    }

    if (name == "java/net/URL") {
        return make_class("java/net/URL", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "owner", "Ljava/lang/Class;"),
            field(kPrivate | kFinal, "name", "Ljava/lang/String;"),
        }, {
            method(kPrivate, "<init>",
                   "(Ljava/lang/Class;Ljava/lang/String;)V"),
            method(kPublic, "openStream", "()Ljava/io/InputStream;"),
            method(kPublic, "toURI", "()Ljava/net/URI;"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        });
    }

    if (name == "java/net/URI") {
        return make_class("java/net/URI", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "path", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "getPath", "()Ljava/lang/String;"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        });
    }

    if (name == "java/lang/ReflectiveOperationException") {
        return make_class("java/lang/ReflectiveOperationException",
                          "java/lang/Exception", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/lang/NoSuchFieldException") {
        return make_class("java/lang/NoSuchFieldException",
                          "java/lang/ReflectiveOperationException",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/lang/NoSuchMethodException") {
        return make_class("java/lang/NoSuchMethodException",
                          "java/lang/ReflectiveOperationException",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "java/lang/reflect/Modifier") {
        return make_class("java/lang/reflect/Modifier", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "isStatic", "(I)Z"),
        });
    }
    if (name == "java/lang/reflect/Field") {
        return make_class("java/lang/reflect/Field", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "declaringClass", "Ljava/lang/Class;"),
            field(kPrivate | kFinal, "name", "Ljava/lang/String;"),
            field(kPrivate | kFinal, "descriptor", "Ljava/lang/String;"),
            field(kPrivate | kFinal, "modifiers", "I"),
        }, {
            method(kPrivate, "<init>",
                   "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;I)V"),
            method(kPublic, "get", "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "getModifiers", "()I"),
            method(kPublic, "getType", "()Ljava/lang/Class;"),
        });
    }
    if (name == "java/lang/reflect/Method") {
        return make_class("java/lang/reflect/Method", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "declaringClass", "Ljava/lang/Class;"),
            field(kPrivate | kFinal, "name", "Ljava/lang/String;"),
            field(kPrivate | kFinal, "descriptor", "Ljava/lang/String;"),
            field(kPrivate | kFinal, "modifiers", "I"),
        }, {
            method(kPrivate, "<init>",
                   "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;I)V"),
            method(kPublic, "invoke",
                   "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "getModifiers", "()I"),
        });
    }

    if (name == "java/text/SimpleDateFormat") {
        return make_class("java/text/SimpleDateFormat", "java/lang/Object",
                          kOrdinary, {
            field(kPrivate | kFinal, "pattern", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "format",
                   "(Ljava/util/Date;)Ljava/lang/String;"),
        });
    }

    if (name == "java/util/concurrent/TimeUnit") {
        return make_class("java/util/concurrent/TimeUnit", "java/lang/Enum",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "nanosPerUnit", "J"),
            field(kPublic | kStatic | kFinal, "NANOSECONDS",
                  "Ljava/util/concurrent/TimeUnit;"),
            field(kPublic | kStatic | kFinal, "MICROSECONDS",
                  "Ljava/util/concurrent/TimeUnit;"),
            field(kPublic | kStatic | kFinal, "MILLISECONDS",
                  "Ljava/util/concurrent/TimeUnit;"),
            field(kPublic | kStatic | kFinal, "SECONDS",
                  "Ljava/util/concurrent/TimeUnit;"),
            field(kPublic | kStatic | kFinal, "MINUTES",
                  "Ljava/util/concurrent/TimeUnit;"),
            field(kPublic | kStatic | kFinal, "HOURS",
                  "Ljava/util/concurrent/TimeUnit;"),
            field(kPublic | kStatic | kFinal, "DAYS",
                  "Ljava/util/concurrent/TimeUnit;"),
        }, {
            method(kPrivate, "<init>", "(Ljava/lang/String;IJ)V"),
            method(kStatic, "<clinit>", "()V"),
            method(kPublic, "toNanos", "(J)J"),
            method(kPublic, "toMicros", "(J)J"),
            method(kPublic, "toMillis", "(J)J"),
            method(kPublic, "toSeconds", "(J)J"),
            method(kPublic, "convert",
                   "(JLjava/util/concurrent/TimeUnit;)J"),
        });
    }

    return nullptr;
}

} // namespace

void register_jdk8_compat_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_jdk8_compat_class);
}

} // namespace phoneme::vm
