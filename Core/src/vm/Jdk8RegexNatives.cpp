#include "Jdk8CompatNativesParts.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kPatternSourceField = 0U;
constexpr usize kPatternFlagsField = 1U;
constexpr usize kMatcherPatternField = 0U;
constexpr usize kMatcherInputField = 1U;
constexpr usize kMatcherSearchFromField = 2U;
constexpr usize kMatcherGroupsField = 3U;
constexpr usize kMatcherMatchedField = 4U;

struct RegexMatch final {
    usize start {0U};
    usize end {0U};
    std::vector<std::u16string> captures;
};

enum class PatternKind {
    asset_original,
    asset_decoded,
    json_quoted_array,
    bracket_content,
    ui_alt_image,
    ui_image,
    fixed_integer,
    fixed_string,
    generic_integer,
    unsupported,
};

[[nodiscard]] bool regex_space(char16_t value) noexcept {
    return value == u' ' || value == u'\t' || value == u'\n' ||
           value == u'\r' || value == u'\f';
}

[[nodiscard]] usize skip_space(std::u16string_view input, usize cursor) {
    while (cursor < input.size() && regex_space(input[cursor])) ++cursor;
    return cursor;
}

[[nodiscard]] bool ascii_digit(char16_t value) noexcept {
    return value >= u'0' && value <= u'9';
}

[[nodiscard]] bool key_character(char16_t value) noexcept {
    return (value >= u'a' && value <= u'z') || value == u'_';
}

[[nodiscard]] std::u16string normalize_quote_escapes(
    std::u16string_view source) {
    std::u16string normalized;
    normalized.reserve(source.size());
    for (usize index = 0U; index < source.size(); ++index) {
        if (source[index] == u'\\' && index + 1U < source.size() &&
            source[index + 1U] == u'"') {
            normalized.push_back(u'"');
            ++index;
        } else {
            normalized.push_back(source[index]);
        }
    }
    return normalized;
}

[[nodiscard]] PatternKind pattern_kind(std::u16string_view source) {
    if (source == u"img_(\\d+)\\.mid") {
        return PatternKind::asset_original;
    }
    if (source == u"data__img__img_(\\d+)\\.mid\\.png") {
        return PatternKind::asset_decoded;
    }
    if (source == u"\\[\\s*\"((?:\\\\.|[^\"])*)\"\\s*\\]") {
        return PatternKind::json_quoted_array;
    }
    if (source == u"\\[([^\\]]+)\\]") {
        return PatternKind::bracket_content;
    }

    const std::u16string normalized = normalize_quote_escapes(source);
    if (normalized ==
        u"\"alt_image_ref\"\\s*:\\s*\\{\\s*\"id\"\\s*:\\s*(-?\\d+)\\s*,\\s*\"mode\"\\s*:\\s*(-?\\d+)") {
        return PatternKind::ui_alt_image;
    }
    if (normalized ==
        u"\"image_ref\"\\s*:\\s*\\{\\s*\"id\"\\s*:\\s*(-?\\d+)\\s*,\\s*\"mode\"\\s*:\\s*(-?\\d+)") {
        return PatternKind::ui_image;
    }
    if (normalized == u"\"([a-z_]+)\"\\s*:\\s*(-?\\d+)") {
        return PatternKind::generic_integer;
    }
    if (!normalized.empty() && normalized.front() == u'"') {
        const usize closing = normalized.find(u'"', 1U);
        if (closing != std::u16string::npos) {
            const std::u16string_view tail =
                std::u16string_view(normalized).substr(closing + 1U);
            if (tail == u"\\s*:\\s*(-?\\d+)") {
                return PatternKind::fixed_integer;
            }
            if (tail == u"\\s*:\\s*\"([^\"]*)\"") {
                return PatternKind::fixed_string;
            }
        }
    }
    return PatternKind::unsupported;
}

[[nodiscard]] std::u16string fixed_key(std::u16string_view source) {
    const std::u16string normalized = normalize_quote_escapes(source);
    if (normalized.empty() || normalized.front() != u'"') return {};
    const usize closing = normalized.find(u'"', 1U);
    return closing == std::u16string::npos
        ? std::u16string {}
        : normalized.substr(1U, closing - 1U);
}

[[nodiscard]] bool parse_signed_integer(std::u16string_view input,
                                        usize& cursor,
                                        std::u16string& value) {
    const usize start = cursor;
    if (cursor < input.size() && input[cursor] == u'-') ++cursor;
    const usize digits = cursor;
    while (cursor < input.size() && ascii_digit(input[cursor])) ++cursor;
    if (cursor == digits) {
        cursor = start;
        return false;
    }
    value.assign(input.substr(start, cursor - start));
    return true;
}

[[nodiscard]] bool parse_quoted_key(std::u16string_view input,
                                    usize& cursor,
                                    std::u16string& key) {
    if (cursor >= input.size() || input[cursor] != u'"') return false;
    const usize start = ++cursor;
    while (cursor < input.size() && input[cursor] != u'"') {
        if (!key_character(input[cursor])) return false;
        ++cursor;
    }
    if (cursor >= input.size() || cursor == start) return false;
    key.assign(input.substr(start, cursor - start));
    ++cursor;
    return true;
}

[[nodiscard]] bool parse_named_integer(std::u16string_view input,
                                       usize& cursor,
                                       std::u16string_view expected_key,
                                       std::u16string& value) {
    cursor = skip_space(input, cursor);
    std::u16string key;
    if (!parse_quoted_key(input, cursor, key) || key != expected_key) {
        return false;
    }
    cursor = skip_space(input, cursor);
    if (cursor >= input.size() || input[cursor] != u':') return false;
    cursor = skip_space(input, cursor + 1U);
    return parse_signed_integer(input, cursor, value);
}

[[nodiscard]] std::optional<RegexMatch> find_asset(
    std::u16string_view input,
    usize from,
    std::u16string_view prefix,
    std::u16string_view suffix) {
    usize cursor = from;
    while (cursor < input.size()) {
        const usize start = input.find(prefix, cursor);
        if (start == std::u16string::npos) return std::nullopt;
        usize digits_end = start + prefix.size();
        const usize digits_start = digits_end;
        while (digits_end < input.size() && ascii_digit(input[digits_end])) {
            ++digits_end;
        }
        if (digits_end > digits_start &&
            input.substr(digits_end, suffix.size()) == suffix) {
            RegexMatch result;
            result.start = start;
            result.end = digits_end + suffix.size();
            result.captures.emplace_back(
                input.substr(digits_start, digits_end - digits_start));
            return result;
        }
        cursor = start + 1U;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RegexMatch> find_json_quoted_array(
    std::u16string_view input,
    usize from) {
    usize cursor = from;
    while (cursor < input.size()) {
        const usize start = input.find(u'[', cursor);
        if (start == std::u16string::npos) return std::nullopt;
        usize position = skip_space(input, start + 1U);
        if (position >= input.size() || input[position] != u'"') {
            cursor = start + 1U;
            continue;
        }
        const usize capture_start = ++position;
        bool escaped = false;
        while (position < input.size()) {
            const char16_t value = input[position];
            if (!escaped && value == u'"') break;
            if (!escaped && value == u'\\') escaped = true;
            else escaped = false;
            ++position;
        }
        if (position >= input.size()) return std::nullopt;
        const usize capture_end = position++;
        position = skip_space(input, position);
        if (position < input.size() && input[position] == u']') {
            RegexMatch result;
            result.start = start;
            result.end = position + 1U;
            result.captures.emplace_back(
                input.substr(capture_start, capture_end - capture_start));
            return result;
        }
        cursor = start + 1U;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RegexMatch> find_bracket_content(
    std::u16string_view input,
    usize from) {
    usize cursor = from;
    while (cursor < input.size()) {
        const usize start = input.find(u'[', cursor);
        if (start == std::u16string::npos) return std::nullopt;
        const usize end = input.find(u']', start + 1U);
        if (end == std::u16string::npos) return std::nullopt;
        if (end > start + 1U) {
            RegexMatch result;
            result.start = start;
            result.end = end + 1U;
            result.captures.emplace_back(
                input.substr(start + 1U, end - start - 1U));
            return result;
        }
        cursor = end + 1U;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RegexMatch> find_fixed_json_value(
    std::u16string_view input,
    usize from,
    std::u16string_view key,
    bool string_value) {
    std::u16string quoted_key(u"\"");
    quoted_key.append(key);
    quoted_key.push_back(u'"');
    usize cursor = from;
    while (cursor < input.size()) {
        const usize start = input.find(quoted_key, cursor);
        if (start == std::u16string::npos) return std::nullopt;
        usize position = skip_space(input, start + quoted_key.size());
        if (position >= input.size() || input[position] != u':') {
            cursor = start + 1U;
            continue;
        }
        position = skip_space(input, position + 1U);
        RegexMatch result;
        result.start = start;
        if (string_value) {
            if (position >= input.size() || input[position] != u'"') {
                cursor = start + 1U;
                continue;
            }
            const usize capture_start = ++position;
            while (position < input.size() && input[position] != u'"') {
                ++position;
            }
            if (position >= input.size()) return std::nullopt;
            result.captures.emplace_back(
                input.substr(capture_start, position - capture_start));
            result.end = position + 1U;
            return result;
        }
        std::u16string integer;
        if (!parse_signed_integer(input, position, integer)) {
            cursor = start + 1U;
            continue;
        }
        result.captures.push_back(std::move(integer));
        result.end = position;
        return result;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RegexMatch> find_generic_integer(
    std::u16string_view input,
    usize from) {
    usize cursor = from;
    while (cursor < input.size()) {
        const usize start = input.find(u'"', cursor);
        if (start == std::u16string::npos) return std::nullopt;
        usize position = start;
        std::u16string key;
        if (!parse_quoted_key(input, position, key)) {
            cursor = start + 1U;
            continue;
        }
        position = skip_space(input, position);
        if (position >= input.size() || input[position] != u':') {
            cursor = start + 1U;
            continue;
        }
        position = skip_space(input, position + 1U);
        std::u16string integer;
        if (!parse_signed_integer(input, position, integer)) {
            cursor = start + 1U;
            continue;
        }
        RegexMatch result;
        result.start = start;
        result.end = position;
        result.captures.push_back(std::move(key));
        result.captures.push_back(std::move(integer));
        return result;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RegexMatch> find_ui_image(
    std::u16string_view input,
    usize from,
    std::u16string_view root_key) {
    std::u16string quoted_root(u"\"");
    quoted_root.append(root_key);
    quoted_root.push_back(u'"');
    usize cursor = from;
    while (cursor < input.size()) {
        const usize start = input.find(quoted_root, cursor);
        if (start == std::u16string::npos) return std::nullopt;
        usize position = skip_space(input, start + quoted_root.size());
        if (position >= input.size() || input[position] != u':') {
            cursor = start + 1U;
            continue;
        }
        position = skip_space(input, position + 1U);
        if (position >= input.size() || input[position] != u'{') {
            cursor = start + 1U;
            continue;
        }
        ++position;
        std::u16string id;
        if (!parse_named_integer(input, position, u"id", id)) {
            cursor = start + 1U;
            continue;
        }
        position = skip_space(input, position);
        if (position >= input.size() || input[position] != u',') {
            cursor = start + 1U;
            continue;
        }
        ++position;
        std::u16string mode;
        if (!parse_named_integer(input, position, u"mode", mode)) {
            cursor = start + 1U;
            continue;
        }
        RegexMatch result;
        result.start = start;
        result.end = position;
        result.captures.push_back(std::move(id));
        result.captures.push_back(std::move(mode));
        return result;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RegexMatch> find_next(
    std::u16string_view source,
    std::u16string_view input,
    usize from) {
    const PatternKind kind = pattern_kind(source);
    switch (kind) {
    case PatternKind::asset_original:
        return find_asset(input, from, u"img_", u".mid");
    case PatternKind::asset_decoded:
        return find_asset(input, from, u"data__img__img_", u".mid.png");
    case PatternKind::json_quoted_array:
        return find_json_quoted_array(input, from);
    case PatternKind::bracket_content:
        return find_bracket_content(input, from);
    case PatternKind::ui_alt_image:
        return find_ui_image(input, from, u"alt_image_ref");
    case PatternKind::ui_image:
        return find_ui_image(input, from, u"image_ref");
    case PatternKind::fixed_integer:
        return find_fixed_json_value(input, from, fixed_key(source), false);
    case PatternKind::fixed_string:
        return find_fixed_json_value(input, from, fixed_key(source), true);
    case PatternKind::generic_integer:
        return find_generic_integer(input, from);
    case PatternKind::unsupported:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] Result<ObjectRef> matcher_input_string(
    Machine& machine,
    ObjectRef input) {
    auto is_string = machine.object_is_instance(input, "java/lang/String");
    if (!is_string) return std::unexpected(is_string.error());
    if (*is_string) return input;
    auto converted = invoke_checked(machine, input, "java/lang/Object",
                                    "toString", "()Ljava/lang/String;");
    if (!converted) return std::unexpected(converted.error());
    if (!converted->has_value()) {
        return fail(ErrorCode::internal_error,
                    "CharSequence.toString returned no value");
    }
    return converted->value().as_reference();
}

[[nodiscard]] Result<ObjectRef> build_group_array(
    Machine& machine,
    std::u16string_view input,
    const RegexMatch& match) {
    auto groups = allocate_object_array(machine, match.captures.size() + 1U);
    if (!groups) return std::unexpected(groups.error());
    auto groups_root = machine.pin_native_root(*groups);
    if (!groups_root) return std::unexpected(groups_root.error());
    std::vector<std::u16string> all_groups;
    all_groups.reserve(match.captures.size() + 1U);
    all_groups.emplace_back(input.substr(match.start, match.end - match.start));
    all_groups.insert(all_groups.end(), match.captures.begin(),
                      match.captures.end());
    for (usize index = 0U; index < all_groups.size(); ++index) {
        auto string = create_string(machine, std::move(all_groups[index]));
        if (!string) return std::unexpected(string.error());
        auto stored = machine.heap().set_element(
            *groups, index, Value::from_reference(*string));
        if (!stored) return std::unexpected(stored.error());
    }
    return *groups;
}

void register_pattern(NativeMethodRegistry& registry) {
    add(registry, "java/util/regex/Pattern", "<init>",
        "(Ljava/lang/String;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto pattern = receiver(arguments);
            auto source = reference_argument(arguments, 1U);
            auto flags = int_argument(arguments, 2U);
            if (!pattern) return std::unexpected(pattern.error());
            if (!source) return std::unexpected(source.error());
            if (!flags) return std::unexpected(flags.error());
            auto source_text = string_value(machine, *source);
            if (!source_text) return std::unexpected(source_text.error());
            if (pattern_kind(*source_text) == PatternKind::unsupported) {
                return fail_java("java/util/regex/PatternSyntaxException",
                                 "Pattern is outside the Core regex subset");
            }
            auto source_stored = set_reference_field(
                machine, *pattern, kPatternSourceField, *source);
            auto flags_stored = set_int_field(machine, *pattern,
                                              kPatternFlagsField, *flags);
            if (!source_stored) {
                return std::unexpected(source_stored.error());
            }
            if (!flags_stored) return std::unexpected(flags_stored.error());
            return std::optional<Value> {};
        });
    const auto compile = [&registry](const char* descriptor, bool flags) {
        add(registry, "java/util/regex/Pattern", "compile", descriptor,
            [flags](Machine& machine,
                    std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto source = reference_argument(arguments, 0U);
                if (!source) return std::unexpected(source.error());
                i32 flag_value = 0;
                if (flags) {
                    auto supplied = int_argument(arguments, 1U);
                    if (!supplied) return std::unexpected(supplied.error());
                    flag_value = *supplied;
                }
                auto pattern = new_instance(machine,
                                            "java/util/regex/Pattern");
                if (!pattern) return std::unexpected(pattern.error());
                auto pattern_root = machine.pin_native_root(*pattern);
                if (!pattern_root) {
                    return std::unexpected(pattern_root.error());
                }
                const std::array<Value, 3> initialize {
                    Value::from_reference(*pattern),
                    Value::from_reference(*source),
                    Value::from_int(flag_value),
                };
                auto initialized = invoke_native(
                    machine, "java/util/regex/Pattern", "<init>",
                    "(Ljava/lang/String;I)V", initialize);
                if (!initialized) return std::unexpected(initialized.error());
                return std::optional<Value>(Value::from_reference(*pattern));
            });
    };
    compile("(Ljava/lang/String;)Ljava/util/regex/Pattern;", false);
    compile("(Ljava/lang/String;I)Ljava/util/regex/Pattern;", true);
    add(registry, "java/util/regex/Pattern", "matcher",
        "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto pattern = receiver(arguments);
            auto input = reference_argument(arguments, 1U);
            if (!pattern) return std::unexpected(pattern.error());
            if (!input) return std::unexpected(input.error());
            auto input_string = matcher_input_string(machine, *input);
            if (!input_string) return std::unexpected(input_string.error());
            auto matcher = new_instance(machine,
                                        "java/util/regex/Matcher");
            if (!matcher) return std::unexpected(matcher.error());
            auto pattern_stored = set_reference_field(
                machine, *matcher, kMatcherPatternField, *pattern);
            auto input_stored = set_reference_field(
                machine, *matcher, kMatcherInputField, *input_string);
            auto search_stored = set_int_field(machine, *matcher,
                                               kMatcherSearchFromField, 0);
            auto groups_stored = set_reference_field(
                machine, *matcher, kMatcherGroupsField, {});
            auto matched_stored = set_int_field(machine, *matcher,
                                                kMatcherMatchedField, 0);
            if (!pattern_stored) {
                return std::unexpected(pattern_stored.error());
            }
            if (!input_stored) return std::unexpected(input_stored.error());
            if (!search_stored) return std::unexpected(search_stored.error());
            if (!groups_stored) return std::unexpected(groups_stored.error());
            if (!matched_stored) {
                return std::unexpected(matched_stored.error());
            }
            return std::optional<Value>(Value::from_reference(*matcher));
        });
}

void register_matcher(NativeMethodRegistry& registry) {
    add(registry, "java/util/regex/Matcher", "<init>",
        "(Ljava/util/regex/Pattern;Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto matcher = receiver(arguments);
            auto pattern = reference_argument(arguments, 1U);
            auto input = reference_argument(arguments, 2U);
            if (!matcher) return std::unexpected(matcher.error());
            if (!pattern) return std::unexpected(pattern.error());
            if (!input) return std::unexpected(input.error());
            auto pattern_stored = set_reference_field(
                machine, *matcher, kMatcherPatternField, *pattern);
            auto input_stored = set_reference_field(
                machine, *matcher, kMatcherInputField, *input);
            auto search_stored = set_int_field(machine, *matcher,
                                               kMatcherSearchFromField, 0);
            auto groups_stored = set_reference_field(
                machine, *matcher, kMatcherGroupsField, {});
            auto matched_stored = set_int_field(machine, *matcher,
                                                kMatcherMatchedField, 0);
            if (!pattern_stored) {
                return std::unexpected(pattern_stored.error());
            }
            if (!input_stored) return std::unexpected(input_stored.error());
            if (!search_stored) return std::unexpected(search_stored.error());
            if (!groups_stored) return std::unexpected(groups_stored.error());
            if (!matched_stored) {
                return std::unexpected(matched_stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "java/util/regex/Matcher", "find", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto matcher = receiver(arguments);
            if (!matcher) return std::unexpected(matcher.error());
            auto pattern = reference_field(machine, *matcher,
                                           kMatcherPatternField);
            auto input = reference_field(machine, *matcher,
                                         kMatcherInputField);
            auto from = int_field(machine, *matcher, kMatcherSearchFromField);
            if (!pattern) return std::unexpected(pattern.error());
            if (!input) return std::unexpected(input.error());
            if (!from) return std::unexpected(from.error());
            auto source_ref = reference_field(machine, *pattern,
                                              kPatternSourceField);
            if (!source_ref) return std::unexpected(source_ref.error());
            auto source = string_value(machine, *source_ref);
            auto input_text = string_value(machine, *input);
            if (!source) return std::unexpected(source.error());
            if (!input_text) return std::unexpected(input_text.error());
            const usize search_from = *from < 0
                ? 0U : static_cast<usize>(*from);
            auto match = find_next(*source, *input_text, search_from);
            if (!match.has_value()) {
                auto groups_cleared = set_reference_field(
                    machine, *matcher, kMatcherGroupsField, {});
                auto matched_cleared = set_int_field(
                    machine, *matcher, kMatcherMatchedField, 0);
                auto search_finished = set_int_field(
                    machine, *matcher, kMatcherSearchFromField,
                    static_cast<i32>(std::min<usize>(
                        input_text->size() + 1U,
                        static_cast<usize>(INT32_MAX))));
                if (!groups_cleared) {
                    return std::unexpected(groups_cleared.error());
                }
                if (!matched_cleared) {
                    return std::unexpected(matched_cleared.error());
                }
                if (!search_finished) {
                    return std::unexpected(search_finished.error());
                }
                return std::optional<Value>(Value::from_int(0));
            }
            auto groups = build_group_array(machine, *input_text, *match);
            if (!groups) return std::unexpected(groups.error());
            auto groups_stored = set_reference_field(
                machine, *matcher, kMatcherGroupsField, *groups);
            auto matched_stored = set_int_field(
                machine, *matcher, kMatcherMatchedField, 1);
            const usize next = match->end > match->start
                ? match->end : match->end + 1U;
            auto search_stored = set_int_field(
                machine, *matcher, kMatcherSearchFromField,
                static_cast<i32>(std::min<usize>(
                    next, static_cast<usize>(INT32_MAX))));
            if (!groups_stored) {
                return std::unexpected(groups_stored.error());
            }
            if (!matched_stored) {
                return std::unexpected(matched_stored.error());
            }
            if (!search_stored) {
                return std::unexpected(search_stored.error());
            }
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, "java/util/regex/Matcher", "group",
        "(I)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto matcher = receiver(arguments);
            auto requested = int_argument(arguments, 1U);
            if (!matcher) return std::unexpected(matcher.error());
            if (!requested) return std::unexpected(requested.error());
            auto matched = int_field(machine, *matcher,
                                     kMatcherMatchedField);
            if (!matched) return std::unexpected(matched.error());
            if (*matched == 0) {
                return fail_java("java/lang/IllegalStateException",
                                 "No successful regex match");
            }
            auto groups = reference_field(machine, *matcher,
                                          kMatcherGroupsField);
            if (!groups) return std::unexpected(groups.error());
            auto length = machine.heap().array_length(*groups);
            if (!length) return std::unexpected(length.error());
            if (*requested < 0 ||
                static_cast<usize>(*requested) >= *length) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Regex group index is out of range");
            }
            auto value = machine.heap().element(
                *groups, static_cast<usize>(*requested));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
}

} // namespace

void register_jdk8_regex_natives(NativeMethodRegistry& registry) {
    register_pattern(registry);
    register_matcher(registry);
}

} // namespace phoneme::vm
