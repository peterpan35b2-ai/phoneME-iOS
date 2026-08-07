#include "phoneme/vm/NativeMethodRegistry.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"

namespace phoneme::vm {
namespace {

class ProcessNativeCoverage final {
public:
    ProcessNativeCoverage() {
        const char* configured = std::getenv("PHONEME_NATIVE_COVERAGE");
        if (configured != nullptr && *configured != '\0') {
            output_path_ = configured;
        }
    }

    ProcessNativeCoverage(const ProcessNativeCoverage&) = delete;
    ProcessNativeCoverage& operator=(const ProcessNativeCoverage&) = delete;

    void flush() noexcept {
        if (output_path_.empty()) return;
        const std::filesystem::path path(output_path_);
        std::error_code directory_error;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(
                path.parent_path(), directory_error);
        }
        if (directory_error) {
            std::fprintf(stderr,
                         "cannot create native coverage directory: %s\n",
                         directory_error.message().c_str());
            return;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::fprintf(stderr,
                         "cannot open native coverage file: %s\n",
                         output_path_.c_str());
            return;
        }
        output << "owner\tname\tdescriptor\tinvocations\n";
        std::scoped_lock lock(mutex_);
        for (const auto& [signature, count] : counts_) {
            const auto& [owner, name, descriptor] = signature;
            output << owner << '\t' << name << '\t' << descriptor
                   << '\t' << count << '\n';
        }
    }

    void record(const NativeMethodSignature& signature) {
        if (output_path_.empty()) return;
        std::scoped_lock lock(mutex_);
        auto& count = counts_[std::tuple {
            signature.owner, signature.name, signature.descriptor,
        }];
        if (count != std::numeric_limits<std::size_t>::max()) ++count;
    }

private:
    std::string output_path_;
    std::mutex mutex_;
    std::map<std::tuple<std::string, std::string, std::string>, std::size_t>
        counts_;
};

ProcessNativeCoverage& process_native_coverage() {
    // Intentionally process-lifetime storage: VM worker shutdown can still
    // record a final native call while ordinary function-static destructors are
    // running. Leaking this tiny optional telemetry object keeps its mutex and
    // map valid until the OS tears down the process.
    static ProcessNativeCoverage* coverage = [] {
        auto* instance = new ProcessNativeCoverage();
        std::atexit([] {
            process_native_coverage().flush();
        });
        return instance;
    }();
    return *coverage;
}

} // namespace

Status NativeMethodRegistry::register_method(
    std::string owner,
    std::string name,
    std::string descriptor,
    NativeMethod implementation,
    NativeJitPolicy jit_policy) {
    if (owner.empty() || name.empty() || descriptor.empty() || !implementation) {
        return fail(ErrorCode::invalid_argument,
                    "native method registration is incomplete");
    }

    PerformanceCounters::record_metadata_key_construction();
    const std::string method_key = key(owner, name, descriptor);
    std::scoped_lock lock(mutex_);
    if (ids_by_key_.contains(method_key)) {
        return fail(ErrorCode::invalid_state,
                    "native method is already registered");
    }
    if (entries_.size() >= static_cast<usize>(
            std::numeric_limits<u32>::max() - 1U)) {
        return fail(ErrorCode::overflow,
                    "native method ID space is exhausted");
    }
    const NativeMethodId method_id {
        static_cast<u32>(entries_.size() + 1U),
    };
    entries_.push_back(Entry {
        .signature = NativeMethodSignature {
            .id = method_id,
            .owner = std::move(owner),
            .name = std::move(name),
            .descriptor = std::move(descriptor),
        },
        .implementation = std::move(implementation),
        .jit_policy = jit_policy,
        .invocation_count = 0U,
    });
    ids_by_key_.emplace(method_key, method_id);
    generation_ = generation_ == std::numeric_limits<u64>::max()
        ? 1U
        : generation_ + 1U;
    return {};
}

Status NativeMethodRegistry::register_alias(
    std::string_view source_owner,
    std::string_view source_name,
    std::string_view source_descriptor,
    std::string target_owner,
    std::string target_name,
    std::string target_descriptor) {
    if (source_owner.empty() || source_name.empty() ||
        source_descriptor.empty() || target_owner.empty() ||
        target_name.empty() || target_descriptor.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "native method alias registration is incomplete");
    }

    PerformanceCounters::record_metadata_key_construction();
    const std::string source_key = key(source_owner, source_name,
                                       source_descriptor);
    const std::string target_key = key(target_owner, target_name,
                                       target_descriptor);
    std::scoped_lock lock(mutex_);
    const auto source = ids_by_key_.find(source_key);
    if (source == ids_by_key_.end()) {
        return fail(ErrorCode::class_not_found,
                    "native method alias source is not registered");
    }
    if (ids_by_key_.contains(target_key)) {
        return fail(ErrorCode::invalid_state,
                    "native method alias target is already registered");
    }
    const usize source_index =
        static_cast<usize>(source->second.value - 1U);
    if (source_index >= entries_.size()) {
        return fail(ErrorCode::invalid_state,
                    "native method alias source ID is invalid");
    }
    if (entries_.size() >= static_cast<usize>(
            std::numeric_limits<u32>::max() - 1U)) {
        return fail(ErrorCode::overflow,
                    "native method ID space is exhausted");
    }
    const NativeMethodId method_id {
        static_cast<u32>(entries_.size() + 1U),
    };
    entries_.push_back(Entry {
        .signature = NativeMethodSignature {
            .id = method_id,
            .owner = std::move(target_owner),
            .name = std::move(target_name),
            .descriptor = std::move(target_descriptor),
        },
        .implementation = entries_[source_index].implementation,
        .jit_policy = entries_[source_index].jit_policy,
        .invocation_count = 0U,
    });
    ids_by_key_.emplace(target_key, method_id);
    generation_ = generation_ == std::numeric_limits<u64>::max()
        ? 1U
        : generation_ + 1U;
    return {};
}

NativeMethodId NativeMethodRegistry::resolve(
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) const noexcept {
    return resolve_binding(owner, name, descriptor).id;
}

NativeMethodBinding NativeMethodRegistry::resolve_binding(
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor) const noexcept {
    PerformanceCounters::record_native_registry_lookup();
    PerformanceCounters::record_metadata_key_construction();
    std::scoped_lock lock(mutex_);
    const auto found = ids_by_key_.find(key(owner, name, descriptor));
    return NativeMethodBinding {
        .id = found == ids_by_key_.end() ? NativeMethodId {} : found->second,
        .generation = generation_,
    };
}

bool NativeMethodRegistry::contains(std::string_view owner,
                                    std::string_view name,
                                    std::string_view descriptor) const noexcept {
    return resolve(owner, name, descriptor).valid();
}

NativeJitPolicy NativeMethodRegistry::jit_policy(
    NativeMethodId method_id) const noexcept {
    if (!method_id.valid()) return NativeJitPolicy::conservative;
    std::scoped_lock lock(mutex_);
    const usize index = static_cast<usize>(method_id.value - 1U);
    if (index >= entries_.size() || entries_[index].signature.id != method_id) {
        return NativeJitPolicy::conservative;
    }
    return entries_[index].jit_policy;
}

Result<std::optional<Value>> NativeMethodRegistry::invoke(
    Machine& machine,
    NativeMethodId method_id,
    std::span<const Value> arguments) const {
    if (!method_id.valid()) {
        return fail(ErrorCode::unsupported_feature,
                    "native method is not ported");
    }

    NativeMethod implementation;
    NativeMethodSignature signature;
    {
        std::scoped_lock lock(mutex_);
        const usize index = static_cast<usize>(method_id.value - 1U);
        if (index >= entries_.size() || entries_[index].signature.id != method_id) {
            return fail(ErrorCode::unsupported_feature,
                        "native method ID is stale or invalid");
        }
        Entry& entry = entries_[index];
        implementation = entry.implementation;
        signature = entry.signature;
        ++entry.invocation_count;
    }
    PerformanceCounters::record_native_invocation();
    process_native_coverage().record(signature);
    auto result = implementation(machine, arguments);
    if (!result) {
        Error error = result.error();
        // Java-visible exception messages are part of the language/API
        // contract. Keep them byte-for-byte intact; only internal native
        // failures receive the method context prefix used for diagnostics.
        if (error.java_exception_class.empty()) {
            error.message = signature.owner + "." + signature.name +
                            signature.descriptor + ": " + error.message;
        }
        return std::unexpected(std::move(error));
    }
    return result;
}

Result<std::optional<Value>> NativeMethodRegistry::invoke(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments) const {
    const NativeMethodId method_id = resolve(owner, name, descriptor);
    if (!method_id.valid()) {
        return fail(ErrorCode::unsupported_feature,
                    "native method is not ported: " + std::string(owner) +
                        "." + std::string(name) + std::string(descriptor));
    }
    return invoke(machine, method_id, arguments);
}

std::vector<NativeMethodSignature>
NativeMethodRegistry::registered_methods() const {
    std::scoped_lock lock(mutex_);
    std::vector<NativeMethodSignature> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        result.push_back(entry.signature);
    }
    std::ranges::sort(result, {}, [](const NativeMethodSignature& signature) {
        return std::tie(signature.owner, signature.name, signature.descriptor);
    });
    return result;
}

std::vector<NativeMethodInvocationCount>
NativeMethodRegistry::invocation_counts() const {
    std::scoped_lock lock(mutex_);
    std::vector<NativeMethodInvocationCount> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        result.push_back(NativeMethodInvocationCount {
            .signature = entry.signature,
            .count = entry.invocation_count,
        });
    }
    std::ranges::sort(result, {},
        [](const NativeMethodInvocationCount& entry) {
            return std::tie(entry.signature.owner,
                            entry.signature.name,
                            entry.signature.descriptor);
        });
    return result;
}

void NativeMethodRegistry::reset_invocation_counts() noexcept {
    std::scoped_lock lock(mutex_);
    for (Entry& entry : entries_) {
        entry.invocation_count = 0U;
    }
}

u64 NativeMethodRegistry::generation() const noexcept {
    std::scoped_lock lock(mutex_);
    return generation_;
}

void NativeMethodRegistry::clear() noexcept {
    std::scoped_lock lock(mutex_);
    ids_by_key_.clear();
    entries_.clear();
    generation_ = generation_ == std::numeric_limits<u64>::max()
        ? 1U
        : generation_ + 1U;
}

std::string NativeMethodRegistry::key(std::string_view owner,
                                      std::string_view name,
                                      std::string_view descriptor) {
    std::string result;
    result.reserve(owner.size() + name.size() + descriptor.size() + 2);
    result.append(owner);
    result.push_back('#');
    result.append(name);
    result.push_back(':');
    result.append(descriptor);
    return result;
}

} // namespace phoneme::vm
