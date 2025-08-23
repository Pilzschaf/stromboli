#ifndef STROMBOLI_VMA_H
#define STROMBOLI_VMA_H

#include "stromboli.h"

#include <vk_mem_alloc.h>

typedef struct StromboliVmaAllocator {
    StromboliAllocationContext allocationContext;
    VmaAllocator vmaAllocator;
} StromboliVmaAllocator;

static VkDeviceMemory stromboliVmaAllocatorAllocate(StromboliAllocationContext* context, VkMemoryRequirements memoryRequirements, VkDeviceSize* outOffset, void** mapped, void** allocationData) {
    StromboliVmaAllocator* allocator = (StromboliVmaAllocator*)context;
    VmaAllocationInfo vmaAllocationInfo = {0};
    VmaAllocationCreateInfo createInfo = {0};
    VmaAllocation allocation = 0;
    vmaAllocateMemory(allocator->vmaAllocator, &memoryRequirements, &createInfo, &allocation, &vmaAllocationInfo);
    if(outOffset) {
        *outOffset = vmaAllocationInfo.offset;
    }
    if(allocationData) {
        *allocationData = allocation;
    }
    return vmaAllocationInfo.deviceMemory;
}

static void stromboliVmaAllocatorDeallocate(StromboliAllocationContext* context, VkDeviceMemory memory, void* allocationData) {
    StromboliVmaAllocator* allocator = (StromboliVmaAllocator*)context;
    VmaAllocation allocation = (VmaAllocation)allocationData;
    vmaFreeMemory(allocator->vmaAllocator, allocation);
}

StromboliVmaAllocator stromboliCreateVmaAllocator(StromboliContext* context, u32 memoryProperties, u64 size) {
    StromboliVmaAllocator result = {0};
    result.allocationContext.allocate = &stromboliVmaAllocatorAllocate;
	result.allocationContext.deallocate = &stromboliVmaAllocatorDeallocate;
    result.vmaAllocator = context->vmaAllocator;

    return result;
}

void stromboliFreeVmaAllocator(StromboliContext* context, StromboliVmaAllocator* allocator) {
    
}

#endif // STROMBOLI_VMA_H