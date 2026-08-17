#include <iostream>
#include <set>
#include <string>
#include <string_view>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/ClassLayout.hpp"
#include "phoneme/vm/ClassRepository.hpp"

namespace {

[[nodiscard]] bool ends_with(std::string_view value,
                             std::string_view suffix) noexcept {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

void add_class_reference(std::set<std::string>& references,
                         std::string_view class_name) {
    if (class_name.empty()) return;
    if (class_name.front() != '[') {
        references.emplace("C\t" + std::string(class_name));
        return;
    }

    // CONSTANT_Class may name an array descriptor. The array class itself is
    // synthesized by the VM; only a reference component needs linkage.
    const auto object = class_name.find('L');
    const auto terminator = class_name.rfind(';');
    if (object != std::string_view::npos &&
        terminator != std::string_view::npos && terminator > object + 1U) {
        references.emplace(
            "C\t" + std::string(class_name.substr(
                object + 1U, terminator - object - 1U)));
    }
}

[[nodiscard]] std::set<std::string> collect_references(
    const phoneme::archive::ZipArchive& archive) {
    std::set<std::string> references;
    for (const auto& entry : archive.entries()) {
        if (!ends_with(entry.name, ".class")) continue;
        auto bytes = archive.read(entry);
        if (!bytes) {
            std::cerr << "unable to read " << entry.name << ": "
                      << bytes.error().message << '\n';
            continue;
        }
        auto parsed = phoneme::classfile::ClassFile::parse(*bytes);
        if (!parsed) {
            std::cerr << "unable to parse " << entry.name << ": "
                      << parsed.error().message << '\n';
            continue;
        }

        const auto& constants = parsed->constants();
        for (std::size_t index = 1U; index < constants.size(); ++index) {
            const auto kind = constants[index].kind;
            const auto cp_index = static_cast<phoneme::u16>(index);
            if (kind == phoneme::classfile::ConstantKind::class_ref) {
                auto class_name = parsed->class_name_constant(cp_index);
                if (class_name) add_class_reference(references, *class_name);
                continue;
            }
            if (kind != phoneme::classfile::ConstantKind::field_ref &&
                kind != phoneme::classfile::ConstantKind::method_ref &&
                kind != phoneme::classfile::ConstantKind::interface_method_ref) {
                continue;
            }

            auto member = parsed->member_reference(cp_index);
            if (!member) {
                std::cerr << "unable to decode member reference in "
                          << entry.name << " cp#" << index << ": "
                          << member.error().message << '\n';
                continue;
            }
            add_class_reference(references, member->owner);
            const char prefix =
                kind == phoneme::classfile::ConstantKind::field_ref ? 'F' : 'M';
            references.emplace(
                std::string(1U, prefix) + "\t" + member->owner + "\t" +
                member->name + "\t" + member->descriptor);
        }
    }
    return references;
}

[[nodiscard]] std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> parts;
    while (true) {
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos) {
            parts.push_back(line);
            break;
        }
        parts.push_back(line.substr(0U, tab));
        line.remove_prefix(tab + 1U);
    }
    return parts;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: JarLinkageAudit <application.jar>\n";
        return 64;
    }

    phoneme::archive::ZipLimits limits;
    limits.maximum_entries = 50'000U;
    limits.maximum_archive_bytes = 1024ULL * 1024ULL * 1024ULL;
    limits.maximum_total_uncompressed_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    auto archive = phoneme::archive::ZipArchive::open(argv[1], limits);
    if (!archive) {
        std::cerr << "unable to open JAR: " << archive.error().message << '\n';
        return 1;
    }

    phoneme::vm::ClassRepository classes;
    auto added = classes.add_archive(argv[1]);
    if (!added) {
        std::cerr << "unable to add JAR: " << added.error().message << '\n';
        return 1;
    }
    phoneme::vm::ClassStateRegistry states(classes);

    const auto references = collect_references(*archive);
    std::size_t checked = 0U;
    std::size_t classes_checked = 0U;
    std::size_t methods_checked = 0U;
    std::size_t fields_checked = 0U;
    std::size_t missing = 0U;

    for (const std::string& line : references) {
        const auto parts = split_tabs(line);
        if (parts.empty()) continue;

        bool present = false;
        std::string detail;
        if (parts[0] == "C" && parts.size() == 2U) {
            ++classes_checked;
            auto loaded = classes.load(parts[1]);
            present = loaded.has_value();
            if (!present) detail = loaded.error().message;
        } else if (parts[0] == "M" && parts.size() == 4U) {
            ++methods_checked;
            auto resolved = parts[2] == "<init>"
                ? classes.resolve_declared_method(parts[1], parts[2], parts[3])
                : classes.resolve_method(parts[1], parts[2], parts[3]);
            present = resolved.has_value();
            if (!present) detail = resolved.error().message;
        } else if (parts[0] == "F" && parts.size() == 4U) {
            ++fields_checked;
            auto static_field = states.resolve_field(
                parts[1], parts[2], parts[3], true);
            if (static_field) {
                present = true;
            } else {
                auto instance_field = states.resolve_field(
                    parts[1], parts[2], parts[3], false);
                present = instance_field.has_value();
                if (!present) detail = instance_field.error().message;
            }
        } else {
            std::cerr << "invalid reference: " << line << '\n';
            return 65;
        }

        ++checked;
        if (present) continue;
        ++missing;
        std::cout << line;
        if (!detail.empty()) std::cout << '\t' << detail;
        std::cout << '\n';
    }

    std::cerr << "checked=" << checked
              << " classes=" << classes_checked
              << " methods=" << methods_checked
              << " fields=" << fields_checked
              << " missing=" << missing << '\n';
    return missing == 0U ? 0 : 2;
}
