#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::network {

std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return {};
}

} // namespace phoneme::network

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CldcLibraryTests failure: " << message << '\n';
        std::exit(1);
    }
}

void require_java_zero(phoneme::vm::Machine& machine,
                       std::string_view class_name,
                       std::string_view method_name) {
    auto result = machine.invoke_static(class_name, method_name, "()I");
    if (!result) {
        std::cerr << class_name << '.' << method_name
                  << " native error: " << result.error().message << '\n';
        std::exit(1);
    }
    if (result->throwable.has_value()) {
        auto thrown = machine.heap().class_name(*result->throwable);
        std::cerr << class_name << '.' << method_name
                  << " threw " << (thrown ? *thrown : "<stale>") << '\n';
        std::exit(1);
    }
    require(result->return_value.has_value(),
            "fixture returned no int value");
    auto value = result->return_value->as_int();
    require(value.has_value(), "fixture result was not int");
    if (*value != 0) {
        std::cerr << class_name << '.' << method_name
                  << " returned failure code " << *value << '\n';
        if (!machine.console_output().empty()) {
            std::cerr << "Java console: ";
            for (const char16_t character : machine.console_output()) {
                std::cerr << (character <= 0x7FU
                    ? static_cast<char>(character) : '?');
            }
            std::cerr << '\n';
        }
        std::exit(1);
    }
}

int invoke_legacy_chain_link(phoneme::vm::Machine& machine,
                             phoneme::vm::ObjectRef root,
                             phoneme::vm::ObjectRef value) {
    const std::array<phoneme::vm::Value, 2> arguments {
        phoneme::vm::Value::from_reference(root),
        phoneme::vm::Value::from_reference(value),
    };
    auto result = machine.natives().invoke(
        machine,
        "java/lang/System",
        "linkLegacyWtChain",
        "(Ljava/lang/Object;Ljava/lang/Object;)Z",
        arguments);
    require(result.has_value(), "legacy chain linker native failed");
    require(result->has_value(), "legacy chain linker returned no value");
    auto linked = result->value().as_int();
    require(linked.has_value(), "legacy chain linker returned non-boolean");
    return *linked;
}

void test_legacy_chain_linker(phoneme::vm::Machine& machine) {
    auto first = machine.class_states().allocate_instance(machine.heap(), "wt");
    auto second = machine.class_states().allocate_instance(machine.heap(), "wt");
    auto target = machine.class_states().allocate_instance(machine.heap(), "wt");
    require(first.has_value() && second.has_value() && target.has_value(),
            "failed to allocate legacy chain nodes");

    auto link_field = machine.class_states().resolve_field(
        "wt", "c", "Lwt;", false);
    require(link_field.has_value(), "legacy chain field was not resolved");
    auto seeded = machine.heap().set_field(
        *first,
        link_field->index,
        phoneme::vm::Value::from_reference(*second));
    require(seeded.has_value(), "failed to seed legacy chain");

    require(invoke_legacy_chain_link(machine, *first, *target) == 1,
            "legacy chain was not linked");
    auto linked = machine.heap().field(*second, link_field->index);
    require(linked.has_value(), "failed to read linked legacy node");
    auto linked_reference = linked->as_reference();
    require(linked_reference.has_value() && *linked_reference == *target,
            "legacy chain target was stored in the wrong node");
    require(invoke_legacy_chain_link(machine, *first, *target) == 1,
            "existing legacy chain target was not detected");

    auto ordinary = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/Object");
    require(ordinary.has_value(), "failed to allocate ordinary object");
    require(invoke_legacy_chain_link(machine, *ordinary, *target) == 0,
            "object without c:Lwt; field was accepted");
    require(invoke_legacy_chain_link(machine, {}, *target) == 0,
            "null legacy chain root was accepted");
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "expected fixture JAR path");

    phoneme::vm::ClassRepository classes;
    auto added = classes.add_archive(argv[1]);
    require(added.has_value(), "failed to add fixture archive");

    phoneme::vm::Machine machine(classes);
    test_legacy_chain_linker(machine);
    require_java_zero(machine, "corefixture/CldcLibraryOps", "runAll");
    machine.set_system_property(u"microedition.profiles", u"MIDP-2.1");
    machine.set_system_property(u"microedition.configuration", u"CLDC-1.1.1");
    require_java_zero(machine, "corefixture/CldcLibraryOps",
                      "versionProperties");
    require_java_zero(machine, "corefixture/TimerOps", "run");

    require_java_zero(machine, "corefixture/WeakReferenceOps", "explicitClear");
    require_java_zero(machine, "corefixture/WeakReferenceOps", "createWeakOnly");
    auto weak_collection = machine.collect_garbage();
    require(weak_collection.has_value(), "weak-reference collection failed");
    require_java_zero(machine, "corefixture/WeakReferenceOps", "expectCleared");

    require_java_zero(machine, "corefixture/WeakReferenceOps", "createStrong");
    auto strong_collection = machine.collect_garbage();
    require(strong_collection.has_value(), "strong-reference collection failed");
    require_java_zero(machine, "corefixture/WeakReferenceOps", "expectStrong");
    require_java_zero(machine, "corefixture/WeakReferenceOps", "releaseStrong");
    auto released_collection = machine.collect_garbage();
    require(released_collection.has_value(), "released-reference collection failed");
    require_java_zero(machine, "corefixture/WeakReferenceOps", "expectCleared");

    std::cout << "CldcLibraryTests passed\n";
    return 0;
}
