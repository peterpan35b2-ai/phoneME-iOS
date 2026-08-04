#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"

namespace {

enum class ResultKind : char {
    integer = 'I',
    long_integer = 'J',
    string = 'S',
    exception = 'E',
};

struct CaseSpec final {
    std::string_view id;
    std::string_view method;
    std::string_view descriptor;
    ResultKind kind;
};

struct OracleRecord final {
    const CaseSpec* spec {nullptr};
    std::string expected;
};

constexpr CaseSpec kCases[] {
    {"int-overflow", "intOverflow", "()I", ResultKind::integer},
    {"int-div-rem", "intDivisionRemainder", "()I", ResultKind::integer},
    {"int-shift-mask", "intShiftMasking", "()I", ResultKind::integer},
    {"long-overflow", "longOverflow", "()J", ResultKind::long_integer},
    {"long-shift-mask", "longShiftMasking", "()J", ResultKind::long_integer},
    {"float-nan-bits", "floatNanBits", "()I", ResultKind::integer},
    {"float-negative-zero", "floatNegativeZeroBits", "()I", ResultKind::integer},
    {"double-nan-bits", "doubleNanBits", "()J", ResultKind::long_integer},
    {"double-negative-zero", "doubleNegativeZeroBits", "()J", ResultKind::long_integer},
    {"floating-conversions", "floatingConversions", "()I", ResultKind::integer},
    {"double-long-conversions", "doubleToLongConversions", "()J", ResultKind::long_integer},
    {"dense-switch", "denseSwitch", "()I", ResultKind::integer},
    {"sparse-switch", "sparseSwitch", "()I", ResultKind::integer},
    {"primitive-arrays", "primitiveArrays", "()I", ResultKind::integer},
    {"multi-array", "multiArray", "()I", ResultKind::integer},
    {"reference-arrays-casts", "referenceArraysAndCasts", "()I", ResultKind::integer},
    {"exception-finally", "exceptionAndFinally", "()I", ResultKind::integer},
    {"dispatch", "dispatch", "()I", ResultKind::integer},
    {"class-initialization", "classInitialization", "()I", ResultKind::integer},
    {"unicode-string", "unicodeString", "()I", ResultKind::integer},
    {"string-operations", "stringOperations", "()I", ResultKind::integer},
    {"string-buffer", "stringBufferOperations", "()I", ResultKind::integer},
    {"vector", "vectorOperations", "()I", ResultKind::integer},
    {"hashtable", "hashtableOperations", "()I", ResultKind::integer},
    {"tokenizer", "tokenizerOperations", "()I", ResultKind::integer},
    {"tokenizer-delimiter-change", "tokenizerDelimiterChange", "()I", ResultKind::integer},
    {"tokenizer-return-delimiters", "tokenizerReturnDelimiters", "()I", ResultKind::integer},
    {"tokenizer-exhaustion", "uncaughtTokenizerExhaustion", "()V", ResultKind::exception},
    {"data-stream", "dataStreamRoundTrip", "()J", ResultKind::long_integer},
    {"random", "randomSequence", "()I", ResultKind::integer},
    {"system-arraycopy", "systemArrayCopy", "()I", ResultKind::integer},
    {"wrapper-semantics", "wrapperSemantics", "()I", ResultKind::integer},
    {"math-semantics", "mathSemantics", "()J", ResultKind::long_integer},
    {"stack", "stackOperations", "()I", ResultKind::integer},
    {"enumeration", "enumerationOperations", "()I", ResultKind::integer},
    {"date", "dateOperations", "()J", ResultKind::long_integer},
    {"calendar-utc", "calendarUtcOperations", "()I", ResultKind::integer},
    {"timezone", "timeZoneOperations", "()I", ResultKind::integer},
    {"modified-utf", "modifiedUtfRoundTrip", "()I", ResultKind::integer},
    {"reader-writer", "readerWriterRoundTrip", "()I", ResultKind::integer},
    {"class-semantics", "classSemantics", "()I", ResultKind::integer},
    {"thread-runnable-join", "runnableThreadJoin", "()I", ResultKind::integer},
    {"thread-synchronized", "synchronizedThreadCounters", "()I", ResultKind::integer},
    {"openjdk-remaining-surface", "openJdkRemainingSurfaceTrace", "()Ljava/lang/String;", ResultKind::string},
    {"exception-constructors", "exceptionConstructorSurfaceTrace", "()Ljava/lang/String;", ResultKind::string},
    {"print-writer-surface", "printWriterSurfaceTrace", "()Ljava/lang/String;", ResultKind::string},
    {"reader-input-surface", "readerInputSurfaceTrace", "()Ljava/lang/String;", ResultKind::string},
    {"throwable-thread-permission-file", "throwableThreadPermissionFileTrace", "()Ljava/lang/String;", ResultKind::string},
    {"legacy-util-full", "legacyUtilFullTrace", "()Ljava/lang/String;", ResultKind::string},
    {"local-time", "localTimeTrace", "()Ljava/lang/String;", ResultKind::string},
    {"wrapper-full", "wrapperFullTrace", "()Ljava/lang/String;", ResultKind::string},
    {"math-full", "mathFullTrace", "()Ljava/lang/String;", ResultKind::string},
    {"string-api", "stringApiTrace", "()Ljava/lang/String;", ResultKind::string},
    {"string-builder-api", "stringBuilderTrace", "()Ljava/lang/String;", ResultKind::string},
    {"string-buffer-api", "stringBufferExtendedTrace", "()Ljava/lang/String;", ResultKind::string},
    {"headless-collections", "headlessCollectionsTrace", "()Ljava/lang/String;", ResultKind::string},
    {"headless-arrays", "headlessArraysTrace", "()Ljava/lang/String;", ResultKind::string},
    {"headless-base64-objects", "headlessBase64ObjectsTrace", "()Ljava/lang/String;", ResultKind::string},
    {"headless-io", "headlessIoTrace", "()Ljava/lang/String;", ResultKind::string},
    {"exception-npe", "uncaughtNullPointer", "()V", ResultKind::exception},
    {"exception-array-bounds", "uncaughtArrayBounds", "()V", ResultKind::exception},
    {"exception-class-cast", "uncaughtClassCast", "()V", ResultKind::exception},
    {"exception-arithmetic", "uncaughtArithmetic", "()V", ResultKind::exception},
    {"broken-init-first", "brokenInitializationFirst", "()I", ResultKind::exception},
    {"broken-init-second", "brokenInitializationSecond", "()I", ResultKind::exception},
};

[[nodiscard]] std::optional<std::int64_t> parse_i64(std::string_view value) {
    std::int64_t parsed = 0;
    const auto conversion = std::from_chars(value.data(),
                                            value.data() + value.size(),
                                            parsed);
    if (conversion.ec != std::errc {} ||
        conversion.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::vector<OracleRecord> read_oracle(
    const std::string& path,
    bool& valid) {
    valid = false;
    std::ifstream input(path);
    if (!input) {
        std::cerr << "Cannot open Java oracle output: " << path << '\n';
        return {};
    }

    std::unordered_map<std::string_view, const CaseSpec*> cases;
    cases.reserve(std::size(kCases));
    for (const auto& spec : kCases) {
        cases.emplace(spec.id, &spec);
    }

    std::unordered_map<std::string_view, bool> seen;
    seen.reserve(std::size(kCases));
    std::vector<OracleRecord> records;
    records.reserve(std::size(kCases));

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const auto first_tab = line.find('\t');
        const auto second_tab = first_tab == std::string::npos
            ? std::string::npos
            : line.find('\t', first_tab + 1U);
        if (first_tab == std::string::npos || second_tab == std::string::npos ||
            second_tab != first_tab + 2U) {
            std::cerr << "Malformed oracle line " << line_number << ": "
                      << line << '\n';
            return {};
        }

        const std::string_view id(line.data(), first_tab);
        const auto found = cases.find(id);
        if (found == cases.end()) {
            std::cerr << "Unknown oracle case at line " << line_number
                      << ": " << id << '\n';
            return {};
        }
        if (seen.contains(found->first)) {
            std::cerr << "Duplicate oracle case: " << id << '\n';
            return {};
        }
        seen.emplace(found->first, true);

        const char kind = line[first_tab + 1U];
        if (kind != static_cast<char>(found->second->kind)) {
            std::cerr << "Oracle kind mismatch for " << id << '\n';
            return {};
        }
        records.push_back(OracleRecord {
            .spec = found->second,
            .expected = line.substr(second_tab + 1U),
        });
    }

    if (records.size() != std::size(kCases)) {
        std::cerr << "Oracle produced " << records.size() << " cases; expected "
                  << std::size(kCases) << '\n';
        return {};
    }
    valid = true;
    return records;
}

[[nodiscard]] bool compare_numeric(const OracleRecord& record,
                                   const phoneme::vm::ExecutionResult& result) {
    if (!result.completed_normally() || !result.return_value.has_value()) {
        std::cerr << "DIFF " << record.spec->id
                  << ": VM did not return normally";
        if (!result.exception_context.empty()) {
            std::cerr << " (" << result.exception_context << ')';
        }
        std::cerr << '\n';
        return false;
    }

    const auto expected = parse_i64(record.expected);
    if (!expected.has_value()) {
        std::cerr << "Invalid numeric oracle value for " << record.spec->id
                  << ": " << record.expected << '\n';
        return false;
    }

    std::optional<std::int64_t> actual;
    if (record.spec->kind == ResultKind::integer) {
        if (const auto value = result.return_value->as_int(); value.has_value()) {
            actual = static_cast<std::int64_t>(*value);
        }
    } else {
        if (const auto value = result.return_value->as_long(); value.has_value()) {
            actual = static_cast<std::int64_t>(*value);
        }
    }

    if (!actual.has_value()) {
        std::cerr << "DIFF " << record.spec->id
                  << ": VM returned the wrong value category\n";
        return false;
    }
    if (*actual != *expected) {
        std::cerr << "DIFF " << record.spec->id << ": Java=" << *expected
                  << " C++VM=" << *actual << '\n';
        return false;
    }
    std::cout << "MATCH " << record.spec->id << " = " << *actual << '\n';
    return true;
}

[[nodiscard]] bool compare_string(const OracleRecord& record,
                                  const phoneme::vm::ExecutionResult& result,
                                  phoneme::vm::Machine& machine) {
    if (!result.completed_normally() || !result.return_value.has_value()) {
        std::cerr << "DIFF " << record.spec->id
                  << ": VM did not return a String normally";
        if (result.throwable.has_value()) {
            const auto throwable_class = machine.heap().class_name(*result.throwable);
            if (throwable_class.has_value()) {
                std::cerr << " [" << *throwable_class << ']';
            }
            const auto message = machine.heap().field(*result.throwable, 0U);
            if (message.has_value()) {
                const auto reference = message->as_reference();
                if (reference.has_value() && !reference->is_null()) {
                    const auto text = machine.heap().string_value(*reference);
                    if (text.has_value()) {
                        std::cerr << " message=";
                        for (const char16_t unit : *text) {
                            std::cerr << static_cast<char>(unit <= 0x7FU ? unit : '?');
                        }
                    }
                }
            }
        }
        std::cerr << '\n';
        return false;
    }
    const auto reference = result.return_value->as_reference();
    if (!reference.has_value() || reference->is_null()) {
        std::cerr << "DIFF " << record.spec->id
                  << ": VM returned a non-String/null value\n";
        return false;
    }
    const auto text = machine.heap().string_value(*reference);
    if (!text.has_value()) {
        std::cerr << "DIFF " << record.spec->id
                  << ": VM String has no payload\n";
        return false;
    }
    std::string actual;
    actual.reserve(text->size());
    for (char16_t unit : *text) {
        if (unit > 0x7FU) {
            std::cerr << "DIFF " << record.spec->id
                      << ": trace contains non-ASCII data\n";
            return false;
        }
        actual.push_back(static_cast<char>(unit));
    }
    if (actual != record.expected) {
        std::cerr << "DIFF " << record.spec->id << ": Java="
                  << record.expected << " C++VM=" << actual << '\n';
        return false;
    }
    std::cout << "MATCH " << record.spec->id << " = " << actual << '\n';
    return true;
}

[[nodiscard]] bool compare_exception(const OracleRecord& record,
                                     const phoneme::vm::ExecutionResult& result,
                                     phoneme::vm::Machine& machine) {
    if (result.completed_normally() || !result.throwable.has_value()) {
        std::cerr << "DIFF " << record.spec->id << ": Java threw "
                  << record.expected << " but C++ VM completed normally\n";
        return false;
    }
    const auto actual = machine.heap().class_name(*result.throwable);
    if (!actual.has_value()) {
        std::cerr << "DIFF " << record.spec->id
                  << ": C++ VM throwable has no valid class\n";
        return false;
    }
    if (*actual != record.expected) {
        std::cerr << "DIFF " << record.spec->id << ": Java="
                  << record.expected << " C++VM=" << *actual << '\n';
        return false;
    }
    std::cout << "MATCH " << record.spec->id << " throws " << *actual << '\n';
    return true;
}

[[nodiscard]] bool write_native_coverage(
    const std::string& path,
    const phoneme::vm::NativeMethodRegistry& registry) {
    std::ofstream output(path);
    if (!output) {
        std::cerr << "Cannot write native coverage: " << path << '\n';
        return false;
    }
    output << "owner\tname\tdescriptor\tinvocations\n";
    for (const auto& entry : registry.invocation_counts()) {
        output << entry.signature.owner << '\t'
               << entry.signature.name << '\t'
               << entry.signature.descriptor << '\t'
               << entry.count << '\n';
    }
    return output.good();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: VmDifferentialHarness FIXTURE.jar ORACLE.tsv "
                     "[NATIVE_COVERAGE.tsv]\n";
        return 2;
    }

    bool oracle_valid = false;
    auto records = read_oracle(argv[2], oracle_valid);
    if (!oracle_valid) {
        return 2;
    }

    phoneme::vm::ClassRepository classes;
    const auto archive = classes.add_archive(argv[1]);
    if (!archive.has_value()) {
        std::cerr << "Cannot load differential fixture JAR: "
                  << archive.error().message << '\n';
        return 2;
    }

    phoneme::vm::Machine machine(classes);
    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto sandbox = std::filesystem::temp_directory_path() /
        ("phoneme-vm-differential-" + std::to_string(unique));
    const auto sandbox_files = sandbox / "files";
    const auto sandbox_temp = sandbox / "tmp";
    std::error_code directory_error;
    std::filesystem::create_directories(sandbox_files, directory_error);
    if (directory_error) {
        std::cerr << "Cannot create differential filesystem sandbox: "
                  << directory_error.message() << '\n';
        return 2;
    }
    std::filesystem::create_directories(sandbox_temp, directory_error);
    if (directory_error) {
        std::cerr << "Cannot create differential temporary directory: "
                  << directory_error.message() << '\n';
        return 2;
    }
    const auto filesystem_configured = machine.configure_filesystem(
        sandbox_files.string(), sandbox_temp.string());
    if (!filesystem_configured) {
        std::cerr << "Cannot configure differential filesystem sandbox: "
                  << filesystem_configured.error().message << '\n';
        return 2;
    }
    std::size_t passed = 0;
    std::size_t failed = 0;
    for (const auto& record : records) {
        const auto invocation = machine.invoke_static(
            "compat/diff/VmDifferentialOps",
            record.spec->method,
            record.spec->descriptor,
            {},
            50'000'000U);
        if (!invocation.has_value()) {
            std::cerr << "DIFF " << record.spec->id << ": VM invocation error: "
                      << invocation.error().message;
            if (!invocation.error().java_exception_class.empty()) {
                std::cerr << " [" << invocation.error().java_exception_class << ']';
            }
            std::cerr << '\n';
            ++failed;
            continue;
        }

        const bool matches = record.spec->kind == ResultKind::exception
            ? compare_exception(record, *invocation, machine)
            : (record.spec->kind == ResultKind::string
                ? compare_string(record, *invocation, machine)
                : compare_numeric(record, *invocation));
        if (matches) {
            ++passed;
        } else {
            ++failed;
        }
    }

    bool coverage_written = true;
    if (argc == 4) {
        coverage_written = write_native_coverage(argv[3], machine.natives());
    }
    machine.shutdown();
    std::error_code cleanup_error;
    std::filesystem::remove_all(sandbox, cleanup_error);
    std::cout << "VM differential summary: " << passed << '/' << records.size()
              << " matched";
    if (failed != 0U) {
        std::cout << ", " << failed << " mismatched";
    }
    std::cout << '\n';
    return failed == 0U && coverage_written ? 0 : 1;
}
