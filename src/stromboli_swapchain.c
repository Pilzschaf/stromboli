#include <stromboli/stromboli.h>
#include <grounded/memory/grounded_arena.h>
#include <grounded/logger/grounded_logger.h>
#include <grounded/threading/grounded_threading.h>

#include <stdio.h>

static bool createSwapchain(StromboliContext* context, StromboliSwapchain* swapchain, VkImageUsageFlags usage, u32 width, u32 height, bool vsync, bool mailbox) {
    const char* error = 0;
    VkResult result;
    VkSurfaceFormatKHR* availableFormats = 0;
    u32 numFormats = 0;
	VkSwapchainKHR oldSwapchain = swapchain->swapchain;
	bool skipDestroyOnError = false;

    MemoryArena* scratch = threadContextGetScratch(0);
	ArenaTempMemory temp = arenaBeginTemp(scratch);

    if(!swapchain->surface) {
		error = "Invalid surface";
	}

    if(!error) { // We assume that the graphics queue always supports present. Spec does not guarantee this!
		VkBool32 supportsPresent = 0;
		result = vkGetPhysicalDeviceSurfaceSupportKHR(context->physicalDevice, context->graphicsQueues[0].familyIndex, swapchain->surface, &supportsPresent);
		if(result != VK_SUCCESS) {
			error = "Could not determine whether graphics queue supports present.";
		} else if (!supportsPresent) {
			error = "Graphics queue does not support present";
		}
	}

    for(u32 i = 0; i < MAX_QUEUE_COUNT; ++i) {
		if(context->queues[i].queue) {
			VkBool32 supportsPresent = 0;
			vkGetPhysicalDeviceSurfaceSupportKHR(context->physicalDevice, context->queues[i].familyIndex, swapchain->surface, &supportsPresent);
			if(supportsPresent) {
				context->queues[i].flags |= STROMBOLI_QUEUE_FLAG_SUPPORTS_PRESENT;
			} else {
				context->queues[i].flags &= ~STROMBOLI_QUEUE_FLAG_SUPPORTS_PRESENT;
			}
        }
	}

	if(!error) {
		result = vkGetPhysicalDeviceSurfaceFormatsKHR(context->physicalDevice, swapchain->surface, &numFormats, 0);
		if(numFormats <= 0 || result != VK_SUCCESS) {
			error = "Could not query device surface formats";
		} else {
			availableFormats = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, numFormats, VkSurfaceFormatKHR);
			result = vkGetPhysicalDeviceSurfaceFormatsKHR(context->physicalDevice, swapchain->surface, &numFormats, availableFormats);
			if (numFormats <= 0 || result != VK_SUCCESS) {
				error = "No surface formats available";
			}
		}
	}

    if(!error) {
        // First available format should be a sensible default in most cases
		VkFormat format = availableFormats[0].format;
		VkColorSpaceKHR colorSpace = availableFormats[0].colorSpace;

        for(u32 i = 0; i < numFormats; ++i) {
			format = availableFormats[i].format;
			colorSpace = availableFormats[i].colorSpace;

			VkImageFormatProperties formatProperties;
			VkResult formatResult = vkGetPhysicalDeviceImageFormatProperties(context->physicalDevice, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &formatProperties);
			if(formatResult == VK_ERROR_FORMAT_NOT_SUPPORTED) {
				GROUNDED_LOG_WARNING("Swapchain format does not support requested usage flags");
			} else {
				break;
			}
		}

        VkSurfaceCapabilitiesKHR surfaceCapabilities;
		result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physicalDevice, swapchain->surface, &surfaceCapabilities);
		surfaceCapabilities.currentExtent.width = width;
		surfaceCapabilities.currentExtent.height = height;
		if (surfaceCapabilities.currentExtent.width == 0 || surfaceCapabilities.currentExtent.height == 0) {
			// Invalid size. Application is probably minimized
			error = "Invalid size. Try creating the swapchain again later";
			skipDestroyOnError = true;
		}
		surfaceCapabilities.currentExtent.width = CLAMP(surfaceCapabilities.minImageExtent.width, surfaceCapabilities.currentExtent.width, surfaceCapabilities.maxImageExtent.width);
		surfaceCapabilities.currentExtent.height = CLAMP(surfaceCapabilities.minImageExtent.height, surfaceCapabilities.currentExtent.height, surfaceCapabilities.maxImageExtent.height);
		
		if (surfaceCapabilities.maxImageCount == 0 || surfaceCapabilities.maxImageCount > MAX_SWAPCHAIN_IMAGES) {
			surfaceCapabilities.maxImageCount = MAX_SWAPCHAIN_IMAGES;
		}
		if(result != VK_SUCCESS) {
			error = "Error querying surface capabilities";
		}

        if(!error) {
			VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
			createInfo.surface = swapchain->surface;
			createInfo.minImageCount = CLAMP(surfaceCapabilities.minImageCount, 3, surfaceCapabilities.maxImageCount);
			createInfo.imageFormat = format;
			createInfo.imageColorSpace = colorSpace;
			createInfo.imageExtent = surfaceCapabilities.currentExtent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = usage;
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			VkPresentModeKHR presentMode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
			if(mailbox) {
				presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
			createInfo.presentMode = presentMode;
			createInfo.oldSwapchain = oldSwapchain ? oldSwapchain : 0;
			result = vkCreateSwapchainKHR(context->device, &createInfo, 0, &swapchain->swapchain);
			if(result != VK_SUCCESS) {
				error = "Error creating swapchain";
			}
			swapchain->format = format;
			swapchain->width = surfaceCapabilities.currentExtent.width;
			swapchain->height = surfaceCapabilities.currentExtent.height;
		}

        if(!error) { // Acquire swapchain images
			if(swapchain->imageViews[0]) {
				for (u32 i = 0; i < swapchain->numImages; ++i) {
					vkDestroyImageView(context->device, swapchain->imageViews[i], 0);
				}
			}
			if(oldSwapchain) {
				vkDestroySwapchainKHR(context->device, oldSwapchain, 0);
			}

			result = vkGetSwapchainImagesKHR(context->device, swapchain->swapchain, &swapchain->numImages, 0);
			ASSERT(swapchain->numImages <= MAX_SWAPCHAIN_IMAGES);
			result = vkGetSwapchainImagesKHR(context->device, swapchain->swapchain, &swapchain->numImages, swapchain->images);

			// Create image views
			for (u32 i = 0; i < swapchain->numImages; ++i) {
				STROMBOLI_NAME_OBJECT_EXPLICIT(context, swapchain->images[i], VK_OBJECT_TYPE_IMAGE, "Swapchain image");
				VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				createInfo.image = swapchain->images[i];
				createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				createInfo.format = format;
				createInfo.components = (VkComponentMapping){0};
				createInfo.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				result = vkCreateImageView(context->device, &createInfo, 0, &swapchain->imageViews[i]);
			}
		}
    }

    if(error && !skipDestroyOnError) {
		if(swapchain->imageViews[0]) {
			for (u32 i = 0; i < swapchain->numImages; ++i) {
				vkDestroyImageView(context->device, swapchain->imageViews[i], 0);
			}
		}
		if(swapchain->swapchain) {
			vkDestroySwapchainKHR(context->device, swapchain->swapchain, 0);
		}
		
		printf("Error creating swapchain: %s\n", error);
	}

    arenaEndTemp(temp);

    return !error;
}

StromboliSwapchain stromboliSwapchainCreate(StromboliContext* context, VkSurfaceKHR surface, VkImageUsageFlags usage, u32 width, u32 height, bool vsync, bool mailbox) {
    StromboliSwapchain result = {0};
	result.surface = surface;

	createSwapchain(context, &result, usage, width, height, vsync, mailbox);

	return result;
}

void stromboliSwapchainDestroy(StromboliContext* context, StromboliSwapchain* swapchain) {
    for (u32 i = 0; i < swapchain->numImages; ++i) {
		vkDestroyImageView(context->device, swapchain->imageViews[i], 0);
	}
	vkDestroySwapchainKHR(context->device, swapchain->swapchain, 0);
    swapchain->swapchain = 0;
}

bool stromboliSwapchainResize(StromboliContext* context, StromboliSwapchain* swapchain, VkImageUsageFlags usage, u32 width, u32 height, bool vsync, bool mailbox) {
	return createSwapchain(context, swapchain, usage, width, height, vsync, mailbox);
}
