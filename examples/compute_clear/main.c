#include <grounded/window/grounded_window.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

#include <stromboli/stromboli.h>

// Next time: Utils and first compute frame?

StromboliSwapchain swapchain;

void resizeApplication(StromboliContext* context, u32 width, u32 height) {
    vkDeviceWaitIdle(context->device);
    stromboliSwapchainResize(context, &swapchain, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, width, height);
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
    GroundedWindow* window = groundedCreateWindow(&(struct GroundedWindowCreateParameters){
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
    });
    if(STROMBOLI_ERROR(error)) {
        ASSERT(false);
    }

    swapchain = stromboliSwapchainCreate(&context, groundedWindowGetVulkanSurface(window, context.instance), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, groundedGetWindowWidth(window), groundedGetWindowHeight(window));

    StromboliPipeline computePipeline = stromboliPipelineCreateCompute(&context, STR8_LITERAL("examples/compute_clear/clear.comp.spv"));

    // Message loop
    u32 eventCount = 0;
    bool running = true;
    while(running) {
        GroundedEvent* events = groundedPollEvents(&eventCount);
        for(u32 i = 0; i < eventCount; ++i) {
            if(events[i].type == GROUNDED_EVENT_TYPE_CLOSE_REQUEST) {
                running = false;
                break;
            }
            if(events[i].type == GROUNDED_EVENT_TYPE_RESIZE) {
                stromboliSwapchainResize(&context, &swapchain, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, events[i].resize.width, events[i].resize.height);
            }
        }

        // Do your per-frame work here
    }

    vkDeviceWaitIdle(context.device);

    stromboliPipelineDestroy(&context, &computePipeline);
    stromboliSwapchainDestroy(&context, &swapchain);
    vkDestroySurfaceKHR(context.instance, swapchain.surface, 0);
    shutdownStromboli(&context);

    // Release resources
    groundedDestroyWindow(window);
    groundedShutdownWindowSystem();

    return 0;
}