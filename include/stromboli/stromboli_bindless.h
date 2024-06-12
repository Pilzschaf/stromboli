#ifndef STROMBOLI_BINDLESS_H
#define STROMBOLI_BINDLESS_H

#include "stromboli.h"

// Requires either Vulkan 1.2 or VK_EXT_descriptor_indexing extension

// Include bindless.glsl in shaders to use bindless rendering

void stromboliBindlessInit(StromboliContext* context);
void stromboliBindlessShutdown(StromboliContext* context);
void stromboliBindlessBeginFrame(u32 frameIndex);

u32 stromboliBindlessBindBuffer(StromboliBuffer* buffer);
u32 stromboliBindlessBindImage(StromboliContext* context, StromboliImage* image, VkImageLayout layout);
void stromboliBindlessBindDescriptorSet(StromboliPipeline pipeline, VkCommandBuffer commandBuffer, u32 frameIndex);
VkDescriptorSetLayout stromboliBindlessGetDescriptorSetLayout();

#endif // STROMBOLI_BINDLESS_H