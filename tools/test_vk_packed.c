// Real GPU sampling of NVN packed texels, through the production format mapping.
// NVN RGB565 puts R in bits 0..4, G in 5..10 and B in 11..15.
#include "../runtime/nvn/vulkan/packed_formats.h"
#include <shaderc/shaderc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static struct { VkInstance inst; VkPhysicalDevice phys; VkPhysicalDeviceProperties props;
                VkDevice dev; VkQueue queue; } g_vk;
static int failures;
#define CHECK(x) do { VkResult r = (x); if (r != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #x, r); exit(2); } } while (0)

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    fprintf(stderr, "no compatible memory\n"); exit(2);
}

int main(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    CHECK(vkCreateInstance(&ic, NULL, &g_vk.inst));
    uint32_t n = 0;
    CHECK(vkEnumeratePhysicalDevices(g_vk.inst, &n, NULL));
    if (!n) return 2;
    VkPhysicalDevice *devices = calloc(n, sizeof *devices);
    CHECK(vkEnumeratePhysicalDevices(g_vk.inst, &n, devices));
    g_vk.phys = devices[0]; free(devices);
    vkGetPhysicalDeviceProperties(g_vk.phys, &g_vk.props);
    printf("GPU: %s\n", g_vk.props.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo qc = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0,
                                  .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qc};
    CHECK(vkCreateDevice(g_vk.phys, &dc, NULL, &g_vk.dev));
    vkGetDeviceQueue(g_vk.dev, 0, 0, &g_vk.queue);
    VkBuffer buffer;
    VkBufferCreateInfo bc = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 272,
                             .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    CHECK(vkCreateBuffer(g_vk.dev, &bc, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.dev, buffer, &req);
    VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
                               .memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkDeviceMemory bm;
    CHECK(vkAllocateMemory(g_vk.dev, &ma, NULL, &bm));
    CHECK(vkBindBufferMemory(g_vk.dev, buffer, bm, 0));
    uint8_t *mapped;
    CHECK(vkMapMemory(g_vk.dev, bm, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
    VkImage image;
    VkFormat format = vk_packed_format(0x3C);
    VkImageCreateInfo image_ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = format, .extent = {1,1,1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    CHECK(vkCreateImage(g_vk.dev, &image_ci, NULL, &image));
    vkGetImageMemoryRequirements(g_vk.dev, image, &req);
    ma.allocationSize = req.size; ma.memoryTypeIndex = memory_type(req.memoryTypeBits, 0);
    VkDeviceMemory im;
    CHECK(vkAllocateMemory(g_vk.dev, &ma, NULL, &im));
    CHECK(vkBindImageMemory(g_vk.dev, image, im, 0));
    VkImageViewCreateInfo vc = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view;
    CHECK(vkCreateImageView(g_vk.dev, &vc, NULL, &view));
    VkSamplerCreateInfo sc = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST, .maxLod = 0};
    VkSampler sampler;
    CHECK(vkCreateSampler(g_vk.dev, &sc, NULL, &sampler));
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo lc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings};
    VkDescriptorSetLayout layout;
    CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &lc, NULL, &layout));
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
        .poolSizeCount = 2, .pPoolSizes = sizes};
    VkDescriptorPool pool;
    CHECK(vkCreateDescriptorPool(g_vk.dev, &pc, NULL, &pool));
    VkDescriptorSetAllocateInfo sa = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout};
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(g_vk.dev, &sa, &set));
    VkDescriptorImageInfo ii = {sampler,view,VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo bi = {buffer,256,16};
    VkWriteDescriptorSet writes[2] = {
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,
         .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,
         .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi}};
    vkUpdateDescriptorSets(g_vk.dev, 2, writes, 0, NULL);
    const char *source = "#version 450\nlayout(local_size_x=1) in;layout(binding=0) uniform sampler2D t;"
        "layout(std430,binding=1) buffer Result{vec4 value;};void main(){value=texelFetch(t,ivec2(0),0);}";
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, source, strlen(source), shaderc_compute_shader,
        "packed.comp", "main", NULL);
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        fprintf(stderr,"%s\n",shaderc_result_get_error_message(result)); return 2;
    }
    VkShaderModuleCreateInfo mc = {.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=shaderc_result_get_length(result),.pCode=(const uint32_t *)shaderc_result_get_bytes(result)};
    VkShaderModule module;
    CHECK(vkCreateShaderModule(g_vk.dev,&mc,NULL,&module));
    VkPipelineLayoutCreateInfo plc = {.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};
    VkPipelineLayout pipeline_layout;
    CHECK(vkCreatePipelineLayout(g_vk.dev,&plc,NULL,&pipeline_layout));
    VkComputePipelineCreateInfo cp = {.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=pipeline_layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&cp,NULL,&pipeline));
    VkCommandPoolCreateInfo cmdp = {.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VkCommandPool command_pool;
    CHECK(vkCreateCommandPool(g_vk.dev,&cmdp,NULL,&command_pool));
    VkCommandBufferAllocateInfo ca = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(g_vk.dev,&ca,&command));
    const uint16_t words[3] = {0x001F,0x07E0,0xF800};
    for (int i=0;i<3;i++) {
        memcpy(mapped,words+i,2); memset(mapped+256,0,16);
        CHECK(vkResetCommandBuffer(command,0));
        VkCommandBufferBeginInfo begin = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkBeginCommandBuffer(command,&begin));
        VkImageMemoryBarrier barrier = {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout=i?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy = {.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={1,1,1}};
        vkCmdCopyBufferToImage(command,buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,NULL);
        vkCmdDispatch(command,1,1,1);
        VkMemoryBarrier host = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
        CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
        CHECK(vkQueueSubmit(g_vk.queue,1,&submit,VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(g_vk.queue));
        const float *value=(const float *)(mapped+256);
        bool ok=true;
        for (int c=0;c<4;c++) if (fabsf(value[c]-(c==i||c==3?1.0f:0.0f))>0.001f) ok=false;
        printf("%s RGB565 0x%04x -> %.3f %.3f %.3f %.3f\n",ok?"PASS":"FAIL",words[i],value[0],value[1],value[2],value[3]);
        if (!ok) failures++;
    }
    vkDestroyCommandPool(g_vk.dev,command_pool,NULL);
    vkDestroyPipeline(g_vk.dev,pipeline,NULL); vkDestroyPipelineLayout(g_vk.dev,pipeline_layout,NULL);
    vkDestroyShaderModule(g_vk.dev,module,NULL); vkDestroyDescriptorPool(g_vk.dev,pool,NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev,layout,NULL); vkDestroySampler(g_vk.dev,sampler,NULL);
    vkDestroyImageView(g_vk.dev,view,NULL); vkDestroyImage(g_vk.dev,image,NULL); vkFreeMemory(g_vk.dev,im,NULL);
    vkUnmapMemory(g_vk.dev,bm); vkDestroyBuffer(g_vk.dev,buffer,NULL); vkFreeMemory(g_vk.dev,bm,NULL);
    vkDestroyDevice(g_vk.dev,NULL); vkDestroyInstance(g_vk.inst,NULL);
    shaderc_result_release(result); shaderc_compiler_release(compiler);
    return failures?1:0;
}
