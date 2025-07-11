#define VOLK_IMPLEMENTATION
#include <volk/volk.h>

#include <stromboli/stromboli.h>

#include <grounded/memory/grounded_arena.h>
#include <grounded/threading/grounded_threading.h>
#include <grounded/memory/grounded_memory.h>

#include <stdio.h>

VkBool32 VKAPI_CALL debugReportCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        printf("Vulkan Error: %s\n", callbackData->pMessage);
        return VK_FALSE;
	} else if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		printf("Vulkan Warning: %s\n", callbackData->pMessage);
        return VK_FALSE;
	} else {
        printf("Vulkan Info: %s\n", callbackData->pMessage);
        return VK_FALSE;
    }
	return VK_FALSE;
}

VkDebugUtilsMessengerEXT registerDebugCallback(VkInstance instance) {
	VkDebugUtilsMessengerCreateInfoEXT callbackInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	callbackInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	callbackInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
	callbackInfo.pfnUserCallback = debugReportCallback;

	VkDebugUtilsMessengerEXT callback = 0;
	vkCreateDebugUtilsMessengerEXT(instance, &callbackInfo, 0, &callback);

	return callback;
}

StromboliResult initVulkanInstance(StromboliContext* context, StromboliInitializationParameters* parameters) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    ASSERT(parameters);
    StromboliResult error = STROMBOLI_SUCCESS();
    VkResult result = VK_SUCCESS;

    // Check if volk is already loaded
    u32 volkInstanceVersion = volkGetInstanceVersion();
    if(!volkInstanceVersion) {
        result = volkInitialize();
        if(result != VK_SUCCESS) {
            error = STROMBOLI_MAKE_ERROR(STROMBOLI_VOLK_INITIALIZE_ERROR, "Failed to initialize volk");
        }
    }

    u32 apiVersion = parameters->vulkanApiVersion ? parameters->vulkanApiVersion : VK_API_VERSION_1_0;

    // Should be large enough to hold all layers we can enable
    const char* enabledLayers[2];
    u32 enabledLayerCount = 0;
    bool validationLayerEnabled = false;
    if(STROMBOLI_NO_ERROR(error) && parameters->enableValidation) {
        GROUNDED_LOG_INFO("Validation layer has been requested by the application");
        u32 availableLayerCount = 0;
        VkLayerProperties* availableLayers;
        bool foundValidationLayers = false;
        vkEnumerateInstanceLayerProperties(&availableLayerCount, 0);
        if(availableLayerCount > 0) {
            availableLayers = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, availableLayerCount, VkLayerProperties);
            if(availableLayers) {
                vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers);
                for(u32 i = 0; i < availableLayerCount; ++i) {
                    if(strcmp(availableLayers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                        u32 major = VK_API_VERSION_MAJOR(availableLayers[i].specVersion);
                        u32 minor = VK_API_VERSION_MAJOR(availableLayers[i].specVersion);
                        u32 patch = VK_API_VERSION_MAJOR(availableLayers[i].specVersion);
                        printf("Enabling Khronos validation layer. Spec version: %u.%u.%u\n", major, minor, patch);
                        foundValidationLayers = true;
                        break;
                    }
                }
            }
        }
        
        if(!foundValidationLayers) {
            GROUNDED_LOG_WARNING("Validation layer could not be found. Are the Vulkan validation layers / The Vulkan SDK installed?");
        } else {
            ASSERT(enabledLayerCount < ARRAY_COUNT(enabledLayers));
            enabledLayers[enabledLayerCount++] = "VK_LAYER_KHRONOS_validation";
            validationLayerEnabled = true;
        }
    }
    if(parameters->enableApiDump) {
        ASSERT(enabledLayerCount < ARRAY_COUNT(enabledLayers));
        enabledLayers[enabledLayerCount++] = "VK_LAYER_LUNARG_api_dump";
    }

    u32 requestedInstanceExtensionCount = parameters->additionalInstanceExtensionCount;
    const char** requestedInstanceExtensions = parameters->additionalInstanceExtensions;
    if(STROMBOLI_NO_ERROR(error) && parameters->platformGetRequiredNativeInstanceExtensions) {
        u32 count = 0;
        const char** platformInstanceExtensions = parameters->platformGetRequiredNativeInstanceExtensions(&count);
        if(count > 0 && platformInstanceExtensions) {
            const char** oldRequestedInstanceExtensions = requestedInstanceExtensions;
            requestedInstanceExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, requestedInstanceExtensionCount + count, const char*);
            if(requestedInstanceExtensionCount) {
                memcpy(requestedInstanceExtensions, oldRequestedInstanceExtensions, sizeof(const char*) * requestedInstanceExtensionCount);
            }
            memcpy(requestedInstanceExtensions + requestedInstanceExtensionCount, platformInstanceExtensions, sizeof(const char*) * count);
            requestedInstanceExtensionCount += count;
        }
    }

    // We always request VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME when we create a Vulkan 1.0 instance
    if(apiVersion < VK_API_VERSION_1_1) {
        const char** oldRequestedInstanceExtensions = requestedInstanceExtensions;
        requestedInstanceExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, requestedInstanceExtensionCount + 1, const char*);
        memcpy(requestedInstanceExtensions, oldRequestedInstanceExtensions, sizeof(const char*) * requestedInstanceExtensionCount);
        static const char* extension[] = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
        memcpy(requestedInstanceExtensions + requestedInstanceExtensionCount, extension, sizeof(const char*));
        requestedInstanceExtensionCount += 1;
    }

    // Check that both validation layer extensions are actually provided by the layer and enable if they are
    bool validationFeaturesAvailable = false;
    if(STROMBOLI_NO_ERROR(error) && validationLayerEnabled) {
        const char** oldRequestedInstanceExtensions = requestedInstanceExtensions;
        requestedInstanceExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, requestedInstanceExtensionCount + 2, const char*);
        memcpy(requestedInstanceExtensions, oldRequestedInstanceExtensions, sizeof(const char*) * requestedInstanceExtensionCount);
        u32 count = 0;
        vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &count, 0);
        VkExtensionProperties* availableValidationExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, count, VkExtensionProperties);
        vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &count, availableValidationExtensions);
        for(u32 i = 0; i < count; ++i) {
            if(strcmp(availableValidationExtensions[i].extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0) {
                requestedInstanceExtensions[requestedInstanceExtensionCount++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
                validationFeaturesAvailable = true;
            } else if(strcmp(availableValidationExtensions[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                requestedInstanceExtensions[requestedInstanceExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            }
        }
    }

    if(STROMBOLI_NO_ERROR(error)) { // Check if all requested instance extensions are available
        u32 availableInstanceExtensionCount = 0;
        vkEnumerateInstanceExtensionProperties(0, &availableInstanceExtensionCount, 0);
        VkExtensionProperties* availableInstanceExtensions = 0;
        if (availableInstanceExtensionCount > 0) {
            availableInstanceExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, availableInstanceExtensionCount, VkExtensionProperties);
            vkEnumerateInstanceExtensionProperties(0, &availableInstanceExtensionCount, availableInstanceExtensions);
            for (u32 i = 0; i < requestedInstanceExtensionCount; ++i) {
                bool found = false;
                for (u32 j = 0; j < availableInstanceExtensionCount; ++j) {
                    if (strcmp(availableInstanceExtensions[j].extensionName, requestedInstanceExtensions[i]) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if(strcmp(requestedInstanceExtensions[i], "VK_EXT_validation_features") == 0) {
                        // Ignore as this extension is only provided by the layer and not the instance directly
                    } else {
                        printf("Could not find vulkan instance extension %s\n", requestedInstanceExtensions[i]);
                        error = STROMBOLI_MAKE_ERROR(STROMBOLI_INSTANCE_CREATE_ERROR, "Could not find vulkan instance extension");
                    }
                }
            }
        }
    }

    VkValidationFeatureEnableEXT enabledValidationFeatures[8];
    u32 enabledValidationFeatureCount = 0;
    if(validationLayerEnabled && validationFeaturesAvailable) {
        if(parameters->enableBestPracticeWarning) {
            enabledValidationFeatures[enabledValidationFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT;
        }
        if(parameters->enableShaderDebugPrintf) {
            if(parameters->enableGpuAssistedValidation) {
                GROUNDED_LOG_WARNING("Can not activate shader debug printf at the same time as gpu assisted validation. Ignoring shader debug printf");
            } else {
                enabledValidationFeatures[enabledValidationFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
            }    
        }
        if(parameters->enableSynchronizationValidation) {
            enabledValidationFeatures[enabledValidationFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        }
        if(parameters->enableGpuAssistedValidation) {
            enabledValidationFeatures[enabledValidationFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
        }
        if(parameters->enableGpuReservedBindingSlot) {
            if(!parameters->enableGpuAssistedValidation) {
                GROUNDED_LOG_WARNING("GPU reserved binding slot is enabled without gpu assisted validation. This is not valid and is ignored");
            } else {
                enabledValidationFeatures[enabledValidationFeatureCount++] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT;
            }
        }
    }
    ASSERT(enabledValidationFeatureCount <= ARRAY_COUNT(enabledValidationFeatures));
    VkValidationFeaturesEXT validationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	validationFeatures.enabledValidationFeatureCount = enabledValidationFeatureCount;
	validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;

    if(STROMBOLI_NO_ERROR(error)) { // Init instance
        VkApplicationInfo applicationInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = str8GetCstrOrNull(scratch, parameters->applicationName);
        applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, parameters->applicationMajorVersion, parameters->applicationMinorVersion, parameters->applicationPatchVersion);
        applicationInfo.pEngineName = "Stromboli";
        applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        applicationInfo.apiVersion = apiVersion;

        VkInstanceCreateInfo createInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pNext = &validationFeatures;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledLayerCount = enabledLayerCount;
        createInfo.ppEnabledLayerNames = enabledLayers;
        createInfo.enabledExtensionCount = requestedInstanceExtensionCount;
        createInfo.ppEnabledExtensionNames = requestedInstanceExtensions;
        result = vkCreateInstance(&createInfo, 0, &context->instance);
        if(result != VK_SUCCESS) {
            error = STROMBOLI_MAKE_ERROR(STROMBOLI_INSTANCE_CREATE_ERROR, "Could not create vulkan instance. Make sure you are using the latest graphics driver");
        } else {
            context->apiVersion = applicationInfo.apiVersion;
            volkLoadInstanceOnly(context->instance);
            if(validationLayerEnabled) {
                context->debugCallback = registerDebugCallback(context->instance);
            }
        }
    }

    arenaEndTemp(temp);
    return error;
}

StromboliResult initVulkanDevice(StromboliContext* context, StromboliInitializationParameters* parameters) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);
    StromboliResult error = STROMBOLI_SUCCESS();

    // Already create the VkDeviceCreateInfo here so we can already build the pNextChain
    VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    void** pNextChain = (void**)&createInfo.pNext;

    // The maximum number of device extensions that can be added by the following code (excluding parameters->additionalDeviceExtensionCount)
    #define MAX_ADDED_DEVICE_EXTENSION_COUNT 64
    u32 requestedDeviceExtensionCount = parameters->additionalDeviceExtensionCount;
    const char** requestedDeviceExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, parameters->additionalDeviceExtensionCount + MAX_ADDED_DEVICE_EXTENSION_COUNT, const char*);
    if(requestedDeviceExtensionCount) {
        memcpy(requestedDeviceExtensions, parameters->additionalDeviceExtensions, parameters->additionalDeviceExtensionCount * sizeof(const char*));
    }

    // First check which extensions we need
    if(!parameters->disableSwapchain) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    if(parameters->calibratedTimestamps) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    }
    if(parameters->rayQuery) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
    }
    if(parameters->accelerationStructure) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    }
    if(parameters->rayTracingPipeline) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
    }
    if(parameters->dynamicRendering && context->apiVersion < VK_API_VERSION_1_3) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    }
    if(parameters->dynamicRenderingUnusedAttachments && context->apiVersion < VK_API_VERSION_1_3) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME;
    }
    if(parameters->synchronization2) { //Promoted to VK_API_VERSION_1_3
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
    }
    if(parameters->maintenance4 && context->apiVersion < VK_API_VERSION_1_3) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_MAINTENANCE_4_EXTENSION_NAME;
    }
    if(parameters->bufferDeviceAddress && context->apiVersion < VK_API_VERSION_1_2) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
    }
    if(parameters->scalarBlockLayout && context->apiVersion < VK_API_VERSION_1_2) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME;
    }
    if(parameters->nonUniformIndexingSampledImageArray && context->apiVersion < VK_API_VERSION_1_2) {
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
    }
    if(parameters->descriptorUpdateTemplate) { // Promoted to VK_API_VERSION_1_1
        requestedDeviceExtensions[requestedDeviceExtensionCount++] = VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME;
    }

    // Select physical device
    u32 numDevices = 0;
    VkResult result = vkEnumeratePhysicalDevices(context->instance, &numDevices, 0);
    if (numDevices == 0 || result != VK_SUCCESS) {
        error = STROMBOLI_MAKE_ERROR(STROMBOLI_NO_SUITABLE_GPUS, "Failed to find GPUs with Vulkan support!");
    } else {
        ArenaTempMemory temp = arenaBeginTemp(scratch);
        VkPhysicalDevice* physicalDevices = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, numDevices, VkPhysicalDevice);
        result = vkEnumeratePhysicalDevices(context->instance, &numDevices, physicalDevices);
        for(uint32_t i = 0; i < numDevices; ++i) {
            bool applicable = true;
            VkPhysicalDeviceProperties properties = {0};
            vkGetPhysicalDeviceProperties(physicalDevices[i], &properties);

            // Check if device extensions are available
            u32 availableDeviceExtensionCount = 0;
            vkEnumerateDeviceExtensionProperties(physicalDevices[i], 0, &availableDeviceExtensionCount, 0);
            VkExtensionProperties* availableDeviceExtensions = 0;
            if (availableDeviceExtensionCount > 0) {
                availableDeviceExtensions = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, availableDeviceExtensionCount, VkExtensionProperties);
                vkEnumerateDeviceExtensionProperties(physicalDevices[i], 0, &availableDeviceExtensionCount, availableDeviceExtensions);
                for (u32 i = 0; i < requestedDeviceExtensionCount; ++i) {
                    bool found = false;
                    for (u32 j = 0; j < availableDeviceExtensionCount; ++j) {
                        if (strcmp(availableDeviceExtensions[j].extensionName, requestedDeviceExtensions[i]) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        printf("Could not find device extension %s\n", requestedDeviceExtensions[i]);
                        applicable = false;
                        // Do not break here so we can list all missing requested device extensions
                        printf("GPU %s not used because extension %s is missing\n", properties.deviceName, requestedDeviceExtensions[i]);
                    }
                }
            }

            if(applicable) {
                context->physicalDevice = physicalDevices[i];
                vkGetPhysicalDeviceProperties(context->physicalDevice, &context->physicalDeviceProperties);
                printf("Selected GPU: %s\n", context->physicalDeviceProperties.deviceName);

                vkGetPhysicalDeviceFeatures(context->physicalDevice, &context->physicalDeviceFeatures);
                context->physicalDeviceLimits = context->physicalDeviceProperties.limits;
                break;
            }
        }
        if(!context->physicalDevice) {
            error = STROMBOLI_MAKE_ERROR(STROMBOLI_NO_SUITABLE_GPUS, "Could not find any GPU that supports all required features");
        }
        
        arenaEndTemp(temp);
    }

    if(STROMBOLI_NO_ERROR(error)) { // Init device
        ArenaTempMemory temp = arenaBeginTemp(scratch);

        // Queues
        u32 numQueueFamilies = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->physicalDevice, &numQueueFamilies, 0);
        VkQueueFamilyProperties* queueFamilies = ARENA_PUSH_ARRAY_NO_CLEAR(scratch, numQueueFamilies, VkQueueFamilyProperties);
        vkGetPhysicalDeviceQueueFamilyProperties(context->physicalDevice, &numQueueFamilies, queueFamilies);

        u32 graphicsQueueIndex = 0;
        u32 availableGraphicsQueueCount = 0;
        u32 computeQueueIndex = 0;
        u32 availableComputeQueueCount = 0;
        u32 transferQueueIndex = 0;
        u32 availableTransferQueueCount = 0;
        for (u32 i = 0; i < numQueueFamilies; ++i) {
            VkQueueFamilyProperties queueFamily = queueFamilies[i];
            if (queueFamily.queueCount > 0) {
                if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    graphicsQueueIndex = i;
                    availableGraphicsQueueCount = queueFamily.queueCount;
                } else if(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    computeQueueIndex = i;
                    availableComputeQueueCount = queueFamily.queueCount;
                } else if(queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT && !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    transferQueueIndex = i;
                    availableTransferQueueCount = queueFamily.queueCount;
                }
            }
        }

        float priorities[MAX_QUEUE_COUNT];
        for(u32 i = 0; i < MAX_QUEUE_COUNT; ++i) {
            priorities[i] = 1.0f;
        }
        VkDeviceQueueCreateInfo queueCreateInfos[4];
        u32 queueCreateInfoCount = 0;
        // Graphics queues
        queueCreateInfos[queueCreateInfoCount] = (VkDeviceQueueCreateInfo){ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueCreateInfos[queueCreateInfoCount].queueFamilyIndex = graphicsQueueIndex;
        queueCreateInfos[queueCreateInfoCount].queueCount = MIN(availableGraphicsQueueCount, parameters->additionalGraphicsQueueRequestCount+1);
        queueCreateInfos[queueCreateInfoCount].pQueuePriorities = priorities;
        queueCreateInfoCount++;

        // Compute queues
        if(parameters->computeQueueRequestCount > 0) {
            queueCreateInfos[queueCreateInfoCount] = (VkDeviceQueueCreateInfo){ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueCreateInfos[queueCreateInfoCount].queueFamilyIndex = computeQueueIndex;
            queueCreateInfos[queueCreateInfoCount].queueCount = MIN(availableComputeQueueCount, parameters->computeQueueRequestCount);
            queueCreateInfos[queueCreateInfoCount].pQueuePriorities = priorities;
            queueCreateInfoCount++;
        }

        // Transfer queues
        if(parameters->transferQueueRequestCount > 0) {
            queueCreateInfos[queueCreateInfoCount] = (VkDeviceQueueCreateInfo){ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueCreateInfos[queueCreateInfoCount].queueFamilyIndex = transferQueueIndex;
            queueCreateInfos[queueCreateInfoCount].queueCount = MIN(availableTransferQueueCount, parameters->transferQueueRequestCount);
            queueCreateInfos[queueCreateInfoCount].pQueuePriorities = priorities;
            queueCreateInfoCount++;
        }

        // Vulkan 1.0 features
        VkPhysicalDeviceFeatures enabledFeatures = {0};
        enabledFeatures.sampleRateShading = parameters->sampleRateShadingFeature;
        enabledFeatures.fillModeNonSolid = parameters->fillModeNonSolidFeature;
        enabledFeatures.fragmentStoresAndAtomics = parameters->fragmentStoresAndAtomicsFeature;
        enabledFeatures.independentBlend = parameters->independentBlendFeature;
        enabledFeatures.samplerAnisotropy = parameters->samplerAnisotropyFeature;
        enabledFeatures.depthClamp = parameters->depthClampFeature;

        createInfo.queueCreateInfoCount = queueCreateInfoCount;
        createInfo.pQueueCreateInfos = queueCreateInfos;
        createInfo.enabledExtensionCount = requestedDeviceExtensionCount;
        createInfo.ppEnabledExtensionNames = requestedDeviceExtensions;
        createInfo.pEnabledFeatures = &enabledFeatures;

        VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT dynamicRenderingUnusedAttachmentsFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT};

        if(context->apiVersion >= VK_API_VERSION_1_2) {
            features12.bufferDeviceAddress = parameters->bufferDeviceAddress;
            features12.scalarBlockLayout = parameters->scalarBlockLayout;
            features12.shaderSampledImageArrayNonUniformIndexing = parameters->nonUniformIndexingSampledImageArray;
            features12.runtimeDescriptorArray = parameters->runtimeDescriptorArray;
            features12.descriptorBindingVariableDescriptorCount = parameters->descriptorBindingVariableDescriptorCount;
            features12.descriptorBindingSampledImageUpdateAfterBind = parameters->descriptorBindingSampledImageUpdateAfterBind;
            features12.descriptorBindingUniformBufferUpdateAfterBind = parameters->descriptorBindingUniformBufferUpdateAfterBind;
            features12.descriptorBindingStorageBufferUpdateAfterBind = parameters->descriptorBindingStorageBufferUpdateAfterBind;
            features12.descriptorBindingStorageImageUpdateAfterBind = parameters->descriptorBindingStorageImageUpdateAfterBind;
            features12.descriptorBindingPartiallyBound = parameters->descriptorBindingPartiallyBound;
            *pNextChain = &features12;
            pNextChain = &features12.pNext;
        }

        if(context->apiVersion >= VK_API_VERSION_1_3) {
            features13.dynamicRendering = parameters->dynamicRendering;
            features13.synchronization2 = parameters->synchronization2;
            features13.maintenance4 = parameters->maintenance4;
            *pNextChain = &features13;
            pNextChain = &features13.pNext;
        } else if(parameters->synchronization2) {
            VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
            synchronization2Features.synchronization2 = true;
            *pNextChain = &synchronization2Features;
            pNextChain = &synchronization2Features.pNext;
        }

        if(parameters->rayQuery) {
            rayQueryFeatures.rayQuery = true;
            *pNextChain = &rayQueryFeatures;
            pNextChain = &rayQueryFeatures.pNext;
        }

        if(parameters->accelerationStructure) {
            asFeatures.accelerationStructure = true;
            *pNextChain = &asFeatures;
            pNextChain = &asFeatures.pNext;
        }
        
        if(parameters->rayTracingPipeline) {
            rayTracingFeatures.rayTracingPipeline = true;
            *pNextChain = &rayTracingFeatures;
            pNextChain = &rayTracingFeatures.pNext;
        }

        if(parameters->dynamicRenderingUnusedAttachments) {
            dynamicRenderingUnusedAttachmentsFeatures.dynamicRenderingUnusedAttachments = true;
            *pNextChain = &dynamicRenderingUnusedAttachmentsFeatures;
            pNextChain = &dynamicRenderingUnusedAttachmentsFeatures.pNext;
        }

        if (vkCreateDevice(context->physicalDevice, &createInfo, 0, &context->device)) {
            error = STROMBOLI_MAKE_ERROR(STROMBOLI_DEVICE_CREATE_ERROR, "Failed to create vulkan logical device");
        } else {
            volkLoadDevice(context->device);

            // Acquire queues
            u32 currentQueueIndex = 0;
            for(u32 i = 0; i < queueCreateInfoCount; ++i) {
                if(queueCreateInfos[i].queueFamilyIndex == graphicsQueueIndex) {
                    context->graphicsQueues = &context->queues[currentQueueIndex];
                    context->graphicsQueueCount = queueCreateInfos[i].queueCount;
                } else if(queueCreateInfos[i].queueFamilyIndex == computeQueueIndex) {
                    context->computeQueues = &context->queues[currentQueueIndex];
                    context->computeQueueCount = queueCreateInfos[i].queueCount;
                } else if(queueCreateInfos[i].queueFamilyIndex == transferQueueIndex) {
                    context->transferQueues = &context->queues[currentQueueIndex];
                    context->transferQueueCount = queueCreateInfos[i].queueCount;
                }
                for(u32 j = 0; j < queueCreateInfos[i].queueCount; ++j) {
                    context->queues[currentQueueIndex].familyIndex = queueCreateInfos[i].queueFamilyIndex;
                    //context->queues[currentQueueIndex].mutex = groundedCreateMutex();
                    vkGetDeviceQueue(context->device, context->queues[currentQueueIndex].familyIndex, j, &context->queues[currentQueueIndex].queue);
                    currentQueueIndex++;
                }
            }

            // Can this be done directly after physical device selection?
            vkGetPhysicalDeviceMemoryProperties(context->physicalDevice, &context->physicalDeviceMemoryProperties);
        }
        arenaEndTemp(temp);
    }

#ifndef STROMBOLI_NO_VMA
    if(STROMBOLI_NO_ERROR(error)) { // Create vma allocator
        VmaVulkanFunctions vmaFunctions = {0};
        vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        vmaFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vmaFunctions.vkAllocateMemory = vkAllocateMemory;
        vmaFunctions.vkFreeMemory = vkFreeMemory;
        vmaFunctions.vkMapMemory = vkMapMemory;
        vmaFunctions.vkUnmapMemory = vkUnmapMemory;
        vmaFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
        vmaFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
        vmaFunctions.vkBindBufferMemory = vkBindBufferMemory;
        vmaFunctions.vkBindImageMemory = vkBindImageMemory;
        vmaFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
        vmaFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
        vmaFunctions.vkCreateBuffer = vkCreateBuffer;
        vmaFunctions.vkDestroyBuffer = vkDestroyBuffer;
        vmaFunctions.vkCreateImage = vkCreateImage;
        vmaFunctions.vkDestroyImage = vkDestroyImage;
        vmaFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
        vmaFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
        vmaFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
        vmaFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
        vmaFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
        vmaFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
        vmaFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;
        VmaAllocatorCreateInfo createInfo = {0};
        if(parameters->bufferDeviceAddress) {
            createInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        }
        createInfo.vulkanApiVersion = context->apiVersion;
        createInfo.physicalDevice = context->physicalDevice;
        createInfo.device = context->device;
        createInfo.instance = context->instance;
        createInfo.pVulkanFunctions = &vmaFunctions;
        vmaCreateAllocator(&createInfo, &context->vmaAllocator);
    }
#endif

    arenaEndTemp(temp);
    return error;
}

StromboliResult initStromboli(StromboliContext* context, StromboliInitializationParameters* parameters) {
    *context = (StromboliContext){0};
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);
    if(!parameters) {
        // Those must be handled as readonly
        static StromboliInitializationParameters defaultParameters = {0};
        ASSERT(MEMORY_IS_ZERO(&defaultParameters, sizeof(defaultParameters)));
        parameters = &defaultParameters;
    }
    ASSERT(parameters);

    StromboliResult error = initVulkanInstance(context, parameters);

    if(STROMBOLI_NO_ERROR(error)) {
        error = initVulkanDevice(context, parameters);
    }

    if(STROMBOLI_ERROR(error)) {
        shutdownStromboli(context);
        printf("Error initializing Vulkan: %s\n", error.errorString.base);
    } else {
        ASSERT(context->graphicsQueues);
    }

    arenaEndTemp(temp);
    return error;
}

void shutdownStromboli(StromboliContext* context) {
#ifndef STROMBOLI_NO_VMA
    if(context->vmaAllocator) {
        vmaDestroyAllocator(context->vmaAllocator);
    }
#endif

    if(context->device) {
        vkDestroyDevice(context->device, 0);
    }
    if(context->debugCallback) {
        vkDestroyDebugUtilsMessengerEXT(context->instance, context->debugCallback, 0);
    }
    if(context->instance) {
        vkDestroyInstance(context->instance, 0);
    }
    *context = (StromboliContext){0};
}
