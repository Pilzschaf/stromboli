#include <stromboli.h>
#include <grounded/memory/grounded_arena.h>
#include <grounded/threading/grounded_threading.h>

StromboliRenderpass stromboliRenderpassCreate(StromboliContext* context, u32 subpassCount, StromboliSubpass* subpasses) {
    MemoryArena* scratch = threadContextGetScratch(0);
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
    VkClearValue* clearColors = (VkClearValue*)malloc(sizeof(VkClearValue) * maxNumClearColors);
    u32 currentReferenceIndex = 0;
    VkAttachmentReference* references = ARENA_PUSH_ARRAY(scratch, maxNumReferences, VkAttachmentReference);
    u32 currentAttachmentIndex = 0;
    VkAttachmentDescription* attachments = ARENA_PUSH_ARRAY(scratch, maxNumAttachments, VkAttachmentDescription);
    StromboliFramebuffer** attachmentViews = ARENA_PUSH_ARRAY(scratch, maxNumAttachments, StromboliFramebuffer*);
    u32 width = 0; // All attachments must have the same size
    u32 height = 0;

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
                ASSERT(width == 0 || width == a.framebuffer->images[0].width);
                ASSERT(height == 0 || height == a.framebuffer->images[0].height);
                width = a.framebuffer->images[0].width;
                height = a.framebuffer->images[0].height;
                attachmentViews[currentAttachmentIndex] = a.framebuffer;
                attachments[currentAttachmentIndex] = (VkAttachmentDescription){0};
                attachments[currentAttachmentIndex].format = a.framebuffer->format;
                attachments[currentAttachmentIndex].samples = a.framebuffer->sampleCount;
                attachments[currentAttachmentIndex].loadOp = a.loadOp;
                attachments[currentAttachmentIndex].storeOp = a.storeOp;
                attachments[currentAttachmentIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachments[currentAttachmentIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[currentAttachmentIndex].initialLayout = a.initialLayout;
                attachments[currentAttachmentIndex].finalLayout = a.finalLayout;
                if(attachments[currentAttachmentIndex].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                    clearColors[currentClearColorIndex].color = a.clearColor.color;
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
    u32 numFramebuffers = MAX_SWAPCHAIN_IMAGES;
    VkImageView* framebufferViews = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, currentAttachmentIndex, VkImageView);
    for(u32 i = 0; i < numFramebuffers; ++i) {
        for(u32 j = 0; j < currentAttachmentIndex; ++j) {
            // Apply all image views
            framebufferViews[j] = attachmentViews[j]->images[i].view;
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

}
