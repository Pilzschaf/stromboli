#include "stromboli.h"

static inline void stromboliNameObject(StromboliContext* context, u64 handle, VkObjectType type, const char* name) {
	if (vkSetDebugUtilsObjectNameEXT) {
		VkDebugUtilsObjectNameInfoEXT nameInfo = { 0 };
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = type;
		nameInfo.objectHandle = handle;
		nameInfo.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
	}
}

static inline StromboliDescriptorInfo stromboliCreateBufferDescriptor(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
	StromboliDescriptorInfo result;

	result.bufferInfo.buffer = buffer;
	result.bufferInfo.offset = offset;
	result.bufferInfo.range = range;

	return result;
}

static inline StromboliDescriptorInfo stromboliCreateImageDescriptor(VkImageLayout imageLayout, VkImageView imageView, VkSampler sampler) {
	StromboliDescriptorInfo result;

	result.imageInfo.imageLayout = imageLayout;
	result.imageInfo.imageView = imageView;
	result.imageInfo.sampler = sampler;

	return result;
}

static inline void stromboliPipelineBarrier(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, u32 bufferBarrierCount, const VkBufferMemoryBarrier2KHR* bufferBarriers, u32 imageBarrierCount, const VkImageMemoryBarrier2KHR* imageBarriers) {
    VkDependencyInfoKHR dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
    dependencyInfo.dependencyFlags = dependencyFlags;
    dependencyInfo.bufferMemoryBarrierCount = bufferBarrierCount;
	dependencyInfo.pBufferMemoryBarriers = bufferBarriers;
	dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
	dependencyInfo.pImageMemoryBarriers = imageBarriers;

    vkCmdPipelineBarrier2KHR(commandBuffer, &dependencyInfo);
}

static inline VkImageMemoryBarrier2 stromboliCreateImageBarrier(VkImage image, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkImageLayout oldLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, VkImageLayout newLayout) {
	VkImageMemoryBarrier2 result = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };

	result.srcStageMask = srcStageMask;
	result.srcAccessMask = srcAccessMask;
	result.dstStageMask = dstStageMask;
	result.dstAccessMask = dstAccessMask;
	result.oldLayout = oldLayout;
	result.newLayout = newLayout;
	result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.image = image;
	result.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	result.subresourceRange.baseMipLevel = 0;
	result.subresourceRange.levelCount = 1;
	result.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	return result;
}