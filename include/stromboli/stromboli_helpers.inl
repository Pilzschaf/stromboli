#include "stromboli.h"
#include <stromboli/stromboli_tracy.h>

static inline void stromboliNameObject(StromboliContext* context, u64 handle, VkObjectType type, const char* name) {
	if (vkSetDebugUtilsObjectNameEXT) {
		VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
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
    if(vkCmdPipelineBarrier2KHR) {
		VkDependencyInfoKHR dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependencyInfo.dependencyFlags = dependencyFlags;
		dependencyInfo.bufferMemoryBarrierCount = bufferBarrierCount;
		dependencyInfo.pBufferMemoryBarriers = bufferBarriers;
		dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
		dependencyInfo.pImageMemoryBarriers = imageBarriers;

		vkCmdPipelineBarrier2KHR(commandBuffer, &dependencyInfo);
	} else {
		ASSERT(false);
		/*
		VkPipelineStageFlags srcStageMask = 0;
		VkPipelineStageFlags dstStageMask = 0;
		if(bufferBarrierCount) {
			srcStageMask = bufferBarriers[0].srcStageMask;
		}
		vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, 0, 0, bufferBarrierCount, bufferBarriers, imageBarrierCount, imageBarriers);*/
	}
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

static inline void stromboliCmdSetViewportAndScissor(VkCommandBuffer commandBuffer, u32 width, u32 height) {
	VkViewport viewport = { 0 };
	viewport.width = (float)width;
	viewport.height = (float)height;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissors = { 0 };
	scissors.offset.x = 0;
	scissors.offset.y = 0;
	scissors.extent.width = width;
	scissors.extent.height = height;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissors);
}

static inline void stromboliCmdBeginRenderpass(VkCommandBuffer commandBuffer, StromboliRenderpass* renderpass, u32 width, u32 height, u32 imageIndex) {
	VkRenderPassBeginInfo beginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	beginInfo.renderPass = renderpass->renderPass;
	beginInfo.clearValueCount = renderpass->numClearColors;
	beginInfo.pClearValues = renderpass->clearColors;
	beginInfo.framebuffer = renderpass->framebuffers[0];
	beginInfo.renderArea.offset.x = 0;
	beginInfo.renderArea.offset.y = 0;
	beginInfo.renderArea.extent.width = width;
	beginInfo.renderArea.extent.height = height;
	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

#ifndef MAX_TIMING_SECTIONS_PER_RENDER_SECTION
#define MAX_TIMING_SECTIONS_PER_RENDER_SECTION 32
#endif

struct StromboliRenderSectionTimingEntry {
	const char* name;
	float duration;
	u32 startIndex;
	u32 endIndex;
};

typedef struct StromboliRenderSection {
	VkCommandPool commandPools[MAX_SWAPCHAIN_IMAGES];
	VkCommandBuffer commandBuffers[MAX_SWAPCHAIN_IMAGES];
	StromboliQueue* queue;
	VkFence fences[MAX_SWAPCHAIN_IMAGES];

	VkQueryPool timestampQueryPools[MAX_SWAPCHAIN_IMAGES];
	float durationOfLastCompletedInvocation;
	u32 queryEntryCount[MAX_SWAPCHAIN_IMAGES];
	u32 timingEntryCount[MAX_SWAPCHAIN_IMAGES];
	struct StromboliRenderSectionTimingEntry timingEntries[MAX_SWAPCHAIN_IMAGES][MAX_TIMING_SECTIONS_PER_RENDER_SECTION];

#ifdef TRACY_ENABLE
	TracyStromboliScope tracyScopes[MAX_TIMING_SECTIONS_PER_RENDER_SECTION];
#endif
} StromboliRenderSection;

inline static StromboliRenderSection createRenderSection(StromboliContext* context, StromboliQueue* queue) {
	StromboliRenderSection result = { 0 };
	result.queue = queue;
	for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
		{
			VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			createInfo.queueFamilyIndex = queue->familyIndex;
			vkCreateCommandPool(context->device, &createInfo, 0, &result.commandPools[i]);

			VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocateInfo.commandPool = result.commandPools[i];
			allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocateInfo.commandBufferCount = 1;
			vkAllocateCommandBuffers(context->device, &allocateInfo, &result.commandBuffers[i]);
		}
		{
			VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			vkCreateFence(context->device, &createInfo, 0, &result.fences[i]);
		}
		{
			VkQueryPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			createInfo.queryCount = MAX_TIMING_SECTIONS_PER_RENDER_SECTION * 2;
			vkCreateQueryPool(context->device, &createInfo, 0, &result.timestampQueryPools[i]);
		}
	}
	return result;
}

inline static void destroyRenderSection(StromboliContext* context, StromboliRenderSection* renderSection) {
	for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
		vkDestroyCommandPool(context->device, renderSection->commandPools[i], 0);
		vkDestroyFence(context->device, renderSection->fences[i], 0);
		vkDestroyQueryPool(context->device, renderSection->timestampQueryPools[i], 0);
	}
}

inline static void renderSectionBeginTimingSection(StromboliContext* context, StromboliRenderSection* section, u32 frameIndex, const char* name) {
	ASSERT(section->queryEntryCount[frameIndex] < MAX_TIMING_SECTIONS_PER_RENDER_SECTION * 2);
	ASSERT(section->timingEntryCount[frameIndex] < MAX_TIMING_SECTIONS_PER_RENDER_SECTION);
	u32 queryIndex = section->queryEntryCount[frameIndex]++;
	u32 timingEntryIndex = section->timingEntryCount[frameIndex]++;
	vkCmdWriteTimestamp(section->commandBuffers[frameIndex], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, section->timestampQueryPools[frameIndex], queryIndex);
	section->timingEntries[frameIndex][timingEntryIndex] = (struct StromboliRenderSectionTimingEntry){ 0 };
	section->timingEntries[frameIndex][timingEntryIndex].startIndex = queryIndex;
	section->timingEntries[frameIndex][timingEntryIndex].name = name;

#ifdef TRACY_ENABLE
	section->tracyScopes[timingEntryIndex] = createTracyStromboliScopeAllocSource( getTracyContext(), __LINE__, __FILE__, strlen( __FILE__ ), __FUNCTION__, strlen(__FUNCTION__), name, strlen(name), section->commandBuffers[frameIndex], true);
#endif
}

inline static void renderSectionEndTimingSection(StromboliContext* context, StromboliRenderSection* section, u32 frameIndex) {
	ASSERT(section->queryEntryCount[frameIndex] < MAX_TIMING_SECTIONS_PER_RENDER_SECTION * 2);
	u32 queryIndex = section->queryEntryCount[frameIndex]++;
	// Find timing entry index by iterating backwards and finding the first one that does not have an end index
	u32 timingEntryIndex = 0xFFFFFFFF;
	for (u32 i = section->timingEntryCount[frameIndex]; i > 0; --i) {
		if (!section->timingEntries[frameIndex][i - 1].endIndex) {
			timingEntryIndex = i - 1;
			break;
		}
	}
	// If this assert triggers, there are probably more renderSectionEndTimingSection than renderSectionBeginTimingSection
	ASSERT(timingEntryIndex != 0xFFFFFFFF);
	section->timingEntries[frameIndex][timingEntryIndex].endIndex = queryIndex;
	vkCmdWriteTimestamp(section->commandBuffers[frameIndex], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, section->timestampQueryPools[frameIndex], queryIndex);

#ifdef TRACY_ENABLE
	destroyTracyStromboliScope(&section->tracyScopes[timingEntryIndex]);
#endif
}

inline static VkCommandBuffer beginRenderSection(StromboliContext* context, StromboliRenderSection* section, u32 frameIndex, const char* name) {
	// Wait for fence
	vkWaitForFences(context->device, 1, &section->fences[frameIndex], VK_TRUE, UINT64_MAX);
	vkResetFences(context->device, 1, &section->fences[frameIndex]);

	// Get results from timestamp query pool
	uint64_t timestamps[MAX_TIMING_SECTIONS_PER_RENDER_SECTION * 2] = { 0 };
	u32 timestampCount = section->queryEntryCount[frameIndex];
	if (timestampCount > 0) {
		VkResult timestampsValid = vkGetQueryPoolResults(context->device, section->timestampQueryPools[frameIndex], 0, timestampCount, sizeof(timestamps), timestamps, sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT);
		if (timestampsValid == VK_SUCCESS) {
			for (u32 i = 0; i < section->timingEntryCount[frameIndex]; ++i) {
				u32 startIndex = section->timingEntries[frameIndex][i].startIndex;
				u32 endIndex = section->timingEntries[frameIndex][i].endIndex;
				double begin = ((double)timestamps[startIndex]) * context->physicalDeviceProperties.limits.timestampPeriod * 1e-6;
				double end = ((double)timestamps[endIndex]) * context->physicalDeviceProperties.limits.timestampPeriod * 1e-6;
				float delta = (float)(end - begin);
				section->timingEntries[frameIndex][i].duration = delta;
				//printf("%s: %fms\n", section->timingEntries[frameIndex][i].name, delta);
			}
			section->durationOfLastCompletedInvocation = section->timingEntries[frameIndex][0].duration;
		}
		else {
			section->durationOfLastCompletedInvocation = 0.0f;
		}
	}
	//TODO: We still need a way to give the data to the caller as it gets overwritten with the renderSectionBeginTimingSection
	// Maybe give it out to the user by the user providing a MemoryArena and a pointer to write the count into

	// Begin command buffer
	vkResetCommandPool(context->device, section->commandPools[frameIndex], 0);
	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(section->commandBuffers[frameIndex], &beginInfo);

	vkCmdResetQueryPool(section->commandBuffers[frameIndex], section->timestampQueryPools[frameIndex], 0, 64);
	section->queryEntryCount[frameIndex] = 0;
	section->timingEntryCount[frameIndex] = 0;
	renderSectionBeginTimingSection(context, section, frameIndex, name);

	return section->commandBuffers[frameIndex];
}
inline static void endRenderSection(StromboliContext* context, StromboliRenderSection* section, u32 frameIndex, u32 numWaitSemaphores, VkSemaphore* waitSemaphores, VkPipelineStageFlags* waitMasks, u32 numSignalSemaphores, VkSemaphore* signalSemaphores) {
	renderSectionEndTimingSection(context, section, frameIndex);
	// This assert triggers if there is a non mathing number of renderSectionBeginTimingSection and renderSectionEndTimingSection
	ASSERT(section->timingEntryCount[frameIndex] * 2 == section->queryEntryCount[frameIndex]);
	vkEndCommandBuffer(section->commandBuffers[frameIndex]);

	// vkQueueSubmit2 allows semaphore signaling based on stage

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = numWaitSemaphores;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitMasks;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &section->commandBuffers[frameIndex];
	submitInfo.signalSemaphoreCount = numSignalSemaphores;
	submitInfo.pSignalSemaphores = signalSemaphores;
	//groundedLockMutex(&section->queue->mutex);
	vkQueueSubmit(section->queue->queue, 1, &submitInfo, section->fences[frameIndex]);
	//groundedUnlockMutex(&section->queue->mutex);
}

// If it returns false, the swapchain should be resized
inline static bool presentImage(StromboliContext* context, StromboliSwapchain* swapchain, VkSemaphore releaseSemaphore, u32 imageIndex) {
	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &releaseSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain->swapchain;
	presentInfo.pImageIndices = &imageIndex;
	StromboliQueue* queue = &context->graphicsQueues[0];
	ASSERT(queue->flags & STROMBOLI_QUEUE_FLAG_SUPPORTS_PRESENT);
	VkResult result = vkQueuePresentKHR(queue->queue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		// Swapchain is out of date
		//groundedLockMutex(&queue->mutex);
		vkQueueWaitIdle(queue->queue);
		//groundedUnlockMutex(&queue->mutex);
		return false;
	}
	return true;
}

static inline void stromboliCmdCopyImageToSwapchain(VkCommandBuffer commandBuffer, StromboliImage* source, VkFormat sourceFormat, StromboliSwapchain* swapchain, u32 imageIndex) {
	if (swapchain->format == sourceFormat) {
		// Formats match so do a simple blit
		VkImageCopy region = { 0 };
		region.srcOffset = (VkOffset3D){ 0 };
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.mipLevel = 0;
		region.dstOffset = (VkOffset3D){ 0 };
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.mipLevel = 0;
		region.extent = (VkExtent3D){ swapchain->width, swapchain->height, 1 };

		vkCmdCopyImage(commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			swapchain->images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
	else {
		VkOffset3D blitSize;
		blitSize.x = swapchain->width;
		blitSize.y = swapchain->height;
		blitSize.z = 1;
		VkImageBlit imageBlitRegion = { 0 };
		imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.srcSubresource.layerCount = 1;
		imageBlitRegion.srcOffsets[1] = blitSize;
		imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.dstSubresource.layerCount = 1;
		imageBlitRegion.dstOffsets[1] = blitSize;

		// Issue the blit command
		vkCmdBlitImage(
			commandBuffer,
			source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			swapchain->images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&imageBlitRegion,
			VK_FILTER_NEAREST);
	}
}

static inline void stromboliCmdCopyWholeImage(VkCommandBuffer commandBuffer, StromboliImage* source, StromboliImage* target) {
	ASSERT(source->width == target->width);
	ASSERT(source->height == target->height);
	ASSERT(source->depth == target->depth);

	VkImageCopy region = { 0 };
	region.srcOffset = (VkOffset3D){ 0 };
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.baseArrayLayer = 0;
	region.srcSubresource.layerCount = 1;
	region.srcSubresource.mipLevel = 0;
	region.dstOffset = (VkOffset3D){ 0 };
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.baseArrayLayer = 0;
	region.dstSubresource.layerCount = 1;
	region.dstSubresource.mipLevel = 0;
	region.extent = (VkExtent3D){ source->width, source->height, source->depth };
	vkCmdCopyImage(commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		target->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

// If it returns false, acquiring has not succeeded and the swapchain must be resized.
// Fence must make sure that the acquire semaphore is ready to be used again eg. that the submit that last used it has completed
inline static bool acquireImage(StromboliContext* context, StromboliSwapchain* swapchain, VkSemaphore acquireSemaphore, VkFence fence, u32* imageIndex) {

	// Wait for the n-MAX_FRAMES_IN_FLIGHT frame to finish to be able to reuse its acquireSemaphore in vkAcquireNextImageKHR
	if (fence) {
		vkWaitForFences(context->device, 1, &fence, VK_TRUE, UINT64_MAX);
	}

	VkResult result = vkAcquireNextImageKHR(context->device, swapchain->swapchain, UINT64_MAX, acquireSemaphore, 0, imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		// Swapchain is out of date
		//groundedLockMutex(&context->graphicsQueues[0].mutex);
		vkQueueWaitIdle(context->graphicsQueues[0].queue);
		//groundedUnlockMutex(&context->graphicsQueues[0].mutex);
		return false;
	}
	return true;
}

static inline void stromboliCmdTraceRays(VkCommandBuffer commandBuffer, StromboliPipeline* pipeline, u32 width, u32 height) {
	ASSERT(pipeline->type == STROMBOLI_PIPELINE_TYPE_RAYTRACING);
	vkCmdTraceRaysKHR(commandBuffer, &pipeline->raytracing.sbtRayGenRegion, &pipeline->raytracing.sbtMissRegion, &pipeline->raytracing.sbtHitRegion, &pipeline->raytracing.sbtCallableRegion, width, height, 1);
}

static inline void stromboliCmdDispatch(VkCommandBuffer commandBuffer, StromboliPipeline* pipeline, u32 width, u32 height) {
	ASSERT(pipeline->type == STROMBOLI_PIPELINE_TYPE_COMPUTE);
	u32 workgroupWidth = pipeline->compute.workgroupWidth;
	u32 workgroupHeight = pipeline->compute.workgroupHeight;
	ASSERT(workgroupWidth > 0 && workgroupHeight > 0);
	vkCmdDispatch(commandBuffer, (width + (workgroupWidth-1)) / workgroupWidth, (height + (workgroupHeight-1)) / workgroupHeight, 1);
}

static inline VkDescriptorSet createDescriptorSet(StromboliContext* context, StromboliPipeline* pipeline, VkDescriptorPool descriptorPool, u32 setIndex) {
	VkDescriptorSet result = {0};
	VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocateInfo.descriptorPool = descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = pipeline->descriptorLayouts + setIndex;
	VkResult error = vkAllocateDescriptorSets(context->device, &allocateInfo, &result);
	ASSERT(error == VK_SUCCESS);
	return result;
}
