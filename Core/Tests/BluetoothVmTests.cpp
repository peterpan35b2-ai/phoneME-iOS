#include <cstdlib>
#include <iostream>
#include <memory>

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
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: BluetoothVmTests <fixture.jar>");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(argv[1]).has_value(),
            "add Bluetooth fixture archive");
    phoneme::vm::Machine machine(classes);
    auto result = machine.invoke_static("corefixture/BluetoothOps",
                                        "run",
                                        "()I",
                                        {},
                                        20'000'000);
    if (!result) {
        std::cerr << "VM invoke error: " << result.error().message << '\n';
    }
    require(result.has_value(), "invoke Bluetooth fixture through VM");
    if (result->throwable.has_value()) {
        auto class_name = machine.heap().class_name(*result->throwable);
        if (class_name) {
            std::cerr << "Throwable: " << *class_name;
            if (!result->exception_context.empty()) {
                std::cerr << " from " << result->exception_context;
            }
            std::cerr << '\n';
        }
    }
    require(result->completed_normally() && result->return_value.has_value(),
            "Bluetooth fixture completes normally");
    auto status = result->return_value->as_int();
    if (status) std::cerr << "Fixture status: " << *status << '\n';
    require(status.has_value() && *status == 0,
            "JSR-82 UUID, DataElement and discovery semantics");

    std::cout << "Bluetooth VM tests passed\n";
    return 0;
}
