#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/RuntimeMetadata.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

phoneme::classfile::Method method(std::string name,
                                  std::string descriptor) {
    return phoneme::classfile::Method {
        .access_flags = 0x0009U,
        .name = std::move(name),
        .descriptor = std::move(descriptor),
        .code = std::nullopt,
    };
}

} // namespace

int main() {
    using phoneme::classfile::ClassFile;
    using phoneme::vm::RuntimeMetadata;

    std::vector<phoneme::classfile::Method> methods;
    methods.push_back(method("sum", "(II)I"));
    methods.push_back(method("wide", "(IDLjava/lang/Object;)J"));
    methods.push_back(method("empty", "()V"));
    methods.push_back(phoneme::classfile::Method {
        .access_flags = 0x0009U,
        .name = "coded",
        .descriptor = "()I",
        .code = phoneme::classfile::CodeAttribute {
            .max_stack = 1U,
            .max_locals = 0U,
            .bytecode = {0x04U, 0xACU},
        },
    });

    auto class_file = std::make_shared<const ClassFile>(ClassFile::builtin(
        "test/Metadata",
        "java/lang/Object",
        0x0001U,
        {},
        std::move(methods)));

    const auto* sum = class_file->find_method("sum", "(II)I");
    const auto* wide = class_file->find_method(
        "wide", "(IDLjava/lang/Object;)J");
    const auto* coded = class_file->find_method("coded", "()I");
    require(sum != nullptr, "indexed method lookup finds sum");
    require(wide != nullptr, "indexed method lookup finds wide");
    require(coded != nullptr, "indexed method lookup finds coded method");
    require(class_file->find_method("missing", "()V") == nullptr,
            "indexed method lookup rejects missing method");

    RuntimeMetadata metadata;
    const auto initial_generation = metadata.generation();
    auto object_class = std::make_shared<const ClassFile>(ClassFile::builtin(
        "java/lang/Object", "", 0x0001U, {}, {}));
    auto runtime_object = metadata.publish_class(object_class);
    require(runtime_object.has_value(), "publish runtime Object class");
    auto runtime_class = metadata.publish_class(class_file);
    require(runtime_class.has_value(), "publish runtime class");
    require((*runtime_class)->id.valid(), "runtime class has valid ID");
    require((*runtime_class)->class_file.get() == class_file.get(),
            "runtime class retains stable class file");
    require((*runtime_class)->super_name == "java/lang/Object" &&
                (*runtime_class)->super_id == (*runtime_object)->id,
            "runtime class retains superclass metadata");
    require((*runtime_class)->interface_names.empty() &&
                (*runtime_class)->interface_ids.empty(),
            "runtime class retains interface metadata");

    auto sum_metadata = metadata.publish_method(class_file, *sum);
    auto wide_metadata = metadata.publish_method(class_file, *wide);
    auto coded_metadata = metadata.publish_method(class_file, *coded);
    require(sum_metadata.has_value(), "publish sum method");
    require(wide_metadata.has_value(), "publish wide method");
    require(coded_metadata.has_value(), "publish coded method");
    require((*sum_metadata)->id.valid() && (*wide_metadata)->id.valid(),
            "runtime methods have valid IDs");
    require((*sum_metadata)->id != (*wide_metadata)->id,
            "runtime method IDs are unique");
    require((*sum_metadata)->decoded == nullptr,
            "method without Code has no decoded body");
    require((*coded_metadata)->decoded != nullptr &&
                (*coded_metadata)->decoded->method_id == (*coded_metadata)->id &&
                (*coded_metadata)->decoded->instructions.size() == 2U,
            "runtime method publishes immutable decoded bytecode");
    require(metadata.publish_method(class_file, *sum).value().get() ==
                sum_metadata->get(),
            "publishing a method twice reuses metadata");

    auto descriptor = metadata.method_descriptor(
        "(IDLjava/lang/Object;)J");
    auto descriptor_again = metadata.method_descriptor(
        "(IDLjava/lang/Object;)J");
    require(descriptor.has_value() && descriptor_again.has_value(),
            "cache parsed method descriptor");
    require(descriptor->get() == descriptor_again->get(),
            "descriptor cache returns stable object");
    require((*descriptor)->argument_values == 3U,
            "descriptor caches argument value count");
    require((*descriptor)->argument_slots_without_receiver == 4U,
            "descriptor caches category-two argument slots");
    require((*descriptor)->argument_slots_with_receiver == 5U,
            "descriptor caches receiver slot");
    require((*descriptor)->returns_category_two,
            "descriptor caches category-two return kind");

    require(metadata.find_class("test/Metadata").get() ==
                runtime_class->get(),
            "find class by internal name");
    require(metadata.find_class((*runtime_class)->id).get() ==
                runtime_class->get(),
            "find class by stable ClassId");
    require(metadata.find_method(class_file.get(), sum).get() ==
                sum_metadata->get(),
            "find method by stable pointers");
    require(metadata.find_method((*sum_metadata)->id).get() ==
                sum_metadata->get(),
            "find method by stable MethodId");

    metadata.clear();
    require(metadata.generation() != initial_generation,
            "clear advances metadata generation");
    require(metadata.find_class("test/Metadata") == nullptr,
            "clear invalidates class publication");
    require(metadata.find_method(class_file.get(), sum) == nullptr,
            "clear invalidates method publication");

    std::cout << "Runtime metadata tests passed\n";
    return 0;
}
