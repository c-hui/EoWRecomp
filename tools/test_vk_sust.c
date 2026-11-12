// Synthesize Maxwell SUST.P.B, compile the production translator's GLSL, and check actual GPU image writes.
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include "../runtime/nvn/maxwell/shader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { VkResult r = (x); if (r) { fprintf(stderr, "%s: %d\n", #x, r); exit(2); } } while (0)

static uint32_t mem_type(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(gpu, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    abort();
}

int main(void) {
    uint64_t code[16] = {0,
        0x0100000000070000ull, 0x0100000000070001ull,
        0x0100000000070018ull | (0x3e800000ull << 20), 0,
        0x0100000000070019ull | (0x3f000000ull << 20),
        0x010000000007001aull | (0x3f400000ull << 20),
        0x010000000007001bull | (0x3f800000ull << 20), 0,
        0x4c98000000070027ull | (2ull << 34) | (0x48ull << 20),
        0xeb20138600f70018ull, 0xe30000000007000full, 0, 0xe2400fffff87000full};
    ShaderKey key = {.glsl = true};
    ShaderInfo *si = maxwell_translate((const uint8_t *)code, sizeof code, STAGE_COMPUTE, &key);
    if (!si->msl) { fprintf(stderr, "SUST translation: %s\n", si->error); return 1; }
    assert(si->ntex == 1 && si->tex[0].is_image && si->tex[0].handle_offset == 0x120);
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t opt = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(opt, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, si->msl, strlen(si->msl), shaderc_compute_shader,
                                                                  "sust.comp", "main", opt);
    if (shaderc_result_get_compilation_status(result)) { fprintf(stderr, "%s", shaderc_result_get_error_message(result)); return 1; }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst; CHECK(vkCreateInstance(&ic, NULL, &inst));
    uint32_t n = 1; VkPhysicalDevice gpu; CHECK(vkEnumeratePhysicalDevices(inst, &n, &gpu));
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu, &props);
    VkPhysicalDeviceFeatures features; vkGetPhysicalDeviceFeatures(gpu, &features);
    assert(features.shaderStorageImageWriteWithoutFormat);
    features = (VkPhysicalDeviceFeatures){.shaderStorageImageWriteWithoutFormat = true};
    float priority = 1;
    VkDeviceQueueCreateInfo qc = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0,
                                  .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qc, .pEnabledFeatures = &features};
    VkDevice dev; CHECK(vkCreateDevice(gpu, &dc, NULL, &dev));
    VkQueue queue; vkGetDeviceQueue(dev, 0, 0, &queue);
    VkImageCreateInfo image_ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {1,1,1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage image; CHECK(vkCreateImage(dev, &image_ci, NULL, &image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, image, &req);
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
                                  .memoryTypeIndex = mem_type(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkDeviceMemory image_mem; CHECK(vkAllocateMemory(dev, &alloc, NULL, &image_mem)); CHECK(vkBindImageMemory(dev, image, image_mem, 0));
    VkImageViewCreateInfo vc = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image_ci.format, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; CHECK(vkCreateImageView(dev, &vc, NULL, &view));
    VkBufferCreateInfo bc = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 512,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
    VkBuffer buffer; CHECK(vkCreateBuffer(dev, &bc, NULL, &buffer)); vkGetBufferMemoryRequirements(dev, buffer, &req);
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = mem_type(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory buffer_mem; CHECK(vkAllocateMemory(dev, &alloc, NULL, &buffer_mem)); CHECK(vkBindBufferMemory(dev, buffer, buffer_mem, 0));
    uint8_t *mapped; CHECK(vkMapMemory(dev, buffer_mem, 0, VK_WHOLE_SIZE, 0, (void **)&mapped)); memset(mapped, 0, 512);
    VkDescriptorSetLayoutBinding bindings[] = {{2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {32,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo lc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings};
    VkDescriptorSetLayout layout; CHECK(vkCreateDescriptorSetLayout(dev, &lc, NULL, &layout));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
    VkDescriptorPoolCreateInfo pc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes};
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(dev, &pc, NULL, &pool));
    VkDescriptorSetAllocateInfo sa = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout};
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(dev, &sa, &set));
    VkDescriptorImageInfo ii = {VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo bi = {buffer,0,512};
    VkWriteDescriptorSet wr[] = {{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 32,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &ii},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 2,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bi}};
    vkUpdateDescriptorSets(dev, 2, wr, 0, NULL);
    VkShaderModuleCreateInfo mc = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = shaderc_result_get_length(result),
        .pCode = (const uint32_t *)shaderc_result_get_bytes(result)};
    VkShaderModule module; CHECK(vkCreateShaderModule(dev, &mc, NULL, &module));
    VkPipelineLayoutCreateInfo plc = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &layout};
    VkPipelineLayout pl; CHECK(vkCreatePipelineLayout(dev, &plc, NULL, &pl));
    VkComputePipelineCreateInfo cp = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout = pl,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"}};
    VkPipeline pipeline; CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cp, NULL, &pipeline));
    VkCommandPoolCreateInfo cmdpc = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
    VkCommandPool cmdpool; CHECK(vkCreateCommandPool(dev, &cmdpc, NULL, &cmdpool));
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(dev, &ca, &cmd));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier bar = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = vc.subresourceRange, .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,NULL,0,NULL,1,&bar);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline); vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&set,0,NULL);
    vkCmdDispatch(cmd,1,1,1);
    bar.oldLayout = bar.newLayout; bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&bar);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .imageExtent = {1,1,1}};
    vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);
    VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    bool ok = abs((int)mapped[0]-64)<=1 && abs((int)mapped[1]-128)<=1 && abs((int)mapped[2]-191)<=1 && mapped[3]==255;
    printf("%s SUST.P.B on %s: %u %u %u %u (want 64 128 191 255)\n",ok?"PASS":"FAIL",props.deviceName,mapped[0],mapped[1],mapped[2],mapped[3]);
    vkDestroyCommandPool(dev,cmdpool,NULL); vkDestroyPipeline(dev,pipeline,NULL); vkDestroyPipelineLayout(dev,pl,NULL);
    vkDestroyShaderModule(dev,module,NULL); vkDestroyDescriptorPool(dev,pool,NULL); vkDestroyDescriptorSetLayout(dev,layout,NULL);
    vkUnmapMemory(dev,buffer_mem); vkDestroyBuffer(dev,buffer,NULL); vkFreeMemory(dev,buffer_mem,NULL);
    vkDestroyImageView(dev,view,NULL); vkDestroyImage(dev,image,NULL); vkFreeMemory(dev,image_mem,NULL);
    vkDestroyDevice(dev,NULL); vkDestroyInstance(inst,NULL);
    shaderc_result_release(result); shaderc_compile_options_release(opt); shaderc_compiler_release(compiler); maxwell_free(si);
    return ok ? 0 : 1;
}
