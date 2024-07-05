#ifndef RENDER_GRAPH_DEFINITIONS
#define RENDER_GRAPH_DEFINITIONS

#include <stromboli/stromboli_render_graph.h>

#ifndef FINGERPRINT_BITS
#define FINGERPRINT_BITS 3
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
    bool requiresClear; //TODO: Only used for renderGraphCreateClearedFramebuffer
} RenderGraphImage;

struct RenderAttachment {
    VkImageLayout layout; // Layout must be stored per attachment and not per image
    VkAccessFlags access;
    VkPipelineStageFlags stage;
    VkImageUsageFlags usage;
    RenderGraphImageHandle imageHandle;
    //TODO: Should probably store producer here for combined input+output framebuffer support (as producers are changing for a single image)
    struct RenderGraphBuildPass* producer; 
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
    VkSemaphore imageReleaseSemaphore;
    RenderGraphImageHandle finalImageHandle;
    VkImageMemoryBarrier2KHR finalImageBarrier;
    u32 swapchainOutputPassIndex; // required to know where to put layout transitions for swapchain images

    RenderGraphPass* sortedPasses;
    u32 passCount;
    u32 commandBufferCountPerFrame;
    u16* buildPassToSortedPass;

    StromboliImage* images;
    VkClearValue* clearValues;
    //bool* requiresClear;
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

static inline struct RenderGraphBuildImage* getImageFromHandle(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    u32 stepCount = builder->currentResourceIndex - imageHandle.handle - 1;
    struct RenderGraphBuildImage* image = builder->imageSentinel.next;
    for(u32 i = 0; i < stepCount; ++i) {
        image = image->next;
    }
    return image;
}

static inline u32 getFingerprint(RenderGraphPassHandle passHandle) {
    u32 result = ((passHandle.handle & (~INVERSE_FINGERPRINT_MASK)) >> FINGERPRINT_SHIFT);
    return result;
}

static inline u32 getHandleData(RenderGraphPassHandle passHandle) {
    u32 result = passHandle.handle & INVERSE_FINGERPRINT_MASK;
    return result;
}

static inline bool isFingerprintValid(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle) {
    bool result = builder->fingerprint == getFingerprint(passHandle);
    return result;
}

#endif // RENDER_GRAPH_DEFINITIONS
