#include <stromboli/stromboli_render_graph.h>

#include "render_graph_definitions.inl"

#include <grounded/memory/grounded_memory.h>
#include <stdio.h>

static inline struct RenderGraphBuildPass* getPassAtIndex(struct RenderGraphBuildPass* firstPass, u32 index) {
    struct RenderGraphBuildPass* result = firstPass;
    for(u32 i = 0; i < index; ++i) {
        result = result->next;
    }
    return result;
}

static inline u32 getIndexOfPass(struct RenderGraphBuildPass* firstPass, struct RenderGraphBuildPass* pass) {
    u32 result = 0;
    while(firstPass && firstPass != pass) {
        result++;
        firstPass = firstPass->next;
    }
    return result;
}

static void sortPasses(RenderGraphBuilder* builder, u32 passCount, RenderGraph* result) {
    struct RenderGraphBuildPass* firstPass = builder->passSentinel.next;

    // Create graph
    for(u32 i = 0; i < passCount; ++i) {
        struct RenderGraphBuildPass* pass = getPassAtIndex(firstPass, i);
        for(u32 j = 0; j < pass->inputCount; ++j) {
            struct RenderGraphBuildImage* input = getImageFromHandle(builder, pass->inputs[j].imageHandle);
            struct RenderGraphBuildPass* producer = pass->inputs[j].producer;
            ASSERT(producer);
            input->usage |= pass->inputs[j].usage;

            // Create an edge between this pass and the pass that produces the input
            // Edges are basically inputs->producer

            // TODO: We could remove nodes that have no edges
        }
    }

    // Topological sort
    result->buildPassToSortedPass = ARENA_PUSH_ARRAY(&result->arena, passCount, u16);
    for(u32 i = 0; i < passCount; ++i) {
        result->buildPassToSortedPass[i] = UINT16_MAX;
    }
    RenderGraphPass* sortedPasses = ARENA_PUSH_ARRAY(&result->arena, passCount, RenderGraphPass);
    u32 sortedCount = 0;

    // We use a non-recusive alogorithm so we need a stack
    u32* stack = ARENA_PUSH_ARRAY(builder->arena, passCount, u32);
    u8* visited = ARENA_PUSH_ARRAY(builder->arena, passCount, u8);
    u32 stackSize = 0;

    // Do a DFS
    //u32 i = result->swapchainOutputPassIndex;
    for(u32 i = 0; i < passCount; ++i) {
        if(!getPassAtIndex(firstPass, i)->external || visited[i]) {
            // No entrypoint as not external or already visited
            continue;
        }
        // Push
        stack[stackSize++] = i;
        visited[i] = 1;
        while(stackSize > 0) {
            ASSERT(stackSize <= passCount);
            u32 passIndex = stack[stackSize-1];
            if(visited[passIndex] == 3) {
                // Pop
                stackSize--;
                continue;
            }
            if(visited[passIndex] == 2) {
                visited[passIndex] = 3;
                struct RenderGraphBuildPass* buildPass = getPassAtIndex(firstPass, passIndex);
                sortedPasses[sortedCount].name = str8Copy(&result->arena, buildPass->name);
                sortedPasses[sortedCount].type = buildPass->type;
                sortedPasses[sortedCount].inputCount = buildPass->inputCount;
                sortedPasses[sortedCount].outputCount = buildPass->outputCount;
                MEMORY_COPY_ARRAY(sortedPasses[sortedCount].inputs, buildPass->inputs);
                MEMORY_COPY_ARRAY(sortedPasses[sortedCount].outputs, buildPass->outputs);
                result->buildPassToSortedPass[passCount - passIndex - 1] = sortedCount++;
                // Pop
                stackSize--;
                continue;
            }
            // First visit
            visited[passIndex] = 2;
            struct RenderGraphBuildPass* pass = getPassAtIndex(firstPass, passIndex);
            if(pass->inputCount == 0) {
                // Leaf node
                continue;
            }
            // Iterate through all edges and add them to the stack
            // Edges are basically inputs->producer
            for(u32 j = 0; j < pass->inputCount; ++j) {
                struct RenderGraphBuildImage* input = getImageFromHandle(builder, pass->inputs[j].imageHandle);
                struct RenderGraphBuildPass* producer = pass->inputs[j].producer;
                ASSERT(producer);
                struct RenderGraphBuildPass* child = producer;
                u32 childIndex = getIndexOfPass(firstPass, child);
                ASSERT(childIndex < passCount);
                if(!visited[childIndex]) {
                    // Push
                    stack[stackSize++] = childIndex;
                    visited[childIndex] = 1;
                }
                /*struct RenderGraphBuildPass* lastReader = pass->inputs[j].lastReader;
                if(lastReader) {
                    u32 childIndex = getIndexOfPass(firstPass, lastReader);
                    ASSERT(childIndex < passCount);
                    if(!visited[childIndex]) {
                        // Push
                        stack[stackSize++] = childIndex;
                        visited[childIndex] = 1;
                    }
                }*/
            }
        }
    }
    ASSERT(sortedCount <= passCount);

    // SortedPasses is acually in reverse order so reverse it...
    /*for(u32 i = 0; i < sortedCount / 2; ++i) {
        RenderGraphPass* tmp = sortedPasses[i];
        sortedPasses[i] = sortedPasses[sortedCount - i - 1];
        sortedPasses[sortedCount - i - 1] = tmp;
    }*/

    for(u32 i = 0; i < sortedCount; ++i) {
        sortedPasses[i].graph = result;
        for(u32 j = 0; j < sortedPasses[i].outputCount; ++j) {
            //struct RenderGraphBuildImage* output = getImageFromHandle(builder, sortedPasses[i].outputs[j].imageHandle);
            //output->producer = &sortedPasses[i];
        }
    }
    result->sortedPasses = sortedPasses;
    result->passCount = sortedCount;
}

StromboliImage renderGraphAllocateFramebuffer(RenderGraph* graph, StromboliContext* context, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlags samples) {
    StromboliImage result = {0};

    {
        VkImageCreateInfo createInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.extent.width = width;
		createInfo.extent.height = height;
		createInfo.extent.depth = 1;
		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;
		createInfo.format = format;
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.usage = usage;
		createInfo.samples = samples;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(context->device, &createInfo, 0, &result.image);
    }

    // Asking for requirements already requires image
    VkImageMemoryRequirementsInfo2 memoryInfo = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
	memoryInfo.image = result.image;
	VkMemoryDedicatedRequirements dedicatedRequirements = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
	VkMemoryRequirements2 imageMemoryRequirements = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
	imageMemoryRequirements.pNext = &dedicatedRequirements;
	vkGetImageMemoryRequirements2(context->device, &memoryInfo, &imageMemoryRequirements);
    VkMemoryRequirements memoryRequirements = imageMemoryRequirements.memoryRequirements;

    // Check if we have an available block. Otherwise allocate a new one!
    struct RenderGraphMemoryBlock* memoryBlock = graph->firstBlock;
    struct RenderGraphMemoryBlock** nextPointerLocation = &graph->firstBlock;
    u32 memoryTypeIndex = stromboliFindMemoryType(context, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    while(memoryBlock) {
        if(memoryBlock->type == memoryTypeIndex) {
            u64 memoryOffset = ALIGN_UP_POW2(memoryBlock->offset, memoryRequirements.alignment);
            if(memoryOffset + memoryRequirements.size <= memoryBlock->size) {
                // Enough space left
                break;
            }
        }
        nextPointerLocation = &memoryBlock->next;
        memoryBlock = memoryBlock->next;
    }
    if(!memoryBlock) {
        // Need to allocate a new memory block
        printf("Allcoating new memory block for framebuffers\n");
        ASSERT(*nextPointerLocation == 0);
        VkMemoryAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocateInfo.allocationSize = MAX(memoryRequirements.size, MB(256));
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        memoryBlock = ARENA_PUSH_STRUCT(&graph->arena, struct RenderGraphMemoryBlock);
        vkAllocateMemory(context->device, &allocateInfo, 0, &memoryBlock->memory);
        memoryBlock->size = allocateInfo.allocationSize;
        memoryBlock->offset = 0;
        memoryBlock->type = memoryTypeIndex;
        // Add to blocks
        *nextPointerLocation = memoryBlock;
    }
    u64 memoryOffset = ALIGN_UP_POW2(memoryBlock->offset, memoryRequirements.alignment);
    vkBindImageMemory(context->device, result.image, memoryBlock->memory, memoryOffset);
    memoryBlock->offset = memoryOffset + memoryRequirements.size;

    { // Create the image view
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if(isDepthFormat(format)) {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
		VkImageViewCreateInfo createInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		createInfo.image = result.image;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = format;
		createInfo.subresourceRange.aspectMask = aspect;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.layerCount = 1;
		vkCreateImageView(context->device, &createInfo, 0, &result.view);
	}
	result.width = width;
	result.height = height;
	result.depth = 1;
	result.mipCount = 1;
	result.format = format;
	result.samples = samples;
	return result;
}

static struct RenderGraphMemoryBlock* copyAndResetMemoryBlockList(MemoryArena* arena, struct RenderGraphMemoryBlock* firstBlock) {
    struct RenderGraphMemoryBlock* result = 0;

    struct RenderGraphMemoryBlock* memoryBlock = firstBlock;
    struct RenderGraphMemoryBlock** nextPointerLocation = &result;
    while(memoryBlock) {
        struct RenderGraphMemoryBlock* newBlock = ARENA_PUSH_STRUCT_NO_CLEAR(arena, struct RenderGraphMemoryBlock);
        MEMORY_COPY_STRUCT(newBlock, memoryBlock);
        newBlock->next = 0;
        newBlock->offset = 0;
        *nextPointerLocation = newBlock;
        nextPointerLocation = &newBlock->next;
        memoryBlock = memoryBlock->next;
    }

    return result;
}

RenderGraph* renderGraphCompile(RenderGraphBuilder* builder, RenderGraphImageHandle swapchainOutputHandle, RenderGraph* oldGraph) {
    // We can use the builder arena as scratch here
    MemoryArena* scratch = builder->arena;

    VkCommandBuffer* oldCommandBuffers = 0;
    u32 oldCommandBufferCountPerFrame = 0;
    StromboliImage* imageDeleteQueue = 0;
    u32 imageDeleteCount = 0;
    RenderGraph* result = 0;
    struct RenderGraphMemoryBlock* firstBlock = 0; 
    if(oldGraph) {
        // Move data from old graph to scratch memory
        oldCommandBuffers = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, oldGraph->commandBufferCountPerFrame*2, VkCommandBuffer);
        oldCommandBufferCountPerFrame = oldGraph->commandBufferCountPerFrame;
        MEMORY_COPY(oldCommandBuffers, oldGraph->commandBuffers, sizeof(VkCommandBuffer) * oldGraph->commandBufferCountPerFrame * 2);
        // We cannot destroy the images here as they might still be used in rendering!
        imageDeleteQueue = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, oldGraph->imageCount, StromboliImage);
        imageDeleteCount = oldGraph->imageCount;
        MEMORY_COPY(imageDeleteQueue, oldGraph->images, sizeof(StromboliImage) * imageDeleteCount);
        ASSERT(!oldGraph->imageDeleteCount);
        firstBlock = copyAndResetMemoryBlockList(scratch, oldGraph->firstBlock);
        arenaResetToMarker(oldGraph->resetMarker);
        result = oldGraph;
    } else {
        result = ARENA_BOOTSTRAP_PUSH_STRUCT(createGrowingArena(osGetMemorySubsystem(), KB(4)), RenderGraph, arena);
        enableDebugMemoryOverflowDetectForArena(&result->arena);
        result->resetMarker = arenaCreateMarker(&result->arena);
    }

    // Result is either cleared or contains the data from oldGraph
    ASSERT(result);
    if(result) {
        // Reset graph
        result->context = builder->context;
        result->finalImageHandle.handle = 0;
        result->sortedPasses = 0;
        result->passCount = 0;
        result->images = 0;
        result->imageCount = 0;
        result->commandBuffers = 0;
        result->fingerprint = builder->fingerprint;

        // Flatten images to array
        u32 imageCount = 0;
        struct RenderGraphBuildImage* image = builder->imageSentinel.next;
        while(image) {
            image = image->next;
            imageCount++;
        }
        result->images = ARENA_PUSH_ARRAY_NO_CLEAR(&result->arena, imageCount, StromboliImage);
        result->clearValues = ARENA_PUSH_ARRAY_NO_CLEAR(&result->arena, imageCount, VkClearValue);
        //result->requiresClear = ARENA_PUSH_ARRAY(&result->arena, imageCount, bool);
        result->imageCount = imageCount;
        ASSERT(result->imageCount == builder->currentResourceIndex);

        struct RenderGraphBuildImage* swapchainOutput = getImageFromHandle(builder, swapchainOutputHandle);
        result->finalImageHandle = swapchainOutputHandle;
        if(swapchainOutput) {
            swapchainOutput->usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            swapchainOutput->isSwpachainOutput = true;
        }

        if(firstBlock) {
            result->firstBlock = copyAndResetMemoryBlockList(&result->arena, firstBlock);
        }

        // Create semaphores if not already existing
        if(!result->imageAcquireSemaphore) {
            VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vkCreateSemaphore(builder->context->device, &createInfo, 0, &result->imageAcquireSemaphore);
            for(u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
                vkCreateSemaphore(builder->context->device, &createInfo, 0, &result->imageReleaseSemaphores[i]);
            }
        }

        if(imageDeleteCount) {
            result->imageDeleteCount = imageDeleteCount;
            result->imageDeleteQueue = ARENA_PUSH_ARRAY_NO_CLEAR(&result->arena, imageDeleteCount, StromboliImage);
            MEMORY_COPY(result->imageDeleteQueue, imageDeleteQueue, sizeof(StromboliImage) * imageDeleteCount);
        }

        // Sort passes
        result->swapchainOutputPassIndex = getIndexOfPass(builder->passSentinel.next, swapchainOutput->producer);
        swapchainOutput->producer->external = true;
        struct RenderGraphBuildPass* firstPass = builder->passSentinel.next;
        u32 passCount = 0;
        struct RenderGraphBuildPass* pass = firstPass;
        while(pass) {
            pass = pass->next;
            passCount++;
        }
        sortPasses(builder, passCount, result);
        ASSERT(result->passCount <= passCount);

        // Create images
        u32 totalClearCount = 0;
        image = builder->imageSentinel.next;
        for(u32 i = 0; i < result->imageCount; ++i) {
            ASSERT(image->image.width);
            ASSERT(image->image.height);
            VkImageUsageFlags usage = image->usage;
            if(usage <= 3) {
                // Only transfer usage is not allowed so we add VK_IMAGE_USAGE_SAMPLED_BIT
                usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            
            #if 1
            image->image = renderGraphAllocateFramebuffer(result, builder->context, image->image.width, image->image.height, image->format, usage, image->image.samples);
            #else
            image->image = stromboliImageCreate(builder->context, image->image.width, image->image.height, image->format, usage, &(struct StromboliImageParameters) {
                .sampleCount = image->image.samples,
            });
            #endif
            result->images[result->imageCount-i-1] = image->image;
            image = image->next;
        }

        // Create barriers
        VkImageMemoryBarrier2KHR* totalClearBarriers = ARENA_PUSH_ARRAY(&result->arena, totalClearCount, VkImageMemoryBarrier2KHR);
        u32 totalClearBarrierIndex = 0;
        for(u32 passIndex = 0; passIndex < result->passCount; ++passIndex) {
            RenderGraphPass* pass = &result->sortedPasses[passIndex];
            pass->afterClearBarriers = &totalClearBarriers[totalClearBarrierIndex];
            pass->imageBarriers = ARENA_PUSH_ARRAY(&result->arena, pass->inputCount + pass->outputCount, VkImageMemoryBarrier2KHR);
            for(u32 i = 0; i < pass->inputCount; ++i) {
                struct RenderAttachment inputAttachment = pass->inputs[i];
                RenderGraphImage* inputImage = getImageFromHandle(builder, inputAttachment.imageHandle);
                struct RenderGraphBuildPass* producer = inputAttachment.producer;
                ASSERT(producer);

                struct RenderAttachment outputAttachment = {0};
                for(u32 j = 0; j < producer->outputCount; ++j) {
                    if(producer->outputs[j].imageHandle.handle == inputAttachment.imageHandle.handle) {
                        outputAttachment = producer->outputs[j];
                    }
                }

                if(inputImage->isSwpachainOutput) {
                    pass->imageBarriers[pass->imageBarrierCount++] = stromboliCreateImageBarrier(result->images[getImageHandleData(inputAttachment.imageHandle)].image, 
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        inputAttachment.stage, inputAttachment.access, inputAttachment.layout);
                } else {
                    pass->imageBarriers[pass->imageBarrierCount++] = stromboliCreateImageBarrier(result->images[getImageHandleData(inputAttachment.imageHandle)].image, 
                        outputAttachment.stage, outputAttachment.access, outputAttachment.layout,
                        inputAttachment.stage, inputAttachment.access, inputAttachment.layout);
                }
                if(isDepthFormat(result->images[getImageHandleData(inputAttachment.imageHandle)].format)) {
                    pass->imageBarriers[pass->imageBarrierCount-1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                }
            }
            for(u32 i = 0; i < pass->outputCount; ++i) {
                struct RenderAttachment outputAttachment = pass->outputs[i];
                bool usedAsInput = false;
                for(u32 j = 0; j < pass->inputCount; ++j) {
                    if(pass->inputs[j].imageHandle.handle == pass->outputs[i].imageHandle.handle) {
                        usedAsInput = true;
                    }
                }
                if(usedAsInput) {
                    // No barrier for our own inputs!
                    continue;
                }
                RenderGraphImage* outputImage = getImageFromHandle(builder, outputAttachment.imageHandle);
                if(pass->outputs[i].requiresClear) {
                    result->clearValues[getImageHandleData(pass->outputs[i].imageHandle)] = outputImage->clearColor;
                }
                if(pass->outputs[i].requiresClear && pass->type != RENDER_GRAPH_PASS_TYPE_GRAPHICS) {
                    // We need barriers for an intermediate clear call
                    pass->imageBarriers[pass->imageBarrierCount++] = stromboliCreateImageBarrier(result->images[getImageHandleData(outputAttachment.imageHandle)].image, 
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED, 
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
                    if(isDepthFormat(result->images[getImageHandleData(outputAttachment.imageHandle)].format)) {
                        pass->imageBarriers[pass->imageBarrierCount-1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    }

                    VkImageMemoryBarrier2KHR afterClearBarrier = stromboliCreateImageBarrier(result->images[getImageHandleData(outputAttachment.imageHandle)].image,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                        outputAttachment.stage, outputAttachment.access, outputAttachment.layout);
                    if(isDepthFormat(result->images[getImageHandleData(outputAttachment.imageHandle)].format)) {
                        afterClearBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    }
                    totalClearBarriers[totalClearBarrierIndex++] = afterClearBarrier;
                    pass->afterClearBarrierCount++;
                } else {
                    // We do not clear so we can transition from undefined and ignore previous content
                    pass->imageBarriers[pass->imageBarrierCount++] = stromboliCreateImageBarrier(result->images[getImageHandleData(outputAttachment.imageHandle)].image, 
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED, 
                        outputAttachment.stage, outputAttachment.access, outputAttachment.layout);
                    if(isDepthFormat(result->images[getImageHandleData(outputAttachment.imageHandle)].format)) {
                        pass->imageBarriers[pass->imageBarrierCount-1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    }
                }
            }
        }
        ASSERT(totalClearBarrierIndex <= totalClearCount);

        // Create swapchain barrier
        struct RenderGraphBuildPass* producer = swapchainOutput->producer;
        result->swapchainOutputPassIndex = result->passCount - getIndexOfPass(builder->passSentinel.next, producer) - 1;
        struct RenderAttachment outputAttachment = {0};
        for(u32 j = 0; j < producer->outputCount; ++j) {
            if(producer->outputs[j].imageHandle.handle == swapchainOutputHandle.handle) {
                outputAttachment = producer->outputs[j];
            }
        }
        result->finalImageBarrier = stromboliCreateImageBarrier(result->images[getImageHandleData(swapchainOutputHandle)].image, 
            outputAttachment.stage, outputAttachment.access, outputAttachment.layout, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Create command pools if not existing
        if(!result->commandPools[0]) {
            for(u32 i = 0; i < ARRAY_COUNT(result->commandPools); ++i) {
                VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                createInfo.queueFamilyIndex = builder->context->graphicsQueues[0].familyIndex;
                vkCreateCommandPool(builder->context->device, &createInfo, 0, &result->commandPools[i]);
            }
        }

        // Create query pools if not existing
        if(!result->queryPools[0]) {
            for(u32 i = 0; i < ARRAY_COUNT(result->queryPools); ++i) {
                VkQueryPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
                createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
                createInfo.queryCount = TIMING_SECTION_COUNT * 2;
                vkCreateQueryPool(builder->context->device, &createInfo, 0, &result->queryPools[i]);
            }
        }

        // Allocate commandbuffers
        result->commandBufferCountPerFrame = MAX(result->passCount, oldCommandBufferCountPerFrame);
        ASSERT(result->commandBufferCountPerFrame >= result->passCount);
        result->commandBuffers = ARENA_PUSH_ARRAY(&result->arena, result->commandBufferCountPerFrame * 2, VkCommandBuffer);
        
        if(result->commandBufferCountPerFrame == oldCommandBufferCountPerFrame) {
            // We have the exact same number of passes. This means we can just reuse the old command buffers as is
            MEMORY_COPY(result->commandBuffers, oldCommandBuffers, sizeof(VkCommandBuffer) * oldCommandBufferCountPerFrame * 2);
        } else if(result->commandBufferCountPerFrame < oldCommandBufferCountPerFrame) {
            // Should not happen anymore as new scheme only grows!
            // We require less than the previous graph. We have to keep in mind that the command buffers belong to two separate pools
            //MEMORY_COPY(result->commandBuffers, oldCommandBuffers, sizeof(VkCommandBuffer) * result->passCount);
            //MEMORY_COPY(result->commandBuffers+result->passCount, oldCommandBuffers + oldPassCount, sizeof(VkCommandBuffer) * result->passCount);
            ASSERT(false);
        } else {
            // We require new command buffers!
            //TODO: Currently we only support this if previous graph was empty
            ASSERT(!oldCommandBufferCountPerFrame);
            vkResetCommandPool(result->context->device, result->commandPools[0], 0);
            vkResetCommandPool(result->context->device, result->commandPools[1], 0);
            for(u32 i = 0; i < result->commandBufferCountPerFrame * 2; ++i) {
                VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandPool = result->commandPools[i>=result->commandBufferCountPerFrame];
                allocateInfo.commandBufferCount = 1;
                vkAllocateCommandBuffers(result->context->device, &allocateInfo, &result->commandBuffers[i]);
            }
        }
        for(u32 i = 0; i < result->commandBufferCountPerFrame; ++i) {
            stromboliNameObject(result->context, (u64)result->commandBuffers[i], VK_OBJECT_TYPE_COMMAND_BUFFER, str8GetCstr(builder->arena, result->sortedPasses[i].name));
            stromboliNameObject(result->context, (u64)result->commandBuffers[i+result->commandBufferCountPerFrame], VK_OBJECT_TYPE_COMMAND_BUFFER, str8GetCstr(builder->arena, result->sortedPasses[i].name));
        }
    }

    // We do not need the builder memory after this point as everything is stored in the RenderGraph arena
    arenaResetToMarker(builder->resetMarker);
    return result;
}
