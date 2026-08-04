#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace phoneme {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using usize = std::size_t;
using isize = std::ptrdiff_t;

#if defined(__EMSCRIPTEN__)
static_assert(sizeof(void*) == sizeof(u32),
              "phoneME Web requires the wasm32 Emscripten ABI");
static_assert(sizeof(usize) == sizeof(u32),
              "phoneME Web requires 32-bit size_t");
#else
static_assert(sizeof(void*) == sizeof(u64), "phoneME Core is arm64-only");
static_assert(sizeof(usize) == sizeof(u64), "size_t must be 64-bit");
#endif
static_assert(std::endian::native == std::endian::little,
              "phoneME Core supports little-endian targets only");
static_assert(std::numeric_limits<unsigned char>::digits == 8,
              "phoneME requires 8-bit bytes");

template <typename Enum>
[[nodiscard]] constexpr auto to_underlying(Enum value) noexcept
    -> std::underlying_type_t<Enum> {
    static_assert(std::is_enum_v<Enum>);
    return static_cast<std::underlying_type_t<Enum>>(value);
}

struct AppId final {
    i32 value {0};

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }
    friend constexpr bool operator==(AppId, AppId) noexcept = default;
};

struct SuiteId final {
    i32 value {0};

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }
    friend constexpr bool operator==(SuiteId, SuiteId) noexcept = default;
};

struct Dimensions final {
    i32 width {0};
    i32 height {0};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return width > 0 && height > 0;
    }
};

} // namespace phoneme
