#ifndef PB_SOURCE_H
#define PB_SOURCE_H

#include <stdint.h>

uint64_t pb_source_hash(
    const char *cloud_base_url,
    const char *device_serial
);

#endif
