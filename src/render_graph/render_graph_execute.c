#include <stromboli/stromboli_render_graph.h>

#include "render_graph_definitions.inl"

#include <grounded/threading/grounded_threading.h>

RenderGraphPass* beginRenderPass(RenderGraph* graph, RenderGraphPassHandle passHandle) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    // When this assert triggers it is very likely, that you use an outdated pass handle that has not been submitted to the builder of this graph
    ASSERT(getPassFingerprint(passHandle) == graph->fingerprint);

    u32 passHandleValue = getPassHandleData(passHandle);
    ASSERT(passHandleValue <= graph->passCount);

    RenderGraphPass* pass = 0;
    if(passHandleValue > 0) {
        u32 sortedHandle = graph->buildPassToSortedPass[passHandleValue-1];
        if(sortedHandle != UINT32_MAX) {
            pass = &graph->sortedPasses[sortedHandle];
        }
    }

    if(pass) {
        VkCommandBuffer commandBuffer = graph->commandBuffers[(pass - graph->sortedPasses)+graph->commandBufferOffset];
        pass->commandBuffer = commandBuffer;

        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        if(pass == &graph->sortedPasses[0]) {
            // First command buffer
            vkCmdResetQueryPool(commandBuffer, graph->queryPools[0], 0, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, graph->queryPools[0], 0);
            graph->timestampCount++;
        }

        // Layout transitions
        stromboliPipelineBarrier(commandBuffer, 0, 0, 0, pass->imageBarrierCount, pass->imageBarriers);
        
        if(pass->type == RENDER_GRAPH_PASS_TYPE_GRAPHICS) {
            ASSERT(pass->outputCount > 0);
            VkRenderingInfo renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
            renderingInfo.colorAttachmentCount = 0;
            renderingInfo.layerCount = 1;
            VkRenderingAttachmentInfo* attachments = ARENA_PUSH_ARRAY(scratch, pass->outputCount, VkRenderingAttachmentInfo);
            renderingInfo.pColorAttachments = attachments;
            for(u32 i = 0; i < pass->outputCount; ++i) {
                struct RenderAttachment output = pass->outputs[i];
                if(output.resolveTarget) {
                    // Resolve targets are not directly used as attachments and therefore skipped
                    continue;
                }
                ASSERT(getImageFingerprint(output.imageHandle) == graph->fingerprint);
                u32 imageHandleData = getImageHandleData(output.imageHandle);
                StromboliImage* outputImage = &graph->images[imageHandleData];
                if(i == 0) {
                    stromboliCmdSetViewportAndScissor(commandBuffer, outputImage->width, outputImage->height);
                    renderingInfo.renderArea = (VkRect2D){{0, 0}, {outputImage->width, outputImage->height}};
                }
                VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                if(output.requiresClear) {
                    loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                }
                VkRenderingAttachmentInfo* attachment = ARENA_PUSH_STRUCT(scratch, VkRenderingAttachmentInfo);
                *attachment = (struct VkRenderingAttachmentInfo) {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .loadOp = loadOp,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .imageView = outputImage->view,
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                };
                attachment->clearValue = graph->clearValues[imageHandleData];
                if(output.resolve.handle) {
                    StromboliImage* resolveTarget = renderPassGetOutputResource(pass, output.resolve);
                    if(resolveTarget) {
                        attachment->resolveMode = output.resolveMode;
                        attachment->resolveImageView = resolveTarget->view;
                        attachment->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;   
                    }
                }

                if(isDepthFormat(outputImage->format)) {
                    attachment->imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    renderingInfo.pDepthAttachment = attachment;
                } else {
                    attachments[renderingInfo.colorAttachmentCount++] = *attachment;
                }
            }
            //stromboliCmdBeginRenderpass(pass->commandBuffer, &pass->renderpass, pass->width, pass->height, 0);
            vkCmdBeginRendering(commandBuffer, &renderingInfo);
        } else {
            for(u32 i = 0; i < pass->outputCount; ++i) {
                struct RenderAttachment output = pass->outputs[i];
                ASSERT(getImageFingerprint(output.imageHandle) == graph->fingerprint);
                u32 imageHandleData = getImageHandleData(output.imageHandle);
                StromboliImage* outputImage = &graph->images[imageHandleData];
                bool depth = isDepthFormat(outputImage->format);
                if(!depth) {
                    if(output.requiresClear) {
                        VkImageSubresourceRange range = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .layerCount = 1,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .baseMipLevel = 0,
                        };
                        vkCmdClearColorImage(commandBuffer, outputImage->image, output.layout, &graph->clearValues[imageHandleData].color, 1, &range);
                    }
                } else {
                    if(output.requiresClear) {
                        VkImageSubresourceRange range = {
                            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                            .layerCount = 1,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .baseMipLevel = 0,
                        };
                        vkCmdClearDepthStencilImage(commandBuffer, outputImage->image, VK_IMAGE_LAYOUT_GENERAL, &graph->clearValues[imageHandleData].depthStencil, 1, &range);
                    }
                }
            }
            // Make clear(s) visible
            if(pass->afterClearBarrierCount > 0) {
                stromboliPipelineBarrier(commandBuffer, 0, 0, 0, pass->afterClearBarrierCount, pass->afterClearBarriers);
            }
        }
    }

    arenaEndTemp(temp);
    return pass;
}

u32 renderGraphGetFrameIndex(RenderGraph* graph) {
    ASSERT(graph->passCount > 0);
    return graph->commandBufferOffset > 0;
}

bool renderPassIsActive(RenderGraphPass* pass) {
    return pass != 0;
}

VkCommandBuffer renderPassGetCommandBuffer(RenderGraphPass* pass) {
    return pass->commandBuffer;
}

StromboliImage* renderPassGetInputResource(RenderGraphPass* pass, RenderGraphImageHandle imageHandle) {
    ASSERT(getImageFingerprint(imageHandle) == pass->graph->fingerprint);
    u32 imageHandleData = getImageHandleData(imageHandle);
    StromboliImage* result = &pass->graph->images[imageHandleData];
    return result;
}

StromboliImage* renderPassGetOutputResource(RenderGraphPass* pass, RenderGraphImageHandle imageHandle) {
    ASSERT(getImageFingerprint(imageHandle) == pass->graph->fingerprint);
    u32 imageHandleData = getImageHandleData(imageHandle);
    StromboliImage* result = &pass->graph->images[imageHandleData];
    return result;
}

bool renderGraphExecute(RenderGraph* graph, StromboliSwapchain* swapchain, VkFence fence) {
    StromboliContext* context = graph->context;

    // Wait for fence
    vkWaitForFences(context->device, 1, &fence, true, UINT64_MAX);
    vkResetFences(context->device, 1, &fence);

    if(graph->imageDeleteCount) {
        for(u32 i = 0; i < graph->imageDeleteCount; ++i) {
            stromboliImageDestroy(context, &graph->imageDeleteQueue[i]);
        }
        graph->imageDeleteCount = 0;
        graph->imageDeleteQueue = 0;
    }

    // Read timestamps
    uint64_t timestamps[TIMING_SECTION_COUNT * 2] = { 0 };
    u32 timestampCount = graph->timestampCount;
    ASSERT(timestampCount <= ARRAY_COUNT(timestamps));
    if(timestampCount > 1) {
        VkResult timestampsValid = vkGetQueryPoolResults(context->device, graph->queryPools[0], 0, timestampCount, sizeof(timestamps), timestamps, sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT);
        if(timestampsValid == VK_SUCCESS) {
            u32 startIndex = 0;
            u32 endIndex = 1;
            double begin = ((double)timestamps[startIndex]) * context->physicalDeviceProperties.limits.timestampPeriod * 1e-9;
            double end = ((double)timestamps[endIndex]) * context->physicalDeviceProperties.limits.timestampPeriod * 1e-9;
            float delta = (float)(end - begin);
            graph->lastDuration = delta;
        } else {
            graph->lastDuration = 0.0f;
        }
    } else {
        graph->lastDuration = 0.0f;
    }
    graph->timestampCount = 0;

    // Acquire image
    u32 imageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(context->device, swapchain->swapchain, UINT64_MAX, graph->imageAcquireSemaphore, 0, &imageIndex);
    if(acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain is out of date and must be resized
        return false;
    }
    ASSERT(acquireResult == VK_SUCCESS || acquireResult == VK_SUBOPTIMAL_KHR);

    VkImage image = swapchain->images[imageIndex];
    VkImageView imageView = swapchain->imageViews[imageIndex];

    for(u32 i = 0; i < graph->passCount; ++i) {
        VkCommandBuffer commandBuffer = graph->sortedPasses[i].commandBuffer;
        ASSERT(commandBuffer); // If this triggers, this pass has not been submitted!
        if(graph->sortedPasses[i].type == RENDER_GRAPH_PASS_TYPE_GRAPHICS) {
            vkCmdEndRendering(commandBuffer);
        }

        if(i == graph->swapchainOutputPassIndex) {
            // Layout transition
            VkImageMemoryBarrier2KHR imageBarrier = {0};
            ASSERT(getImageFingerprint(graph->finalImageHandle) == graph->fingerprint);
            u32 imageHandleData = getImageHandleData(graph->finalImageHandle);
            StromboliImage* finalImage = &graph->images[imageHandleData];

            VkImageMemoryBarrier2KHR imageBarriers[] = {
                graph->finalImageBarrier,
                stromboliCreateImageBarrier(image,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
                    };
            stromboliPipelineBarrier(commandBuffer, 0, 0, 0, ARRAY_COUNT(imageBarriers), imageBarriers);

            VkOffset3D blitSize;
            blitSize.x = swapchain->width;
            blitSize.y = swapchain->height;
            blitSize.z = 1;
            VkImageBlit region = (VkImageBlit){
                .srcSubresource.layerCount = 1,
                .srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .srcOffsets[1] = blitSize,
                .dstSubresource.layerCount = 1,
                .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .dstOffsets[1] = blitSize,
            };
            vkCmdBlitImage(commandBuffer, finalImage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
            
            // Layout transition
            imageBarrier = stromboliCreateImageBarrier(image, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            stromboliPipelineBarrier(commandBuffer, 0, 0, 0, 1, &imageBarrier);
        }

        if(i == graph->passCount -1) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, graph->queryPools[0], 1);
            graph->timestampCount++;
        }

        vkEndCommandBuffer(commandBuffer);
    }

    // Submit
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = graph->passCount;
    submitInfo.pCommandBuffers = graph->commandBuffers + graph->commandBufferOffset;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &graph->imageReleaseSemaphores[imageIndex];
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &graph->imageAcquireSemaphore;
    VkPipelineStageFlags waitMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitMask;
    vkQueueSubmit(context->graphicsQueues[0].queue, 1, &submitInfo, fence);

    // Present
    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain->swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &graph->imageReleaseSemaphores[imageIndex];
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(context->graphicsQueues[0].queue, &presentInfo);

    if(graph->commandBufferOffset) {
        graph->commandBufferOffset = 0;
    } else {
        graph->commandBufferOffset = graph->commandBufferCountPerFrame;
    }
    vkResetCommandPool(context->device, graph->commandPools[!!graph->commandBufferOffset], 0);
    
    if(presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        vkQueueWaitIdle(context->graphicsQueues[0].queue);
        return false;
    }
    return true;
}

float renderGraphGetLastDuration(RenderGraph* graph) {
    float result = 0.0f;
    if(graph) {
        result = graph->lastDuration;
    }
    return result;
}

void renderGraphDestroy(RenderGraph* graph, VkFence fence) {
    StromboliContext* context = graph->context;

    // Wait for fence
    vkWaitForFences(context->device, 1, &fence, true, UINT64_MAX);
    vkResetFences(context->device, 1, &fence);

    if(graph->imageDeleteCount) {
        for(u32 i = 0; i < graph->imageDeleteCount; ++i) {
            stromboliImageDestroy(context, &graph->imageDeleteQueue[i]);
        }
        graph->imageDeleteCount = 0;
        graph->imageDeleteQueue = 0;
    }

    struct RenderGraphMemoryBlock* memoryBlock = graph->firstBlock;
    while(memoryBlock) {
        vkFreeMemory(context->device, memoryBlock->memory, 0);
        memoryBlock = memoryBlock->next;
    }

    vkDestroyCommandPool(context->device, graph->commandPools[0], 0);
    vkDestroyCommandPool(context->device, graph->commandPools[1], 0);

    vkDestroyQueryPool(context->device, graph->queryPools[0], 0);
    vkDestroyQueryPool(context->device, graph->queryPools[1], 0);

    vkDestroySemaphore(context->device, graph->imageAcquireSemaphore, 0);
    for(u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
        vkDestroySemaphore(context->device, graph->imageReleaseSemaphores[i], 0);
    }

    for(u32 i = 0; i < graph->imageCount; ++i) {
        stromboliImageDestroy(context, &graph->images[i]);
    }

    MEMORY_CLEAR_STRUCT(graph);
}
