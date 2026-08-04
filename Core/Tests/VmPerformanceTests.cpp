#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/Heap.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"
#include "phoneme/vm/RuntimeMetadata.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

} // namespace

int main(int argc, char** argv) {
    using phoneme::classfile::ClassFile;
    using phoneme::classfile::Method;
    using phoneme::vm::Heap;
    using phoneme::vm::ObjectRef;
    using phoneme::vm::PerformanceCounters;
    using phoneme::vm::RuntimeMetadata;
    using phoneme::vm::Value;

    require(argc == 2, "usage: VmPerformanceTests <output.json>");
    require(PerformanceCounters::enabled(),
            "VM performance test requires profiling enabled");
    PerformanceCounters::reset();

    std::vector<Method> methods;
    methods.reserve(128U);
    for (std::size_t index = 0; index < 128U; ++index) {
        methods.push_back(Method {
            .access_flags = 0x0009U,
            .name = "method" + std::to_string(index),
            .descriptor = index % 2U == 0U ? "(II)I" : "(JLjava/lang/Object;)J",
            .code = std::nullopt,
        });
    }
    auto class_file = std::make_shared<const ClassFile>(ClassFile::builtin(
        "benchmark/Metadata",
        "java/lang/Object",
        0x0001U,
        {},
        std::move(methods)));

    RuntimeMetadata metadata;
    auto published_class = metadata.publish_class(class_file);
    require(published_class.has_value(), "publish benchmark class");
    for (const Method& candidate : class_file->methods()) {
        require(metadata.publish_method(class_file, candidate).has_value(),
                "publish benchmark method");
    }

    Heap heap(phoneme::vm::HeapLimits {
        .maximum_objects = 4'096U,
        .maximum_bytes = 16U * 1024U * 1024U,
    });
    std::vector<ObjectRef> roots;
    roots.reserve(32U);

    constexpr std::size_t kLookupIterations = 400'000U;
    constexpr std::size_t kDescriptorIterations = 200'000U;
    constexpr std::size_t kAllocationIterations = 1'000U;
    std::size_t lookup_checksum = 0U;

    const auto started = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < kLookupIterations; ++iteration) {
        const std::size_t method_index = iteration & 127U;
        const std::string name = "method" + std::to_string(method_index);
        const std::string_view descriptor = method_index % 2U == 0U
            ? std::string_view("(II)I")
            : std::string_view("(JLjava/lang/Object;)J");
        const Method* found = class_file->find_method(name, descriptor);
        require(found != nullptr, "benchmark indexed method lookup");
        lookup_checksum += found->name.size();
    }
    for (std::size_t iteration = 0;
         iteration < kDescriptorIterations;
         ++iteration) {
        const std::string_view descriptor = iteration % 2U == 0U
            ? std::string_view("(II)I")
            : std::string_view("(JLjava/lang/Object;)J");
        auto cached = metadata.method_descriptor(descriptor);
        require(cached.has_value(), "benchmark descriptor cache lookup");
        lookup_checksum += (*cached)->argument_slots_without_receiver;
    }
    for (std::size_t iteration = 0;
         iteration < kAllocationIterations;
         ++iteration) {
        auto array = heap.allocate_array("[I", 64U, Value::from_int(0));
        require(array.has_value(), "benchmark array allocation");
        require(heap.set_element(*array,
                                 iteration & 63U,
                                 Value::from_int(
                                     static_cast<phoneme::i32>(iteration)))
                    .has_value(),
                "benchmark array store");
        if (iteration % 50U == 0U) roots.push_back(*array);
    }
    require(heap.collect(roots).has_value(), "benchmark garbage collection");
    const auto finished = std::chrono::steady_clock::now();

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        finished - started).count();
    const auto counters = PerformanceCounters::snapshot();
    const auto heap_stats = heap.stats();

    const std::filesystem::path output_path(argv[1]);
    std::error_code directory_error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(
            output_path.parent_path(), directory_error);
    }
    require(!directory_error, "create benchmark output directory");
    std::ofstream output(output_path, std::ios::trunc);
    require(output.good(), "open benchmark output");
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"benchmark\": \"synthetic_metadata_heap\",\n"
           << "  \"elapsed_nanoseconds\": "
           << (elapsed > 0 ? elapsed : 0) << ",\n"
           << "  \"lookup_iterations\": " << kLookupIterations << ",\n"
           << "  \"descriptor_iterations\": "
           << kDescriptorIterations << ",\n"
           << "  \"allocation_iterations\": "
           << kAllocationIterations << ",\n"
           << "  \"checksum\": " << lookup_checksum << ",\n"
           << "  \"descriptor_cache_hits\": "
           << counters.descriptor_cache_hits << ",\n"
           << "  \"descriptor_cache_misses\": "
           << counters.descriptor_cache_misses << ",\n"
           << "  \"locked_heap_operations\": "
           << counters.public_locked_heap_operations << ",\n"
           << "  \"gc_count\": " << counters.gc_count << ",\n"
           << "  \"gc_max_pause_nanoseconds\": "
           << counters.gc_max_pause_nanoseconds << ",\n"
           << "  \"gc_objects_reclaimed\": "
           << counters.gc_objects_reclaimed << ",\n"
           << "  \"live_objects_after_gc\": "
           << heap_stats.live_objects << "\n"
           << "}\n";
    require(output.good(), "write benchmark output");

    std::cout << "VM performance benchmark passed\n";
    return 0;
}
