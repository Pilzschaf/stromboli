workspace "Stromboli"
    architecture "x64"
    configurations
    {
        "Debug",
        "Release",
    }

    kind "ConsoleApp"
    language "C"
    cdialect "C11"
    staticruntime "on"
    objdir "obj/%{cfg.buildcfg:lower()}"
    targetdir "bin/examples/%{cfg.buildcfg:lower()}"
    characterset "Unicode"

    includedirs
    {
        "libs/grounded/include",
        "include",
        "libs",
        "libs/SPIRV-Reflect",
        "libs/Vulkan-Headers/include",
        "libs/VulkanMemoryAllocator/include",
    }
    defines
    {
        "GROUNDED_VULKAN_SUPPORT",
    }

    filter "system:linux"
        buildoptions
        {
            "-Wall",
            "-Werror",
            "-Wno-unused-variable",
        	"-Wno-unused-function",
            "-Wno-unused-but-set-variable",
        }
        defines
        {
            "_GNU_SOURCE",
        }
    
    filter "system:windows"
        -- TODO: Check if we should enable stuff like Werror/Wall for windows
        defines
        { 
            "_CRT_SECURE_NO_WARNING",
        }
    
    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        symbols "off"
    

project "ComputeClear"
    files
    {
        "examples/compute_clear/main.c",
    }
    links
    {
        "StromboliStatic",
        "GroundedStatic",
        "vma",
    }
    filter "system:linux"
        links
        {
            "stdc++",
        }

project "Triangle"
    files
    {
        "examples/triangle/main.c",
    }
    links
    {
        "StromboliStatic",
        "GroundedStatic",
        "vma",
    }
    filter "system:linux"
        links
        {
            "stdc++",
        }

project "GroundedStatic"
    kind "StaticLib"
    targetdir "bin/static/%{cfg.buildcfg:lower()}"
    files
    {
        "libs/grounded/src/file/grounded_file.c",
        "libs/grounded/src/logger/grounded_logger.c",
        "libs/grounded/src/memory/grounded_arena.c",
        "libs/grounded/src/memory/grounded_memory.c",
        "libs/grounded/src/string/grounded_string.c",
        "libs/grounded/src/threading/grounded_threading.c",
        "libs/grounded/src/window/grounded_window.c",
        "libs/grounded/src/window/grounded_window_extra.c",
    }

project "GroundedDynamic"
    kind "SharedLib"
    targetdir "bin/dynamic/%{cfg.buildcfg:lower()}"
    files
    {
        "libs/grounded/src/file/grounded_file.c",
        "libs/grounded/src/logger/grounded_logger.c",
        "libs/grounded/src/memory/grounded_arena.c",
        "libs/grounded/src/memory/grounded_memory.c",
        "libs/grounded/src/string/grounded_string.c",
        "libs/grounded/src/threading/grounded_threading.c",
        "libs/grounded/src/window/grounded_window.c",
        "libs/grounded/src/window/grounded_window_extra.c",
    }
    filter "system:windows"
        defines
        {
            "GROUNDED_WIN32_DYNAMIC_EXPORT"
        }

project "StromboliStatic"
    kind "StaticLib"
    targetdir "bin/static/%{cfg.buildcfg:lower()}"
    files
    {
        "src/stromboli_device.c",
        "src/stromboli_swapchain.c",
        "src/stromboli_pipeline.c",
        "src/stromboli_renderpass.c",
        "src/stromboli_utils.c",
        "libs/SPIRV-Reflect/spirv_reflect.c",
    }
    links
    {
        "GroundedStatic",
        "vma",   
    }

project "StromboliDynamic"
    kind "SharedLib"
    targetdir "bin/dynamic/%{cfg.buildcfg:lower()}"
    files
    {
        "src/stromboli_device.c",
        "src/stromboli_swapchain.c",
        "src/stromboli_pipeline.c",
        "src/stromboli_renderpass.c",
        "src/stromboli_utils.c",
        "libs/SPIRV-Reflect/spirv_reflect.c",
    }
    links
    {
        "GroundedDynamic",
    }
    filter "system:windows"
        links
        {
            "vma",
        }

project "vma"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    files
    {
        "src/vma.cpp",
    }
    filter "system:linux"
        buildoptions
        {
            "-Wno-error",
        }