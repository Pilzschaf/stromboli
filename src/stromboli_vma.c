#include <stromboli/stromboli_vma.h>

#include <vk_mem_alloc.h>

struct StromboliVmaAllocator {
    StromboliAllocationContext allocationContext;
    VmaAllocator vmaAllocator;
} stromboliVmaAllocator;

static VkDeviceMemory stromboliVmaAllocatorAllocate(StromboliAllocationContext* context, VkMemoryRequirements memoryRequirements, VkMemoryPropertyFlags memoryProperties, VkDeviceSize* outOffset, void** mapped, void** allocationData) {
    StromboliVmaAllocator* allocator = (StromboliVmaAllocator*)context;
    VmaAllocationInfo vmaAllocationInfo = {0};
    VmaAllocationCreateInfo createInfo = {0};
    // We can always set mapped bit as it is simply ignored on devices where device local is not mappable eg. dedicated GPUs
    createInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    createInfo.requiredFlags = memoryProperties;
    VmaAllocation allocation = 0;
    vmaAllocateMemory(allocator->vmaAllocator, &memoryRequirements, &createInfo, &allocation, &vmaAllocationInfo);
    if(outOffset) {
        *outOffset = vmaAllocationInfo.offset;
    }
    if(allocationData) {
        *allocationData = allocation;
    }
    if(mapped) {
        *mapped = vmaAllocationInfo.pMappedData;
    }
    return vmaAllocationInfo.deviceMemory;
}

static void stromboliVmaAllocatorDeallocate(StromboliAllocationContext* context, VkDeviceMemory memory, void* allocationData) {
    StromboliVmaAllocator* allocator = (StromboliVmaAllocator*)context;
    VmaAllocation allocation = (VmaAllocation)allocationData;
    vmaFreeMemory(allocator->vmaAllocator, allocation);
}

bool stromboliVmaInit(StromboliContext* stromboli) {
    bool result = false;
    ASSUME(!stromboliVmaAllocator.vmaAllocator) {
        stromboliVmaAllocator.allocationContext.allocate = &stromboliVmaAllocatorAllocate;
	    stromboliVmaAllocator.allocationContext.deallocate = &stromboliVmaAllocatorDeallocate;
        
        VmaVulkanFunctions vmaFunctions = {0};
        vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        vmaFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vmaFunctions.vkAllocateMemory = vkAllocateMemory;
        vmaFunctions.vkFreeMemory = vkFreeMemory;
        vmaFunctions.vkMapMemory = vkMapMemory;
        vmaFunctions.vkUnmapMemory = vkUnmapMemory;
        vmaFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
        vmaFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
        vmaFunctions.vkBindBufferMemory = vkBindBufferMemory;
        vmaFunctions.vkBindImageMemory = vkBindImageMemory;
        vmaFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
        vmaFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
        vmaFunctions.vkCreateBuffer = vkCreateBuffer;
        vmaFunctions.vkDestroyBuffer = vkDestroyBuffer;
        vmaFunctions.vkCreateImage = vkCreateImage;
        vmaFunctions.vkDestroyImage = vkDestroyImage;
        vmaFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
        vmaFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
        vmaFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
        vmaFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
        vmaFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
        vmaFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
        vmaFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;
        VmaAllocatorCreateInfo createInfo = {0};
        //TODO: Check does not work as feature must be explicitly enabled upon device creation. Must save this in stromboli context
        /*if(vkGetBufferDeviceAddressKHR || vkGetBufferDeviceAddress || vkGetBufferDeviceAddressEXT) {
            createInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        }*/
        createInfo.vulkanApiVersion = stromboli->apiVersion;
        createInfo.physicalDevice = stromboli->physicalDevice;
        createInfo.device = stromboli->device;
        createInfo.instance = stromboli->instance;
        createInfo.pVulkanFunctions = &vmaFunctions;
        result = (vmaCreateAllocator(&createInfo, &stromboliVmaAllocator.vmaAllocator) == VK_SUCCESS);
    }
    return result;
}

void stromboliVmaShutdown() {
    ASSUME(stromboliVmaAllocator.vmaAllocator) {
        vmaDestroyAllocator(stromboliVmaAllocator.vmaAllocator);
        stromboliVmaAllocator.vmaAllocator = 0;
    }
}

StromboliAllocationContext* stromboliGetVmaAllocator() {
    StromboliAllocationContext* result = 0;
    ASSUME(stromboliVmaAllocator.vmaAllocator) {
        result = &stromboliVmaAllocator.allocationContext;
    }
    return result;
}

struct VmaAllocator_T* stromboliGetInternalVmaAllocator() {
    VmaAllocator result = 0;
    ASSUME(stromboliVmaAllocator.vmaAllocator) {
        result = stromboliVmaAllocator.vmaAllocator;
    }
    return result;
}
