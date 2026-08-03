#include <cstdlib>
#include <iostream>
#include <memory>

#include "GameApiNatives.hpp"
#include "GraphicsNatives.hpp"
#include "ImageNatives.hpp"
#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::network {

std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return {};
}

} // namespace phoneme::network

namespace phoneme::vm {

void register_core_natives(NativeMethodRegistry& registry) {
    register_game_api_natives(registry);
    register_graphics_natives(registry);
    register_image_natives(registry);
}

} // namespace phoneme::vm

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: GameApiVmTests <fixture.jar>");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(argv[1]).has_value(),
            "add Game API fixture archive");
    phoneme::vm::Machine machine(classes);
    auto result = machine.invoke_static("corefixture/GameApiOps",
                                        "run",
                                        "()I",
                                        {},
                                        80'000'000);
    if (!result) {
        std::cerr << "VM invoke error: " << result.error().message << '\n';
    }
    require(result.has_value(), "invoke Game API fixture through VM");
    if (result->throwable.has_value()) {
        auto class_name = machine.heap().class_name(*result->throwable);
        if (class_name) std::cerr << "Throwable: " << *class_name << '\n';
    }
    require(result->completed_normally() && result->return_value.has_value(),
            "Game API fixture completes normally");
    auto status = result->return_value->as_int();
    if (status) std::cerr << "Fixture status: " << *status << '\n';
    require(status.has_value() && *status == 0,
            "Layer Sprite TiledLayer and LayerManager semantics");

    std::cout << "Game API VM tests passed\n";
    return 0;
}
