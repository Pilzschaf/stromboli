#include <grounded/window/grounded_window.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

#include <stromboli/stromboli.h>

StromboliSwapchain swapchain;
StromboliPipeline computePipeline;

VkCommandPool commandPool;
VkCommandBuffer commandBuffer;
VkSemaphore imageAcquireSemaphore;
VkDescriptorPool descriptorPool;
VkDescriptorSet descriptorSet;

// Next time: Graphics pipeline
// Renderpasses

void resizeApplication(StromboliContext* context, u32 width, u32 height) {
    vkDeviceWaitIdle(context->device);
    stromboliSwapchainResize(context, &swapchain, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, width, height);
}

void createResources(StromboliContext* context) {
    computePipeline = stromboliPipelineCreateCompute(context, STR8_LITERAL("examples/compute_clear/clear.comp.spv"));
    {
        VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        createInfo.queueFamilyIndex = context->computeQueues[0].familyIndex;
        vkCreateCommandPool(context->device, &createInfo, 0, &commandPool);

        VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkAllocateCommandBuffers(context->device, &allocateInfo, &commandBuffer);
    }
    {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        };
        VkDescriptorPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        createInfo.maxSets = 1;
        createInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
        createInfo.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(context->device, &createInfo, 0, &descriptorPool);

        VkDescriptorSetAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &computePipeline.descriptorLayouts[0];
        vkAllocateDescriptorSets(context->device, &allocateInfo, &descriptorSet);
    }
    {
        VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context->device, &createInfo, 0, &imageAcquireSemaphore);
    }
}

void destroyResources(StromboliContext* context) {
    vkDestroyCommandPool(context->device, commandPool, 0);
    vkDestroyDescriptorPool(context->device, descriptorPool, 0);
    vkDestroySemaphore(context->device, imageAcquireSemaphore, 0);
}

void renderFrame(StromboliContext* context) {
    u32 imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphore, 0, &imageIndex);
    ASSERT(acquireResult == VK_SUCCESS);
    
    vkResetCommandPool(context->device, commandPool, 0);
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    StromboliDescriptorInfo set0[1];
    set0[0] = stromboliCreateImageDescriptor(VK_IMAGE_LAYOUT_GENERAL, swapchain.imageViews[imageIndex], 0);
    vkUpdateDescriptorSetWithTemplateKHR(context->device, descriptorSet, computePipeline.updateTemplates[0], set0);

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
        { // Swapchain image Undefined -> General
            VkImageMemoryBarrier2KHR imageBarrier = stromboliCreateImageBarrier(swapchain.images[imageIndex],
            /*Src*/ VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
            /*Dst*/ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
            stromboliPipelineBarrier(commandBuffer, 0, 0, 0, 1, &imageBarrier);
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.layout, 0, 1, &descriptorSet, 0, 0);
        u32 workgroupWidth = computePipeline.compute.workgroupWidth;
        u32 workgroupHeight = computePipeline.compute.workgroupHeight; 
        vkCmdDispatch(commandBuffer, (swapchain.width + workgroupWidth - 1) / workgroupWidth, (swapchain.height + workgroupHeight - 1) / workgroupHeight, 1);

        { // Swapchain image General -> Present
            VkImageMemoryBarrier2KHR imageBarrier = stromboliCreateImageBarrier(swapchain.images[imageIndex], 
            /*Src*/ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            /*Dst*/ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            stromboliPipelineBarrier(commandBuffer, 0, 0, 0, 1, &imageBarrier);
        }
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.pWaitSemaphores = &imageAcquireSemaphore;
    submitInfo.waitSemaphoreCount = 1;
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    submitInfo.pWaitDstStageMask = &waitMask;
    vkQueueSubmit(context->computeQueues[0].queue, 1, &submitInfo, 0);

    vkDeviceWaitIdle(context->device);

    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    VkResult presentResult = vkQueuePresentKHR(context->computeQueues[0].queue, &presentInfo);
    ASSERT(presentResult == VK_SUCCESS);
}

int main(int argc, char** argv) {
    { // Thread context initialization
        MemoryArena arena1 = createGrowingArena(osGetMemorySubsystem(), KB(256));
        MemoryArena arena2 = createGrowingArena(osGetMemorySubsystem(), KB(16));

        threadContextInit(arena1, arena2, &groundedDefaultConsoleLogger);
    }

    groundedInitWindowSystem();
    String8 applicationName = STR8_LITERAL("Vulkan Compute Clear");

    // Create window
    GroundedWindow* window = groundedCreateWindow(threadContextGetScratch(0), &(struct GroundedWindowCreateParameters){
        .title = applicationName,
        .width = 1920,
        .height = 1080,
    });

    StromboliContext context = {0};
    StromboliResult error = initStromboli(&context, &(StromboliInitializationParameters) {
        .applicationName = applicationName,
        .applicationMajorVersion = 1,
        .platformGetRequiredNativeInstanceExtensions = groundedWindowGetVulkanInstanceExtensions,
        .enableValidation = true,
        .enableSynchronizationValidation = true,
        .synchronization2 = true,
        .computeQueueRequestCount = 1,
        .descriptorUpdateTemplate = true,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    });
    if(STROMBOLI_ERROR(error)) {
        ASSERT(false);
        return 1;
    }

    swapchain = stromboliSwapchainCreate(&context, groundedWindowGetVulkanSurface(window, context.instance), VK_IMAGE_USAGE_STORAGE_BIT, groundedWindowGetWidth(window), groundedWindowGetHeight(window));

    createResources(&context);

    // Message loop
    u32 eventCount = 0;
    bool running = true;
    while(running) {
        GroundedEvent* events = groundedWindowPollEvents(&eventCount);
        for(u32 i = 0; i < eventCount; ++i) {
            if(events[i].type == GROUNDED_EVENT_TYPE_CLOSE_REQUEST) {
                running = false;
                break;
            }
            if(events[i].type == GROUNDED_EVENT_TYPE_RESIZE) {
                stromboliSwapchainResize(&context, &swapchain, VK_IMAGE_USAGE_STORAGE_BIT, events[i].resize.width, events[i].resize.height);
            }
        }

        // Do your per-frame work here
        renderFrame(&context);
    }

    vkDeviceWaitIdle(context.device);

    destroyResources(&context);
    stromboliPipelineDestroy(&context, &computePipeline);
    stromboliSwapchainDestroy(&context, &swapchain);
    vkDestroySurfaceKHR(context.instance, swapchain.surface, 0);
    shutdownStromboli(&context);

    // Release resources
    groundedDestroyWindow(window);
    groundedShutdownWindowSystem();

    return 0;
}