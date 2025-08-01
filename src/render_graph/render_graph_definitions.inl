#ifndef RENDER_GRAPH_DEFINITIONS
#define RENDER_GRAPH_DEFINITIONS

#include <stromboli/stromboli_render_graph.h>

#ifndef FINGERPRINT_BITS
#define FINGERPRINT_BITS 8
#endif
STATIC_ASSERT(FINGERPRINT_BITS > 0);

#ifndef TIMING_SECTION_COUNT
#define TIMING_SECTION_COUNT 256
#endif

// Leaves all bits where the fingerprint is not present
#define INVERSE_FINGERPRINT_MASK (0xFFFFFFFF >> FINGERPRINT_BITS)
STATIC_ASSERT(IS_MASK(INVERSE_FINGERPRINT_MASK));
#define FINGERPRINT_SHIFT (32 - FINGERPRINT_BITS)

enum RenderGraphPassType {
    RENDER_GRAPH_PASS_TYPE_GRAPHICS = 0,
    RENDER_GRAPH_PASS_TYPE_COMPUTE,
    RENDER_GRAPH_PASS_TYPE_TRANSFER,
    RENDER_GRAPH_PASS_TYPE_RAYTRACE,
    RENDER_GRAPH_PASS_TYPE_COUNT,
};

typedef struct RenderGraphBuildImage {
    struct RenderGraphBuildImage* next;
    StromboliImage image;
    struct RenderGraphBuildPass* producer;
    VkImageUsageFlags usage;
    VkFormat format;
    VkClearValue clearColor;
    bool isSwpachainOutput;
    bool requiresClear; // Only used for renderGraphCreateClearedFramebuffer as we cannot use attachment clear directly there
} RenderGraphImage;

struct RenderAttachment {
    VkImageLayout layout; // Layout must be stored per attachment and not per image
    VkAccessFlags access;
    VkPipelineStageFlags2 stage;
    VkImageUsageFlags usage;
    RenderGraphImageHandle imageHandle;
    struct RenderGraphBuildPass* producer; // We store producer here for combined input+output framebuffer support (as producers are changing for a single image)
    struct RenderGraphBuildPass* lastReader; // Can be null
    bool requiresClear;
    bool resolveTarget; // This attachment is used as a resolve target and otherwise unused in the pass
    RenderGraphImageHandle resolve; // The optional handle to an output where this attachment should be resolved to
    VkResolveModeFlags resolveMode;
};

struct RenderGraphBuildPass {
    String8 name;
    struct RenderGraphBuildPass* next;
    enum RenderGraphPassType type;

    struct RenderAttachment inputs[8];
    struct RenderAttachment outputs[8];
    u32 inputCount;
    u32 outputCount;
    bool external; // This indicates that this pass produces external output and must not be evicted when compiling
};

struct RenderGraphMemoryBlock {
    struct RenderGraphMemoryBlock* next;
    VkDeviceMemory memory;
    u64 size;
    u64 offset;
    u32 type;
};

struct RenderGraphPass {
    String8 name;
    RenderGraph* graph;
    VkCommandBuffer commandBuffer;
    enum RenderGraphPassType type;
    //u32 passIndex;

    struct RenderAttachment inputs[8];
    struct RenderAttachment outputs[8];
    u32 inputCount;
    u32 outputCount;

    u32 imageBarrierCount;
    VkImageMemoryBarrier2KHR* imageBarriers;

    u32 afterClearBarrierCount;
    VkImageMemoryBarrier2KHR* afterClearBarriers;
};

struct RenderGraph {
    MemoryArena arena;
    ArenaMarker resetMarker;

    StromboliContext* context;
    VkSemaphore imageAcquireSemaphore;
    VkSemaphore imageReleaseSemaphores[MAX_SWAPCHAIN_IMAGES];
    RenderGraphImageHandle finalImageHandle;
    VkImageMemoryBarrier2KHR finalImageBarrier;
    u32 swapchainOutputPassIndex; // required to know where to put layout transitions for swapchain images

    RenderGraphPass* sortedPasses;
    u32 passCount;
    u32 commandBufferCountPerFrame;
    u16* buildPassToSortedPass;

    StromboliImage* images;
    VkClearValue* clearValues;
    u32 imageCount;
    u32 fingerprint;

    StromboliImage* imageDeleteQueue; // Images to delete once we have waited for the respective fence
    u32 imageDeleteCount;
    float lastDuration; // The total duration of the last execution in seconds
    u32 timestampCount;

    VkQueryPool queryPools[2];
    VkCommandPool commandPools[2];
    VkCommandBuffer* commandBuffers; // Each pass uses passIndex+commandBufferOffset as its pass
    u32 commandBufferOffset; // Switches between 0 and commandBufferCountPerFrame

    struct RenderGraphMemoryBlock* firstBlock;
};

struct RenderGraphBuilder {
    MemoryArena* arena;
    ArenaMarker resetMarker;
    struct RenderGraphBuildPass passSentinel;
    struct RenderGraphBuildImage imageSentinel;
    StromboliContext* context;
    u32 currentResourceIndex;
    u32 currentPassIndex;
    u32 fingerprint;
};

static inline u32 getPassFingerprint(RenderGraphPassHandle passHandle) {
    u32 result = ((passHandle.handle & (~INVERSE_FINGERPRINT_MASK)) >> FINGERPRINT_SHIFT);
    return result;
}

static inline u32 getImageFingerprint(RenderGraphImageHandle imageHandle) {
    u32 result = ((imageHandle.handle & (~INVERSE_FINGERPRINT_MASK)) >> FINGERPRINT_SHIFT);
    return result;
}

static inline u32 getPassHandleData(RenderGraphPassHandle passHandle) {
    u32 result = passHandle.handle & INVERSE_FINGERPRINT_MASK;
    return result;
}

static inline u32 getImageHandleData(RenderGraphImageHandle imageHandle) {
    u32 result = imageHandle.handle & INVERSE_FINGERPRINT_MASK;
    return result;
}

static inline bool isPassFingerprintValid(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle) {
    bool result = builder->fingerprint == getPassFingerprint(passHandle);
    return result;
}

static inline bool isImageFingerprintValid(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    bool result = builder->fingerprint == getImageFingerprint(imageHandle);
    return result;
}

static inline struct RenderGraphBuildImage* getImageFromHandle(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    ASSERT(isImageFingerprintValid(builder, imageHandle));
    u32 stepCount = builder->currentResourceIndex - getImageHandleData(imageHandle) - 1;
    struct RenderGraphBuildImage* image = builder->imageSentinel.next;
    for(u32 i = 0; i < stepCount; ++i) {
        image = image->next;
    }
    return image;
}

#endif // RENDER_GRAPH_DEFINITIONS
