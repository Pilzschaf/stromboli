#ifndef STROMBOLI_BINDLESS_H
#define STROMBOLI_BINDLESS_H

#include "stromboli.h"

// Requires either Vulkan 1.2 or VK_EXT_descriptor_indexing extension

// Include bindless.glsl in shaders to use bindless rendering

void stromboliBindlessInit(StromboliContext* context);
void stromboliBindlessShutdown(StromboliContext* context);

u32 stromboliBindlessBindBuffer(StromboliBuffer* buffer);
u32 stromboliBindlessBindImage(StromboliImage* image);
void stromboliBindlessBindDescriptorSet(VkCommandBuffer commandBuffer, u32 frameIndex);

#endif // STROMBOLI_BINDLESS_H