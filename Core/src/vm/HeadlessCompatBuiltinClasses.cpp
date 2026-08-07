#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_headless_compat_class(std::string_view name) {
    if (name == "java/lang/Iterable") {
        return make_class("java/lang/Iterable", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "iterator", "()Ljava/util/Iterator;"),
        });
    }
    if (name == "java/lang/Comparable") {
        return make_class("java/lang/Comparable", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "compareTo", "(Ljava/lang/Object;)I"),
        });
    }
    if (name == "java/lang/AutoCloseable") {
        return make_class("java/lang/AutoCloseable", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "close", "()V"),
        });
    }
    if (name == "java/io/Closeable") {
        return make_class("java/io/Closeable", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "close", "()V"),
        }, {"java/lang/AutoCloseable"});
    }
    if (name == "java/util/Iterator") {
        return make_class("java/util/Iterator", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "hasNext", "()Z"),
            method(kPublic | kAbstract, "next", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "remove", "()V"),
        });
    }
    if (name == "java/util/Collection") {
        return make_class("java/util/Collection", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "size", "()I"),
            method(kPublic | kAbstract, "isEmpty", "()Z"),
            method(kPublic | kAbstract, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "iterator", "()Ljava/util/Iterator;"),
            method(kPublic | kAbstract, "toArray", "()[Ljava/lang/Object;"),
            method(kPublic | kAbstract, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "remove", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "addAll", "(Ljava/util/Collection;)Z"),
            method(kPublic, "removeIf", "(Ljava/util/function/Predicate;)Z"),
            method(kPublic, "stream", "()Ljava/util/stream/Stream;"),
            method(kPublic | kAbstract, "clear", "()V"),
        }, {"java/lang/Iterable"});
    }
    if (name == "java/util/Queue") {
        return make_class("java/util/Queue", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "offer", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "remove", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "poll", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "element", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "peek", "()Ljava/lang/Object;"),
        }, {"java/util/Collection"});
    }
    if (name == "java/util/Deque") {
        return make_class("java/util/Deque", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "addFirst", "(Ljava/lang/Object;)V"),
            method(kPublic | kAbstract, "addLast", "(Ljava/lang/Object;)V"),
            method(kPublic | kAbstract, "offerFirst", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "offerLast", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "removeFirst", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "removeLast", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "pollFirst", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "pollLast", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "getFirst", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "getLast", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "peekFirst", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "peekLast", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "removeFirstOccurrence",
                   "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "removeLastOccurrence",
                   "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "push", "(Ljava/lang/Object;)V"),
            method(kPublic | kAbstract, "pop", "()Ljava/lang/Object;"),
            method(kPublic | kAbstract, "descendingIterator",
                   "()Ljava/util/Iterator;"),
        }, {"java/util/Queue"});
    }
    if (name == "java/util/Map") {
        return make_class("java/util/Map", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "size", "()I"),
            method(kPublic | kAbstract, "isEmpty", "()Z"),
            method(kPublic | kAbstract, "containsKey", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "containsValue", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "get", "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "getOrDefault",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kAbstract, "put",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "putIfAbsent",
                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic, "computeIfAbsent",
                   "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;"),
            method(kPublic, "merge",
                   "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;"),
            method(kPublic | kAbstract, "remove",
                   "(Ljava/lang/Object;)Ljava/lang/Object;"),
            method(kPublic | kAbstract, "putAll", "(Ljava/util/Map;)V"),
            method(kPublic | kAbstract, "clear", "()V"),
            method(kPublic | kAbstract, "keySet", "()Ljava/util/Set;"),
            method(kPublic | kAbstract, "values", "()Ljava/util/Collection;"),
            method(kPublic | kAbstract, "entrySet", "()Ljava/util/Set;"),
        });
    }
    if (name == "java/util/Set") {
        return make_class("java/util/Set", "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "size", "()I"),
            method(kPublic | kAbstract, "isEmpty", "()Z"),
            method(kPublic | kAbstract, "contains", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "iterator", "()Ljava/util/Iterator;"),
            method(kPublic | kAbstract, "toArray", "()[Ljava/lang/Object;"),
            method(kPublic | kAbstract, "toArray",
                   "([Ljava/lang/Object;)[Ljava/lang/Object;"),
            method(kPublic | kAbstract, "add", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "remove", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "containsAll", "(Ljava/util/Collection;)Z"),
            method(kPublic | kAbstract, "addAll", "(Ljava/util/Collection;)Z"),
            method(kPublic | kAbstract, "retainAll", "(Ljava/util/Collection;)Z"),
            method(kPublic | kAbstract, "removeAll", "(Ljava/util/Collection;)Z"),
            method(kPublic | kAbstract, "clear", "()V"),
            method(kPublic | kAbstract, "equals", "(Ljava/lang/Object;)Z"),
            method(kPublic | kAbstract, "hashCode", "()I"),
        }, {"java/util/Collection"});
    }
    if (name == "java/util/ArrayIterator") {
        return make_class("java/util/ArrayIterator", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "values", "[Ljava/lang/Object;"),
            field(kPrivate, "index", "I"),
            field(kPrivate, "size", "I"),
            field(kPrivate, "owner", "Ljava/lang/Object;"),
            field(kPrivate, "lastReturned", "I"),
            field(kPrivate, "removeKind", "I"),
        }, {
            method(kPublic, "hasNext", "()Z"),
            method(kPublic, "next", "()Ljava/lang/Object;"),
            method(kPublic, "remove", "()V"),
        }, {"java/util/Iterator"});
    }
    if (name == "java/util/HashMap") {
        return make_class("java/util/HashMap", "java/lang/Object", kOrdinary, {
            field(kPrivate, "keys", "[Ljava/lang/Object;"),
            field(kPrivate, "values", "[Ljava/lang/Object;"),
            field(kPrivate, "size", "I"),
            field(kPrivate, "hashes", "[I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(I)V"),
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
    if (name == "java/util/HashSet") {
        return make_class("java/util/HashSet", "java/lang/Object", kOrdinary, {
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
    if (name == "java/util/Arrays") {
        return make_class("java/util/Arrays", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic | kStatic, "equals", "([B[B)Z"),
            method(kPublic | kStatic, "equals", "([I[I)Z"),
            method(kPublic | kStatic, "equals", "([Ljava/lang/Object;[Ljava/lang/Object;)Z"),
            method(kPublic | kStatic, "fill", "([BB)V"),
            method(kPublic | kStatic, "fill", "([BIIB)V"),
            method(kPublic | kStatic, "fill", "([II)V"),
            method(kPublic | kStatic, "fill", "([IIII)V"),
            method(kPublic | kStatic, "fill", "([SS)V"),
            method(kPublic | kStatic, "fill", "([SIIS)V"),
            method(kPublic | kStatic, "fill", "([ZZ)V"),
            method(kPublic | kStatic, "fill", "([ZIIZ)V"),
            method(kPublic | kStatic, "fill", "([Ljava/lang/Object;Ljava/lang/Object;)V"),
            method(kPublic | kStatic, "fill", "([Ljava/lang/Object;IILjava/lang/Object;)V"),
            method(kPublic | kStatic, "copyOf", "([BI)[B"),
            method(kPublic | kStatic, "copyOf", "([II)[I"),
            method(kPublic | kStatic, "copyOf", "([SI)[S"),
            method(kPublic | kStatic, "copyOf", "([ZI)[Z"),
            method(kPublic | kStatic, "copyOf", "([Ljava/lang/Object;I)[Ljava/lang/Object;"),
            method(kPublic | kStatic, "copyOfRange", "([BII)[B"),
            method(kPublic | kStatic, "copyOfRange", "([III)[I"),
            method(kPublic | kStatic, "sort", "([B)V"),
            method(kPublic | kStatic, "sort", "([I)V"),
            method(kPublic | kStatic, "sort", "([III)V"),
            method(kPublic | kStatic, "binarySearch", "([II)I"),
            method(kPublic | kStatic, "toString", "([B)Ljava/lang/String;"),
            method(kPublic | kStatic, "toString", "([I)Ljava/lang/String;"),
            method(kPublic | kStatic, "toString", "([S)Ljava/lang/String;"),
            method(kPublic | kStatic, "toString", "([Ljava/lang/Object;)Ljava/lang/String;"),
            method(kPublic | kStatic, "asList", "([Ljava/lang/Object;)Ljava/util/List;"),
        });
    }
    if (name == "java/util/Collections") {
        return make_class("java/util/Collections", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic | kStatic, "swap", "(Ljava/util/List;II)V"),
            method(kPublic | kStatic, "reverse", "(Ljava/util/List;)V"),
            method(kPublic | kStatic, "shuffle", "(Ljava/util/List;)V"),
            method(kPublic | kStatic, "fill", "(Ljava/util/List;Ljava/lang/Object;)V"),
            method(kPublic | kStatic, "sort", "(Ljava/util/List;)V"),
            method(kPublic | kStatic, "emptyList", "()Ljava/util/List;"),
            method(kPublic | kStatic, "singletonList", "(Ljava/lang/Object;)Ljava/util/List;"),
            method(kPublic | kStatic, "singleton", "(Ljava/lang/Object;)Ljava/util/Set;"),
            method(kPublic | kStatic, "unmodifiableList", "(Ljava/util/List;)Ljava/util/List;"),
            method(kPublic | kStatic, "unmodifiableMap", "(Ljava/util/Map;)Ljava/util/Map;"),
            method(kPublic | kStatic, "unmodifiableSet", "(Ljava/util/Set;)Ljava/util/Set;"),
            method(kPublic | kStatic, "emptyMap", "()Ljava/util/Map;"),
            method(kPublic | kStatic, "emptySet", "()Ljava/util/Set;"),
            method(kPublic | kStatic, "newSetFromMap", "(Ljava/util/Map;)Ljava/util/Set;"),
            method(kPublic | kStatic, "addAll", "(Ljava/util/Collection;[Ljava/lang/Object;)Z"),
        });
    }
    if (name == "java/util/Optional") {
        return make_class("java/util/Optional", "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kFinal, "value", "Ljava/lang/Object;"),
            field(kPrivate | kFinal, "present", "Z"),
        }, {
            method(kPrivate, "<init>", "(Ljava/lang/Object;Z)V"),
            method(kPublic | kStatic, "empty", "()Ljava/util/Optional;"),
            method(kPublic | kStatic, "of", "(Ljava/lang/Object;)Ljava/util/Optional;"),
            method(kPublic | kStatic, "ofNullable", "(Ljava/lang/Object;)Ljava/util/Optional;"),
            method(kPublic, "isPresent", "()Z"),
            method(kPublic, "get", "()Ljava/lang/Object;"),
            method(kPublic, "orElse", "(Ljava/lang/Object;)Ljava/lang/Object;"),
        });
    }
    if (name == "java/util/Base64") {
        return make_class("java/util/Base64", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic | kStatic, "getEncoder", "()Ljava/util/Base64$Encoder;"),
            method(kPublic | kStatic, "getDecoder", "()Ljava/util/Base64$Decoder;"),
        });
    }
    if (name == "java/util/Base64$Encoder") {
        return make_class("java/util/Base64$Encoder", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic, "encode", "([B)[B"),
            method(kPublic, "encodeToString", "([B)Ljava/lang/String;"),
        });
    }
    if (name == "java/util/Base64$Decoder") {
        return make_class("java/util/Base64$Decoder", "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic, "decode", "([B)[B"),
            method(kPublic, "decode", "(Ljava/lang/String;)[B"),
        });
    }
    return nullptr;
}

} // namespace

void register_headless_compat_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_headless_compat_class);
}

} // namespace phoneme::vm
