#include <stromboli/stromboli.h>
#include <stdio.h>

bool isDepthFormat(VkFormat format) {
	if( format == VK_FORMAT_D32_SFLOAT || 
		format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
		format == VK_FORMAT_D16_UNORM ||
		format == VK_FORMAT_D16_UNORM_S8_UINT ||
		format == VK_FORMAT_D24_UNORM_S8_UINT ||
		format == VK_FORMAT_X8_D24_UNORM_PACK32) {
		return true;
	}
	return false;
}

u32 stromboliFindMemoryType(StromboliContext* context, u32 typeFilter, VkMemoryPropertyFlags memoryProperties) {
	ASSERT(typeFilter);
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
	vkGetPhysicalDeviceMemoryProperties(context->physicalDevice, &deviceMemoryProperties);

	for (u32 i = 0; i < deviceMemoryProperties.memoryTypeCount; ++i) {
		// Check if required memory type is allowed
		if ((typeFilter & (1 << i)) != 0) {
			// Check if required properties are satisfied
			if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties) {
				// Return this memory type index
				//LOG_INFO("Using memory heap index ", deviceMemoryProperties.memoryTypes[i].heapIndex);
				return i;
			}
		}
	}

	// No matching avaialble memory type found
	ASSERT(false);
	return UINT32_MAX;
}

void stromboliUploadDataToBuffer(StromboliContext* context, StromboliBuffer* buffer, const void* data, size_t size, StromboliUploadContext* uploadContext) {
	//void* mapping = 0;
	ASSERT(buffer->mapped);
	//vkMapMemory(context->device, buffer->memory, 0, size, 0, &mapping);
	memcpy(buffer->mapped, data, size);
	//vkUnmapMemory(context->device, buffer->memory);
}

VkDeviceAddress getBufferDeviceAddress(StromboliContext* context, StromboliBuffer* buffer) {
    VkBufferDeviceAddressInfo addressInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addressInfo.buffer = buffer->buffer;
    return vkGetBufferDeviceAddress(context->device, &addressInfo);
}

StromboliImage stromboliImageCreate(StromboliContext* context, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, struct StromboliImageParameters* parameters, StromboliAllocationContext* allocationContext) {
	StromboliImage result = {0};
	if(!parameters) {
		static struct StromboliImageParameters defaultParameters = {0};
		parameters = &defaultParameters;
	}
	u32 depth = parameters->depth == 0 ? 1 : parameters->depth;
	u32 mipChainLength = parameters->mipCount == 0 ? 1 : parameters->mipCount;
	VkImageType imageType = VK_IMAGE_TYPE_2D;
	if(depth > 1) {
		imageType = VK_IMAGE_TYPE_3D;
	}
	VkSampleCountFlags samples = parameters->sampleCount == 0 ? VK_SAMPLE_COUNT_1_BIT : parameters->sampleCount;
	u32 layerCount = parameters->layerCount == 0 ? 1 : parameters->layerCount;
	{
		VkImageCreateInfo createInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		if(parameters->cubemap) {
			createInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}
		createInfo.imageType = imageType;
		createInfo.extent.width = width;
		createInfo.extent.height = height;
		createInfo.extent.depth = depth;
		createInfo.mipLevels = mipChainLength;
		createInfo.arrayLayers = layerCount;
		createInfo.format = format;
		createInfo.tiling = parameters->tiling;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.usage = usage;
		createInfo.samples = samples;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		#ifdef STROMBOLI_NO_VMA
		vkCreateImage(context->device, &createInfo, 0, &result.image);
		#else
		VmaAllocationCreateInfo allocInfo = {0};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		if(parameters->requireCPUAccess) {
			ASSERT(parameters->tiling == VK_IMAGE_TILING_LINEAR);
			allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}
		vmaCreateImage(context->vmaAllocator, &createInfo, &allocInfo, &result.image, &result.allocation, 0);
		#endif
	}

	#ifdef STROMBOLI_NO_VMA
	VkMemoryRequirements memoryRequirements;
	if(!vkGetImageMemoryRequirements2) {
		vkGetImageMemoryRequirements(context->device, result.image, &memoryRequirements);
	} else {
		VkImageMemoryRequirementsInfo2 memoryInfo = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
		memoryInfo.image = result.image;
		
		VkMemoryDedicatedRequirements dedicatedRequirements = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};

		VkMemoryRequirements2 imageMemoryRequirements = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
		imageMemoryRequirements.pNext = &dedicatedRequirements;
		vkGetImageMemoryRequirements2(context->device, &memoryInfo, &imageMemoryRequirements);

		memoryRequirements = imageMemoryRequirements.memoryRequirements;
		if(dedicatedRequirements.prefersDedicatedAllocation) {
			printf("Prefers dedicated allocation\n");
		}
	}
	
	VkMemoryAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = stromboliFindMemoryType(context, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceSize memoryOffset = 0;
	if(allocationContext) {
		result.memory = allocationContext->allocate(allocationContext, memoryRequirements, &memoryOffset, 0);
		result.allocator = allocationContext;
	} else {
		vkAllocateMemory(context->device, &allocateInfo, 0, &result.memory);
	}
	vkBindImageMemory(context->device, result.image, result.memory, memoryOffset);
	#endif

	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	if(isDepthFormat(format)) {
		aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	if(depth > 1) {
		viewType = VK_IMAGE_VIEW_TYPE_3D;
	}
	if(parameters->cubemap) {
		viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	}

	{
		VkImageViewCreateInfo createInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		createInfo.image = result.image;
		createInfo.viewType = viewType;
		createInfo.format = format;
		createInfo.subresourceRange.aspectMask = aspect;
		createInfo.subresourceRange.levelCount = mipChainLength;
		createInfo.subresourceRange.layerCount = layerCount;
		createInfo.components = parameters->componentMapping;
		vkCreateImageView(context->device, &createInfo, 0, &result.view);
	}
	result.width = width;
	result.height = height;
	result.depth = depth;
	result.mipCount = mipChainLength;
	result.format = format;
	result.samples = samples;
	return result;
}

void stromboliImageDestroy(StromboliContext* context, StromboliImage* image) {
	vkDestroyImageView(context->device, image->view, 0);
#ifdef STROMBOLI_NO_VMA
	vkDestroyImage(context->device, image->image, 0);
	if(image->allocator) {
		image->allocator->deallocate(image->allocator, image->memory);
		image->memory = 0;
	}
	if(image->memory) {
		vkFreeMemory(context->device, image->memory, 0);
	}
#else
	if(image->allocation) {
		vmaDestroyImage(context->vmaAllocator, image->image, image->allocation);
	} else {
		vkDestroyImage(context->device, image->image, 0);
	}
#endif
}

void stromboliUploadDataToImage(StromboliContext* context, StromboliImage* image, void* data, size_t size, VkImageLayout finalLayout, VkAccessFlags dstAccessMask, StromboliUploadContext* uploadContext) {
	//TRACY_ZONE_HELPER(uploadDataToImage);
	ASSERT((size % image->width * image->height * image->depth) == 0);

	// Make sure we have an upload context
	StromboliUploadContext uc = ensureValidUploadContext(context, uploadContext);
	if(uc.commandPool) uploadContext = &uc;

	// Upload with staging buffer
	VkCommandBuffer commandBuffer = ensureUploadContextIsRecording(context, uploadContext);
	u64 scratchOffset = uploadToScratch(context, uploadContext, data, size);

	{
		if(vkCmdPipelineBarrier2) {
			VkImageMemoryBarrier2 imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageBarrier.image = image->image;
			imageBarrier.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			};
			VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
			dependencyInfo.imageMemoryBarrierCount = 1;
			dependencyInfo.pImageMemoryBarriers = &imageBarrier;
			vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
		} else {
			VkImageMemoryBarrier imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarrier.image = image->image;
			imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBarrier.subresourceRange.levelCount = 1;
			imageBarrier.subresourceRange.layerCount = 1;
			imageBarrier.srcAccessMask = 0;
			imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &imageBarrier);
		}
	}

	VkBufferImageCopy region = {0};
	region.bufferOffset = scratchOffset;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = (VkExtent3D){image->width, image->height, image->depth};
	vkCmdCopyBufferToImage(commandBuffer, uploadContext->scratch->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	if(finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL){
		VkImageMemoryBarrier imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = finalLayout;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = image->image;
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &imageBarrier);
	}

	if(uc.commandPool) {
		destroyUploadContext(context, &uc);
	}
}

void stromboliDestroyBuffer(StromboliContext* context, StromboliBuffer* buffer) {
	#ifndef STROMBOLI_NO_VMA
	if(buffer->mapped && !buffer->memory) {
		vmaUnmapMemory(context->vmaAllocator, buffer->allocation);
		// Unmapping should not be necessary?
		//vkUnmapMemory(context->device, buffer->memory);
	}
	#endif
	
	buffer->mapped = 0;
	// Assumes that the buffer owns its own memory block
	if(buffer->allocator) {
		buffer->allocator->deallocate(buffer->allocator, buffer->memory);
		buffer->memory = 0;
	}
	if(buffer->memory) {
		vkUnmapMemory(context->device, buffer->memory);
		vkFreeMemory(context->device, buffer->memory, 0);
		vkDestroyBuffer(context->device, buffer->buffer, 0);
		return;
	}
#ifdef STROMBOLI_NO_VMA
	vkDestroyBuffer(context->device, buffer->buffer, 0);
#else
	vmaDestroyBuffer(context->vmaAllocator, buffer->buffer, buffer->allocation);
#endif
}

StromboliAccelerationStructure createAccelerationStructure(StromboliContext* context, u32 count, VkAccelerationStructureGeometryKHR* geometries, VkAccelerationStructureBuildRangeInfoKHR* buildRanges, bool allowUpdate, bool compact, StromboliUploadContext* uploadContext) {
    //TRACY_ZONE_HELPER(createAccelerationStructure);

	StromboliAccelerationStructure result;

	// Make sure we have an upload context
	StromboliUploadContext uc = ensureValidUploadContext(context, uploadContext);
	if(uc.commandPool) uploadContext = &uc;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &asProperties;
    vkGetPhysicalDeviceProperties2(context->physicalDevice, &properties);
    
    VkAccelerationStructureTypeKHR accelerationStructureType = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if(geometries->geometryType == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
        accelerationStructureType = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = count;
    buildInfo.pGeometries = geometries;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.type = accelerationStructureType;
	if(allowUpdate) {
		buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	}
	if(compact) {
		buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
	}

    // Find sizes
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    u32 maxPrimitiveCount = buildRanges->primitiveCount;
    vkGetAccelerationStructureBuildSizesKHR(context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount, &sizeInfo);
    sizeInfo.accelerationStructureSize = ALIGN_UP_POW2(sizeInfo.accelerationStructureSize, 256);
    sizeInfo.buildScratchSize = ALIGN_UP_POW2(sizeInfo.buildScratchSize, asProperties.minAccelerationStructureScratchOffsetAlignment);

    result.accelerationStructureBuffer = stromboliCreateBuffer(context, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    {
        VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.type = accelerationStructureType;
        createInfo.buffer = result.accelerationStructureBuffer.buffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        vkCreateAccelerationStructureKHR(context->device, &createInfo, 0, &result.accelerationStructure);
    }
    // Scratch buffer to hold temporary data for the acceleration structure builder
	//TODO: Must ensure some minimal alignment. Currently using VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR to ensure this in a simple way
    StromboliBuffer scratchBuffer = stromboliCreateBuffer(context, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkDeviceAddress scratchAddress = getBufferDeviceAddress(context, &scratchBuffer);
    buildInfo.dstAccelerationStructure = result.accelerationStructure;
    buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(context, &scratchBuffer);

    VkCommandBuffer commandBuffer = ensureUploadContextIsRecording(context, uploadContext);

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = buildRanges;
    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pBuildRange);

	//TODO: Flush so we can destroy the buffer. This would not be required if we would use the scratch buffer of the context
	flushUploadContext(context, uploadContext);
    stromboliDestroyBuffer(context, &scratchBuffer);

	if(compact) {
		ensureUploadContextIsRecording(context, uploadContext);

		VkQueryPool compactQueryPool;
		VkQueryPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
		createInfo.queryCount = 1;
		createInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
		vkCreateQueryPool(context->device, &createInfo, 0, &compactQueryPool);

		vkCmdResetQueryPool(commandBuffer, compactQueryPool, 0, 1);
		vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, 1, &result.accelerationStructure, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, compactQueryPool, 0);
		submitUploadContext(context, uploadContext, 0, 0);

		// We need to get the query pool results
		flushUploadContext(context, uploadContext);
		VkDeviceSize compactSize = {0};
		VkResult error = vkGetQueryPoolResults(context->device, compactQueryPool, 0, 1, sizeof(VkDeviceSize), &compactSize, sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT);
		ASSERT(error == VK_SUCCESS);

		// Create compact version of AS
		VkAccelerationStructureKHR compactedAccelerationStructure = {0};
		StromboliBuffer compactedAccelerationStructureBuffer = {0};
		compactedAccelerationStructureBuffer = stromboliCreateBuffer(context, compactSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
		VkAccelerationStructureCreateInfoKHR asCreateInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
		asCreateInfo.size = compactSize;
		asCreateInfo.type = accelerationStructureType;
		asCreateInfo.buffer = compactedAccelerationStructureBuffer.buffer;
		vkCreateAccelerationStructureKHR(context->device, &asCreateInfo, 0, &compactedAccelerationStructure);

		ensureUploadContextIsRecording(context, uploadContext);
		VkCopyAccelerationStructureInfoKHR copyInfo = {VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
		copyInfo.src = result.accelerationStructure;
		copyInfo.dst = compactedAccelerationStructure;
		copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
		vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
		submitUploadContext(context, uploadContext, 0, 0);

		vkDestroyQueryPool(context->device, compactQueryPool, 0);

		flushUploadContext(context, uploadContext);
		vkDestroyAccelerationStructureKHR(context->device, result.accelerationStructure, 0);
		result.accelerationStructure = compactedAccelerationStructure;
		stromboliDestroyBuffer(context, &result.accelerationStructureBuffer);
		result.accelerationStructureBuffer = compactedAccelerationStructureBuffer;
	}

	if(uc.commandPool) {
		destroyUploadContext(context, &uc);
	}

    return result;
}

void destroyAccelerationStructure(StromboliContext* context, StromboliAccelerationStructure* accelerationStructure) {
	if(vkDestroyAccelerationStructureKHR) {
		vkDestroyAccelerationStructureKHR(context->device, accelerationStructure->accelerationStructure, 0);
	}
	stromboliDestroyBuffer(context, &accelerationStructure->accelerationStructureBuffer);
}

StromboliUploadContext createUploadContext(StromboliContext* context, StromboliQueue* queue, StromboliBuffer* scratch) {
	//TRACY_ZONE_HELPER(createUploadContext);

	StromboliUploadContext result = {0};
    result.queue = queue;
    result.scratch = scratch;
    
	VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	createInfo.queueFamilyIndex = queue->familyIndex;
	vkCreateCommandPool(context->device, &createInfo, 0, &result.commandPool);

	VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocateInfo.commandPool = result.commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	vkAllocateCommandBuffers(context->device, &allocateInfo, &result.commandBuffer);
    
	VkFenceCreateInfo fenceCreateInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	vkCreateFence(context->device, &fenceCreateInfo, 0, &result.fence);

    return result;
}

StromboliBuffer stromboliCreateBuffer(StromboliContext* context, uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties, StromboliAllocationContext* allocationContext) {
	StromboliBuffer result = {0};
	VkBufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	createInfo.size = size;
	createInfo.usage = usage;

	vkCreateBuffer(context->device, &createInfo, 0, &result.buffer);

#ifndef STROMBOLI_NO_VMA
	if(!allocationContext) {
		VmaAllocationCreateInfo allocInfo = {0};
		allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		vmaCreateBuffer(context->vmaAllocator, &createInfo, &allocInfo, &result.buffer, &result.allocation, 0);
		//vmaAllocateMemoryForBuffer(context->vmaAllocator, buffer->buffer, &allocInfo, &buffer->allocation, 0);
		vmaMapMemory(context->vmaAllocator, result.allocation, &result.mapped);
	} else
#endif
	{
		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(context->device, result.buffer, &memoryRequirements);
		u32 memoryIndex = stromboliFindMemoryType(context, memoryRequirements.memoryTypeBits, memoryProperties);
		ASSERT(memoryIndex != UINT32_MAX);

		//VkMemoryAllocateFlagsInfo allocateFlags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
		//allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

		VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		//allocateInfo.pNext = &allocateFlags;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = memoryIndex;

		VkDeviceSize memoryOffset = 0;
		if(allocationContext) {
			result.allocator = allocationContext;
			result.memory = allocationContext->allocate(allocationContext, memoryRequirements, &memoryOffset, &result.mapped);
		} else {
			vkAllocateMemory(context->device, &allocateInfo, 0, &result.memory);
		}

		vkBindBufferMemory(context->device, result.buffer, result.memory, memoryOffset);

		// We have to check all 3 as we are only checking user requested and not the property of the returned memory
		if(!result.mapped) {
			if((memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) || (memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || (memoryProperties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
				vkMapMemory(context->device, result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped);
			} else {
				result.mapped = 0;
			}
		}
	}

	result.size = size;
	return result;
}

void destroyUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext) {
	//TRACY_ZONE_HELPER(destroyUploadContext);

	if((uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_RECORDING) || (uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_SUBMIT_ACTIVE)) {
		flushUploadContext(context, uploadContext);
	}
    uploadContext->commandBuffer = 0;
    vkDestroyCommandPool(context->device, uploadContext->commandPool, 0);
	vkDestroyFence(context->device, uploadContext->fence, 0);

	// Destroy buffer if it is owned by the upload context
	if(uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_OWNS_BUFFER && uploadContext->scratch) {
		stromboliDestroyBuffer(context, uploadContext->scratch);
	}
}

StromboliUploadContext ensureValidUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext) {
	StromboliUploadContext result = {0};
	if(!uploadContext || uploadContext->commandPool == 0) {
		// Allows to specify which queue to use
		StromboliQueue* queue =  uploadContext->queue;
		ASSERT(queue);
		result = createUploadContext(context, queue, 0);
		result.flags |= STROMBOLI_UPLOAD_CONTEXT_OWNS_BUFFER;
		beginRecordUploadContext(context, &result);
	}
	return result;
}

void beginRecordUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext) {
    if(!(uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_RECORDING)) {
        vkResetCommandPool(context->device, uploadContext->commandPool, 0);
        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(uploadContext->commandBuffer, &beginInfo);

        uploadContext->flags |= STROMBOLI_UPLOAD_CONTEXT_RECORDING;
    }
}

VkCommandBuffer ensureUploadContextIsRecording(StromboliContext* context, StromboliUploadContext* uploadContext) {
	// beginRecord already checks if we are recording and starts it if not
	beginRecordUploadContext(context, uploadContext);
	return uploadContext->commandBuffer;
}

// Returns UINT64_MAX if there is no space left to fit this upload request
u64 tryUploadToScratch(StromboliContext* context, StromboliUploadContext* uploadContext, void* data, u64 size) {
	if(uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_SUBMIT_ACTIVE) {
        flushUploadContext(context, uploadContext);
		beginRecordUploadContext(context, uploadContext);
    }
	// If buffer is not created yet, create it!
	if(!uploadContext->scratch) {
		uploadContext->scratch = &uploadContext->ownedBuffer;
		*uploadContext->scratch = stromboliCreateBuffer(context, MAX(MB(1), ALIGN_UP_POW2(size, 4096)), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0);
		STROMBOLI_NAME_OBJECT_EXPLICIT(context, uploadContext->ownedBuffer.buffer, VK_OBJECT_TYPE_BUFFER, "Upload context scratch buffer");
		uploadContext->flags |= STROMBOLI_UPLOAD_CONTEXT_OWNS_BUFFER;
	}

	u64 newOffset = uploadContext->scratchOffset + size;
    if(newOffset > uploadContext->scratch->size) {
		return UINT64_MAX;
	}
	memcpy(((uint8_t*)uploadContext->scratch->mapped)+uploadContext->scratchOffset, data, size);
    u64 result = uploadContext->scratchOffset;
    uploadContext->scratchOffset = newOffset;
	return result;
}

// May auto flush to fit the data
// Returns UINT64_MAX if the scratch buffer is too small.
u64 uploadToScratch(StromboliContext* context, StromboliUploadContext* uploadContext, void* data, u64 size) {
	u64 result = tryUploadToScratch(context, uploadContext, data, size);
	if(result == UINT64_MAX) {
		// No space left so auto flush upload context
		flushUploadContext(context, uploadContext);
		beginRecordUploadContext(context, uploadContext);

		result = tryUploadToScratch(context, uploadContext, data, size);
		// Whole scratch buffer too small to fit the data
		ASSERT(result != UINT64_MAX);
	}
	return result;
}

void submitUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext, u32 signalSemaphoreCount, VkSemaphore* signalSemaphores) {
    //TRACY_ZONE_HELPER(submitUploadContext);

	if(uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_RECORDING) {
        vkEndCommandBuffer(uploadContext->commandBuffer);

        VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadContext->commandBuffer;
		submitInfo.signalSemaphoreCount = signalSemaphoreCount;
		submitInfo.pSignalSemaphores = signalSemaphores;
		//groundedLockMutex(&uploadContext->queue->mutex);
        vkQueueSubmit(uploadContext->queue->queue, 1, &submitInfo, uploadContext->fence);
		//groundedUnlockMutex(&uploadContext->queue->mutex);

        uploadContext->flags &= ~STROMBOLI_UPLOAD_CONTEXT_RECORDING;
        uploadContext->flags |= STROMBOLI_UPLOAD_CONTEXT_SUBMIT_ACTIVE;
    } else {
		printf("No commands to submit in this upload context\n");
    }
}

void flushUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext) {
	//TRACY_ZONE_HELPER(flushUploadContext);

    if(uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_RECORDING) {
		// Auto submit if flush is called
        submitUploadContext(context, uploadContext, 0, 0);
    }
    if(uploadContext->flags & STROMBOLI_UPLOAD_CONTEXT_SUBMIT_ACTIVE) {
        vkWaitForFences(context->device, 1, &uploadContext->fence, VK_TRUE, UINT64_MAX);
		vkResetFences(context->device, 1, &uploadContext->fence);
        uploadContext->flags &= ~STROMBOLI_UPLOAD_CONTEXT_SUBMIT_ACTIVE;
    } else {
		//printf("No submits to flush in this upload context\n");
    }
    uploadContext->scratchOffset = 0;
}

void stromboliUploadDataToImageSubregion(StromboliContext* context, StromboliImage* image, void* data, u64 size, u32 offsetX, u32 offsetY, u32 offsetZ, u32 width, u32 height, u32 depth, u32 inputStrideInPixels, u32 mipLevel, u32 layer, VkImageLayout finalLayout, VkAccessFlags dstAccessMask, StromboliUploadContext* uploadContext) {
	//TRACY_ZONE_HELPER(uploadDataToImageSubregion);

	ASSERT((size % (width * height * depth)) == 0);

	// Make sure we have an upload context
	StromboliUploadContext uc = ensureValidUploadContext(context, uploadContext);
	if(uc.commandPool) uploadContext = &uc;

	// Upload with staging buffer
	VkCommandBuffer commandBuffer = ensureUploadContextIsRecording(context, uploadContext);
	u64 scratchOffset = uploadToScratch(context, uploadContext, data, size);

	{ // Prepare target texture for transfer
		VkImageMemoryBarrier2 imageBarrier = stromboliCreateImageBarrier(image->image, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		imageBarrier.subresourceRange.baseMipLevel = mipLevel;
		imageBarrier.subresourceRange.levelCount = 1;
		stromboliPipelineBarrier(commandBuffer, 0, 0, 0, 1, &imageBarrier);
	}

	VkBufferImageCopy region = {0};
	region.bufferRowLength = inputStrideInPixels;
	region.bufferOffset = scratchOffset;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.baseArrayLayer = layer;
	region.imageSubresource.mipLevel = mipLevel;
	region.imageOffset = (VkOffset3D){offsetX, offsetY, offsetZ};
	region.imageExtent = (VkExtent3D){width, height, depth};
	vkCmdCopyBufferToImage(commandBuffer, uploadContext->scratch->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	{ // Switch target to final layout
		if(finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			VkImageMemoryBarrier2 imageBarrier = stromboliCreateImageBarrier(image->image, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, finalLayout);
			imageBarrier.subresourceRange.baseMipLevel = mipLevel;
			imageBarrier.subresourceRange.levelCount = 1;
			stromboliPipelineBarrier(commandBuffer, 0, 0, 0, 1, &imageBarrier);
		}
	}

	if(uc.commandPool) {
		destroyUploadContext(context, &uc);
	}
}

VkSampler stromboliSamplerCreate(StromboliContext* context, bool linear, VkSamplerAddressMode edgeMode) {
	VkSampler result = 0;
	VkFilter filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; // Bilinear
	VkSamplerMipmapMode mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST; // Trilinear

	VkSamplerCreateInfo createInfo = (VkSamplerCreateInfo){VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	createInfo.magFilter = filter;
	createInfo.minFilter = filter;
	createInfo.mipmapMode = mipmapMode;
	createInfo.addressModeU = edgeMode;
	createInfo.addressModeV = edgeMode;
    createInfo.addressModeW = edgeMode;
	createInfo.mipLodBias = 0.0f;
    createInfo.maxAnisotropy = 1.0f;
    createInfo.minLod = -1000;
    createInfo.maxLod = 1000;
	vkCreateSampler(context->device, &createInfo, 0, &result);
	
	return result;
}

static VkDeviceMemory stromboliArenaAllocatorAllocate(StromboliAllocationContext* context, VkMemoryRequirements memoryRequirements, VkDeviceSize* outOffset, void** mapped) {
	StromboliArenaAllocator* allocator = (StromboliArenaAllocator*)context;
	VkDeviceMemory result = {0};
	u64 offset = 0;
	ASSUME(allocator) {
		//u32 memoryTypeIndex = stromboliFindMemoryType(context, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		//ASSERT(memoryTypeIndex == allocator.memoryTypeIndex);
		result = allocator->memory;
		offset = allocator->currentOffset;
		allocator->currentOffset += memoryRequirements.size;

		u64 alignment = memoryRequirements.alignment;
		allocator->currentOffset = ALIGN_UP_POW2(allocator->currentOffset, alignment);
		offset = allocator->currentOffset;
	}

	ASSUME(outOffset) {
		*outOffset = offset;
	}
	if(mapped) {
		ASSERT(allocator->mapped);
		*mapped = (u8*)allocator->mapped + offset;
	}
	return result;
}

static void stromboliArenaAllocatorDeallocate(StromboliAllocationContext* context, VkDeviceMemory memory) {
	// Noop
}

StromboliArenaAllocator stromboliCreateArenaAllocator(StromboliContext* context, u32 memoryProperties, u64 size) {
	StromboliArenaAllocator result = {0};
	result.allocationContext.allocate = &stromboliArenaAllocatorAllocate;
	result.allocationContext.deallocate = &stromboliArenaAllocatorDeallocate;
	VkMemoryAllocateInfo allocationInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	if(!size) {
		size = MB(1);
	}
	allocationInfo.allocationSize = size;
	u32 memoryTypeBits = memoryProperties;
	allocationInfo.memoryTypeIndex = stromboliFindMemoryType(context, memoryTypeBits, 0);
	result.memoryTypeIndex = allocationInfo.memoryTypeIndex;
	vkAllocateMemory(context->device, &allocationInfo, 0, &result.memory);
	
	// We have to check all 3 as we are only checking user requested and not the property of the returned memory
	if((memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) || (memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || (memoryProperties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
		vkMapMemory(context->device, result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped);
	}

	return result;
}

void stromboliFreeArenaAllocator(StromboliContext* context, StromboliArenaAllocator* allocator) {
	ASSUME(allocator && context) {
		if(allocator->mapped) {
			vkUnmapMemory(context->device, allocator->memory);
			allocator->mapped = 0;
		}
		if(allocator->memory) {
			vkFreeMemory(context->device, allocator->memory, 0);
			allocator->memory = 0;
		}
	}
}
