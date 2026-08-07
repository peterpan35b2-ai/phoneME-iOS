#include "phoneme/vm/Descriptor.hpp"

#include <limits>

namespace phoneme::vm {
namespace {

class DescriptorReader final {
public:
    explicit DescriptorReader(std::string_view descriptor) noexcept
        : descriptor_(descriptor) {}

    [[nodiscard]] bool empty() const noexcept {
        return position_ >= descriptor_.size();
    }

    [[nodiscard]] usize position() const noexcept { return position_; }

    [[nodiscard]] Result<char> peek() const {
        if (empty()) {
            return fail(ErrorCode::malformed_class,
                        "descriptor ended unexpectedly");
        }
        return descriptor_[position_];
    }

    [[nodiscard]] Result<char> read() {
        auto character = peek();
        if (!character) {
            return std::unexpected(character.error());
        }
        ++position_;
        return *character;
    }

    [[nodiscard]] Result<std::string> read_class_name() {
        const usize start = position_;
        while (!empty() && descriptor_[position_] != ';') {
            const char character = descriptor_[position_];
            if (character == '.' || character == '[' || character == '(' ||
                character == ')' || character == '\0') {
                return fail(ErrorCode::malformed_class,
                            "invalid character in class descriptor");
            }
            ++position_;
        }
        if (empty() || descriptor_[position_] != ';') {
            return fail(ErrorCode::malformed_class,
                        "class descriptor is missing its terminator");
        }
        if (position_ == start) {
            return fail(ErrorCode::malformed_class,
                        "class descriptor has an empty class name");
        }
        std::string result(descriptor_.substr(start, position_ - start));
        ++position_;
        return result;
    }

private:
    std::string_view descriptor_;
    usize position_ {0};
};

[[nodiscard]] Result<TypeDescriptor> parse_type(DescriptorReader& reader,
                                                bool allow_void) {
    auto token = reader.read();
    if (!token) {
        return std::unexpected(token.error());
    }

    switch (*token) {
    case 'V':
        if (!allow_void) {
            return fail(ErrorCode::malformed_class,
                        "void is not valid in this descriptor position");
        }
        return TypeDescriptor {.kind = JavaTypeKind::void_type};
    case 'Z':
        return TypeDescriptor {.kind = JavaTypeKind::boolean};
    case 'B':
        return TypeDescriptor {.kind = JavaTypeKind::byte};
    case 'C':
        return TypeDescriptor {.kind = JavaTypeKind::character};
    case 'S':
        return TypeDescriptor {.kind = JavaTypeKind::short_integer};
    case 'I':
        return TypeDescriptor {.kind = JavaTypeKind::integer};
    case 'F':
        return TypeDescriptor {.kind = JavaTypeKind::float32};
    case 'J':
        return TypeDescriptor {.kind = JavaTypeKind::long_integer};
    case 'D':
        return TypeDescriptor {.kind = JavaTypeKind::float64};
    case 'L': {
        auto class_name = reader.read_class_name();
        if (!class_name) {
            return std::unexpected(class_name.error());
        }
        return TypeDescriptor {
            .kind = JavaTypeKind::reference,
            .class_name = std::move(*class_name),
        };
    }
    case '[': {
        u16 dimensions = 1;
        while (true) {
            auto next = reader.peek();
            if (!next) {
                return std::unexpected(next.error());
            }
            if (*next != '[') {
                break;
            }
            (void)reader.read();
            ++dimensions;
            if (dimensions > std::numeric_limits<u8>::max()) {
                return fail(ErrorCode::malformed_class,
                            "array descriptor has too many dimensions");
            }
        }

        auto component = parse_type(reader, false);
        if (!component) {
            return std::unexpected(component.error());
        }
        if (component->kind == JavaTypeKind::void_type) {
            return fail(ErrorCode::malformed_class,
                        "array component cannot be void");
        }

        std::string class_name;
        if (component->kind == JavaTypeKind::reference ||
            component->kind == JavaTypeKind::array) {
            class_name = std::move(component->class_name);
        }
        return TypeDescriptor {
            .kind = JavaTypeKind::array,
            .class_name = std::move(class_name),
            .array_component_kind = component->kind,
            .array_dimensions = static_cast<u8>(dimensions),
        };
    }
    default:
        return fail(ErrorCode::malformed_class,
                    "unknown Java descriptor token");
    }
}

} // namespace

usize MethodDescriptor::parameter_slots(bool include_receiver) const noexcept {
    usize slots = include_receiver ? 1 : 0;
    for (const TypeDescriptor& parameter : parameters) {
        slots += parameter.slot_count();
    }
    return slots;
}

Result<TypeDescriptor> parse_field_descriptor(std::string_view descriptor) {
    if (descriptor.empty()) {
        return fail(ErrorCode::malformed_class,
                    "field descriptor must not be empty");
    }

    DescriptorReader reader(descriptor);
    auto type = parse_type(reader, false);
    if (!type) {
        return std::unexpected(type.error());
    }
    if (!reader.empty()) {
        return fail(ErrorCode::malformed_class,
                    "field descriptor contains trailing characters");
    }
    return type;
}

Result<MethodDescriptor> parse_method_descriptor(std::string_view descriptor) {
    DescriptorReader reader(descriptor);
    auto opening = reader.read();
    if (!opening || *opening != '(') {
        return fail(ErrorCode::malformed_class,
                    "method descriptor must start with '('");
    }

    MethodDescriptor result;
    while (true) {
        auto next = reader.peek();
        if (!next) {
            return std::unexpected(next.error());
        }
        if (*next == ')') {
            (void)reader.read();
            break;
        }
        auto parameter = parse_type(reader, false);
        if (!parameter) {
            return std::unexpected(parameter.error());
        }
        result.parameters.push_back(std::move(*parameter));
        if (result.parameter_slots(false) > 255) {
            return fail(ErrorCode::malformed_class,
                        "method descriptor exceeds the JVM parameter-slot limit");
        }
    }

    auto return_type = parse_type(reader, true);
    if (!return_type) {
        return std::unexpected(return_type.error());
    }
    if (!reader.empty()) {
        return fail(ErrorCode::malformed_class,
                    "method descriptor contains trailing characters");
    }
    result.return_type = std::move(*return_type);
    return result;
}

} // namespace phoneme::vm
