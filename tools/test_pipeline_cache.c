#include "../runtime/nvn/vulkan/pipeline_cache.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint32_t blob[12] = {32, 1, 0x5143, 0x840, 1, 2, 3, 4};
    uint8_t uuid[16];
    memcpy(uuid, blob + 4, 16);
    assert(vk_pipeline_cache_valid(blob, sizeof blob, 0x5143, 0x840, uuid));
    for (size_t n = 0; n < 32; n++) assert(!vk_pipeline_cache_valid(blob, n, 0x5143, 0x840, uuid));
    assert(!vk_pipeline_cache_valid(blob, VK_PIPELINE_CACHE_MAX_BYTES + 1u, 0x5143, 0x840, uuid));
    assert(!vk_pipeline_cache_valid(NULL, 48, 0x5143, 0x840, uuid));
    for (int i = 0; i < 8; i++) {
        blob[i] ^= 1;
        assert(!vk_pipeline_cache_valid(blob, sizeof blob, 0x5143, 0x840, uuid));
        blob[i] ^= 1;
    }
    puts("pipeline cache: compatible header accepted; truncation, oversize and identity drift rejected");
}
