#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"

namespace {

constexpr std::uint16_t kPublic = 0x0001U;
constexpr std::uint16_t kProtected = 0x0004U;
constexpr std::uint16_t kStatic = 0x0008U;
constexpr std::uint16_t kFinal = 0x0010U;
constexpr std::uint16_t kSynchronized = 0x0020U;
constexpr std::uint16_t kAbstract = 0x0400U;
constexpr std::uint16_t kRelevantMethodFlags =
    kPublic | kProtected | kStatic | kFinal | kSynchronized | kAbstract;
constexpr std::uint16_t kRelevantFieldFlags =
    kPublic | kProtected | kStatic | kFinal;

struct Gap final {
    std::string owner;
    std::string member;
    std::string detail;
};

[[nodiscard]] bool exported(std::uint16_t access_flags) noexcept {
    return (access_flags & (kPublic | kProtected)) != 0U;
}

[[nodiscard]] std::string visibility(std::uint16_t access_flags) {
    if ((access_flags & kPublic) != 0U) return "public";
    if ((access_flags & kProtected) != 0U) return "protected";
    return "package/private";
}

[[nodiscard]] std::string category(std::string_view class_name,
                                   std::uint16_t access_flags) {
    if ((access_flags & kPublic) == 0U) {
        return "non-public implementation class";
    }
    if (class_name.starts_with("java/") ||
        class_name.starts_with("javax/")) {
        return "public API";
    }
    return "phoneME internal public class";
}

[[nodiscard]] std::string escape_markdown(std::string text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char character : text) {
        if (character == '|') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

[[nodiscard]] std::vector<std::string> read_class_names(
    const std::string& path) {
    std::ifstream input(path);
    std::vector<std::string> names;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) names.push_back(std::move(line));
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: ApiSurfaceAudit <reference-classes.zip> "
                     "<class-list.txt> <report.md>\n";
        return 64;
    }

    phoneme::archive::ZipLimits audit_limits;
    audit_limits.maximum_entries = 50'000U;
    audit_limits.maximum_total_uncompressed_bytes =
        1024ULL * 1024ULL * 1024ULL;
    auto reference = phoneme::archive::ZipArchive::open(argv[1], audit_limits);
    if (!reference) {
        std::cerr << "Unable to load reference archive: "
                  << reference.error().message << '\n';
        return 1;
    }

    const auto class_names = read_class_names(argv[2]);
    if (class_names.empty()) {
        std::cerr << "Reference class list is empty\n";
        return 1;
    }

    std::vector<Gap> missing_classes;
    std::vector<Gap> hierarchy_gaps;
    std::vector<Gap> method_gaps;
    std::vector<Gap> method_access_gaps;
    std::vector<Gap> field_gaps;
    std::vector<Gap> field_access_gaps;
    std::vector<Gap> load_errors;
    std::size_t compared_classes = 0U;

    for (const std::string& class_name : class_names) {
        const std::string entry_name = class_name + ".class";
        const auto* entry = reference->find(entry_name);
        if (entry == nullptr) {
            load_errors.push_back({class_name, "class", "archive entry missing"});
            continue;
        }
        auto bytes = reference->read(*entry);
        if (!bytes) {
            load_errors.push_back({class_name, "class", bytes.error().message});
            continue;
        }
        auto expected = phoneme::classfile::ClassFile::parse(*bytes);
        if (!expected) {
            load_errors.push_back({class_name, "class", expected.error().message});
            continue;
        }

        if (!phoneme::vm::is_builtin_class(class_name)) {
            missing_classes.push_back({
                class_name, "class",
                category(class_name, expected->access_flags()),
            });
            continue;
        }
        auto actual = phoneme::vm::load_builtin_class(class_name);
        if (!actual) {
            load_errors.push_back({class_name, "builtin", actual.error().message});
            continue;
        }
        ++compared_classes;

        if (expected->super_name() != (*actual)->super_name()) {
            hierarchy_gaps.push_back({
                class_name,
                "super",
                "expected `" + expected->super_name() + "`, got `" +
                    (*actual)->super_name() + "`",
            });
        }
        for (const std::string& expected_interface : expected->interfaces()) {
            const auto& actual_interfaces = (*actual)->interfaces();
            if (std::find(actual_interfaces.begin(), actual_interfaces.end(),
                          expected_interface) == actual_interfaces.end()) {
                hierarchy_gaps.push_back({
                    class_name,
                    "interface",
                    "missing `" + expected_interface + "`",
                });
            }
        }

        for (const auto& expected_method : expected->methods()) {
            if (!exported(expected_method.access_flags) ||
                expected_method.name == "<clinit>") {
                continue;
            }
            const auto* actual_method = (*actual)->find_method(
                expected_method.name, expected_method.descriptor);
            const std::string member = expected_method.name +
                                       expected_method.descriptor;
            if (actual_method == nullptr) {
                method_gaps.push_back({class_name, member,
                                       visibility(expected_method.access_flags)});
                continue;
            }
            const std::uint16_t expected_flags =
                expected_method.access_flags & kRelevantMethodFlags;
            const std::uint16_t actual_flags =
                actual_method->access_flags & kRelevantMethodFlags;
            if ((expected_flags & (kPublic | kProtected)) !=
                (actual_flags & (kPublic | kProtected)) ||
                (expected_flags & (kStatic | kFinal | kAbstract)) !=
                (actual_flags & (kStatic | kFinal | kAbstract))) {
                method_access_gaps.push_back({
                    class_name,
                    member,
                    "expected flags " + std::to_string(expected_flags) +
                        ", got " + std::to_string(actual_flags),
                });
            }
        }

        for (const auto& expected_field : expected->fields()) {
            if (!exported(expected_field.access_flags)) continue;
            const auto& actual_fields = (*actual)->fields();
            const auto found = std::find_if(
                actual_fields.begin(), actual_fields.end(),
                [&](const auto& candidate) {
                    return candidate.name == expected_field.name &&
                           candidate.descriptor == expected_field.descriptor;
                });
            const std::string member = expected_field.name + ":" +
                                       expected_field.descriptor;
            if (found == actual_fields.end()) {
                field_gaps.push_back({class_name, member,
                                      visibility(expected_field.access_flags)});
                continue;
            }
            const std::uint16_t expected_flags =
                expected_field.access_flags & kRelevantFieldFlags;
            const std::uint16_t actual_flags =
                found->access_flags & kRelevantFieldFlags;
            if (expected_flags != actual_flags) {
                field_access_gaps.push_back({
                    class_name,
                    member,
                    "expected flags " + std::to_string(expected_flags) +
                        ", got " + std::to_string(actual_flags),
                });
            }
        }
    }

    const auto sort_gaps = [](std::vector<Gap>& gaps) {
        std::sort(gaps.begin(), gaps.end(), [](const Gap& left, const Gap& right) {
            if (left.owner != right.owner) return left.owner < right.owner;
            return left.member < right.member;
        });
    };
    sort_gaps(missing_classes);
    sort_gaps(hierarchy_gaps);
    sort_gaps(method_gaps);
    sort_gaps(method_access_gaps);
    sort_gaps(field_gaps);
    sort_gaps(field_access_gaps);
    sort_gaps(load_errors);

    const auto missing_category_count =
        [&missing_classes](std::string_view expected) {
            return static_cast<std::size_t>(std::count_if(
                missing_classes.begin(), missing_classes.end(),
                [expected](const Gap& gap) {
                    return gap.detail == expected;
                }));
        };
    const std::size_t missing_standard_api =
        missing_category_count("public API");
    const std::size_t missing_internal_public =
        missing_category_count("phoneME internal public class");
    const std::size_t missing_non_public =
        missing_category_count("non-public implementation class");

    std::ofstream report(argv[3]);
    if (!report) {
        std::cerr << "Unable to create report: " << argv[3] << '\n';
        return 1;
    }

    report << "# phoneME C++ API Surface Audit\n\n";
    report << "Reference: `" << escape_markdown(argv[1]) << "`\n\n";
    report << "| Metric | Count |\n| --- | ---: |\n";
    report << "| Reference classes | " << class_names.size() << " |\n";
    report << "| Compared builtin classes | " << compared_classes << " |\n";
    report << "| Missing classes (all phoneME internals included) | "
           << missing_classes.size() << " |\n";
    report << "| Missing application-visible `java`/`javax` API classes | "
           << missing_standard_api << " |\n";
    report << "| Missing phoneME internal public classes | "
           << missing_internal_public << " |\n";
    report << "| Missing non-public implementation classes | "
           << missing_non_public << " |\n";
    report << "| Missing exported methods | " << method_gaps.size() << " |\n";
    report << "| Method access/shape mismatches | "
           << method_access_gaps.size() << " |\n";
    report << "| Missing exported fields | " << field_gaps.size() << " |\n";
    report << "| Field access/shape mismatches | "
           << field_access_gaps.size() << " |\n";
    report << "| Hierarchy mismatches | " << hierarchy_gaps.size() << " |\n";
    report << "| Load errors | " << load_errors.size() << " |\n\n";

    const auto write_section = [&report](std::string_view title,
                                         const std::vector<Gap>& gaps) {
        report << "## " << title << "\n\n";
        if (gaps.empty()) {
            report << "None.\n\n";
            return;
        }
        report << "| Class | Member | Detail |\n| --- | --- | --- |\n";
        for (const Gap& gap : gaps) {
            report << "| `" << escape_markdown(gap.owner) << "` | `"
                   << escape_markdown(gap.member) << "` | "
                   << escape_markdown(gap.detail) << " |\n";
        }
        report << '\n';
    };

    write_section("Missing classes", missing_classes);
    write_section("Missing exported methods", method_gaps);
    write_section("Method access or shape mismatches", method_access_gaps);
    write_section("Missing exported fields", field_gaps);
    write_section("Field access or shape mismatches", field_access_gaps);
    write_section("Hierarchy mismatches", hierarchy_gaps);
    write_section("Load errors", load_errors);

    std::cout << "API surface report: " << argv[3] << '\n';
    std::cout << "Missing classes: " << missing_classes.size()
              << ", methods: " << method_gaps.size()
              << ", fields: " << field_gaps.size() << '\n';
    return load_errors.empty() ? 0 : 2;
}
