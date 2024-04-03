#include <stromboli/stromboli.h>
#include <grounded/memory/grounded_arena.h>
#include <grounded/file/grounded_file.h>

#include <spirv_reflect.h>

struct ShaderDescriptorSetInfo {
    VkDescriptorType descriptorTypes[32];
    u32 descriptorCounts[32];
    VkShaderStageFlagBits stages[32];
    u32 descriptorMask;
    //VkDescriptorBindingFlags flags[32];
};

typedef struct ShaderInfo {
    VkShaderStageFlagBits stage;
    VkPushConstantRange range;
    struct ShaderDescriptorSetInfo descriptorSets[4];

    // Options that are only filled out depending on shader type
    u32 computeWorkgroupWidth;
    u32 computeWorkgroupHeight;
    u32 computeWorkgroupDepth;
} ShaderInfo;

// Only returns true if it is a real substring eg. length(s) > length(searchPattern)
static inline bool startsWith(const char* s, const char* searchPattern) {
    while(*s != '\0') {
        if(*searchPattern == '\0') return true;
        if(*s != *searchPattern) {
            return false;
        }
        ++s;
        ++searchPattern;
    }
    return false;
}

static VkShaderModule createShaderModule(StromboliContext* context, MemoryArena* arena, String8 filename, ShaderInfo* shaderInfo) {
    u64 dataSize = 0;
    u8* data = groundedReadFileImmutable(filename, &dataSize);
    ASSERT((dataSize % 4) == 0);

    VkShaderModule result;
    {
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = dataSize;
        createInfo.pCode = (u32*)data;
        vkCreateShaderModule(context->device, &createInfo, 0, &result);
    }

    SpvReflectShaderModule reflectModule;
    SpvReflectResult error = spvReflectCreateShaderModule(dataSize, data, &reflectModule);
    ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);

    groundedFreeFileImmutable(data, dataSize);
    data = 0;
    dataSize = 0;

    shaderInfo->stage = (VkShaderStageFlagBits)reflectModule.shader_stage;
    if(shaderInfo->stage == VK_SHADER_STAGE_COMPUTE_BIT) {
        const SpvReflectEntryPoint* entryPoint = spvReflectGetEntryPoint(&reflectModule, "main");
        if(entryPoint) {
            shaderInfo->computeWorkgroupWidth = entryPoint->local_size.x;
            shaderInfo->computeWorkgroupHeight = entryPoint->local_size.y;
            shaderInfo->computeWorkgroupDepth = entryPoint->local_size.z;
        }
    }

    { // Descriptor Sets
        u32 descriptorSetCount = 0;
        error = spvReflectEnumerateDescriptorSets(&reflectModule, &descriptorSetCount, 0);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        SpvReflectDescriptorSet** descriptorSets = ARENA_PUSH_ARRAY_NO_CLEAR(arena, descriptorSetCount, SpvReflectDescriptorSet*);
        error = spvReflectEnumerateDescriptorSets(&reflectModule, &descriptorSetCount, descriptorSets);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);

        ASSERT(descriptorSetCount <= 4);
        for(u32 j = 0; j < descriptorSetCount; ++j) {
            for(u32 i = 0; i < descriptorSets[j]->binding_count; ++i) {
                SpvReflectDescriptorBinding* binding = descriptorSets[j]->bindings[i];
                u32 setIndex = descriptorSets[j]->set;
            
                // Larger binding numbers are not supported here
                ASSERT(binding->binding < 32);
                ASSERT(shaderInfo->descriptorSets[setIndex].descriptorTypes[binding->binding] == 0 || shaderInfo->descriptorSets[setIndex].descriptorTypes[binding->binding] == (VkDescriptorType)binding->descriptor_type);
                shaderInfo->descriptorSets[setIndex].descriptorTypes[binding->binding] = (VkDescriptorType)binding->descriptor_type;
                shaderInfo->descriptorSets[setIndex].descriptorCounts[binding->binding] = binding->count;
                ASSERT(binding->count > 0);

                // Binding name is the instance name and not the type name!
                if(startsWith(binding->name, "dynamic")) {
                    if(shaderInfo->descriptorSets[setIndex].descriptorTypes[binding->binding] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                        shaderInfo->descriptorSets[setIndex].descriptorTypes[binding->binding] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    } else {
                        GROUNDED_LOG_WARNING("'dynamic' instance name for non uniform block.");
                    }
                }

                shaderInfo->descriptorSets[setIndex].stages[binding->binding] = shaderInfo->stage;
                shaderInfo->descriptorSets[setIndex].descriptorMask |= 1 << binding->binding;
            }
        }
    }

    { // Push constants
        u32 numPushConstantBlocks = 0;
        error = spvReflectEnumeratePushConstantBlocks(&reflectModule, &numPushConstantBlocks, 0);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        SpvReflectBlockVariable* pushConstantBlocks = ARENA_PUSH_ARRAY_NO_CLEAR(arena, numPushConstantBlocks, SpvReflectBlockVariable);
        error = spvReflectEnumeratePushConstantBlocks(&reflectModule, &numPushConstantBlocks, &pushConstantBlocks);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        for(u32 i = 0; i < numPushConstantBlocks; ++i) {
            u32 size = pushConstantBlocks[i].offset + pushConstantBlocks[i].size;
            shaderInfo->range.size = MAX(size, shaderInfo->range.size);
            shaderInfo->range.stageFlags |= shaderInfo->stage;
        }
    }

    spvReflectDestroyShaderModule(&reflectModule);
    STROMBOLI_NAME_OBJECT_EXPLICIT(context, result, VK_OBJECT_TYPE_SHADER_MODULE, str8GetCstr(arena, filename));

    return result;
}

static VkDescriptorSetLayout createSetLayout(StromboliContext* context, struct ShaderDescriptorSetInfo* shaderDescriptorInfo) {
    u32 currentBindingIndex = 0;
    VkDescriptorSetLayoutBinding bindings[32]; // 32 is worst case actually because of our 32 binding limitation from above

    for(u32 i = 0; i < 32; ++i) {
        if(shaderDescriptorInfo->descriptorMask & ((u32)1 << i)) {
            VkDescriptorSetLayoutBinding* binding = bindings + currentBindingIndex;
            *binding = (VkDescriptorSetLayoutBinding){0};
            binding->binding = i;
            binding->descriptorType = shaderDescriptorInfo->descriptorTypes[i];
            binding->descriptorCount = shaderDescriptorInfo->descriptorCounts[i];
            binding->stageFlags = shaderDescriptorInfo->stages[i];
            ++currentBindingIndex;
        }
    }

    VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.flags = 0; // VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR
    createInfo.bindingCount = currentBindingIndex;
    createInfo.pBindings = bindings;

    VkDescriptorSetLayout result = 0;
    vkCreateDescriptorSetLayout(context->device, &createInfo, 0, &result);

    return result;
}

ShaderInfo combineShaderInfos(u32 shaderInfoCount, ShaderInfo* shaderInfos) {
    // We reuse the ShaderInfo type here. ShaderInfo.stage should be ignored
    ShaderInfo result = {0};

    for(u32 i = 0; i < shaderInfoCount; ++i) {
        ShaderInfo* info = &shaderInfos[i];

        // Vertex input
        ASSERT(false);
        /*if(info->stage == VK_SHADER_STAGE_VERTEX_BIT) {
            memcpy(&result.vertexInputCreateInfo, &info->vertexInputCreateInfo, sizeof(info->vertexInputCreateInfo));
        }*/
        
        // Descriptor sets
        for(u32 setIndex = 0; setIndex < 4; ++setIndex) {
            if(info->descriptorSets[setIndex].descriptorMask) {
                for(u32 j = 0; j < ARRAY_COUNT(info->descriptorSets[setIndex].descriptorTypes); ++j) {
                    if(info->descriptorSets[setIndex].descriptorMask & ((u32)1 << j)) {
                        // Check that type is the same if it is already set
                        ASSERT(result.descriptorSets[setIndex].descriptorTypes[j] == 0 \
                            || result.descriptorSets[setIndex].descriptorTypes[j] == info->descriptorSets[setIndex].descriptorTypes[j]);
                        
                        result.descriptorSets[setIndex].descriptorTypes[j] = info->descriptorSets[setIndex].descriptorTypes[j];
                        result.descriptorSets[setIndex].descriptorCounts[j] = info->descriptorSets[setIndex].descriptorCounts[j];
                        ASSERT(false);
                        //TODO: result.descriptorSets[setIndex].flags[j] |= info->descriptorSets[setIndex].flags[j];

                        result.descriptorSets[setIndex].stages[j] |= info->stage;
                        result.descriptorSets[setIndex].descriptorMask |= 1 << j;
                    }
                }
            }
        }

        // Push constants
        result.range.size = MAX(result.range.size, info->range.size);
        result.range.stageFlags |= info->stage;

        // Specialization constants
        //TODO:
        /*for(u32 j = 0; j < ARRAY_COUNT(result.constants); ++j) {
            if(shaderInfos[i].constants[j].name.size) {
                result.constants[j].name = shaderInfos[i].constants[j].name;
            }
        }*/
    }

    return result;
}

static VkDescriptorUpdateTemplate createDescriptorUpdateTemplate(StromboliContext* context, struct ShaderDescriptorSetInfo* shaderDescriptorInfo, VkDescriptorSetLayout layout, u32 setIndex, VkPipelineBindPoint bindPoint) {
    u32 currentEntryIndex = 0;
    u32 currentOffset = 0;
    VkDescriptorUpdateTemplateEntry updateTemplateEntries[32];

    for(u32 i = 0; i < 32; ++i) {
        if(shaderDescriptorInfo->descriptorMask & ((u32)1 << i)) {
            VkDescriptorUpdateTemplateEntry* templateEntry = updateTemplateEntries + currentEntryIndex;
            templateEntry->dstBinding = i;
            templateEntry->dstArrayElement = 0;
            templateEntry->descriptorCount = shaderDescriptorInfo->descriptorCounts[i];
            templateEntry->descriptorType = shaderDescriptorInfo->descriptorTypes[i];
            templateEntry->offset = sizeof(struct StromboliDescriptorInfo)*currentOffset; //TODO: Maybe set the offset based on i not currentEntryIndex?
            templateEntry->stride = sizeof(struct StromboliDescriptorInfo);

            ++currentEntryIndex;
            currentOffset += shaderDescriptorInfo->descriptorCounts[i];
        }
    }

    VkDescriptorUpdateTemplateCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
    createInfo.descriptorUpdateEntryCount = currentEntryIndex;
    createInfo.pDescriptorUpdateEntries = updateTemplateEntries;
    createInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    createInfo.descriptorSetLayout = layout;
    createInfo.set = setIndex;
    createInfo.pipelineBindPoint = bindPoint;
    
    VkDescriptorUpdateTemplate result;
    vkCreateDescriptorUpdateTemplateKHR(context->device, &createInfo, 0, &result);
    
    return result;
}

static VkPipelineLayout createPipelineLayout(StromboliContext* context, ShaderInfo* shaderInfo, VkDescriptorSetLayout* descriptorLayouts, VkDescriptorUpdateTemplate* descriptorUpdateTemplates, VkPipelineBindPoint bindPoint) {
    VkPipelineLayout result = 0;

    for(u32 i = 0; i < 4; ++i) {
        if(shaderInfo->descriptorSets[i].descriptorMask) {
            descriptorLayouts[i] = createSetLayout(context, &shaderInfo->descriptorSets[i]);
            descriptorUpdateTemplates[i] = createDescriptorUpdateTemplate(context, &shaderInfo->descriptorSets[i], descriptorLayouts[i], i, bindPoint);
        } else {
            // We have to create a stub descriptor set layout
            VkDescriptorSetLayout layout;
            VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            vkCreateDescriptorSetLayout(context->device, &createInfo, 0, &layout);
            descriptorLayouts[i] = layout;
        }
    }

    VkPipelineLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    createInfo.setLayoutCount = 4;
    createInfo.pSetLayouts = descriptorLayouts;
    createInfo.pushConstantRangeCount = shaderInfo->range.size > 0 ? 1 : 0;
    createInfo.pPushConstantRanges = &shaderInfo->range;
    vkCreatePipelineLayout(context->device, &createInfo, 0, &result);
    
    return result;
}

StromboliPipeline stromboliPipelineCreateCompute(StromboliContext* context, String8 filename) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    ShaderInfo shaderInfo = {0};
    VkShaderModule shaderModule = createShaderModule(context, scratch, filename, &shaderInfo);

    VkPipelineShaderStageCreateInfo shaderStage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    shaderStage.pSpecializationInfo = 0;

    VkDescriptorSetLayout descriptorLayouts[4] = {0};
    VkDescriptorUpdateTemplate descriptorUpdateTemplates[4] = {0};
    VkPipelineLayout pipelineLayout = createPipelineLayout(context, &shaderInfo, descriptorLayouts, descriptorUpdateTemplates, VK_PIPELINE_BIND_POINT_COMPUTE);

    VkPipeline pipeline;
    {
        VkComputePipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        createInfo.stage = shaderStage;
        createInfo.layout = pipelineLayout;
        vkCreateComputePipelines(context->device, 0, 1, &createInfo, 0, &pipeline);
    }
    // Shader module can be destroyed after pipeline creation
    vkDestroyShaderModule(context->device, shaderModule, 0);

    StromboliPipeline result = {0};
    result.type = STROMBOLI_PIPELINE_TYPE_COMPUTE,
    result.pipeline = pipeline,
    result.layout = pipelineLayout;
    for(u32 i = 0; i < 4; ++i) {
        result.descriptorLayouts[i] = descriptorLayouts[i];
        result.updateTemplates[i] = descriptorUpdateTemplates[i];
    }
    result.compute.workgroupWidth = shaderInfo.computeWorkgroupWidth;
    result.compute.workgroupHeight = shaderInfo.computeWorkgroupHeight;
    result.compute.workgroupDepth = shaderInfo.computeWorkgroupDepth;
    
    arenaEndTemp(temp);
    return result;
}

StromboliPipeline stromboliPipelineCreateGraphics(StromboliContext* context, struct StromboliGraphicsPipelineParameters* parameters) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    ShaderInfo vertexShaderInfo = {0};
    VkShaderModule vertexShaderModule = createShaderModule(context, scratch, parameters->vertexShaderFilename, &vertexShaderInfo);
    ShaderInfo fragmentShaderInfo = {0};
    VkShaderModule fragmentShaderModule = createShaderModule(context, scratch, parameters->fragmentShaderFilename, &fragmentShaderInfo);

    // Merge the shader infos into a single combined info
    ShaderInfo shaderInfos[] = {vertexShaderInfo, fragmentShaderInfo};
    ShaderInfo combinedInfo = combineShaderInfos(PASS_ARRAY(shaderInfos));

    VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].module = vertexShaderModule;
    shaderStages[0].pName = "main";
    shaderStages[0].stage = vertexShaderInfo.stage;
    //shaderStages[0].pSpecializationInfo = specializationPointer;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].module = fragmentShaderModule;
    shaderStages[1].pName = "main";
    shaderStages[1].stage = fragmentShaderInfo.stage;
    //shaderStages[1].pSpecializationInfo = specializationPointer;

    VkDescriptorSetLayout descriptorLayouts[4] = {0};
    VkDescriptorUpdateTemplate descriptorUpdateTemplates[4] = {0};
    VkPipelineLayout pipelineLayout = createPipelineLayout(context, &combinedInfo, descriptorLayouts, descriptorUpdateTemplates, VK_PIPELINE_BIND_POINT_GRAPHICS);

    VkPipeline pipeline;
    {
        VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        createInfo.layout = pipelineLayout;
        vkCreateGraphicsPipelines(context->device, 0, 1, &createInfo, 0, &pipeline);
    }

    StromboliPipeline result = {0};
    result.type = STROMBOLI_PIPELINE_TYPE_GRAPHICS,
    result.pipeline = pipeline,
    result.layout = pipelineLayout;

    arenaEndTemp(temp);
    return result;
}

void stromboliPipelineDestroy(StromboliContext* context, StromboliPipeline* pipeline) {
    for(u32 i = 0; i < ARRAY_COUNT(pipeline->descriptorLayouts); ++i) {
        vkDestroyDescriptorSetLayout(context->device, pipeline->descriptorLayouts[i], 0);
        if(pipeline->updateTemplates[i]) {
            vkDestroyDescriptorUpdateTemplateKHR(context->device, pipeline->updateTemplates[i], 0);
        }
    }
    vkDestroyPipelineLayout(context->device, pipeline->layout, 0);
    vkDestroyPipeline(context->device, pipeline->pipeline, 0);
}