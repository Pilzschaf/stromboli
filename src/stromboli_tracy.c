#include <tracy/TracyC.h>
#include <stromboli/stromboli.h>
#include <stromboli/stromboli_tracy.h>

static VkCommandPool tracyCommandPool = 0;
static VkCommandBuffer tracyCommandBuffer = 0;
static TracyStromboliContext tracyContext = {0};
u32 stromboliTracyContextCounter = 0;

void initStromboliTracyContext(StromboliContext* context) {
    {
        VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        //createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = context->graphicsQueues[0].familyIndex;
        vkCreateCommandPool(context->device, &createInfo, 0, &tracyCommandPool);

        VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = tracyCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(context->device, &allocateInfo, &tracyCommandBuffer);
    }

    tracyContext = TracyCVkContextCalibrated(context->physicalDevice, context->device, context->graphicsQueues[0].queue, tracyCommandBuffer, tracyCommandPool, vkGetPhysicalDeviceCalibrateableTimeDomainsEXT, vkGetCalibratedTimestampsEXT);
}

void destroyStromboliTracyContext(StromboliContext* context) {
    TracyCVkDestroy(&tracyContext);
    if(tracyCommandPool) {
        vkDestroyCommandPool(context->device, tracyCommandPool, 0);
    }
}

void collectTracyStromboli(VkCommandBuffer commandBuffer) {
    TracyCVkCollect(&tracyContext, commandBuffer);
}

TracyStromboliContext* getTracyContext() {
    return &tracyContext;
}
