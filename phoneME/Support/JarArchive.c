#include "JarArchive.h"
#include <limits.h>
#include <zlib.h>

uint32_t phoneme_crc32(const uint8_t *data, size_t length) {
    return (uint32_t)crc32(0L, data, (uInt)length);
}

static int phoneme_inflate(
    const uint8_t *source,
    size_t source_length,
    uint8_t *destination,
    size_t destination_length,
    int window_bits
) {
    if (source_length > UINT_MAX || destination_length > UINT_MAX) {
        return -3;
    }

    z_stream stream = {0};
    stream.next_in = (Bytef *)source;
    stream.avail_in = (uInt)source_length;
    stream.next_out = destination;
    stream.avail_out = (uInt)destination_length;

    if (inflateInit2(&stream, window_bits) != Z_OK) {
        return -1;
    }

    int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (result != Z_STREAM_END || stream.total_out != destination_length) {
        return -2;
    }
    return 0;
}

int phoneme_inflate_raw(
    const uint8_t *source,
    size_t source_length,
    uint8_t *destination,
    size_t destination_length
) {
    return phoneme_inflate(
        source,
        source_length,
        destination,
        destination_length,
        -MAX_WBITS
    );
}

int phoneme_inflate_zlib(
    const uint8_t *source,
    size_t source_length,
    uint8_t *destination,
    size_t destination_length
) {
    return phoneme_inflate(
        source,
        source_length,
        destination,
        destination_length,
        MAX_WBITS
    );
}
