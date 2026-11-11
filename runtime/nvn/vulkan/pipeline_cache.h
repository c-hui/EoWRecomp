// Validate the driver-defined header before handing disk bytes to Vulkan. Like S2Arm's
// PipelineCacheFile, a cache for another GPU/driver is a miss, not driver input.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VK_PIPELINE_CACHE_MAX_BYTES (64u << 20)
static inline bool vk_pipeline_cache_valid(const void *data, size_t size, uint32_t vendor, uint32_t device,
                                           const uint8_t uuid[16]) {
    if (!data || size < 32 || size > VK_PIPELINE_CACHE_MAX_BYTES) return false;
    uint32_t header[4];
    memcpy(header, data, sizeof header);
    return header[0] == 32 && header[1] == 1 && header[2] == vendor && header[3] == device &&
           !memcmp((const uint8_t *)data + 16, uuid, 16);
}
