#include <stromboli/stromboli_render_graph.h>

#include "render_graph_definitions.inl"

static u32 builderFingerprint;

// Arena is used for building the graph. The compile step then uses its own arena stored as part of the render graph
// You are responsible to reset the frameArena memory sometime after RenderGraph has been compiled
RenderGraphBuilder* createRenderGraphBuilder(StromboliContext* context, MemoryArena* frameArena) {
    RenderGraphBuilder* result = ARENA_PUSH_STRUCT(frameArena, RenderGraphBuilder);
    if(result) {
        result->arena = frameArena;
        result->context = context;
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
        ASSERT(isPassFingerprintValid(builder, result));
        ASSERT(getPassHandleData(result) == builder->currentPassIndex-1);
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
        ASSERT(isPassFingerprintValid(builder, result));
        ASSERT(getPassHandleData(result) == builder->currentPassIndex-1);
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
        ASSERT(isPassFingerprintValid(builder, result));
        ASSERT(getPassHandleData(result) == builder->currentPassIndex-1);
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
        ASSERT(isPassFingerprintValid(builder, result));
        ASSERT(getPassHandleData(result) == builder->currentPassIndex-1);
    }
    
    return result;
}

static inline struct RenderGraphBuildPass* getPassFromHandle(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle) {
    ASSERT(isPassFingerprintValid(builder, passHandle));
    u32 passHandleValue = getPassHandleData(passHandle);
    u32 stepCount = builder->currentPassIndex - passHandleValue - 1;
    struct RenderGraphBuildPass* pass = builder->passSentinel.next;
    for(u32 i = 0; i < stepCount; ++i) {
        pass = pass->next;
    }
    return pass;
}

RenderGraphImageHandle renderGraphCreateClearedFramebuffer(RenderGraphBuilder* builder, u32 width, u32 height, VkFormat format, VkSampleCountFlags sampleCount, VkClearValue clearColor) {
    struct RenderGraphBuildImage* result = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildImage);
    if(result) {
        result->next = builder->imageSentinel.next;
        builder->imageSentinel.next = result;
        result->image.width = width;
        result->image.height = height;
        result->format = format;
        result->image.samples = sampleCount;
        result->requiresClear = true;
        result->clearColor = clearColor;
    }

    RenderGraphImageHandle resultHandle = {0};
    resultHandle.handle = builder->currentResourceIndex++;
    ASSERT(resultHandle.handle < INVERSE_FINGERPRINT_MASK);
    resultHandle.handle |= builder->fingerprint << FINGERPRINT_SHIFT;
    ASSERT(isImageFingerprintValid(builder, resultHandle));
    ASSERT(getImageHandleData(resultHandle) == builder->currentResourceIndex-1);
    return resultHandle;
}

RenderGraphImageHandle renderPassAddOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, u32 width, u32 height, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags2 stage, VkImageUsageFlags usage, VkFormat format, struct RenderPassOutputParameters* parameters) {
    struct RenderGraphBuildImage* result = ARENA_PUSH_STRUCT(builder->arena, struct RenderGraphBuildImage);
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);
    if(!parameters) {
        static struct RenderPassOutputParameters defaultParameters = {0};
        parameters = &defaultParameters;
    }

    RenderGraphImageHandle outputHandle = {0};
    outputHandle.handle = builder->currentResourceIndex++;
    ASSERT(outputHandle.handle < INVERSE_FINGERPRINT_MASK);
    outputHandle.handle |= builder->fingerprint << FINGERPRINT_SHIFT;
    ASSERT(isImageFingerprintValid(builder, outputHandle));
    ASSERT(getImageHandleData(outputHandle) == builder->currentResourceIndex-1);

    if(result) {
        result->producer = pass;
        result->next = builder->imageSentinel.next;
        result->image.width = width;
        result->image.height = height;
        result->usage |= usage;
        result->format = format;
        result->image.samples = parameters->sampleCount ? parameters->sampleCount : VK_SAMPLE_COUNT_1_BIT;
        result->clearColor = parameters->clearValue;
        result->requiresClear = false; // This is set elsewhere
        builder->imageSentinel.next = result;
        pass->outputs[pass->outputCount].layout = layout;
        pass->outputs[pass->outputCount].access = access;
        pass->outputs[pass->outputCount].stage = stage;
        pass->outputs[pass->outputCount].usage = usage;
        pass->outputs[pass->outputCount].requiresClear = parameters->clear;
        pass->outputs[pass->outputCount++].imageHandle = outputHandle;   
    }

    if(parameters->resolveMode && parameters->sampleCount > 0 && parameters->sampleCount != VK_SAMPLE_COUNT_1_BIT) {
        // Not sure about usage and layout
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if(isDepthFormat(format)) {
            usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        outputHandle = renderPassAddOutput(builder, passHandle, width, height, layout, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, usage, format, 0);
        pass->outputs[pass->outputCount - 2].resolve = outputHandle;
        pass->outputs[pass->outputCount - 2].resolveMode = parameters->resolveMode;
        pass->outputs[pass->outputCount - 1].resolveTarget = true;
    }

    return outputHandle;
}

RenderGraphImageHandle renderPassAddInput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, RenderGraphImageHandle inputHandle, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags2 stage, VkImageUsageFlags usage) {
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);
    pass->inputs[pass->inputCount].layout = layout;
    pass->inputs[pass->inputCount].access = access;
    pass->inputs[pass->inputCount].stage = stage;
    pass->inputs[pass->inputCount].usage = usage;
    struct RenderGraphBuildImage* input = getImageFromHandle(builder, inputHandle);

    //TODO: Why outdated???
    //(outdated) As we have possibly changed layout etc. we mark ourselves as producer as otherwise the barriers could be in the wrong order and therefore do wrong layout transitions
    pass->inputs[pass->inputCount].producer = input->producer;
    pass->inputs[pass->inputCount].lastReader = pass;
    
    pass->inputs[pass->inputCount++].imageHandle = inputHandle;

    // We simply return the input handle
    return inputHandle;
}

RenderGraphImageHandle renderPassAddInputOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, RenderGraphImageHandle inputHandle, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags2 stage, VkImageUsageFlags usage, VkResolveModeFlags resolve) {
    struct RenderGraphBuildPass* pass = getPassFromHandle(builder, passHandle);
    struct RenderGraphBuildImage* input = getImageFromHandle(builder, inputHandle);
    if(!input->producer) {
        if(input->requiresClear) {
            if(pass->type != RENDER_GRAPH_PASS_TYPE_GRAPHICS) {
                usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
        }
        return renderPassAddOutput(builder, passHandle, input->image.width, input->image.height, layout, access, stage, usage, input->format, &(struct RenderPassOutputParameters){
            .sampleCount = input->image.samples,
            .clear = input->requiresClear,
            .clearValue = input->clearColor,
            .resolveMode = resolve,
        });
    }

    //TODO: @Temporary Not supported yet
    ASSERT(!resolve);

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

VkSampleCountFlags renderGraphImageGetSampleCount(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    VkSampleCountFlags result = VK_SAMPLE_COUNT_1_BIT;
    struct RenderGraphBuildImage* image = getImageFromHandle(builder, imageHandle);
    if(image) {
        result = image->image.samples;
    }
    return result;
}

RenderGraphImageHandle renderGraphImageResolve(RenderGraphBuilder* builder, RenderGraphImageHandle imageHandle) {
    struct RenderGraphBuildImage* image = getImageFromHandle(builder, imageHandle);
    if(image && image->image.samples && image->image.samples != VK_SAMPLE_COUNT_1_BIT) {
        // We actually require a resovle
        RenderGraphPassHandle resolvePass = renderGraphAddGraphicsPass(builder, STR8_LITERAL("__internal_resolve_pass"));
        //TODO: General layout is also ok. Maybe prefer this if image is already in general layout
        renderPassAddInput(builder, resolvePass, imageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        imageHandle = renderPassAddOutput(builder, resolvePass, image->image.width, image->image.height, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT, image->format, 0);
    }
    return imageHandle;
}
