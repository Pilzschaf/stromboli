#define VOLK_IMPLEMENTATION
#include <volk/volk.h>

#include <stromboli/stromboli.h>

#include <grounded/memory/grounded_arena.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

StromboliResult initVulkanInstance(StromboliContext* context, StromboliInitializationParameters* parameters) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    ASSERT(parameters);
    StromboliResult error = STROMBOLI_SUCCESS();
    VkResult result = VK_SUCCESS;

    // Check if volk is already loaded
    u32 volkInstanceVersion = volkGetInstanceVersion();
    if(!volkInstanceVersion) {
        result = volkInitialize();
        if(result != VK_SUCCESS) {
            error = STROMBOLI_MAKE_ERROR(STROMBOLI_VOLK_INITIALIZE_ERROR, "Failed to initialize volk");
        }
    }

    if(STROMBOLI_NO_ERROR(error)) { // Init instance
        VkApplicationInfo applicationInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = str8GetCstrOrNull(scratch, parameters->applicationName);
        applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, parameters->applicationMajorVersion, parameters->applicationMinorVersion, parameters->applicationPatchVersion);
        applicationInfo.pEngineName = "Stromboli";
        applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        applicationInfo.apiVersion = parameters->vulkanApiVersion ? parameters->vulkanApiVersion : VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &applicationInfo;
        /*createInfo.enabledLayerCount = enabledLayerCount;
        createInfo.ppEnabledLayerNames = enabledLayers;
        createInfo.enabledExtensionCount = requestedInstanceExtensionCount;
        createInfo.ppEnabledExtensionNames = requestedInstanceExtensions;*/
        result = vkCreateInstance(&createInfo, 0, &context->instance);
        if(result != VK_SUCCESS) {
            error = STROMBOLI_MAKE_ERROR(STROMBOLI_INSTANCE_CREATE_ERROR, "Could not create vulkan instance. Make sure you are using the latest graphics driver");
        } else {
            volkLoadInstanceOnly(context->instance);
        }
    }

    arenaEndTemp(temp);
    return error;
}

StromboliResult initStromboli(StromboliContext* context, StromboliInitializationParameters* parameters) {
    *context = (StromboliContext){0};
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);
    if(!parameters) {
        // Those must be handled as readonly
        static StromboliInitializationParameters defaultParameters = {0};
        ASSERT(MEMORY_IS_ZERO(&defaultParameters, sizeof(defaultParameters)));
        parameters = &defaultParameters;
    }
    ASSERT(parameters);

    StromboliResult error = initVulkanInstance(context, parameters);

    arenaEndTemp(temp);
    return error;
}

void shutdownStromboli(StromboliContext* context) {
    vkDestroyInstance(context->instance, 0);
}
