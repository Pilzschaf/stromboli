#include <stromboli/stromboli.h>
#include <grounded/memory/grounded_arena.h>
#include <grounded/file/grounded_file.h>

#include <spirv_reflect.h>

struct ShaderDescriptorSetInfo {
    VkDescriptorType descriptorTypes[32];
    u32 descriptorCounts[32];
    VkShaderStageFlagBits stages[32];
    u32 descriptorMask;
    VkDescriptorBindingFlags flags[32];
};

typedef struct ShaderInfo {
    VkShaderStageFlagBits stage;
    VkPushConstantRange range;
    struct ShaderDescriptorSetInfo descriptorSets[4];
    //StromboliSpecializationConstant constants[4]; //TODO: Current maximum of 4

    // Options that are only filled out depending on shader type
    VkPipelineVertexInputStateCreateInfo vertexInputCreateInfo;
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
    if(shaderInfo->stage == VK_SHADER_STAGE_VERTEX_BIT) {
        // Vertex input description
        u32 inputVariableCount = 0;
        error = spvReflectEnumerateInputVariables(&reflectModule, &inputVariableCount, 0);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        //TODO: Could use scratch for this allocation
        SpvReflectInterfaceVariable** inputVariables = ARENA_PUSH_ARRAY(arena, inputVariableCount, SpvReflectInterfaceVariable*);
        error = spvReflectEnumerateInputVariables(&reflectModule, &inputVariableCount, inputVariables);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        
        shaderInfo->vertexInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkVertexInputAttributeDescription* vertexAttributes = 0;
        VkVertexInputBindingDescription* vertexInputBinding = 0;
        vertexAttributes = ARENA_PUSH_ARRAY(arena, inputVariableCount, VkVertexInputAttributeDescription);
        vertexInputBinding = ARENA_PUSH_ARRAY(arena, 1, VkVertexInputBindingDescription);
        
        u32 currentOffset = 0;
        u32 currentAttributeIndex = 0;
        for(u32 j = 0; j < inputVariableCount; ++j) {
            // built_in was signed before but now seems to be unsigneed (at least on linux) so we also compare to SpvBuiltInMax and assume that no valid value lies above that constant
            if(inputVariables[j]->built_in >= 0 && inputVariables[j]->built_in < SpvBuiltInMax) {
                // We don't have to fill out builtins
                continue;
            }
            vertexAttributes[currentAttributeIndex] = (VkVertexInputAttributeDescription){0};
            vertexAttributes[currentAttributeIndex].location = inputVariables[j]->location;
            vertexAttributes[currentAttributeIndex].binding = 0;
            vertexAttributes[currentAttributeIndex].format = (VkFormat)inputVariables[j]->format;
            vertexAttributes[currentAttributeIndex].offset = currentOffset;
            currentAttributeIndex++;
            
            currentOffset += (inputVariables[j]->numeric.scalar.width / 8) * inputVariables[j]->numeric.vector.component_count;
        }

        if(currentAttributeIndex > 0) {
            vertexInputBinding->binding = 0;
            vertexInputBinding->stride = currentOffset;
            vertexInputBinding->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Input variables are vertex attributes
            shaderInfo->vertexInputCreateInfo.vertexBindingDescriptionCount = 1;
            shaderInfo->vertexInputCreateInfo.pVertexBindingDescriptions = vertexInputBinding;
            shaderInfo->vertexInputCreateInfo.vertexAttributeDescriptionCount = currentAttributeIndex;
            shaderInfo->vertexInputCreateInfo.pVertexAttributeDescriptions = vertexAttributes;
        }
    } else if(shaderInfo->stage == VK_SHADER_STAGE_COMPUTE_BIT) {
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
                if(binding->count == 0) {
                    shaderInfo->descriptorSets[setIndex].flags[binding->binding] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                    //shaderInfo->descriptorSets[setIndex].flags[binding->binding] |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
                    shaderInfo->descriptorSets[setIndex].flags[binding->binding] |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                    shaderInfo->descriptorSets[setIndex].descriptorCounts[binding->binding] = 32768;
                }

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

    /*{ // Specialization constants
        u32 numSpecializationConstants = 0;
        error = spvReflectEnumerateSpecializationConstants(&reflectModule, &numSpecializationConstants, 0);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        SpvReflectSpecializationConstant* specializationConstants = ARENA_PUSH_ARRAY_NO_CLEAR(arena, numSpecializationConstants, SpvReflectSpecializationConstant);
        error = spvReflectEnumerateSpecializationConstants(&reflectModule, &numSpecializationConstants, &specializationConstants);
        ASSERT(error == SPV_REFLECT_RESULT_SUCCESS);
        for(u32 i = 0; i < numSpecializationConstants; ++i) {
            String8 name = str8FromCstr(specializationConstants[i].name);
            u32 index = specializationConstants[i].constant_id;
            ASSERT(index < ARRAY_COUNT(shaderInfo->constants));
            shaderInfo->constants[index].name = str8Copy(arena, name);
        }
    }*/

    spvReflectDestroyShaderModule(&reflectModule);
    STROMBOLI_NAME_OBJECT_EXPLICIT(context, result, VK_OBJECT_TYPE_SHADER_MODULE, str8GetCstr(arena, filename));

    return result;
}

static VkDescriptorSetLayout createSetLayout(StromboliContext* context, struct ShaderDescriptorSetInfo* shaderDescriptorInfo) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    u32 currentBindingIndex = 0;
    VkDescriptorSetLayoutBinding bindings[32]; // 32 is worst case actually because of our 32 binding limitation from above

    VkDescriptorSetLayoutBindingFlagsCreateInfo layoutFlags = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    layoutFlags.pBindingFlags = ARENA_PUSH_ARRAY(scratch, 32, VkDescriptorBindingFlags);
    bool containsLayoutFlags = false;

    for(u32 i = 0; i < 32; ++i) {
        if(shaderDescriptorInfo->descriptorMask & ((u32)1 << i)) {
            VkDescriptorSetLayoutBinding* binding = bindings + currentBindingIndex;
            *binding = (VkDescriptorSetLayoutBinding){0};
            binding->binding = i;
            binding->descriptorType = shaderDescriptorInfo->descriptorTypes[i];
            binding->descriptorCount = shaderDescriptorInfo->descriptorCounts[i];
            binding->stageFlags = shaderDescriptorInfo->stages[i];
            if(shaderDescriptorInfo->flags[i]) {
                ((VkDescriptorBindingFlags*)layoutFlags.pBindingFlags)[i] = shaderDescriptorInfo->flags[i];
                containsLayoutFlags = true;                
            }
            ++currentBindingIndex;
        }
    }

    VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.flags = 0; // VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR
    createInfo.bindingCount = currentBindingIndex;
    createInfo.pBindings = bindings;
    if(containsLayoutFlags) {
        layoutFlags.bindingCount = currentBindingIndex;
        createInfo.pNext = &layoutFlags;
        //TODO: Check which flags are actually set
        //createInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    VkDescriptorSetLayout result = 0;
    vkCreateDescriptorSetLayout(context->device, &createInfo, 0, &result);

    arenaEndTemp(temp);
    return result;
}

ShaderInfo combineShaderInfos(u32 shaderInfoCount, ShaderInfo* shaderInfos) {
    // We reuse the ShaderInfo type here. ShaderInfo.stage should be ignored
    ShaderInfo result = {0};

    for(u32 i = 0; i < shaderInfoCount; ++i) {
        ShaderInfo* info = &shaderInfos[i];

        // Vertex input
        if(info->stage == VK_SHADER_STAGE_VERTEX_BIT) {
            memcpy(&result.vertexInputCreateInfo, &info->vertexInputCreateInfo, sizeof(info->vertexInputCreateInfo));
        }
        
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
                        result.descriptorSets[setIndex].flags[j] |= info->descriptorSets[setIndex].flags[j];
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
        if(descriptorLayouts[i]) {
            // Layout has been submitted from outside so skip this set and use existing layout
            continue;
        }
        if(shaderInfo->descriptorSets[i].descriptorMask) {
            descriptorLayouts[i] = createSetLayout(context, &shaderInfo->descriptorSets[i]);
            descriptorUpdateTemplates[i] = createDescriptorUpdateTemplate(context, &shaderInfo->descriptorSets[i], descriptorLayouts[i], i, bindPoint);
        } else {
            // We have to create a stub descriptor set layout
            VkDescriptorSetLayout layout;
            VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            vkCreateDescriptorSetLayout(context->device, &createInfo, 0, &layout);
            descriptorLayouts[i] = layout;
            descriptorUpdateTemplates[i] = (VkDescriptorUpdateTemplate)(1); // Special value indicating that we have to destroy this layout
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

StromboliPipeline stromboliPipelineCreateCompute(StromboliContext* context, struct StromboliComputePipelineParameters* parameters) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    ShaderInfo shaderInfo = {0};
    VkShaderModule shaderModule = createShaderModule(context, scratch, parameters->filename, &shaderInfo);

    VkPipelineShaderStageCreateInfo shaderStage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    shaderStage.pSpecializationInfo = 0;

    VkDescriptorSetLayout descriptorLayouts[4];
    MEMORY_COPY_ARRAY(descriptorLayouts, parameters->setLayotus);
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

    // SpecializationConstants
    VkSpecializationInfo specialization;
    VkSpecializationInfo* specializationPointer = 0;
    if(parameters->constants && parameters->constantsCount) {
        VkSpecializationMapEntry* specializationEntries = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, parameters->constantsCount, VkSpecializationMapEntry);
        for(u32 i = 0; i < parameters->constantsCount; ++i) {
            s32 index = -1;
            //TODO: Once spirv reflect supports spec constants we can improve this by actually looking at the shader spec constant defines. We simply use i as index for now
            /*for(u32 j = 0; j < ARRAY_COUNT(combinedInfo.constants); ++j) {
                if(combinedInfo.constants[j].name.size) {
                    if(str8Compare(combinedInfo.constants[j].name, parameters->constants[i].name)) {
                        index = j;
                        break;
                    }
                }
            }*/
            index = i;
            if(index >= 0) {
                specializationEntries[index].constantID = i;
                specializationEntries[index].offset = i * 4;
                specializationEntries[index].size = sizeof(int);
            } else {
                // Not found
            }
        }

        specialization = (VkSpecializationInfo){
            .mapEntryCount = parameters->constantsCount,
            .pMapEntries = specializationEntries,
            .dataSize = sizeof(int),
            .pData = &parameters->constants,
        };
        specializationPointer = &specialization;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].module = vertexShaderModule;
    shaderStages[0].pName = "main";
    shaderStages[0].stage = vertexShaderInfo.stage;
    shaderStages[0].pSpecializationInfo = specializationPointer;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].module = fragmentShaderModule;
    shaderStages[1].pName = "main";
    shaderStages[1].stage = fragmentShaderInfo.stage;
    shaderStages[1].pSpecializationInfo = specializationPointer;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    if(parameters->primitiveMode == STROMBOLI_PRIMITVE_MODE_LINE_LIST) {
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    } else {
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1; // Still used
    //viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1; // Still used
    //viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    if(parameters->wireframe) {
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    } else {
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    }
    rasterizer.lineWidth = 1.0f;
    if(parameters->cullMode == STROMBOLI_CULL_MODE_BACK) {
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    } else if(parameters->cullMode == STROMBOLI_CULL_MODE_FRONT) {
        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    } else {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.sampleShadingEnable = VK_FALSE;
    if(parameters->multisampleCount) {
        multisampling.rasterizationSamples = parameters->multisampleCount;
    } else {
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    }

    VkPipelineDepthStencilStateCreateInfo depthStencilState = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencilState.depthTestEnable = parameters->depthTest;
    depthStencilState.depthWriteEnable = parameters->depthWrite;
    if(parameters->reverseZ) {
        depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER;
    } else {
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
    }
    if(parameters->depthWrite && !parameters->depthTest) {
        // This must be emulated by setting depthCompareOp to ALWAYS
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    }
    depthStencilState.minDepthBounds = 0.0f;
    depthStencilState.maxDepthBounds = 1.0f;

    u32 attachmentCount = 1 + parameters->additionalAttachmentCount;
    VkPipelineColorBlendAttachmentState* colorBlendAttachments = ARENA_PUSH_ARRAY(scratch, attachmentCount, VkPipelineColorBlendAttachmentState);
    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = parameters->enableBlending ? VK_TRUE : VK_FALSE; // Blending enable
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    for(u32 i = 1; i < attachmentCount; ++i) {
        colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE; // Blending enable
        colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo colorBlending = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = attachmentCount;
    colorBlending.pAttachments = colorBlendAttachments;

    // Available are scissor, line width, blend constants, depth bounds, stencil etc. See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkDynamicState.html
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = ARRAY_COUNT(dynamicStates);
    dynamicState.pDynamicStates = dynamicStates;

    VkDescriptorSetLayout descriptorLayouts[4] = {0};
    VkDescriptorUpdateTemplate descriptorUpdateTemplates[4] = {0};
    VkPipelineLayout pipelineLayout = createPipelineLayout(context, &combinedInfo, descriptorLayouts, descriptorUpdateTemplates, VK_PIPELINE_BIND_POINT_GRAPHICS);

    VkPipeline pipeline;
    {
        VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        createInfo.flags = 0;
        createInfo.stageCount = ARRAY_COUNT(shaderStages);
        createInfo.pStages = shaderStages;
        createInfo.pVertexInputState = &combinedInfo.vertexInputCreateInfo;
        createInfo.pTessellationState = 0;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterizer;
        createInfo.pMultisampleState = &multisampling;
        createInfo.pDepthStencilState = &depthStencilState;
        createInfo.pColorBlendState = &colorBlending;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = pipelineLayout;

        // VK_KHR_dynamic_rendering
        VkPipelineRenderingCreateInfo pipelineRenderingInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        if(!parameters->renderPass) {
            pipelineRenderingInfo.colorAttachmentCount = 1;
            pipelineRenderingInfo.pColorAttachmentFormats = &parameters->framebufferFormat;
            if(parameters->depthFormat != VK_FORMAT_UNDEFINED) {
                pipelineRenderingInfo.depthAttachmentFormat = parameters->depthFormat;
            }
            //pipelineRnderingInfo.stencilAttachmentFormat = ;
            createInfo.pNext = &pipelineRenderingInfo;
        } else {
            ASSERT(parameters->renderPass);
            createInfo.renderPass = parameters->renderPass;
            createInfo.subpass = parameters->subpassIndex;
        }

        vkCreateGraphicsPipelines(context->device, 0, 1, &createInfo, 0, &pipeline);
    }

    // Shader module can be destroyed after pipeline creation
    vkDestroyShaderModule(context->device, vertexShaderModule, 0);
    vkDestroyShaderModule(context->device, fragmentShaderModule, 0);

    StromboliPipeline result = {0};
    result.type = STROMBOLI_PIPELINE_TYPE_GRAPHICS,
    result.pipeline = pipeline,
    result.layout = pipelineLayout;
    for(u32 i = 0; i < 4; ++i) {
        result.descriptorLayouts[i] = descriptorLayouts[i];
        result.updateTemplates[i] = descriptorUpdateTemplates[i];
    }

    arenaEndTemp(temp);
    return result;
}

StromboliPipeline createRaytracingPipeline(StromboliContext* context, struct StromboliRaytracingPipelineParameters* parameters) {
    //TRACY_ZONE_HELPER(createRaytracingPipeline);

    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    u32 totalShaderCount = 1; // We always have a raygen shader
    u32 totalGroupCount = 1;
    //if(parameters->missShaderCount) totalGroupCount++;
    totalGroupCount += parameters->missShaderCount;
    totalShaderCount += parameters->missShaderCount;
    //if(parameters->hitShaderCount) totalGroupCount++;
    totalGroupCount += parameters->hitShaderCount;
    totalShaderCount += parameters->hitShaderCount;
    totalShaderCount += parameters->intersectionShaderCount;
    
    ShaderInfo* shaderInfos = ARENA_PUSH_ARRAY(scratch, totalShaderCount, ShaderInfo);
    VkShaderModule* shaderModules = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, totalShaderCount, VkShaderModule);
    VkPipelineShaderStageCreateInfo* stages = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, totalShaderCount, VkPipelineShaderStageCreateInfo);
    VkRayTracingShaderGroupCreateInfoKHR* groups = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, totalGroupCount, VkRayTracingShaderGroupCreateInfoKHR);
    
    u32 currentShaderIndex = 0;
    shaderModules[currentShaderIndex] = createShaderModule(context, scratch, str8FromCstr(parameters->raygenShaderFilename), &shaderInfos[currentShaderIndex]);
    currentShaderIndex++;
    for(u32 i = 0; i < parameters->missShaderCount; ++i) {
        shaderModules[currentShaderIndex] = createShaderModule(context, scratch, str8FromCstr(parameters->missShaderFilenames[i]), &shaderInfos[currentShaderIndex]);
        currentShaderIndex++;
    }
    for(u32 i = 0; i < parameters->hitShaderCount; ++i) {
        shaderModules[currentShaderIndex] = createShaderModule(context, scratch, str8FromCstr(parameters->hitShaderFilenames[i]), &shaderInfos[currentShaderIndex]);
        currentShaderIndex++;
    }
    u32 firstIntersectionShaderIndex = currentShaderIndex;
    for(u32 i = 0; i < parameters->intersectionShaderCount; ++i) {
        shaderModules[currentShaderIndex] = createShaderModule(context, scratch, str8FromCstr(parameters->intersectionShaders[i].filename), &shaderInfos[currentShaderIndex]);
        currentShaderIndex++;
    }
    ASSERT(currentShaderIndex == totalShaderCount);

    // Merge the shader infos into a single combined info
    ShaderInfo combinedInfo = combineShaderInfos(totalShaderCount, shaderInfos);

    for(u32 i = 0; i < totalShaderCount; ++i) {
        stages[i] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[i].stage = shaderInfos[i].stage;
        stages[i].module = shaderModules[i];
        stages[i].pName = "main";
    }
    
    u32 currentGroupIndex = 0;
    currentShaderIndex = 0;
    // Raygen
    groups[currentGroupIndex] = (VkRayTracingShaderGroupCreateInfoKHR){VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    groups[currentGroupIndex].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[currentGroupIndex].generalShader = currentShaderIndex++;
    groups[currentGroupIndex].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[currentGroupIndex].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[currentGroupIndex].intersectionShader = VK_SHADER_UNUSED_KHR;
    currentGroupIndex++;

    // Miss
    for(u32 i = 0; i < parameters->missShaderCount; ++i) {
        groups[currentGroupIndex] = (VkRayTracingShaderGroupCreateInfoKHR){VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        groups[currentGroupIndex].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[currentGroupIndex].generalShader = currentShaderIndex++;
        groups[currentGroupIndex].closestHitShader = VK_SHADER_UNUSED_KHR;
        groups[currentGroupIndex].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[currentGroupIndex].intersectionShader = VK_SHADER_UNUSED_KHR;
        currentGroupIndex++;
    }

    // Closest hit
    for(u32 i = 0; i < parameters->hitShaderCount; ++i) {
        groups[currentGroupIndex] = (VkRayTracingShaderGroupCreateInfoKHR){VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        groups[currentGroupIndex].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[currentGroupIndex].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        groups[currentGroupIndex].generalShader = VK_SHADER_UNUSED_KHR;
        groups[currentGroupIndex].closestHitShader = currentShaderIndex++;
        groups[currentGroupIndex].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[currentGroupIndex].intersectionShader = VK_SHADER_UNUSED_KHR;
        for(u32 j = 0; j < parameters->intersectionShaderCount; ++j) {
            if(parameters->intersectionShaders[j].matchingHitShaderIndex == i) {
                // There should not be multiple intersection shaders that map to the same closest hit shader
                ASSERT(groups[currentGroupIndex].intersectionShader == VK_SHADER_UNUSED_KHR);
                groups[currentGroupIndex].intersectionShader = firstIntersectionShaderIndex + j;
            }
        }
        currentGroupIndex++;
    }

    ASSERT(currentGroupIndex == totalGroupCount);
    ASSERT(currentShaderIndex == totalShaderCount - parameters->intersectionShaderCount);
    
    VkDescriptorSetLayout descriptorLayouts[4] = {0};
    VkDescriptorUpdateTemplate descriptorUpdateTemplates[4] = {0};
    VkPipelineLayout pipelineLayout = createPipelineLayout(context, &combinedInfo, descriptorLayouts, descriptorUpdateTemplates, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.flags = 0;
    createInfo.stageCount = totalShaderCount;
    createInfo.pStages = stages;
    createInfo.groupCount = totalGroupCount;
    createInfo.pGroups = groups;
    createInfo.maxPipelineRayRecursionDepth = 1; // Depth of call tree
    createInfo.layout = pipelineLayout;
    
    VkPipeline pipeline;
    vkCreateRayTracingPipelinesKHR(context->device, 0, 0, 1, &createInfo, 0, &pipeline);

    // Shader module can be destroyed after pipeline creation
    for(u32 i = 0; i < totalShaderCount; ++i) {
        vkDestroyShaderModule(context->device, shaderModules[i], 0);
    }

    StromboliPipeline result = {0};
    result.type = STROMBOLI_PIPELINE_TYPE_RAYTRACING;
    result.pipeline = pipeline;
    result.layout = pipelineLayout;
    for(u32 i = 0; i < 4; ++i) {
        result.descriptorLayouts[i] = descriptorLayouts[i];
        result.updateTemplates[i] = descriptorUpdateTemplates[i];
    }

    //TODO: Correct the values here so they match the given parameters 
    // Create SBT buffer
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 otherProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &rayTracingProperties};
    vkGetPhysicalDeviceProperties2(context->physicalDevice, &otherProperties);
    const VkDeviceSize sbtHandleSize = rayTracingProperties.shaderGroupHandleSize;
    const VkDeviceSize sbtBaseAlignment = rayTracingProperties.shaderGroupBaseAlignment;
    const VkDeviceSize sbtHandleAlignment = rayTracingProperties.shaderGroupHandleAlignment;
    
    const VkDeviceSize sbtStride = ALIGN_UP_POW2(sbtHandleSize, sbtHandleAlignment);
    ASSERT(sbtStride <= rayTracingProperties.maxShaderGroupStride);

    // Lay out regions
    result.raytracing.sbtRayGenRegion.stride = ALIGN_UP_POW2(sbtStride, sbtBaseAlignment);
    result.raytracing.sbtRayGenRegion.size = result.raytracing.sbtRayGenRegion.stride;
    result.raytracing.sbtMissRegion.stride = sbtStride;
    result.raytracing.sbtMissRegion.size = ALIGN_UP_POW2(parameters->missShaderCount * sbtStride, sbtBaseAlignment);
    result.raytracing.sbtHitRegion.stride = sbtStride;
    result.raytracing.sbtHitRegion.size = ALIGN_UP_POW2(parameters->hitShaderCount * sbtStride, sbtBaseAlignment);
    result.raytracing.sbtCallableRegion.size = 0;

    u32 sbtHandleCount = 1 + parameters->missShaderCount + parameters->hitShaderCount;
    u32 cpuDataSize = (u32)(sbtHandleSize * sbtHandleCount);
    u8* cpuShaderHandleStorage = ARENA_PUSH_ARRAY(scratch, cpuDataSize, u8);
    vkGetRayTracingShaderGroupHandlesKHR(context->device, result.pipeline, 0, sbtHandleCount, cpuDataSize, cpuShaderHandleStorage);
    
    u32 sbtSize = (u32)(result.raytracing.sbtRayGenRegion.size + result.raytracing.sbtMissRegion.size + result.raytracing.sbtHitRegion.size + result.raytracing.sbtCallableRegion.size);
    result.raytracing.sbtBuffer = stromboliCreateBuffer(context, sbtSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    // Setup device addresses for each sbt entry
    const VkDeviceAddress sbtStartAddress = getBufferDeviceAddress(context, &result.raytracing.sbtBuffer);
    result.raytracing.sbtRayGenRegion.deviceAddress = sbtStartAddress;
    result.raytracing.sbtMissRegion.deviceAddress = sbtStartAddress + result.raytracing.sbtRayGenRegion.size;
    result.raytracing.sbtHitRegion.deviceAddress = sbtStartAddress + result.raytracing.sbtRayGenRegion.size + result.raytracing.sbtMissRegion.size;
    result.raytracing.sbtCallableRegion.deviceAddress = 0;

    // Map sbt buffer and write in the handles
    u8* mappedSBT = (u8*) result.raytracing.sbtBuffer.mapped;
    u32 handleIndex = 0;
    // Raygen
    memcpy(mappedSBT, cpuShaderHandleStorage, sbtHandleSize);
    handleIndex++;
    // Miss
    mappedSBT += result.raytracing.sbtRayGenRegion.size;
    for(u32 i = 0; i < parameters->missShaderCount; ++i) {
        memcpy(mappedSBT, cpuShaderHandleStorage + sbtHandleSize * handleIndex, sbtHandleSize);
        handleIndex++;
        mappedSBT += result.raytracing.sbtMissRegion.stride;
    }
    // Hit
    mappedSBT = (u8*) result.raytracing.sbtBuffer.mapped;
    mappedSBT += result.raytracing.sbtRayGenRegion.size + result.raytracing.sbtMissRegion.size;
    for(u32 i = 0; i < parameters->missShaderCount; ++i) {
        memcpy(mappedSBT, cpuShaderHandleStorage + sbtHandleSize * handleIndex, sbtHandleSize);
        handleIndex++;
        mappedSBT += result.raytracing.sbtHitRegion.stride;
    }

    /*for(size_t groupIndex = 0; groupIndex < totalGroupCount; groupIndex++) {
        memcpy(&mappedSBT[groupIndex * sbtStride], &cpuShaderHandleStorage[groupIndex * sbtHandleSize], sbtHandleSize);
    }*/

    arenaEndTemp(temp);
    return result;
}

void stromboliPipelineDestroy(StromboliContext* context, StromboliPipeline* pipeline) {
    for(u32 i = 0; i < ARRAY_COUNT(pipeline->descriptorLayouts); ++i) {
        // We assume here that we created the layout ourselves, when an update template exists for it!
        if(pipeline->updateTemplates[i]) {
            vkDestroyDescriptorSetLayout(context->device, pipeline->descriptorLayouts[i], 0);
            if(pipeline->updateTemplates[i] != (VkDescriptorUpdateTemplate)1) {
                vkDestroyDescriptorUpdateTemplateKHR(context->device, pipeline->updateTemplates[i], 0);
            }
        }
    }
    vkDestroyPipelineLayout(context->device, pipeline->layout, 0);
    vkDestroyPipeline(context->device, pipeline->pipeline, 0);
}