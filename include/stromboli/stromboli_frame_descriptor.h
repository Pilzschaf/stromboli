#ifndef STROMBOLI_FRAME_DESCRIPTOR_H
#define STROMBOLI_FRAME_DESCRIPTOR_H

#include "stromboli.h"
#include <grounded/memory/grounded_arena.h>

void stromboliFrameDescriptorInit(StromboliContext* context);
void stromboliFrameDescriptorShutdown(StromboliContext* context);
void stromboliFrameDescriptorBeginNewFrame(StromboliContext* context, MemoryArena* nextFrameArena, u32 frameIndex);

VkDescriptorSet stromboliGetDescriptorSetForPipeline(StromboliContext* context, StromboliPipeline* pipeline, u32 setIndex, u32 frameIndex);
void stromboliUpdateAndBindDescriptorSetForPipeline(StromboliContext* context, VkCommandBuffer commandBuffer, StromboliPipeline* pipeline, u32 setIndex, u32 frameIndex, StromboliDescriptorInfo* descriptors);

#endif // STROMBOLI_FRAME_DESCRIPTOR_H