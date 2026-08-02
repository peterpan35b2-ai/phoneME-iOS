#pragma once

#include <limits>
#include <type_traits>

#include "phoneme/base/Error.hpp"

namespace phoneme {

template <typename To, typename From>
[[nodiscard]] Result<To> checked_narrow(From value) {
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);

    if constexpr (std::is_signed_v<From> == std::is_signed_v<To>) {
        if (value < static_cast<From>(std::numeric_limits<To>::min()) ||
            value > static_cast<From>(std::numeric_limits<To>::max())) {
            return fail(ErrorCode::overflow, "integer conversion overflow");
        }
    } else if constexpr (std::is_signed_v<From>) {
        if (value < 0) {
            return fail(ErrorCode::overflow, "negative value cannot be narrowed");
        }
        using UnsignedFrom = std::make_unsigned_t<From>;
        if (static_cast<UnsignedFrom>(value) > std::numeric_limits<To>::max()) {
            return fail(ErrorCode::overflow, "integer conversion overflow");
        }
    } else {
        using UnsignedTo = std::make_unsigned_t<To>;
        if (value > static_cast<UnsignedTo>(std::numeric_limits<To>::max())) {
            return fail(ErrorCode::overflow, "integer conversion overflow");
        }
    }

    return static_cast<To>(value);
}

[[nodiscard]] inline Result<usize> checked_add(usize left, usize right) {
    if (right > std::numeric_limits<usize>::max() - left) {
        return fail(ErrorCode::overflow, "size addition overflow");
    }
    return left + right;
}

[[nodiscard]] inline Result<usize> checked_multiply(usize left, usize right) {
    if (left != 0 && right > std::numeric_limits<usize>::max() / left) {
        return fail(ErrorCode::overflow, "size multiplication overflow");
    }
    return left * right;
}

} // namespace phoneme
