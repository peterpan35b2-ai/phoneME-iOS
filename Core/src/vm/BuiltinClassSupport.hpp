#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::vm::builtin {

constexpr u16 kPublic = 0x0001U;
constexpr u16 kPrivate = 0x0002U;
constexpr u16 kProtected = 0x0004U;
constexpr u16 kStatic = 0x0008U;
constexpr u16 kFinal = 0x0010U;
constexpr u16 kSuper = 0x0020U;
constexpr u16 kSynchronized = 0x0020U;
constexpr u16 kInterface = 0x0200U;
constexpr u16 kAbstract = 0x0400U;
constexpr u16 kOrdinary = kPublic | kSuper;

using ClassPtr = std::shared_ptr<const classfile::ClassFile>;

[[nodiscard]] inline classfile::Field field(u16 access_flags,
                                            const char* name,
                                            const char* descriptor) {
    return classfile::Field {
        .access_flags = access_flags,
        .name = name,
        .descriptor = descriptor,
        .constant_value_index = std::nullopt,
    };
}

[[nodiscard]] inline classfile::Method method(u16 access_flags,
                                              const char* name,
                                              const char* descriptor) {
    return classfile::Method {
        .access_flags = access_flags,
        .name = name,
        .descriptor = descriptor,
        .code = std::nullopt,
    };
}

[[nodiscard]] inline std::vector<classfile::Method> text_builder_methods(
    std::string_view class_name,
    bool synchronized) {
    const u16 instance_flags = kPublic | (synchronized ? kSynchronized : 0U);
    const std::string return_descriptor = "L" + std::string(class_name) + ";";
    const auto builder_method = [&](const char* name,
                                    std::string descriptor) {
        return classfile::Method {
            .access_flags = instance_flags,
            .name = name,
            .descriptor = std::move(descriptor),
            .code = std::nullopt,
        };
    };
    return {
        builder_method("<init>", "()V"),
        builder_method("<init>", "(I)V"),
        builder_method("<init>", "(Ljava/lang/String;)V"),
        builder_method("append", "(Ljava/lang/String;)" + return_descriptor),
        builder_method("append", "(Ljava/lang/Object;)" + return_descriptor),
        builder_method("append", "(Ljava/lang/CharSequence;)" + return_descriptor),
        builder_method("append", "(Ljava/lang/CharSequence;II)" + return_descriptor),
        builder_method("append", "(Z)" + return_descriptor),
        builder_method("append", "(C)" + return_descriptor),
        builder_method("append", "(I)" + return_descriptor),
        builder_method("append", "(J)" + return_descriptor),
        builder_method("append", "(F)" + return_descriptor),
        builder_method("append", "(D)" + return_descriptor),
        builder_method("append", "([C)" + return_descriptor),
        builder_method("append", "([CII)" + return_descriptor),
        builder_method("length", "()I"),
        builder_method("capacity", "()I"),
        builder_method("charAt", "(I)C"),
        builder_method("subSequence", "(II)Ljava/lang/CharSequence;"),
        builder_method("setCharAt", "(IC)V"),
        builder_method("getChars", "(II[CI)V"),
        builder_method("ensureCapacity", "(I)V"),
        builder_method("setLength", "(I)V"),
        builder_method("delete", "(II)" + return_descriptor),
        builder_method("deleteCharAt", "(I)" + return_descriptor),
        builder_method("insert", "(ILjava/lang/String;)" + return_descriptor),
        builder_method("insert", "(ILjava/lang/Object;)" + return_descriptor),
        builder_method("insert", "(IZ)" + return_descriptor),
        builder_method("insert", "(IC)" + return_descriptor),
        builder_method("insert", "(II)" + return_descriptor),
        builder_method("insert", "(IJ)" + return_descriptor),
        builder_method("insert", "(IF)" + return_descriptor),
        builder_method("insert", "(ID)" + return_descriptor),
        builder_method("insert", "(I[C)" + return_descriptor),
        builder_method("reverse", "()" + return_descriptor),
        builder_method("toString", "()Ljava/lang/String;"),
    };
}

[[nodiscard]] inline ClassPtr make_class(
    std::string name,
    std::string super_name,
    u16 access_flags,
    std::vector<classfile::Field> fields = {},
    std::vector<classfile::Method> methods = {},
    std::vector<std::string> interfaces = {}) {
    return std::make_shared<const classfile::ClassFile>(
        classfile::ClassFile::builtin(std::move(name),
                                      std::move(super_name),
                                      access_flags,
                                      std::move(fields),
                                      std::move(methods),
                                      std::move(interfaces)));
}

} // namespace phoneme::vm::builtin
