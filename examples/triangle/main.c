#include <grounded/window/grounded_window.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>
#include <grounded/file/grounded_file.h>

#include <stromboli/stromboli.h>

#include <stdio.h>

StromboliSwapchain swapchain;
StromboliPipeline graphicsPipeline;
StromboliRenderpass renderPass;

VkSemaphore imageAcquireSemaphore;
VkSemaphore imageReleaseSemaphore;

StromboliRenderSection mainPass;

void recreateRenderPass(StromboliContext* context, u32 width, u32 height) {
    if(renderPass.renderPass) {
        stromboliRenderpassDestroy(context, &renderPass);
    }
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
}

void resizeApplication(StromboliContext* context, u32 width, u32 height) {
    vkDeviceWaitIdle(context->device);
    stromboliSwapchainResize(context, &swapchain, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, width, height, true, false);
    recreateRenderPass(context, width, height);
}

void createResources(StromboliContext* context) {
    recreateRenderPass(context, swapchain.width, swapchain.height);

    u64 vertexShaderSize = 0;
    u8* vertexShader = groundedReadFileImmutable(STR8_LITERAL("examples/triangle/triangle.vert.spv"), &vertexShaderSize);
    u64 fragmentShaderSize = 0;
    u8* fragmentShader = groundedReadFileImmutable(STR8_LITERAL("examples/triangle/triangle.frag.spv"), &fragmentShaderSize);
    graphicsPipeline = stromboliPipelineCreateGraphics(context, &(struct StromboliGraphicsPipelineParameters) {
        .vertexShader = (StromboliShaderSource){.data = vertexShader, .size = vertexShaderSize},
        .fragmentShader = (StromboliShaderSource){.data = fragmentShader, .size = fragmentShaderSize},
        .renderPass = renderPass.renderPass,
    });
    groundedFreeFileImmutable(vertexShader, vertexShaderSize);
    groundedFreeFileImmutable(fragmentShader, fragmentShaderSize);
    
    {
        VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context->device, &createInfo, 0, &imageAcquireSemaphore);
        vkCreateSemaphore(context->device, &createInfo, 0, &imageReleaseSemaphore);
    }
    mainPass = createRenderSection(context, &context->graphicsQueues[0]);
}

void destroyResources(StromboliContext* context) {
    stromboliRenderpassDestroy(context, &renderPass);
    stromboliPipelineDestroy(context, &graphicsPipeline);
    destroyRenderSection(context, &mainPass);
    vkDestroySemaphore(context->device, imageAcquireSemaphore, 0);
    vkDestroySemaphore(context->device, imageReleaseSemaphore, 0);
}

void renderFrame(StromboliContext* context) {
    vkWaitForFences(context->device, 1, &mainPass.fences[0], true, UINT64_MAX);
    u32 imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain.swapchain, UINT64_MAX, imageAcquireSemaphore, 0, &imageIndex);
    ASSERT(acquireResult == VK_SUCCESS || acquireResult == VK_SUBOPTIMAL_KHR);

    VkCommandBuffer commandBuffer = beginRenderSection(context, &mainPass, 0, "Main pass", 0);
        stromboliCmdBeginRenderpass(commandBuffer, &renderPass, swapchain.width, swapchain.height, imageIndex);
        stromboliCmdSetViewportAndScissor(commandBuffer, swapchain.width, swapchain.height);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.pipeline);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    endRenderSection(context, &mainPass, 0, 1, &imageAcquireSemaphore, &waitMask, 1, &imageReleaseSemaphore);
    float duration = mainPass.durationOfLastCompletedInvocation;
    printf("Duration: %fms\n", duration);

    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &imageReleaseSemaphore;
    VkResult presentResult = vkQueuePresentKHR(context->graphicsQueues[0].queue, &presentInfo);
    ASSERT(presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR);
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
    u32 instanceExtensionCount = 0;
    const char** instanceExtensions = groundedWindowGetVulkanInstanceExtensions(&instanceExtensionCount);
    StromboliResult error = initStromboli(&context, &(StromboliInitializationParameters) {
        .applicationName = applicationName,
        .applicationMajorVersion = 1,
        .additionalInstanceExtensionCount = instanceExtensionCount,
        .additionalInstanceExtensions = instanceExtensions,
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

    swapchain = stromboliSwapchainCreate(&context, groundedWindowGetVulkanSurface(window, context.instance), VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, groundedWindowGetWidth(window), groundedWindowGetHeight(window), true, false);

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
                resizeApplication(&context, events[i].resize.width, events[i].resize.height);
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