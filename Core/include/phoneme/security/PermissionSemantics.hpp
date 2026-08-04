#pragma once

// Header-only CLDC 1.1.1 permission semantics with no VM dependency, so the
// wildcard / actions / collection / hashcode behaviour is unit-testable without
// linking the interpreter. SecurityNatives.cpp wires these onto Java objects.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace phoneme::security {

constexpr int kActionNone {0};
constexpr int kActionRead {1};
constexpr int kActionWrite {2};
constexpr int kActionAll {kActionRead | kActionWrite};

[[nodiscard]] inline int parse_actions(std::string_view actions) {
    if (actions.empty()) {
        return kActionNone;
    }
    const auto equals_ignore_case = [](std::string_view token,
                                       std::string_view literal) {
        if (token.size() != literal.size()) {
            return false;
        }
        for (std::size_t index {0}; index < token.size(); ++index) {
            const auto lower = [](char value) {
                return (value >= 'A' && value <= 'Z')
                           ? static_cast<char>(value + ('a' - 'A'))
                           : value;
            };
            if (lower(token[index]) != literal[index]) {
                return false;
            }
        }
        return true;
    };

    int mask {kActionNone};
    std::size_t begin {0};
    while (true) {
        const std::size_t comma = actions.find(',', begin);
        const std::string_view token = (comma == std::string_view::npos)
                                           ? actions.substr(begin)
                                           : actions.substr(begin, comma - begin);
        if (equals_ignore_case(token, "read")) {
            mask |= kActionRead;
        } else if (equals_ignore_case(token, "write")) {
            mask |= kActionWrite;
        } else {
            return -1;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1U;
    }
    return mask;
}

[[nodiscard]] inline std::string format_actions(int mask) {
    const bool read = (mask & kActionRead) != 0;
    const bool write = (mask & kActionWrite) != 0;
    if (read && write) {
        return "read,write";
    }
    if (read) {
        return "read";
    }
    if (write) {
        return "write";
    }
    return {};
}

struct BasicName {
    std::string_view path;
    bool wildcard {false};
};

[[nodiscard]] inline BasicName parse_basic_name(std::string_view name) {
    if (name.empty()) {
        return BasicName {.path = std::string_view {}, .wildcard = false};
    }
    const char last = name.back();
    const bool is_wildcard =
        last == '*' && (name.size() == 1U || name[name.size() - 2U] == '.');
    if (!is_wildcard) {
        return BasicName {.path = name, .wildcard = false};
    }
    if (name.size() == 1U) {
        return BasicName {.path = std::string_view {}, .wildcard = true};
    }
    return BasicName {.path = name.substr(0U, name.size() - 1U), .wildcard = true};
}

[[nodiscard]] inline bool basic_implies_name(std::string_view holder,
                                             std::string_view requested) {
    const BasicName this_name = parse_basic_name(holder);
    const BasicName that_name = parse_basic_name(requested);
    if (this_name.wildcard) {
        if (that_name.wildcard) {
            return that_name.path.starts_with(this_name.path);
        }
        return that_name.path.size() > this_name.path.size() &&
               that_name.path.starts_with(this_name.path);
    }
    if (that_name.wildcard) {
        return false;
    }
    return this_name.path == that_name.path;
}

[[nodiscard]] inline bool property_implies(std::string_view holder_name,
                                            int holder_mask,
                                            std::string_view requested_name,
                                            int requested_mask) {
    return ((holder_mask & requested_mask) == requested_mask) &&
           basic_implies_name(holder_name, requested_name);
}

[[nodiscard]] inline std::int32_t java_string_hashcode(std::u16string_view text) {
    std::uint32_t hash {0};
    for (char16_t unit : text) {
        hash = hash * 31U + static_cast<std::uint32_t>(unit);
    }
    return static_cast<std::int32_t>(hash);
}

struct PermissionEntry {
    std::string name;
    int mask {kActionNone};
};

[[nodiscard]] inline bool basic_collection_implies(
    std::span<const PermissionEntry> entries,
    std::string_view requested_name) {
    for (const PermissionEntry& entry : entries) {
        if (basic_implies_name(entry.name, requested_name)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool property_collection_implies(
    std::span<const PermissionEntry> entries,
    std::string_view requested_name,
    int requested_mask) {
    int effective {kActionNone};
    for (const PermissionEntry& entry : entries) {
        if (basic_implies_name(entry.name, requested_name)) {
            effective |= entry.mask;
        }
    }
    return (effective & requested_mask) == requested_mask;
}

} // namespace phoneme::security
