#include <stromboli/stromboli_bindless.h>

static VkDescriptorPool bindlessDescriptorPools[2];
static VkDescriptorSet bindlessDescriptorSets[2];
static VkDescriptorSetLayout bindlessLayout;

static u32 currentImageDescriptorIndex = 0;
static u32 currentUniformBufferDescriptorIndex = 0;
static u32 currentStorageBufferDescriptorIndex = 0;
static u32 currentFrameIndex = 0;

void stromboliBindlessInit(StromboliContext* context) {
    // We do not support immutable samplers!
    VkDescriptorSetLayoutBinding bindings[] = {
        {.binding = 0, .descriptorCount = 512, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .stageFlags = VK_SHADER_STAGE_ALL},
        {.binding = 1, .descriptorCount = 512, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .stageFlags = VK_SHADER_STAGE_ALL},
        {.binding = 2, .descriptorCount = 512, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stageFlags = VK_SHADER_STAGE_ALL},
    };
    VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    //TODO: Also use flag VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
    createInfo.flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT;
    createInfo.bindingCount = ARRAY_COUNT(bindings);
    createInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(context->device, &createInfo, 0, &bindlessLayout);

    for(u32 i = 0; i < ARRAY_COUNT(bindlessDescriptorPools); ++i) {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512},
        };
        // We require either Vulkan 1.2 or VK_EXT_descriptor_indexing extension
        VkDescriptorPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
        createInfo.maxSets = 1;
        createInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
        createInfo.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(context->device, &createInfo, 0, &bindlessDescriptorPools[i]);

        VkDescriptorSetAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = bindlessDescriptorPools[i];
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &bindlessLayout;
        vkAllocateDescriptorSets(context->device, &allocateInfo, &bindlessDescriptorSets[i]);
    }
}

void stromboliBindlessShutdown(StromboliContext* context) {
    for(u32 i = 0; i < ARRAY_COUNT(bindlessDescriptorPools); ++i) {
        vkDestroyDescriptorPool(context->device, bindlessDescriptorPools[i], 0);
    }
    vkDestroyDescriptorSetLayout(context->device, bindlessLayout, 0);
}

void stromboliBindlessBeginFrame(u32 frameIndex) {
    currentImageDescriptorIndex = 0;
    currentUniformBufferDescriptorIndex = 0;
    currentStorageBufferDescriptorIndex = 0;
    currentFrameIndex = frameIndex;
}

u32 stromboliBindlessBindBuffer(StromboliBuffer* buffer) {
    
}

u32 stromboliBindlessBindImage(StromboliContext* context, StromboliImage* image, VkImageLayout layout) {
    //TODO: Should batch updates
    ASSERT(currentImageDescriptorIndex < 512);
    VkDescriptorImageInfo imageInfo = {
        .imageLayout = layout,
        .imageView = image->view,
        .sampler = 0, // No sampler for storage images
    };
    VkWriteDescriptorSet writes = {
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .dstArrayElement = currentImageDescriptorIndex++,
        .dstSet = bindlessDescriptorSets[currentFrameIndex],
        .dstBinding = 0, // Binding 0 for storage images
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(context->device, 1, &writes, 0, 0);
}

void stromboliBindlessBindDescriptorSet(StromboliPipeline pipeline, VkCommandBuffer commandBuffer, u32 frameIndex) {
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if(pipeline.type == STROMBOLI_PIPELINE_TYPE_COMPUTE) {
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    } else if(pipeline.type == STROMBOLI_PIPELINE_TYPE_RAYTRACING) {
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    }
    vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipeline.layout, 0, 1, &bindlessDescriptorSets[frameIndex], 0, 0);
}
