#ifndef JarArchive_h
#define JarArchive_h

#include <stddef.h>
#include <stdint.h>

int phoneme_inflate_raw(
    const uint8_t *source,
    size_t source_length,
    uint8_t *destination,
    size_t destination_length
);

int phoneme_inflate_zlib(
    const uint8_t *source,
    size_t source_length,
    uint8_t *destination,
    size_t destination_length
);

uint32_t phoneme_crc32(const uint8_t *data, size_t length);

#endif
