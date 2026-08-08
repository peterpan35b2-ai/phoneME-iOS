#include "Jdk8CompatNativesParts.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <string>
#include <string_view>

#include "Jdk8CompatNativeSupport.hpp"
#include "RegexEngine.hpp"

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
constexpr i32 kKnownPatternFlags = 0x01FF;

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

    auto whole = create_string(machine, std::u16string(
        input.substr(match.start, match.end - match.start)));
    if (!whole) return std::unexpected(whole.error());
    auto whole_stored = machine.heap().set_element(
        *groups, 0U, Value::from_reference(*whole));
    if (!whole_stored) return std::unexpected(whole_stored.error());

    for (usize index = 0U; index < match.captures.size(); ++index) {
        const RegexCapture& capture = match.captures[index];
        if (!capture.matched) continue;
        auto value = create_string(machine, std::u16string(
            input.substr(capture.start, capture.end - capture.start)));
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(
            *groups, index + 1U, Value::from_reference(*value));
        if (!stored) return std::unexpected(stored.error());
    }
    return *groups;
}

struct PatternState final {
    std::u16string source;
    i32 flags {0};
};

[[nodiscard]] Result<PatternState> pattern_state(Machine& machine,
                                                 ObjectRef pattern) {
    auto source_ref = reference_field(machine, pattern, kPatternSourceField);
    auto flags = int_field(machine, pattern, kPatternFlagsField);
    if (!source_ref) return std::unexpected(source_ref.error());
    if (!flags) return std::unexpected(flags.error());
    auto source = string_value(machine, *source_ref);
    if (!source) return std::unexpected(source.error());
    return PatternState {.source = std::move(*source), .flags = *flags};
}

[[nodiscard]] Status clear_match(Machine& machine,
                                 ObjectRef matcher,
                                 usize input_size) {
    auto groups = set_reference_field(machine, matcher, kMatcherGroupsField, {});
    auto matched = set_int_field(machine, matcher, kMatcherMatchedField, 0);
    auto search = set_int_field(
        machine, matcher, kMatcherSearchFromField,
        static_cast<i32>(std::min<usize>(input_size + 1U,
                                        static_cast<usize>(INT32_MAX))));
    if (!groups) return groups;
    if (!matched) return matched;
    return search;
}

[[nodiscard]] Status store_match(Machine& machine,
                                 ObjectRef matcher,
                                 std::u16string_view input,
                                 const RegexMatch& match,
                                 usize next_search) {
    auto groups = build_group_array(machine, input, match);
    if (!groups) return std::unexpected(groups.error());
    auto groups_stored = set_reference_field(
        machine, matcher, kMatcherGroupsField, *groups);
    auto matched_stored = set_int_field(
        machine, matcher, kMatcherMatchedField, 1);
    auto search_stored = set_int_field(
        machine, matcher, kMatcherSearchFromField,
        static_cast<i32>(std::min<usize>(
            next_search, static_cast<usize>(INT32_MAX))));
    if (!groups_stored) return groups_stored;
    if (!matched_stored) return matched_stored;
    return search_stored;
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
            if ((*flags & ~kKnownPatternFlags) != 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Unknown Pattern flag");
            }
            auto source_text = string_value(machine, *source);
            if (!source_text) return std::unexpected(source_text.error());
            auto validated = validate_java_regex(*source_text, *flags);
            if (!validated) return std::unexpected(validated.error());
            auto source_stored = set_reference_field(
                machine, *pattern, kPatternSourceField, *source);
            auto flags_stored = set_int_field(machine, *pattern,
                                              kPatternFlagsField, *flags);
            if (!source_stored) return std::unexpected(source_stored.error());
            if (!flags_stored) return std::unexpected(flags_stored.error());
            return std::optional<Value> {};
        });

    const auto compile = [&registry](const char* descriptor, bool has_flags) {
        add(registry, "java/util/regex/Pattern", "compile", descriptor,
            [has_flags](Machine& machine,
                        std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto source = reference_argument(arguments, 0U);
                if (!source) return std::unexpected(source.error());
                i32 flags = 0;
                if (has_flags) {
                    auto supplied = int_argument(arguments, 1U);
                    if (!supplied) return std::unexpected(supplied.error());
                    flags = *supplied;
                }
                auto pattern = new_instance(machine, "java/util/regex/Pattern");
                if (!pattern) return std::unexpected(pattern.error());
                auto pattern_root = machine.pin_native_root(*pattern);
                if (!pattern_root) return std::unexpected(pattern_root.error());
                const std::array<Value, 3> initialize {
                    Value::from_reference(*pattern),
                    Value::from_reference(*source),
                    Value::from_int(flags),
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

    add(registry, "java/util/regex/Pattern", "matches",
        "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto source = reference_argument(arguments, 0U);
            auto input = reference_argument(arguments, 1U);
            if (!source) return std::unexpected(source.error());
            if (!input) return std::unexpected(input.error());
            auto input_string = matcher_input_string(machine, *input);
            if (!input_string) return std::unexpected(input_string.error());
            auto source_text = string_value(machine, *source);
            auto input_text = string_value(machine, *input_string);
            if (!source_text) return std::unexpected(source_text.error());
            if (!input_text) return std::unexpected(input_text.error());
            auto match = match_java_regex(*source_text, *input_text);
            if (!match) return std::unexpected(match.error());
            return std::optional<Value>(Value::from_int(
                match->has_value() ? 1 : 0));
        });

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
            auto matcher = new_instance(machine, "java/util/regex/Matcher");
            if (!matcher) return std::unexpected(matcher.error());
            auto pattern_stored = set_reference_field(
                machine, *matcher, kMatcherPatternField, *pattern);
            auto input_stored = set_reference_field(
                machine, *matcher, kMatcherInputField, *input_string);
            auto search_stored = set_int_field(
                machine, *matcher, kMatcherSearchFromField, 0);
            auto groups_stored = set_reference_field(
                machine, *matcher, kMatcherGroupsField, {});
            auto matched_stored = set_int_field(
                machine, *matcher, kMatcherMatchedField, 0);
            if (!pattern_stored) return std::unexpected(pattern_stored.error());
            if (!input_stored) return std::unexpected(input_stored.error());
            if (!search_stored) return std::unexpected(search_stored.error());
            if (!groups_stored) return std::unexpected(groups_stored.error());
            if (!matched_stored) return std::unexpected(matched_stored.error());
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
            auto search_stored = set_int_field(
                machine, *matcher, kMatcherSearchFromField, 0);
            auto groups_stored = set_reference_field(
                machine, *matcher, kMatcherGroupsField, {});
            auto matched_stored = set_int_field(
                machine, *matcher, kMatcherMatchedField, 0);
            if (!pattern_stored) return std::unexpected(pattern_stored.error());
            if (!input_stored) return std::unexpected(input_stored.error());
            if (!search_stored) return std::unexpected(search_stored.error());
            if (!groups_stored) return std::unexpected(groups_stored.error());
            if (!matched_stored) return std::unexpected(matched_stored.error());
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
            auto state = pattern_state(machine, *pattern);
            auto input_text = string_value(machine, *input);
            if (!state) return std::unexpected(state.error());
            if (!input_text) return std::unexpected(input_text.error());
            const usize search_from = *from < 0
                ? 0U : static_cast<usize>(*from);
            auto match = find_java_regex(state->source, *input_text,
                                         search_from, state->flags);
            if (!match) return std::unexpected(match.error());
            if (!match->has_value()) {
                auto cleared = clear_match(machine, *matcher,
                                           input_text->size());
                if (!cleared) return std::unexpected(cleared.error());
                return std::optional<Value>(Value::from_int(0));
            }
            const RegexMatch& found = **match;
            const usize next = found.end > found.start
                ? found.end : found.end + 1U;
            auto stored = store_match(machine, *matcher, *input_text,
                                      found, next);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(1));
        });

    add(registry, "java/util/regex/Matcher", "matches", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto matcher = receiver(arguments);
            if (!matcher) return std::unexpected(matcher.error());
            auto pattern = reference_field(machine, *matcher,
                                           kMatcherPatternField);
            auto input = reference_field(machine, *matcher,
                                         kMatcherInputField);
            if (!pattern) return std::unexpected(pattern.error());
            if (!input) return std::unexpected(input.error());
            auto state = pattern_state(machine, *pattern);
            auto input_text = string_value(machine, *input);
            if (!state) return std::unexpected(state.error());
            if (!input_text) return std::unexpected(input_text.error());
            auto match = match_java_regex(state->source, *input_text,
                                          state->flags);
            if (!match) return std::unexpected(match.error());
            if (!match->has_value()) {
                auto cleared = clear_match(machine, *matcher,
                                           input_text->size());
                if (!cleared) return std::unexpected(cleared.error());
                return std::optional<Value>(Value::from_int(0));
            }
            const RegexMatch& found = **match;
            auto stored = store_match(machine, *matcher, *input_text,
                                      found, found.end);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(1));
        });

    const auto group = [&registry](const char* descriptor, bool indexed) {
        add(registry, "java/util/regex/Matcher", "group", descriptor,
            [indexed](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto matcher = receiver(arguments);
                if (!matcher) return std::unexpected(matcher.error());
                i32 requested = 0;
                if (indexed) {
                    auto value = int_argument(arguments, 1U);
                    if (!value) return std::unexpected(value.error());
                    requested = *value;
                }
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
                if (requested < 0 ||
                    static_cast<usize>(requested) >= *length) {
                    return fail_java("java/lang/IndexOutOfBoundsException",
                                     "Regex group index is out of range");
                }
                auto value = machine.heap().element(
                    *groups, static_cast<usize>(requested));
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(*value);
            });
    };
    group("()Ljava/lang/String;", false);
    group("(I)Ljava/lang/String;", true);
}

} // namespace

void register_jdk8_regex_natives(NativeMethodRegistry& registry) {
    register_pattern(registry);
    register_matcher(registry);
}

} // namespace phoneme::vm
