#include <stromboli/stromboli_render_graph.h>

#include "render_graph_definitions.inl"

#include <stdio.h>
#include "../vulkan_enum_string_helper.h"

void renderGraphBuilderPrint(RenderGraphBuilder* builder) {
    // Print all passes with all inputs and outputs
    struct RenderGraphBuildPass* pass = builder->passSentinel.next;
    u32 index = 0;
    while(pass) {
        printf("Pass%u: %.*s\n", index, (int)pass->name.size, (const char*)pass->name.base);

        printf("\tInputs:\n");
        for(u32 i = 0; i < pass->inputCount; ++i) {
            struct RenderAttachment input = pass->inputs[i];
            struct RenderGraphBuildImage* inputImage = getImageFromHandle(builder, input.imageHandle);
            printf("\t\tIndex: %u\n", getImageHandleData(input.imageHandle));
            printf("\t\tFormat: %u\n", inputImage->format);
        }

        printf("\tOutputs:\n");
        for(u32 i = 0; i < pass->outputCount; ++i) {
            struct RenderAttachment output = pass->outputs[i];
            struct RenderGraphBuildImage* outputImage = getImageFromHandle(builder, output.imageHandle);
            printf("\t\tIndex: %u\n", getImageHandleData(output.imageHandle));
            printf("\t\tFormat: %u\n", outputImage->format);
        }

        pass = pass->next;
        ++index;
    }
}

static void printBarrier(VkImageMemoryBarrier2KHR barrier) {
    printf("\t\tVkImage: %p\n", barrier.image);
    printf("\t\tSource stage: %s\n", string_VkPipelineStageFlagBits2(barrier.srcStageMask));
    printf("\t\tSource access: %s\n", string_VkAccessFlagBits2(barrier.srcAccessMask));
    printf("\t\tOld layout: %s\n", string_VkImageLayout(barrier.oldLayout));
    printf("\t\tDest stage: %s\n", string_VkPipelineStageFlagBits2(barrier.dstStageMask));
    printf("\t\tDest access: %s\n", string_VkAccessFlagBits2(barrier.dstAccessMask));
    printf("\t\tNew layout: %s\n", string_VkImageLayout(barrier.newLayout));
}

void renderGraphPrint(RenderGraph* graph) {
    for(u32 i = 0; i < graph->passCount * 2; ++i) {
        printf("Command buffer%u: %p\n", i, graph->commandBuffers[i]);
    }
    for(u32 passIndex = 0; passIndex < graph->passCount; ++passIndex) {
        RenderGraphPass pass = graph->sortedPasses[passIndex];
        printf("Pass%u: %.*s\n", passIndex, (int)pass.name.size, (const char*)pass.name.base);
        if(passIndex == graph->swapchainOutputPassIndex) {
            printf("\tSwapchain output\n");
        }
        printf("\tCommand buffer: %p\n", graph->commandBuffers[graph->commandBufferOffset + passIndex]);

        printf("\tBarriers:\n");
        for(u32 i = 0; i < pass.imageBarrierCount; ++i) {
            VkImageMemoryBarrier2KHR barrier = pass.imageBarriers[i];
            printBarrier(barrier);
        }

        if(pass.afterClearBarrierCount) {
            printf("\tAfter clear barriers:\n");
            for(u32 i = 0; i < pass.afterClearBarrierCount; ++i) {
                VkImageMemoryBarrier2KHR barrier = pass.afterClearBarriers[i];
                printBarrier(barrier);
            }
        }

        printf("\tInputs:\n");
        for(u32 i = 0; i < pass.inputCount; ++i) {
            struct RenderAttachment input = pass.inputs[i];
            struct StromboliImage* inputImage = &graph->images[getImageHandleData(input.imageHandle)];
            printf("\tInput%u:\n", i);
            printf("\t\tImage Index: %u\n", getImageHandleData(input.imageHandle));
            printf("\t\tVkImage: %p\n", inputImage->image);
            printf("\t\tVkImageView: %p\n", inputImage->view);
            printf("\t\tFormat: %s\n", string_VkFormat(inputImage->format));
            printf("\t\tDimensions(WxHxD): %ux%ux%u\n", inputImage->width, inputImage->height, inputImage->depth);
            printf("\t\tMipLevels: %u\n", inputImage->mipCount);
            printf("\t\tSamples: %s\n", string_VkSampleCountFlagBits(inputImage->samples));
        }

        printf("\tOutputs:\n");
        for(u32 i = 0; i < pass.outputCount; ++i) {
            struct RenderAttachment output = pass.outputs[i];
            struct StromboliImage* outputImage = &graph->images[getImageHandleData(output.imageHandle)];
            printf("\tOutput%u:\n", i);
            printf("\t\tImage Index: %u\n", getImageHandleData(output.imageHandle));
            printf("\t\tVkImage: %p\n", outputImage->image);
            printf("\t\tVkImageView: %p\n", outputImage->view);
            printf("\t\tFormat: %s\n", string_VkFormat(outputImage->format));
            printf("\t\tDimensions(WxHxD): %ux%ux%u\n", outputImage->width, outputImage->height, outputImage->depth);
            printf("\t\tMipLevels: %u\n", outputImage->mipCount);
            printf("\t\tSamples: %s\n", string_VkSampleCountFlagBits(outputImage->samples));
            if(output.requiresClear) {
                VkClearValue clearValue = graph->clearValues[getImageHandleData(output.imageHandle)];
                printf("\t\tCleared with: (%f,%f,%f,%f)\n", clearValue.color.float32[0], clearValue.color.float32[1], clearValue.color.float32[2], clearValue.color.float32[3]);
            }
        }
    }
    if(graph->finalImageBarrier.image) {
        printf("\tFinal barrier:\n");
        VkImageMemoryBarrier2KHR barrier = graph->finalImageBarrier;
        printBarrier(barrier);
    }
}
