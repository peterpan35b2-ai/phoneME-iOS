#include "phoneme/graphics/PngDecoder.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

namespace phoneme::graphics {
namespace {

constexpr std::array<u8, 8> kSignature {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
};
constexpr usize kMaximumCompressedBytes = 64U * 1024U * 1024U;
constexpr usize kMaximumInflatedBytes = 256U * 1024U * 1024U;

struct Header final {
    i32 width {0};
    i32 height {0};
    u8 bit_depth {0};
    u8 color_type {0};
    u8 interlace {0};
};

struct Pass final {
    i32 start_x {0};
    i32 start_y {0};
    i32 step_x {1};
    i32 step_y {1};
};

constexpr std::array<Pass, 7> kAdam7 {{
    {0, 0, 8, 8},
    {4, 0, 8, 8},
    {0, 4, 4, 8},
    {2, 0, 4, 4},
    {0, 2, 2, 4},
    {1, 0, 2, 2},
    {0, 1, 1, 2},
}};

[[nodiscard]] u32 read_be32(std::span<const u8> bytes, usize offset) {
    return (static_cast<u32>(bytes[offset]) << 24U) |
           (static_cast<u32>(bytes[offset + 1U]) << 16U) |
           (static_cast<u32>(bytes[offset + 2U]) << 8U) |
           static_cast<u32>(bytes[offset + 3U]);
}

[[nodiscard]] u16 read_be16(std::span<const u8> bytes, usize offset) {
    return static_cast<u16>((static_cast<u16>(bytes[offset]) << 8U) |
                            static_cast<u16>(bytes[offset + 1U]));
}

[[nodiscard]] bool valid_chunk_type(std::span<const u8> type) noexcept {
    if (type.size() != 4U) {
        return false;
    }
    for (const u8 character : type) {
        const bool uppercase = character >= static_cast<u8>('A') &&
                               character <= static_cast<u8>('Z');
        const bool lowercase = character >= static_cast<u8>('a') &&
                               character <= static_cast<u8>('z');
        if (!uppercase && !lowercase) {
            return false;
        }
    }
    return (type[2] & 0x20U) == 0U;
}

[[nodiscard]] Result<usize> checked_add(usize left,
                                        usize right,
                                        std::string_view message) {
    if (left > std::numeric_limits<usize>::max() - right) {
        return fail(ErrorCode::overflow, std::string(message));
    }
    return left + right;
}

[[nodiscard]] Result<usize> checked_multiply(usize left,
                                             usize right,
                                             std::string_view message) {
    if (right != 0U && left > std::numeric_limits<usize>::max() / right) {
        return fail(ErrorCode::overflow, std::string(message));
    }
    return left * right;
}

[[nodiscard]] Result<u8> channel_count(u8 color_type) {
    switch (color_type) {
    case 0:
        return 1U;
    case 2:
        return 3U;
    case 3:
        return 1U;
    case 4:
        return 2U;
    case 6:
        return 4U;
    default:
        return fail(ErrorCode::unsupported_feature,
                    "PNG color type is unsupported");
    }
}

[[nodiscard]] bool valid_bit_depth(u8 color_type, u8 bit_depth) noexcept {
    switch (color_type) {
    case 0:
        return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U ||
               bit_depth == 8U || bit_depth == 16U;
    case 2:
    case 4:
    case 6:
        return bit_depth == 8U || bit_depth == 16U;
    case 3:
        return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U ||
               bit_depth == 8U;
    default:
        return false;
    }
}

[[nodiscard]] i32 pass_extent(i32 full,
                              i32 start,
                              i32 step) noexcept {
    if (full <= start) {
        return 0;
    }
    return (full - start + step - 1) / step;
}

[[nodiscard]] Result<usize> row_bytes(i32 width,
                                      u8 channels,
                                      u8 bit_depth) {
    if (width < 0) {
        return fail(ErrorCode::invalid_argument,
                    "PNG pass width is negative");
    }
    auto sample_count = checked_multiply(static_cast<usize>(width),
                                         channels,
                                         "PNG row sample count overflows");
    if (!sample_count) return std::unexpected(sample_count.error());
    auto bit_count = checked_multiply(*sample_count,
                                      bit_depth,
                                      "PNG row bit count overflows");
    if (!bit_count) return std::unexpected(bit_count.error());
    auto rounded = checked_add(*bit_count, 7U,
                               "PNG row size overflows");
    if (!rounded) return std::unexpected(rounded.error());
    return *rounded / 8U;
}

[[nodiscard]] Result<usize> expected_inflated_size(const Header& header,
                                                   u8 channels) {
    usize total = 0;
    const auto add_pass = [&](Pass pass) -> Status {
        const i32 width = pass_extent(header.width, pass.start_x, pass.step_x);
        const i32 height = pass_extent(header.height, pass.start_y, pass.step_y);
        if (width == 0 || height == 0) return {};
        auto bytes = row_bytes(width, channels, header.bit_depth);
        if (!bytes) return std::unexpected(bytes.error());
        auto row_with_filter = checked_add(*bytes, 1U,
                                           "PNG filtered row size overflows");
        if (!row_with_filter) {
            return std::unexpected(row_with_filter.error());
        }
        auto pass_size = checked_multiply(*row_with_filter,
                                          static_cast<usize>(height),
                                          "PNG pass size overflows");
        if (!pass_size) return std::unexpected(pass_size.error());
        auto combined = checked_add(total, *pass_size,
                                    "PNG inflated size overflows");
        if (!combined) return std::unexpected(combined.error());
        total = *combined;
        return {};
    };

    if (header.interlace == 0U) {
        auto status = add_pass(Pass {});
        if (!status) return std::unexpected(status.error());
    } else {
        for (const Pass pass : kAdam7) {
            auto status = add_pass(pass);
            if (!status) return std::unexpected(status.error());
        }
    }
    if (total > kMaximumInflatedBytes) {
        return fail(ErrorCode::overflow,
                    "PNG decompressed data exceeds the graphics budget");
    }
    return total;
}

[[nodiscard]] u8 paeth(u8 left, u8 above, u8 upper_left) noexcept {
    const i32 prediction = static_cast<i32>(left) + above - upper_left;
    const i32 left_distance = std::abs(prediction - left);
    const i32 above_distance = std::abs(prediction - above);
    const i32 diagonal_distance = std::abs(prediction - upper_left);
    if (left_distance <= above_distance &&
        left_distance <= diagonal_distance) {
        return left;
    }
    if (above_distance <= diagonal_distance) {
        return above;
    }
    return upper_left;
}

[[nodiscard]] Status unfilter_row(u8 filter,
                                  std::span<const u8> encoded,
                                  std::span<const u8> previous,
                                  std::span<u8> output,
                                  usize bytes_per_pixel) {
    if (encoded.size() != output.size() ||
        (!previous.empty() && previous.size() != output.size())) {
        return fail(ErrorCode::internal_error,
                    "PNG unfilter row sizes do not match");
    }
    for (usize index = 0; index < encoded.size(); ++index) {
        const u8 left = index >= bytes_per_pixel
            ? output[index - bytes_per_pixel]
            : 0U;
        const u8 above = previous.empty() ? 0U : previous[index];
        const u8 upper_left = previous.empty() || index < bytes_per_pixel
            ? 0U
            : previous[index - bytes_per_pixel];
        u8 predictor = 0U;
        switch (filter) {
        case 0:
            predictor = 0U;
            break;
        case 1:
            predictor = left;
            break;
        case 2:
            predictor = above;
            break;
        case 3:
            predictor = static_cast<u8>(
                (static_cast<u16>(left) + above) / 2U);
            break;
        case 4:
            predictor = paeth(left, above, upper_left);
            break;
        default:
            return fail(ErrorCode::malformed_archive,
                        "PNG row uses an invalid filter type");
        }
        output[index] = static_cast<u8>(encoded[index] + predictor);
    }
    return {};
}

[[nodiscard]] u16 sample(std::span<const u8> row,
                         usize sample_index,
                         u8 bit_depth) noexcept {
    if (bit_depth == 16U) {
        return static_cast<u16>(
            (static_cast<u16>(row[sample_index * 2U]) << 8U) |
            row[sample_index * 2U + 1U]);
    }
    if (bit_depth == 8U) {
        return row[sample_index];
    }
    const usize bit_offset = sample_index * bit_depth;
    const usize byte_index = bit_offset / 8U;
    const usize within_byte = bit_offset % 8U;
    const u8 shift = static_cast<u8>(8U - bit_depth - within_byte);
    const u8 mask = static_cast<u8>((1U << bit_depth) - 1U);
    return static_cast<u16>((row[byte_index] >> shift) & mask);
}

[[nodiscard]] u8 scale_sample(u16 value, u8 bit_depth) noexcept {
    if (bit_depth == 16U) {
        return static_cast<u8>(value >> 8U);
    }
    if (bit_depth == 8U) {
        return static_cast<u8>(value);
    }
    const u16 maximum = static_cast<u16>((1U << bit_depth) - 1U);
    return static_cast<u8>((static_cast<u32>(value) * 255U +
                            maximum / 2U) /
                           maximum);
}

[[nodiscard]] Result<Pixel> decode_pixel(
    std::span<const u8> row,
    usize pixel_index,
    const Header& header,
    std::span<const Pixel> palette,
    std::span<const u8> palette_alpha,
    std::optional<u16> transparent_gray,
    std::optional<std::array<u16, 3>> transparent_rgb) {
    const u8 depth = header.bit_depth;
    switch (header.color_type) {
    case 0: {
        const u16 gray = sample(row, pixel_index, depth);
        const u8 value = scale_sample(gray, depth);
        const u8 alpha_value = transparent_gray.has_value() &&
                               gray == *transparent_gray
            ? 0U
            : 255U;
        return argb(alpha_value, value, value, value);
    }
    case 2: {
        const usize first = pixel_index * 3U;
        const u16 red_value = sample(row, first, depth);
        const u16 green_value = sample(row, first + 1U, depth);
        const u16 blue_value = sample(row, first + 2U, depth);
        const u8 alpha_value = transparent_rgb.has_value() &&
                               red_value == (*transparent_rgb)[0] &&
                               green_value == (*transparent_rgb)[1] &&
                               blue_value == (*transparent_rgb)[2]
            ? 0U
            : 255U;
        return argb(alpha_value,
                    scale_sample(red_value, depth),
                    scale_sample(green_value, depth),
                    scale_sample(blue_value, depth));
    }
    case 3: {
        const u16 palette_index = sample(row, pixel_index, depth);
        // A number of commercial J2ME assets contain isolated 8-bit palette
        // indices beyond the short PLTE table. Reference handset decoders and
        // ImageIO tolerate these entries as opaque black; rejecting the whole
        // image prevents the accompanying bitmap-font metrics from loading.
        Pixel value = palette_index < palette.size()
            ? palette[palette_index]
            : argb(255U, 0U, 0U, 0U);
        if (palette_index < palette_alpha.size()) {
            value = (value & 0x00FFFFFFU) |
                    (static_cast<Pixel>(palette_alpha[palette_index]) << 24U);
        }
        return value;
    }
    case 4: {
        const usize first = pixel_index * 2U;
        const u8 gray = scale_sample(sample(row, first, depth), depth);
        const u8 alpha_value = scale_sample(
            sample(row, first + 1U, depth), depth);
        return argb(alpha_value, gray, gray, gray);
    }
    case 6: {
        const usize first = pixel_index * 4U;
        return argb(scale_sample(sample(row, first + 3U, depth), depth),
                    scale_sample(sample(row, first, depth), depth),
                    scale_sample(sample(row, first + 1U, depth), depth),
                    scale_sample(sample(row, first + 2U, depth), depth));
    }
    default:
        return fail(ErrorCode::unsupported_feature,
                    "PNG color type is unsupported");
    }
}

} // namespace

Result<Image> decode_png(std::span<const u8> bytes) {
    if (bytes.size() < kSignature.size() ||
        !std::equal(kSignature.begin(), kSignature.end(), bytes.begin())) {
        return fail(ErrorCode::malformed_archive,
                    "image data does not have a PNG signature");
    }

    Header header;
    bool has_header = false;
    bool has_end = false;
    bool has_idat = false;
    bool has_palette = false;
    bool has_transparency = false;
    bool idat_ended = false;
    std::vector<u8> compressed;
    std::vector<Pixel> palette;
    std::vector<u8> palette_alpha;
    std::optional<u16> transparent_gray;
    std::optional<std::array<u16, 3>> transparent_rgb;

    usize cursor = kSignature.size();
    while (cursor < bytes.size()) {
        if (bytes.size() - cursor < 12U) {
            return fail(ErrorCode::malformed_archive,
                        "PNG chunk header is truncated");
        }
        const u32 chunk_length_u32 = read_be32(bytes, cursor);
        const usize chunk_length = chunk_length_u32;
        const usize type_offset = cursor + 4U;
        const usize data_offset = cursor + 8U;
        auto data_end = checked_add(data_offset,
                                    chunk_length,
                                    "PNG chunk length overflows");
        if (!data_end) return std::unexpected(data_end.error());
        auto chunk_end = checked_add(*data_end, 4U,
                                     "PNG chunk CRC offset overflows");
        if (!chunk_end) return std::unexpected(chunk_end.error());
        if (*chunk_end > bytes.size()) {
            return fail(ErrorCode::malformed_archive,
                        "PNG chunk payload is truncated");
        }
        const std::span<const u8> type = bytes.subspan(type_offset, 4U);
        if (!valid_chunk_type(type)) {
            return fail(ErrorCode::malformed_archive,
                        "PNG chunk type is invalid");
        }
        const std::span<const u8> data = bytes.subspan(data_offset,
                                                       chunk_length);
        const u32 expected_crc = read_be32(bytes, *data_end);
        uLong actual_crc = crc32(0L, Z_NULL, 0);
        actual_crc = crc32(actual_crc,
                           reinterpret_cast<const Bytef*>(type.data()),
                           static_cast<uInt>(type.size()));
        if (data.size() > std::numeric_limits<uInt>::max()) {
            return fail(ErrorCode::overflow,
                        "PNG chunk is too large for zlib CRC validation");
        }
        actual_crc = crc32(actual_crc,
                           reinterpret_cast<const Bytef*>(data.data()),
                           static_cast<uInt>(data.size()));
        if (static_cast<u32>(actual_crc) != expected_crc) {
            return fail(ErrorCode::checksum_mismatch,
                        "PNG chunk CRC does not match");
        }

        const std::string_view type_name(
            reinterpret_cast<const char*>(type.data()), 4U);
        if (type_name == "IHDR") {
            if (has_header || chunk_length != 13U || cursor != 8U) {
                return fail(ErrorCode::malformed_archive,
                            "PNG IHDR is missing, duplicated or malformed");
            }
            const u32 width = read_be32(data, 0U);
            const u32 height = read_be32(data, 4U);
            if (width == 0U || height == 0U ||
                width > static_cast<u32>(std::numeric_limits<i32>::max()) ||
                height > static_cast<u32>(std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "PNG dimensions are invalid or too large");
            }
            header.width = static_cast<i32>(width);
            header.height = static_cast<i32>(height);
            header.bit_depth = data[8U];
            header.color_type = data[9U];
            if (data[10U] != 0U || data[11U] != 0U || data[12U] > 1U) {
                return fail(ErrorCode::unsupported_feature,
                            "PNG compression, filter or interlace method is unsupported");
            }
            header.interlace = data[12U];
            if (!valid_bit_depth(header.color_type, header.bit_depth)) {
                return fail(ErrorCode::unsupported_feature,
                            "PNG bit depth/color type combination is unsupported");
            }
            auto pixel_budget = validated_pixel_count(header.width,
                                                      header.height);
            if (!pixel_budget) {
                return std::unexpected(pixel_budget.error());
            }
            has_header = true;
        } else if (type_name == "PLTE") {
            const usize entry_count = data.size() / 3U;
            const usize maximum_entries = header.color_type == 3U
                ? static_cast<usize>(1U << header.bit_depth)
                : 256U;
            if (!has_header || has_idat || has_palette || data.empty() ||
                data.size() % 3U != 0U || data.size() > 768U ||
                entry_count > maximum_entries ||
                header.color_type == 0U || header.color_type == 4U) {
                return fail(ErrorCode::malformed_archive,
                            "PNG PLTE chunk is malformed or out of order");
            }
            palette.reserve(entry_count);
            for (usize index = 0; index < data.size(); index += 3U) {
                palette.push_back(argb(255U,
                                       data[index],
                                       data[index + 1U],
                                       data[index + 2U]));
            }
            has_palette = true;
        } else if (type_name == "tRNS") {
            if (!has_header || has_idat || has_transparency) {
                return fail(ErrorCode::malformed_archive,
                            "PNG tRNS chunk is duplicated or out of order");
            }
            if (header.color_type == 0U && data.size() == 2U) {
                const u16 value = read_be16(data, 0U);
                const u32 maximum = header.bit_depth == 16U
                    ? 65535U
                    : (1U << header.bit_depth) - 1U;
                if (value > maximum) {
                    return fail(ErrorCode::malformed_archive,
                                "PNG grayscale tRNS sample exceeds bit depth");
                }
                transparent_gray = value;
            } else if (header.color_type == 2U && data.size() == 6U) {
                const std::array<u16, 3> values {
                    read_be16(data, 0U),
                    read_be16(data, 2U),
                    read_be16(data, 4U),
                };
                if (header.bit_depth == 8U &&
                    (values[0] > 255U || values[1] > 255U ||
                     values[2] > 255U)) {
                    return fail(ErrorCode::malformed_archive,
                                "PNG truecolor tRNS sample exceeds bit depth");
                }
                transparent_rgb = values;
            } else if (header.color_type == 3U && has_palette &&
                       data.size() <= palette.size()) {
                palette_alpha.assign(data.begin(), data.end());
            } else {
                return fail(ErrorCode::malformed_archive,
                            "PNG tRNS chunk is invalid for its color type");
            }
            has_transparency = true;
        } else if (type_name == "IDAT") {
            if (!has_header || idat_ended ||
                (header.color_type == 3U && !has_palette)) {
                return fail(ErrorCode::malformed_archive,
                            "PNG IDAT is out of order");
            }
            auto combined = checked_add(compressed.size(), data.size(),
                                        "PNG compressed stream overflows");
            if (!combined) return std::unexpected(combined.error());
            if (*combined > kMaximumCompressedBytes) {
                return fail(ErrorCode::overflow,
                            "PNG compressed stream exceeds the graphics budget");
            }
            compressed.insert(compressed.end(), data.begin(), data.end());
            has_idat = true;
        } else if (type_name == "IEND") {
            if (!has_header || !has_idat || has_end || chunk_length != 0U) {
                return fail(ErrorCode::malformed_archive,
                            "PNG IEND is malformed or appears too early");
            }
            has_end = true;
            idat_ended = true;
            cursor = *chunk_end;
            break;
        } else if ((type[0] & 0x20U) == 0U) {
            return fail(ErrorCode::unsupported_feature,
                        "PNG contains an unsupported critical chunk");
        } else if (has_idat) {
            idat_ended = true;
        }
        cursor = *chunk_end;
    }

    if (!has_header || !has_idat || !has_end) {
        return fail(ErrorCode::malformed_archive,
                    "PNG is missing IHDR, IDAT or IEND");
    }
    // Reference phone decoders stop at IEND. Legacy JAR packers frequently
    // append alignment bytes or concatenate asset payloads inside a larger
    // byte array, and Image.createImage(byte[], offset, length) must still
    // decode the first complete PNG. All chunks through IEND remain fully
    // length/CRC validated above.
    if (header.color_type == 3U && palette.empty()) {
        return fail(ErrorCode::malformed_archive,
                    "indexed PNG is missing its palette");
    }

    auto channels = channel_count(header.color_type);
    if (!channels) return std::unexpected(channels.error());
    auto inflated_size = expected_inflated_size(header, *channels);
    if (!inflated_size) return std::unexpected(inflated_size.error());
    std::vector<u8> inflated(*inflated_size);
    uLongf destination_length = static_cast<uLongf>(inflated.size());
    if (compressed.size() > std::numeric_limits<uLong>::max() ||
        inflated.size() > std::numeric_limits<uLongf>::max()) {
        return fail(ErrorCode::overflow,
                    "PNG stream exceeds zlib addressable size");
    }
    const int zlib_status = uncompress(
        reinterpret_cast<Bytef*>(inflated.data()),
        &destination_length,
        reinterpret_cast<const Bytef*>(compressed.data()),
        static_cast<uLong>(compressed.size()));
    if (zlib_status != Z_OK || destination_length != inflated.size()) {
        return fail(ErrorCode::malformed_archive,
                    "PNG IDAT stream cannot be decompressed exactly");
    }

    auto output_count = validated_pixel_count(header.width, header.height);
    if (!output_count) return std::unexpected(output_count.error());
    std::vector<Pixel> output(*output_count, 0U);
    usize input_cursor = 0;
    const usize bits_per_pixel = static_cast<usize>(*channels) *
                                 header.bit_depth;
    const usize filter_bytes_per_pixel = std::max<usize>(
        1U, (bits_per_pixel + 7U) / 8U);

    const auto decode_pass = [&](Pass pass) -> Status {
        const i32 pass_width = pass_extent(header.width,
                                           pass.start_x,
                                           pass.step_x);
        const i32 pass_height = pass_extent(header.height,
                                            pass.start_y,
                                            pass.step_y);
        if (pass_width == 0 || pass_height == 0) return {};
        auto bytes_per_row = row_bytes(pass_width,
                                       *channels,
                                       header.bit_depth);
        if (!bytes_per_row) return std::unexpected(bytes_per_row.error());
        std::vector<u8> previous(*bytes_per_row, 0U);
        std::vector<u8> current(*bytes_per_row, 0U);
        bool has_previous = false;
        for (i32 row = 0; row < pass_height; ++row) {
            if (input_cursor >= inflated.size() ||
                inflated.size() - input_cursor - 1U < *bytes_per_row) {
                return fail(ErrorCode::malformed_archive,
                            "PNG decompressed rows are truncated");
            }
            const u8 filter = inflated[input_cursor++];
            const std::span<const u8> encoded =
                std::span<const u8>(inflated).subspan(input_cursor,
                                                      *bytes_per_row);
            input_cursor += *bytes_per_row;
            auto unfiltered = unfilter_row(
                filter,
                encoded,
                has_previous ? std::span<const u8>(previous)
                             : std::span<const u8> {},
                current,
                filter_bytes_per_pixel);
            if (!unfiltered) return unfiltered;
            for (i32 column = 0; column < pass_width; ++column) {
                auto pixel_value = decode_pixel(current,
                                                static_cast<usize>(column),
                                                header,
                                                palette,
                                                palette_alpha,
                                                transparent_gray,
                                                transparent_rgb);
                if (!pixel_value) {
                    return std::unexpected(pixel_value.error());
                }
                const i32 output_x = pass.start_x + column * pass.step_x;
                const i32 output_y = pass.start_y + row * pass.step_y;
                output[static_cast<usize>(output_y) *
                           static_cast<usize>(header.width) +
                       static_cast<usize>(output_x)] = *pixel_value;
            }
            previous.swap(current);
            std::fill(current.begin(), current.end(), 0U);
            has_previous = true;
        }
        return {};
    };

    if (header.interlace == 0U) {
        auto decoded = decode_pass(Pass {});
        if (!decoded) return std::unexpected(decoded.error());
    } else {
        for (const Pass pass : kAdam7) {
            auto decoded = decode_pass(pass);
            if (!decoded) return std::unexpected(decoded.error());
        }
    }
    if (input_cursor != inflated.size()) {
        return fail(ErrorCode::malformed_archive,
                    "PNG decompressed stream has trailing row data");
    }
    return Image::create_immutable_owned(
        header.width, header.height, std::move(output));
}

} // namespace phoneme::graphics
