#include "RegexEngine.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace phoneme::vm {
namespace {

constexpr i32 kUnixLines = 0x01;
constexpr i32 kCaseInsensitive = 0x02;
constexpr i32 kComments = 0x04;
constexpr i32 kMultiline = 0x08;
constexpr i32 kLiteral = 0x10;
constexpr i32 kDotall = 0x20;
constexpr i32 kCanonEq = 0x80;
constexpr usize kUnlimited = std::numeric_limits<usize>::max();
constexpr usize kRegexStepBudget = 2'000'000U;
constexpr usize kRegexStateBudget = 100'000U;

struct CharRange final {
    char16_t first {0};
    char16_t last {0};
};

enum class NodeKind : u8 {
    empty,
    literal,
    any,
    character_class,
    begin_anchor,
    end_anchor,
    word_boundary,
    sequence,
    alternation,
    group,
    repeat,
};

struct Node final {
    NodeKind kind {NodeKind::empty};
    char16_t literal {0};
    std::vector<CharRange> ranges;
    bool negated {false};
    bool boundary_negated {false};
    std::vector<usize> children;
    usize child {0U};
    usize group {0U};
    usize minimum {0U};
    usize maximum {0U};
    bool greedy {true};
};

struct Program final {
    std::vector<Node> nodes;
    usize root {0U};
    usize capture_count {0U};
    i32 flags {0};
};

[[nodiscard]] bool ascii_digit(char16_t value) noexcept {
    return value >= u'0' && value <= u'9';
}

[[nodiscard]] bool ascii_lower(char16_t value) noexcept {
    return value >= u'a' && value <= u'z';
}

[[nodiscard]] bool ascii_upper(char16_t value) noexcept {
    return value >= u'A' && value <= u'Z';
}

[[nodiscard]] bool ascii_word(char16_t value) noexcept {
    return ascii_digit(value) || ascii_lower(value) || ascii_upper(value) ||
           value == u'_';
}

[[nodiscard]] bool java_space(char16_t value) noexcept {
    return value == u' ' || value == u'\t' || value == u'\n' ||
           value == u'\v' || value == u'\f' || value == u'\r';
}

[[nodiscard]] bool java_line_terminator(char16_t value,
                                        i32 flags) noexcept {
    if (value == u'\n') return true;
    if ((flags & kUnixLines) != 0) return false;
    return value == u'\r' || value == u'\x0085' ||
           value == u'\x2028' || value == u'\x2029';
}

[[nodiscard]] char16_t ascii_fold(char16_t value) noexcept {
    return ascii_upper(value)
        ? static_cast<char16_t>(value - u'A' + u'a')
        : value;
}

[[nodiscard]] bool equal_character(char16_t left,
                                   char16_t right,
                                   i32 flags) noexcept {
    if (left == right) return true;
    if ((flags & kCaseInsensitive) == 0) return false;
    // CLDC/MIDP content overwhelmingly uses ASCII identifiers. Keep the
    // no-throw engine deterministic on every host; full Unicode case folding
    // can be layered here without changing regex semantics or call sites.
    return ascii_fold(left) == ascii_fold(right);
}

[[nodiscard]] Error syntax_error(std::string message) {
    return Error::make_java("java/util/regex/PatternSyntaxException",
                            std::move(message));
}

class Parser final {
public:
    Parser(std::u16string_view source, i32 flags)
        : source_(source), flags_(flags) {}

    [[nodiscard]] Result<Program> parse() {
        Program program;
        program.flags = flags_;
        program_ = &program;

        if ((flags_ & kCanonEq) != 0) {
            return std::unexpected(syntax_error(
                "CANON_EQ is not supported by the Core regex engine"));
        }

        if ((flags_ & kLiteral) != 0) {
            std::vector<usize> literals;
            literals.reserve(source_.size());
            for (const char16_t value : source_) {
                Node node;
                node.kind = NodeKind::literal;
                node.literal = value;
                literals.push_back(add_node(std::move(node)));
            }
            program.root = make_sequence(std::move(literals));
            program.capture_count = 0U;
            program_ = nullptr;
            return program;
        }

        auto root = parse_expression(false);
        if (!root) return std::unexpected(root.error());
        skip_ignored();
        if (position_ != source_.size()) {
            return std::unexpected(syntax_error("Unexpected regex token"));
        }
        program.root = *root;
        program.capture_count = capture_count_;
        program_ = nullptr;
        return program;
    }

private:
    [[nodiscard]] usize add_node(Node node) {
        program_->nodes.push_back(std::move(node));
        return program_->nodes.size() - 1U;
    }

    [[nodiscard]] usize make_empty() {
        Node node;
        node.kind = NodeKind::empty;
        return add_node(std::move(node));
    }

    [[nodiscard]] usize make_sequence(std::vector<usize> children) {
        if (children.empty()) return make_empty();
        if (children.size() == 1U) return children.front();
        Node node;
        node.kind = NodeKind::sequence;
        node.children = std::move(children);
        return add_node(std::move(node));
    }

    [[nodiscard]] usize make_alternation(std::vector<usize> children) {
        if (children.size() == 1U) return children.front();
        Node node;
        node.kind = NodeKind::alternation;
        node.children = std::move(children);
        return add_node(std::move(node));
    }

    void skip_ignored() {
        if ((flags_ & kComments) == 0) return;
        while (position_ < source_.size()) {
            const char16_t value = source_[position_];
            if (java_space(value)) {
                ++position_;
                continue;
            }
            if (value == u'#') {
                ++position_;
                while (position_ < source_.size() &&
                       source_[position_] != u'\n' &&
                       source_[position_] != u'\r') {
                    ++position_;
                }
                continue;
            }
            break;
        }
    }

    [[nodiscard]] bool consume(char16_t expected) {
        skip_ignored();
        if (position_ >= source_.size() || source_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] Result<usize> parse_expression(bool group) {
        std::vector<usize> branches;
        while (true) {
            auto sequence = parse_sequence(group);
            if (!sequence) return std::unexpected(sequence.error());
            branches.push_back(*sequence);
            skip_ignored();
            if (position_ >= source_.size() || source_[position_] != u'|') {
                break;
            }
            ++position_;
        }
        return make_alternation(std::move(branches));
    }

    [[nodiscard]] Result<usize> parse_sequence(bool group) {
        std::vector<usize> children;
        while (true) {
            skip_ignored();
            if (position_ >= source_.size()) break;
            const char16_t value = source_[position_];
            if (value == u'|' || (group && value == u')')) break;
            if (!group && value == u')') {
                return std::unexpected(syntax_error("Unmatched closing parenthesis"));
            }
            auto atom = parse_quantified();
            if (!atom) return std::unexpected(atom.error());
            children.push_back(*atom);
        }
        return make_sequence(std::move(children));
    }

    [[nodiscard]] Result<usize> parse_quantified() {
        auto atom = parse_atom();
        if (!atom) return std::unexpected(atom.error());
        skip_ignored();
        if (position_ >= source_.size()) return *atom;

        usize minimum = 0U;
        usize maximum = 0U;
        bool quantified = true;
        switch (source_[position_]) {
        case u'?':
            minimum = 0U;
            maximum = 1U;
            ++position_;
            break;
        case u'*':
            minimum = 0U;
            maximum = kUnlimited;
            ++position_;
            break;
        case u'+':
            minimum = 1U;
            maximum = kUnlimited;
            ++position_;
            break;
        case u'{': {
            const usize saved = position_;
            ++position_;
            auto first = parse_unsigned_decimal();
            if (!first.has_value()) {
                position_ = saved;
                quantified = false;
                break;
            }
            minimum = *first;
            maximum = minimum;
            if (position_ < source_.size() && source_[position_] == u',') {
                ++position_;
                auto second = parse_unsigned_decimal();
                maximum = second.has_value() ? *second : kUnlimited;
            }
            if (position_ >= source_.size() || source_[position_] != u'}') {
                return std::unexpected(syntax_error("Unclosed regex quantifier"));
            }
            ++position_;
            if (maximum != kUnlimited && maximum < minimum) {
                return std::unexpected(syntax_error("Illegal regex repetition range"));
            }
            break;
        }
        default:
            quantified = false;
            break;
        }
        if (!quantified) return *atom;

        bool greedy = true;
        if (position_ < source_.size() && source_[position_] == u'?') {
            greedy = false;
            ++position_;
        } else if (position_ < source_.size() && source_[position_] == u'+') {
            // Possessive repetitions affect only backtracking choices. Treating
            // them as greedy would silently produce incorrect matches, so fail
            // explicitly until the matcher carries atomic-group state.
            return std::unexpected(syntax_error(
                "Possessive regex quantifiers are not supported"));
        }

        Node node;
        node.kind = NodeKind::repeat;
        node.child = *atom;
        node.minimum = minimum;
        node.maximum = maximum;
        node.greedy = greedy;
        return add_node(std::move(node));
    }

    [[nodiscard]] std::optional<usize> parse_unsigned_decimal() {
        if (position_ >= source_.size() || !ascii_digit(source_[position_])) {
            return std::nullopt;
        }
        usize value = 0U;
        while (position_ < source_.size() && ascii_digit(source_[position_])) {
            const usize digit = static_cast<usize>(source_[position_] - u'0');
            if (value > (kUnlimited - digit) / 10U) {
                value = kUnlimited;
            } else {
                value = value * 10U + digit;
            }
            ++position_;
        }
        return value;
    }

    [[nodiscard]] Result<usize> parse_atom() {
        skip_ignored();
        if (position_ >= source_.size()) {
            return std::unexpected(syntax_error("Missing regex atom"));
        }
        const char16_t value = source_[position_++];
        switch (value) {
        case u'.': {
            Node node;
            node.kind = NodeKind::any;
            return add_node(std::move(node));
        }
        case u'^': {
            Node node;
            node.kind = NodeKind::begin_anchor;
            return add_node(std::move(node));
        }
        case u'$': {
            Node node;
            node.kind = NodeKind::end_anchor;
            return add_node(std::move(node));
        }
        case u'[':
            return parse_character_class();
        case u'(':
            return parse_group();
        case u'\\':
            return parse_escape(false);
        case u'*':
        case u'+':
        case u'?':
            return std::unexpected(syntax_error("Dangling regex quantifier"));
        default: {
            Node node;
            node.kind = NodeKind::literal;
            node.literal = value;
            return add_node(std::move(node));
        }
        }
    }

    [[nodiscard]] Result<usize> parse_group() {
        bool capturing = true;
        if (position_ < source_.size() && source_[position_] == u'?') {
            ++position_;
            if (position_ < source_.size() && source_[position_] == u':') {
                ++position_;
                capturing = false;
            } else {
                return std::unexpected(syntax_error(
                    "Unsupported regex group construct"));
            }
        }

        usize group = 0U;
        if (capturing) group = ++capture_count_;
        auto child = parse_expression(true);
        if (!child) return std::unexpected(child.error());
        skip_ignored();
        if (position_ >= source_.size() || source_[position_] != u')') {
            return std::unexpected(syntax_error("Unclosed regex group"));
        }
        ++position_;
        if (!capturing) return *child;

        Node node;
        node.kind = NodeKind::group;
        node.child = *child;
        node.group = group;
        return add_node(std::move(node));
    }

    void append_digit_ranges(std::vector<CharRange>& ranges) {
        ranges.push_back({u'0', u'9'});
    }

    void append_word_ranges(std::vector<CharRange>& ranges) {
        ranges.push_back({u'0', u'9'});
        ranges.push_back({u'A', u'Z'});
        ranges.push_back({u'_', u'_'});
        ranges.push_back({u'a', u'z'});
    }

    void append_space_ranges(std::vector<CharRange>& ranges) {
        ranges.push_back({u'\t', u'\r'});
        ranges.push_back({u' ', u' '});
    }

    [[nodiscard]] Result<usize> predefined_class(char16_t escape) {
        Node node;
        node.kind = NodeKind::character_class;
        switch (escape) {
        case u'd': append_digit_ranges(node.ranges); break;
        case u'D': append_digit_ranges(node.ranges); node.negated = true; break;
        case u'w': append_word_ranges(node.ranges); break;
        case u'W': append_word_ranges(node.ranges); node.negated = true; break;
        case u's': append_space_ranges(node.ranges); break;
        case u'S': append_space_ranges(node.ranges); node.negated = true; break;
        default:
            return std::unexpected(syntax_error("Unknown predefined class"));
        }
        return add_node(std::move(node));
    }

    [[nodiscard]] Result<usize> parse_escape(bool in_class) {
        if (position_ >= source_.size()) {
            return std::unexpected(syntax_error("Trailing regex escape"));
        }
        const char16_t value = source_[position_++];
        if (value == u'd' || value == u'D' || value == u'w' || value == u'W' ||
            value == u's' || value == u'S') {
            return predefined_class(value);
        }
        if (!in_class && (value == u'b' || value == u'B')) {
            Node node;
            node.kind = NodeKind::word_boundary;
            node.boundary_negated = value == u'B';
            return add_node(std::move(node));
        }
        if (!in_class && (value == u'A' || value == u'z' || value == u'Z')) {
            Node node;
            node.kind = value == u'A' ? NodeKind::begin_anchor : NodeKind::end_anchor;
            return add_node(std::move(node));
        }
        if (!in_class && value == u'Q') {
            std::vector<usize> literals;
            while (position_ < source_.size()) {
                if (source_[position_] == u'\\' &&
                    position_ + 1U < source_.size() &&
                    source_[position_ + 1U] == u'E') {
                    position_ += 2U;
                    return make_sequence(std::move(literals));
                }
                Node literal;
                literal.kind = NodeKind::literal;
                literal.literal = source_[position_++];
                literals.push_back(add_node(std::move(literal)));
            }
            // Java treats \Q to end-of-pattern as quoted text.
            return make_sequence(std::move(literals));
        }

        char16_t literal = value;
        switch (value) {
        case u't': literal = u'\t'; break;
        case u'n': literal = u'\n'; break;
        case u'r': literal = u'\r'; break;
        case u'f': literal = u'\f'; break;
        case u'a': literal = u'\a'; break;
        case u'e': literal = u'\x001B'; break;
        case u'u': {
            auto parsed = parse_hex(4U);
            if (!parsed) return std::unexpected(parsed.error());
            literal = *parsed;
            break;
        }
        case u'x': {
            auto parsed = parse_hex(2U);
            if (!parsed) return std::unexpected(parsed.error());
            literal = *parsed;
            break;
        }
        default:
            if (ascii_digit(value)) {
                return std::unexpected(syntax_error(
                    "Regex back references are not supported"));
            }
            break;
        }
        Node node;
        node.kind = NodeKind::literal;
        node.literal = literal;
        return add_node(std::move(node));
    }

    [[nodiscard]] Result<char16_t> parse_hex(usize digits) {
        if (source_.size() - position_ < digits) {
            return std::unexpected(syntax_error("Incomplete hexadecimal escape"));
        }
        u32 value = 0U;
        for (usize index = 0U; index < digits; ++index) {
            const char16_t character = source_[position_++];
            u32 digit = 0U;
            if (character >= u'0' && character <= u'9') {
                digit = static_cast<u32>(character - u'0');
            } else if (character >= u'a' && character <= u'f') {
                digit = 10U + static_cast<u32>(character - u'a');
            } else if (character >= u'A' && character <= u'F') {
                digit = 10U + static_cast<u32>(character - u'A');
            } else {
                return std::unexpected(syntax_error("Invalid hexadecimal escape"));
            }
            value = value * 16U + digit;
        }
        return static_cast<char16_t>(value);
    }

    struct ClassElement final {
        bool single {false};
        char16_t value {0};
        std::vector<CharRange> ranges;
    };

    [[nodiscard]] Result<ClassElement> parse_class_element() {
        if (position_ >= source_.size()) {
            return std::unexpected(syntax_error("Unclosed character class"));
        }
        char16_t value = source_[position_++];
        if (value != u'\\') {
            return ClassElement {.single = true, .value = value};
        }
        if (position_ >= source_.size()) {
            return std::unexpected(syntax_error("Trailing escape in character class"));
        }
        const char16_t escaped = source_[position_++];
        ClassElement element;
        if (escaped == u'd' || escaped == u'w' || escaped == u's') {
            if (escaped == u'd') append_digit_ranges(element.ranges);
            if (escaped == u'w') append_word_ranges(element.ranges);
            if (escaped == u's') append_space_ranges(element.ranges);
            return element;
        }
        if (escaped == u'D' || escaped == u'W' || escaped == u'S') {
            return std::unexpected(syntax_error(
                "Negated predefined classes inside [] are not supported"));
        }
        element.single = true;
        switch (escaped) {
        case u't': element.value = u'\t'; break;
        case u'n': element.value = u'\n'; break;
        case u'r': element.value = u'\r'; break;
        case u'f': element.value = u'\f'; break;
        default: element.value = escaped; break;
        }
        return element;
    }

    [[nodiscard]] Result<usize> parse_character_class() {
        Node node;
        node.kind = NodeKind::character_class;
        if (position_ < source_.size() && source_[position_] == u'^') {
            node.negated = true;
            ++position_;
        }

        bool first = true;
        bool closed = false;
        while (position_ < source_.size()) {
            if (source_[position_] == u']' && !first) {
                ++position_;
                closed = true;
                break;
            }
            first = false;
            auto left = parse_class_element();
            if (!left) return std::unexpected(left.error());
            if (!left->single) {
                node.ranges.insert(node.ranges.end(),
                                   left->ranges.begin(), left->ranges.end());
                continue;
            }

            if (position_ + 1U < source_.size() &&
                source_[position_] == u'-' &&
                source_[position_ + 1U] != u']') {
                ++position_;
                auto right = parse_class_element();
                if (!right) return std::unexpected(right.error());
                if (!right->single) {
                    return std::unexpected(syntax_error(
                        "Character class range endpoint must be literal"));
                }
                if (right->value < left->value) {
                    return std::unexpected(syntax_error(
                        "Illegal character class range"));
                }
                node.ranges.push_back({left->value, right->value});
            } else {
                node.ranges.push_back({left->value, left->value});
            }
        }
        if (!closed) {
            return std::unexpected(syntax_error("Unclosed character class"));
        }
        return add_node(std::move(node));
    }

    std::u16string_view source_;
    i32 flags_ {0};
    usize position_ {0U};
    usize capture_count_ {0U};
    Program* program_ {nullptr};
};

struct MatchState final {
    usize position {0U};
    std::vector<RegexCapture> captures;
};

class Matcher final {
public:
    Matcher(const Program& program, std::u16string_view input)
        : program_(program), input_(input) {}

    [[nodiscard]] Result<std::vector<MatchState>> expand(usize node_index,
                                                         MatchState state) {
        std::vector<MatchState> output;
        auto status = expand_into(node_index, std::move(state), output);
        if (!status) return std::unexpected(status.error());
        return output;
    }

private:
    [[nodiscard]] Status step() {
        if (++steps_ > kRegexStepBudget) {
            return fail(ErrorCode::unsupported_feature,
                        "Regex execution budget was exhausted");
        }
        return {};
    }

    [[nodiscard]] Status append_state(std::vector<MatchState>& output,
                                      MatchState state) {
        if (output.size() >= kRegexStateBudget) {
            return fail(ErrorCode::unsupported_feature,
                        "Regex backtracking state budget was exhausted");
        }
        output.push_back(std::move(state));
        return {};
    }

    [[nodiscard]] bool class_matches(const Node& node,
                                     char16_t value) const noexcept {
        bool inside = false;
        for (const CharRange range : node.ranges) {
            if (equal_character(value, range.first, program_.flags) ||
                equal_character(value, range.last, program_.flags) ||
                (value >= range.first && value <= range.last)) {
                inside = true;
                break;
            }
            if ((program_.flags & kCaseInsensitive) != 0) {
                const char16_t folded = ascii_fold(value);
                if (folded >= ascii_fold(range.first) &&
                    folded <= ascii_fold(range.last)) {
                    inside = true;
                    break;
                }
            }
        }
        return node.negated ? !inside : inside;
    }

    [[nodiscard]] Status expand_into(usize node_index,
                                     MatchState state,
                                     std::vector<MatchState>& output) {
        auto budget = step();
        if (!budget) return budget;
        if (node_index >= program_.nodes.size()) {
            return fail(ErrorCode::internal_error, "Regex node index is invalid");
        }
        const Node& node = program_.nodes[node_index];
        switch (node.kind) {
        case NodeKind::empty:
            return append_state(output, std::move(state));
        case NodeKind::literal:
            if (state.position < input_.size() &&
                equal_character(input_[state.position], node.literal,
                                program_.flags)) {
                ++state.position;
                return append_state(output, std::move(state));
            }
            return {};
        case NodeKind::any:
            if (state.position < input_.size() &&
                (((program_.flags & kDotall) != 0) ||
                 !java_line_terminator(input_[state.position], program_.flags))) {
                ++state.position;
                return append_state(output, std::move(state));
            }
            return {};
        case NodeKind::character_class:
            if (state.position < input_.size() &&
                class_matches(node, input_[state.position])) {
                ++state.position;
                return append_state(output, std::move(state));
            }
            return {};
        case NodeKind::begin_anchor: {
            const bool at_begin = state.position == 0U;
            const bool multiline =
                (program_.flags & kMultiline) != 0 && state.position > 0U &&
                java_line_terminator(input_[state.position - 1U], program_.flags);
            if (at_begin || multiline) {
                return append_state(output, std::move(state));
            }
            return {};
        }
        case NodeKind::end_anchor: {
            const bool at_end = state.position == input_.size();
            bool before_final_terminator = false;
            if (!at_end && state.position + 1U == input_.size()) {
                before_final_terminator =
                    java_line_terminator(input_[state.position], program_.flags);
            } else if (!at_end && state.position + 2U == input_.size() &&
                       input_[state.position] == u'\r' &&
                       input_[state.position + 1U] == u'\n') {
                before_final_terminator = true;
            }
            const bool multiline =
                (program_.flags & kMultiline) != 0 &&
                state.position < input_.size() &&
                java_line_terminator(input_[state.position], program_.flags);
            if (at_end || before_final_terminator || multiline) {
                return append_state(output, std::move(state));
            }
            return {};
        }
        case NodeKind::word_boundary: {
            const bool left = state.position > 0U &&
                              ascii_word(input_[state.position - 1U]);
            const bool right = state.position < input_.size() &&
                               ascii_word(input_[state.position]);
            const bool boundary = left != right;
            if (boundary != node.boundary_negated) {
                return append_state(output, std::move(state));
            }
            return {};
        }
        case NodeKind::sequence: {
            std::vector<MatchState> current;
            current.push_back(std::move(state));
            for (const usize child : node.children) {
                std::vector<MatchState> next;
                for (MatchState& candidate : current) {
                    auto status = expand_into(child, std::move(candidate), next);
                    if (!status) return status;
                }
                if (next.empty()) return {};
                current = std::move(next);
            }
            for (MatchState& candidate : current) {
                auto status = append_state(output, std::move(candidate));
                if (!status) return status;
            }
            return {};
        }
        case NodeKind::alternation:
            for (const usize child : node.children) {
                auto status = expand_into(child, state, output);
                if (!status) return status;
            }
            return {};
        case NodeKind::group: {
            const usize start = state.position;
            std::vector<MatchState> matches;
            auto status = expand_into(node.child, std::move(state), matches);
            if (!status) return status;
            for (MatchState& candidate : matches) {
                if (node.group == 0U || node.group > candidate.captures.size()) {
                    return fail(ErrorCode::internal_error,
                                "Regex capture group index is invalid");
                }
                RegexCapture& capture = candidate.captures[node.group - 1U];
                capture.matched = true;
                capture.start = start;
                capture.end = candidate.position;
                status = append_state(output, std::move(candidate));
                if (!status) return status;
            }
            return {};
        }
        case NodeKind::repeat:
            return expand_repeat(node, std::move(state), 0U, output);
        }
        return fail(ErrorCode::internal_error, "Unknown regex node kind");
    }

    [[nodiscard]] Status expand_repeat(const Node& node,
                                       MatchState state,
                                       usize count,
                                       std::vector<MatchState>& output) {
        auto budget = step();
        if (!budget) return budget;

        const bool can_accept = count >= node.minimum;
        if (!node.greedy && can_accept) {
            auto status = append_state(output, state);
            if (!status) return status;
        }

        if (count < node.maximum) {
            std::vector<MatchState> next;
            const usize before = state.position;
            auto status = expand_into(node.child, state, next);
            if (!status) return status;
            for (MatchState& candidate : next) {
                if (candidate.position == before) {
                    if (count + 1U >= node.minimum) {
                        status = append_state(output, std::move(candidate));
                        if (!status) return status;
                    }
                    continue;
                }
                status = expand_repeat(node, std::move(candidate), count + 1U,
                                       output);
                if (!status) return status;
            }
        }

        if (node.greedy && can_accept) {
            return append_state(output, std::move(state));
        }
        return {};
    }

    const Program& program_;
    std::u16string_view input_;
    usize steps_ {0U};
};

[[nodiscard]] Result<Program> compile_regex(std::u16string_view pattern,
                                            i32 flags) {
    Parser parser(pattern, flags);
    return parser.parse();
}

[[nodiscard]] RegexMatch to_public_match(const MatchState& state,
                                         usize start) {
    return RegexMatch {
        .start = start,
        .end = state.position,
        .captures = state.captures,
    };
}

[[nodiscard]] Result<std::optional<RegexMatch>> match_from(
    const Program& program,
    std::u16string_view input,
    usize start,
    bool require_end) {
    MatchState initial;
    initial.position = start;
    initial.captures.resize(program.capture_count);
    Matcher matcher(program, input);
    auto states = matcher.expand(program.root, std::move(initial));
    if (!states) return std::unexpected(states.error());
    for (const MatchState& state : *states) {
        if (!require_end || state.position == input.size()) {
            return std::optional<RegexMatch>(to_public_match(state, start));
        }
    }
    return std::optional<RegexMatch> {};
}

[[nodiscard]] Status append_java_replacement(
    std::u16string& output,
    std::u16string_view input,
    std::u16string_view replacement,
    const RegexMatch& match) {
    for (usize index = 0U; index < replacement.size(); ++index) {
        const char16_t value = replacement[index];
        if (value == u'\\') {
            if (index + 1U >= replacement.size()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Trailing escape in regex replacement");
            }
            output.push_back(replacement[++index]);
            continue;
        }
        if (value != u'$') {
            output.push_back(value);
            continue;
        }
        if (index + 1U >= replacement.size() ||
            !ascii_digit(replacement[index + 1U])) {
            return fail_java("java/lang/IllegalArgumentException",
                             "Illegal group reference in regex replacement");
        }

        usize group = static_cast<usize>(replacement[++index] - u'0');
        while (index + 1U < replacement.size() &&
               ascii_digit(replacement[index + 1U])) {
            const usize candidate =
                group * 10U + static_cast<usize>(replacement[index + 1U] - u'0');
            if (candidate > match.captures.size()) break;
            group = candidate;
            ++index;
        }
        if (group > match.captures.size()) {
            return fail_java("java/lang/IndexOutOfBoundsException",
                             "Regex replacement group index is out of range");
        }
        if (group == 0U) {
            output.append(input.substr(match.start, match.end - match.start));
            continue;
        }
        const RegexCapture& capture = match.captures[group - 1U];
        if (capture.matched) {
            output.append(input.substr(capture.start,
                                       capture.end - capture.start));
        }
    }
    return {};
}

} // namespace

Status validate_java_regex(std::u16string_view pattern, i32 flags) {
    auto compiled = compile_regex(pattern, flags);
    if (!compiled) return std::unexpected(compiled.error());
    return {};
}

Result<std::optional<RegexMatch>> find_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    usize from,
    i32 flags) {
    if (from > input.size()) return std::optional<RegexMatch> {};
    auto program = compile_regex(pattern, flags);
    if (!program) return std::unexpected(program.error());
    for (usize start = from; start <= input.size(); ++start) {
        auto match = match_from(*program, input, start, false);
        if (!match) return std::unexpected(match.error());
        if (match->has_value()) return match;
    }
    return std::optional<RegexMatch> {};
}

Result<std::optional<RegexMatch>> match_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    i32 flags) {
    auto program = compile_regex(pattern, flags);
    if (!program) return std::unexpected(program.error());
    return match_from(*program, input, 0U, true);
}

Result<std::vector<std::u16string>> split_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    i32 limit,
    i32 flags) {
    auto program = compile_regex(pattern, flags);
    if (!program) return std::unexpected(program.error());
    if (limit == 1) {
        return std::vector<std::u16string> {std::u16string(input)};
    }

    std::vector<std::u16string> parts;
    usize cursor = 0U;
    usize search_from = 0U;
    bool accepted = false;
    while (search_from <= input.size()) {
        std::optional<RegexMatch> found;
        for (usize start = search_from; start <= input.size(); ++start) {
            auto match = match_from(*program, input, start, false);
            if (!match) return std::unexpected(match.error());
            if (match->has_value()) {
                found = std::move(**match);
                break;
            }
        }
        if (!found.has_value()) break;

        const bool leading_zero_width =
            !accepted && cursor == 0U && found->start == 0U && found->end == 0U;
        const bool limit_reached =
            limit > 0 && parts.size() + 1U >= static_cast<usize>(limit);
        if (limit_reached) break;
        if (!leading_zero_width) {
            parts.emplace_back(input.substr(cursor, found->start - cursor));
            cursor = found->end;
            accepted = true;
        }

        if (found->end > found->start) {
            search_from = found->end;
        } else if (found->end < input.size()) {
            search_from = found->end + 1U;
        } else {
            break;
        }
    }

    if (!accepted) {
        return std::vector<std::u16string> {std::u16string(input)};
    }
    parts.emplace_back(input.substr(cursor));
    if (limit == 0) {
        while (!parts.empty() && parts.back().empty()) parts.pop_back();
    }
    return parts;
}

Result<std::u16string> replace_all_java_regex(
    std::u16string_view pattern,
    std::u16string_view input,
    std::u16string_view replacement,
    i32 flags) {
    auto program = compile_regex(pattern, flags);
    if (!program) return std::unexpected(program.error());

    std::u16string output;
    output.reserve(input.size());
    usize cursor = 0U;
    usize search_from = 0U;
    while (search_from <= input.size()) {
        std::optional<RegexMatch> found;
        for (usize start = search_from; start <= input.size(); ++start) {
            auto match = match_from(*program, input, start, false);
            if (!match) return std::unexpected(match.error());
            if (match->has_value()) {
                found = std::move(**match);
                break;
            }
        }
        if (!found.has_value()) break;

        output.append(input.substr(cursor, found->start - cursor));
        auto appended = append_java_replacement(output, input, replacement, *found);
        if (!appended) return std::unexpected(appended.error());
        cursor = found->end;

        if (found->end > found->start) {
            search_from = found->end;
        } else if (found->end < input.size()) {
            // A zero-width replacement occurs at the current position, then the
            // search advances by one UTF-16 code unit while the skipped input is
            // still emitted by the next prefix append.
            search_from = found->end + 1U;
        } else {
            break;
        }
    }
    output.append(input.substr(cursor));
    return output;
}

} // namespace phoneme::vm
