#ifndef STROMBOLI_H
#define STROMBOLI_H

#include <grounded/string/grounded_string.h>
#include <volk/volk.h>
#include <vk_mem_alloc.h>

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

#ifndef STROMBOLI_NO_VMA
    VmaAllocator vmaAllocator;
#endif
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

typedef struct StromboliBuffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
    u64 size;
    void* mapped; // If this buffer is host visible this points to the buffer data
    VmaAllocation allocation;
} StromboliBuffer;

enum StromboliPipelineType {
    STROMBOLI_PIPELINE_TYPE_COMPUTE,
    STROMBOLI_PIPELINE_TYPE_GRAPHICS,
    STROMBOLI_PIPELINE_TYPE_RAYTRACING,
    STROMBOLI_PIPELINE_TYPE_COUNT,
};

typedef struct StromboliPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;

    VkDescriptorSetLayout descriptorLayouts[4];
    VkDescriptorUpdateTemplate updateTemplates[4];

    enum StromboliPipelineType type;
    union {
        struct {
            u32 workgroupWidth;
            u32 workgroupHeight;
            u32 workgroupDepth;
        } compute;
        struct {
            StromboliBuffer sbtBuffer;
            VkStridedDeviceAddressRegionKHR sbtRayGenRegion;
            VkStridedDeviceAddressRegionKHR sbtMissRegion;
            VkStridedDeviceAddressRegionKHR sbtHitRegion;
            VkStridedDeviceAddressRegionKHR sbtCallableRegion;
        } raytracing;
    };
} StromboliPipeline;

// Pipelines must be built with explicit formats for its attachments. How to handle switchable formats? Could do complicated stuff like recreating on demand etc.
// But best and simplest solution would be a multi format pipeline where upon build time a certain set of supported format combinations is passed in and the pipeline is built for each of them.
// Upon rendering we retrieve the pipeline for our current configuration
typedef struct StromboliMultiFormatPipeline {
    StromboliPipeline pipeline;
    struct StromboliPipelineFormatEntry {
        VkFormat format;
        VkPipeline pipeline;
    } entries[16];
} StromboliMultiFormatPipeline;

typedef struct {
    VkAccelerationStructureKHR accelerationStructure;
    StromboliBuffer accelerationStructureBuffer;
} StromboliAccelerationStructure;

typedef struct StromboliImage {
    VkImage image;
    VkImageView view;
    u32 width, height, depth, mipCount;
    VkFormat format;
    VkSampleCountFlagBits samples;

#ifdef STROMBOLI_NO_VMA
    VkDeviceMemory memory;
#else
    VmaAllocation allocation;
#endif

} StromboliImage;

struct StromboliImageParameters {
    u32 depth; // Can be left 0 for 2D images
    VkSampleCountFlags sampleCount; // Value of 0 also means VK_SAMPLE_COUNT_1_BIT
    VkImageTiling tiling; // Optimal by default
    u32 mipCount; // Value of 0 means a single mip eg. no mip mapping
    u32 layerCount; // Value of 0 or 1 means a single layer
    bool requireCPUAccess;
    bool cubemap;
    VkComponentMapping componentMapping;
};

typedef struct StromboliDescriptorInfo {
    union {
        VkDescriptorBufferInfo bufferInfo;
        VkDescriptorImageInfo imageInfo;
        VkAccelerationStructureKHR accelerationStructureInfo;
    };
} StromboliDescriptorInfo;

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
    bool depthClampFeature;
    bool descriptorUpdateTemplate;
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
    bool dynamicRenderingUnusedAttachments;
    bool synchronization2;
    bool maintenance4;
    bool rayQuery;
    bool accelerationStructure;
    bool rayTracingPipeline;
    bool nonUniformIndexingSampledImageArray;
    bool runtimeDescriptorArray;
    bool descriptorBindingVariableDescriptorCount;
    bool descriptorBindingSampledImageUpdateAfterBind;
    bool descriptorBindingUniformBufferUpdateAfterBind;
    bool descriptorBindingStorageBufferUpdateAfterBind;
    bool descriptorBindingStorageImageUpdateAfterBind;
    bool descriptorBindingPartiallyBound;
} StromboliInitializationParameters;

struct StromboliAttachment {
    VkImageView imageView;
    VkFormat format;
    VkSampleCountFlags sampleCount;

    VkAttachmentLoadOp loadOp;
    VkAttachmentStoreOp storeOp;
    VkImageLayout initialLayout;
    VkImageLayout usageLayout;
    VkImageLayout finalLayout;
    VkClearValue clearColor;

    // Internal use only. This may be overwritten by the implementation. It does not require this to be set to any specific value
    s32 __assignedSlot;
};

typedef struct StromboliSubpass {
    u32 inputAttachmentCount;
    u32 outputAttachmentCount;
    struct StromboliAttachment* inputAttachments;
    struct StromboliAttachment* outputAttachments;
    struct StromboliAttachment* depthAttachment;

    StromboliSwapchain* swapchainOutput;
} StromboliSubpass;

typedef struct StromboliRenderpass {
    VkRenderPass renderPass;
    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
    u32 numClearColors;
    VkClearValue* clearColors;
} StromboliRenderpass;

enum StromboliPrimitiveMode {
    STROMBOLI_PRIMITVE_MODE_TRIANGLE_LIST,
    STROMBOLI_PRIMITVE_MODE_LINE_LIST,
    STROMBOLI_PRIMITVE_MODE_COUNT,
};

enum StromboliCullMode {
    STROMBOLI_CULL_MODE_NONE = 0,
    STROMBOLI_CULL_MODE_BACK,
    STROMBOLI_CULL_MODE_FRONT,
    STROMBOLI_CULL_MODE_COUNT,
};

typedef struct StromboliSpecializationConstant {
    String8 name;
    union {
        int intOrBoolValue;
        float floatValue;
    };
} StromboliSpecializationConstant;

typedef struct StromboliGraphicsPipelineParameters {
    String8 vertexShaderFilename;
    String8 fragmentShaderFilename;

    VkRenderPass renderPass;
    u32 subpassIndex;
    enum StromboliPrimitiveMode primitiveMode;
    enum StromboliCullMode cullMode;
    VkSampleCountFlags multisampleCount;
    float multiSampleShadingFactor; // minSampleShading. Sample shading is disabled if equal to 0
    u32 additionalAttachmentCount;
    VkFormat* framebufferFormats;
    VkFormat depthFormat;

    VkPipelineVertexInputStateCreateInfo* vertexDataFormat; // If 0 the format is generated via shader reflection

    StromboliSpecializationConstant* constants;
    u32 constantsCount;

    VkDescriptorSetLayout setLayotus[4]; // Optionally overwrite descriptor set layouts

    bool wireframe;
    bool depthTest;
    bool depthWrite;
    bool reverseZ;
    bool enableBlending;
} StromboliGraphicsPipelineParameters;

struct StromboliComputePipelineParameters {
    String8 filename;

    VkDescriptorSetLayout setLayotus[4]; // Optionally overwrite descriptor set layouts
};

struct StromboliIntersectionShaderSlot {
    const char* filename;
    u32 matchingHitShaderIndex;
};

struct StromboliRaytracingPipelineParameters {
    // Multile raygen shaders are possible but each launch can only use one
    const char* raygenShaderFilename;

    const char** missShaderFilenames;
    u32 missShaderCount;

    const char** hitShaderFilenames;
    u32 hitShaderCount;

    struct StromboliIntersectionShaderSlot* intersectionShaders;
    u32 intersectionShaderCount;

    //TODO: Any hit shaders should work similarily to interseciton shaders and must specify an index for the corresponding hit shader
};

// An upload context is used for the upload of data into device local buffers.
#define STROMBOLI_UPLOAD_CONTEXT_RECORDING     0x01
#define STROMBOLI_UPLOAD_CONTEXT_SUBMIT_ACTIVE 0x02
#define STROMBOLI_UPLOAD_CONTEXT_OWNS_BUFFER   0x04
#define STROMBOLI_UPLOAD_CONTEXT_USE_QUEUE(queue) &(StromboliUploadContext){queue}
typedef struct StromboliUploadContext {
    StromboliQueue* queue;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence fence;
    StromboliBuffer* scratch;
    StromboliBuffer ownedBuffer;
    u64 scratchOffset;
    u32 flags;
} StromboliUploadContext;

StromboliResult initStromboli(StromboliContext* context, StromboliInitializationParameters* parameters);
void shutdownStromboli(StromboliContext* context);

StromboliSwapchain stromboliSwapchainCreate(StromboliContext* context, VkSurfaceKHR surface, VkImageUsageFlags usage, u32 width, u32 height, bool vsync, bool mailbox);
// Resizing of swapchain recreates images, image views etc. stored in the swapchain. Swapchain should not be in use anymore
bool stromboliSwapchainResize(StromboliContext* context, StromboliSwapchain* swapchain, VkImageUsageFlags usage, u32 width, u32 height, bool vsync, bool mailbox);
void stromboliSwapchainDestroy(StromboliContext* context, StromboliSwapchain* swapchain);

StromboliRenderpass stromboliRenderpassCreate(StromboliContext* context, u32 width, u32 height, u32 subpassCount, StromboliSubpass* subpasses);
void stromboliRenderpassDestroy(StromboliContext* context, StromboliRenderpass* renderPass);

StromboliPipeline stromboliPipelineCreateCompute(StromboliContext* context, struct StromboliComputePipelineParameters* parameters);
StromboliPipeline stromboliPipelineCreateGraphics(StromboliContext* context, struct StromboliGraphicsPipelineParameters* parameters);
StromboliMultiFormatPipeline stromboliPipelineCreateMultiFormatGraphics(StromboliContext* context, struct StromboliGraphicsPipelineParameters* parameters);
StromboliPipeline createRaytracingPipeline(StromboliContext* context, struct StromboliRaytracingPipelineParameters* parameters);
void stromboliPipelineDestroy(StromboliContext* context, StromboliPipeline* pipeline);
StromboliPipeline stromboliPipelineRetrieveForFormat(StromboliMultiFormatPipeline multiFormatPipeline, VkFormat targetFormat);

StromboliBuffer stromboliCreateBuffer(StromboliContext* context, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties);
void stromboliUploadDataToBuffer(StromboliContext* context, StromboliBuffer* buffer, const void* data, size_t size, StromboliUploadContext* uploadContext);
void stromboliDestroyBuffer(StromboliContext* context, StromboliBuffer* buffer);

StromboliImage stromboliImageCreate(StromboliContext* context, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, struct StromboliImageParameters* parameters);
void stromboliUploadDataToImage(StromboliContext* context, StromboliImage* image, void* data, u64 size, VkImageLayout finalLayout, VkAccessFlags dstAccessMask, StromboliUploadContext* uploadContext);
void stromboliUploadDataToImageSubregion(StromboliContext* context, StromboliImage* image, void* data, u64 size, u32 offsetX, u32 offsetY, u32 offsetZ, u32 width, u32 height, u32 depth, u32 inputStrideInPixels, u32 mipLevel, u32 layer, VkImageLayout finalLayout, VkAccessFlags dstAccessMask, StromboliUploadContext* uploadContext);
void stromboliImageDestroy(StromboliContext* context, StromboliImage* image);

#define STROMBOLI_NAME_OBJECT_EXPLICIT(context, object, type, name) stromboliNameObject(context, INT_FROM_PTR(object), type, name)
#define STROMBOLI_NAME_OBJECT(context, object, type) STROMBOLI_NAME_OBJECT_EXPLICIT(context, object, type, #object)
static inline void stromboliNameObject(StromboliContext* context, u64 handle, VkObjectType type, const char* name);

static inline StromboliDescriptorInfo stromboliCreateBufferDescriptor(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
static inline StromboliDescriptorInfo stromboliCreateImageDescriptor(VkImageLayout imageLayout, VkImageView imageView, VkSampler sampler);

static inline void stromboliPipelineBarrier(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, u32 bufferBarrierCount, const VkBufferMemoryBarrier2KHR* bufferBarriers, u32 imageBarrierCount, const VkImageMemoryBarrier2KHR* imageBarriers);

StromboliUploadContext createUploadContext(StromboliContext* context, StromboliQueue* queue, StromboliBuffer* scratch);
void destroyUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext);
StromboliUploadContext ensureValidUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext);
void beginRecordUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext);
VkCommandBuffer ensureUploadContextIsRecording(StromboliContext* context, StromboliUploadContext* uploadContext);
u64 uploadToScratch(StromboliContext* context, StromboliUploadContext* uploadContext, void* data, u64 size);
void submitUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext, u32 signalSemaphoreCount, VkSemaphore* signalSemaphores);
void flushUploadContext(StromboliContext* context, StromboliUploadContext* uploadContext);
bool isDepthFormat(VkFormat format);
u32 stromboliFindMemoryType(StromboliContext* context, u32 typeFilter, VkMemoryPropertyFlags memoryProperties);
VkSampler stromboliSamplerCreate(StromboliContext* context, bool linear);

VkDeviceAddress getBufferDeviceAddress(StromboliContext* context, StromboliBuffer* buffer);
StromboliAccelerationStructure createAccelerationStructure(StromboliContext* context, u32 count, VkAccelerationStructureGeometryKHR* geometries, VkAccelerationStructureBuildRangeInfoKHR* buildRanges, bool allowUpdate, bool compact, StromboliUploadContext* uploadContext);
void updateAccelerationStructure(StromboliContext* context, StromboliAccelerationStructure* accelerationStructure, u32 count, VkAccelerationStructureGeometryKHR* geometries, VkAccelerationStructureBuildRangeInfoKHR* buildRanges, StromboliUploadContext* uploadContext);
void destroyAccelerationStructure(StromboliContext* context, StromboliAccelerationStructure* accelerationStructure);

#ifndef __cplusplus
#include "stromboli_helpers.inl"
#endif

#endif // STROMBOLI_H
