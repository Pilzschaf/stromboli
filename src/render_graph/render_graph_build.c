#include <stromboli/stromboli_render_graph.h>

#include "render_graph_definitions.inl"

//#include <grounded/memory/grounded_memory.h>

static u32 builderFingerprint;

RenderGraphBuilder* createRenderGraphBuilder(StromboliContext* context, MemoryArena* arena) {
    ArenaMarker resetMarker = arenaCreateMarker(arena);
    RenderGraphBuilder* result = ARENA_PUSH_STRUCT(arena, RenderGraphBuilder);
    if(result) {
        result->arena = arena;
        result->context = context;
        result->resetMarker = resetMarker;
        result->currentPassIndex = 1;
        result->fingerprint = builderFingerprint;
    }
    builderFingerprint = (builderFingerprint+1) & ((1<<FINGERPRINT_BITS) - 1);

    return result;
}

RenderGraphPassHandle renderGraphAddGraphicsPass(RenderGraphBuilder* builder, String8 name) {
    RenderGraphPassHandle result = {0};
    struct RenderGraphBuildPass* pass = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildPass);

    if(pass) {
        pass->next = builder->passSentinel.next;
        builder->passSentinel.next = pass;
        pass->name = name;
        pass->type = RENDER_GRAPH_PASS_TYPE_GRAPHICS;

        result.handle = builder->currentPassIndex++;
        ASSERT(result.handle < INVERSE_FINGERPRINT_MASK);
        result.handle |= builder->fingerprint << FINGERPRINT_SHIFT;
        ASSERT(isFingerprintValid(builder, result));
        ASSERT(getHandleData(result) == builder->currentPassIndex-1);
    }
    
    return result;
}

RenderGraphPassHandle renderGraphAddTransferPass(RenderGraphBuilder* builder, String8 name) {
    RenderGraphPassHandle result = {0};
    struct RenderGraphBuildPass* pass = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildPass);

    if(pass) {
        pass->next = builder->passSentinel.next;
        builder->passSentinel.next = pass;
        pass->name = name;
        pass->type = RENDER_GRAPH_PASS_TYPE_TRANSFER;

        result.handle = builder->currentPassIndex++;
        ASSERT(result.handle < INVERSE_FINGERPRINT_MASK);
        result.handle |= builder->fingerprint << FINGERPRINT_SHIFT;
        ASSERT(isFingerprintValid(builder, result));
        ASSERT(getHandleData(result) == builder->currentPassIndex-1);
    }
    
    return result;
}

RenderGraphPassHandle renderGraphAddComputePass(RenderGraphBuilder* builder, String8 name) {
    RenderGraphPassHandle result = {0};
    struct RenderGraphBuildPass* pass = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildPass);

    if(pass) {
        pass->next = builder->passSentinel.next;
        builder->passSentinel.next = pass;
        pass->name = name;
        pass->type = RENDER_GRAPH_PASS_TYPE_COMPUTE;

        result.handle = builder->currentPassIndex++;
        ASSERT(result.handle < INVERSE_FINGERPRINT_MASK);
        result.handle |= builder->fingerprint << FINGERPRINT_SHIFT;
        ASSERT(isFingerprintValid(builder, result));
        ASSERT(getHandleData(result) == builder->currentPassIndex-1);
    }
    
    return result;
}

RenderGraphPassHandle renderGraphAddRaytracePass(RenderGraphBuilder* builder, String8 name) {
    RenderGraphPassHandle result = {0};
    struct RenderGraphBuildPass* pass = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildPass);

    if(pass) {
        pass->next = builder->passSentinel.next;
        builder->passSentinel.next = pass;
        pass->name = name;
        pass->type = RENDER_GRAPH_PASS_TYPE_RAYTRACE;

        result.handle = builder->currentPassIndex++;
        ASSERT(result.handle < INVERSE_FINGERPRINT_MASK);
        result.handle |= builder->fingerprint << FINGERPRINT_SHIFT;
        ASSERT(isFingerprintValid(builder, result));
        ASSERT(getHandleData(result) == builder->currentPassIndex-1);
    }
    
    return result;
}

static inline struct RenderGraphBuildPass* getPassFromHandle(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle) {
    ASSERT(isFingerprintValid(builder, passHandle));
    u32 passHandleValue = getHandleData(passHandle);
    u32 stepCount = builder->currentPassIndex - passHandleValue - 1;
    struct RenderGraphBuildPass* pass = builder->passSentinel.next;
    for(u32 i = 0; i < stepCount; ++i) {
        pass = pass->next;
    }
    return pass;
}

RenderGraphImageHandle renderGraphCreateClearedFramebuffer(RenderGraphBuilder* builder, u32 width, u32 height, VkFormat format, VkClearValue clearColor) {
    struct RenderGraphBuildImage* result = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildImage);
    if(result) {
        result->next = builder->imageSentinel.next;
        builder->imageSentinel.next = result;
        result->image.width = width;
        result->image.height = height;
        result->format = format;
        result->requiresClear = true;
        result->clearColor = clearColor;
    }

    return (RenderGraphImageHandle){builder->currentResourceIndex++};
}

RenderGraphImageHandle renderPassAddOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, u32 width, u32 height, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage, VkFormat format) {
    struct RenderGraphBuildImage* result = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildImage);
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);

    if(result) {
        result->producer = pass;
        result->next = builder->imageSentinel.next;
        result->image.width = width;
        result->image.height = height;
        result->usage |= usage;
        result->format = format;
        builder->imageSentinel.next = result;
        pass->outputs[pass->outputCount].layout = layout;
        pass->outputs[pass->outputCount].access = access;
        pass->outputs[pass->outputCount].stage = stage;
        pass->outputs[pass->outputCount].usage = usage;
        pass->outputs[pass->outputCount++].imageHandle.handle = builder->currentResourceIndex;
    }

    return (RenderGraphImageHandle){builder->currentResourceIndex++};
}

RenderGraphImageHandle renderPassAddClearedOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, u32 width, u32 height, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage, VkFormat format, VkClearValue clearColor) {
    struct RenderGraphBuildImage* result = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildImage);
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);

    if(result) {
        result->producer = pass;
        result->next = builder->imageSentinel.next;
        result->image.width = width;
        result->image.height = height;
        result->usage |= usage;
        result->format = format;
        result->clearColor = clearColor;
        result->requiresClear = false;
        builder->imageSentinel.next = result;
        pass->outputs[pass->outputCount].layout = layout;
        pass->outputs[pass->outputCount].access = access;
        pass->outputs[pass->outputCount].stage = stage;
        pass->outputs[pass->outputCount].usage = usage;
        pass->outputs[pass->outputCount].requiresClear = true;
        pass->outputs[pass->outputCount++].imageHandle.handle = builder->currentResourceIndex;
    }

    return (RenderGraphImageHandle){builder->currentResourceIndex++};
}

RenderGraphImageHandle renderPassAddInput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, RenderGraphImageHandle inputHandle, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage) {
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);
    pass->inputs[pass->inputCount].layout = layout;
    pass->inputs[pass->inputCount].access = access;
    pass->inputs[pass->inputCount].stage = stage;
    pass->inputs[pass->inputCount].usage = usage;
    struct RenderGraphBuildImage* input = getImageFromHandle(builder, inputHandle);

    //(outdated) As we have possibly changed layout etc. we mark ourselves as producer as otherwise the barriers could be in the wrong order and therefore do wrong layout transitions
    pass->inputs[pass->inputCount].producer = input->producer;
    pass->inputs[pass->inputCount].lastReader = pass;
    
    pass->inputs[pass->inputCount++].imageHandle = inputHandle;

    // We simply return the input handle
    return inputHandle;
}

RenderGraphImageHandle renderPassAddInputOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, RenderGraphImageHandle inputHandle, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage) {
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);
    struct RenderGraphBuildImage* input = getImageFromHandle(builder, inputHandle);
    if(!input->producer) {
        if(input->requiresClear) {
            if(pass->type != RENDER_GRAPH_PASS_TYPE_GRAPHICS) {
                usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            return renderPassAddClearedOutput(builder, passHandle, input->image.width, input->image.height, layout, access, stage, usage, input->format, input->clearColor);
        } else {
            return renderPassAddOutput(builder, passHandle, input->image.width, input->image.height, layout, access, stage, usage, input->format);
        }
        
    }

    input->usage |= usage;
    
    pass->inputs[pass->inputCount].layout = layout;
    pass->inputs[pass->inputCount].access = access;
    pass->inputs[pass->inputCount].stage = stage;
    pass->inputs[pass->inputCount].usage = usage;
    pass->inputs[pass->inputCount].producer = input->producer;
    //pass->inputs[pass->inputCount].lastReader = pass; // while not hurting this should not be necessary as we are producing an output
    pass->inputs[pass->inputCount++].imageHandle = inputHandle;

    input->producer = pass;
    pass->outputs[pass->outputCount].layout = layout;
    pass->outputs[pass->outputCount].access = access;
    pass->outputs[pass->outputCount].stage = stage;
    pass->outputs[pass->outputCount].usage = usage;
    pass->outputs[pass->outputCount++].imageHandle = inputHandle;

    return inputHandle;
}

void renderPassSetExternal(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, bool external) {
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);
    if(pass) {
        pass->external = external;
    }
}

VkFormat renderGraphImageGetFormat(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    VkFormat result = VK_FORMAT_UNDEFINED;
    struct RenderGraphBuildImage* image = getImageFromHandle(builder, imageHandle);
    if(image) {
        result = image->format;   
    }
    return result;
}

u32 renderGraphImageGetWidth(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    u32 result = 0;
    struct RenderGraphBuildImage* image = getImageFromHandle(builder, imageHandle);
    if(image) {
        result = image->image.width;
    }
    return result;
}

u32 renderGraphImageGetHeight(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    u32 result = 0;
    struct RenderGraphBuildImage* image = getImageFromHandle(builder, imageHandle);
    if(image) {
        result = image->image.height;
    }
    return result;
}
