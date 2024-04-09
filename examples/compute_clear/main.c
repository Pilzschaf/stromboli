#include <grounded/window/grounded_window.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

#include <stromboli/stromboli.h>

StromboliSwapchain swapchain;
StromboliRenderpass renderPass;
StromboliPipeline computePipeline;
StromboliPipeline graphicsPipeline;

VkCommandPool computeCommandPool;
VkCommandBuffer computeCommandBuffer;
VkCommandPool graphicsCommandPool;
VkCommandBuffer graphicsCommandBuffer;
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
    graphicsPipeline = stromboliPipelineCreateGraphics(context, &(struct StromboliGraphicsPipelineParameters) {
        .vertexShaderFilename = STR8_LITERAL("examples/compute_clear/triangle.vert.spv"),
        .fragmentShaderFilename = STR8_LITERAL("examples/compute_clear/triangle.frag.spv"),
        .renderPass = renderPass.renderPass,
    });

    computePipeline = stromboliPipelineCreateCompute(context, STR8_LITERAL("examples/compute_clear/clear.comp.spv"));
    {
        VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        createInfo.queueFamilyIndex = context->computeQueues[0].familyIndex;
        vkCreateCommandPool(context->device, &createInfo, 0, &computeCommandPool);

        VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = computeCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkAllocateCommandBuffers(context->device, &allocateInfo, &computeCommandBuffer);
    }
    {
        VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        createInfo.queueFamilyIndex = context->graphicsQueues[0].familyIndex;
        vkCreateCommandPool(context->device, &createInfo, 0, &graphicsCommandPool);

        VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = graphicsCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkAllocateCommandBuffers(context->device, &allocateInfo, &graphicsCommandBuffer);
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
    vkDestroyCommandPool(context->device, graphicsCommandPool, 0);
    vkDestroyCommandPool(context->device, computeCommandPool, 0);
    vkDestroyDescriptorPool(context->device, descriptorPool, 0);
    vkDestroySemaphore(context->device, imageAcquireSemaphore, 0);
    stromboliPipelineDestroy(context, &graphicsPipeline);
    stromboliRenderpassDestroy(context, &renderPass);
}

void renderComputeFrame(StromboliContext* context) {
    u32 imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphore, 0, &imageIndex);
    ASSERT(acquireResult == VK_SUCCESS);
    
    vkResetCommandPool(context->device, computeCommandPool, 0);
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    StromboliDescriptorInfo set0[1];
    set0[0] = stromboliCreateImageDescriptor(VK_IMAGE_LAYOUT_GENERAL, swapchain.imageViews[imageIndex], 0);
    vkUpdateDescriptorSetWithTemplateKHR(context->device, descriptorSet, computePipeline.updateTemplates[0], set0);

    vkBeginCommandBuffer(computeCommandBuffer, &beginInfo);
    {
        VkCommandBuffer commandBuffer = computeCommandBuffer;
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
    vkEndCommandBuffer(computeCommandBuffer);

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &computeCommandBuffer;
    submitInfo.pWaitSemaphores = &imageAcquireSemaphore;
    submitInfo.waitSemaphoreCount = 1;
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    submitInfo.pWaitDstStageMask = &waitMask;
    vkQueueSubmit(context->computeQueues[0].queue, 1, &submitInfo, 0);

    //TODO:
    vkDeviceWaitIdle(context->device);

    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    VkResult presentResult = vkQueuePresentKHR(context->computeQueues[0].queue, &presentInfo);
    ASSERT(presentResult == VK_SUCCESS);
}

void renderGraphicsFrame(StromboliContext* context) {
    u32 imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphore, 0, &imageIndex);
    ASSERT(acquireResult == VK_SUCCESS);
    
    vkResetCommandPool(context->device, graphicsCommandPool, 0);
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(graphicsCommandBuffer, &beginInfo);
    {
        VkCommandBuffer commandBuffer = graphicsCommandBuffer;
        VkRenderPassBeginInfo beginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        beginInfo.renderPass = renderPass.renderPass;
        beginInfo.clearValueCount = renderPass.numClearColors;
        beginInfo.pClearValues = renderPass.clearColors;
        beginInfo.framebuffer = renderPass.framebuffers[imageIndex];
        beginInfo.renderArea = (VkRect2D){.offset = {0.0f, 0.0f}, .extent = {swapchain.width, swapchain.height}};
        stromboliCmdSetViewportAndScissor(commandBuffer, swapchain.width, swapchain.height);
        vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.pipeline);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }
    vkEndCommandBuffer(graphicsCommandBuffer);

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &graphicsCommandBuffer;
    submitInfo.pWaitSemaphores = &imageAcquireSemaphore;
    submitInfo.waitSemaphoreCount = 1;
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    submitInfo.pWaitDstStageMask = &waitMask;
    vkQueueSubmit(context->graphicsQueues[0].queue, 1, &submitInfo, 0);

    //TODO:
    vkDeviceWaitIdle(context->device);

    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    VkResult presentResult = vkQueuePresentKHR(context->graphicsQueues[0].queue, &presentInfo);
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
                stromboliSwapchainResize(&context, &swapchain, VK_IMAGE_USAGE_STORAGE_BIT, events[i].resize.width, events[i].resize.height);
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