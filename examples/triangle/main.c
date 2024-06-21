#include <grounded/window/grounded_window.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

#include <stromboli/stromboli.h>

StromboliSwapchain swapchain;
StromboliPipeline graphicsPipeline;
StromboliRenderpass renderPass;

VkCommandPool commandPool;
VkCommandBuffer commandBuffer;
VkSemaphore imageAcquireSemaphore;

void resizeApplication(StromboliContext* context, u32 width, u32 height) {
    vkDeviceWaitIdle(context->device);
    stromboliSwapchainResize(context, &swapchain, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, width, height);
}

void createResources(StromboliContext* context) {
    struct StromboliAttachment outputAttachment = {
        .format = swapchain.format,
        .sampleCount = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .usageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    outputAttachment.clearColor.color = (VkClearColorValue){{0.0f, 0.0f, 0.0f, 1.0f}};
    StromboliSubpass subpass = {
        .outputAttachmentCount = 1,
        .outputAttachments = &outputAttachment,
        .swapchainOutput = &swapchain,
    };
    renderPass = stromboliRenderpassCreate(context, swapchain.width, swapchain.height, 1, &subpass);
    graphicsPipeline = stromboliPipelineCreateGraphics(context, &(struct StromboliGraphicsPipelineParameters) {
        .vertexShaderFilename = STR8_LITERAL("examples/triangle/triangle.vert.spv"),
        .fragmentShaderFilename = STR8_LITERAL("examples/triangle/triangle.frag.spv"),
        .renderPass = renderPass.renderPass,
    });
    {
        VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        createInfo.queueFamilyIndex = context->graphicsQueues[0].familyIndex;
        vkCreateCommandPool(context->device, &createInfo, 0, &commandPool);

        VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkAllocateCommandBuffers(context->device, &allocateInfo, &commandBuffer);
    }
    {
        VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context->device, &createInfo, 0, &imageAcquireSemaphore);
    }
}

void destroyResources(StromboliContext* context) {
    stromboliRenderpassDestroy(context, &renderPass);
    stromboliPipelineDestroy(context, &graphicsPipeline);
    vkDestroyCommandPool(context->device, commandPool, 0);
    vkDestroySemaphore(context->device, imageAcquireSemaphore, 0);
}

void renderFrame(StromboliContext* context) {
    u32 imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphore, 0, &imageIndex);
    ASSERT(acquireResult == VK_SUCCESS);
    
    vkResetCommandPool(context->device, commandPool, 0);
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkRenderPassBeginInfo renderBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .clearValueCount = renderPass.numClearColors,
            .pClearValues = renderPass.clearColors,
            .framebuffer = renderPass.framebuffers[imageIndex],
            .renderArea = (VkRect2D) {.extent = {swapchain.width, swapchain.height}, .offset = {0}},
            .renderPass = renderPass.renderPass,
        };
        vkCmdBeginRenderPass(commandBuffer, &renderBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.pipeline);
        VkViewport viewport = {
            .x = 0, .y = 0, .width = swapchain.width, .height = swapchain.height, .minDepth = 0.0f, .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor = {
            .extent = {swapchain.width, swapchain.height},
            .offset = {0},
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.pWaitSemaphores = &imageAcquireSemaphore;
    submitInfo.waitSemaphoreCount = 1;
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    submitInfo.pWaitDstStageMask = &waitMask;
    vkQueueSubmit(context->graphicsQueues[0].queue, 1, &submitInfo, 0);

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
        .vulkanApiVersion = VK_API_VERSION_1_2,
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
        renderFrame(&context);
    }

    // Wait for device to finish work before releasing resources
    vkDeviceWaitIdle(context.device);

    // Release Vulkan resources
    destroyResources(&context);
    stromboliSwapchainDestroy(&context, &swapchain);
    vkDestroySurfaceKHR(context.instance, swapchain.surface, 0);
    shutdownStromboli(&context);

    // Release window resources
    groundedDestroyWindow(window);
    groundedShutdownWindowSystem();

    return 0;
}