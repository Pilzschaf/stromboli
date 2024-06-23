/*
 * Unfortunately when beginning a renderpass we need to know in which images we want to render.
 * This places restrictions on the way the render graph API can be structured.
 * Possibility 1:
 *  We just use a callback based API. This requires us to use callbacks for all rendering commands
 *  And either need to use non-typesafe userpointers or global data to access our variables. And the controlflow is bad
 * Possibility 2:
 *  We make it completely immediate mode and abstract command buffers and all vkCmds. Then we can first store them
 *  Without concreate image views and record the commands right before submission into the actual command buffers
 *  This variant requires a lot of abstraction and work. But would be easy to expand to other graphics APIs.
 * Possibilty 3:
 *  We use a not completely immediate mode API. We have a step which creates the graph itself. This could be done
 *  per frame or at another more coarse granularity. We create passes with unique ids or names or similar
 *  When rendering we say start out pass and can receive all relevant info and framebuffers etc. at this point and can directly write into the command buffer
 * Currently I strongly prefer Variant 3 at least as a first step. Variant 2 might be really interesting when expanding onto other APIs
 */

#ifndef RENDER_GRAPH_H
#define RENDER_GRAPH_H

#include <stromboli/stromboli.h>
#include <grounded/memory/grounded_arena.h>

typedef struct RenderGraph RenderGraph;
typedef struct RenderGraphBuilder RenderGraphBuilder;
typedef struct RenderGraphPass RenderGraphPass;

//TODO: Also add a small id to the upper bits to check that the pass index is valid for the current frame!
typedef struct  RenderGraphPassHandle {
    u32 handle;
} RenderGraphPassHandle;
typedef struct RenderGraphImageHandle {
    u32 handle;
} RenderGraphImageHandle;

// Build
RenderGraphBuilder* createRenderGraphBuilder(StromboliContext* context, MemoryArena* arena);
RenderGraphPassHandle renderGraphAddGraphicsPass(RenderGraphBuilder* builder, String8 name);
RenderGraphPassHandle renderGraphAddTransferPass(RenderGraphBuilder* builder, String8 name);
RenderGraphPassHandle renderGraphAddComputePass(RenderGraphBuilder* builder, String8 name);
RenderGraphPassHandle renderGraphAddRaytracePass(RenderGraphBuilder* builder, String8 name);
RenderGraphImageHandle renderGraphCreateClearedFramebuffer(RenderGraphBuilder* builder, u32 width, u32 height, VkFormat format, VkClearValue clearColor);
RenderGraphImageHandle renderPassAddClearedOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, u32 width, u32 height, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage, VkFormat format, VkClearValue clearColor);
RenderGraphImageHandle renderPassAddOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, u32 width, u32 height, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage, VkFormat format);
RenderGraphImageHandle renderPassAddInput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, RenderGraphImageHandle input, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage);
RenderGraphImageHandle renderPassAddInputOutput(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, RenderGraphImageHandle input, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage, VkImageUsageFlags usage);
void renderPassSetExternal(RenderGraphBuilder* builder, RenderGraphPassHandle passHandle, bool external); // Marks the render pass as producing external resources. This makes sure the pass is not pruned when compiling
VkFormat renderGraphImageGetFormat(RenderGraphBuilder* builder, RenderGraphImageHandle image);
u32 renderGraphImageGetWidth(RenderGraphBuilder* builder, RenderGraphImageHandle image);
u32 renderGraphImageGetHeight(RenderGraphBuilder* builder, RenderGraphImageHandle image);

// Compile
RenderGraph* renderGraphCompile(RenderGraphBuilder* builder, RenderGraphImageHandle swapchainOutput, RenderGraph* oldGraph);
void renderGraphDestroy(RenderGraph* graph, VkFence fence);

// Execute
RenderGraphPass* beginRenderPass(RenderGraph* graph, RenderGraphPassHandle pass);
u32 renderGraphGetFrameIndex(RenderGraph* graph);
bool renderPassIsActive(RenderGraphPass* pass);
VkCommandBuffer renderPassGetCommandBuffer(RenderGraphPass* pass);
StromboliImage* renderPassGetInputResource(RenderGraphPass* pass, RenderGraphImageHandle image);
StromboliImage* renderPassGetOutputResource(RenderGraphPass* pass, RenderGraphImageHandle image);
bool renderGraphExecute(RenderGraph* graph, StromboliSwapchain* swapchain, VkFence fence); // Returns false if swapchain must be resized

void renderGraphBuilderPrint(RenderGraphBuilder* builder);
void renderGraphPrint(RenderGraph* graph);

#endif // RENDER_GRAPH_H
