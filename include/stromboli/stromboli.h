#ifndef STROMBOLI_H
#define STROMBOLI_H

#include <grounded/string/grounded_string.h>
#include <volk/volk.h>

#define STROMBOLI_SUCCESS() ((StromboliResult){0})
#define STROMBOLI_MAKE_ERROR(code, text) ((StromboliResult){code, STR8_LITERAL(text)})
#define STROMBOLI_ERROR(result) (result.error)
#define STROMBOLI_NO_ERROR(result) (!STROMBOLI_ERROR(result))

#ifndef MAX_QUEUE_COUNT
#define MAX_QUEUE_COUNT 8
#endif

#ifndef MAX_SWAPCHAIN_IMAGES
#define MAX_SWAPCHAIN_IMAGES 8
#endif

#define STROMBOLI_QUEUE_FLAG_SUPPORTS_PRESENT      0x01
typedef struct StromboliQueue {
	VkQueue queue;
    // Queue submits should be guarded with this mutex
    //GroundedMutex mutex;
	u32 familyIndex;
    u32 flags;
} StromboliQueue;

typedef struct StromboliContext {
    VkInstance instance;
    u32 apiVersion; // VK_API_VERSION_X_X
    VkDebugUtilsMessengerEXT debugCallback;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
    VkPhysicalDeviceLimits physicalDeviceLimits;
    VkPhysicalDeviceFeatures physicalDeviceFeatures;
    VkDevice device;

    // Queues
    StromboliQueue queues[MAX_QUEUE_COUNT];
    StromboliQueue* graphicsQueues;
    StromboliQueue* computeQueues;
    StromboliQueue* transferQueues;
    u32 graphicsQueueCount; // Graphics queues always support present. Other queues might depening on their flags
    u32 computeQueueCount;
    u32 transferQueueCount;

} StromboliContext;

typedef enum StromboliErrorCode {
    STROMBOLI_SUCCESS = 0,
    STROMBOLI_VOLK_INITIALIZE_ERROR,
    STROMBOLI_INSTANCE_CREATE_ERROR,
    STROMBOLI_NO_SUITABLE_GPUS,
    STROMBOLI_DEVICE_CREATE_ERROR,
} StromboliErrorCode;

typedef struct StromboliResult {
    int error;
    String8 errorString;
} StromboliResult;

typedef struct StromboliSwapchain {
    VkSwapchainKHR swapchain;
    VkSurfaceKHR surface;
    u32 width;
    u32 height;
    VkFormat format;
    u32 numImages;

    VkImage images[MAX_SWAPCHAIN_IMAGES];
    VkImageView imageViews[MAX_SWAPCHAIN_IMAGES];
} StromboliSwapchain;

enum StromboliPipelineType {
    STROMBOLI_PIPELINE_TYPE_COMPUTE,
    STROMBOLI_PIPELINE_TYPE_COUNT,
};

typedef struct StromboliPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;

    VkDescriptorSetLayout descriptorLayouts[4];

    enum StromboliPipelineType type;
    union {
        u32 workgroupWidth;
        u32 workgroupHeight;
        u32 workgroupDepth;
    } compute;
} StromboliPipeline;

typedef struct StromboliInitializationParameters {
    u32 additionalInstanceExtensionCount;
    u32 additionalDeviceExtensionCount;
    const char** additionalInstanceExtensions;
    const char** additionalDeviceExtensions;
    // Callback so the applciation does not have to do heap allocations for the instance extension list
    const char** (*platformGetRequiredNativeInstanceExtensions) (u32*); // A value of 0 indicates that no instance extensions are required by the given backend

    String8 applicationName;
    u32 applicationMajorVersion;
    u32 applicationMinorVersion;
    u32 applicationPatchVersion;
    u32 vulkanApiVersion;

    u32 additionalGraphicsQueueRequestCount; // The maximum number of additional graphics queues. Actual available count might be less
    u32 computeQueueRequestCount; // The maximum number of comptue queues. Actual available count might be less
    u32 transferQueueRequestCount; // The maximum number of transfre queues. Actual available count might be less

    bool enableValidation;
    bool enableSynchronizationValidation;
    bool enableBestPracticeWarning;
    bool enableShaderDebugPrintf; // Printf buffer size can be enlarged by placing vk_layer_settings.txt file in executable working directory with content: khronos_validation.printf_buffer_size = YOUR_BUFFER_SIZE     Default size is 1024
    bool enableGpuAssistedValidation;
    bool enableGpuReservedBindingSlot; // Can only be activated in conjunction with enableGpuAssistedValidation
    bool enableApiDump;

    // Features
    bool disableSwapchain;
    bool calibratedTimestamps;
    bool sampleRateShadingFeature;
    bool fillModeNonSolidFeature;
    bool fragmentStoresAndAtomicsFeature;
    bool independentBlendFeature;
    bool samplerAnisotropyFeature;
    bool bufferDeviceAddress;
    bool scalarBlockLayout;
    bool dynamicRendering;
    bool synchronization2;
    bool maintenance4;
    bool rayQuery;
    bool accelerationStructure;
    bool rayTracingPipeline;
    bool nonUniformIndexingSampledImageArray;
    bool runtimeDescriptorArray;
    bool descriptorBindingVariableDescriptorCount;
    bool descriptorBindingSampledImageUpdateAfterBind;
    bool descriptorBindingPartiallyBound;
} StromboliInitializationParameters;

StromboliResult initStromboli(StromboliContext* context, StromboliInitializationParameters* parameters);
void shutdownStromboli(StromboliContext* context);

StromboliSwapchain stromboliSwapchainCreate(StromboliContext* context, VkSurfaceKHR surface, VkImageUsageFlags usage, u32 width, u32 height);
// Resizing of swapchain recreates images, image views etc. stored in the swapchain. Swapchain should not be in use anymore
bool stromboliSwapchainResize(StromboliContext* context, StromboliSwapchain* swapchain, VkImageUsageFlags usage, u32 width, u32 height);
void stromboliSwapchainDestroy(StromboliContext* context, StromboliSwapchain* swapchain);

StromboliPipeline stromboliPipelineCreateCompute(StromboliContext* context, String8 filename);
void stromboliPipelineDestroy(StromboliContext* context, StromboliPipeline* pipeline);

#define STROMBOLI_NAME_OBJECT_EXPLICIT(context, object, type, name) stromboliNameObject(context, INT_FROM_PTR(object), type, name)
#define STROMBOLI_NAME_OBJECT(context, object, type) STROMBOLI_NAME_OBJECT_EXPLICIT(context, object, type, #object)
static inline void stromboliNameObject(StromboliContext* context, u64 handle, VkObjectType type, const char* name) {
	if (vkSetDebugUtilsObjectNameEXT) {
		VkDebugUtilsObjectNameInfoEXT nameInfo = { 0 };
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = type;
		nameInfo.objectHandle = handle;
		nameInfo.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
	}
}

#endif // STROMBOLI_H