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
        std::exit(1);
    }
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "expected fixture JAR path");

    phoneme::vm::ClassRepository classes;
    auto added = classes.add_archive(argv[1]);
    require(added.has_value(), "failed to add fixture archive");

    phoneme::vm::Machine machine(classes);
    require_java_zero(machine, "corefixture/CldcLibraryOps", "runAll");
    require_java_zero(machine, "corefixture/TimerOps", "run");

    std::cout << "CldcLibraryTests passed\n";
    return 0;
}
