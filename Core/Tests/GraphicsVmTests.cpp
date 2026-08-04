#include <cstdlib>
#include <iostream>
#include <memory>

#include "ConnectionNatives.hpp"
#include "GraphicsNatives.hpp"
#include "ImageNatives.hpp"
#include "IONatives.hpp"
#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MediaEventDispatch.hpp"

namespace phoneme::network {

std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return {};
}

} // namespace phoneme::network

namespace phoneme::vm {

Result<std::optional<i32>> connection_stream_read_one(Machine&, ObjectRef) {
    return std::optional<i32> {};
}

Result<std::optional<i32>> connection_stream_read_range(
    Machine&,
    ObjectRef,
    ObjectRef,
    i32,
    i32) {
    return 0;
}

Result<std::optional<usize>> connection_stream_available(Machine&, ObjectRef) {
    return std::optional<usize> {};
}

Result<std::optional<bool>> connection_stream_write_one(Machine&,
                                                        ObjectRef,
                                                        u8) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_write_bytes(
    Machine&,
    ObjectRef,
    std::span<const u8>) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_flush(Machine&, ObjectRef) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_close_input(Machine&, ObjectRef) {
    return std::optional<bool> {};
}

Result<std::optional<bool>> connection_stream_close_output(Machine&, ObjectRef) {
    return std::optional<bool> {};
}

Status dispatch_media_event(Machine&,
                            ObjectRef,
                            const media::MediaEvent&) {
    return {};
}

void register_core_natives(NativeMethodRegistry& registry) {
    register_graphics_natives(registry);
    register_image_natives(registry);
    register_io_natives(registry);
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
    require(argc == 2, "usage: GraphicsVmTests <fixture.jar>");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(argv[1]).has_value(),
            "add graphics fixture archive");
    phoneme::vm::Machine machine(classes);
    auto result = machine.invoke_static("corefixture/GraphicsOps",
                                        "run",
                                        "()I",
                                        {},
                                        40'000'000);
    if (!result) {
        std::cerr << "VM invoke error: " << result.error().message << '\n';
    }
    require(result.has_value(), "invoke graphics fixture through VM");
    require(result->completed_normally() && result->return_value.has_value(),
            "graphics fixture completes normally");
    auto status = result->return_value->as_int();
    require(status.has_value() && *status == 0,
            "graphics fixture validates native Image Graphics and Font APIs");

    std::cout << "Graphics VM tests passed\n";
    return 0;
}
