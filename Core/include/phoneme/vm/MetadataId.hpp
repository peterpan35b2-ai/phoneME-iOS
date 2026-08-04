#pragma once

#include <compare>
#include <functional>

#include "phoneme/base/Types.hpp"

namespace phoneme::vm {

template <typename Tag>
struct MetadataId final {
    u32 value {0};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(MetadataId, MetadataId) noexcept = default;
    friend constexpr auto operator<=>(MetadataId, MetadataId) noexcept = default;
};

struct ClassIdTag;
struct MethodIdTag;
struct FieldIdTag;
struct NativeMethodIdTag;

using ClassId = MetadataId<ClassIdTag>;
using MethodId = MetadataId<MethodIdTag>;
using FieldId = MetadataId<FieldIdTag>;
using NativeMethodId = MetadataId<NativeMethodIdTag>;

template <typename Id>
struct MetadataIdHash final {
    [[nodiscard]] usize operator()(Id id) const noexcept {
        return std::hash<u32>{}(id.value);
    }
};

} // namespace phoneme::vm
