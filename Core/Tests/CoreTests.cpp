#include <array>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <zlib.h>

#include "../src/vm/M3gLoader.hpp"
#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/BytecodeVerifier.hpp"
#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/filesystem/FileSystem.hpp"
#include "phoneme/runtime/Framebuffer.hpp"
#include "phoneme/runtime/Runtime.hpp"
#include "phoneme/runtime/SuiteStore.hpp"
#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"
#include "phoneme/vm/ClassLayout.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/Heap.hpp"
#include "phoneme/vm/Interpreter.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MonitorTable.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"
#include "phoneme/vm/SlotStorage.hpp"
#include "phoneme/vm/Verifier.hpp"

#include "FakeNetworkAdapter.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void write_performance_snapshot() {
    const char* output_path = std::getenv("PHONEME_VM_PROFILE_JSON");
    if (output_path == nullptr || *output_path == '\0') return;

    const auto snapshot = phoneme::vm::PerformanceCounters::snapshot();
    const std::filesystem::path path(output_path);
    std::error_code directory_error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), directory_error);
    }
    require(!directory_error, "create VM profile output directory");

    std::ofstream output(path, std::ios::trunc);
    require(output.good(), "open VM profile output");
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"profiling_enabled\": "
           << (phoneme::vm::PerformanceCounters::enabled() ? "true" : "false")
           << ",\n"
           << "  \"interpreter\": {\n"
           << "    \"executed_bytecodes\": " << snapshot.executed_bytecodes << ",\n"
           << "    \"method_invocations\": " << snapshot.method_invocations << ",\n"
           << "    \"native_invocations\": " << snapshot.native_invocations << ",\n"
           << "    \"maximum_java_call_depth\": "
           << snapshot.maximum_java_call_depth << ",\n"
           << "    \"exception_dispatches\": " << snapshot.exception_dispatches << ",\n"
           << "    \"class_initializations\": " << snapshot.class_initializations << ",\n"
           << "    \"instruction_budget_exits\": "
           << snapshot.instruction_budget_exits << ",\n"
           << "    \"scheduler_quanta\": " << snapshot.scheduler_quanta << "\n"
           << "  },\n"
           << "  \"metadata\": {\n"
           << "    \"class_cache_hits\": " << snapshot.class_cache_hits << ",\n"
           << "    \"class_cache_misses\": " << snapshot.class_cache_misses << ",\n"
           << "    \"method_resolution_hits\": "
           << snapshot.method_resolution_hits << ",\n"
           << "    \"method_resolution_misses\": "
           << snapshot.method_resolution_misses << ",\n"
           << "    \"declared_method_resolution_hits\": "
           << snapshot.declared_method_resolution_hits << ",\n"
           << "    \"declared_method_resolution_misses\": "
           << snapshot.declared_method_resolution_misses << ",\n"
           << "    \"field_resolution_hits\": "
           << snapshot.field_resolution_hits << ",\n"
           << "    \"field_resolution_misses\": "
           << snapshot.field_resolution_misses << ",\n"
           << "    \"assignability_cache_hits\": "
           << snapshot.assignability_cache_hits << ",\n"
           << "    \"assignability_cache_misses\": "
           << snapshot.assignability_cache_misses << ",\n"
           << "    \"native_registry_lookups\": "
           << snapshot.native_registry_lookups << ",\n"
           << "    \"metadata_key_constructions\": "
           << snapshot.metadata_key_constructions << ",\n"
           << "    \"virtual_inline_cache_hits\": "
           << snapshot.virtual_inline_cache_hits << ",\n"
           << "    \"virtual_inline_cache_misses\": "
           << snapshot.virtual_inline_cache_misses << ",\n"
           << "    \"direct_call_cache_hits\": "
           << snapshot.direct_call_cache_hits << ",\n"
           << "    \"direct_call_cache_misses\": "
           << snapshot.direct_call_cache_misses << ",\n"
           << "    \"operand_resolution_hits\": "
           << snapshot.operand_resolution_hits << ",\n"
           << "    \"operand_resolution_misses\": "
           << snapshot.operand_resolution_misses << ",\n"
           << "    \"operand_resolution_failures\": "
           << snapshot.operand_resolution_failures << ",\n"
           << "    \"descriptor_cache_hits\": "
           << snapshot.descriptor_cache_hits << ",\n"
           << "    \"descriptor_cache_misses\": "
           << snapshot.descriptor_cache_misses << ",\n"
           << "    \"decoded_methods\": "
           << snapshot.decoded_methods << ",\n"
           << "    \"decoded_instructions\": "
           << snapshot.decoded_instructions << ",\n"
           << "    \"decoded_operands\": "
           << snapshot.decoded_operands << ",\n"
           << "    \"decoded_switch_entries\": "
           << snapshot.decoded_switch_entries << ",\n"
           << "    \"decoded_opcode_dispatches\": "
           << snapshot.decoded_opcode_dispatches << ",\n"
           << "    \"decoded_operand_dispatches\": "
           << snapshot.decoded_operand_dispatches << "\n"
           << "  },\n"
           << "  \"heap\": {\n"
           << "    \"failed_allocations\": " << snapshot.failed_allocations << ",\n"
           << "    \"public_locked_operations\": "
           << snapshot.public_locked_heap_operations << ",\n"
           << "    \"vm_fast_operations\": "
           << snapshot.vm_fast_heap_operations << ",\n"
           << "    \"gc_count\": " << snapshot.gc_count << ",\n"
           << "    \"gc_total_nanoseconds\": "
           << snapshot.gc_total_nanoseconds << ",\n"
           << "    \"gc_max_pause_nanoseconds\": "
           << snapshot.gc_max_pause_nanoseconds << ",\n"
           << "    \"gc_roots_scanned\": " << snapshot.gc_roots_scanned << ",\n"
           << "    \"gc_objects_scanned\": " << snapshot.gc_objects_scanned << ",\n"
           << "    \"gc_objects_reclaimed\": "
           << snapshot.gc_objects_reclaimed << ",\n"
           << "    \"gc_primitive_bytes_scanned\": "
           << snapshot.gc_primitive_bytes_scanned << "\n"
           << "  },\n"
           << "  \"scheduler\": {\n"
           << "    \"state_transitions\": "
           << snapshot.scheduler_state_transitions << ",\n"
           << "    \"queue_erase_scans\": "
           << snapshot.scheduler_queue_erase_scans << ",\n"
           << "    \"yields\": " << snapshot.scheduler_yields << ",\n"
           << "    \"sleeps\": " << snapshot.scheduler_sleeps << ",\n"
           << "    \"event_wakeups\": "
           << snapshot.scheduler_event_wakeups << ",\n"
           << "    \"spurious_wakeups\": "
           << snapshot.scheduler_spurious_wakeups << "\n"
           << "  },\n"
           << "  \"opcode_counts\": [";
    for (std::size_t index = 0; index < snapshot.opcode_counts.size(); ++index) {
        if (index != 0U) output << ',';
        output << snapshot.opcode_counts[index];
    }
    output << "]\n}\n";
    require(output.good(), "write VM profile output");
}

void test_archive_and_classfile(const std::string& fixture_jar) {
    auto archive = phoneme::archive::ZipArchive::open(fixture_jar);
    require(archive.has_value(), "open fixture JAR");
    const auto* entry = archive->find("corefixture/Arithmetic.class");
    require(entry != nullptr, "find fixture class");
    auto bytes = archive->read(*entry);
    require(bytes.has_value(), "inflate fixture class");
    auto parsed = phoneme::classfile::ClassFile::parse(*bytes);
    require(parsed.has_value(), "parse fixture class");
    require(parsed->name() == "corefixture/Arithmetic",
            "fixture class name is correct");
    require(parsed->major_version() == 52U,
            "Java 8 class version remains native and is not downgraded");

    const auto* exception_entry = archive->find("corefixture/Exceptions.class");
    require(exception_entry != nullptr, "find exception fixture class");
    auto exception_bytes = archive->read(*exception_entry);
    require(exception_bytes.has_value(), "inflate exception fixture class");
    auto exception_class = phoneme::classfile::ClassFile::parse(*exception_bytes);
    require(exception_class.has_value(), "parse exception fixture class");
    const auto* nested_catch = exception_class->find_method(
        "catchNestedDivideByZero", "()I");
    require(nested_catch != nullptr && nested_catch->code.has_value(),
            "exception fixture method has Code");
    require(!nested_catch->code->exception_table.empty(),
            "Code parser preserves Java exception table");
    require(!nested_catch->code->stack_map_frames.empty(),
            "Code parser preserves Java StackMapTable frames");
}

void test_suite_permissions(const std::string& fixture_jar) {
    const auto store_root =
        std::filesystem::path(fixture_jar).parent_path() /
        "suite-permission-store";
    std::error_code cleanup_error;
    std::filesystem::remove_all(store_root, cleanup_error);
    require(!cleanup_error, "clear suite permission test store");

    phoneme::runtime::SuiteStore suites;
    auto configured = suites.configure(phoneme::runtime::SuiteStoreConfig {
        .root_path = store_root.string(),
    });
    if (!configured) {
        std::cerr << "SuiteStore configure error: "
                  << configured.error().message << '\n';
    }
    require(configured.has_value(), "configure suite permission test store");
    auto suite_id = suites.install(fixture_jar);
    if (!suite_id) {
        std::cerr << "SuiteStore install error: "
                  << suite_id.error().message << '\n';
    }
    require(suite_id.has_value(), "inspect suite permission manifest");
    const auto* suite = suites.find(*suite_id);
    require(suite != nullptr, "find installed permission fixture suite");
    require(suite->declared_midlets.size() == 1U &&
                suite->declared_midlets.front() ==
                    "corefixture.LifecycleApp",
            "permission attributes are not parsed as MIDlet declarations");
    require(suite->has_permission_declarations &&
                suite->declared_permissions.size() == 3U &&
                suite->declared_permissions[0] ==
                    "javax.microedition.io.Connector.http" &&
                suite->declared_permissions[1] ==
                    "javax.microedition.io.Connector.file.read" &&
                suite->declared_permissions[2] ==
                    "javax.microedition.media.capture.audio",
            "required and optional MIDP permissions are parsed per suite");

    std::filesystem::remove_all(store_root, cleanup_error);
    require(!cleanup_error, "remove suite permission test store");
}

void test_builtin_boot_classes() {
    phoneme::vm::ClassRepository classes;

    auto object = classes.load("java/lang/Object");
    auto string = classes.load("java.lang.String");
    auto midlet = classes.load("javax/microedition/midlet/MIDlet");
    auto media_manager = classes.load("javax/microedition/media/Manager");
    auto media_player = classes.load("javax/microedition/media/Player");
    auto micro3d_figure = classes.load(
        "com/mascotcapsule/micro3d/v3/Figure");
    auto micro3d_graphics = classes.load(
        "com/mascotcapsule/micro3d/v3/Graphics3D");
    require(object.has_value(), "load C++ built-in java/lang/Object");
    require(string.has_value(), "load C++ built-in java/lang/String");
    require(midlet.has_value(), "load C++ built-in MIDlet");
    require(media_manager.has_value(), "load C++ built-in MMAPI Manager");
    require(media_player.has_value(), "load C++ built-in MMAPI Player");
    require(micro3d_figure.has_value(), "load C++ built-in Micro3D Figure");
    require(micro3d_graphics.has_value(),
            "load C++ built-in Micro3D Graphics3D");
    require((*object)->super_name().empty(), "Object has no superclass");
    require((*string)->super_name() == "java/lang/Object",
            "String inherits built-in Object");
    require((*midlet)->super_name() == "java/lang/Object",
            "MIDlet inherits built-in Object");
    require((*midlet)->find_method("<init>", "()V") != nullptr,
            "built-in MIDlet exposes its constructor");
    require(!classes.load("java/lang/ProcessBuilder").has_value(),
            "unported classes fail instead of falling back to phoneME");
}

void test_class_layout(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add fixture JAR to class repository");

    phoneme::vm::ClassStateRegistry states(classes);
    auto arithmetic_layout = states.layout("corefixture/Arithmetic");
    require(arithmetic_layout.has_value(),
            "build fixture layout with built-in Object parent");
    require((*arithmetic_layout)->super_name == "java/lang/Object",
            "fixture layout resolves its C++ built-in parent");
    require((*arithmetic_layout)->instance_field_slots == 1,
            "fixture has one instance field");

    auto value_field = states.resolve_field(
        "corefixture/Arithmetic", "value", "I", false);
    auto value_field_again = states.resolve_field(
        "corefixture/Arithmetic", "value", "I", false);
    auto initialized_field = states.resolve_field(
        "corefixture/Arithmetic", "initializedValue", "I", true);
    require(value_field.has_value() && value_field->id.valid(),
            "resolved instance field has a runtime ID");
    require(value_field_again.has_value() &&
                value_field_again->id == value_field->id,
            "repeated field resolution reuses the runtime ID");
    require(initialized_field.has_value() && initialized_field->id.valid() &&
                initialized_field->id != value_field->id,
            "distinct fields receive unique runtime IDs");
    require(value_field->declaring_class_id.valid() &&
                initialized_field->declaring_class_id ==
                    value_field->declaring_class_id,
            "resolved fields retain their declaring ClassId");
    require(value_field->value_kind == phoneme::vm::ValueKind::int32 &&
                initialized_field->value_kind ==
                    phoneme::vm::ValueKind::int32,
            "field metadata caches its storage value kind");

    phoneme::vm::Heap heap(64);
    auto instance = states.allocate_instance(heap, "corefixture/Arithmetic");
    require(instance.has_value(), "allocate fixture instance");
    auto class_name = heap.class_name(*instance);
    require(class_name.has_value() && *class_name == "corefixture/Arithmetic",
            "allocated instance preserves its runtime class");
}

void test_heap_generation_and_collection() {
    phoneme::vm::Heap heap(64);
    auto child = heap.allocate_object("test/Child", 0);
    auto root = heap.allocate_object("test/Root", 1);
    require(child.has_value() && root.has_value(), "allocate heap objects");
    require(heap.set_field(*root, 0,
                           phoneme::vm::Value::from_reference(*child)).has_value(),
            "link root to child");

    const phoneme::vm::ObjectRef roots[] {*root};
    require(heap.collect(roots).has_value(), "collect with a live root");
    const auto live_stats = heap.stats();
    require(live_stats.live_objects == 2,
            "GC preserves transitive references");
    require(heap.estimated_bytes() == live_stats.estimated_bytes,
            "fast heap byte total matches detailed statistics");

    require(heap.collect(std::span<const phoneme::vm::ObjectRef> {}).has_value(),
            "collect without roots");
    require(heap.stats().live_objects == 0,
            "GC releases unreachable objects");
    require(heap.estimated_bytes() == 0,
            "fast heap byte total clears after collection");
    require(!heap.class_name(*child).has_value(),
            "stale object reference is rejected");

    auto replacement = heap.allocate_object("test/Replacement", 0);
    require(replacement.has_value(), "reuse a free heap slot");
    require(replacement->bits != child->bits,
            "reused slot receives a new generation");
}

void test_descriptors_and_slots() {
    auto method = phoneme::vm::parse_method_descriptor(
        "(IJLjava/lang/String;[B)D");
    require(method.has_value(), "parse mixed method descriptor");
    require(method->parameters.size() == 4,
            "method descriptor has four parameters");
    require(method->parameter_slots(false) == 5,
            "Java long consumes two parameter slots");
    require(method->parameter_slots(true) == 6,
            "receiver consumes one parameter slot");

    auto array = phoneme::vm::parse_field_descriptor("[[Ljava/lang/String;");
    require(array.has_value(), "parse reference array descriptor");
    require(array->kind == phoneme::vm::JavaTypeKind::array &&
                array->array_dimensions == 2 &&
                array->class_name == "java/lang/String",
            "array descriptor preserves component type");

    phoneme::vm::LocalVariables locals(4);
    require(locals.set(0, phoneme::vm::Value::from_long(42)).has_value(),
            "store Java long in two local slots");
    require(locals.get(0).has_value(), "read Java long head slot");
    require(!locals.get(1).has_value(),
            "cannot read category-2 continuation slot");

    phoneme::vm::OperandStack stack(2);
    require(stack.push(phoneme::vm::Value::from_double(1.5)).has_value(),
            "Java double consumes two operand slots");
    require(!stack.push(phoneme::vm::Value::from_int(1)).has_value(),
            "max_stack is measured in Java slots");
}

void test_bytecode_structure_verifier() {
    const std::vector<phoneme::u8> valid_branch {
        0x03,             // iconst_0
        0x99, 0x00, 0x04, // ifeq -> ireturn
        0x04,             // iconst_1
        0xAC,             // ireturn
    };
    require(phoneme::classfile::verify_code_structure(valid_branch, {})
                .has_value(),
            "structural verifier accepts aligned branch targets");

    const std::vector<phoneme::u8> generic_local_operands {
        0x19, 0x18, // aload 24
        0x3A, 0x18, // astore 24
        0xB1,       // return
    };
    require(phoneme::classfile::verify_code_structure(
                generic_local_operands, {}).has_value(),
            "structural verifier consumes generic local operands");

    const std::vector<phoneme::u8> branch_into_operand {
        0x10, 0x01,       // bipush 1
        0xA7, 0xFF, 0xFF, // goto pc 1 (inside bipush operand)
        0xB1,
    };
    require(!phoneme::classfile::verify_code_structure(branch_into_operand, {})
                 .has_value(),
            "structural verifier rejects branch into an operand");

    const std::vector<phoneme::u8> handler_code {
        0x10, 0x01,
        0x57,
        0xB1,
    };
    const std::vector<phoneme::classfile::ExceptionHandler> bad_handlers {
        phoneme::classfile::ExceptionHandler {
            .start_pc = 0,
            .end_pc = 3,
            .handler_pc = 1,
            .catch_type = "java/lang/Throwable",
        },
    };
    require(!phoneme::classfile::verify_code_structure(handler_code,
                                                        bad_handlers)
                 .has_value(),
            "structural verifier rejects unaligned exception handler target");

    const std::vector<phoneme::u8> truncated_switch {
        0x03,
        0xAA, 0x00, 0x00, // tableswitch padding only
    };
    require(!phoneme::classfile::verify_code_structure(truncated_switch, {})
                 .has_value(),
            "structural verifier rejects truncated switch payload");

    const std::vector<phoneme::u8> nonzero_switch_padding {
        0x03,             // iconst_0
        0xAA,             // tableswitch at pc 1
        0x7F, 0x55,       // legacy obfuscator alignment bytes
        0x00, 0x00, 0x00, 0x13, // default -> pc 20
        0x00, 0x00, 0x00, 0x00, // low = 0
        0x00, 0x00, 0x00, 0x00, // high = 0
        0x00, 0x00, 0x00, 0x13, // case 0 -> pc 20
        0xB1,             // return
    };
    require(phoneme::classfile::verify_code_structure(
                nonzero_switch_padding, {}).has_value(),
            "structural verifier accepts non-zero legacy switch padding");

    const std::vector<phoneme::classfile::StackMapFrame> bad_stack_map {
        phoneme::classfile::StackMapFrame {
            .kind = phoneme::classfile::StackMapFrameKind::same,
            .bytecode_offset = 1,
        },
    };
    require(!phoneme::classfile::verify_code_structure(handler_code,
                                                        {},
                                                        bad_stack_map)
                 .has_value(),
            "structural verifier rejects unaligned stack map frame");
}

void test_type_state_verifier() {
    const auto verify_rejects = [](std::vector<phoneme::u8> bytecode,
                                   phoneme::u16 max_stack,
                                   phoneme::u16 max_locals,
                                   const char* descriptor,
                                   const char* message) {
        phoneme::classfile::Method method {
            .access_flags = 0x0009U,
            .name = "invalid",
            .descriptor = descriptor,
            .code = phoneme::classfile::CodeAttribute {
                .max_stack = max_stack,
                .max_locals = max_locals,
                .bytecode = std::move(bytecode),
            },
        };
        const auto owner = phoneme::classfile::ClassFile::builtin(
            "corefixture/VerifierFixture",
            "java/lang/Object",
            0x0021U,
            {},
            {method});
        require(!phoneme::vm::verify_method(owner, method).has_value(),
                message);
    };

    verify_rejects({0x60, 0xAC},
                   2,
                   0,
                   "()I",
                   "type verifier rejects operand stack underflow");
    verify_rejects({0x2A, 0x57, 0x03, 0xAC},
                   1,
                   1,
                   "()I",
                   "type verifier rejects aload from unusable local");
    verify_rejects({0xB1},
                   0,
                   0,
                   "()I",
                   "type verifier rejects return opcode mismatch");
    verify_rejects({0x03, 0x99, 0x00, 0x04, 0x04, 0xAC},
                   1,
                   0,
                   "()I",
                   "type verifier rejects incompatible branch stack depth");

    phoneme::classfile::Method legacy_method {
        .access_flags = 0x0009U,
        .name = "legacy",
        .descriptor = "()I",
        .code = phoneme::classfile::CodeAttribute {
            .max_stack = 1,
            .max_locals = 0,
            .bytecode = {0x03, 0x99, 0x00, 0x05,
                         0x04, 0xAC, 0x05, 0xAC},
            .stack_map_frames = {
                phoneme::classfile::StackMapFrame {
                    .kind = phoneme::classfile::StackMapFrameKind::cldc_full,
                    .bytecode_offset = 6,
                    .stack = {
                        phoneme::classfile::VerificationType {
                            .kind = phoneme::classfile::VerificationTypeKind::integer,
                        },
                    },
                },
            },
        },
    };
    const auto legacy_owner = phoneme::classfile::ClassFile::builtin(
        "corefixture/LegacyVerifierFixture",
        "java/lang/Object",
        0x0021U,
        {},
        {legacy_method});
    require(phoneme::vm::verify_method(legacy_owner, legacy_method).has_value(),
            "legacy CLDC verifier ignores stale StackMap after safe data-flow validation");

    auto modern_method = legacy_method;
    modern_method.name = "modern";
    modern_method.code->stack_map_frames.front().kind =
        phoneme::classfile::StackMapFrameKind::full;
    const auto modern_owner = phoneme::classfile::ClassFile::builtin(
        "corefixture/ModernVerifierFixture",
        "java/lang/Object",
        0x0021U,
        {},
        {modern_method});
    require(!phoneme::vm::verify_method(modern_owner, modern_method).has_value(),
            "modern StackMapTable inconsistency remains strict");

    phoneme::classfile::Method legacy_void_return {
        .access_flags = 0x0009U,
        .name = "legacyVoidReturn",
        .descriptor = "()V",
        .code = phoneme::classfile::CodeAttribute {
            .max_stack = 1,
            .max_locals = 0,
            .bytecode = {0x01, 0xB1},
            .stack_map_frames = {
                phoneme::classfile::StackMapFrame {
                    .kind = phoneme::classfile::StackMapFrameKind::cldc_full,
                    .bytecode_offset = 1,
                    .stack = {
                        phoneme::classfile::VerificationType {
                            .kind = phoneme::classfile::VerificationTypeKind::object,
                            .class_name = "java/lang/Object",
                        },
                    },
                },
            },
        },
    };
    const auto legacy_return_owner = phoneme::classfile::ClassFile::builtin(
        "corefixture/LegacyReturnVerifierFixture",
        "java/lang/Object",
        0x0021U,
        {},
        {legacy_void_return});
    require(phoneme::vm::verify_method(legacy_return_owner,
                                       legacy_void_return).has_value(),
            "legacy CLDC verifier tolerates obsolete operand at void return");

    auto legacy_unmapped_return = legacy_void_return;
    legacy_unmapped_return.name = "legacyUnmappedVoidReturn";
    legacy_unmapped_return.code->stack_map_frames.clear();
    const auto legacy_unmapped_owner = phoneme::classfile::ClassFile::builtin(
        "corefixture/LegacyUnmappedReturnVerifierFixture",
        "java/lang/Object",
        0x0021U,
        {},
        {legacy_unmapped_return});
    require(phoneme::vm::verify_method(legacy_unmapped_owner,
                                       legacy_unmapped_return).has_value(),
            "pre-Java-6 verifier tolerates obsolete return stack without maps");

    auto modern_void_return = legacy_void_return;
    modern_void_return.name = "modernVoidReturn";
    modern_void_return.code->stack_map_frames.front().kind =
        phoneme::classfile::StackMapFrameKind::full;
    const auto modern_return_owner = phoneme::classfile::ClassFile::builtin(
        "corefixture/ModernReturnVerifierFixture",
        "java/lang/Object",
        0x0021U,
        {},
        {modern_void_return});
    require(phoneme::vm::verify_method(modern_return_owner,
                                       modern_void_return).has_value(),
            "modern StackMapTable permits operands discarded by void return");

    phoneme::classfile::Method modern_boolean_return {
        .access_flags = 0x0009U,
        .name = "modernBooleanReturn",
        .descriptor = "()Z",
        .code = phoneme::classfile::CodeAttribute {
            .max_stack = 2,
            .max_locals = 0,
            .bytecode = {0x03, 0x04, 0xAC},
            .stack_map_frames = {
                phoneme::classfile::StackMapFrame {
                    .kind = phoneme::classfile::StackMapFrameKind::full,
                    .bytecode_offset = 2,
                    .stack = {
                        phoneme::classfile::VerificationType {
                            .kind = phoneme::classfile::VerificationTypeKind::integer,
                        },
                        phoneme::classfile::VerificationType {
                            .kind = phoneme::classfile::VerificationTypeKind::integer,
                        },
                    },
                },
            },
        },
    };
    const auto modern_boolean_owner = phoneme::classfile::ClassFile::builtin(
        "corefixture/ModernBooleanReturnVerifierFixture",
        "java/lang/Object",
        0x0021U,
        {},
        {modern_boolean_return});
    require(phoneme::vm::verify_method(modern_boolean_owner,
                                       modern_boolean_return).has_value(),
            "typed return consumes its value and discards lower operands");
}

void test_monitor_table() {
    phoneme::vm::MonitorTable monitors;
    const phoneme::vm::ObjectRef object =
        phoneme::vm::ObjectRef::make(1U, 1U);

    auto first = monitors.enter(object, 1U);
    require(first.has_value() &&
                *first == phoneme::vm::MonitorEnterResult::acquired,
            "monitor table acquires unowned monitor");
    auto reentrant = monitors.enter(object, 1U);
    require(reentrant.has_value() &&
                *reentrant == phoneme::vm::MonitorEnterResult::acquired,
            "monitor table supports reentrant entry");
    auto snapshot = monitors.snapshot(object);
    require(snapshot.has_value() && snapshot->owner == 1U &&
                snapshot->recursion == 2U,
            "monitor table tracks owner and recursion");

    auto contended = monitors.enter(object, 2U);
    require(contended.has_value() &&
                *contended == phoneme::vm::MonitorEnterResult::would_block,
            "monitor table reports contention without stealing ownership");
    require(!monitors.exit(object, 2U).has_value(),
            "monitor table rejects exit by non-owner");
    require(monitors.exit(object, 1U).has_value() &&
                monitors.exit(object, 1U).has_value(),
            "monitor table releases every reentrant level");
    snapshot = monitors.snapshot(object);
    require(snapshot.has_value() && snapshot->owner == 0U &&
                snapshot->recursion == 0U,
            "monitor table clears owner after final exit");

    monitors.clear();
    const auto late_object = phoneme::vm::ObjectRef::make(2U, 1U);
    auto late_entry = monitors.enter(late_object, 2U);
    require(!late_entry.has_value() &&
                late_entry.error().code == phoneme::ErrorCode::invalid_state,
            "monitor cancellation remains terminal for late shutdown waiters");
}

void test_integer_interpreter() {
    phoneme::classfile::CodeAttribute code {
        .max_stack = 2,
        .max_locals = 0,
        .bytecode = {0x05, 0x06, 0x60, 0xAC},
    };
    phoneme::vm::Interpreter interpreter;
    auto result = interpreter.execute(code);
    require(result.has_value() && result->return_value.has_value(),
            "execute integer bytecode");
    auto value = result->return_value->as_int();
    require(value.has_value() && *value == 5, "2 + 3 returns 5");
}

void test_machine_heap_memory_reporting() {
    constexpr phoneme::usize heap_bytes = 12U * 1024U * 1024U;
    phoneme::vm::ClassRepository classes;
    phoneme::vm::Machine machine(
        classes,
        phoneme::vm::HeapLimits {
            .maximum_objects = 1'000'000U,
            .maximum_bytes = heap_bytes,
        });

    auto runtime_result = machine.invoke_static(
        "java/lang/Runtime",
        "getRuntime",
        "()Ljava/lang/Runtime;");
    require(runtime_result.has_value() &&
                runtime_result->completed_normally() &&
                runtime_result->return_value.has_value(),
            "obtain Java Runtime for configured heap reporting");
    auto runtime_reference = runtime_result->return_value->as_reference();
    require(runtime_reference.has_value() && !runtime_reference->is_null(),
            "Java Runtime singleton is non-null");

    auto total_result = machine.invoke_instance(
        *runtime_reference,
        "java/lang/Runtime",
        "totalMemory",
        "()J");
    require(total_result.has_value() &&
                total_result->completed_normally() &&
                total_result->return_value.has_value(),
            "query configured Java heap capacity");
    auto total_memory = total_result->return_value->as_long();
    require(total_memory.has_value() &&
                *total_memory == static_cast<phoneme::i64>(heap_bytes),
            "Runtime.totalMemory reflects the configured heap limit");

    auto free_result = machine.invoke_instance(
        *runtime_reference,
        "java/lang/Runtime",
        "freeMemory",
        "()J");
    require(free_result.has_value() &&
                free_result->completed_normally() &&
                free_result->return_value.has_value(),
            "query free Java heap capacity");
    auto free_memory = free_result->return_value->as_long();
    require(free_memory.has_value() &&
                *free_memory > 0 && *free_memory <= *total_memory,
            "Runtime.freeMemory uses the configured heap limit");
}

void test_machine_invocation(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add fixture JAR to VM classpath");

    phoneme::vm::Machine machine(classes);
    auto initialized = machine.invoke_static("corefixture/Arithmetic",
                                             "initializedValue",
                                             "()I");
    require(initialized.has_value() && initialized->return_value.has_value(),
            "execute class initializer without external boot archive");
    auto initialized_integer = initialized->return_value->as_int();
    require(initialized_integer.has_value() && *initialized_integer == 4,
            "class initializer sets static field");

    auto result = machine.invoke_static("corefixture/Arithmetic",
                                        "run",
                                        "()I");
    require(result.has_value() && result->return_value.has_value(),
            "execute constructor, fields and calls");
    auto integer = result->return_value->as_int();
    require(integer.has_value() && *integer == 13,
            "fixture execution returns 13");

    auto native_time = machine.invoke_static("corefixture/Arithmetic",
                                             "currentTime",
                                             "()J");
    require(native_time.has_value() && native_time->return_value.has_value(),
            "dispatch built-in System.currentTimeMillis");
    auto milliseconds = native_time->return_value->as_long();
    require(milliseconds.has_value() && *milliseconds > 946684800000LL,
            "native time is plausible");

    auto string_length = machine.invoke_static("corefixture/Arithmetic",
                                               "vietnameseLength",
                                               "()I");
    require(string_length.has_value() && string_length->return_value.has_value(),
            "create built-in String from modified UTF-8");
    auto string_length_value = string_length->return_value->as_int();
    require(string_length_value.has_value() && *string_length_value == 4,
            "Vietnamese literal has four UTF-16 code units");

    auto string_character = machine.invoke_static("corefixture/Arithmetic",
                                                  "vietnameseCharacter",
                                                  "()I");
    require(string_character.has_value() &&
                string_character->return_value.has_value(),
            "call built-in String.charAt");
    auto character_value = string_character->return_value->as_int();
    require(character_value.has_value() && *character_value == 0x1EC7,
            "String.charAt preserves U+1EC7");

    auto array_result = machine.invoke_static("corefixture/Arithmetic",
                                              "arrayRun",
                                              "()I");
    require(array_result.has_value() && array_result->return_value.has_value(),
            "execute primitive array bytecodes");
    auto array_integer = array_result->return_value->as_int();
    require(array_integer.has_value() && *array_integer == 12,
            "array operations return 12");

    auto operand_effects = machine.invoke_static(
        "corefixture/Arithmetic",
        "resolvedOperandEffects",
        "()Lcorefixture/Arithmetic;");
    require(operand_effects.has_value() &&
                operand_effects->completed_normally() &&
                operand_effects->return_value.has_value(),
            "decoded and legacy execution return the mutated heap object");
    auto effects_reference = operand_effects->return_value->as_reference();
    require(effects_reference.has_value() && !effects_reference->is_null(),
            "resolved operand fixture returns a live object");
    auto effects_field = machine.heap().field(*effects_reference, 0U);
    require(effects_field.has_value() &&
                effects_field->as_int().value_or(-1) == 21,
            "resolved field and direct-call operands preserve heap-visible effects");

    auto static_effect = machine.invoke_static(
        "corefixture/Arithmetic",
        "differentialStaticValue",
        "()I");
    require(static_effect.has_value() && static_effect->completed_normally() &&
                static_effect->return_value.has_value() &&
                static_effect->return_value->as_int().value_or(-1) == 10,
            "resolved static operands preserve static-field effects");

    auto stable_throwable = machine.invoke_static(
        "corefixture/Arithmetic",
        "stableThrowableMessage",
        "()V");
    require(stable_throwable.has_value() &&
                !stable_throwable->completed_normally() &&
                stable_throwable->throwable.has_value(),
            "stable throwable fixture reaches the invocation boundary");
    auto stable_throwable_class = machine.heap().class_name(
        *stable_throwable->throwable);
    require(stable_throwable_class.has_value() &&
                *stable_throwable_class == "java/lang/NoSuchMethodError",
            "decoded and legacy execution preserve throwable class");
    auto stable_message_field = machine.heap().field(
        *stable_throwable->throwable, 0U);
    require(stable_message_field.has_value(),
            "stable throwable exposes its detail message field");
    auto stable_message_reference = stable_message_field->as_reference();
    require(stable_message_reference.has_value() &&
                !stable_message_reference->is_null(),
            "stable throwable detail message is non-null");
    auto stable_message = machine.heap().string_value(
        *stable_message_reference);
    require(stable_message.has_value() &&
                *stable_message == u"decoded-linkage-stable",
            "decoded and legacy execution preserve throwable message");

    const auto require_linkage_trace = [&machine](
        const char* method_name,
        std::string_view expected_class,
        const char* message) {
        auto result = machine.invoke_static(
            "corefixture/LinkageOps",
            method_name,
            "()Ljava/lang/String;");
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                message);
        auto reference = result->return_value->as_reference();
        require(reference.has_value() && !reference->is_null(), message);
        auto text = machine.heap().string_value(*reference);
        require(text.has_value(), message);
        std::string ascii;
        ascii.reserve(text->size());
        for (const char16_t unit : *text) {
            require(unit <= 0x7FU, message);
            ascii.push_back(static_cast<char>(unit));
        }
        const std::size_t separator = ascii.find('|');
        require(separator != std::string::npos &&
                    ascii.substr(0U, separator) == ascii.substr(separator + 1U),
                "repeated linkage failure preserves its exact message");
        require(ascii.starts_with(std::string(expected_class) + ':'), message);
    };
    require_linkage_trace("missingClassTrace",
                          "java.lang.NoClassDefFoundError",
                          "missing class resolves to stable NoClassDefFoundError");
    require_linkage_trace("missingArrayTrace",
                          "java.lang.NoClassDefFoundError",
                          "anewarray resolves its component class at the instruction");
    require_linkage_trace("missingTypeTrace",
                          "java.lang.NoClassDefFoundError",
                          "checkcast resolves its target even for a null receiver");
    require_linkage_trace("missingInstanceofTrace",
                          "java.lang.NoClassDefFoundError",
                          "instanceof resolves its target even for a null receiver");
    require_linkage_trace("missingMultiArrayTrace",
                          "java.lang.NoClassDefFoundError",
                          "multianewarray resolves its reference component class");
    require_linkage_trace("missingMethodTrace",
                          "java.lang.NoSuchMethodError",
                          "missing method resolves to stable NoSuchMethodError");
    require_linkage_trace("missingFieldTrace",
                          "java.lang.NoSuchFieldError",
                          "missing field resolves to stable NoSuchFieldError");
}

void test_machine_dispatch(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add dispatch fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    auto interface_call = machine.invoke_static("corefixture/Dispatch",
                                                "interfaceCall",
                                                "()I");
    require(interface_call.has_value() &&
                interface_call->return_value.has_value() &&
                interface_call->return_value->as_int().value_or(-1) == 10,
            "invokeinterface dispatches to implementing class");

    auto polymorphic_interface = machine.invoke_static(
        "corefixture/Dispatch",
        "polymorphicInterfaceCall",
        "()I");
    require(polymorphic_interface.has_value() &&
                polymorphic_interface->return_value.has_value() &&
                polymorphic_interface->return_value->as_int().value_or(-1) ==
                    1012,
            "polymorphic invokeinterface refreshes its monomorphic target");
}

void test_machine_exceptions(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add exception fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    const auto require_int_result = [&machine](const char* method_name,
                                               phoneme::i32 expected,
                                               const char* message) {
        auto result = machine.invoke_static("corefixture/Exceptions",
                                            method_name,
                                            "()I");
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                message);
        auto integer = result->return_value->as_int();
        require(integer.has_value() && *integer == expected, message);
    };

    require_int_result("catchNestedDivideByZero", 41,
                       "unwind ArithmeticException through a caller frame");
    require_int_result("catchNullArrayLength", 42,
                       "catch implicit NullPointerException");
    require_int_result("catchArrayBounds", 43,
                       "catch implicit ArrayIndexOutOfBoundsException");
    require_int_result("catchNegativeArraySize", 44,
                       "catch implicit NegativeArraySizeException");
    require_int_result("catchFinallyAndSuperclass", 45,
                       "execute catch-all finally and superclass handler");
    require_int_result("catchThrowNull", 46,
                       "athrow null creates NullPointerException");
    require_int_result("catchArrayStore", 48,
                       "aastore enforces reference component type");
    require_int_result("typeChecks", 47,
                       "execute instanceof and catch failed checkcast");
    require_int_result("nativeExceptionMessage", 49,
                       "native Java exception preserves diagnostic message");

    auto uncaught = machine.invoke_static("corefixture/Exceptions",
                                          "uncaught",
                                          "()V");
    require(uncaught.has_value() && !uncaught->completed_normally() &&
                uncaught->throwable.has_value(),
            "uncaught Java exception reaches the invocation boundary");
    auto throwable_class = machine.heap().class_name(*uncaught->throwable);
    require(throwable_class.has_value() &&
                *throwable_class == "java/lang/IllegalStateException",
            "uncaught boundary preserves the thrown Java object");

    auto first_clinit = machine.invoke_static("corefixture/ClinitHarness",
                                              "firstUse",
                                              "()I");
    require(first_clinit.has_value() &&
                first_clinit->return_value.has_value(),
            "first failed class initialization is catchable");
    auto first_clinit_value = first_clinit->return_value->as_int();
    require(first_clinit_value.has_value() && *first_clinit_value == 1,
            "non-Error clinit throwable is wrapped once");

    auto second_clinit = machine.invoke_static("corefixture/ClinitHarness",
                                               "secondUse",
                                               "()I");
    require(second_clinit.has_value() &&
                second_clinit->return_value.has_value(),
            "subsequent use of erroneous class is catchable");
    auto second_clinit_value = second_clinit->return_value->as_int();
    require(second_clinit_value.has_value() && *second_clinit_value == 11,
            "erroneous class is not initialized a second time");
}

void test_machine_extended_opcodes(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add opcode fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    const phoneme::vm::Value table_arguments[] {
        phoneme::vm::Value::from_int(1),
    };
    auto table = machine.invoke_static("corefixture/Opcodes",
                                       "tableSwitch",
                                       "(I)I",
                                       table_arguments);
    require(table.has_value() && table->return_value.has_value(),
            "execute tableswitch");
    auto table_value = table->return_value->as_int();
    require(table_value.has_value() && *table_value == 20,
            "tableswitch selects dense case");

    const phoneme::vm::Value lookup_arguments[] {
        phoneme::vm::Value::from_int(999),
    };
    auto lookup = machine.invoke_static("corefixture/Opcodes",
                                        "lookupSwitch",
                                        "(I)I",
                                        lookup_arguments);
    require(lookup.has_value() && lookup->return_value.has_value(),
            "execute lookupswitch");
    auto lookup_value = lookup->return_value->as_int();
    require(lookup_value.has_value() && *lookup_value == 3,
            "lookupswitch selects sparse case");

    const phoneme::vm::Value nan_arguments[] {
        phoneme::vm::Value::from_double(
            std::numeric_limits<double>::quiet_NaN()),
    };
    auto converted = machine.invoke_static("corefixture/Opcodes",
                                           "doubleToLong",
                                           "(D)J",
                                           nan_arguments);
    require(converted.has_value() && converted->return_value.has_value(),
            "execute double-to-long conversion");
    auto converted_value = converted->return_value->as_long();
    require(converted_value.has_value() && *converted_value == 0,
            "Java converts NaN to long zero");

    const phoneme::vm::Value compare_arguments[] {
        phoneme::vm::Value::from_float(1.0F),
        phoneme::vm::Value::from_float(2.0F),
    };
    auto comparison = machine.invoke_static("corefixture/Opcodes",
                                            "floatCompare",
                                            "(FF)I",
                                            compare_arguments);
    require(comparison.has_value() && comparison->return_value.has_value(),
            "execute float comparison");
    auto comparison_value = comparison->return_value->as_int();
    require(comparison_value.has_value() && *comparison_value == -1,
            "float compare preserves Java ordering");

    const phoneme::vm::Value arithmetic_arguments[] {
        phoneme::vm::Value::from_double(7.0),
        phoneme::vm::Value::from_double(2.0),
    };
    auto arithmetic = machine.invoke_static("corefixture/Opcodes",
                                            "doubleArithmetic",
                                            "(DD)D",
                                            arithmetic_arguments);
    require(arithmetic.has_value() && arithmetic->return_value.has_value(),
            "execute double arithmetic");
    auto arithmetic_value = arithmetic->return_value->as_double();
    require(arithmetic_value.has_value() && *arithmetic_value == 22.5,
            "double arithmetic returns expected value");

    const phoneme::vm::Value narrow_arguments[] {
        phoneme::vm::Value::from_int(0x12345),
    };
    auto narrowed = machine.invoke_static("corefixture/Opcodes",
                                          "narrowInt",
                                          "(I)I",
                                          narrow_arguments);
    require(narrowed.has_value() && narrowed->return_value.has_value(),
            "execute integer narrowing conversions");
    auto narrowed_value = narrowed->return_value->as_int();
    require(narrowed_value.has_value() && *narrowed_value == 18127,
            "i2b/i2c/i2s preserve Java narrowing semantics");

    auto multi_array = machine.invoke_static("corefixture/Opcodes",
                                             "multiArray",
                                             "()I");
    require(multi_array.has_value() && multi_array->return_value.has_value(),
            "execute multianewarray");
    auto multi_array_value = multi_array->return_value->as_int();
    require(multi_array_value.has_value() && *multi_array_value == 18,
            "multianewarray allocates every requested dimension");

    auto class_literal = machine.invoke_static("corefixture/Opcodes",
                                               "classLiteral",
                                               "()I");
    require(class_literal.has_value() &&
                class_literal->return_value.has_value(),
            "execute ldc class literal");
    auto class_literal_value = class_literal->return_value->as_int();
    require(class_literal_value.has_value() &&
                *class_literal_value == 60,
            "class literals reuse the canonical class mirror");

    auto array_types = machine.invoke_static("corefixture/Opcodes",
                                             "arrayTypes",
                                             "()I");
    require(array_types.has_value() && array_types->return_value.has_value(),
            "execute array checkcast and instanceof");
    auto array_types_value = array_types->return_value->as_int();
    require(array_types_value.has_value() && *array_types_value == 8,
            "arrays implement Object, Cloneable, and Serializable covariance");

    auto narrow_arrays = machine.invoke_static("corefixture/Opcodes",
                                               "narrowArraySemantics",
                                               "()I");
    require(narrow_arrays.has_value() &&
                narrow_arrays->return_value.has_value(),
            "execute narrow primitive array loads and stores");
    auto narrow_arrays_value = narrow_arrays->return_value->as_int();
    require(narrow_arrays_value.has_value() &&
                *narrow_arrays_value == 15,
            "byte short char and boolean arrays preserve JVM narrowing semantics");

    auto interface_call = machine.invoke_static("corefixture/Dispatch",
                                                "interfaceCall",
                                                "()I");
    require(interface_call.has_value() &&
                interface_call->return_value.has_value(),
            "execute invokeinterface against runtime implementation");
    auto interface_value = interface_call->return_value->as_int();
    require(interface_value.has_value() && *interface_value == 10,
            "invokeinterface dispatches to implementing class");

    auto polymorphic_interface = machine.invoke_static(
        "corefixture/Dispatch",
        "polymorphicInterfaceCall",
        "()I");
    require(polymorphic_interface.has_value() &&
                polymorphic_interface->return_value.has_value(),
            "execute one invokeinterface call site with two receiver classes");
    auto polymorphic_interface_value =
        polymorphic_interface->return_value->as_int();
    require(polymorphic_interface_value.has_value() &&
                *polymorphic_interface_value == 1012,
            "polymorphic invokeinterface refreshes its monomorphic target");

    const auto require_lambda_result = [&machine](const char* method_name,
                                                  phoneme::i32 expected,
                                                  const char* message) {
        auto result = machine.invoke_static("corefixture/LambdaOps",
                                            method_name,
                                            "()I");
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                message);
        auto value = result->return_value->as_int();
        require(value.has_value() && *value == expected, message);
    };
    require_lambda_result("staticLambda", 7,
                          "execute non-capturing JDK 8 lambda");
    require_lambda_result("capturedLambda", 12,
                          "execute capturing JDK 8 lambda");
    require_lambda_result("staticMethodReference", 12,
                          "execute static JDK 8 method reference");
    require_lambda_result("boundMethodReference", 13,
                          "execute bound JDK 8 method reference");
    require_lambda_result("constructorMethodReference", 13,
                          "execute JDK 8 constructor method reference");
    require_lambda_result("serializableLambda", 25,
                          "execute JDK 8 altMetafactory serializable lambda");
    require_lambda_result("unboundMethodReference", 4,
                          "execute unbound JDK 8 method reference");
    require_lambda_result("genericLambda", 4,
                          "execute generic JDK 8 lambda adaptation");
    require_lambda_result("defaultInterfaceMethod", 9,
                          "execute JDK 8 default interface method");

    auto constants = machine.invoke_static("corefixture/Jdk8Semantics",
                                           "constantValues",
                                           "()I");
    require(constants.has_value() && constants->completed_normally() &&
                constants->return_value.has_value(),
            "prepare JDK 8 ConstantValue fields");
    auto constants_value = constants->return_value->as_int();
    require(constants_value.has_value() && *constants_value == 139,
            "primitive and String ConstantValue fields retain their values");

    auto default_initialization = machine.invoke_static(
        "corefixture/Jdk8Semantics",
        "defaultInterfaceInitializationOrder",
        "()I");
    require(default_initialization.has_value() &&
                default_initialization->completed_normally() &&
                default_initialization->return_value.has_value(),
            "initialize JDK 8 default interfaces");
    auto default_initialization_value =
        default_initialization->return_value->as_int();
    require(default_initialization_value.has_value() &&
                *default_initialization_value == 12,
            "default superinterfaces initialize parent-first exactly once");

    const auto require_jdk8_string_result = [&machine](
        const char* method_name,
        phoneme::i32 expected,
        const char* message) {
        auto result = machine.invoke_static("corefixture/Jdk8Semantics",
                                            method_name,
                                            "()I");
        if (!result.has_value()) {
            std::cerr << "JDK8 fixture " << method_name
                      << " VM error: " << result.error().message << '\n';
        } else if (!result->completed_normally()) {
            std::cerr << "JDK8 fixture " << method_name << " threw ";
            if (result->throwable.has_value()) {
                auto throwable_class = machine.heap().class_name(
                    *result->throwable);
                std::cerr << (throwable_class.has_value()
                                  ? *throwable_class
                                  : std::string("unknown throwable"));
                auto message_field = machine.heap().field(
                    *result->throwable, 0U);
                if (message_field &&
                    message_field->kind() == phoneme::vm::ValueKind::reference) {
                    auto message_reference = message_field->as_reference();
                    if (message_reference && !message_reference->is_null()) {
                        auto throwable_message = machine.heap().string_value(
                            *message_reference);
                        if (throwable_message) {
                            std::cerr << ": ";
                            for (char16_t character : *throwable_message) {
                                std::cerr << (character <= 0x7FU
                                    ? static_cast<char>(character)
                                    : '?');
                            }
                        }
                    }
                }
            } else {
                std::cerr << "without throwable";
            }
            std::cerr << '\n';
        } else if (!result->return_value.has_value()) {
            std::cerr << "JDK8 fixture " << method_name
                      << " returned void\n";
        }
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                message);
        auto value = result->return_value->as_int();
        if (!value.has_value() || *value != expected) {
            std::cerr << "JDK8 fixture " << method_name
                      << " expected " << expected << " but returned "
                      << (value.has_value() ? std::to_string(*value)
                                            : std::string("non-int"))
                      << '\n';
        }
        require(value.has_value() && *value == expected, message);
    };
    require_jdk8_string_result("javacStringConcat", 19,
                               "execute javac 8 StringBuilder concatenation");
    require_jdk8_string_result("stringBufferOperations", 9,
                               "execute synchronized StringBuffer operations");
    require_jdk8_string_result("stringBufferExtendedOperations", 63,
                               "execute StringBuffer mutation and copy operations");
    require_jdk8_string_result("multiArrayDefaults", 1,
                               "initialize primitive multianewarray leaves correctly");
    require_jdk8_string_result("builderObjectAppend", 8,
                               "append object and null through StringBuilder");
    require_jdk8_string_result("arraycopyOverlap", 1123,
                               "System.arraycopy preserves overlapping copy semantics");
    require_jdk8_string_result("arraycopyReferences", 5,
                               "System.arraycopy validates reference arrays");
    require_jdk8_string_result("arraycopyExceptions", 60,
                               "native arraycopy failures unwind as Java exceptions");
    require_jdk8_string_result("stringApi", 16383,
                               "execute CLDC String construction and search APIs");
    require_jdk8_string_result("nativeStringExceptions", 15,
                               "native String failures unwind through Java catch blocks");
    auto list_class = classes.load("javax/microedition/lcdui/List");
    if (!list_class.has_value()) {
        std::cerr << "List class load failed: " << list_class.error().message << '\n';
    }
    require(list_class.has_value(), "load built-in MIDP List class");
    auto list_initializer = machine.invoke_static(
        "javax/microedition/lcdui/List", "<clinit>", "()V");
    if (!list_initializer.has_value()) {
        std::cerr << "List <clinit> VM error: "
                  << list_initializer.error().message << '\n';
    }
    require(list_initializer.has_value() &&
                list_initializer->completed_normally(),
            "initialize built-in MIDP List class");
    require_jdk8_string_result("choiceGroupApi", 31,
                               "execute ChoiceGroup storage and selection semantics");
    require_jdk8_string_result("listApi", 7,
                               "execute List and SELECT_COMMAND semantics");
    require_jdk8_string_result("choiceApi", 255,
                               "combine ChoiceGroup and List semantics");
    require_jdk8_string_result("consoleApi", 7,
                               "execute System streams and PrintStream formatting");
    require(machine.console_output() == u"OUT:7\nfalse\n",
            "System.out and System.err append to the bounded VM console");
    require_jdk8_string_result("stringEncodingApi", 127,
                               "execute String byte constructors and charset encoders");
    require_jdk8_string_result("stringEncodingExceptions", 7,
                               "String encoding failures preserve Java exceptions");
    require_jdk8_string_result("wrapperApi", 4095,
                               "execute JDK 8 boxing and java.lang wrapper APIs");
    require_jdk8_string_result("wrapperExceptions", 15,
                               "wrapper parse failures unwind as NumberFormatException");
    require_jdk8_string_result("throwableSuppressedApi", 15,
                               "Throwable preserves Java 8 suppressed exceptions");
    require_jdk8_string_result("mathApi", 255,
                               "execute java.lang.Math numeric semantics");
    require_jdk8_string_result("utilApi", 255,
                               "execute CLDC Vector Stack Hashtable and Random APIs");
    require_jdk8_string_result("headlessCollectionsApi", 255,
                               "execute the bounded Java 8 modding collection profile");
    require_jdk8_string_result("headlessIoApi", 15,
                               "execute buffered streams and AutoCloseable compatibility");
    require_jdk8_string_result("headlessCompatibilityExceptions", 15,
                               "headless profile failures preserve Java exceptions");
    require_jdk8_string_result("utilExceptions", 15,
                               "java.util failures unwind through Java exceptions");
    require_jdk8_string_result("timeApi", 1023,
                               "execute Date TimeZone and Calendar Gregorian semantics");
    require_jdk8_string_result("timeExceptions", 15,
                               "time API failures unwind through Java exceptions");
    require_jdk8_string_result("ioRoundTrip", 1023,
                               "round-trip DataInputStream and DataOutputStream values");
    require_jdk8_string_result("legacyRawNullUtf", 1,
                               "decode KVM-era raw NUL in DataInputStream.readUTF");
    require_jdk8_string_result("byteArrayStreams", 15,
                               "execute byte-array stream mark reset skip and writeTo");
    require_jdk8_string_result("ioExceptions", 15,
                               "java.io EOF UTF and bounds failures unwind correctly");
    require_jdk8_string_result("classApi", 511,
                               "execute java.lang.Class lookup and resource APIs");
    require_jdk8_string_result("classExtendedApi", 63,
                               "execute Class assignability component and primitive APIs");
    require_jdk8_string_result("cloneApi", 15,
                               "Object and array clone preserve shallow-copy semantics");
    require_jdk8_string_result("cloneExceptions", 1,
                               "non-Cloneable objects throw CloneNotSupportedException");
    require_jdk8_string_result("reflectiveNewInstance", 1,
                               "Class.newInstance allocates and runs public constructors");
    require_jdk8_string_result("reflectiveNewInstanceExceptions", 15,
                               "Class.newInstance preserves reflection exceptions");
    require_jdk8_string_result("classExceptions", 3,
                               "Class.forName and missing resources preserve Java semantics");
    require_jdk8_string_result("systemRuntimeApi", 511,
                               "expose CLDC properties Runtime singleton and Object.toString");

    auto missing_native = machine.invoke_static("corefixture/Dispatch",
                                                "missingNativeCall",
                                                "()I");
    require(missing_native.has_value() &&
                missing_native->return_value.has_value(),
            "unregistered native method is catchable in Java");
    auto missing_native_value = missing_native->return_value->as_int();
    require(missing_native_value.has_value() && *missing_native_value == 56,
            "unregistered native method throws UnsatisfiedLinkError");

    auto monitor_result = machine.invoke_static("corefixture/MonitorOps",
                                                "reentrantMonitor",
                                                "()I");
    require(monitor_result.has_value() &&
                monitor_result->return_value.has_value(),
            "execute monitorenter and monitorexit bytecodes");
    auto monitor_value = monitor_result->return_value->as_int();
    require(monitor_value.has_value() && *monitor_value == 57,
            "nested synchronized blocks preserve reentrant ownership");

    auto synchronized_methods = machine.invoke_static(
        "corefixture/MonitorOps",
        "synchronizedMethods",
        "()I");
    require(synchronized_methods.has_value() &&
                synchronized_methods->return_value.has_value(),
            "execute instance and static synchronized methods");
    auto synchronized_value = synchronized_methods->return_value->as_int();
    require(synchronized_value.has_value() && *synchronized_value == 62,
            "synchronized methods acquire and release method monitors");

    auto monitor_receiver = machine.class_states().allocate_instance(
        machine.heap(),
        "corefixture/MonitorOps");
    require(monitor_receiver.has_value(),
            "allocate synchronized-method receiver");
    auto monitor_constructor = machine.invoke_instance(*monitor_receiver,
                                                       "corefixture/MonitorOps",
                                                       "<init>",
                                                       "()V");
    require(monitor_constructor.has_value() &&
                monitor_constructor->completed_normally(),
            "initialize synchronized-method receiver");
    auto monitor_failure = machine.invoke_instance(*monitor_receiver,
                                                   "corefixture/MonitorOps",
                                                   "fail",
                                                   "()V");
    require(monitor_failure.has_value() &&
                !monitor_failure->completed_normally(),
            "synchronized method may unwind with Java exception");
    auto released_monitor = machine.monitors().snapshot(*monitor_receiver);
    require(released_monitor.has_value() &&
                released_monitor->owner == 0U &&
                released_monitor->recursion == 0U,
            "synchronized method releases monitor during exception unwind");

    auto abstract_instance = machine.class_states().allocate_instance(
        machine.heap(),
        "corefixture/AbstractOperation");
    require(!abstract_instance.has_value(),
            "class allocator rejects abstract classes");

    auto raw_abstract = machine.heap().allocate_object(
        "corefixture/AbstractOperation", 0);
    require(raw_abstract.has_value(),
            "construct raw abstract receiver for dispatch regression");
    const phoneme::vm::Value abstract_arguments[] {
        phoneme::vm::Value::from_int(1),
    };
    auto abstract_call = machine.invoke_instance(
        *raw_abstract,
        "corefixture/AbstractOperation",
        "apply",
        "(I)I",
        abstract_arguments);
    require(abstract_call.has_value() &&
                !abstract_call->completed_normally() &&
                abstract_call->throwable.has_value(),
            "abstract dispatch returns a Java throwable");
    auto abstract_throwable = machine.heap().class_name(
        *abstract_call->throwable);
    require(abstract_throwable.has_value() &&
                *abstract_throwable == "java/lang/AbstractMethodError",
            "abstract dispatch throws AbstractMethodError");
}

void test_machine_rms(const std::string& fixture_jar) {
    const std::filesystem::path rms_root =
        std::filesystem::path(fixture_jar).parent_path() / "rms-persistence";
    std::error_code cleanup_error;
    std::filesystem::remove_all(rms_root, cleanup_error);
    require(!cleanup_error, "clear RMS persistence fixture directory");

    const auto invoke_int = [](phoneme::vm::Machine& machine,
                               const char* method,
                               phoneme::i32 expected,
                               const char* message) {
        auto result = machine.invoke_static("corefixture/Jdk8Semantics",
                                            method,
                                            "()I");
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                message);
        auto value = result->return_value->as_int();
        require(value.has_value() && *value == expected, message);
    };

    {
        phoneme::vm::ClassRepository classes;
        require(classes.add_archive(fixture_jar).has_value(),
                "add RMS fixture JAR to first VM classpath");
        phoneme::vm::Machine machine(classes);
        require(machine.configure_record_store_root(rms_root.string()).has_value(),
                "configure first VM RMS root");
        invoke_int(machine, "rmsCreate", 1023,
                   "create mutate enumerate and close RMS store");
        invoke_int(machine, "rmsExceptions", 15,
                   "RMS failures unwind through MIDP exceptions");
    }

    {
        phoneme::vm::ClassRepository classes;
        require(classes.add_archive(fixture_jar).has_value(),
                "add RMS fixture JAR to second VM classpath");
        phoneme::vm::Machine machine(classes);
        require(machine.configure_record_store_root(rms_root.string()).has_value(),
                "configure second VM RMS root");
        invoke_int(machine, "rmsReadPersistent", 1,
                   "RMS records survive a fresh VM instance");
        invoke_int(machine, "rmsDeletePersistent", 1,
                   "delete persisted RMS store and update listing");
    }

    std::filesystem::remove_all(rms_root, cleanup_error);
    require(!cleanup_error, "remove RMS persistence fixture directory");
}

void test_machine_filesystem(const std::string& fixture_jar) {
    const std::filesystem::path root =
        std::filesystem::path(fixture_jar).parent_path() /
        "filesystem-sandbox";
    const std::filesystem::path temporary =
        std::filesystem::path(fixture_jar).parent_path() /
        "filesystem-temporary";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    require(!cleanup_error, "clear filesystem fixture directory");
    std::filesystem::remove_all(temporary, cleanup_error);
    require(!cleanup_error, "clear filesystem temporary directory");

    phoneme::filesystem::FileSystem files;
    require(files.configure(root.string(), temporary.string()).has_value(),
            "configure application filesystem sandbox");
    require(!phoneme::filesystem::normalize_virtual_path("../escape")
                 .has_value(),
            "reject virtual path traversal");
    require(!phoneme::filesystem::path_from_file_url(
                 "file:///save/../../escape")
                 .has_value(),
            "reject file URL traversal");
    require(files.create_directory("native").has_value(),
            "create directory inside sandbox");
    auto output = files.open("native/data.bin",
                             phoneme::filesystem::OpenMode::write,
                             true, true);
    require(output.has_value(), "open sandbox output handle");
    const std::array<phoneme::u8, 4> payload {1, 2, 3, 4};
    auto written = files.write(*output, payload);
    require(written.has_value() && *written == payload.size(),
            "write bytes through runtime-owned handle");
    require(files.flush(*output).has_value(), "flush sandbox output handle");
    require(files.close(*output).has_value(), "close sandbox output handle");
    require(!files.write(*output, payload).has_value(),
            "reject use of a closed runtime-owned handle");

    auto input = files.open("native/data.bin",
                            phoneme::filesystem::OpenMode::read,
                            false, false);
    require(input.has_value(), "open sandbox input handle");
    std::array<phoneme::u8, 4> loaded {};
    auto read = files.read(*input, loaded);
    require(read.has_value() && *read == loaded.size() && loaded == payload,
            "read bytes through runtime-owned handle");
    require(files.close(*input).has_value(), "close sandbox input handle");
    auto temporary_file = files.create_temporary("background-io");
    require(temporary_file.has_value() && temporary_file->handle > 0 &&
                std::filesystem::exists(temporary_file->host_path),
            "create isolated runtime temporary file");
    require(files.close(temporary_file->handle).has_value(),
            "close temporary file handle");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add filesystem fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);
    int file_read_prompts = 0;
    int file_write_prompts = 0;
    auto file_policy =
        std::make_shared<phoneme::security::PermissionPolicy>();
    require(file_policy->configure(
                phoneme::security::PermissionPolicyConfig {
                    .suite_id = phoneme::SuiteId {201},
                    .trust = phoneme::security::SuiteTrust::untrusted,
                    .declared_permissions = {
                        std::string(phoneme::security::permissions::connector_file_read),
                        std::string(phoneme::security::permissions::connector_file_write),
                    },
                    .enforce_declared_permissions = true,
                    .prompt = [&](const phoneme::security::PermissionRequest& request) {
                        if (request.permission ==
                            phoneme::security::permissions::connector_file_read) {
                            ++file_read_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::connector_file_write) {
                            ++file_write_prompts;
                        }
                        return phoneme::security::PermissionResponse {
                            phoneme::security::PermissionDecision::allowed,
                            phoneme::security::PermissionScope::session,
                        };
                    },
                }).has_value(),
            "configure filesystem permission policy");
    machine.set_permission_policy(file_policy);
    require(machine.configure_filesystem(root.string(), temporary.string())
                .has_value(),
            "configure VM filesystem sandbox");
    const auto invoke = [&machine](const char* method,
                                   phoneme::i32 expected,
                                   const char* message) {
        auto result = machine.invoke_static("corefixture/FileOps",
                                            method,
                                            "()I",
                                            {},
                                            20'000'000);
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                message);
        auto value = result->return_value->as_int();
        require(value.has_value() && *value == expected, message);
    };
    invoke("resourceLookup", 1,
           "Class.getResourceAsStream resolves relative and absolute resources");
    invoke("resourceTraversalBlocked", 1,
           "Class.getResourceAsStream rejects archive-root traversal");
    invoke("fileRoundTrip", 1,
           "JSR-75 FileConnection round trip and enumeration succeed");
    invoke("javaIoFileCompatibility", 1,
           "Java 8 File streams and FileConnection navigation succeed");
    invoke("suppressedExceptions", 1,
           "Throwable suppressed exception semantics succeed");
    invoke("traversalBlocked", 1,
           "JSR-75 rejects paths outside the application sandbox");
    invoke("closedHandleRejected", 1,
           "java.io file stream rejects access after close");
    require(file_read_prompts == 1 && file_write_prompts == 1,
            "filesystem gates prompt once per session permission");

    std::filesystem::remove_all(root, cleanup_error);
    require(!cleanup_error, "remove filesystem fixture directory");
    std::filesystem::remove_all(temporary, cleanup_error);
    require(!cleanup_error, "remove filesystem temporary directory");
}

void test_machine_network(const std::string& fixture_jar) {
    auto absolute = phoneme::network::Url::parse(
        "https://[2001:db8::1]:8443/a/../b?q=1#part");
    require(absolute.has_value(), "parse HTTPS IPv6 URL");
    require(absolute->host == "2001:db8::1" &&
                absolute->effective_port() == 8443 &&
                absolute->request_target() == "/a/../b?q=1",
            "preserve URL authority and request target");
    auto redirected = phoneme::network::Url::resolve(
        *absolute, "../next?x=2");
    require(redirected.has_value() && redirected->path == "/next" &&
                redirected->query == "x=2",
            "resolve relative HTTP redirect URL");
    require(!phoneme::network::Url::parse("socket://host").has_value(),
            "reject socket URL without port");
    require(phoneme::network::Url::parse("socket://:0").has_value(),
            "allow ephemeral local server socket port");
    require(phoneme::network::Url::parse("datagram://:0").has_value(),
            "allow ephemeral local datagram port");
    require(!phoneme::network::Url::parse(
                "socket://remote.test:0").has_value(),
            "reject zero remote socket port");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add network fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);
    int socket_prompts = 0;
    int server_socket_prompts = 0;
    int datagram_prompts = 0;
    int datagram_receiver_prompts = 0;
    int http_prompts = 0;
    int https_prompts = 0;
    int sms_send_prompts = 0;
    int sms_receive_prompts = 0;
    auto network_policy =
        std::make_shared<phoneme::security::PermissionPolicy>();
    require(network_policy->configure(
                phoneme::security::PermissionPolicyConfig {
                    .suite_id = phoneme::SuiteId {202},
                    .trust = phoneme::security::SuiteTrust::untrusted,
                    .declared_permissions = {
                        std::string(phoneme::security::permissions::connector_socket),
                        std::string(phoneme::security::permissions::connector_server_socket),
                        std::string(phoneme::security::permissions::connector_datagram),
                        std::string(phoneme::security::permissions::connector_datagram_receiver),
                        std::string(phoneme::security::permissions::connector_http),
                        std::string(phoneme::security::permissions::connector_https),
                        std::string(phoneme::security::permissions::wireless_sms_send),
                        std::string(phoneme::security::permissions::wireless_sms_receive),
                    },
                    .enforce_declared_permissions = true,
                    .prompt = [&](const phoneme::security::PermissionRequest& request) {
                        if (request.permission ==
                            phoneme::security::permissions::connector_socket) {
                            ++socket_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::connector_server_socket) {
                            ++server_socket_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::connector_datagram) {
                            ++datagram_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::connector_datagram_receiver) {
                            ++datagram_receiver_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::connector_http) {
                            ++http_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::connector_https) {
                            ++https_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::wireless_sms_send) {
                            ++sms_send_prompts;
                        } else if (request.permission ==
                                   phoneme::security::permissions::wireless_sms_receive) {
                            ++sms_receive_prompts;
                        }
                        return phoneme::security::PermissionResponse {
                            phoneme::security::PermissionDecision::allowed,
                            phoneme::security::PermissionScope::session,
                        };
                    },
                }).has_value(),
            "configure network permission policy");
    machine.set_permission_policy(network_policy);
    auto adapter = std::make_shared<phoneme::tests::FakeNetworkAdapter>();
    require(machine.configure_network_owner(101).has_value(),
            "configure MIDlet network owner");
    require(machine.configure_network_adapter(adapter).has_value(),
            "configure fake async network adapter");

    const auto invoke_integer = [&](const char* method,
                                    const char* description) {
        auto result = machine.invoke_static("NetworkOps", method, "()I",
                                            {}, 20'000'000);
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                description);
        auto value = result->return_value->as_int();
        require(value.has_value(), description);
        return *value;
    };

    require(invoke_integer("socketRoundTrip",
                           "execute socket GCF fixture") == 4411,
            "socket fixture writes and reads through native streams");
    const phoneme::usize bulk_write_count_before = adapter->write_count();
    require(invoke_integer("nestedDataOutputBulkWrite",
                           "execute nested DataOutputStream fixture") ==
                0x12345678,
            "nested DataOutputStream preserves bulk network writes");
    require(adapter->write_count() == bulk_write_count_before + 1U,
            "DataOutputStream through BufferedOutputStream uses one socket write");

    const phoneme::usize exact_read_count_before = adapter->read_count();
    require(invoke_integer("socketExactReadSemantics",
                           "execute exact socket read fixture") == 2016,
            "small socket reads preserve payload order and content");
    require(adapter->read_count() == exact_read_count_before + 3U,
            "unbuffered phoneME socket reads preserve each Java request");
    require(adapter->last_read_maximum_bytes() == 56U,
            "native socket read receives the exact Java-requested length");

    const phoneme::usize large_read_count_before = adapter->read_count();
    const phoneme::usize large_write_count_before = adapter->write_count();
    adapter->set_maximum_read_chunk(16U * 1024U);
    require(invoke_integer("socketLargePayload",
                           "execute partial 64 KiB socket payload fixture") == 1,
            "large Java socket payload round-trips across partial reads");
    adapter->set_maximum_read_chunk(0U);
    require(adapter->read_count() == large_read_count_before + 4U,
            "DataInputStream.readFully continues across four 16 KiB reads");
    require(adapter->last_read_maximum_bytes() == 16U * 1024U,
            "final partial read requests the remaining 16 KiB");
    require(adapter->write_count() == large_write_count_before + 1U,
            "64 KiB Java write remains one bulk socket operation");

    require(invoke_integer("streamSurvivesConnectionClose",
                           "execute connection ownership fixture") == 0x33,
            "stream remains valid after Connection.close");
    require(invoke_integer("serverSocketOpenClose",
                           "execute server socket permission fixture") == 12345,
            "server socket opens and closes through GCF");
    require(invoke_integer("datagramReceiverOpenClose",
                           "execute datagram receiver permission fixture") == 1,
            "datagram receiver opens and closes through GCF");
    require(invoke_integer("datagramRoundTrip",
                           "execute UDP datagram fixture") == 0x01020304,
            "datagram preserves DataInput DataOutput byte order");
    require(invoke_integer("httpRoundTrip",
                           "execute HTTP fixture") == 278,
            "HTTP fixture exposes MIDP header index date and port semantics");
    auto request = adapter->last_http_request();
    require(request.has_value() && request->method == "POST" &&
                request->body == std::vector<phoneme::u8> {0x41} &&
                !request->headers.empty(),
            "HTTP request preserves method headers and output body");
    require(invoke_integer("runtimeConstantSurface",
                           "execute runtime GCF constants fixture") == 14043,
            "GCF constants are initialized for getstatic bytecode");
    require(invoke_integer("certificateExceptionRoundTrip",
                           "execute CertificateException fixture") == 241,
            "CertificateException preserves Throwable message cause and "
            "certificate reason state");
    require(invoke_integer("httpsRoundTrip",
                           "execute HTTPS adapter fixture") == 288,
            "HTTPS exposes TLS and certificate security metadata");
    adapter->set_defer_reads(true);
    require(invoke_integer("interruptedRead",
                           "execute interrupted network read fixture") == 2,
            "Thread.interrupt aborts read with InterruptedIOException");
    adapter->set_defer_reads(false);

    const auto invoke_wireless = [&](const char* method,
                                     const char* description) {
        auto result = machine.invoke_static(
            "WirelessMessagingOps", method, "()I", {}, 20'000'000);
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                description);
        auto value = result->return_value->as_int();
        require(value.has_value(), description);
        return *value;
    };
    require(invoke_wireless("classSurface",
                            "load WMA built-in interfaces") == 1,
            "WMA classes resolve through Class.forName");
    require(invoke_wireless("textRoundTrip",
                            "execute WMA text message fixture") == 1,
            "WMA text message preserves address payload and segmentation");
    require(invoke_wireless("binaryRoundTrip",
                            "execute WMA binary message fixture") == 1,
            "WMA binary message preserves payload and segmentation");
    require(invoke_wireless("sendRequiresHost",
                            "execute WMA send fallback fixture") == 1,
            "WMA send reports unavailable host transport as IOException");
    require(invoke_wireless("receiveRequiresHost",
                            "execute WMA receive fallback fixture") == 1,
            "WMA receive reports unavailable host transport without blocking");

    require(adapter->open_handle_count() == 0U,
            "all Java network handles close after fixture execution");
    require(socket_prompts == 1 && server_socket_prompts == 1 &&
                datagram_prompts == 1 && datagram_receiver_prompts == 1 &&
                http_prompts == 1 && https_prompts == 1 &&
                sms_send_prompts == 1 && sms_receive_prompts == 1,
            "network and WMA gates prompt once per session permission and "
            "scheme");

    auto reconnect_adapter =
        std::make_shared<phoneme::tests::FakeNetworkAdapter>();
    phoneme::network::ConnectionRegistry first(reconnect_adapter);
    phoneme::network::ConnectionRegistry second(reconnect_adapter);
    first.set_owner(1);
    second.set_owner(2);
    auto first_connection = first.open(
        "socket://first.test:1001",
        phoneme::network::ConnectionMode::read_write, true);
    auto second_connection = second.open(
        "socket://second.test:1002",
        phoneme::network::ConnectionMode::read_write, true);
    require(first_connection.has_value() && second_connection.has_value(),
            "open isolated registry connections");
    require(first.reconnect(first_connection->token).has_value(),
            "reconnect replaces only the owning registry handle");
    require(first.close(first_connection->token).has_value(),
            "close first MIDlet connection");
    require(reconnect_adapter->open_handle_count() == 1U,
            "closing one MIDlet leaves the other MIDlet connection open");
    require(second.close(second_connection->token).has_value(),
            "close second MIDlet connection");
    require(reconnect_adapter->open_handle_count() == 0U,
            "per-MIDlet registries release all owned handles");
}

void test_machine_media(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add media fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);
    int capture_prompts = 0;
    auto media_policy =
        std::make_shared<phoneme::security::PermissionPolicy>();
    require(media_policy->configure(
                phoneme::security::PermissionPolicyConfig {
                    .suite_id = phoneme::SuiteId {203},
                    .trust = phoneme::security::SuiteTrust::untrusted,
                    .declared_permissions = {
                        std::string(phoneme::security::permissions::media_capture_audio),
                    },
                    .enforce_declared_permissions = true,
                    .prompt = [&](const phoneme::security::PermissionRequest& request) {
                        if (request.permission ==
                            phoneme::security::permissions::media_capture_audio) {
                            ++capture_prompts;
                        }
                        return phoneme::security::PermissionResponse {
                            phoneme::security::PermissionDecision::allowed,
                            phoneme::security::PermissionScope::session,
                        };
                    },
                }).has_value(),
            "configure media permission policy");
    machine.set_permission_policy(media_policy);
    auto capture = machine.invoke_static(
        "corefixture/SecurityOps",
        "captureAllowedButUnsupported",
        "()I");
    require(capture.has_value() && capture->completed_normally() &&
                capture->return_value.has_value() &&
                capture->return_value->as_int().has_value() &&
                *capture->return_value->as_int() == 1 &&
                capture_prompts == 1,
            "media capture gate prompts before unsupported capture backend");

    auto result = machine.invoke_static("corefixture/MediaOps",
                                        "run",
                                        "()I",
                                        {},
                                        20'000'000);
    if (!result) {
        std::cerr << "MMAPI fixture VM error: "
                  << result.error().message << '\n';
    } else if (result->throwable.has_value()) {
        auto throwable = machine.heap().class_name(*result->throwable);
        std::cerr << "MMAPI fixture throwable: "
                  << (throwable ? *throwable : std::string("unknown"))
                  << '\n';
    }
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "execute MMAPI lifecycle and control fixture");
    auto events = result->return_value->as_int();
    if (!events || *events != 15) {
        std::cerr << "MMAPI fixture result: "
                  << (events ? std::to_string(*events) : std::string("invalid"))
                  << '\n';
    }
    require(events.has_value() && *events == 15,
            "MMAPI fixture receives start stop close and volume events");
}

void test_machine_xml(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add XML fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    auto result = machine.invoke_static("corefixture/XmlOps",
                                        "run",
                                        "()I",
                                        {},
                                        40'000'000);
    if (!result) {
        std::cerr << "XML fixture VM error: "
                  << result.error().message << '\n';
    } else if (result->throwable.has_value()) {
        auto throwable = machine.heap().class_name(*result->throwable);
        std::cerr << "XML fixture throwable: "
                  << (throwable ? *throwable : std::string("unknown"))
                  << '\n';
    }
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "execute JAXP SAX fixture");
    auto status = result->return_value->as_int();
    require(status.has_value() && *status == 0,
            "SAX parser preserves attributes entities CDATA and callbacks");
}

void test_machine_graphics(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add graphics fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    auto result = machine.invoke_static("corefixture/GraphicsOps",
                                        "run",
                                        "()I",
                                        {},
                                        40'000'000);
    if (!result) {
        std::cerr << "Graphics fixture VM error: "
                  << result.error().message << '\n';
    } else if (result->throwable.has_value()) {
        auto throwable = machine.heap().class_name(*result->throwable);
        std::cerr << "Graphics fixture throwable: "
                  << (throwable ? *throwable : std::string("unknown"))
                  << '\n';
    }
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "execute MIDP Image Graphics Font and PNG fixture");
    auto status = result->return_value->as_int();
    if (!status || *status != 0) {
        std::cerr << "Graphics fixture result: "
                  << (status ? std::to_string(*status) : std::string("invalid"))
                  << '\n';
    }
    require(status.has_value() && *status == 0,
            "graphics fixture preserves pixels transforms alpha and metrics");
}

void test_machine_micro3d(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add Micro3D fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    const auto invoke = [&](const char* method, int expected,
                            const char* operation) {
        auto result = machine.invoke_static("corefixture/Micro3dOps",
                                            method,
                                            "()I",
                                            {},
                                            20'000'000);
        if (!result) {
            std::cerr << operation << " VM error: "
                      << result.error().message << '\n';
        } else if (result->throwable.has_value()) {
            auto throwable = machine.heap().class_name(*result->throwable);
            std::cerr << operation << " throwable: "
                      << (throwable ? *throwable : std::string("unknown"))
                      << '\n';
        }
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                operation);
        auto value = result->return_value->as_int();
        if (!value || *value != expected) {
            std::cerr << operation << " result: "
                      << (value ? std::to_string(*value) : std::string("invalid"))
                      << " expected: " << expected << '\n';
        }
        require(value.has_value() && *value == expected, operation);
    };

    invoke("vectorOps", -187, "execute Micro3D vector fixture");
    invoke("affineOps", 112233, "execute Micro3D affine fixture");
    invoke("rotationOps", 4096, "execute Micro3D rotation fixture");
    invoke("rotationTranslationOps", 10'411'630,
           "execute Micro3D rotation translation-preservation fixture");
    invoke("utilOps", 8448, "execute Micro3D fixed-point utility fixture");
    invoke("resourceFormatOps", 1'196'708,
           "execute Micro3D MTRA and MBAC parser fixture");
    invoke("invalidFormatOps", 1,
           "execute Micro3D invalid-format exception fixture");
    invoke("classAndConstantOps", 77,
           "execute Micro3D class and constant fixture");
}

void test_machine_game_api(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add MIDP Game API fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);

    auto result = machine.invoke_static("corefixture/GameApiOps",
                                        "run",
                                        "()I",
                                        {},
                                        80'000'000);
    if (!result) {
        std::cerr << "Game API fixture VM error: "
                  << result.error().message << '\n';
    } else if (result->throwable.has_value()) {
        auto throwable = machine.heap().class_name(*result->throwable);
        std::cerr << "Game API fixture throwable: "
                  << (throwable ? *throwable : std::string("unknown"))
                  << '\n';
    }
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "execute MIDP Layer Sprite TiledLayer and LayerManager fixture");
    auto status = result->return_value->as_int();
    require(status.has_value() && *status == 0,
            "MIDP Game API fixture preserves transforms collisions and painting");
}

void test_security_policy(const std::string& fixture_jar) {
    namespace security = phoneme::security;
    const auto security_root =
        std::filesystem::path(fixture_jar).parent_path() / "security-policy";
    const auto decision_file = security_root / "101.permissions";
    std::error_code cleanup_error;
    std::filesystem::remove_all(security_root, cleanup_error);
    require(!cleanup_error, "clear security policy test directory");

    int http_prompts = 0;
    int file_prompts = 0;
    int media_prompts = 0;
    security::PermissionPolicy policy;
    require(policy.configure(security::PermissionPolicyConfig {
                .suite_id = phoneme::SuiteId {101},
                .trust = security::SuiteTrust::untrusted,
                .persistence_path = decision_file.string(),
                .declared_permissions = {
                    std::string(security::permissions::connector_http),
                    std::string(security::permissions::connector_file_read),
                    std::string(security::permissions::media_record),
                },
                .enforce_declared_permissions = true,
                .prompt = [&](const security::PermissionRequest& request) {
                    if (request.domain == security::PermissionDomain::network) {
                        ++http_prompts;
                        return security::PermissionResponse {
                            security::PermissionDecision::allowed,
                            security::PermissionScope::one_shot,
                        };
                    }
                    if (request.domain ==
                        security::PermissionDomain::filesystem) {
                        ++file_prompts;
                        return security::PermissionResponse {
                            security::PermissionDecision::allowed,
                            security::PermissionScope::session,
                        };
                    }
                    ++media_prompts;
                    return security::PermissionResponse {
                        security::PermissionDecision::denied,
                        security::PermissionScope::blanket,
                    };
                },
            }).has_value(),
            "configure permission policy");

    require(policy.request(security::permissions::connector_http).has_value() &&
                policy.request(security::permissions::connector_http).has_value() &&
                http_prompts == 2,
            "one-shot permission is not cached");
    require(policy.request(security::permissions::connector_file_read).has_value() &&
                policy.request(security::permissions::connector_file_read).has_value() &&
                file_prompts == 1 &&
                policy.check(security::permissions::connector_file_read) ==
                    security::PermissionDecision::allowed,
            "session permission is cached in memory");

    auto denied = policy.require(security::permissions::media_record);
    require(!denied.has_value() &&
                denied.error().java_exception_class ==
                    "java/lang/SecurityException" &&
                media_prompts == 1,
            "permission denial maps to SecurityException");
    auto undeclared = policy.request("vendor.permission.undeclared");
    require(undeclared.has_value() &&
                undeclared->decision == security::PermissionDecision::denied,
            "undeclared permission is denied without prompt");

    security::PermissionPolicy reloaded;
    require(reloaded.configure(security::PermissionPolicyConfig {
                .suite_id = phoneme::SuiteId {101},
                .trust = security::SuiteTrust::untrusted,
                .persistence_path = decision_file.string(),
                .declared_permissions = {
                    std::string(security::permissions::connector_http),
                    std::string(security::permissions::connector_file_read),
                    std::string(security::permissions::media_record),
                },
                .enforce_declared_permissions = true,
            }).has_value(),
            "reload permission policy");
    require(reloaded.check(security::permissions::media_record) ==
                security::PermissionDecision::denied &&
                reloaded.check(security::permissions::connector_file_read) ==
                security::PermissionDecision::unknown,
            "blanket persists while session does not");

    security::PermissionPolicy isolated;
    require(isolated.configure(security::PermissionPolicyConfig {
                .suite_id = phoneme::SuiteId {103},
                .trust = security::SuiteTrust::untrusted,
                .persistence_path =
                    (security_root / "103.permissions").string(),
            }).has_value(),
            "configure isolated suite permission policy");
    require(isolated.check(security::permissions::media_record) ==
                security::PermissionDecision::unknown,
            "blanket decisions are isolated per suite");

    int trusted_prompts = 0;
    security::PermissionPolicy trusted;
    require(trusted.configure(security::PermissionPolicyConfig {
                .suite_id = phoneme::SuiteId {102},
                .trust = security::SuiteTrust::trusted,
                .prompt = [&](const security::PermissionRequest&) {
                    ++trusted_prompts;
                    return security::PermissionResponse {
                        security::PermissionDecision::denied,
                        security::PermissionScope::one_shot,
                    };
                },
            }).has_value(),
            "configure trusted permission policy");
    auto trusted_result = trusted.request("vendor.permission.compat");
    require(trusted_result.has_value() &&
                trusted_result->decision ==
                    security::PermissionDecision::allowed &&
                trusted_result->scope == security::PermissionScope::session &&
                trusted_prompts == 0,
            "trusted suite bypasses prompt with compatibility session grant");

    require(security::PermissionPolicy::domain_for_permission(
                security::permissions::connector_socket) ==
                security::PermissionDomain::network &&
                security::PermissionPolicy::domain_for_permission(
                    security::permissions::connector_file_write) ==
                    security::PermissionDomain::filesystem &&
                security::PermissionPolicy::domain_for_permission(
                    security::permissions::media_capture_audio) ==
                    security::PermissionDomain::media,
            "permission names map to subsystem domains");

    std::filesystem::remove_all(security_root, cleanup_error);
    require(!cleanup_error, "remove security policy test directory");
}

void test_machine_security(const std::string& fixture_jar) {
    namespace security = phoneme::security;
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add security fixture JAR");
    phoneme::vm::Machine machine(classes);
    auto policy = std::make_shared<security::PermissionPolicy>();
    require(policy->configure(security::PermissionPolicyConfig {
                .suite_id = phoneme::SuiteId {77},
                .trust = security::SuiteTrust::untrusted,
                .prompt = [](const security::PermissionRequest& request) {
                    return security::PermissionResponse {
                        request.domain == security::PermissionDomain::filesystem
                            ? security::PermissionDecision::allowed
                            : security::PermissionDecision::denied,
                        security::PermissionScope::session,
                    };
                },
            }).has_value(),
            "configure machine permission policy");
    require(policy->set_blanket_decision(
                security::permissions::connector_http,
                security::PermissionDecision::allowed).has_value(),
            "allow HTTP for Java permission fixture");
    require(policy->set_blanket_decision(
                security::permissions::media_record,
                security::PermissionDecision::denied).has_value(),
            "deny media for Java permission fixture");
    machine.set_permission_policy(policy);

    const auto invoke = [&](const char* method, phoneme::i32 expected) {
        auto result = machine.invoke_static("corefixture/SecurityOps",
                                            method,
                                            "()I");
        require(result.has_value() && result->completed_normally() &&
                    result->return_value.has_value(),
                "invoke Java security fixture");
        auto value = result->return_value->as_int();
        require(value.has_value() && *value == expected,
                "Java security fixture result");
    };
    invoke("checkStatuses", 15);
    invoke("requestAndRequire", 3);
    require(policy->set_blanket_decision(
                security::permissions::connector_file_read,
                security::PermissionDecision::denied).has_value(),
            "deny file reads for subsystem gate fixture");
    require(policy->set_blanket_decision(
                security::permissions::media_capture_audio,
                security::PermissionDecision::denied).has_value(),
            "deny audio capture for subsystem gate fixture");
    invoke("subsystemDenials", 7);
}

void test_machine_push(const std::string& fixture_jar) {
    const std::filesystem::path push_root =
        std::filesystem::path(fixture_jar).parent_path() / "push-native";
    std::error_code cleanup_error;
    std::filesystem::remove_all(push_root, cleanup_error);
    require(!cleanup_error, "clear PushRegistry native fixture directory");

    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add PushRegistry fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes);
    require(machine.configure_push_registry(push_root.string(),
                                            phoneme::SuiteId {91}).has_value(),
            "configure VM PushRegistry root");

    auto connection = machine.invoke_static("corefixture/PushOps",
                                            "connectionRoundTrip",
                                            "()I");
    require(connection.has_value() && connection->completed_normally() &&
                connection->return_value.has_value(),
            "PushRegistry connection native methods complete");
    auto connection_result = connection->return_value->as_int();
    require(connection_result.has_value() && *connection_result == 31,
            "PushRegistry connection native methods preserve mappings");

    auto alarm = machine.invoke_static("corefixture/PushOps",
                                       "alarmReplacement",
                                       "()J");
    require(alarm.has_value() && alarm->completed_normally() &&
                alarm->return_value.has_value(),
            "PushRegistry alarm native method completes");
    auto alarm_result = alarm->return_value->as_long();
    require(alarm_result.has_value() && *alarm_result == 1'000,
            "PushRegistry alarm returns the previous registration time");

    auto pending = machine.push_registry().pending_launch_count();
    require(pending.has_value() && *pending == 0U,
            "native fixture leaves no pending launch requests");

    std::filesystem::remove_all(push_root, cleanup_error);
    require(!cleanup_error, "remove PushRegistry native fixture directory");
}

void test_runtime_push_queue(const std::string& fixture_jar) {
    const std::filesystem::path runtime_home =
        std::filesystem::path(fixture_jar).parent_path() /
        "push-runtime-home";
    std::error_code cleanup_error;
    std::filesystem::remove_all(runtime_home, cleanup_error);
    require(!cleanup_error, "clear runtime PushRegistry fixture directory");
    std::filesystem::create_directories(runtime_home, cleanup_error);
    require(!cleanup_error, "create runtime PushRegistry fixture directory");

    phoneme::runtime::Runtime runtime;
    require(runtime.configure(runtime_home.string()).has_value(),
            "configure runtime PushRegistry fixture");
    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(), "install runtime PushRegistry fixture suite");

    phoneme::push::PushRegistry registry;
    require(registry.configure((runtime_home / "push").string(),
                               *suite_id).has_value(),
            "open suite PushRegistry from host");
    require(registry.register_connection("socket://:41002",
                                         "corefixture.PushOps",
                                         "*").has_value(),
            "register host PushRegistry connection");
    require(runtime.notify_push_connection_available(
                       *suite_id, "socket://:41002", 5'000).has_value(),
            "runtime queues available push connection");

    auto blocked = runtime.poll_push_launch_requests(
        *suite_id, 5'000, true, 8U);
    require(blocked.has_value() && blocked->empty(),
            "runtime default policy blocks non-foreground push delivery");
    require(runtime.set_push_background_policy(
                       *suite_id,
                       phoneme::push::BackgroundPolicy::system_managed)
                .has_value(),
            "runtime enables system-managed push delivery");
    auto allowed = runtime.poll_push_launch_requests(
        *suite_id, 5'000, true, 8U);
    require(allowed.has_value() && allowed->size() == 1U,
            "runtime exposes push request during iOS background grant");
    require(runtime.acknowledge_push_launch_request(
                       *suite_id, allowed->front().id).has_value(),
            "runtime acknowledges delivered push request");

    std::filesystem::remove_all(runtime_home, cleanup_error);
    require(!cleanup_error, "remove runtime PushRegistry fixture directory");
}

void test_machine_gc_roots(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add GC fixture JAR to VM classpath");
    phoneme::vm::Machine machine(classes, 8U);

    auto pressure = machine.invoke_static("corefixture/GcOps",
                                          "pressure",
                                          "()I");
    require(pressure.has_value() && pressure->completed_normally() &&
                pressure->return_value.has_value(),
            "heap pressure completes with GC retry");
    auto result = pressure->return_value->as_int();
    require(result.has_value() && *result == 59,
            "GC preserves active local and static roots");
    const auto pressure_stats = machine.heap().stats();
    require(pressure_stats.collections > 0U,
            "heap pressure triggers at least one collection");

    const phoneme::vm::HeapLimits string_pressure_limits {
        .maximum_objects = 100'000U,
        .maximum_bytes = 512U * 1024U,
    };
    phoneme::vm::Machine string_pressure_machine(
        classes, string_pressure_limits);
    auto string_pressure = string_pressure_machine.invoke_static(
        "corefixture/GcOps", "temporaryStringPressure", "()I",
        {}, 250'000'000U);
    require(string_pressure.has_value() &&
                string_pressure->completed_normally() &&
                string_pressure->return_value.has_value() &&
                string_pressure->return_value->as_int().has_value() &&
                *string_pressure->return_value->as_int() == 97,
            "temporary String.valueOf pressure completes without freezing");
    const auto string_pressure_stats = string_pressure_machine.heap().stats();
    require(string_pressure_stats.collections > 0U,
            "native temporary strings trigger proactive collection");
    require(string_pressure_stats.failed_allocations == 0U,
            "proactive collection runs before the Java heap hard limit");

    phoneme::vm::Machine constructor_machine(classes, 8U);
    auto constructor_pressure = constructor_machine.invoke_static(
        "corefixture/GcOps", "nestedConstructorPressure", "()I");
    require(constructor_pressure.has_value() &&
                constructor_pressure->completed_normally() &&
                constructor_pressure->return_value.has_value() &&
                constructor_pressure->return_value->as_int().has_value() &&
                *constructor_pressure->return_value->as_int() == 83,
            "GC preserves a pending constructor receiver across nested arguments");
    require(constructor_machine.heap().stats().collections > 0U,
            "nested constructor pressure triggers collection");

    phoneme::vm::Machine constructor_race_machine(classes, 128U);
    auto constructor_race = constructor_race_machine.invoke_static(
        "corefixture/ThreadOps", "constructorRootRace", "()I",
        {}, 250'000'000U);
    require(constructor_race.has_value() &&
                constructor_race->completed_normally() &&
                constructor_race->return_value.has_value() &&
                constructor_race->return_value->as_int().has_value() &&
                *constructor_race->return_value->as_int() == 0,
            "cross-thread GC preserves a pending constructor receiver");
    require(constructor_race_machine.heap().stats().collections > 0U,
            "constructor root race performs a collection");

    phoneme::vm::Machine class_init_race_machine(classes, 128U);
    auto class_init_race = class_init_race_machine.invoke_static(
        "corefixture/ThreadOps", "constructorClassInitRootRace", "()I",
        {}, 250'000'000U);
    require(class_init_race.has_value() &&
                class_init_race->completed_normally() &&
                class_init_race->return_value.has_value() &&
                class_init_race->return_value->as_int().has_value() &&
                *class_init_race->return_value->as_int() == 0,
            "cross-thread GC preserves an outer constructor receiver while "
            "a nested argument class initializer is blocked");
    require(class_init_race_machine.heap().stats().collections > 0U,
            "constructor class-initialization race performs a collection");

    phoneme::vm::Machine class_init_owner_machine(classes, 256U);
    auto class_init_owner = class_init_owner_machine.invoke_static(
        "corefixture/ThreadOps", "classInitializationWaitsForOwner", "()I",
        {}, 250'000'000U);
    require(class_init_owner.has_value() &&
                class_init_owner->completed_normally() &&
                class_init_owner->return_value.has_value() &&
                class_init_owner->return_value->as_int().has_value() &&
                *class_init_owner->return_value->as_int() == 0,
            "a competing thread waits for the class-initialization owner");

    require(machine.collect_garbage().has_value(),
            "idle machine collection succeeds");
    const auto final_stats = machine.heap().stats();
    require(final_stats.live_objects == 2U,
            "idle collection retains the static root and emergency OOME");

    phoneme::vm::Machine object_oom_machine(classes, 8U);
    auto object_oom = object_oom_machine.invoke_static(
        "corefixture/GcOps", "catchObjectOutOfMemory", "()I");
    require(object_oom.has_value() && object_oom->completed_normally() &&
                object_oom->return_value.has_value() &&
                object_oom->return_value->as_int().has_value() &&
                *object_oom->return_value->as_int() == 71,
            "full heap throws catchable OutOfMemoryError for objects");

    phoneme::vm::Machine array_oom_machine(classes, 8U);
    auto array_oom = array_oom_machine.invoke_static(
        "corefixture/GcOps", "catchArrayOutOfMemory", "()I");
    require(array_oom.has_value() && array_oom->completed_normally() &&
                array_oom->return_value.has_value() &&
                array_oom->return_value->as_int().has_value() &&
                *array_oom->return_value->as_int() == 73,
            "full heap throws catchable OutOfMemoryError for arrays");
}

void test_runtime_lcdui(const std::string& fixture_jar) {
    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(fixture_jar).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime for LCDUI fixture");
    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(), "install LCDUI fixture suite");
    require(runtime.start_system().has_value(),
            "start runtime for LCDUI fixture");
    constexpr phoneme::AppId app_id {22};
    auto lcd_ui_started = runtime.start_midlet(
        *suite_id,
        "corefixture.LcduiApp",
        app_id,
        phoneme::Dimensions {320, 240});
    if (!lcd_ui_started) {
        std::cerr << "LCDUI fixture launch error: "
                  << lcd_ui_started.error().message << '\n';
    }
    require(lcd_ui_started.has_value(),
            "start MIDlet that creates native LCDUI components");
    require(runtime.app_state(app_id) == phoneme::runtime::AppState::active,
            "LCDUI fixture remains active after startApp");
    for (int pump = 0; pump < 8; ++pump) {
        (void)runtime.frame_snapshot();
    }

    bool reset_seen = false;
    bool lifecycle_status_seen = false;
    bool screen_created = false;
    bool screen_shown = false;
    bool screen_updated = false;
    bool status_ready = false;
    bool status_serial = false;
    bool status_running = false;
    bool text_field_updated = false;
    bool gauge_updated = false;
    bool choice_group_shown = false;
    bool easy_choice = false;
    bool hard_choice = false;
    bool date_field_shown = false;
    bool spacer_shown = false;
    bool image_item_shown = false;
    bool action_item_shown = false;
    bool tail_item = false;
    bool ok_command = false;
    bool back_command = false;
    bool menu_command = false;
    bool text_command = false;
    bool alert_command = false;
    phoneme::i32 status_id = 0;
    phoneme::i32 text_field_id = 0;
    phoneme::i32 gauge_id = 0;
    phoneme::i32 choice_id = 0;
    phoneme::i32 date_field_id = 0;
    phoneme::i32 image_item_id = 0;
    phoneme::i32 action_item_id = 0;
    phoneme::i32 ok_command_id = 0;
    phoneme::i32 menu_command_id = 0;
    phoneme::i32 text_command_id = 0;
    phoneme::i32 alert_command_id = 0;
    phoneme::i32 bridged_event_count = 0;

    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 1) {
            require(!screen_created && !screen_shown && bridged_event_count == 0,
                    "LCDUI reset precedes the new MIDlet screen event stream");
            reset_seen = true;
        }
        if (event->kind == 0 &&
            event->detail.find("MIDlet") != std::string::npos) {
            lifecycle_status_seen = true;
        }
        if (event->kind >= 2 && event->kind <= 16) {
            require(reset_seen,
                    "LCDUI bridge events are emitted after presentation reset");
            ++bridged_event_count;
            require(event->component_id == 0 ||
                        event->component_id >= app_id.value * 100'000,
                    "LCDUI component IDs are namespaced by app");
        }
        if (event->kind == 2 && event->component_type == 23 &&
            event->text == "Tiêu đề") {
            screen_created = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Tiêu đề") {
            screen_shown = true;
        }
        if (event->kind == 3 && event->component_type == 23 &&
            event->text == "Native Form") {
            screen_updated = true;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 12 &&
            event->text == "Trạng thái" && event->detail == "Ready") {
            status_ready = true;
            status_id = event->component_id;
        }
        if (event->kind == 8 && event->component_type == 12 &&
            event->text == "Trạng thái" && event->detail == "Serial") {
            status_serial = true;
        }
        if (event->kind == 8 && event->component_type == 12 &&
            event->text == "Trạng thái" && event->detail == "Running") {
            status_running = true;
        }
        if (event->component_type == 15 && event->text == "Tên" &&
            event->detail == "phoneME" &&
            event->arguments[0] == 12 && event->arguments[1] == 0 &&
            event->arguments[2] == 7 && event->arguments[3] == -1001) {
            text_field_updated = true;
            text_field_id = event->component_id;
        }
        if (event->component_type == 7 && event->text == "Tiến độ" &&
            event->arguments[0] == 7 && event->arguments[1] == 10 &&
            event->arguments[2] == 1 && event->arguments[3] == -1002) {
            gauge_updated = true;
            gauge_id = event->component_id;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 1 && event->text == "Chế độ") {
            choice_group_shown = true;
            choice_id = event->component_id;
        }
        if (event->kind == 12 && event->text == "Easy" &&
            event->arguments[0] == 1) {
            easy_choice = true;
            choice_id = event->component_id;
        }
        if (event->kind == 12 && event->text == "Hard" &&
            event->arguments[0] == 0) {
            hard_choice = true;
            choice_id = event->component_id;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 5 && event->text == "Ngày" &&
            event->arguments[1] == 3 && event->arguments[3] == -1003 &&
            // phoneME DateField.DATE_TIME discards seconds and
            // milliseconds, so 1700000000000 becomes 1699999980000.
            event->value64 == 1'699'999'980 &&
            event->detail == "GMT+07:00") {
            date_field_shown = true;
            date_field_id = event->component_id;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 11 &&
            event->arguments[2] == 12 && event->arguments[3] == 18) {
            spacer_shown = true;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 8 && event->text == "Preview" &&
            event->detail == "preview" && event->arguments[0] == 2 &&
            event->arguments[1] == 3 && event->arguments[3] == -1004) {
            image_item_shown = true;
            image_item_id = event->component_id;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 14 && event->text == "Action" &&
            event->detail == "Tap") {
            action_item_shown = true;
            action_item_id = event->component_id;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 12 && event->text.empty() &&
            event->detail == "Tail") {
            tail_item = true;
        }
        if (event->kind == 15 && event->text == "OK" &&
            event->arguments[0] == 4 && event->arguments[1] == 1) {
            ok_command = true;
            ok_command_id = event->component_id;
        }
        if (event->kind == 15 && event->text == "Back" &&
            event->detail == "Go back" && event->arguments[0] == 2 &&
            event->arguments[1] == 2) {
            back_command = true;
        }
        if (event->kind == 15 && event->text == "Menu" &&
            event->arguments[0] == 1 && event->arguments[1] == 3) {
            menu_command = true;
            menu_command_id = event->component_id;
        }
        if (event->kind == 15 && event->text == "Text" &&
            event->arguments[0] == 1 && event->arguments[1] == 4) {
            text_command = true;
            text_command_id = event->component_id;
        }
        if (event->kind == 15 && event->text == "Alert" &&
            event->arguments[0] == 1 && event->arguments[1] == 5) {
            alert_command = true;
            alert_command_id = event->component_id;
        }
    }

    require(reset_seen,
            "LCDUI fixture emits a presentation reset before its screen");
    require(lifecycle_status_seen,
            "LCDUI fixture keeps launch diagnostics separate from reset");
    require(bridged_event_count >= 32,
            "LCDUI fixture emits a complete screen and item event stream");
    require(screen_created && screen_shown && screen_updated,
            "LCDUI Form creation visibility and title updates are bridged");
    require(status_ready && status_serial && status_running,
            "StringItem mutation and Display.callSerially are bridged");
    require(text_field_updated,
            "TextField metadata and UTF-8 text are bridged");
    require(gauge_updated,
            "Gauge value and range updates are bridged");
    require(choice_group_shown && easy_choice && hard_choice,
            "ChoiceGroup items and initial selections are bridged");
    require(date_field_shown,
            "DateField epoch mode and timezone metadata are bridged");
    require(spacer_shown,
            "Spacer minimum dimensions are bridged");
    require(image_item_shown,
            "ImageItem dimensions and generation are bridged");
    require(action_item_shown,
            "button StringItem and item command owner are bridged");
    require(tail_item, "Form.append(String) creates a native StringItem");
    require(ok_command && back_command && menu_command && text_command &&
                alert_command,
            "MIDP commands preserve labels types priorities and long labels");
    require(status_id != 0 && text_field_id != 0 && gauge_id != 0 &&
                choice_id != 0 && date_field_id != 0 && image_item_id != 0 &&
                action_item_id != 0 && ok_command_id != 0 &&
                menu_command_id != 0 &&
                text_command_id != 0 && alert_command_id != 0,
            "LCDUI bridge registers interactive component IDs");

    const auto image_metadata = runtime.copy_lcdui_image_rgba(
        image_item_id, {});
    require(image_metadata.dimensions.width == 2 &&
                image_metadata.dimensions.height == 3 &&
                image_metadata.byte_count == 24U &&
                image_metadata.generation > 0U,
            "LCDUI image query exposes dimensions generation and byte count");
    std::vector<phoneme::u8> image_rgba(image_metadata.byte_count, 0U);
    const auto copied_image = runtime.copy_lcdui_image_rgba(
        image_item_id, image_rgba);
    require(copied_image.byte_count == image_rgba.size() &&
                image_rgba[0] == 0x10U && image_rgba[1] == 0x34U &&
                image_rgba[2] == 0x52U && image_rgba[3] == 0xFFU,
            "LCDUI image bridge copies native RGBA pixels");

    runtime.ui_set_text(text_field_id, "Người", 5);
    runtime.ui_set_gauge(gauge_id, 9);
    runtime.ui_set_choice(choice_id, 1, true);
    runtime.ui_set_date(date_field_id, 1'800'000'000);
    runtime.ui_focus_item(text_field_id);
    runtime.ui_select_command(ok_command_id);

    bool native_text_applied = false;
    bool native_gauge_applied = false;
    bool native_choice_applied = false;
    bool native_date_applied = false;
    bool text_state_callback = false;
    bool gauge_state_callback = false;
    bool choice_state_callback = false;
    bool date_state_callback = false;
    bool focus_applied = false;
    bool command_callback_applied = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 8 && event->component_id == text_field_id &&
            event->detail == "Người" && event->arguments[2] == 5) {
            native_text_applied = true;
        }
        if (event->kind == 8 && event->component_id == gauge_id &&
            event->arguments[0] == 9) {
            native_gauge_applied = true;
        }
        if (event->kind == 12 && event->component_id == choice_id &&
            event->text == "Hard" && event->arguments[0] == 1) {
            native_choice_applied = true;
        }
        if (event->kind == 8 && event->component_id == date_field_id &&
            event->component_type == 5 &&
            event->value64 == 1'800'000'000) {
            native_date_applied = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "State Tên") {
            text_state_callback = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "State Tiến độ") {
            gauge_state_callback = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "State Chế độ") {
            choice_state_callback = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "State Ngày") {
            date_state_callback = true;
        }
        if (event->kind == 16 && event->component_id == text_field_id) {
            focus_applied = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "Hard selected") {
            command_callback_applied = true;
        }
    }
    require(native_text_applied && native_gauge_applied &&
                native_choice_applied && native_date_applied && focus_applied,
            "native LCDUI input updates Java item state and emits refresh events");
    require(text_state_callback && gauge_state_callback &&
                choice_state_callback && date_state_callback,
            "Form ItemStateListener receives all native item mutations");
    require(command_callback_applied,
            "native command selection invokes Java CommandListener callback");

    runtime.ui_focus_item(action_item_id);
    bool action_focus_applied = false;
    phoneme::i32 item_command_id = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 16 && event->component_id == action_item_id) {
            action_focus_applied = true;
        }
        if (event->kind == 15 && event->text == "Go" &&
            event->arguments[0] == 8 && event->arguments[1] == 1 &&
            event->arguments[2] == 1 &&
            event->arguments[3] == action_item_id) {
            item_command_id = event->component_id;
        }
    }
    require(action_focus_applied && item_command_id != 0,
            "focused Item exposes its ITEM command with item scope");
    runtime.ui_select_command(item_command_id);
    bool item_command_callback = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "Item Go") {
            item_command_callback = true;
        }
    }
    require(item_command_callback,
            "ItemCommandListener receives the selected item command");

    runtime.ui_select_command(menu_command_id);
    bool list_created = false;
    bool list_shown = false;
    bool first_list_choice = false;
    bool second_list_choice = false;
    phoneme::i32 list_choice_id = 0;
    phoneme::i32 list_item_command_id = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 2 && event->component_type == 23 &&
            event->text == "Menu") {
            list_created = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Menu") {
            list_shown = true;
        }
        if (event->kind == 12 && event->component_type == 2 &&
            event->text == "One" && event->arguments[0] == 1) {
            first_list_choice = true;
            list_choice_id = event->component_id;
        }
        if (event->kind == 12 && event->component_type == 2 &&
            event->text == "Two" && event->arguments[0] == 0) {
            second_list_choice = true;
            list_choice_id = event->component_id;
        }
        if (event->kind == 15 && event->text == "Use" &&
            event->arguments[0] == 8 && event->arguments[1] == 1 &&
            event->arguments[2] == 0 && event->arguments[3] == 0) {
            list_item_command_id = event->component_id;
        }
    }
    require(list_created && list_shown && first_list_choice &&
                second_list_choice && list_choice_id != 0 &&
                list_item_command_id != 0,
            "List screen choices and Command.ITEM are bridged after callback");

    runtime.ui_select_list_item_command(
        list_choice_id, 1, list_item_command_id);
    bool context_selection_applied = false;
    bool list_item_callback_applied = false;
    bool context_form_restored = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 12 && event->component_id == list_choice_id &&
            event->text == "Two" && event->arguments[0] == 1) {
            context_selection_applied = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "List item 1") {
            list_item_callback_applied = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Native Form") {
            context_form_restored = true;
        }
    }
    require(context_selection_applied && list_item_callback_applied &&
                context_form_restored,
            "List Command.ITEM selects the held row without SELECT_COMMAND");

    runtime.ui_select_command(menu_command_id);
    list_choice_id = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 12 && event->component_type == 2 &&
            event->text == "Two") {
            list_choice_id = event->component_id;
        }
    }
    require(list_choice_id != 0,
            "List can be reopened for implicit selection testing");

    runtime.ui_set_choice(list_choice_id, 1, true);
    bool implicit_selection_applied = false;
    bool select_command_callback_applied = false;
    bool form_restored = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 12 && event->component_id == list_choice_id &&
            event->text == "Two" && event->arguments[0] == 1) {
            implicit_selection_applied = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "List 1") {
            select_command_callback_applied = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Native Form") {
            form_restored = true;
        }
    }
    require(implicit_selection_applied && select_command_callback_applied &&
                form_restored,
            "implicit List selection dispatches SELECT_COMMAND and restores Form");

    runtime.ui_select_command(text_command_id);
    bool text_box_created = false;
    bool text_box_kind = false;
    bool text_box_shown = false;
    bool text_box_initial_text = false;
    phoneme::i32 text_box_peer_id = 0;
    phoneme::i32 done_command_id = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 2 && event->component_type == 23 &&
            event->text == "Nhập") {
            text_box_created = true;
        }
        if (event->kind == 3 && event->component_type == 23 &&
            event->text == "Nhập" && event->arguments[0] == 2 &&
            event->arguments[3] == -1006) {
            text_box_kind = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Nhập") {
            text_box_shown = true;
        }
        if ((event->kind == 7 || event->kind == 9) &&
            event->component_type == 15 && event->detail == "abc" &&
            event->arguments[0] == 20 && event->arguments[1] == 0 &&
            event->arguments[2] == 3 && event->arguments[3] == -1001) {
            text_box_initial_text = true;
            text_box_peer_id = event->component_id;
        }
        if (event->kind == 15 && event->text == "Done" &&
            event->arguments[0] == 4 && event->arguments[1] == 1) {
            done_command_id = event->component_id;
        }
    }
    require(text_box_created && text_box_kind && text_box_shown &&
                text_box_initial_text && text_box_peer_id != 0 &&
                done_command_id != 0,
            "TextBox screen peer metadata text and command are bridged");

    runtime.ui_set_text(text_box_peer_id, "Việt Nam", 8);
    runtime.ui_select_command(done_command_id);
    bool text_box_native_text_applied = false;
    bool text_box_callback_applied = false;
    bool text_box_form_restored = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 8 && event->component_id == text_box_peer_id &&
            event->detail == "Việt Nam" && event->arguments[2] == 8) {
            text_box_native_text_applied = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "Text Việt Nam @1800000000") {
            text_box_callback_applied = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Native Form") {
            text_box_form_restored = true;
        }
    }
    require(text_box_native_text_applied && text_box_callback_applied &&
                text_box_form_restored,
            "native TextBox input reaches Java and command restores Form");

    runtime.ui_select_command(alert_command_id);
    bool alert_created = false;
    bool alert_shown = false;
    bool alert_metadata = false;
    phoneme::i32 dismiss_command_id = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 2 && event->component_type == 18 &&
            event->text == "Cảnh báo" &&
            event->detail == "Nội dung Việt") {
            alert_created = true;
        }
        if (event->kind == 4 && event->component_type == 18 &&
            event->text == "Cảnh báo" &&
            event->detail == "Nội dung Việt") {
            alert_shown = true;
            if (event->arguments[0] == -2 &&
                event->arguments[1] != 0 &&
                event->arguments[2] == 3 &&
                event->arguments[3] == -1009) {
                alert_metadata = true;
            }
        }
        if (event->kind == 15 && event->text.empty() &&
            event->arguments[0] == 4 && event->arguments[1] == 0) {
            dismiss_command_id = event->component_id;
        }
    }
    require(alert_created && alert_shown && alert_metadata &&
                dismiss_command_id != 0,
            "Warning Alert body FOREVER timeout next screen and dismiss command are bridged");

    runtime.ui_select_command(dismiss_command_id);
    bool alert_callback_applied = false;
    bool alert_hidden = false;
    bool alert_form_restored = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "Alert dismissed") {
            alert_callback_applied = true;
        }
        if (event->kind == 5 && event->component_type == 18 &&
            event->text == "Cảnh báo") {
            alert_hidden = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Native Form") {
            alert_form_restored = true;
        }
    }
    require(alert_callback_applied && alert_hidden && alert_form_restored,
            "Alert.DISMISS_COMMAND invokes Java listener and restores the next Form");

    require(runtime.destroy_midlet(app_id).has_value(),
            "destroy LCDUI fixture MIDlet");

    constexpr phoneme::AppId timed_alert_app {23};
    require(runtime.start_midlet(
                *suite_id,
                "corefixture.TimedAlertApp",
                timed_alert_app,
                phoneme::Dimensions {320, 240}).has_value(),
            "start timed Alert fixture with a loading Gauge");

    bool loading_alert_shown = false;
    bool loading_gauge_shown = false;
    phoneme::i32 loading_alert_id = 0;
    phoneme::i32 loading_gauge_id = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 4 && event->component_type == 17 &&
            event->text == "Loading" && event->detail == "Complete" &&
            event->arguments[0] == 50) {
            loading_alert_shown = true;
            loading_alert_id = event->component_id;
        }
        if (event->kind == 9 && event->component_type == 6 &&
            event->text == "Progress" && event->arguments[0] == 100 &&
            event->arguments[1] == 100 && event->arguments[2] == 0) {
            loading_gauge_shown = true;
            loading_gauge_id = event->component_id;
        }
    }
    require(loading_alert_shown && loading_gauge_shown &&
                loading_alert_id != 0 && loading_gauge_id != 0,
            "timed loading Alert and its Gauge are shown before expiry");

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    runtime.pump_events();

    bool loading_alert_hidden = false;
    bool loading_gauge_hidden = false;
    bool loaded_form_shown = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 5 &&
            event->component_id == loading_alert_id &&
            event->component_type == 17) {
            loading_alert_hidden = true;
        }
        if (event->kind == 10 &&
            event->component_id == loading_gauge_id &&
            event->parent_id == loading_alert_id) {
            loading_gauge_hidden = true;
        }
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Loaded") {
            loaded_form_shown = true;
        }
    }
    require(loading_alert_hidden && loading_gauge_hidden &&
                loaded_form_shown,
            "native LCDUI pump expires loading Alert and restores next screen");
    require(runtime.destroy_midlet(timed_alert_app).has_value(),
            "destroy timed Alert fixture MIDlet");

    constexpr phoneme::AppId list_image_app {24};
    require(runtime.start_midlet(
                *suite_id,
                "corefixture.ListImageBackApp",
                list_image_app,
                phoneme::Dimensions {320, 240}).has_value(),
            "start List fixture with a real per-item image");

    bool icon_list_shown = false;
    phoneme::i32 icon_choice_id = 0;
    phoneme::i32 icon_image_key = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Icons") {
            icon_list_shown = true;
        }
        if (event->kind == 12 && event->text == "Image item" &&
            event->arguments[3] < 0) {
            icon_choice_id = event->component_id;
            icon_image_key = event->arguments[3];
        }
    }
    require(icon_list_shown && icon_choice_id != 0 && icon_image_key < 0,
            "List initially publishes its real item image key");

    const auto initial_icon = runtime.copy_lcdui_image_rgba(
        icon_image_key, {});
    require(initial_icon.dimensions.width == 2 &&
                initial_icon.dimensions.height == 2 &&
                initial_icon.byte_count == 16U,
            "List item image key resolves to its source pixels");

    runtime.ui_set_choice(icon_choice_id, 0, true);
    bool opening_alert_shown = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 4 && event->component_type == 17 &&
            event->text == "Opening") {
            opening_alert_shown = true;
        }
    }
    require(opening_alert_shown,
            "selecting the image List item opens the intermediate Alert");

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    runtime.pump_events();

    bool icon_list_restored = false;
    bool icon_choice_replayed = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 4 && event->component_type == 23 &&
            event->text == "Icons") {
            icon_list_restored = true;
        }
        if (event->kind == 12 &&
            event->component_id == icon_choice_id &&
            event->text == "Image item" &&
            event->arguments[3] == icon_image_key) {
            icon_choice_replayed = true;
        }
    }
    require(icon_list_restored && icon_choice_replayed,
            "Back to the same List replays each real item image");

    const auto restored_icon = runtime.copy_lcdui_image_rgba(
        icon_image_key, {});
    require(restored_icon.dimensions.width == 2 &&
                restored_icon.dimensions.height == 2 &&
                restored_icon.byte_count == 16U,
            "restored List item image remains resolvable after Back");
    require(runtime.destroy_midlet(list_image_app).has_value(),
            "destroy List image Back fixture MIDlet");
}

void test_runtime_canvas(const std::string& fixture_jar) {
    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(fixture_jar).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime for Canvas fixture");
    require(runtime.configure_keymap({-59, -60, -61, -62,
                                      -20, -21, -22}).has_value(),
            "configure a non-default Canvas key map");
    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(), "install Canvas fixture suite");
    require(runtime.start_system().has_value(),
            "start runtime for Canvas fixture");

    constexpr phoneme::AppId canvas_app {31};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasOps",
                                 canvas_app,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start GameCanvas fixture MIDlet");
    require(runtime.app_state(canvas_app) ==
                phoneme::runtime::AppState::active,
            "Canvas fixture remains active after callbacks");

    bool created = false;
    bool shown = false;
    bool show_notified = false;
    bool first_paint = false;
    int paint_events = 0;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 2 && event->component_type == 22) {
            created = true;
        }
        if (event->kind == 4 && event->component_type == 22) {
            shown = true;
        }
        if (event->kind == 3 && event->component_type == 22 &&
            event->text == "show") {
            show_notified = true;
        }
        if (event->kind == 3 && event->component_type == 22 &&
            event->text.starts_with("paint:")) {
            ++paint_events;
            first_paint = event->text == "paint:1";
        }
    }
    require(created && shown && show_notified,
            "Canvas creation visibility and showNotify are bridged");
    require(first_paint && paint_events == 1,
            "overlapping repaint requests coalesce into one paint callback");

    const auto frame = runtime.frame_snapshot();
    require(frame.dimensions.width == 320 && frame.dimensions.height == 240 &&
                frame.rgba.size() == 320U * 240U * 4U,
            "Canvas paint publishes a complete framebuffer");
    require(frame.rgba[0] == 0x10U && frame.rgba[1] == 0x34U &&
                frame.rgba[2] == 0x52U && frame.rgba[3] == 0xFFU,
            "Canvas Graphics publishes phoneME-compatible RGB565 colors");

    runtime.send_key(-1, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    (void)runtime.frame_snapshot();
    runtime.send_key(-1, false);
    bool key_down = false;
    bool key_repeat = false;
    bool key_up = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind != 3 || event->component_type != 22) continue;
        key_down = key_down ||
            event->text == "down:-59:1:-59:2:UP";
        key_repeat = key_repeat || event->text == "repeat:-59:2";
        key_up = key_up || event->text == "up:-59:0";
    }
    require(key_down && key_repeat && key_up,
            "Canvas dispatches mapped press repeat release and GameCanvas states");

    runtime.send_pointer(7, 9, 1);
    runtime.send_pointer(8, 10, 3);
    runtime.send_pointer(9, 11, 2);
    bool pointer_down = false;
    bool pointer_drag = false;
    bool pointer_up = false;
    bool fullscreen_on = false;
    bool fullscreen_off = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 3 && event->component_type == 22) {
            pointer_down = pointer_down ||
                event->text == "pointerDown:7:9";
            pointer_drag = pointer_drag ||
                event->text == "pointerDrag:8:10";
            pointer_up = pointer_up ||
                event->text == "pointerUp:9:11";
            if (event->arguments[3] == -1007) {
                fullscreen_on = fullscreen_on || event->arguments[0] == 1;
                fullscreen_off = fullscreen_off || event->arguments[0] == 0;
            }
        }
    }
    require(pointer_down && pointer_drag && pointer_up,
            "Canvas dispatches pointer press drag and release callbacks");
    require(fullscreen_on && fullscreen_off,
            "Canvas fullscreen mode changes are bridged to native UI");

    require(runtime.set_foreground(canvas_app,
                                   phoneme::Dimensions {240, 320}).has_value(),
            "resize foreground Canvas");
    bool size_changed = false;
    while (auto event = runtime.poll_ui_event()) {
        size_changed = size_changed ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "size:240:320");
    }
    require(size_changed,
            "Canvas receives sizeChanged before repainting a resized surface");

    constexpr phoneme::AppId plain_app {32};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.LifecycleApp",
                                 plain_app,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start a second MIDlet to hide the Canvas");
    bool hidden = false;
    while (auto event = runtime.poll_ui_event()) {
        hidden = hidden ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "hide");
    }
    require(hidden, "foreground handoff invokes Canvas.hideNotify");

    require(runtime.set_foreground(canvas_app,
                                   phoneme::Dimensions {240, 320}).has_value(),
            "restore Canvas foreground");
    bool shown_again = false;
    while (auto event = runtime.poll_ui_event()) {
        shown_again = shown_again ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "show");
    }
    require(shown_again, "restoring foreground invokes Canvas.showNotify");

    require(runtime.set_foreground(phoneme::AppId {},
                                   phoneme::Dimensions {1, 1}).has_value(),
            "detach the visible MIDlet without pausing its isolate");
    require(!runtime.foreground_app_id().valid(),
            "foreground detach clears the runtime foreground application");
    bool detached_hidden = false;
    while (auto event = runtime.poll_ui_event()) {
        detached_hidden = detached_hidden ||
            (event->kind == 3 && event->component_type == 22 &&
             event->text == "hide");
    }
    require(detached_hidden,
            "foreground detach invokes Canvas.hideNotify");

    require(runtime.set_foreground(canvas_app,
                                   phoneme::Dimensions {240, 320}).has_value(),
            "restore Canvas after host detach");

    require(runtime.destroy_midlet(canvas_app).has_value(),
            "destroy Canvas fixture MIDlet");
    require(runtime.destroy_midlet(plain_app).has_value(),
            "destroy secondary lifecycle MIDlet");

    constexpr phoneme::AppId full_canvas_app {34};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.FullCanvasOps",
                                 full_canvas_app,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start Nokia FullCanvas compatibility fixture");
    bool full_canvas_fullscreen = false;
    bool full_canvas_semantics = false;
    while (auto event = runtime.poll_ui_event()) {
        if (event->kind == 3 && event->component_type == 22) {
            full_canvas_semantics = full_canvas_semantics ||
                event->text == "fullcanvas:ok";
            if (event->arguments[3] == -1007) {
                full_canvas_fullscreen = full_canvas_fullscreen ||
                    event->arguments[0] == 1;
            }
        }
    }
    require(full_canvas_fullscreen,
            "Nokia FullCanvas enters fullscreen during construction");
    require(full_canvas_semantics,
            "Nokia FullCanvas constants and command restrictions match phoneME");
    require(runtime.destroy_midlet(full_canvas_app).has_value(),
            "destroy Nokia FullCanvas compatibility fixture");

    constexpr phoneme::AppId budget_app {33};
    const auto budget_started_at = std::chrono::steady_clock::now();
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.CanvasBudgetOps",
                                 budget_app,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "Canvas callback instruction budget isolates an infinite paint");
    const auto budget_elapsed = std::chrono::steady_clock::now() -
        budget_started_at;
    require(budget_elapsed < std::chrono::seconds(5),
            "infinite Canvas paint cannot block the runtime indefinitely");
    require(runtime.app_state(budget_app) ==
                phoneme::runtime::AppState::active,
            "budget-exhausted Canvas remains active");
    const auto budget_frame = runtime.frame_snapshot();
    require(budget_frame.rgba.size() == 320U * 240U * 4U &&
                budget_frame.rgba[0] == 0x21U &&
                budget_frame.rgba[1] == 0x45U &&
                budget_frame.rgba[2] == 0x63U &&
                budget_frame.rgba[3] == 0xFFU,
            "Canvas commits drawing completed before budget exhaustion");
    require(runtime.destroy_midlet(budget_app).has_value(),
            "destroy Canvas budget fixture MIDlet");
}

void test_machine_shutdown_waiter(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add infinite-wait fixture archive");
    phoneme::vm::Machine machine(classes);
    auto started = machine.invoke_static("corefixture/WaitForeverOps",
                                         "startWaiter",
                                         "()I",
                                         {},
                                         20'000'000U);
    require(started.has_value() && started->completed_normally() &&
                started->return_value.has_value(),
            "start Java worker blocked in Object.wait");
    auto status = started->return_value->as_int();
    require(status.has_value() && *status == 1,
            "waiter fixture reaches its monitor wait");

    const auto shutdown_started = std::chrono::steady_clock::now();
    machine.shutdown();
    require(std::chrono::steady_clock::now() - shutdown_started <
                std::chrono::seconds(2),
            "VM shutdown cancels Object.wait before joining Java workers");
}

void test_machine_timer(const std::string& fixture_jar) {
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar).has_value(),
            "add Timer fixture archive");
    phoneme::vm::Machine machine(classes);
    auto result = machine.invoke_static("corefixture/TimerOps",
                                        "run",
                                        "()I",
                                        {},
                                        100'000'000U);
    if (!result) {
        std::cerr << "Timer fixture VM error: " << result.error().message
                  << '\n';
    }
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "Timer fixture completes normally");
    auto status = result->return_value->as_int();
    if (status && *status != 0) {
        std::cerr << "Timer fixture status: " << *status << '\n';
    }
    require(status.has_value() && *status == 0,
            "Timer/TimerTask scheduling and cancellation semantics");

    auto timer = machine.class_states().allocate_instance(
        machine.heap(), "java/util/Timer");
    require(timer.has_value(), "allocate Timer for shutdown regression");
    auto constructed = machine.invoke_instance(*timer,
                                               "java/util/Timer",
                                               "<init>",
                                               "()V");
    require(constructed.has_value() && constructed->completed_normally(),
            "construct Timer for shutdown regression");
    // Machine destruction below must wake and join an idle Timer thread.
}

void test_runtime_lifecycle(const std::string& fixture_jar) {
    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(fixture_jar).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime without classes.zip");

    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(), "install standalone fixture suite");
    require(runtime.set_suite_trust(
                *suite_id,
                phoneme::security::SuiteTrust::trusted).has_value(),
            "mark standalone fixture suite trusted");
    require(runtime.start_system().has_value(), "start standalone runtime");
    const auto invalid_small_heap = runtime.configure_app_heap(
        phoneme::AppId {1}, 0);
    require(!invalid_small_heap.has_value() &&
                invalid_small_heap.error().code ==
                    phoneme::ErrorCode::invalid_argument,
            "reject a per-app heap smaller than 1 MiB");
    const auto invalid_large_heap = runtime.configure_app_heap(
        phoneme::AppId {1}, 513);
    require(!invalid_large_heap.has_value() &&
                invalid_large_heap.error().code ==
                    phoneme::ErrorCode::invalid_argument,
            "reject a per-app heap larger than 512 MiB");
    require(runtime.configure_app_heap(
                phoneme::AppId {1}, 32).has_value(),
            "configure the Java heap before MIDlet startup");
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.LifecycleApp",
                                 phoneme::AppId {1},
                                 phoneme::Dimensions {320, 240}).has_value(),
            "construct MIDlet using C++ built-in MIDlet superclass");
    require(runtime.app_state(phoneme::AppId {1}) ==
                phoneme::runtime::AppState::active,
            "MIDlet enters active state");
    require(runtime.app_used_memory(phoneme::AppId {1}) > 0,
            "MIDlet reports heap memory");
    const auto live_heap_change = runtime.configure_app_heap(
        phoneme::AppId {1}, 64);
    require(!live_heap_change.has_value() &&
                live_heap_change.error().code ==
                    phoneme::ErrorCode::invalid_state,
            "changing a live MIDlet heap requires restart");
    require(runtime.set_suite_trust(
                *suite_id,
                phoneme::security::SuiteTrust::trusted).has_value(),
            "reasserting unchanged suite trust is safe while MIDlet is alive");
    const auto trust_change = runtime.set_suite_trust(
        *suite_id, phoneme::security::SuiteTrust::untrusted);
    require(!trust_change.has_value() &&
                trust_change.error().code == phoneme::ErrorCode::invalid_state,
            "changing suite trust remains blocked while MIDlet is alive");

    require(runtime.pause_midlet(phoneme::AppId {1}).has_value(),
            "execute pauseApp");
    require(runtime.resume_midlet(phoneme::AppId {1}).has_value(),
            "execute startApp on resume");
    require(runtime.destroy_midlet(phoneme::AppId {1}).has_value(),
            "execute destroyApp");
    require(runtime.app_state(phoneme::AppId {1}) ==
                phoneme::runtime::AppState::destroyed,
            "MIDlet enters destroyed state");
    require(runtime.configure_app_heap(
                phoneme::AppId {1}, 64).has_value(),
            "heap may be reconfigured after MIDlet destruction");

    require(runtime.start_midlet(*suite_id,
                                 "corefixture.SelfDestroyApp",
                                 phoneme::AppId {2},
                                 phoneme::Dimensions {320, 240}).has_value(),
            "MIDlet may notifyDestroyed from startApp");
    require(runtime.app_state(phoneme::AppId {2}) ==
                phoneme::runtime::AppState::destroyed,
            "notifyDestroyed prevents a false active state");

    require(runtime.start_midlet(*suite_id,
                                 "corefixture.SelfPauseApp",
                                 phoneme::AppId {3},
                                 phoneme::Dimensions {320, 240}).has_value(),
            "MIDlet may notifyPaused from startApp");
    require(runtime.app_state(phoneme::AppId {3}) ==
                phoneme::runtime::AppState::paused,
            "notifyPaused enters the paused state");
    require(runtime.resume_midlet(phoneme::AppId {3}).has_value(),
            "resume a MIDlet that paused itself");
    require(runtime.app_state(phoneme::AppId {3}) ==
                phoneme::runtime::AppState::active,
            "resumed self-paused MIDlet becomes active");
    require(runtime.destroy_midlet(phoneme::AppId {3}).has_value(),
            "destroy self-paused MIDlet");

    constexpr phoneme::AppId asynchronous_exit_app {4};
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.AsyncSelfDestroyApp",
                                 asynchronous_exit_app,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start MIDlet that exits from an application worker");
    phoneme::runtime::AppState asynchronous_exit_state =
        phoneme::runtime::AppState::active;
    for (int attempt = 0; attempt < 200; ++attempt) {
        asynchronous_exit_state = runtime.app_state(asynchronous_exit_app);
        if (asynchronous_exit_state ==
            phoneme::runtime::AppState::destroyed) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(asynchronous_exit_state ==
                phoneme::runtime::AppState::destroyed,
            "notifyDestroyed after startApp is observed and finalized");
    require(runtime.destroy_midlet(asynchronous_exit_app).has_value(),
            "host teardown remains idempotent after application exit");
}

void test_runtime_lock_reentry(const std::string& fixture_jar) {
    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(fixture_jar).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime for lifecycle re-entry");

    constexpr phoneme::AppId app_id {21};
    phoneme::i32 prompt_count = 0;
    require(runtime.configure_permission_prompt(
                [&](const phoneme::security::PermissionRequest&)
                    -> phoneme::security::PermissionResponse {
                    require(runtime.is_running(),
                            "permission prompt re-enters Runtime read API");
                    const auto state = runtime.app_state(app_id);
                    require(state == phoneme::runtime::AppState::none ||
                                state == phoneme::runtime::AppState::active ||
                                state == phoneme::runtime::AppState::paused,
                            "re-entrant lifecycle state remains observable");
                    ++prompt_count;
                    return {
                        .decision =
                            phoneme::security::PermissionDecision::allowed,
                        .scope = phoneme::security::PermissionScope::one_shot,
                    };
                })
                .has_value(),
            "configure re-entrant permission prompt");

    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(),
            "install lifecycle re-entry fixture suite");
    require(runtime.start_system().has_value(),
            "start lifecycle re-entry runtime");
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.ReentrantLifecycleApp",
                                 app_id,
                                 {320, 240})
                .has_value(),
            "startApp may synchronously re-enter Runtime without deadlock");
    require(runtime.pause_midlet(app_id).has_value(),
            "pauseApp may synchronously re-enter Runtime without deadlock");
    require(runtime.resume_midlet(app_id).has_value(),
            "resumed startApp may re-enter Runtime without deadlock");
    require(runtime.destroy_midlet(app_id).has_value(),
            "destroyApp may synchronously re-enter Runtime without deadlock");
    require(prompt_count == 4,
            "every lifecycle callback reached the re-entrant permission prompt");
}

void test_runtime_failure_isolation(const std::string& fixture_jar) {
    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(fixture_jar).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime for failure isolation");
    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(), "install failure isolation fixture suite");
    require(runtime.start_system().has_value(),
            "start runtime for failure isolation");

    auto failed = runtime.start_midlet(*suite_id,
                                       "corefixture.FailingLifecycleApp",
                                       phoneme::AppId {10},
                                       phoneme::Dimensions {320, 240});
    require(!failed.has_value() &&
                failed.error().code == phoneme::ErrorCode::java_exception,
            "uncaught startApp exception fails MIDlet launch");
    require(runtime.app_state(phoneme::AppId {10}) ==
                phoneme::runtime::AppState::error,
            "failed MIDlet remains isolated in error state");
    const std::string app_error =
        runtime.app_error_message(phoneme::AppId {10});
    require(app_error.find("java/lang/IllegalStateException") !=
                std::string::npos &&
                app_error.find("fixture start failure") != std::string::npos,
            "failed MIDlet retains Java exception class and message");
    require(runtime.last_error_message() == app_error,
            "runtime exposes the latest MIDlet Java exception message");

    auto healthy = runtime.start_midlet(*suite_id,
                                        "corefixture.LifecycleApp",
                                        phoneme::AppId {11},
                                        phoneme::Dimensions {320, 240});
    require(healthy.has_value(),
            "healthy MIDlet starts after another MIDlet fails");
    require(runtime.app_state(phoneme::AppId {11}) ==
                phoneme::runtime::AppState::active,
            "healthy MIDlet reaches active state after failure isolation");
}

void test_machine_m3g() {
    phoneme::vm::ClassRepository classes;
    phoneme::vm::Machine machine(classes);

    const auto invoke_void = [&](phoneme::vm::ObjectRef object,
                                 const char* owner,
                                 const char* name,
                                 const char* descriptor,
                                 std::span<const phoneme::vm::Value> arguments = {}) {
        auto result = machine.invoke_instance(object, owner, name,
                                              descriptor, arguments);
        if (!result.has_value() || !result->completed_normally()) {
            std::cerr << "M3G invocation failed: " << owner << '.' << name
                      << descriptor;
            if (result.has_value() && result->throwable.has_value()) {
                auto throwable_class = machine.heap().class_name(
                    *result->throwable);
                if (throwable_class.has_value()) {
                    std::cerr << " threw " << *throwable_class;
                }
            }
            std::cerr << '\n';
        }
        require(result.has_value() && result->completed_normally(),
                "invoke M3G method normally");
    };

    auto transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(transform.has_value(), "allocate M3G Transform");
    invoke_void(*transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> translation {
        phoneme::vm::Value::from_float(4.0F),
        phoneme::vm::Value::from_float(-3.0F),
        phoneme::vm::Value::from_float(2.0F),
    };
    invoke_void(*transform, "javax/microedition/m3g/Transform",
                "postTranslate", "(FFF)V", translation);
    auto matrix = machine.heap().allocate_array(
        "[F", 16U, phoneme::vm::Value::from_float(0.0F));
    require(matrix.has_value(), "allocate M3G transform destination");
    const phoneme::vm::Value matrix_argument =
        phoneme::vm::Value::from_reference(*matrix);
    invoke_void(*transform, "javax/microedition/m3g/Transform",
                "get", "([F)V",
                std::span<const phoneme::vm::Value>(&matrix_argument, 1U));
    const auto require_float = [&](phoneme::usize index, float expected) {
        auto value = machine.heap().element(*matrix, index);
        require(value.has_value(), "read M3G matrix element");
        auto number = value->as_float();
        require(number.has_value() && std::abs(*number - expected) < 0.0001F,
                "M3G Transform preserves matrix values");
    };
    require_float(3U, 4.0F);
    require_float(7U, -3.0F);
    require_float(11U, 2.0F);

    const auto require_java_exception = [&](const auto& result,
                                            std::string_view expected,
                                            const char* message) {
        require(result.has_value() && result->throwable.has_value(), message);
        auto class_name = machine.heap().class_name(*result->throwable);
        require(class_name.has_value() && *class_name == expected, message);
    };
    const auto int_array = [&](std::initializer_list<phoneme::i32> values) {
        auto array = machine.heap().allocate_array(
            "[I", values.size(), phoneme::vm::Value::from_int(0));
        require(array.has_value(), "allocate M3G int array");
        phoneme::usize index = 0U;
        for (const phoneme::i32 value : values) {
            require(machine.heap().set_element(
                        *array, index++, phoneme::vm::Value::from_int(value))
                        .has_value(),
                    "populate M3G int array");
        }
        return *array;
    };
    const auto float_array = [&](std::initializer_list<float> values) {
        auto array = machine.heap().allocate_array(
            "[F", values.size(), phoneme::vm::Value::from_float(0.0F));
        require(array.has_value(), "allocate M3G float array");
        phoneme::usize index = 0U;
        for (const float value : values) {
            require(machine.heap().set_element(
                        *array, index++, phoneme::vm::Value::from_float(value))
                        .has_value(),
                    "populate M3G float array");
        }
        return *array;
    };
    const auto vertex_array = [&](phoneme::i32 vertices,
                                  phoneme::i32 components,
                                  phoneme::i32 component_size) {
        auto array = machine.class_states().allocate_instance(
            machine.heap(), "javax/microedition/m3g/VertexArray");
        require(array.has_value(), "allocate M3G VertexArray");
        const std::array<phoneme::vm::Value, 3> arguments {
            phoneme::vm::Value::from_int(vertices),
            phoneme::vm::Value::from_int(components),
            phoneme::vm::Value::from_int(component_size),
        };
        invoke_void(*array, "javax/microedition/m3g/VertexArray",
                    "<init>", "(III)V", arguments);
        return *array;
    };

    auto vertex_buffer = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexBuffer");
    require(vertex_buffer.has_value(), "allocate M3G VertexBuffer");
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "<init>", "()V");
    auto positions = vertex_array(4, 3, 2);
    auto position_bias = float_array({1.0F, 2.0F, 3.0F});
    const std::array<phoneme::vm::Value, 3> position_arguments {
        phoneme::vm::Value::from_reference(positions),
        phoneme::vm::Value::from_float(0.5F),
        phoneme::vm::Value::from_reference(position_bias),
    };
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setPositions",
                "(Ljavax/microedition/m3g/VertexArray;F[F)V",
                position_arguments);
    auto vertex_count = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "getVertexCount", "()I");
    require(vertex_count.has_value() && vertex_count->completed_normally() &&
                vertex_count->return_value.has_value() &&
                vertex_count->return_value->as_int().value_or(-1) == 4,
            "VertexBuffer adopts the first VertexArray count");

    auto tex_coords = vertex_array(4, 2, 1);
    auto texture_bias = float_array({0.25F, -0.5F});
    const std::array<phoneme::vm::Value, 4> texture_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(tex_coords),
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_reference(texture_bias),
    };
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setTexCoords",
                "(ILjavax/microedition/m3g/VertexArray;F[F)V",
                texture_arguments);
    auto scale_bias = float_array({0.0F, 0.0F, 0.0F});
    const std::array<phoneme::vm::Value, 2> texture_get_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(scale_bias),
    };
    auto texture_result = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "getTexCoords", "(I[F)Ljavax/microedition/m3g/VertexArray;",
        texture_get_arguments);
    require(texture_result.has_value() &&
                texture_result->completed_normally() &&
                texture_result->return_value.has_value() &&
                texture_result->return_value->as_reference().value_or(
                    phoneme::vm::ObjectRef {}) == tex_coords,
            "VertexBuffer returns the selected texture coordinate array");
    const std::array<float, 3> expected_scale_bias {2.0F, 0.25F, -0.5F};
    for (phoneme::usize index = 0U; index < expected_scale_bias.size(); ++index) {
        auto value = machine.heap().element(scale_bias, index);
        require(value.has_value() && value->as_float().has_value() &&
                    std::abs(*value->as_float() - expected_scale_bias[index]) <
                        0.0001F,
                "VertexBuffer preserves texture scale and bias");
    }

    auto mismatched_tex_coords = vertex_array(5, 2, 1);
    auto mismatch_arguments = texture_arguments;
    mismatch_arguments[1] =
        phoneme::vm::Value::from_reference(mismatched_tex_coords);
    auto mismatch = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "setTexCoords",
        "(ILjavax/microedition/m3g/VertexArray;F[F)V",
        mismatch_arguments);
    require_java_exception(mismatch, "java/lang/IllegalArgumentException",
                           "VertexBuffer rejects mismatched vertex counts");

    auto four_component_tex_coords = vertex_array(4, 4, 1);
    auto component_arguments = texture_arguments;
    component_arguments[1] =
        phoneme::vm::Value::from_reference(four_component_tex_coords);
    auto bad_components = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "setTexCoords",
        "(ILjavax/microedition/m3g/VertexArray;F[F)V",
        component_arguments);
    require_java_exception(bad_components, "java/lang/IllegalArgumentException",
                           "VertexBuffer rejects 4-component texture coordinates");

    auto short_texture_bias = float_array({0.0F});
    auto bias_arguments = texture_arguments;
    bias_arguments[3] =
        phoneme::vm::Value::from_reference(short_texture_bias);
    auto bad_bias = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "setTexCoords",
        "(ILjavax/microedition/m3g/VertexArray;F[F)V",
        bias_arguments);
    require_java_exception(bad_bias, "java/lang/IllegalArgumentException",
                           "VertexBuffer rejects a short texture bias");

    auto short_scale_bias = float_array({0.0F, 0.0F});
    const std::array<phoneme::vm::Value, 2> short_get_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(short_scale_bias),
    };
    auto bad_destination = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "getTexCoords", "(I[F)Ljavax/microedition/m3g/VertexArray;",
        short_get_arguments);
    require_java_exception(bad_destination,
                           "java/lang/IllegalArgumentException",
                           "VertexBuffer rejects a short scaleBias destination");

    auto normals = vertex_array(4, 3, 1);
    const phoneme::vm::Value normals_argument =
        phoneme::vm::Value::from_reference(normals);
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setNormals", "(Ljavax/microedition/m3g/VertexArray;)V",
                std::span<const phoneme::vm::Value>(&normals_argument, 1U));
    auto colors = vertex_array(4, 4, 1);
    const phoneme::vm::Value colors_argument =
        phoneme::vm::Value::from_reference(colors);
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setColors", "(Ljavax/microedition/m3g/VertexArray;)V",
                std::span<const phoneme::vm::Value>(&colors_argument, 1U));
    auto short_component_colors = vertex_array(4, 4, 2);
    const phoneme::vm::Value invalid_colors_argument =
        phoneme::vm::Value::from_reference(short_component_colors);
    auto bad_colors = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "setColors", "(Ljavax/microedition/m3g/VertexArray;)V",
        std::span<const phoneme::vm::Value>(&invalid_colors_argument, 1U));
    require_java_exception(bad_colors, "java/lang/IllegalArgumentException",
                           "VertexBuffer colors require byte components");

    const phoneme::vm::Value null_reference =
        phoneme::vm::Value::from_reference({});
    const std::array<phoneme::vm::Value, 3> clear_positions {
        null_reference,
        phoneme::vm::Value::from_float(1.0F),
        null_reference,
    };
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setPositions",
                "(Ljavax/microedition/m3g/VertexArray;F[F)V",
                clear_positions);
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setNormals", "(Ljavax/microedition/m3g/VertexArray;)V",
                std::span<const phoneme::vm::Value>(&null_reference, 1U));
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setColors", "(Ljavax/microedition/m3g/VertexArray;)V",
                std::span<const phoneme::vm::Value>(&null_reference, 1U));
    const std::array<phoneme::vm::Value, 4> clear_texture {
        phoneme::vm::Value::from_int(0),
        null_reference,
        phoneme::vm::Value::from_float(1.0F),
        null_reference,
    };
    invoke_void(*vertex_buffer, "javax/microedition/m3g/VertexBuffer",
                "setTexCoords",
                "(ILjavax/microedition/m3g/VertexArray;F[F)V",
                clear_texture);
    vertex_count = machine.invoke_instance(
        *vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "getVertexCount", "()I");
    require(vertex_count.has_value() && vertex_count->return_value.has_value() &&
                vertex_count->return_value->as_int().value_or(-1) == 0,
            "VertexBuffer count returns to zero when the last array is cleared");

    auto explicit_strip = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/TriangleStripArray");
    require(explicit_strip.has_value(), "allocate explicit TriangleStripArray");
    auto explicit_indices = int_array({0, 1, 2, 3, 4});
    auto one_strip = int_array({3});
    const std::array<phoneme::vm::Value, 2> explicit_arguments {
        phoneme::vm::Value::from_reference(explicit_indices),
        phoneme::vm::Value::from_reference(one_strip),
    };
    invoke_void(*explicit_strip,
                "javax/microedition/m3g/TriangleStripArray",
                "<init>", "([I[I)V", explicit_arguments);
    auto index_count = machine.invoke_instance(
        *explicit_strip, "javax/microedition/m3g/IndexBuffer",
        "getIndexCount", "()I");
    require(index_count.has_value() && index_count->return_value.has_value() &&
                index_count->return_value->as_int().value_or(-1) == 5,
            "TriangleStripArray copies extra explicit indices");

    const auto triangle_constructor = [&](phoneme::vm::ObjectRef object,
                                          std::span<const phoneme::vm::Value> args,
                                          const char* descriptor) {
        return machine.invoke_instance(
            object, "javax/microedition/m3g/TriangleStripArray",
            "<init>", descriptor, args);
    };
    auto short_strip = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/TriangleStripArray");
    auto short_indices = int_array({0, 1});
    const std::array<phoneme::vm::Value, 2> short_strip_arguments {
        phoneme::vm::Value::from_reference(short_indices),
        phoneme::vm::Value::from_reference(one_strip),
    };
    require_java_exception(
        triangle_constructor(*short_strip, short_strip_arguments, "([I[I)V"),
        "java/lang/IllegalArgumentException",
        "TriangleStripArray rejects too few explicit indices");

    for (const phoneme::i32 invalid_index : {-1, 65'536}) {
        auto invalid_strip = machine.class_states().allocate_instance(
            machine.heap(), "javax/microedition/m3g/TriangleStripArray");
        auto invalid_indices = int_array({0, 1, invalid_index});
        const std::array<phoneme::vm::Value, 2> invalid_arguments {
            phoneme::vm::Value::from_reference(invalid_indices),
            phoneme::vm::Value::from_reference(one_strip),
        };
        require_java_exception(
            triangle_constructor(*invalid_strip, invalid_arguments, "([I[I)V"),
            "java/lang/IndexOutOfBoundsException",
            "TriangleStripArray rejects indices outside 0..65535");
    }

    auto two_strips = int_array({3, 4});
    auto implicit_strip = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/TriangleStripArray");
    const std::array<phoneme::vm::Value, 2> implicit_arguments {
        phoneme::vm::Value::from_int(10),
        phoneme::vm::Value::from_reference(two_strips),
    };
    invoke_void(*implicit_strip,
                "javax/microedition/m3g/TriangleStripArray",
                "<init>", "(I[I)V", implicit_arguments);
    index_count = machine.invoke_instance(
        *implicit_strip, "javax/microedition/m3g/IndexBuffer",
        "getIndexCount", "()I");
    require(index_count.has_value() && index_count->return_value.has_value() &&
                index_count->return_value->as_int().value_or(-1) == 7,
            "implicit TriangleStripArray expands all strip indices");

    for (const phoneme::i32 invalid_first : {-1, 65'530}) {
        auto invalid_strip = machine.class_states().allocate_instance(
            machine.heap(), "javax/microedition/m3g/TriangleStripArray");
        const std::array<phoneme::vm::Value, 2> invalid_arguments {
            phoneme::vm::Value::from_int(invalid_first),
            phoneme::vm::Value::from_reference(two_strips),
        };
        require_java_exception(
            triangle_constructor(*invalid_strip, invalid_arguments, "(I[I)V"),
            "java/lang/IndexOutOfBoundsException",
            "implicit TriangleStripArray validates the 16-bit index range");
    }

    auto boundary_strip = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/TriangleStripArray");
    const std::array<phoneme::vm::Value, 2> boundary_arguments {
        phoneme::vm::Value::from_int(65'528),
        phoneme::vm::Value::from_reference(two_strips),
    };
    invoke_void(*boundary_strip,
                "javax/microedition/m3g/TriangleStripArray",
                "<init>", "(I[I)V", boundary_arguments);

    auto group = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    auto camera = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Camera");
    require(group.has_value() && camera.has_value(),
            "allocate M3G scene nodes");
    invoke_void(*group, "javax/microedition/m3g/Group", "<init>", "()V");
    invoke_void(*camera, "javax/microedition/m3g/Camera", "<init>", "()V");

    const std::array<phoneme::vm::Value, 3> component_translation {
        phoneme::vm::Value::from_float(4.0F),
        phoneme::vm::Value::from_float(5.0F),
        phoneme::vm::Value::from_float(6.0F),
    };
    const std::array<phoneme::vm::Value, 3> component_scale {
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_float(3.0F),
        phoneme::vm::Value::from_float(4.0F),
    };
    const std::array<phoneme::vm::Value, 4> component_orientation {
        phoneme::vm::Value::from_float(90.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(1.0F),
    };
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "setTranslation", "(FFF)V", component_translation);
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "setOrientation", "(FFFF)V", component_orientation);
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "setScale", "(FFF)V", component_scale);

    const auto read_float_element = [&](phoneme::vm::ObjectRef array,
                                        phoneme::usize index) {
        auto value = machine.heap().element(array, index);
        require(value.has_value() && value->as_float().has_value(),
                "read M3G float array element");
        return *value->as_float();
    };
    const auto require_component_array = [&](const char* method,
                                             phoneme::vm::ObjectRef destination,
                                             std::span<const float> expected,
                                             const char* message) {
        const phoneme::vm::Value argument =
            phoneme::vm::Value::from_reference(destination);
        invoke_void(*camera, "javax/microedition/m3g/Transformable",
                    method, "([F)V",
                    std::span<const phoneme::vm::Value>(&argument, 1U));
        for (phoneme::usize index = 0U; index < expected.size(); ++index) {
            require(std::abs(read_float_element(destination, index) -
                             expected[index]) < 0.001F,
                    message);
        }
    };
    auto translation_result = float_array({0.0F, 0.0F, 0.0F});
    auto scale_result = float_array({0.0F, 0.0F, 0.0F});
    auto orientation_result = float_array({0.0F, 0.0F, 0.0F, 0.0F});
    const std::array<float, 3> expected_translation {4.0F, 5.0F, 6.0F};
    const std::array<float, 3> expected_scale {2.0F, 3.0F, 4.0F};
    const std::array<float, 4> expected_orientation {90.0F, 0.0F, 0.0F, 1.0F};
    require_component_array("getTranslation", translation_result,
                            expected_translation,
                            "Transformable preserves component translation");
    require_component_array("getScale", scale_result, expected_scale,
                            "setScale preserves the existing orientation");
    require_component_array("getOrientation", orientation_result,
                            expected_orientation,
                            "setScale does not destroy orientation");

    auto generic_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(generic_transform.has_value(),
            "allocate Transformable generic transform");
    invoke_void(*generic_transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> generic_translation {
        phoneme::vm::Value::from_float(7.0F),
        phoneme::vm::Value::from_float(8.0F),
        phoneme::vm::Value::from_float(9.0F),
    };
    invoke_void(*generic_transform, "javax/microedition/m3g/Transform",
                "postTranslate", "(FFF)V", generic_translation);
    const phoneme::vm::Value generic_argument =
        phoneme::vm::Value::from_reference(*generic_transform);
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "setTransform", "(Ljavax/microedition/m3g/Transform;)V",
                std::span<const phoneme::vm::Value>(&generic_argument, 1U));

    const auto transform_matrix_result = [&](const char* method) {
        auto destination = machine.class_states().allocate_instance(
            machine.heap(), "javax/microedition/m3g/Transform");
        require(destination.has_value(), "allocate Transform destination");
        invoke_void(*destination, "javax/microedition/m3g/Transform",
                    "<init>", "()V");
        const phoneme::vm::Value destination_argument =
            phoneme::vm::Value::from_reference(*destination);
        invoke_void(*camera, "javax/microedition/m3g/Transformable",
                    method, "(Ljavax/microedition/m3g/Transform;)V",
                    std::span<const phoneme::vm::Value>(
                        &destination_argument, 1U));
        auto values = machine.heap().allocate_array(
            "[F", 16U, phoneme::vm::Value::from_float(0.0F));
        require(values.has_value(), "allocate Transform matrix result");
        const phoneme::vm::Value values_argument =
            phoneme::vm::Value::from_reference(*values);
        invoke_void(*destination, "javax/microedition/m3g/Transform",
                    "get", "([F)V",
                    std::span<const phoneme::vm::Value>(&values_argument, 1U));
        return *values;
    };
    const auto generic_matrix = transform_matrix_result("getTransform");
    require(std::abs(read_float_element(generic_matrix, 3U) - 7.0F) < 0.001F &&
                std::abs(read_float_element(generic_matrix, 7U) - 8.0F) < 0.001F &&
                std::abs(read_float_element(generic_matrix, 11U) - 9.0F) < 0.001F,
            "getTransform returns only the generic matrix");
    const auto composite_transform =
        transform_matrix_result("getCompositeTransform");
    require(std::abs(read_float_element(composite_transform, 3U) + 20.0F) <
                0.001F &&
                std::abs(read_float_element(composite_transform, 7U) - 19.0F) <
                0.001F &&
                std::abs(read_float_element(composite_transform, 11U) - 42.0F) <
                0.001F,
            "getCompositeTransform composes T x R x S x generic transform");

    const std::array<phoneme::vm::Value, 3> incremental_translation {
        phoneme::vm::Value::from_float(1.0F),
        phoneme::vm::Value::from_float(-2.0F),
        phoneme::vm::Value::from_float(3.0F),
    };
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "translate", "(FFF)V", incremental_translation);
    const std::array<float, 3> translated_components {5.0F, 3.0F, 9.0F};
    require_component_array("getTranslation", translation_result,
                            translated_components,
                            "translate adds component-space translation");

    const std::array<phoneme::vm::Value, 3> incremental_scale {
        phoneme::vm::Value::from_float(0.5F),
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_float(0.25F),
    };
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "scale", "(FFF)V", incremental_scale);
    const std::array<float, 3> scaled_components {1.0F, 6.0F, 1.0F};
    require_component_array("getScale", scale_result, scaled_components,
                            "scale multiplies component scale without losing rotation");
    require_component_array("getOrientation", orientation_result,
                            expected_orientation,
                            "component scale operations preserve orientation");

    const std::array<phoneme::vm::Value, 4> identity_orientation {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
    };
    invoke_void(*camera, "javax/microedition/m3g/Transformable",
                "setOrientation", "(FFFF)V", identity_orientation);
    const std::array<float, 4> expected_identity_orientation {
        0.0F, 0.0F, 0.0F, 1.0F};
    require_component_array("getOrientation", orientation_result,
                            expected_identity_orientation,
                            "zero angle accepts a zero axis and produces identity");
    require_component_array("getScale", scale_result, scaled_components,
                            "setOrientation preserves component scale");

    auto empty_transform_array = machine.heap().allocate_array(
        "[F", 0U, phoneme::vm::Value::from_float(0.0F));
    require(empty_transform_array.has_value(),
            "allocate empty Transform input");
    const phoneme::vm::Value empty_transform_argument =
        phoneme::vm::Value::from_reference(*empty_transform_array);
    invoke_void(*generic_transform, "javax/microedition/m3g/Transform",
                "transform", "([F)V",
                std::span<const phoneme::vm::Value>(
                    &empty_transform_argument, 1U));

    auto alignment_root = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    auto aligned_node = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    auto alignment_target = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    require(alignment_root.has_value() && aligned_node.has_value() &&
                alignment_target.has_value(),
            "allocate M3G alignment graph");
    invoke_void(*alignment_root, "javax/microedition/m3g/Group",
                "<init>", "()V");
    invoke_void(*aligned_node, "javax/microedition/m3g/Group",
                "<init>", "()V");
    invoke_void(*alignment_target, "javax/microedition/m3g/Group",
                "<init>", "()V");
    for (const phoneme::vm::ObjectRef child :
         std::array<phoneme::vm::ObjectRef, 2> {
             *aligned_node, *alignment_target}) {
        const phoneme::vm::Value child_argument =
            phoneme::vm::Value::from_reference(child);
        invoke_void(*alignment_root, "javax/microedition/m3g/Group",
                    "addChild", "(Ljavax/microedition/m3g/Node;)V",
                    std::span<const phoneme::vm::Value>(
                        &child_argument, 1U));
    }
    const std::array<phoneme::vm::Value, 3> alignment_translation {
        phoneme::vm::Value::from_float(10.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
    };
    invoke_void(*alignment_target,
                "javax/microedition/m3g/Transformable",
                "setTranslation", "(FFF)V", alignment_translation);
    const std::array<phoneme::vm::Value, 4> alignment_arguments {
        phoneme::vm::Value::from_reference(*alignment_target),
        phoneme::vm::Value::from_int(145),
        phoneme::vm::Value::from_reference({}),
        phoneme::vm::Value::from_int(144),
    };
    invoke_void(*aligned_node, "javax/microedition/m3g/Node",
                "setAlignment",
                "(Ljavax/microedition/m3g/Node;I"
                "Ljavax/microedition/m3g/Node;I)V",
                alignment_arguments);
    const phoneme::vm::Value z_axis =
        phoneme::vm::Value::from_int(148);
    auto alignment_target_result = machine.invoke_instance(
        *aligned_node, "javax/microedition/m3g/Node",
        "getAlignmentTarget", "(I)I",
        std::span<const phoneme::vm::Value>(&z_axis, 1U));
    require(alignment_target_result.has_value() &&
                alignment_target_result->completed_normally() &&
                alignment_target_result->return_value.has_value() &&
                alignment_target_result->return_value->as_int().value_or(0) ==
                    145,
            "Node.getAlignmentTarget returns the target enum");
    auto alignment_reference_result = machine.invoke_instance(
        *aligned_node, "javax/microedition/m3g/Node",
        "getAlignmentReference",
        "(I)Ljavax/microedition/m3g/Node;",
        std::span<const phoneme::vm::Value>(&z_axis, 1U));
    require(alignment_reference_result.has_value() &&
                alignment_reference_result->completed_normally() &&
                alignment_reference_result->return_value.has_value() &&
                alignment_reference_result->return_value->as_reference()
                    .value_or(phoneme::vm::ObjectRef {}) == *alignment_target,
            "Node.getAlignmentReference returns the configured Node");
    const phoneme::vm::Value null_alignment_reference =
        phoneme::vm::Value::from_reference({});
    invoke_void(*aligned_node, "javax/microedition/m3g/Node",
                "align", "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(
                    &null_alignment_reference, 1U));
    auto aligned_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(aligned_transform.has_value(),
            "allocate aligned Transform destination");
    invoke_void(*aligned_transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    const phoneme::vm::Value aligned_transform_argument =
        phoneme::vm::Value::from_reference(*aligned_transform);
    invoke_void(*aligned_node, "javax/microedition/m3g/Transformable",
                "getCompositeTransform",
                "(Ljavax/microedition/m3g/Transform;)V",
                std::span<const phoneme::vm::Value>(
                    &aligned_transform_argument, 1U));
    auto aligned_axis = float_array({0.0F, 0.0F, 1.0F, 0.0F});
    const phoneme::vm::Value aligned_axis_argument =
        phoneme::vm::Value::from_reference(aligned_axis);
    invoke_void(*aligned_transform, "javax/microedition/m3g/Transform",
                "transform", "([F)V",
                std::span<const phoneme::vm::Value>(
                    &aligned_axis_argument, 1U));
    require(std::abs(read_float_element(aligned_axis, 0U) - 1.0F) <
                0.001F &&
                std::abs(read_float_element(aligned_axis, 1U)) < 0.001F &&
                std::abs(read_float_element(aligned_axis, 2U)) < 0.001F,
            "Node.align rotates local Z toward the target origin");

    const phoneme::vm::Value camera_argument =
        phoneme::vm::Value::from_reference(*camera);
    invoke_void(*group, "javax/microedition/m3g/Group", "addChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&camera_argument, 1U));
    auto count = machine.invoke_instance(*group,
        "javax/microedition/m3g/Group", "getChildCount", "()I");
    require(count.has_value() && count->completed_normally() &&
                count->return_value.has_value(),
            "read M3G Group child count");
    auto count_value = count->return_value->as_int();
    require(count_value.has_value() && *count_value == 1,
            "M3G Group owns added child");
    invoke_void(*group, "javax/microedition/m3g/Group", "removeChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&camera_argument, 1U));
    count = machine.invoke_instance(*group,
        "javax/microedition/m3g/Group", "getChildCount", "()I");
    require(count.has_value() && count->return_value.has_value(),
            "read M3G Group count after removal");
    count_value = count->return_value->as_int();
    require(count_value.has_value() && *count_value == 0,
            "M3G Group removes child with void signature");

    auto world = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/World");
    auto background = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Background");
    require(world.has_value() && background.has_value(),
            "allocate M3G World reference graph");
    invoke_void(*world, "javax/microedition/m3g/World", "<init>", "()V");
    invoke_void(*background, "javax/microedition/m3g/Background",
                "<init>", "()V");
    const phoneme::vm::Value user_id = phoneme::vm::Value::from_int(153);
    invoke_void(*background, "javax/microedition/m3g/Object3D",
                "setUserID", "(I)V",
                std::span<const phoneme::vm::Value>(&user_id, 1U));
    const phoneme::vm::Value background_argument =
        phoneme::vm::Value::from_reference(*background);
    invoke_void(*world, "javax/microedition/m3g/World", "setBackground",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &background_argument, 1U));

    auto found = machine.invoke_instance(
        *world, "javax/microedition/m3g/Object3D", "find",
        "(I)Ljavax/microedition/m3g/Object3D;",
        std::span<const phoneme::vm::Value>(&user_id, 1U));
    require(found.has_value() && found->completed_normally() &&
                found->return_value.has_value() &&
                found->return_value->as_reference().value_or(
                    phoneme::vm::ObjectRef {}) == *background,
            "Object3D.find traverses non-child M3G references");

    auto references = machine.heap().allocate_array(
        "[Ljavax/microedition/m3g/Object3D;", 4U,
        phoneme::vm::Value::from_reference({}));
    require(references.has_value(), "allocate Object3D reference result");
    const phoneme::vm::Value references_argument =
        phoneme::vm::Value::from_reference(*references);
    auto reference_count = machine.invoke_instance(
        *world, "javax/microedition/m3g/Object3D", "getReferences",
        "([Ljavax/microedition/m3g/Object3D;)I",
        std::span<const phoneme::vm::Value>(&references_argument, 1U));
    require(reference_count.has_value() &&
                reference_count->completed_normally() &&
                reference_count->return_value.has_value() &&
                reference_count->return_value->as_int().value_or(0) >= 1,
            "Object3D.getReferences reports World state references");
    auto first_reference = machine.heap().element(*references, 0U);
    require(first_reference.has_value() &&
                first_reference->as_reference().value_or(
                    phoneme::vm::ObjectRef {}) == *background,
            "Object3D.getReferences writes reachable World objects");

    auto graphics3d = machine.invoke_static(
        "javax/microedition/m3g/Graphics3D", "getInstance",
        "()Ljavax/microedition/m3g/Graphics3D;");
    require(graphics3d.has_value() && graphics3d->completed_normally() &&
                graphics3d->return_value.has_value(),
            "obtain Graphics3D singleton");
    auto graphics3d_object = graphics3d->return_value->as_reference();
    require(graphics3d_object.has_value() &&
                !graphics3d_object->is_null(),
            "Graphics3D singleton is non-null");
    auto graphics3d_properties = machine.invoke_static(
        "javax/microedition/m3g/Graphics3D", "getProperties",
        "()Ljava/util/Hashtable;");
    require(graphics3d_properties.has_value() &&
                graphics3d_properties->completed_normally() &&
                graphics3d_properties->return_value.has_value(),
            "Graphics3D.getProperties returns capability metadata");
    auto graphics3d_property_table =
        graphics3d_properties->return_value->as_reference();
    require(graphics3d_property_table.has_value() &&
                !graphics3d_property_table->is_null(),
            "Graphics3D property table is non-null");
    auto graphics3d_property_count = machine.invoke_instance(
        *graphics3d_property_table, "java/util/Hashtable", "size", "()I");
    require(graphics3d_property_count.has_value() &&
                graphics3d_property_count->completed_normally() &&
                graphics3d_property_count->return_value.has_value() &&
                graphics3d_property_count->return_value->as_int()
                    .value_or(0) == 15,
            "Graphics3D.getProperties exposes JSR-184 capabilities and limits");

    auto render_image_object = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/lcdui/Image");
    auto render_graphics_object = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/lcdui/Graphics");
    auto render_image = phoneme::graphics::Image::create_mutable(64, 64);
    require(render_image_object.has_value() &&
                render_graphics_object.has_value() && render_image.has_value(),
            "allocate M3G render target");
    require(machine.graphics().attach_image(
                render_image_object->bits, std::move(*render_image)).has_value(),
            "attach M3G render image");
    require(machine.graphics().attach_context(
                render_graphics_object->bits,
                render_image_object->bits, true).has_value(),
            "attach M3G Graphics target");
    const std::array<phoneme::vm::Value, 3> bind_arguments {
        phoneme::vm::Value::from_reference(*render_graphics_object),
        phoneme::vm::Value::from_int(1),
        phoneme::vm::Value::from_int(0),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "bindTarget",
                "(Ljava/lang/Object;ZI)V", bind_arguments);
    auto initial_hints = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getHints", "()I");
    auto initial_depth_enabled = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "isDepthBufferEnabled", "()Z");
    require(initial_hints.has_value() && initial_hints->completed_normally() &&
                initial_hints->return_value.has_value() &&
                initial_hints->return_value->as_int().value_or(-1) == 0 &&
                initial_depth_enabled.has_value() &&
                initial_depth_enabled->completed_normally() &&
                initial_depth_enabled->return_value.has_value() &&
                initial_depth_enabled->return_value->as_int().value_or(0) == 1,
            "Graphics3D M3G 1.1 target state getters reflect bindTarget");
    const std::array<phoneme::vm::Value, 4> viewport_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(64),
        phoneme::vm::Value::from_int(64),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "setViewport",
                "(IIII)V", viewport_arguments);

    auto render_background = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Background");
    require(render_background.has_value(), "allocate M3G render background");
    invoke_void(*render_background, "javax/microedition/m3g/Background",
                "<init>", "()V");
    const phoneme::vm::Value background_color =
        phoneme::vm::Value::from_int(0x00112233);
    invoke_void(*render_background, "javax/microedition/m3g/Background",
                "setColor", "(I)V",
                std::span<const phoneme::vm::Value>(&background_color, 1U));
    const phoneme::vm::Value render_background_argument =
        phoneme::vm::Value::from_reference(*render_background);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    auto render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value() &&
                (*render_payload)->pixels().front() == 0xFF112233U,
            "Graphics3D.clear writes the Background color");

    auto checker_image = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Image2D");
    auto checker_bytes = machine.heap().allocate_array(
        "[B", 12U, phoneme::vm::Value::from_int(0));
    require(checker_image.has_value() && checker_bytes.has_value(),
            "allocate M3G background image");
    const std::array<phoneme::u8, 12> checker_values {
        255U, 0U, 0U, 0U, 255U, 0U,
        0U, 0U, 255U, 255U, 255U, 255U,
    };
    for (phoneme::usize index = 0U; index < checker_values.size(); ++index) {
        require(machine.heap().set_element(
                    *checker_bytes, index,
                    phoneme::vm::Value::from_int(checker_values[index])).has_value(),
                "write M3G checker image byte");
    }
    const std::array<phoneme::vm::Value, 4> checker_arguments {
        phoneme::vm::Value::from_int(99),
        phoneme::vm::Value::from_int(2),
        phoneme::vm::Value::from_int(2),
        phoneme::vm::Value::from_reference(*checker_bytes),
    };
    invoke_void(*checker_image, "javax/microedition/m3g/Image2D",
                "<init>", "(III[B)V", checker_arguments);
    const phoneme::vm::Value checker_image_argument =
        phoneme::vm::Value::from_reference(*checker_image);
    invoke_void(*render_background, "javax/microedition/m3g/Background",
                "setImage", "(Ljavax/microedition/m3g/Image2D;)V",
                std::span<const phoneme::vm::Value>(
                    &checker_image_argument, 1U));
    const std::array<phoneme::vm::Value, 2> repeat_modes {
        phoneme::vm::Value::from_int(33),
        phoneme::vm::Value::from_int(33),
    };
    invoke_void(*render_background, "javax/microedition/m3g/Background",
                "setImageMode", "(II)V", repeat_modes);
    const std::array<phoneme::vm::Value, 4> repeated_crop {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(4),
        phoneme::vm::Value::from_int(4),
    };
    invoke_void(*render_background, "javax/microedition/m3g/Background",
                "setCrop", "(IIII)V", repeated_crop);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(), "read M3G background image result");
    const auto rendered_pixel = [&](phoneme::i32 x, phoneme::i32 y) {
        return (*render_payload)->pixels()[
            static_cast<phoneme::usize>(y) * 64U +
            static_cast<phoneme::usize>(x)];
    };
    require(rendered_pixel(8, 8) == 0xFFFF0000U &&
                rendered_pixel(24, 8) == 0xFF00FF00U &&
                rendered_pixel(8, 24) == 0xFF0000FFU &&
                rendered_pixel(24, 24) == 0xFFFFFFFFU &&
                rendered_pixel(40, 8) == 0xFFFF0000U,
            "Graphics3D.clear stretches and repeats Background images");
    const phoneme::vm::Value no_background_image =
        phoneme::vm::Value::from_reference({});
    invoke_void(*render_background, "javax/microedition/m3g/Background",
                "setImage", "(Ljavax/microedition/m3g/Image2D;)V",
                std::span<const phoneme::vm::Value>(
                    &no_background_image, 1U));

    auto render_camera = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Camera");
    auto render_camera_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(render_camera.has_value() && render_camera_transform.has_value(),
            "allocate M3G render camera");
    invoke_void(*render_camera, "javax/microedition/m3g/Camera",
                "<init>", "()V");
    invoke_void(*render_camera_transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 4> perspective_arguments {
        phoneme::vm::Value::from_float(60.0F),
        phoneme::vm::Value::from_float(1.0F),
        phoneme::vm::Value::from_float(1.0F),
        phoneme::vm::Value::from_float(100.0F),
    };
    invoke_void(*render_camera, "javax/microedition/m3g/Camera",
                "setPerspective", "(FFFF)V", perspective_arguments);
    const std::array<phoneme::vm::Value, 3> camera_translation {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(5.0F),
    };
    invoke_void(*render_camera_transform,
                "javax/microedition/m3g/Transform", "postTranslate",
                "(FFF)V", camera_translation);
    const std::array<phoneme::vm::Value, 2> camera_arguments {
        phoneme::vm::Value::from_reference(*render_camera),
        phoneme::vm::Value::from_reference(*render_camera_transform),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "setCamera",
                "(Ljavax/microedition/m3g/Camera;"
                "Ljavax/microedition/m3g/Transform;)V",
                camera_arguments);

    auto sprite_appearance = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Appearance");
    auto render_sprite = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Sprite3D");
    auto sprite_root_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(sprite_appearance.has_value() && render_sprite.has_value() &&
                sprite_root_transform.has_value(),
            "allocate M3G Sprite3D render state");
    invoke_void(*sprite_appearance, "javax/microedition/m3g/Appearance",
                "<init>", "()V");
    invoke_void(*sprite_root_transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> sprite_arguments {
        phoneme::vm::Value::from_int(1),
        phoneme::vm::Value::from_reference(*checker_image),
        phoneme::vm::Value::from_reference(*sprite_appearance),
    };
    invoke_void(*render_sprite, "javax/microedition/m3g/Sprite3D",
                "<init>",
                "(ZLjavax/microedition/m3g/Image2D;"
                "Ljavax/microedition/m3g/Appearance;)V",
                sprite_arguments);
    const std::array<phoneme::vm::Value, 3> sprite_scale {
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_float(1.0F),
    };
    invoke_void(*render_sprite, "javax/microedition/m3g/Transformable",
                "setScale", "(FFF)V", sprite_scale);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    const std::array<phoneme::vm::Value, 2> sprite_render_arguments {
        phoneme::vm::Value::from_reference(*render_sprite),
        phoneme::vm::Value::from_reference(*sprite_root_transform),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/Node;"
                "Ljavax/microedition/m3g/Transform;)V",
                sprite_render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(), "read rendered M3G Sprite3D image");
    require(rendered_pixel(27, 27) == 0xFFFF0000U &&
                rendered_pixel(37, 27) == 0xFF00FF00U &&
                rendered_pixel(27, 37) == 0xFF0000FFU &&
                rendered_pixel(37, 37) == 0xFFFFFFFFU,
            "Graphics3D.render draws scaled Sprite3D billboards");

    const std::array<phoneme::vm::Value, 4> flipped_sprite_crop {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(-2),
        phoneme::vm::Value::from_int(2),
    };
    invoke_void(*render_sprite, "javax/microedition/m3g/Sprite3D",
                "setCrop", "(IIII)V", flipped_sprite_crop);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/Node;"
                "Ljavax/microedition/m3g/Transform;)V",
                sprite_render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(),
            "read horizontally flipped Sprite3D image");
    require(rendered_pixel(27, 27) == 0xFF00FF00U &&
                rendered_pixel(37, 27) == 0xFFFF0000U,
            "Sprite3D negative crop width flips the image horizontally");

    auto render_vertices = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexArray");
    require(render_vertices.has_value(), "allocate M3G render vertices");
    const std::array<phoneme::vm::Value, 3> vertex_constructor_arguments {
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(2),
    };
    invoke_void(*render_vertices, "javax/microedition/m3g/VertexArray",
                "<init>", "(III)V", vertex_constructor_arguments);
    auto render_vertex_values = machine.heap().allocate_array(
        "[S", 9U, phoneme::vm::Value::from_int(0));
    require(render_vertex_values.has_value(),
            "allocate M3G render vertex values");
    const std::array<phoneme::i32, 9> triangle_values {
        -1, -1, 0, 1, -1, 0, 0, 1, 0,
    };
    for (phoneme::usize index = 0U; index < triangle_values.size(); ++index) {
        require(machine.heap().set_element(
                    *render_vertex_values, index,
                    phoneme::vm::Value::from_int(
                        triangle_values[index])).has_value(),
                "write M3G render vertex value");
    }
    const std::array<phoneme::vm::Value, 3> vertex_set_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_reference(*render_vertex_values),
    };
    invoke_void(*render_vertices, "javax/microedition/m3g/VertexArray",
                "set", "(II[S)V", vertex_set_arguments);

    auto render_vertex_buffer = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexBuffer");
    require(render_vertex_buffer.has_value(),
            "allocate M3G render VertexBuffer");
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer", "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> render_position_arguments {
        phoneme::vm::Value::from_reference(*render_vertices),
        phoneme::vm::Value::from_float(1.0F),
        phoneme::vm::Value::from_reference({}),
    };
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer", "setPositions",
                "(Ljavax/microedition/m3g/VertexArray;F[F)V",
                render_position_arguments);

    auto strip_lengths = machine.heap().allocate_array(
        "[I", 1U, phoneme::vm::Value::from_int(3));
    auto render_indices = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/TriangleStripArray");
    require(strip_lengths.has_value() && render_indices.has_value(),
            "allocate M3G triangle strip");
    const std::array<phoneme::vm::Value, 2> strip_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(*strip_lengths),
    };
    invoke_void(*render_indices,
                "javax/microedition/m3g/TriangleStripArray",
                "<init>", "(I[I)V", strip_arguments);

    auto render_appearance = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Appearance");
    auto render_model_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(render_appearance.has_value() &&
                render_model_transform.has_value(),
            "allocate M3G render state");
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "<init>", "()V");
    invoke_void(*render_model_transform,
                "javax/microedition/m3g/Transform", "<init>", "()V");
    const std::array<phoneme::vm::Value, 4> render_arguments {
        phoneme::vm::Value::from_reference(*render_vertex_buffer),
        phoneme::vm::Value::from_reference(*render_indices),
        phoneme::vm::Value::from_reference(*render_appearance),
        phoneme::vm::Value::from_reference(*render_model_transform),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;"
                "Ljavax/microedition/m3g/Transform;)V",
                render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(), "read rendered M3G image");
    const auto changed_pixels = std::count_if(
        (*render_payload)->pixels().begin(),
        (*render_payload)->pixels().end(),
        [](phoneme::graphics::Pixel pixel) {
            return pixel != 0xFF112233U;
        });
    require(changed_pixels > 100,
            "Graphics3D.render rasterizes visible geometry");

    auto texture_coordinates_zero = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexArray");
    auto texture_values_zero = machine.heap().allocate_array(
        "[S", 6U, phoneme::vm::Value::from_int(1));
    auto texture_coordinates_one = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexArray");
    auto texture_values_one = machine.heap().allocate_array(
        "[S", 6U, phoneme::vm::Value::from_int(3));
    require(texture_coordinates_zero.has_value() &&
                texture_values_zero.has_value() &&
                texture_coordinates_one.has_value() &&
                texture_values_one.has_value(),
            "allocate M3G multi-texture coordinates");
    for (phoneme::usize vertex = 0U; vertex < 3U; ++vertex) {
        require(machine.heap().set_element(
                    *texture_values_zero, vertex * 2U + 1U,
                    phoneme::vm::Value::from_int(3)).has_value(),
                "write M3G texture V coordinate");
    }
    const std::array<phoneme::vm::Value, 3> texture_array_arguments {
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(2),
        phoneme::vm::Value::from_int(2),
    };
    invoke_void(*texture_coordinates_zero,
                "javax/microedition/m3g/VertexArray", "<init>", "(III)V",
                texture_array_arguments);
    invoke_void(*texture_coordinates_one,
                "javax/microedition/m3g/VertexArray", "<init>", "(III)V",
                texture_array_arguments);
    const std::array<phoneme::vm::Value, 3> texture_zero_set_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_reference(*texture_values_zero),
    };
    const std::array<phoneme::vm::Value, 3> texture_one_set_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_reference(*texture_values_one),
    };
    invoke_void(*texture_coordinates_zero,
                "javax/microedition/m3g/VertexArray", "set", "(II[S)V",
                texture_zero_set_arguments);
    invoke_void(*texture_coordinates_one,
                "javax/microedition/m3g/VertexArray", "set", "(II[S)V",
                texture_one_set_arguments);
    const std::array<phoneme::vm::Value, 4> texture_zero_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(*texture_coordinates_zero),
        phoneme::vm::Value::from_float(0.25F),
        phoneme::vm::Value::from_reference({}),
    };
    const std::array<phoneme::vm::Value, 4> texture_one_arguments {
        phoneme::vm::Value::from_int(1),
        phoneme::vm::Value::from_reference(*texture_coordinates_one),
        phoneme::vm::Value::from_float(0.25F),
        phoneme::vm::Value::from_reference({}),
    };
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer", "setTexCoords",
                "(ILjavax/microedition/m3g/VertexArray;F[F)V",
                texture_zero_arguments);
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer", "setTexCoords",
                "(ILjavax/microedition/m3g/VertexArray;F[F)V",
                texture_one_arguments);

    auto texture_zero = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Texture2D");
    auto texture_one = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Texture2D");
    require(texture_zero.has_value() && texture_one.has_value(),
            "allocate M3G Texture2D units");
    const phoneme::vm::Value checker_texture_argument =
        phoneme::vm::Value::from_reference(*checker_image);
    invoke_void(*texture_zero, "javax/microedition/m3g/Texture2D",
                "<init>", "(Ljavax/microedition/m3g/Image2D;)V",
                std::span<const phoneme::vm::Value>(
                    &checker_texture_argument, 1U));
    invoke_void(*texture_one, "javax/microedition/m3g/Texture2D",
                "<init>", "(Ljavax/microedition/m3g/Image2D;)V",
                std::span<const phoneme::vm::Value>(
                    &checker_texture_argument, 1U));
    const std::array<phoneme::vm::Value, 2> texture_unit_zero_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(*texture_zero),
    };
    const std::array<phoneme::vm::Value, 2> texture_unit_one_arguments {
        phoneme::vm::Value::from_int(1),
        phoneme::vm::Value::from_reference(*texture_one),
    };
    invoke_void(*render_appearance,
                "javax/microedition/m3g/Appearance", "setTexture",
                "(ILjavax/microedition/m3g/Texture2D;)V",
                texture_unit_zero_arguments);
    const auto render_textured = [&]() {
        invoke_void(*graphics3d_object,
                    "javax/microedition/m3g/Graphics3D", "clear",
                    "(Ljavax/microedition/m3g/Background;)V",
                    std::span<const phoneme::vm::Value>(
                        &render_background_argument, 1U));
        invoke_void(*graphics3d_object,
                    "javax/microedition/m3g/Graphics3D", "render",
                    "(Ljavax/microedition/m3g/VertexBuffer;"
                    "Ljavax/microedition/m3g/IndexBuffer;"
                    "Ljavax/microedition/m3g/Appearance;"
                    "Ljavax/microedition/m3g/Transform;)V",
                    render_arguments);
        render_payload = machine.graphics().image(render_image_object->bits);
        require(render_payload.has_value(),
                "read textured M3G framebuffer");
        return rendered_pixel(32, 32);
    };
    require(render_textured() == 0xFFFF0000U,
            "Texture2D FUNC_REPLACE samples texture unit zero");

    const phoneme::vm::Value add_texture_function =
        phoneme::vm::Value::from_int(224);
    invoke_void(*texture_one, "javax/microedition/m3g/Texture2D",
                "setBlending", "(I)V",
                std::span<const phoneme::vm::Value>(
                    &add_texture_function, 1U));
    invoke_void(*render_appearance,
                "javax/microedition/m3g/Appearance", "setTexture",
                "(ILjavax/microedition/m3g/Texture2D;)V",
                texture_unit_one_arguments);
    require(render_textured() == 0xFFFFFF00U,
            "Graphics3D combines multiple Texture2D units in order");

    const std::array<phoneme::vm::Value, 2> remove_texture_one_arguments {
        phoneme::vm::Value::from_int(1),
        phoneme::vm::Value::from_reference({}),
    };
    invoke_void(*render_appearance,
                "javax/microedition/m3g/Appearance", "setTexture",
                "(ILjavax/microedition/m3g/Texture2D;)V",
                remove_texture_one_arguments);
    const std::array<phoneme::vm::Value, 3> translated_texture {
        phoneme::vm::Value::from_float(0.5F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
    };
    invoke_void(*texture_zero,
                "javax/microedition/m3g/Transformable", "setTranslation",
                "(FFF)V", translated_texture);
    require(render_textured() == 0xFF00FF00U,
            "Texture2D transform modifies sampled coordinates");
    const std::array<phoneme::vm::Value, 3> reset_texture_translation {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
    };
    invoke_void(*texture_zero,
                "javax/microedition/m3g/Transformable", "setTranslation",
                "(FFF)V", reset_texture_translation);

    for (phoneme::usize vertex = 0U; vertex < 3U; ++vertex) {
        require(machine.heap().set_element(
                    *texture_values_zero, vertex * 2U,
                    phoneme::vm::Value::from_int(5)).has_value(),
                "write out-of-range texture coordinate");
    }
    invoke_void(*texture_coordinates_zero,
                "javax/microedition/m3g/VertexArray", "set", "(II[S)V",
                texture_zero_set_arguments);
    require(render_textured() == 0xFF00FF00U,
            "Texture2D WRAP_CLAMP clamps out-of-range coordinates");
    const std::array<phoneme::vm::Value, 2> repeat_texture_arguments {
        phoneme::vm::Value::from_int(241),
        phoneme::vm::Value::from_int(240),
    };
    invoke_void(*texture_zero, "javax/microedition/m3g/Texture2D",
                "setWrapping", "(II)V", repeat_texture_arguments);
    require(render_textured() == 0xFFFF0000U,
            "Texture2D WRAP_REPEAT repeats out-of-range coordinates");

    for (phoneme::usize vertex = 0U; vertex < 3U; ++vertex) {
        require(machine.heap().set_element(
                    *texture_values_zero, vertex * 2U,
                    phoneme::vm::Value::from_int(2)).has_value(),
                "write centered texture coordinate");
    }
    invoke_void(*texture_coordinates_zero,
                "javax/microedition/m3g/VertexArray", "set", "(II[S)V",
                texture_zero_set_arguments);
    const std::array<phoneme::vm::Value, 2> linear_filter_arguments {
        phoneme::vm::Value::from_int(208),
        phoneme::vm::Value::from_int(209),
    };
    invoke_void(*texture_zero, "javax/microedition/m3g/Texture2D",
                "setFiltering", "(II)V", linear_filter_arguments);
    const auto linearly_filtered = render_textured();
    require(phoneme::graphics::red(linearly_filtered) > 110U &&
                phoneme::graphics::red(linearly_filtered) < 145U &&
                phoneme::graphics::green(linearly_filtered) > 110U &&
                phoneme::graphics::green(linearly_filtered) < 145U &&
                phoneme::graphics::blue(linearly_filtered) < 15U,
            "Texture2D FILTER_LINEAR bilinearly samples texels");

    const std::array<phoneme::vm::Value, 2> remove_texture_zero_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference({}),
    };
    invoke_void(*render_appearance,
                "javax/microedition/m3g/Appearance", "setTexture",
                "(ILjavax/microedition/m3g/Texture2D;)V",
                remove_texture_zero_arguments);
    auto polygon_mode = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/PolygonMode");
    require(polygon_mode.has_value(), "allocate M3G PolygonMode");
    invoke_void(*polygon_mode, "javax/microedition/m3g/PolygonMode",
                "<init>", "()V");
    const phoneme::vm::Value polygon_argument =
        phoneme::vm::Value::from_reference(*polygon_mode);
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "setPolygonMode", "(Ljavax/microedition/m3g/PolygonMode;)V",
                std::span<const phoneme::vm::Value>(&polygon_argument, 1U));
    const phoneme::vm::Value clockwise_winding =
        phoneme::vm::Value::from_int(169);
    invoke_void(*polygon_mode, "javax/microedition/m3g/PolygonMode",
                "setWinding", "(I)V",
                std::span<const phoneme::vm::Value>(&clockwise_winding, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;"
                "Ljavax/microedition/m3g/Transform;)V",
                render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value() && std::all_of(
                (*render_payload)->pixels().begin(),
                (*render_payload)->pixels().end(),
                [](phoneme::graphics::Pixel pixel) {
                    return pixel == 0xFF112233U;
                }),
            "PolygonMode winding and back-face culling reject reversed faces");
    const phoneme::vm::Value no_culling =
        phoneme::vm::Value::from_int(162);
    invoke_void(*polygon_mode, "javax/microedition/m3g/PolygonMode",
                "setCulling", "(I)V",
                std::span<const phoneme::vm::Value>(&no_culling, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;"
                "Ljavax/microedition/m3g/Transform;)V",
                render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value() && std::any_of(
                (*render_payload)->pixels().begin(),
                (*render_payload)->pixels().end(),
                [](phoneme::graphics::Pixel pixel) {
                    return pixel != 0xFF112233U;
                }),
            "PolygonMode CULL_NONE renders both winding directions");

    auto compositing_mode = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/CompositingMode");
    require(compositing_mode.has_value(), "allocate M3G CompositingMode");
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode", "<init>", "()V");
    const phoneme::vm::Value compositing_argument =
        phoneme::vm::Value::from_reference(*compositing_mode);
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "setCompositingMode",
                "(Ljavax/microedition/m3g/CompositingMode;)V",
                std::span<const phoneme::vm::Value>(
                    &compositing_argument, 1U));
    const phoneme::vm::Value color_write_disabled =
        phoneme::vm::Value::from_int(0);
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode",
                "setColorWriteEnable", "(Z)V",
                std::span<const phoneme::vm::Value>(
                    &color_write_disabled, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;"
                "Ljavax/microedition/m3g/Transform;)V",
                render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value() && std::all_of(
                (*render_payload)->pixels().begin(),
                (*render_payload)->pixels().end(),
                [](phoneme::graphics::Pixel pixel) {
                    return pixel == 0xFF112233U;
                }),
            "CompositingMode color-write disable preserves the framebuffer");

    const phoneme::vm::Value color_write_enabled =
        phoneme::vm::Value::from_int(1);
    const phoneme::vm::Value alpha_blending =
        phoneme::vm::Value::from_int(64);
    const phoneme::vm::Value translucent_red =
        phoneme::vm::Value::from_int(static_cast<phoneme::i32>(0x80FF0000U));
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode",
                "setColorWriteEnable", "(Z)V",
                std::span<const phoneme::vm::Value>(
                    &color_write_enabled, 1U));
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode",
                "setBlending", "(I)V",
                std::span<const phoneme::vm::Value>(&alpha_blending, 1U));
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer",
                "setDefaultColor", "(I)V",
                std::span<const phoneme::vm::Value>(&translucent_red, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;"
                "Ljavax/microedition/m3g/Transform;)V",
                render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(), "read alpha-blended M3G geometry");
    const auto blended_center = rendered_pixel(32, 32);
    require(phoneme::graphics::red(blended_center) > 120U &&
                phoneme::graphics::red(blended_center) < 150U &&
                phoneme::graphics::green(blended_center) > 10U &&
                phoneme::graphics::green(blended_center) < 25U &&
                phoneme::graphics::blue(blended_center) > 15U &&
                phoneme::graphics::blue(blended_center) < 35U,
            "CompositingMode ALPHA blends mesh color over the framebuffer");

    const phoneme::vm::Value counter_clockwise_winding =
        phoneme::vm::Value::from_int(168);
    const phoneme::vm::Value back_culling =
        phoneme::vm::Value::from_int(160);
    const phoneme::vm::Value replace_blending =
        phoneme::vm::Value::from_int(68);
    const phoneme::vm::Value opaque_white =
        phoneme::vm::Value::from_int(static_cast<phoneme::i32>(0xFFFFFFFFU));
    invoke_void(*polygon_mode, "javax/microedition/m3g/PolygonMode",
                "setWinding", "(I)V",
                std::span<const phoneme::vm::Value>(
                    &counter_clockwise_winding, 1U));
    invoke_void(*polygon_mode, "javax/microedition/m3g/PolygonMode",
                "setCulling", "(I)V",
                std::span<const phoneme::vm::Value>(&back_culling, 1U));
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode",
                "setBlending", "(I)V",
                std::span<const phoneme::vm::Value>(&replace_blending, 1U));
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer",
                "setDefaultColor", "(I)V",
                std::span<const phoneme::vm::Value>(&opaque_white, 1U));

    auto render_mesh = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Mesh");
    auto render_world = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/World");
    require(render_mesh.has_value() && render_world.has_value(),
            "allocate M3G World render graph");
    const std::array<phoneme::vm::Value, 3> mesh_arguments {
        phoneme::vm::Value::from_reference(*render_vertex_buffer),
        phoneme::vm::Value::from_reference(*render_indices),
        phoneme::vm::Value::from_reference(*render_appearance),
    };
    invoke_void(*render_mesh, "javax/microedition/m3g/Mesh",
                "<init>",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;)V",
                mesh_arguments);
    invoke_void(*render_world, "javax/microedition/m3g/World",
                "<init>", "()V");
    invoke_void(*render_camera, "javax/microedition/m3g/Transformable",
                "setTranslation", "(FFF)V", camera_translation);
    for (const phoneme::vm::ObjectRef child :
         std::array<phoneme::vm::ObjectRef, 2> {
             *render_camera, *render_mesh}) {
        const phoneme::vm::Value child_argument =
            phoneme::vm::Value::from_reference(child);
        invoke_void(*render_world, "javax/microedition/m3g/Group",
                    "addChild", "(Ljavax/microedition/m3g/Node;)V",
                    std::span<const phoneme::vm::Value>(
                        &child_argument, 1U));
    }
    const phoneme::vm::Value active_camera_argument =
        phoneme::vm::Value::from_reference(*render_camera);
    invoke_void(*render_world, "javax/microedition/m3g/World",
                "setActiveCamera", "(Ljavax/microedition/m3g/Camera;)V",
                std::span<const phoneme::vm::Value>(
                    &active_camera_argument, 1U));

    auto ray_intersection = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/RayIntersection");
    require(ray_intersection.has_value(),
            "allocate M3G RayIntersection");
    invoke_void(*ray_intersection,
                "javax/microedition/m3g/RayIntersection",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 8> ray_pick_arguments {
        phoneme::vm::Value::from_int(-1),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(3.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(-1.0F),
        phoneme::vm::Value::from_reference(*ray_intersection),
    };
    auto ray_pick = machine.invoke_instance(
        *render_world, "javax/microedition/m3g/Group", "pick",
        "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z",
        ray_pick_arguments);
    require(ray_pick.has_value() && ray_pick->completed_normally() &&
                ray_pick->return_value.has_value() &&
                ray_pick->return_value->as_int().value_or(0) == 1,
            "Group.pick intersects Mesh triangles with a 3D ray");
    auto picked_node = machine.invoke_instance(
        *ray_intersection, "javax/microedition/m3g/RayIntersection",
        "getIntersected", "()Ljavax/microedition/m3g/Node;");
    require(picked_node.has_value() && picked_node->completed_normally() &&
                picked_node->return_value.has_value() &&
                picked_node->return_value->as_reference().value_or(
                    phoneme::vm::ObjectRef {}) == *render_mesh,
            "RayIntersection returns the nearest intersected Mesh");
    auto picked_distance = machine.invoke_instance(
        *ray_intersection, "javax/microedition/m3g/RayIntersection",
        "getDistance", "()F");
    require(picked_distance.has_value() &&
                picked_distance->completed_normally() &&
                picked_distance->return_value.has_value() &&
                std::abs(picked_distance->return_value->as_float()
                    .value_or(0.0F) - 3.0F) < 0.001F,
            "RayIntersection reports the nearest ray distance");
    auto picked_normal_z = machine.invoke_instance(
        *ray_intersection, "javax/microedition/m3g/RayIntersection",
        "getNormalZ", "()F");
    require(picked_normal_z.has_value() &&
                picked_normal_z->return_value.has_value() &&
                std::abs(picked_normal_z->return_value->as_float()
                    .value_or(0.0F) - 1.0F) < 0.001F,
            "RayIntersection reports the triangle normal");
    const phoneme::vm::Value second_texture_unit =
        phoneme::vm::Value::from_int(1);
    auto picked_texture_s = machine.invoke_instance(
        *ray_intersection, "javax/microedition/m3g/RayIntersection",
        "getTextureS", "(I)F",
        std::span<const phoneme::vm::Value>(&second_texture_unit, 1U));
    auto picked_texture_t = machine.invoke_instance(
        *ray_intersection, "javax/microedition/m3g/RayIntersection",
        "getTextureT", "(I)F",
        std::span<const phoneme::vm::Value>(&second_texture_unit, 1U));
    require(picked_texture_s.has_value() &&
                picked_texture_s->completed_normally() &&
                picked_texture_s->return_value.has_value() &&
                std::abs(picked_texture_s->return_value->as_float()
                    .value_or(0.0F) - 0.75F) < 0.001F &&
                picked_texture_t.has_value() &&
                picked_texture_t->completed_normally() &&
                picked_texture_t->return_value.has_value() &&
                std::abs(picked_texture_t->return_value->as_float()
                    .value_or(0.0F) - 0.75F) < 0.001F,
            "RayIntersection reports all texture coordinate units");
    auto picked_ray = float_array(
        {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    const phoneme::vm::Value picked_ray_argument =
        phoneme::vm::Value::from_reference(picked_ray);
    invoke_void(*ray_intersection,
                "javax/microedition/m3g/RayIntersection",
                "getRay", "([F)V",
                std::span<const phoneme::vm::Value>(
                    &picked_ray_argument, 1U));
    require(std::abs(read_float_element(picked_ray, 2U) - 3.0F) <
                0.001F &&
                std::abs(read_float_element(picked_ray, 5U) + 1.0F) <
                0.001F,
            "RayIntersection preserves the normalized pick ray");

    auto screen_intersection = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/RayIntersection");
    require(screen_intersection.has_value(),
            "allocate screen-space RayIntersection");
    invoke_void(*screen_intersection,
                "javax/microedition/m3g/RayIntersection",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 5> screen_pick_arguments {
        phoneme::vm::Value::from_int(-1),
        phoneme::vm::Value::from_float(0.5F),
        phoneme::vm::Value::from_float(0.5F),
        phoneme::vm::Value::from_reference({}),
        phoneme::vm::Value::from_reference(*screen_intersection),
    };
    auto screen_pick = machine.invoke_instance(
        *render_world, "javax/microedition/m3g/Group", "pick",
        "(IFFLjavax/microedition/m3g/Camera;"
        "Ljavax/microedition/m3g/RayIntersection;)Z",
        screen_pick_arguments);
    require(screen_pick.has_value() && screen_pick->completed_normally() &&
                screen_pick->return_value.has_value() &&
                screen_pick->return_value->as_int().value_or(0) == 1,
            "Group.pick unprojects screen coordinates through the active Camera");

    const phoneme::vm::Value mesh_scope =
        phoneme::vm::Value::from_int(1);
    invoke_void(*render_mesh, "javax/microedition/m3g/Node",
                "setScope", "(I)V",
                std::span<const phoneme::vm::Value>(&mesh_scope, 1U));
    auto masked_arguments = ray_pick_arguments;
    masked_arguments[0U] = phoneme::vm::Value::from_int(2);
    auto masked_pick = machine.invoke_instance(
        *render_world, "javax/microedition/m3g/Group", "pick",
        "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z",
        masked_arguments);
    require(masked_pick.has_value() && masked_pick->completed_normally() &&
                masked_pick->return_value.has_value() &&
                masked_pick->return_value->as_int().value_or(1) == 0,
            "Group.pick respects Node scope masks");
    const phoneme::vm::Value all_scope =
        phoneme::vm::Value::from_int(-1);
    invoke_void(*render_mesh, "javax/microedition/m3g/Node",
                "setScope", "(I)V",
                std::span<const phoneme::vm::Value>(&all_scope, 1U));

    auto render_normals = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexArray");
    auto render_normal_values = machine.heap().allocate_array(
        "[B", 9U, phoneme::vm::Value::from_int(0));
    require(render_normals.has_value() && render_normal_values.has_value(),
            "allocate M3G lighting normals");
    const std::array<phoneme::vm::Value, 3> normal_constructor_arguments {
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(1),
    };
    invoke_void(*render_normals, "javax/microedition/m3g/VertexArray",
                "<init>", "(III)V", normal_constructor_arguments);
    for (phoneme::usize vertex = 0U; vertex < 3U; ++vertex) {
        require(machine.heap().set_element(
                    *render_normal_values, vertex * 3U + 2U,
                    phoneme::vm::Value::from_int(127)).has_value(),
                "write M3G lighting normal");
    }
    const std::array<phoneme::vm::Value, 3> normal_set_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_reference(*render_normal_values),
    };
    invoke_void(*render_normals, "javax/microedition/m3g/VertexArray",
                "set", "(II[B)V", normal_set_arguments);
    const phoneme::vm::Value lighting_normals_argument =
        phoneme::vm::Value::from_reference(*render_normals);
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer", "setNormals",
                "(Ljavax/microedition/m3g/VertexArray;)V",
                std::span<const phoneme::vm::Value>(
                    &lighting_normals_argument, 1U));

    auto render_material = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Material");
    auto world_light = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Light");
    require(render_material.has_value() && world_light.has_value(),
            "allocate M3G Material and World light");
    invoke_void(*render_material, "javax/microedition/m3g/Material",
                "<init>", "()V");
    invoke_void(*world_light, "javax/microedition/m3g/Light",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 2> red_diffuse_arguments {
        phoneme::vm::Value::from_int(2048),
        phoneme::vm::Value::from_int(
            static_cast<phoneme::i32>(0xFFFF0000U)),
    };
    invoke_void(*render_material, "javax/microedition/m3g/Material",
                "setColor", "(II)V", red_diffuse_arguments);
    const phoneme::vm::Value material_argument =
        phoneme::vm::Value::from_reference(*render_material);
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "setMaterial", "(Ljavax/microedition/m3g/Material;)V",
                std::span<const phoneme::vm::Value>(&material_argument, 1U));
    const phoneme::vm::Value world_light_argument =
        phoneme::vm::Value::from_reference(*world_light);
    invoke_void(*render_world, "javax/microedition/m3g/Group",
                "addChild", "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(
                    &world_light_argument, 1U));

    invoke_void(*render_world, "javax/microedition/m3g/World",
                "setBackground", "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    const phoneme::vm::Value lit_world_argument =
        phoneme::vm::Value::from_reference(*render_world);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/World;)V",
                std::span<const phoneme::vm::Value>(
                    &lit_world_argument, 1U));
    const auto lit_center = rendered_pixel(32, 32);
    require(phoneme::graphics::red(lit_center) > 220U &&
                phoneme::graphics::green(lit_center) < 20U &&
                phoneme::graphics::blue(lit_center) < 20U,
            "World Light nodes illuminate Material geometry using normals");

    auto render_fog = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Fog");
    require(render_fog.has_value(), "allocate M3G Fog");
    invoke_void(*render_fog, "javax/microedition/m3g/Fog",
                "<init>", "()V");
    const phoneme::vm::Value blue_fog =
        phoneme::vm::Value::from_int(0x000000FF);
    invoke_void(*render_fog, "javax/microedition/m3g/Fog",
                "setColor", "(I)V",
                std::span<const phoneme::vm::Value>(&blue_fog, 1U));
    const std::array<phoneme::vm::Value, 2> linear_fog_arguments {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(1.0F),
    };
    invoke_void(*render_fog, "javax/microedition/m3g/Fog",
                "setLinear", "(FF)V", linear_fog_arguments);
    const phoneme::vm::Value fog_argument =
        phoneme::vm::Value::from_reference(*render_fog);
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "setFog", "(Ljavax/microedition/m3g/Fog;)V",
                std::span<const phoneme::vm::Value>(&fog_argument, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/World;)V",
                std::span<const phoneme::vm::Value>(
                    &lit_world_argument, 1U));
    const auto linear_fog_center = rendered_pixel(32, 32);
    require(phoneme::graphics::red(linear_fog_center) < 20U &&
                phoneme::graphics::green(linear_fog_center) < 20U &&
                phoneme::graphics::blue(linear_fog_center) > 220U,
            "Fog LINEAR blends distant geometry to the fog color");

    const phoneme::vm::Value exponential_fog =
        phoneme::vm::Value::from_int(80);
    const phoneme::vm::Value zero_density =
        phoneme::vm::Value::from_float(0.0F);
    invoke_void(*render_fog, "javax/microedition/m3g/Fog",
                "setMode", "(I)V",
                std::span<const phoneme::vm::Value>(
                    &exponential_fog, 1U));
    invoke_void(*render_fog, "javax/microedition/m3g/Fog",
                "setDensity", "(F)V",
                std::span<const phoneme::vm::Value>(&zero_density, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/World;)V",
                std::span<const phoneme::vm::Value>(
                    &lit_world_argument, 1U));
    const auto exponential_fog_center = rendered_pixel(32, 32);
    require(phoneme::graphics::red(exponential_fog_center) > 220U &&
                phoneme::graphics::blue(exponential_fog_center) < 20U,
            "Fog EXPONENTIAL honors zero density");

    const phoneme::vm::Value lighting_null_reference =
        phoneme::vm::Value::from_reference({});
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "setFog", "(Ljavax/microedition/m3g/Fog;)V",
                std::span<const phoneme::vm::Value>(
                    &lighting_null_reference, 1U));
    invoke_void(*render_appearance, "javax/microedition/m3g/Appearance",
                "setMaterial", "(Ljavax/microedition/m3g/Material;)V",
                std::span<const phoneme::vm::Value>(
                    &lighting_null_reference, 1U));
    invoke_void(*render_vertex_buffer,
                "javax/microedition/m3g/VertexBuffer", "setNormals",
                "(Ljavax/microedition/m3g/VertexArray;)V",
                std::span<const phoneme::vm::Value>(
                    &lighting_null_reference, 1U));
    invoke_void(*render_world, "javax/microedition/m3g/Group",
                "removeChild", "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(
                    &world_light_argument, 1U));

    invoke_void(*render_world, "javax/microedition/m3g/World",
                "setBackground", "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    const phoneme::vm::Value world_argument =
        phoneme::vm::Value::from_reference(*render_world);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/World;)V",
                std::span<const phoneme::vm::Value>(&world_argument, 1U));
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(), "read World-rendered M3G image");
    const auto world_changed_pixels = std::count_if(
        (*render_payload)->pixels().begin(),
        (*render_payload)->pixels().end(),
        [](phoneme::graphics::Pixel pixel) {
            return pixel != 0xFF112233U;
        });
    require(world_changed_pixels > 100,
            "Graphics3D.render(World) traverses and rasterizes Mesh nodes");

    auto morph_target_vertices = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexArray");
    auto morph_target_values = machine.heap().allocate_array(
        "[S", 9U, phoneme::vm::Value::from_int(0));
    auto morph_target_buffer = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/VertexBuffer");
    require(morph_target_vertices.has_value() &&
                morph_target_values.has_value() &&
                morph_target_buffer.has_value(),
            "allocate MorphingMesh target geometry");
    const std::array<phoneme::i32, 9> shifted_triangle {
        1, -1, 0,
        3, -1, 0,
        2, 1, 0,
    };
    for (phoneme::usize index = 0U; index < shifted_triangle.size(); ++index) {
        require(machine.heap().set_element(
                    *morph_target_values, index,
                    phoneme::vm::Value::from_int(
                        shifted_triangle[index])).has_value(),
                "write MorphingMesh target vertex");
    }
    const std::array<phoneme::vm::Value, 3> morph_vertex_arguments {
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(2),
    };
    invoke_void(*morph_target_vertices,
                "javax/microedition/m3g/VertexArray", "<init>", "(III)V",
                morph_vertex_arguments);
    const std::array<phoneme::vm::Value, 3> morph_vertex_data_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_reference(*morph_target_values),
    };
    invoke_void(*morph_target_vertices,
                "javax/microedition/m3g/VertexArray", "set", "(II[S)V",
                morph_vertex_data_arguments);
    invoke_void(*morph_target_buffer,
                "javax/microedition/m3g/VertexBuffer", "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> morph_positions_arguments {
        phoneme::vm::Value::from_reference(*morph_target_vertices),
        phoneme::vm::Value::from_float(1.0F),
        phoneme::vm::Value::from_reference({}),
    };
    invoke_void(*morph_target_buffer,
                "javax/microedition/m3g/VertexBuffer", "setPositions",
                "(Ljavax/microedition/m3g/VertexArray;F[F)V",
                morph_positions_arguments);
    auto morph_target_array = machine.heap().allocate_array(
        "[Ljavax/microedition/m3g/VertexBuffer;", 1U,
        phoneme::vm::Value::from_reference({}));
    require(morph_target_array.has_value(),
            "allocate MorphingMesh target array");
    require(machine.heap().set_element(
                *morph_target_array, 0U,
                phoneme::vm::Value::from_reference(
                    *morph_target_buffer)).has_value(),
            "store MorphingMesh target");
    auto morphing_mesh = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/MorphingMesh");
    require(morphing_mesh.has_value(), "allocate MorphingMesh");
    const std::array<phoneme::vm::Value, 4> morph_constructor_arguments {
        phoneme::vm::Value::from_reference(*render_vertex_buffer),
        phoneme::vm::Value::from_reference(*morph_target_array),
        phoneme::vm::Value::from_reference(*render_indices),
        phoneme::vm::Value::from_reference(*render_appearance),
    };
    invoke_void(*morphing_mesh,
                "javax/microedition/m3g/MorphingMesh", "<init>",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "[Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;)V",
                morph_constructor_arguments);
    auto full_morph_weight = float_array({1.0F});
    const phoneme::vm::Value full_morph_weight_argument =
        phoneme::vm::Value::from_reference(full_morph_weight);
    invoke_void(*morphing_mesh,
                "javax/microedition/m3g/MorphingMesh", "setWeights",
                "([F)V", std::span<const phoneme::vm::Value>(
                    &full_morph_weight_argument, 1U));
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    const std::array<phoneme::vm::Value, 2> morph_render_arguments {
        phoneme::vm::Value::from_reference(*morphing_mesh),
        phoneme::vm::Value::from_reference(*render_model_transform),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/Node;"
                "Ljavax/microedition/m3g/Transform;)V",
                morph_render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(),
            "read MorphingMesh-rendered framebuffer");
    require(rendered_pixel(32, 32) == 0xFF112233U &&
                rendered_pixel(53, 32) == 0xFFFFFFFFU,
            "MorphingMesh weights deform rendered vertex positions");

    auto deformation_pick_group = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    auto deformation_intersection = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/RayIntersection");
    require(deformation_pick_group.has_value() &&
                deformation_intersection.has_value(),
            "allocate deformed geometry picking state");
    invoke_void(*deformation_pick_group,
                "javax/microedition/m3g/Group", "<init>", "()V");
    invoke_void(*deformation_intersection,
                "javax/microedition/m3g/RayIntersection", "<init>", "()V");
    const phoneme::vm::Value morph_pick_child =
        phoneme::vm::Value::from_reference(*morphing_mesh);
    invoke_void(*deformation_pick_group,
                "javax/microedition/m3g/Group", "addChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&morph_pick_child, 1U));
    const std::array<phoneme::vm::Value, 8> moved_geometry_ray {
        phoneme::vm::Value::from_int(-1),
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(3.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(-1.0F),
        phoneme::vm::Value::from_reference(*deformation_intersection),
    };
    auto morph_pick = machine.invoke_instance(
        *deformation_pick_group, "javax/microedition/m3g/Group", "pick",
        "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z",
        moved_geometry_ray);
    require(morph_pick.has_value() && morph_pick->completed_normally() &&
                morph_pick->return_value.has_value() &&
                morph_pick->return_value->as_int().value_or(0) == 1,
            "Group.pick intersects the current MorphingMesh pose");
    auto stale_morph_ray = moved_geometry_ray;
    stale_morph_ray[1U] = phoneme::vm::Value::from_float(0.0F);
    auto stale_morph_pick = machine.invoke_instance(
        *deformation_pick_group, "javax/microedition/m3g/Group", "pick",
        "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z",
        stale_morph_ray);
    require(stale_morph_pick.has_value() &&
                stale_morph_pick->completed_normally() &&
                stale_morph_pick->return_value.has_value() &&
                stale_morph_pick->return_value->as_int().value_or(1) == 0,
            "Group.pick does not intersect the stale MorphingMesh base pose");
    invoke_void(*deformation_pick_group,
                "javax/microedition/m3g/Group", "removeChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&morph_pick_child, 1U));

    auto skeleton = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    auto bone = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Group");
    require(skeleton.has_value() && bone.has_value(),
            "allocate SkinnedMesh skeleton");
    invoke_void(*skeleton, "javax/microedition/m3g/Group",
                "<init>", "()V");
    invoke_void(*bone, "javax/microedition/m3g/Group",
                "<init>", "()V");
    const phoneme::vm::Value bone_argument =
        phoneme::vm::Value::from_reference(*bone);
    invoke_void(*skeleton, "javax/microedition/m3g/Group", "addChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&bone_argument, 1U));
    auto skinned_mesh = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/SkinnedMesh");
    require(skinned_mesh.has_value(), "allocate SkinnedMesh");
    const std::array<phoneme::vm::Value, 4> skinned_constructor_arguments {
        phoneme::vm::Value::from_reference(*render_vertex_buffer),
        phoneme::vm::Value::from_reference(*render_indices),
        phoneme::vm::Value::from_reference(*render_appearance),
        phoneme::vm::Value::from_reference(*skeleton),
    };
    invoke_void(*skinned_mesh,
                "javax/microedition/m3g/SkinnedMesh", "<init>",
                "(Ljavax/microedition/m3g/VertexBuffer;"
                "Ljavax/microedition/m3g/IndexBuffer;"
                "Ljavax/microedition/m3g/Appearance;"
                "Ljavax/microedition/m3g/Group;)V",
                skinned_constructor_arguments);
    const std::array<phoneme::vm::Value, 4> bone_transform_arguments {
        phoneme::vm::Value::from_reference(*bone),
        phoneme::vm::Value::from_int(255),
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(3),
    };
    invoke_void(*skinned_mesh,
                "javax/microedition/m3g/SkinnedMesh", "addTransform",
                "(Ljavax/microedition/m3g/Node;III)V",
                bone_transform_arguments);
    auto rest_bone_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(rest_bone_transform.has_value(),
            "allocate SkinnedMesh rest transform destination");
    invoke_void(*rest_bone_transform,
                "javax/microedition/m3g/Transform", "<init>", "()V");
    const std::array<phoneme::vm::Value, 2> get_bone_transform_arguments {
        phoneme::vm::Value::from_reference(*bone),
        phoneme::vm::Value::from_reference(*rest_bone_transform),
    };
    invoke_void(*skinned_mesh,
                "javax/microedition/m3g/SkinnedMesh", "getBoneTransform",
                "(Ljavax/microedition/m3g/Node;"
                "Ljavax/microedition/m3g/Transform;)V",
                get_bone_transform_arguments);
    auto rest_bone_matrix = float_array({
        0.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
    });
    const phoneme::vm::Value rest_bone_matrix_argument =
        phoneme::vm::Value::from_reference(rest_bone_matrix);
    invoke_void(*rest_bone_transform,
                "javax/microedition/m3g/Transform", "get", "([F)V",
                std::span<const phoneme::vm::Value>(
                    &rest_bone_matrix_argument, 1U));
    require(std::abs(read_float_element(rest_bone_matrix, 0U) - 1.0F) <
                0.001F &&
                std::abs(read_float_element(rest_bone_matrix, 5U) - 1.0F) <
                0.001F &&
                std::abs(read_float_element(rest_bone_matrix, 10U) - 1.0F) <
                0.001F &&
                std::abs(read_float_element(rest_bone_matrix, 15U) - 1.0F) <
                0.001F,
            "SkinnedMesh.getBoneTransform returns the stored rest pose");
    const std::array<phoneme::vm::Value, 3> moved_bone_translation {
        phoneme::vm::Value::from_float(2.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
    };
    invoke_void(*bone, "javax/microedition/m3g/Transformable",
                "setTranslation", "(FFF)V", moved_bone_translation);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    const std::array<phoneme::vm::Value, 2> skinned_render_arguments {
        phoneme::vm::Value::from_reference(*skinned_mesh),
        phoneme::vm::Value::from_reference(*render_model_transform),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "render",
                "(Ljavax/microedition/m3g/Node;"
                "Ljavax/microedition/m3g/Transform;)V",
                skinned_render_arguments);
    render_payload = machine.graphics().image(render_image_object->bits);
    require(render_payload.has_value(),
            "read SkinnedMesh-rendered framebuffer");
    require(rendered_pixel(32, 32) == 0xFF112233U &&
                rendered_pixel(53, 32) == 0xFFFFFFFFU,
            "SkinnedMesh bone transforms deform rendered vertex positions");
    const phoneme::vm::Value skinned_pick_child =
        phoneme::vm::Value::from_reference(*skinned_mesh);
    invoke_void(*deformation_pick_group,
                "javax/microedition/m3g/Group", "addChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&skinned_pick_child, 1U));
    auto skinned_pick = machine.invoke_instance(
        *deformation_pick_group, "javax/microedition/m3g/Group", "pick",
        "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z",
        moved_geometry_ray);
    require(skinned_pick.has_value() && skinned_pick->completed_normally() &&
                skinned_pick->return_value.has_value() &&
                skinned_pick->return_value->as_int().value_or(0) == 1,
            "Group.pick intersects the current SkinnedMesh pose");
    auto stale_skinned_pick = machine.invoke_instance(
        *deformation_pick_group, "javax/microedition/m3g/Group", "pick",
        "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z",
        stale_morph_ray);
    require(stale_skinned_pick.has_value() &&
                stale_skinned_pick->completed_normally() &&
                stale_skinned_pick->return_value.has_value() &&
                stale_skinned_pick->return_value->as_int().value_or(1) == 0,
            "Group.pick does not intersect the stale SkinnedMesh base pose");
    invoke_void(*deformation_pick_group,
                "javax/microedition/m3g/Group", "removeChild",
                "(Ljavax/microedition/m3g/Node;)V",
                std::span<const phoneme::vm::Value>(&skinned_pick_child, 1U));

    auto animation_sequence = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/KeyframeSequence");
    auto animation_controller = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/AnimationController");
    auto animation_track = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/AnimationTrack");
    require(animation_sequence.has_value() &&
                animation_controller.has_value() && animation_track.has_value(),
            "allocate M3G animation state");
    const std::array<phoneme::vm::Value, 3> sequence_arguments {
        phoneme::vm::Value::from_int(2),
        phoneme::vm::Value::from_int(3),
        phoneme::vm::Value::from_int(176),
    };
    invoke_void(*animation_sequence,
                "javax/microedition/m3g/KeyframeSequence", "<init>",
                "(III)V", sequence_arguments);
    auto first_animation_values = machine.heap().allocate_array(
        "[F", 3U, phoneme::vm::Value::from_float(0.0F));
    auto second_animation_values = machine.heap().allocate_array(
        "[F", 3U, phoneme::vm::Value::from_float(0.0F));
    require(first_animation_values.has_value() &&
                second_animation_values.has_value(),
            "allocate M3G keyframe values");
    const std::array<float, 3> second_keyframe {10.0F, 20.0F, 30.0F};
    for (phoneme::usize index = 0U; index < second_keyframe.size(); ++index) {
        require(machine.heap().set_element(
                    *second_animation_values, index,
                    phoneme::vm::Value::from_float(
                        second_keyframe[index])).has_value(),
                "write M3G keyframe value");
    }
    const std::array<phoneme::vm::Value, 3> first_keyframe_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(*first_animation_values),
    };
    const std::array<phoneme::vm::Value, 3> second_keyframe_arguments {
        phoneme::vm::Value::from_int(1),
        phoneme::vm::Value::from_int(100),
        phoneme::vm::Value::from_reference(*second_animation_values),
    };
    invoke_void(*animation_sequence,
                "javax/microedition/m3g/KeyframeSequence", "setKeyframe",
                "(II[F)V", first_keyframe_arguments);
    invoke_void(*animation_sequence,
                "javax/microedition/m3g/KeyframeSequence", "setKeyframe",
                "(II[F)V", second_keyframe_arguments);
    const phoneme::vm::Value animation_duration =
        phoneme::vm::Value::from_int(100);
    invoke_void(*animation_sequence,
                "javax/microedition/m3g/KeyframeSequence", "setDuration",
                "(I)V", std::span<const phoneme::vm::Value>(
                    &animation_duration, 1U));
    invoke_void(*animation_controller,
                "javax/microedition/m3g/AnimationController", "<init>",
                "()V");
    const std::array<phoneme::vm::Value, 2> track_arguments {
        phoneme::vm::Value::from_reference(*animation_sequence),
        phoneme::vm::Value::from_int(275),
    };
    invoke_void(*animation_track,
                "javax/microedition/m3g/AnimationTrack", "<init>",
                "(Ljavax/microedition/m3g/KeyframeSequence;I)V",
                track_arguments);
    const phoneme::vm::Value controller_argument =
        phoneme::vm::Value::from_reference(*animation_controller);
    invoke_void(*animation_track,
                "javax/microedition/m3g/AnimationTrack", "setController",
                "(Ljavax/microedition/m3g/AnimationController;)V",
                std::span<const phoneme::vm::Value>(
                    &controller_argument, 1U));
    const phoneme::vm::Value animation_track_argument =
        phoneme::vm::Value::from_reference(*animation_track);
    invoke_void(*render_mesh, "javax/microedition/m3g/Object3D",
                "addAnimationTrack",
                "(Ljavax/microedition/m3g/AnimationTrack;)V",
                std::span<const phoneme::vm::Value>(
                    &animation_track_argument, 1U));
    const phoneme::vm::Value animation_time =
        phoneme::vm::Value::from_int(50);
    auto animated = machine.invoke_instance(
        *render_mesh, "javax/microedition/m3g/Object3D", "animate", "(I)I",
        std::span<const phoneme::vm::Value>(&animation_time, 1U));
    require(animated.has_value() && animated->completed_normally(),
            "Object3D.animate evaluates active tracks");
    auto animated_translation = machine.heap().allocate_array(
        "[F", 3U, phoneme::vm::Value::from_float(0.0F));
    require(animated_translation.has_value(),
            "allocate M3G animated translation destination");
    const phoneme::vm::Value animated_translation_argument =
        phoneme::vm::Value::from_reference(*animated_translation);
    invoke_void(*render_mesh, "javax/microedition/m3g/Transformable",
                "getTranslation", "([F)V",
                std::span<const phoneme::vm::Value>(
                    &animated_translation_argument, 1U));
    for (phoneme::usize index = 0U; index < 3U; ++index) {
        auto value = machine.heap().element(*animated_translation, index);
        require(value.has_value(), "read M3G animated translation");
        auto number = value->as_float();
        require(number.has_value() &&
                    std::abs(*number - second_keyframe[index] * 0.5F) < 0.01F,
                "Object3D.animate linearly applies translation keyframes");
    }

    auto far_geometry_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(far_geometry_transform.has_value(),
            "allocate M3G depth-test transform");
    invoke_void(*far_geometry_transform,
                "javax/microedition/m3g/Transform", "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> far_translation {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(-1.0F),
    };
    invoke_void(*far_geometry_transform,
                "javax/microedition/m3g/Transform", "postTranslate",
                "(FFF)V", far_translation);
    const auto draw_depth_geometry = [&](phoneme::vm::ObjectRef transform) {
        const std::array<phoneme::vm::Value, 4> arguments {
            phoneme::vm::Value::from_reference(*render_vertex_buffer),
            phoneme::vm::Value::from_reference(*render_indices),
            phoneme::vm::Value::from_reference(*render_appearance),
            phoneme::vm::Value::from_reference(transform),
        };
        invoke_void(*graphics3d_object,
                    "javax/microedition/m3g/Graphics3D", "render",
                    "(Ljavax/microedition/m3g/VertexBuffer;"
                    "Ljavax/microedition/m3g/IndexBuffer;"
                    "Ljavax/microedition/m3g/Appearance;"
                    "Ljavax/microedition/m3g/Transform;)V",
                    arguments);
    };
    const phoneme::vm::Value depth_white =
        phoneme::vm::Value::from_int(
            static_cast<phoneme::i32>(0xFFFFFFFFU));
    const phoneme::vm::Value depth_red =
        phoneme::vm::Value::from_int(
            static_cast<phoneme::i32>(0xFFFF0000U));
    const auto set_depth_color = [&](const phoneme::vm::Value& color) {
        invoke_void(*render_vertex_buffer,
                    "javax/microedition/m3g/VertexBuffer",
                    "setDefaultColor", "(I)V",
                    std::span<const phoneme::vm::Value>(&color, 1U));
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    set_depth_color(depth_white);
    draw_depth_geometry(*render_model_transform);
    const std::array<phoneme::vm::Value, 2> negative_depth_offset {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(-1.0F),
    };
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode",
                "setDepthOffset", "(FF)V", negative_depth_offset);
    set_depth_color(depth_red);
    draw_depth_geometry(*render_model_transform);
    require(rendered_pixel(32, 32) == 0xFFFF0000U,
            "CompositingMode depth offset resolves coplanar geometry");
    const std::array<phoneme::vm::Value, 2> zero_depth_offset {
        phoneme::vm::Value::from_float(0.0F),
        phoneme::vm::Value::from_float(0.0F),
    };
    invoke_void(*compositing_mode,
                "javax/microedition/m3g/CompositingMode",
                "setDepthOffset", "(FF)V", zero_depth_offset);

    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    set_depth_color(depth_white);
    draw_depth_geometry(*render_model_transform);
    set_depth_color(depth_red);
    draw_depth_geometry(*far_geometry_transform);
    require(rendered_pixel(32, 32) == 0xFFFFFFFFU,
            "Graphics3D depth buffer rejects farther geometry");

    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "releaseTarget", "()V");
    const std::array<phoneme::vm::Value, 3> no_depth_bind_arguments {
        phoneme::vm::Value::from_reference(*render_graphics_object),
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_int(16),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "bindTarget",
                "(Ljava/lang/Object;ZI)V", no_depth_bind_arguments);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "clear",
                "(Ljavax/microedition/m3g/Background;)V",
                std::span<const phoneme::vm::Value>(
                    &render_background_argument, 1U));
    set_depth_color(depth_white);
    draw_depth_geometry(*render_model_transform);
    set_depth_color(depth_red);
    draw_depth_geometry(*far_geometry_transform);
    auto disabled_depth = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "isDepthBufferEnabled", "()Z");
    auto overwrite_hints = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getHints", "()I");
    require(rendered_pixel(32, 32) == 0xFFFF0000U &&
                disabled_depth.has_value() &&
                disabled_depth->return_value.has_value() &&
                disabled_depth->return_value->as_int().value_or(1) == 0 &&
                overwrite_hints.has_value() &&
                overwrite_hints->return_value.has_value() &&
                overwrite_hints->return_value->as_int().value_or(0) == 16,
            "bindTarget depth=false disables depth testing and preserves hints");
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "releaseTarget", "()V");
    const phoneme::vm::Value simple_bind_target =
        phoneme::vm::Value::from_reference(*render_graphics_object);
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "bindTarget",
                "(Ljava/lang/Object;)V",
                std::span<const phoneme::vm::Value>(
                    &simple_bind_target, 1U));
    auto default_depth = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "isDepthBufferEnabled", "()Z");
    auto default_hints = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getHints", "()I");
    require(default_depth.has_value() && default_depth->return_value.has_value() &&
                default_depth->return_value->as_int().value_or(0) == 1 &&
                default_hints.has_value() && default_hints->return_value.has_value() &&
                default_hints->return_value->as_int().value_or(-1) == 0,
            "bindTarget(target) restores the JSR-184 default depth and hints");
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "releaseTarget", "()V");

    auto light = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Light");
    auto light_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(light.has_value() && light_transform.has_value(),
            "allocate Graphics3D light state");
    invoke_void(*light, "javax/microedition/m3g/Light", "<init>", "()V");
    invoke_void(*light_transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    invoke_void(*light_transform, "javax/microedition/m3g/Transform",
                "postTranslate", "(FFF)V", translation);
    const std::array<phoneme::vm::Value, 2> add_light_arguments {
        phoneme::vm::Value::from_reference(*light),
        phoneme::vm::Value::from_reference(*light_transform),
    };
    auto light_index = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "addLight",
        "(Ljavax/microedition/m3g/Light;"
        "Ljavax/microedition/m3g/Transform;)I",
        add_light_arguments);
    require(light_index.has_value() && light_index->completed_normally() &&
                light_index->return_value.has_value() &&
                light_index->return_value->as_int().value_or(-1) == 0,
            "Graphics3D.addLight stores the first light");
    auto light_count = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getLightCount", "()I");
    require(light_count.has_value() && light_count->completed_normally() &&
                light_count->return_value.has_value() &&
                light_count->return_value->as_int().value_or(0) == 1,
            "Graphics3D.getLightCount reports active lights");

    auto returned_transform = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Transform");
    require(returned_transform.has_value(),
            "allocate Graphics3D light transform destination");
    invoke_void(*returned_transform, "javax/microedition/m3g/Transform",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 2> get_light_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(*returned_transform),
    };
    auto returned_light = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getLight",
        "(ILjavax/microedition/m3g/Transform;)"
        "Ljavax/microedition/m3g/Light;",
        get_light_arguments);
    require(returned_light.has_value() &&
                returned_light->completed_normally() &&
                returned_light->return_value.has_value() &&
                returned_light->return_value->as_reference().value_or(
                    phoneme::vm::ObjectRef {}) == *light,
            "Graphics3D.getLight returns the stored light");
    auto returned_matrix = machine.heap().allocate_array(
        "[F", 16U, phoneme::vm::Value::from_float(0.0F));
    require(returned_matrix.has_value(),
            "allocate returned light matrix destination");
    const phoneme::vm::Value returned_matrix_argument =
        phoneme::vm::Value::from_reference(*returned_matrix);
    invoke_void(*returned_transform, "javax/microedition/m3g/Transform",
                "get", "([F)V",
                std::span<const phoneme::vm::Value>(
                    &returned_matrix_argument, 1U));
    auto returned_translation = machine.heap().element(*returned_matrix, 3U);
    require(returned_translation.has_value() &&
                std::abs(returned_translation->as_float().value_or(0.0F) -
                         4.0F) < 0.0001F,
            "Graphics3D.getLight copies the stored transform");

    auto replacement_light = machine.class_states().allocate_instance(
        machine.heap(), "javax/microedition/m3g/Light");
    require(replacement_light.has_value(),
            "allocate replacement Graphics3D light");
    invoke_void(*replacement_light, "javax/microedition/m3g/Light",
                "<init>", "()V");
    const std::array<phoneme::vm::Value, 3> set_light_arguments {
        phoneme::vm::Value::from_int(0),
        phoneme::vm::Value::from_reference(*replacement_light),
        phoneme::vm::Value::from_reference({}),
    };
    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "setLight",
                "(ILjavax/microedition/m3g/Light;"
                "Ljavax/microedition/m3g/Transform;)V",
                set_light_arguments);
    returned_light = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getLight",
        "(ILjavax/microedition/m3g/Transform;)"
        "Ljavax/microedition/m3g/Light;",
        get_light_arguments);
    require(returned_light.has_value() &&
                returned_light->completed_normally() &&
                returned_light->return_value.has_value() &&
                returned_light->return_value->as_reference().value_or(
                    phoneme::vm::ObjectRef {}) == *replacement_light,
            "Graphics3D.setLight replaces stored state");

    invoke_void(*graphics3d_object,
                "javax/microedition/m3g/Graphics3D", "resetLights", "()V");
    returned_light = machine.invoke_instance(
        *graphics3d_object, "javax/microedition/m3g/Graphics3D",
        "getLight",
        "(ILjavax/microedition/m3g/Transform;)"
        "Ljavax/microedition/m3g/Light;",
        get_light_arguments);
    require(returned_light.has_value() &&
                !returned_light->completed_normally() &&
                returned_light->throwable.has_value(),
            "Graphics3D.resetLights removes indexed light state");

    const auto append_u8 = [](std::vector<phoneme::u8>& bytes,
                              phoneme::u8 value) {
        bytes.push_back(value);
    };
    const auto append_u16 = [](std::vector<phoneme::u8>& bytes,
                               phoneme::u16 value) {
        bytes.push_back(static_cast<phoneme::u8>(value & 0xFFU));
        bytes.push_back(static_cast<phoneme::u8>((value >> 8U) & 0xFFU));
    };
    const auto append_u32 = [](std::vector<phoneme::u8>& bytes,
                               phoneme::u32 value) {
        for (phoneme::usize shift = 0U; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<phoneme::u8>(
                (value >> shift) & 0xFFU));
        }
    };
    const auto append_float = [&](std::vector<phoneme::u8>& bytes,
                                  float value) {
        append_u32(bytes, std::bit_cast<phoneme::u32>(value));
    };
    const auto append_object3d = [&](std::vector<phoneme::u8>& payload,
                                     phoneme::u32 user_id = 0U) {
        append_u32(payload, user_id);
        append_u32(payload, 0U);
        append_u32(payload, 0U);
    };
    const auto append_object = [&](std::vector<phoneme::u8>& section,
                                   phoneme::u8 type,
                                   const std::vector<phoneme::u8>& payload) {
        append_u8(section, type);
        append_u32(section, static_cast<phoneme::u32>(payload.size()));
        section.insert(section.end(), payload.begin(), payload.end());
    };

    std::vector<phoneme::u8> serialized_objects;
    append_object(serialized_objects, 0U, {});
    std::vector<phoneme::u8> serialized_vertices;
    append_object3d(serialized_vertices, 77U);
    append_u8(serialized_vertices, 2U);
    append_u8(serialized_vertices, 3U);
    append_u8(serialized_vertices, 0U);
    append_u16(serialized_vertices, 3U);
    for (const phoneme::i32 value :
         std::array<phoneme::i32, 9> {
             -1, -1, 0, 1, -1, 0, 0, 1, 0}) {
        append_u16(serialized_vertices,
                   static_cast<phoneme::u16>(value));
    }
    append_object(serialized_objects, 20U, serialized_vertices);

    std::vector<phoneme::u8> serialized_strip;
    append_object3d(serialized_strip);
    append_u8(serialized_strip, 0U);
    append_u32(serialized_strip, 0U);
    append_u32(serialized_strip, 1U);
    append_u32(serialized_strip, 3U);
    append_object(serialized_objects, 11U, serialized_strip);

    std::vector<phoneme::u8> serialized_vertex_buffer;
    append_object3d(serialized_vertex_buffer);
    append_u8(serialized_vertex_buffer, 255U);
    append_u8(serialized_vertex_buffer, 255U);
    append_u8(serialized_vertex_buffer, 255U);
    append_u8(serialized_vertex_buffer, 255U);
    append_u32(serialized_vertex_buffer, 2U);
    append_float(serialized_vertex_buffer, 0.0F);
    append_float(serialized_vertex_buffer, 0.0F);
    append_float(serialized_vertex_buffer, 0.0F);
    append_float(serialized_vertex_buffer, 1.0F);
    append_u32(serialized_vertex_buffer, 0U);
    append_u32(serialized_vertex_buffer, 0U);
    append_u32(serialized_vertex_buffer, 0U);
    append_object(serialized_objects, 21U, serialized_vertex_buffer);

    std::vector<phoneme::u8> serialized_m3g {
        0xABU, 0x4AU, 0x53U, 0x52U, 0x31U, 0x38U,
        0x34U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
    };
    std::vector<phoneme::u8> section;
    append_u8(section, 0U);
    const phoneme::u32 section_length = static_cast<phoneme::u32>(
        13U + serialized_objects.size());
    append_u32(section, section_length);
    append_u32(section,
               static_cast<phoneme::u32>(serialized_objects.size()));
    section.insert(section.end(), serialized_objects.begin(),
                   serialized_objects.end());
    uLong checksum = ::adler32(0L, Z_NULL, 0);
    checksum = ::adler32(checksum,
        reinterpret_cast<const Bytef*>(section.data()),
        static_cast<uInt>(section.size()));
    append_u32(section, static_cast<phoneme::u32>(checksum));
    serialized_m3g.insert(serialized_m3g.end(), section.begin(), section.end());

    auto loaded_m3g = phoneme::vm::m3g::load_m3g(
        machine, serialized_m3g, {});
    require(loaded_m3g.has_value(), "load a serialized M3G geometry graph");
    auto root_count = machine.heap().array_length(*loaded_m3g);
    require(root_count.has_value() && *root_count == 2U,
            "M3G loader returns only unreferenced root objects");
    phoneme::vm::ObjectRef loaded_strip {};
    phoneme::vm::ObjectRef loaded_vertex_buffer {};
    for (phoneme::usize index = 0U; index < *root_count; ++index) {
        auto value = machine.heap().element(*loaded_m3g, index);
        require(value.has_value(), "read M3G loaded root");
        auto reference = value->as_reference();
        require(reference.has_value() && !reference->is_null(),
                "M3G loaded root is non-null");
        auto class_name = machine.heap().class_name(*reference);
        require(class_name.has_value(), "read M3G loaded root class");
        if (*class_name == "javax/microedition/m3g/TriangleStripArray") {
            loaded_strip = *reference;
        } else if (*class_name == "javax/microedition/m3g/VertexBuffer") {
            loaded_vertex_buffer = *reference;
        }
    }
    require(!loaded_strip.is_null() && !loaded_vertex_buffer.is_null(),
            "M3G loader preserves geometry root types");
    auto loaded_index_count = machine.invoke_instance(
        loaded_strip, "javax/microedition/m3g/IndexBuffer",
        "getIndexCount", "()I");
    auto loaded_vertex_count = machine.invoke_instance(
        loaded_vertex_buffer, "javax/microedition/m3g/VertexBuffer",
        "getVertexCount", "()I");
    require(loaded_index_count.has_value() &&
                loaded_index_count->return_value.has_value() &&
                loaded_index_count->return_value->as_int().value_or(-1) == 3,
            "M3G loader decodes TriangleStripArray indices");
    require(loaded_vertex_count.has_value() &&
                loaded_vertex_count->return_value.has_value() &&
                loaded_vertex_count->return_value->as_int().value_or(-1) == 3,
            "M3G loader links VertexBuffer positions");
}

void test_framebuffer_sizes() {
    phoneme::runtime::Framebuffer framebuffer;
    require(framebuffer.resize({320, 240}).has_value(),
            "allocate 320x240 framebuffer");
    auto frame = framebuffer.snapshot();
    require(frame.rgba.size() == 320U * 240U * 4U,
            "framebuffer uses exact RGBA byte count");
    require(!framebuffer.resize({0, 240}).has_value(),
            "reject zero framebuffer width");
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: CoreTests <fixture.jar>");
    phoneme::vm::PerformanceCounters::reset();
    require(std::atexit(write_performance_snapshot) == 0,
            "register VM profile writer");
    const std::string fixture_jar = argv[1];
    const char* filter = std::getenv("PHONEME_TEST_FILTER");
    if (filter != nullptr &&
        std::string_view(filter) == "vm-invocation") {
        test_machine_invocation(fixture_jar);
        std::cout << "Standalone VM invocation tests passed\n";
        return 0;
    }
    if (filter != nullptr &&
        std::string_view(filter) == "vm-dispatch") {
        test_machine_dispatch(fixture_jar);
        std::cout << "Standalone VM dispatch tests passed\n";
        return 0;
    }
    if (filter != nullptr &&
        std::string_view(filter) == "monitor-shutdown") {
        test_monitor_table();
        test_machine_shutdown_waiter(fixture_jar);
        std::cout << "Standalone monitor shutdown tests passed\n";
        return 0;
    }
    if (filter != nullptr &&
        std::string_view(filter) == "vm-extended") {
        test_machine_extended_opcodes(fixture_jar);
        std::cout << "Standalone VM extended opcode tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "security") {
        test_archive_and_classfile(fixture_jar);
        test_suite_permissions(fixture_jar);
        test_security_policy(fixture_jar);
        test_machine_security(fixture_jar);
        test_machine_filesystem(fixture_jar);
        test_machine_network(fixture_jar);
        test_machine_media(fixture_jar);
        std::cout << "Standalone Security Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "network") {
        test_archive_and_classfile(fixture_jar);
        test_builtin_boot_classes();
        test_machine_network(fixture_jar);
        std::cout << "Standalone Network Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "media") {
        test_archive_and_classfile(fixture_jar);
        test_builtin_boot_classes();
        test_machine_media(fixture_jar);
        std::cout << "Standalone Media Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "xml") {
        test_builtin_boot_classes();
        test_machine_xml(fixture_jar);
        std::cout << "Standalone XML Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "graphics") {
        test_archive_and_classfile(fixture_jar);
        test_builtin_boot_classes();
        test_machine_graphics(fixture_jar);
        std::cout << "Standalone Graphics Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "canvas") {
        test_archive_and_classfile(fixture_jar);
        test_builtin_boot_classes();
        test_runtime_canvas(fixture_jar);
        std::cout << "Standalone Canvas Graphics tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "m3g") {
        test_builtin_boot_classes();
        test_machine_m3g();
        std::cout << "Standalone M3G Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "micro3d") {
        test_archive_and_classfile(fixture_jar);
        test_builtin_boot_classes();
        test_machine_micro3d(fixture_jar);
        std::cout << "Standalone Micro3D Core tests passed\n";
        return 0;
    }
    if (filter != nullptr && std::string_view(filter) == "game-api") {
        test_archive_and_classfile(fixture_jar);
        test_builtin_boot_classes();
        test_machine_game_api(fixture_jar);
        std::cout << "Standalone MIDP Game API tests passed\n";
        return 0;
    }
    if (filter != nullptr &&
        std::string_view(filter) == "failure-isolation") {
        test_runtime_failure_isolation(fixture_jar);
        std::cout << "Standalone runtime failure isolation tests passed\n";
        return 0;
    }
    test_archive_and_classfile(fixture_jar);
    test_suite_permissions(fixture_jar);
    test_security_policy(fixture_jar);
    test_machine_security(fixture_jar);
    test_machine_filesystem(fixture_jar);
    test_builtin_boot_classes();
    test_class_layout(fixture_jar);
    test_heap_generation_and_collection();
    test_descriptors_and_slots();
    test_bytecode_structure_verifier();
    test_type_state_verifier();
    test_monitor_table();
    test_integer_interpreter();
    test_machine_heap_memory_reporting();
    test_machine_invocation(fixture_jar);
    test_machine_exceptions(fixture_jar);
    test_machine_extended_opcodes(fixture_jar);
    test_machine_rms(fixture_jar);
    test_machine_push(fixture_jar);
    test_runtime_push_queue(fixture_jar);
    test_machine_network(fixture_jar);
    test_machine_media(fixture_jar);
    test_machine_xml(fixture_jar);
    test_machine_graphics(fixture_jar);
    test_machine_game_api(fixture_jar);
    test_machine_m3g();
    test_machine_micro3d(fixture_jar);
    test_machine_gc_roots(fixture_jar);
    test_machine_shutdown_waiter(fixture_jar);
    test_runtime_lcdui(fixture_jar);
    test_runtime_canvas(fixture_jar);
    test_machine_timer(fixture_jar);
    test_runtime_lifecycle(fixture_jar);
    test_runtime_lock_reentry(fixture_jar);
    test_runtime_failure_isolation(fixture_jar);
    test_framebuffer_sizes();
    std::cout << "Standalone Core tests passed\n";
    return 0;
}
