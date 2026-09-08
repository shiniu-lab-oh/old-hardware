#include "pb_source.h"

#include <stddef.h>

#define PB_SOURCE_HASH_OFFSET UINT64_C(14695981039346656037)
#define PB_SOURCE_HASH_PRIME UINT64_C(1099511628211)

uint64_t pb_source_hash(const char *cloud_base_url, const char *device_serial)
{
    uint64_t hash = PB_SOURCE_HASH_OFFSET;
    const char *parts[] = {cloud_base_url, device_serial};

    for (size_t part = 0; part < sizeof(parts) / sizeof(parts[0]); ++part) {
        for (const unsigned char *cursor = (const unsigned char *)parts[part];
             *cursor != '\0';
             ++cursor) {
            hash ^= *cursor;
            hash *= PB_SOURCE_HASH_PRIME;
        }
        hash ^= 0xffU;
        hash *= PB_SOURCE_HASH_PRIME;
    }

    return hash;
}
