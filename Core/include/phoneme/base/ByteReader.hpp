#pragma once

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "phoneme/base/Checked.hpp"

namespace phoneme {

class ByteReader final {
public:
    explicit ByteReader(std::span<const u8> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] usize position() const noexcept { return position_; }
    [[nodiscard]] usize remaining() const noexcept { return bytes_.size() - position_; }
    [[nodiscard]] bool empty() const noexcept { return remaining() == 0; }

    [[nodiscard]] Result<u8> read_u8() {
        auto span = read_span(1);
        if (!span) {
            return std::unexpected(span.error());
        }
        return span->front();
    }

    [[nodiscard]] Result<u16> read_be_u16() {
        auto span = read_span(2);
        if (!span) {
            return std::unexpected(span.error());
        }
        return static_cast<u16>((static_cast<u16>((*span)[0]) << 8U) |
                                static_cast<u16>((*span)[1]));
    }

    [[nodiscard]] Result<u32> read_be_u32() {
        auto span = read_span(4);
        if (!span) {
            return std::unexpected(span.error());
        }
        return (static_cast<u32>((*span)[0]) << 24U) |
               (static_cast<u32>((*span)[1]) << 16U) |
               (static_cast<u32>((*span)[2]) << 8U) |
               static_cast<u32>((*span)[3]);
    }

    [[nodiscard]] Result<u64> read_be_u64() {
        auto high = read_be_u32();
        if (!high) {
            return std::unexpected(high.error());
        }
        auto low = read_be_u32();
        if (!low) {
            return std::unexpected(low.error());
        }
        return (static_cast<u64>(*high) << 32U) | static_cast<u64>(*low);
    }

    [[nodiscard]] Result<u16> read_le_u16() {
        auto span = read_span(2);
        if (!span) {
            return std::unexpected(span.error());
        }
        return static_cast<u16>(static_cast<u16>((*span)[0]) |
                                (static_cast<u16>((*span)[1]) << 8U));
    }

    [[nodiscard]] Result<u32> read_le_u32() {
        auto span = read_span(4);
        if (!span) {
            return std::unexpected(span.error());
        }
        return static_cast<u32>((*span)[0]) |
               (static_cast<u32>((*span)[1]) << 8U) |
               (static_cast<u32>((*span)[2]) << 16U) |
               (static_cast<u32>((*span)[3]) << 24U);
    }

    [[nodiscard]] Result<std::span<const u8>> read_span(usize count) {
        auto end = checked_add(position_, count);
        if (!end || *end > bytes_.size()) {
            return fail(ErrorCode::out_of_range, "binary read exceeds input");
        }
        auto result = bytes_.subspan(position_, count);
        position_ = *end;
        return result;
    }

    [[nodiscard]] Result<std::string> read_string(usize count) {
        auto span = read_span(count);
        if (!span) {
            return std::unexpected(span.error());
        }
        return std::string(reinterpret_cast<const char*>(span->data()), span->size());
    }

    [[nodiscard]] Status skip(usize count) {
        auto span = read_span(count);
        if (!span) {
            return std::unexpected(span.error());
        }
        return {};
    }

    [[nodiscard]] Status seek(usize position) {
        if (position > bytes_.size()) {
            return fail(ErrorCode::out_of_range, "binary seek exceeds input");
        }
        position_ = position;
        return {};
    }

private:
    std::span<const u8> bytes_;
    usize position_ {0};
};

} // namespace phoneme
