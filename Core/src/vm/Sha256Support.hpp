#pragma once

#include <array>
#include <span>

#include "phoneme/base/Types.hpp"

namespace phoneme::vm::crypto {

constexpr std::array<u32, 64> kSha256Constants {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

[[nodiscard]] constexpr u32 rotate_right(u32 value, u32 count) noexcept {
    return (value >> count) | (value << (32U - count));
}

class Sha256 final {
public:
    void update(std::span<const u8> bytes) noexcept {
        for (const u8 byte : bytes) {
            block_[block_size_++] = byte;
            total_bytes_ += 1U;
            if (block_size_ == block_.size()) {
                transform(block_);
                block_size_ = 0U;
            }
        }
    }

    [[nodiscard]] std::array<u8, 32> finish() noexcept {
        const u64 bit_count = total_bytes_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            while (block_size_ < block_.size()) {
                block_[block_size_++] = 0U;
            }
            transform(block_);
            block_size_ = 0U;
        }
        while (block_size_ < 56U) {
            block_[block_size_++] = 0U;
        }
        for (usize index = 0U; index < 8U; ++index) {
            block_[63U - index] = static_cast<u8>(
                bit_count >> static_cast<u32>(index * 8U));
        }
        transform(block_);

        std::array<u8, 32> digest {};
        for (usize index = 0U; index < state_.size(); ++index) {
            digest[index * 4U] = static_cast<u8>(state_[index] >> 24U);
            digest[index * 4U + 1U] = static_cast<u8>(state_[index] >> 16U);
            digest[index * 4U + 2U] = static_cast<u8>(state_[index] >> 8U);
            digest[index * 4U + 3U] = static_cast<u8>(state_[index]);
        }
        return digest;
    }

private:
    void transform(const std::array<u8, 64>& block) noexcept {
        std::array<u32, 64> schedule {};
        for (usize index = 0U; index < 16U; ++index) {
            const usize offset = index * 4U;
            schedule[index] = (static_cast<u32>(block[offset]) << 24U) |
                              (static_cast<u32>(block[offset + 1U]) << 16U) |
                              (static_cast<u32>(block[offset + 2U]) << 8U) |
                              static_cast<u32>(block[offset + 3U]);
        }
        for (usize index = 16U; index < schedule.size(); ++index) {
            const u32 s0 = rotate_right(schedule[index - 15U], 7U) ^
                           rotate_right(schedule[index - 15U], 18U) ^
                           (schedule[index - 15U] >> 3U);
            const u32 s1 = rotate_right(schedule[index - 2U], 17U) ^
                           rotate_right(schedule[index - 2U], 19U) ^
                           (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 +
                              schedule[index - 7U] + s1;
        }

        u32 a = state_[0];
        u32 b = state_[1];
        u32 c = state_[2];
        u32 d = state_[3];
        u32 e = state_[4];
        u32 f = state_[5];
        u32 g = state_[6];
        u32 h = state_[7];

        for (usize index = 0U; index < schedule.size(); ++index) {
            const u32 sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                             rotate_right(e, 25U);
            const u32 choose = (e & f) ^ ((~e) & g);
            const u32 temporary1 = h + sum1 + choose +
                                   kSha256Constants[index] + schedule[index];
            const u32 sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                             rotate_right(a, 22U);
            const u32 majority = (a & b) ^ (a & c) ^ (b & c);
            const u32 temporary2 = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<u32, 8> state_ {
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
    };
    std::array<u8, 64> block_ {};
    usize block_size_ {0U};
    u64 total_bytes_ {0U};
};

[[nodiscard]] inline std::array<u8, 32> sha256(
    std::span<const u8> bytes) noexcept {
    Sha256 digest;
    digest.update(bytes);
    return digest.finish();
}

} // namespace phoneme::vm::crypto
