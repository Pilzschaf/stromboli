#include <stromboli/stromboli.h>

static bool isDepthFormat(VkFormat format) {
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

StromboliImage stromboliImageCreate(StromboliContext* context, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, struct StromboliImageParameters* parameters) {
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
		createInfo.samples = parameters->sampleCount == 0 ? VK_SAMPLE_COUNT_1_BIT : parameters->sampleCount;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		//vkCreateImage(context->device, &createInfo, 0, &image->image);
		VmaAllocationCreateInfo allocInfo = {0};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		if(parameters->requireCPUAccess) {
			ASSERT(parameters->tiling == VK_IMAGE_TILING_LINEAR);
			allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}
		vmaCreateImage(context->vmaAllocator, &createInfo, &allocInfo, &result.image, &result.allocation, 0);
	}

	/*VkMemoryRequirements memoryRequirements;
	#if 0
	vkGetImageMemoryRequirements(context->device, image->image, &memoryRequirements);
	#else
	VkImageMemoryRequirementsInfo2 memoryInfo = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
	memoryInfo.image = image->image;
	
	VkMemoryDedicatedRequirements dedicatedRequirements = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};

	VkMemoryRequirements2 imageMemoryRequirements = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
	imageMemoryRequirements.pNext = &dedicatedRequirements;
	vkGetImageMemoryRequirements2(context->device, &memoryInfo, &imageMemoryRequirements);

	memoryRequirements = imageMemoryRequirements.memoryRequirements;
	if(dedicatedRequirements.prefersDedicatedAllocation) {
		printf("Prefers dedicated allocation\n");
	}
	#endif
	
	VkMemoryAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = findMemoryType(context, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkAllocateMemory(context->device, &allocateInfo, 0, &image->memory);
	vkBindImageMemory(context->device, image->image, image->memory, 0);*/

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
		vkCreateImageView(context->device, &createInfo, 0, &result.view);
	}
	result.width = width;
	result.height = height;
	result.depth = depth;
	result.mipCount = mipChainLength;
	return result;
}

void stromboliImageDestroy(StromboliContext* context, StromboliImage* image) {
	vkDestroyImageView(context->device, image->view, 0);
	//vkDestroyImage(context->device, image->image, 0);
	//vkFreeMemory(context->device, image->memory, 0);
	vmaDestroyImage(context->vmaAllocator, image->image, image->allocation);
}
