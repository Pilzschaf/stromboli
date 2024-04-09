#include <grounded/window/grounded_window.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

#include <stromboli/stromboli.h>

#ifndef MAX_FRAMES_IN_FLIGHT
#define MAX_FRAMES_IN_FLIGHT 1
#endif

StromboliSwapchain swapchain;
StromboliRenderpass renderPass;
StromboliPipeline computePipeline;
StromboliPipeline graphicsPipeline;
StromboliRenderSection graphicsSection;
StromboliRenderSection computeSection;

VkSemaphore imageAcquireSemaphores[MAX_FRAMES_IN_FLIGHT];
VkSemaphore imageReleaseSemaphores[MAX_FRAMES_IN_FLIGHT];
VkDescriptorPool descriptorPool;
VkDescriptorSet descriptorSet;

void recreateRenderpass(StromboliContext* context) {
    if(renderPass.renderPass) {
        stromboliRenderpassDestroy(context, &renderPass);
        MEMORY_CLEAR_STRUCT(&renderPass);
    }
    struct StromboliAttachment outputAttachments[] = {
        { // Color attachment
            .clearColor.color = (VkClearColorValue){{0.4f, 0.05f, 1.0f, 1.0f}},
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .usageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .format = swapchain.format,
        },
    };
    StromboliSubpass subpass = {
        .outputAttachmentCount = ARRAY_COUNT(outputAttachments),
        .outputAttachments = outputAttachments,
        .swapchainOutput = &swapchain,
    };
    renderPass = stromboliRenderpassCreate(context, swapchain.width, swapchain.height, 1, &subpass);
}

void resizeApplication(StromboliContext* context, u32 width, u32 height) {
    vkDeviceWaitIdle(context->device);
    stromboliSwapchainResize(context, &swapchain, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, width, height);
    recreateRenderpass(context);
}

void createResources(StromboliContext* context) {
    recreateRenderpass(context);
    graphicsPipeline = stromboliPipelineCreateGraphics(context, &(struct StromboliGraphicsPipelineParameters) {
        .vertexShaderFilename = STR8_LITERAL("examples/compute_clear/triangle.vert.spv"),
        .fragmentShaderFilename = STR8_LITERAL("examples/compute_clear/triangle.frag.spv"),
        .renderPass = renderPass.renderPass,
    });
    computePipeline = stromboliPipelineCreateCompute(context, STR8_LITERAL("examples/compute_clear/clear.comp.spv"));

    graphicsSection = createRenderSection(context, &context->graphicsQueues[0]);
    computeSection = createRenderSection(context, &context->computeQueues[0]);
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
    for(u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context->device, &createInfo, 0, &imageAcquireSemaphores[i]);
        vkCreateSemaphore(context->device, &createInfo, 0, &imageReleaseSemaphores[i]);
    }
}

void destroyResources(StromboliContext* context) {
    destroyRenderSection(context, &graphicsSection);
    destroyRenderSection(context, &computeSection);
    vkDestroyDescriptorPool(context->device, descriptorPool, 0);
    for(u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(context->device, imageAcquireSemaphores[i], 0);
        vkDestroySemaphore(context->device, imageReleaseSemaphores[i], 0);
    }
    stromboliPipelineDestroy(context, &graphicsPipeline);
    stromboliRenderpassDestroy(context, &renderPass);
}

void renderComputeFrame(StromboliContext* context) {
    u32 imageIndex = 0;
    static u32 frameIndex = 0;
    VkCommandBuffer commandBuffer = beginRenderSection(context, &computeSection, frameIndex, "Compute");
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphores[frameIndex], 0, &imageIndex);
    ASSERT(acquireResult == VK_SUCCESS);

    StromboliDescriptorInfo set0[1];
    set0[0] = stromboliCreateImageDescriptor(VK_IMAGE_LAYOUT_GENERAL, swapchain.imageViews[imageIndex], 0);
    vkUpdateDescriptorSetWithTemplateKHR(context->device, descriptorSet, computePipeline.updateTemplates[0], set0);
    {
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
    }
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    endRenderSection(context, &computeSection, frameIndex, 1, &imageAcquireSemaphores[frameIndex], &waitMask, 1, &imageReleaseSemaphores[frameIndex]);

    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &imageReleaseSemaphores[frameIndex];
    VkResult presentResult = vkQueuePresentKHR(context->computeQueues[0].queue, &presentInfo);
    ASSERT(presentResult == VK_SUCCESS);

    frameIndex = (frameIndex+1) % MAX_FRAMES_IN_FLIGHT;
}

void renderGraphicsFrame(StromboliContext* context) {
    static u32 frameIndex = 0;
    u32 imageIndex = 0;
    
    VkCommandBuffer commandBuffer = beginRenderSection(context, &graphicsSection, frameIndex, "Graphics");
    {
        VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphores[frameIndex], 0, &imageIndex);
        ASSERT(acquireResult == VK_SUCCESS);

        stromboliCmdSetViewportAndScissor(commandBuffer, swapchain.width, swapchain.height);
        stromboliCmdBeginRenderpass(commandBuffer, &renderPass, swapchain.width, swapchain.height, imageIndex);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.pipeline);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    endRenderSection(context, &graphicsSection, frameIndex, 1, &imageAcquireSemaphores[frameIndex], &waitMask, 1, &imageReleaseSemaphores[frameIndex]);

    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &imageReleaseSemaphores[frameIndex];
    VkResult presentResult = vkQueuePresentKHR(context->graphicsQueues[0].queue, &presentInfo);
    ASSERT(presentResult == VK_SUCCESS);

    frameIndex = (frameIndex+1) % MAX_FRAMES_IN_FLIGHT;
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
        .vulkanApiVersion = VK_API_VERSION_1_0,
    });
    if(STROMBOLI_ERROR(error)) {
        ASSERT(false);
        return 1;
    }

    swapchain = stromboliSwapchainCreate(&context, groundedWindowGetVulkanSurface(window, context.instance), VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, groundedWindowGetWidth(window), groundedWindowGetHeight(window));

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
                vkDeviceWaitIdle(context.device);
                resizeApplication(&context, events[i].resize.width, events[i].resize.height);
            }
        }

        // Do your per-frame work here
        //renderComputeFrame(&context);
        renderGraphicsFrame(&context);
    }

    // Wait for device to finish work before releasing resources
    vkDeviceWaitIdle(context.device);

    // Release Vulkan resources
    destroyResources(&context);
    stromboliPipelineDestroy(&context, &computePipeline);
    stromboliSwapchainDestroy(&context, &swapchain);
    vkDestroySurfaceKHR(context.instance, swapchain.surface, 0);
    shutdownStromboli(&context);

    // Release window resources
    groundedDestroyWindow(window);
    groundedShutdownWindowSystem();

    return 0;
}