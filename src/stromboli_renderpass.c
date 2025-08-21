#include <stromboli/stromboli.h>
#include <grounded/memory/grounded_arena.h>
#include <grounded/threading/grounded_threading.h>

StromboliRenderpass stromboliRenderpassCreate(StromboliContext* context, MemoryArena* clearValueArena, u32 width, u32 height, u32 subpassCount, StromboliSubpass* subpasses) {
    MemoryArena* scratch = threadContextGetScratch(clearValueArena);
    ArenaTempMemory tempMemory = arenaBeginTemp(scratch);

    // Only a single subpass allowed currently as things like dependencies must be ironed out
    ASSERT(subpassCount == 1);

    // Calculate upper bounds
    u32 maxNumAttachments = 0;
    u32 maxNumReferences = 0;
    u32 maxNumClearColors = 0; // Still an upper bound as some attachments might be deduplicated
    for(u32 i = 0; i < subpassCount; ++i) {
        maxNumAttachments += subpasses[i].inputAttachmentCount;
        maxNumReferences += subpasses[i].inputAttachmentCount;
        maxNumAttachments += subpasses[i].outputAttachmentCount;
        maxNumReferences += subpasses[i].outputAttachmentCount;
        for(u32 j = 0; j < subpasses[i].outputAttachmentCount; ++j) {
            if(subpasses[i].outputAttachments[j].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                maxNumClearColors++;
            }
        }
        if(subpasses[i].depthAttachment) {
            maxNumAttachments++;
            maxNumReferences++;
            if(subpasses[i].depthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                maxNumClearColors++;
            }
        }
    }

    // Reset slot assignments
    for(u32 i = 0; i < subpassCount; ++i) {
        for(u32 j = 0; j < subpasses[i].outputAttachmentCount; ++j) {
            subpasses[i].outputAttachments[j].__assignedSlot = -1;
        }
        if(subpasses[i].depthAttachment) {
            subpasses[i].depthAttachment->__assignedSlot = -1;
        }
        for(u32 j = 0; j < subpasses[i].inputAttachmentCount; ++j) {
            subpasses[i].inputAttachments[j].__assignedSlot = -1;
        }
    }

    u32 currentClearColorIndex = 0;
    // Arena may be 0 in which case we do no explicit clear values
    VkClearValue* clearColors = 0;
    if(clearValueArena) {
        clearColors = ARENA_PUSH_ARRAY(clearValueArena, maxNumClearColors, VkClearValue);
    }
    u32 currentReferenceIndex = 0;
    VkAttachmentReference* references = ARENA_PUSH_ARRAY(scratch, maxNumReferences, VkAttachmentReference);
    u32 currentAttachmentIndex = 0;
    VkAttachmentDescription* attachments = ARENA_PUSH_ARRAY(scratch, maxNumAttachments, VkAttachmentDescription);
    VkImageView* attachmentViews = ARENA_PUSH_ARRAY(scratch, maxNumAttachments, VkImageView);

    // Create attachments, references and subpass descriptions
    VkSubpassDescription* subpassDescriptions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, subpassCount, VkSubpassDescription);
    for(u32 i = 0; i < subpassCount; ++i) {
        subpassDescriptions[i] = (VkSubpassDescription){0};
        subpassDescriptions[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        // Outputs
        subpassDescriptions[i].colorAttachmentCount = subpasses[i].outputAttachmentCount;
        subpassDescriptions[i].pColorAttachments = &references[currentReferenceIndex];
        for(u32 j = 0; j < subpasses[i].outputAttachmentCount; ++j) {
            if(subpasses[i].outputAttachments[j].__assignedSlot == -1) {
                subpasses[i].outputAttachments[j].__assignedSlot = currentAttachmentIndex;
                struct StromboliAttachment a = subpasses[i].outputAttachments[j];
                attachmentViews[currentAttachmentIndex] = a.imageView;
                attachments[currentAttachmentIndex] = (VkAttachmentDescription){0};
                attachments[currentAttachmentIndex].format = a.format;
                attachments[currentAttachmentIndex].samples = a.sampleCount ? a.sampleCount : VK_SAMPLE_COUNT_1_BIT;
                attachments[currentAttachmentIndex].loadOp = a.loadOp;
                attachments[currentAttachmentIndex].storeOp = a.storeOp;
                attachments[currentAttachmentIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachments[currentAttachmentIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[currentAttachmentIndex].initialLayout = a.initialLayout;
                attachments[currentAttachmentIndex].finalLayout = a.finalLayout;
                if(attachments[currentAttachmentIndex].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                    if(clearColors) {
                        clearColors[currentClearColorIndex].color = a.clearColor.color;
                    }
                    ++currentClearColorIndex;
                    ASSERT(currentClearColorIndex <= maxNumClearColors);
                }
                currentAttachmentIndex++;
            } else {
                ASSERT(false);
            }
            if(subpasses[i].outputAttachments[j].usageLayout) {
                references[currentReferenceIndex].layout = subpasses[i].outputAttachments[j].usageLayout;
            } else {
                references[currentReferenceIndex].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            references[currentReferenceIndex].attachment = subpasses[i].outputAttachments[j].__assignedSlot;
            ++currentReferenceIndex;
        }
        // Depth
        if(subpasses[i].depthAttachment) {
            subpassDescriptions[i].pDepthStencilAttachment = &references[currentReferenceIndex];
            if(subpasses[i].depthAttachment->__assignedSlot == -1) {
                subpasses[i].depthAttachment->__assignedSlot = currentAttachmentIndex;
                struct StromboliAttachment a = *subpasses[i].depthAttachment;
                attachmentViews[currentAttachmentIndex] = subpasses[i].depthAttachment->imageView;
                attachments[currentAttachmentIndex] = (VkAttachmentDescription){0};
                attachments[currentAttachmentIndex].format = a.format;
                attachments[currentAttachmentIndex].samples = a.sampleCount ? a.sampleCount : VK_SAMPLE_COUNT_1_BIT;
                attachments[currentAttachmentIndex].loadOp = a.loadOp;
                attachments[currentAttachmentIndex].storeOp = a.storeOp;
                attachments[currentAttachmentIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachments[currentAttachmentIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[currentAttachmentIndex].initialLayout = a.initialLayout;
                attachments[currentAttachmentIndex].finalLayout = a.finalLayout;
                if(attachments[currentAttachmentIndex].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                    if(clearColors) {
                        clearColors[currentClearColorIndex].depthStencil = subpasses[i].depthAttachment->clearColor.depthStencil;
                    }
                    ++currentClearColorIndex;
                    ASSERT(currentClearColorIndex <= maxNumClearColors);
                }
                currentAttachmentIndex++;
            } else {
                ASSERT(false);
            }
            if(subpasses[i].depthAttachment->usageLayout) {
                references[currentReferenceIndex].layout = subpasses[i].depthAttachment->usageLayout;
            } else {
                references[currentReferenceIndex].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
            references[currentReferenceIndex].attachment = subpasses[i].depthAttachment->__assignedSlot;
            ++currentReferenceIndex;
        }
        // Inputs
        subpassDescriptions[i].inputAttachmentCount = subpasses[i].inputAttachmentCount;
        subpassDescriptions[i].pInputAttachments = &references[currentReferenceIndex];
        for(u32 j = 0; j < subpasses[i].inputAttachmentCount; ++j) {
            if(subpasses[i].inputAttachments[j].__assignedSlot == -1) {
                subpasses[i].inputAttachments[j].__assignedSlot = currentAttachmentIndex;
                struct StromboliAttachment a = subpasses[i].inputAttachments[j];
                attachmentViews[currentAttachmentIndex] = a.imageView;
                attachments[currentAttachmentIndex] = (VkAttachmentDescription){0};
                attachments[currentAttachmentIndex].format = a.format;
                attachments[currentAttachmentIndex].samples = a.sampleCount ? a.sampleCount : VK_SAMPLE_COUNT_1_BIT;
                attachments[currentAttachmentIndex].loadOp = a.loadOp;
                attachments[currentAttachmentIndex].storeOp = a.storeOp;
                attachments[currentAttachmentIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachments[currentAttachmentIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[currentAttachmentIndex].initialLayout = a.initialLayout;
                attachments[currentAttachmentIndex].finalLayout = a.finalLayout;
                // Clear not allowed for input attachments
                ASSERT(attachments[currentAttachmentIndex].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR);
                currentAttachmentIndex++;
            } else {
                // The only one that may reuse attachments from this subpass
            }
            if(subpasses[i].inputAttachments[j].usageLayout) {
                references[currentReferenceIndex].layout = subpasses[i].inputAttachments[j].usageLayout;
            } else {
                references[currentReferenceIndex].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            references[currentReferenceIndex].attachment = subpasses[i].inputAttachments[j].__assignedSlot;
            ++currentReferenceIndex;
        }
    }

    ASSERT(currentReferenceIndex == maxNumReferences);
    ASSERT(currentAttachmentIndex <= maxNumAttachments);

    VkRenderPassCreateInfo createInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    createInfo.attachmentCount = currentAttachmentIndex;
    createInfo.pAttachments = attachments;
    createInfo.subpassCount = subpassCount;
    createInfo.pSubpasses = subpassDescriptions;

    StromboliRenderpass result = {0};
    vkCreateRenderPass(context->device, &createInfo, 0, &result.renderPass);
    result.clearColors = clearColors;
    result.numClearColors = currentClearColorIndex;

    // Framebuffer
    ASSERT(subpassCount == 1);
    u32 numFramebuffers = subpasses->swapchainOutput ? subpasses->swapchainOutput->numImages : 1;
    VkImageView* framebufferViews = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, currentAttachmentIndex, VkImageView);
    for(u32 i = 0; i < numFramebuffers; ++i) {
        for(u32 j = 0; j < currentAttachmentIndex; ++j) {
            // Apply all image views
            framebufferViews[j] = attachmentViews[j];
            if(!framebufferViews[j]) {
                ASSERT(subpasses->swapchainOutput);
                framebufferViews[j] = subpasses->swapchainOutput->imageViews[i];
            }
        }
        
        VkFramebufferCreateInfo createInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        createInfo.renderPass = result.renderPass;
        createInfo.attachmentCount = currentAttachmentIndex;
        createInfo.pAttachments = framebufferViews;
        createInfo.width = width;
        createInfo.height = height;
        createInfo.layers = 1;

        vkCreateFramebuffer(context->device, &createInfo, 0, &result.framebuffers[i]);
    }

    arenaEndTemp(tempMemory);
    return result;
}

void stromboliRenderpassDestroy(StromboliContext* context, StromboliRenderpass* renderPass) {
    for(u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
        if(renderPass->framebuffers[i]) {
            vkDestroyFramebuffer(context->device, renderPass->framebuffers[i], 0);
        }
    }
    vkDestroyRenderPass(context->device, renderPass->renderPass, 0);
}
