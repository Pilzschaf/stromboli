#include <stromboli/stromboli_frame_descriptor.h>
#include <grounded/memory/grounded_arena.h>

//TODO: How to do the memory management?
// Own growable memory arena?
// Next frame arena is somewhat difficult as we still need the data to release the old pools!
// Maybe we can also use a single pool per frame and either live with a static size or employ some kind of grow scheme for it
// Grow scheme for a single pool is difficult as we already have allocated sets when we recognize that we need more space!
// Reallocating descriptor sets mid-frame does not sound like a good idea!
// Maybe use the next frame arena in such a case to create additional overflow pool(s)

struct DescriptorPoolData {
    VkDescriptorPool pool;
    u32 usedSetCount;
    u32 usedStorageImageCount;
    u32 usedUniformBufferCount;
    u32 usedStorageBufferCount;
};

struct DescriptorPoolNode {
    struct DescriptorPoolNode* next;
    struct DescriptorPoolData pool;
};

struct StromboliFrameDescriptor {
    struct DescriptorPoolData framePools[2];
    struct DescriptorPoolNode overflowSentinel;
    struct DescriptorPoolNode deleteQueueSentinel;

    //u32 activeFrameIndex;
} stromboliFrameDescriptor;

void stromboliFrameDescriptorInit(StromboliContext* context) {
    for(u32 i = 0; i < ARRAY_COUNT(stromboliFrameDescriptor.framePools); ++i) {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512},
        };
        VkDescriptorPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        //TODO: Look at VK_DESCRIPTOR_POOL_CREATE_ALLOW_OVERALLOCATION_SETS_BIT_NV
        //TODO: Also look at VK_DESCRIPTOR_POOL_CREATE_ALLOW_OVERALLOCATION_POOLS_BIT_NV
        // Both extensions should allow us to use a single descriptor pool per frame for everything on nvidia. No on demand creation of additional sets and pools necessary! But does it have runtime costs?
        createInfo.maxSets = 256;
        createInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
        createInfo.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(context->device, &createInfo, 0, &stromboliFrameDescriptor.framePools[i].pool);
    }
    // This is for per frame descriptor sets
    /*for(u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512},
        };
        VkDescriptorPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        createInfo.maxSets = 256;
        createInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
        createInfo.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(context->device, &createInfo, 0, &stromboliFrameDescriptor.frameDescriptorPools[i]);
    }*/
}

void stromboliFrameDescriptorIsActive() {
    bool result = stromboliFrameDescriptor.framePools[0].pool != 0;
    return result;
}

void stromboliFrameDescriptorShutdown(StromboliContext* context) {
    for(u32 i = 0; i < ARRAY_COUNT(stromboliFrameDescriptor.framePools); ++i) {
        vkDestroyDescriptorPool(context->device, stromboliFrameDescriptor.framePools[i].pool, 0);
    }
    //TODO: Handle overflow and delete queue nodes
    /*for(int i = 0; i < ARRAY_COUNT(stromboliFrameDescriptor.pools); ++i) {
        struct DescriptorPoolNode* nodeToDelete = stromboliFrameDescriptor.pools[i].next;
        while(nodeToDelete) {
            vkDestroyDescriptorPool(context->device,nodeToDelete->pool, 0);
            nodeToDelete = nodeToDelete->next;
        }
    }*/
}

void stromboliFrameDescriptorBeginNewFrame(StromboliContext* context, MemoryArena* nextFrameArena, u32 frameIndex) {
    // We only allow alternating frame indices
    ASSERT(frameIndex < 2);
    struct DescriptorPoolNode* nodeToDelete = stromboliFrameDescriptor.deleteQueueSentinel.next;
    while(nodeToDelete) {
        vkDestroyDescriptorPool(context->device, nodeToDelete->pool.pool, 0);
        nodeToDelete = nodeToDelete->next;
        //TODO: Here we have to enlarge our normal framePool
        ASSERT(false);
    }

    if(stromboliFrameDescriptor.overflowSentinel.next) {
        // We have to copy them over to the delete queue. It is important to reallocate them as their allocation will be invalid in the next frame
        ASSERT(false);
    }

    vkResetDescriptorPool(context->device, stromboliFrameDescriptor.framePools[frameIndex].pool, 0);
}

VkDescriptorSet stromboliGetDescriptorSetForPipeline(StromboliContext* context, StromboliPipeline* pipeline, u32 setIndex, u32 frameIndex) {
    //TODO: Count up actual descriptor use
    VkDescriptorSet result = {0};
    VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    //TODO: For now we assume that the pool has still space left
    allocateInfo.descriptorPool = stromboliFrameDescriptor.framePools[frameIndex].pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &pipeline->descriptorLayouts[setIndex];
    VkResult error = vkAllocateDescriptorSets(context->device, &allocateInfo, &result);
    ASSERT(error == VK_SUCCESS);
    return result;
}

void stromboliUpdateAndBindDescriptorSetForPipeline(StromboliContext* context, VkCommandBuffer commandBuffer, StromboliPipeline* pipeline, u32 setIndex, u32 frameIndex, StromboliDescriptorInfo* descriptors) {
    VkDescriptorSet descriptorSet = stromboliGetDescriptorSetForPipeline(context, pipeline, setIndex, frameIndex);
    
    vkUpdateDescriptorSetWithTemplate(context->device, descriptorSet, pipeline->updateTemplates[setIndex], descriptors);

    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if(pipeline->type == STROMBOLI_PIPELINE_TYPE_COMPUTE) {
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    } else if(pipeline->type == STROMBOLI_PIPELINE_TYPE_RAYTRACING) {
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    }
    vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipeline->layout, setIndex, 1, &descriptorSet, 0, 0);
}