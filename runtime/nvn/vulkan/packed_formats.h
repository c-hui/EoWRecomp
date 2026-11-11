// Packed 16-bit NVN formats, shared with the GPU sampling regression test.
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

static inline VkFormat vk_packed_format(uint32_t format) {
    switch (format) {
    case 0x39: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
    case 0x3A: return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case 0x3B: return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
    // NVN names fields from the low bits: R 0..4, G 5..10, B 11..15.
    // Vulkan PACK16 names fields from the high bits, so the order is reversed.
    case 0x3C: return VK_FORMAT_B5G6R5_UNORM_PACK16;
    default: return VK_FORMAT_UNDEFINED;
    }
}
