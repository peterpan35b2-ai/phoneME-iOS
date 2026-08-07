#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::vm {

enum class JavaTypeKind : u8 {
    void_type,
    boolean,
    byte,
    character,
    short_integer,
    integer,
    float32,
    long_integer,
    float64,
    reference,
    array,
};

struct TypeDescriptor final {
    JavaTypeKind kind {JavaTypeKind::void_type};
    std::string class_name;
    JavaTypeKind array_component_kind {JavaTypeKind::void_type};
    u8 array_dimensions {0};

    [[nodiscard]] constexpr usize slot_count() const noexcept {
        switch (kind) {
        case JavaTypeKind::void_type:
            return 0;
        case JavaTypeKind::long_integer:
        case JavaTypeKind::float64:
            return 2;
        default:
            return 1;
        }
    }

    [[nodiscard]] constexpr bool reference_like() const noexcept {
        return kind == JavaTypeKind::reference || kind == JavaTypeKind::array;
    }
};

struct MethodDescriptor final {
    std::vector<TypeDescriptor> parameters;
    TypeDescriptor return_type;

    [[nodiscard]] usize parameter_slots(bool include_receiver) const noexcept;
};

[[nodiscard]] Result<TypeDescriptor> parse_field_descriptor(
    std::string_view descriptor);
[[nodiscard]] Result<MethodDescriptor> parse_method_descriptor(
    std::string_view descriptor);

} // namespace phoneme::vm
