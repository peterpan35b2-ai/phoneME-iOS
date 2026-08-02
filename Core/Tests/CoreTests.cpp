#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/BytecodeVerifier.hpp"
#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/filesystem/FileSystem.hpp"
#include "phoneme/runtime/Framebuffer.hpp"
#include "phoneme/runtime/Runtime.hpp"
#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"
#include "phoneme/vm/ClassLayout.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/Heap.hpp"
#include "phoneme/vm/Interpreter.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/MonitorTable.hpp"
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

void test_builtin_boot_classes() {
    phoneme::vm::ClassRepository classes;

    auto object = classes.load("java/lang/Object");
    auto string = classes.load("java.lang.String");
    auto midlet = classes.load("javax/microedition/midlet/MIDlet");
    auto media_manager = classes.load("javax/microedition/media/Manager");
    auto media_player = classes.load("javax/microedition/media/Player");
    require(object.has_value(), "load C++ built-in java/lang/Object");
    require(string.has_value(), "load C++ built-in java/lang/String");
    require(midlet.has_value(), "load C++ built-in MIDlet");
    require(media_manager.has_value(), "load C++ built-in MMAPI Manager");
    require(media_player.has_value(), "load C++ built-in MMAPI Player");
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
    require(heap.stats().live_objects == 2,
            "GC preserves transitive references");

    require(heap.collect(std::span<const phoneme::vm::ObjectRef> {}).has_value(),
            "collect without roots");
    require(heap.stats().live_objects == 0,
            "GC releases unreachable objects");
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
    require_jdk8_string_result("builderObjectAppend", 8,
                               "append object and null through StringBuilder");
    require_jdk8_string_result("arraycopyOverlap", 1123,
                               "System.arraycopy preserves overlapping copy semantics");
    require_jdk8_string_result("arraycopyReferences", 5,
                               "System.arraycopy validates reference arrays");
    require_jdk8_string_result("arraycopyExceptions", 60,
                               "native arraycopy failures unwind as Java exceptions");
    require_jdk8_string_result("stringApi", 4095,
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
    require_jdk8_string_result("wrapperApi", 511,
                               "execute JDK 8 boxing and java.lang wrapper APIs");
    require_jdk8_string_result("wrapperExceptions", 15,
                               "wrapper parse failures unwind as NumberFormatException");
    require_jdk8_string_result("mathApi", 255,
                               "execute java.lang.Math numeric semantics");
    require_jdk8_string_result("utilApi", 63,
                               "execute CLDC Vector Stack Hashtable and Random APIs");
    require_jdk8_string_result("utilExceptions", 15,
                               "java.util failures unwind through Java exceptions");
    require_jdk8_string_result("timeApi", 1023,
                               "execute Date TimeZone and Calendar Gregorian semantics");
    require_jdk8_string_result("timeExceptions", 15,
                               "time API failures unwind through Java exceptions");
    require_jdk8_string_result("ioRoundTrip", 1023,
                               "round-trip DataInputStream and DataOutputStream values");
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
    require_jdk8_string_result("systemRuntimeApi", 255,
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
    const std::filesystem::path temporary = root / "tmp";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    require(!cleanup_error, "clear filesystem fixture directory");

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
    invoke("traversalBlocked", 1,
           "JSR-75 rejects paths outside the application sandbox");
    invoke("closedHandleRejected", 1,
           "java.io file stream rejects access after close");

    std::filesystem::remove_all(root, cleanup_error);
    require(!cleanup_error, "remove filesystem fixture directory");
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
    require(invoke_integer("streamSurvivesConnectionClose",
                           "execute connection ownership fixture") == 0x33,
            "stream remains valid after Connection.close");
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
    require(invoke_integer("httpsRoundTrip",
                           "execute HTTPS adapter fixture") == 288,
            "HTTPS exposes TLS and certificate security metadata");
    require(adapter->open_handle_count() == 0U,
            "all Java network handles close after fixture execution");

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

    auto result = machine.invoke_static("corefixture/MediaOps",
                                        "run",
                                        "()I",
                                        {},
                                        20'000'000);
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "execute MMAPI lifecycle and control fixture");
    auto events = result->return_value->as_int();
    require(events.has_value() && *events == 15,
            "MMAPI fixture receives start stop close and volume events");
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
    require(result.has_value() && result->completed_normally() &&
                result->return_value.has_value(),
            "execute MIDP Image Graphics Font and PNG fixture");
    auto status = result->return_value->as_int();
    require(status.has_value() && *status == 0,
            "graphics fixture preserves pixels transforms alpha and metrics");
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

    require(machine.collect_garbage().has_value(),
            "idle machine collection succeeds");
    const auto final_stats = machine.heap().stats();
    require(final_stats.live_objects == 1U,
            "idle collection retains only the static root");
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
    require(runtime.start_midlet(*suite_id,
                                 "corefixture.LcduiApp",
                                 app_id,
                                 phoneme::Dimensions {320, 240}).has_value(),
            "start MIDlet that creates native LCDUI components");
    require(runtime.app_state(app_id) == phoneme::runtime::AppState::active,
            "LCDUI fixture remains active after startApp");

    bool screen_created = false;
    bool screen_shown = false;
    bool screen_updated = false;
    bool status_ready = false;
    bool status_running = false;
    bool text_field_updated = false;
    bool gauge_updated = false;
    bool choice_group_shown = false;
    bool easy_choice = false;
    bool hard_choice = false;
    bool tail_item = false;
    bool ok_command = false;
    bool back_command = false;
    bool menu_command = false;
    phoneme::i32 status_id = 0;
    phoneme::i32 text_field_id = 0;
    phoneme::i32 gauge_id = 0;
    phoneme::i32 choice_id = 0;
    phoneme::i32 ok_command_id = 0;
    phoneme::i32 menu_command_id = 0;
    phoneme::i32 bridged_event_count = 0;

    while (auto event = runtime.poll_ui_event()) {
        if (event->kind >= 2 && event->kind <= 16) {
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
    }

    require(bridged_event_count >= 23,
            "LCDUI fixture emits a complete screen and item event stream");
    require(screen_created && screen_shown && screen_updated,
            "LCDUI Form creation visibility and title updates are bridged");
    require(status_ready && status_running,
            "StringItem creation and mutation are bridged");
    require(text_field_updated,
            "TextField metadata and UTF-8 text are bridged");
    require(gauge_updated,
            "Gauge value and range updates are bridged");
    require(choice_group_shown && easy_choice && hard_choice,
            "ChoiceGroup items and initial selections are bridged");
    require(tail_item, "Form.append(String) creates a native StringItem");
    require(ok_command && back_command && menu_command,
            "MIDP commands preserve labels types priorities and long labels");
    require(status_id != 0 && text_field_id != 0 && gauge_id != 0 &&
                choice_id != 0 && ok_command_id != 0 && menu_command_id != 0,
            "LCDUI bridge registers interactive component IDs");

    runtime.ui_set_text(text_field_id, "Người", 5);
    runtime.ui_set_gauge(gauge_id, 9);
    runtime.ui_set_choice(choice_id, 1, true);
    runtime.ui_focus_item(text_field_id);
    runtime.ui_select_command(ok_command_id);

    bool native_text_applied = false;
    bool native_gauge_applied = false;
    bool native_choice_applied = false;
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
        if (event->kind == 16 && event->component_id == text_field_id) {
            focus_applied = true;
        }
        if (event->kind == 8 && event->component_id == status_id &&
            event->detail == "Hard selected") {
            command_callback_applied = true;
        }
    }
    require(native_text_applied && native_gauge_applied &&
                native_choice_applied && focus_applied,
            "native LCDUI input updates Java item state and emits refresh events");
    require(command_callback_applied,
            "native command selection invokes Java CommandListener callback");

    runtime.ui_select_command(menu_command_id);
    bool list_created = false;
    bool list_shown = false;
    bool first_list_choice = false;
    bool second_list_choice = false;
    phoneme::i32 list_choice_id = 0;
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
    }
    require(list_created && list_shown && first_list_choice &&
                second_list_choice && list_choice_id != 0,
            "List screen and implicit choices are bridged after command callback");

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

    require(runtime.destroy_midlet(app_id).has_value(),
            "destroy LCDUI fixture MIDlet");
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
    require(frame.rgba[0] == 0x12U && frame.rgba[1] == 0x34U &&
                frame.rgba[2] == 0x56U && frame.rgba[3] == 0xFFU,
            "Canvas Graphics renders through the agreed render contract");

    runtime.send_key(-1, true);
    runtime.send_key(-1, true);
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

    require(runtime.destroy_midlet(canvas_app).has_value(),
            "destroy Canvas fixture MIDlet");
    require(runtime.destroy_midlet(plain_app).has_value(),
            "destroy secondary lifecycle MIDlet");
}

void test_runtime_lifecycle(const std::string& fixture_jar) {
    phoneme::runtime::Runtime runtime;
    const std::string runtime_home =
        std::filesystem::path(fixture_jar).parent_path().string();
    require(runtime.configure(runtime_home).has_value(),
            "configure runtime without classes.zip");

    auto suite_id = runtime.install_jar(fixture_jar);
    require(suite_id.has_value(), "install standalone fixture suite");
    require(runtime.start_system().has_value(), "start standalone runtime");
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

    require(runtime.pause_midlet(phoneme::AppId {1}).has_value(),
            "execute pauseApp");
    require(runtime.resume_midlet(phoneme::AppId {1}).has_value(),
            "execute startApp on resume");
    require(runtime.destroy_midlet(phoneme::AppId {1}).has_value(),
            "execute destroyApp");
    require(runtime.app_state(phoneme::AppId {1}) ==
                phoneme::runtime::AppState::destroyed,
            "MIDlet enters destroyed state");

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
    const std::string fixture_jar = argv[1];
    const char* filter = std::getenv("PHONEME_TEST_FILTER");
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
    test_archive_and_classfile(fixture_jar);
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
    test_machine_invocation(fixture_jar);
    test_machine_exceptions(fixture_jar);
    test_machine_extended_opcodes(fixture_jar);
    test_machine_rms(fixture_jar);
    test_machine_push(fixture_jar);
    test_runtime_push_queue(fixture_jar);
    test_machine_network(fixture_jar);
    test_machine_media(fixture_jar);
    test_machine_graphics(fixture_jar);
    test_machine_gc_roots(fixture_jar);
    test_runtime_lcdui(fixture_jar);
    test_runtime_canvas(fixture_jar);
    test_runtime_lifecycle(fixture_jar);
    test_runtime_failure_isolation(fixture_jar);
    test_framebuffer_sizes();
    std::cout << "Standalone Core tests passed\n";
    return 0;
}
