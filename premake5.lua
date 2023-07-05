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
    objdir "obj/%{cfg.buildcfg}" -- Check if we can use cfg.buildcfg in lowercase
    targetdir "bin/examples/%{cfg.buildcfg}"
    characterset "Unicode"

    includedirs
    {
        "libs/grounded/include",
        "include",
        "libs",
    }

    filter "system:linux"
        buildoptions
        {
            "-Wall",
            "-Werror",
            "-Wno-unused-variable",
        	"-Wno-unused-function",
        }
        defines
        {
            "_GNU_SOURCE",
        }
    
    filter "system:window"
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
    }

project "StromboliStatic"
    kind "StaticLib"
    targetdir "bin/static/%{cfg.buildcfg}"
    files
    {
        "src/stromboli_device.c",
    }
    links
    {
        "GroundedStatic",
    }

project "StromboliDynamic"
    kind "SharedLib"
    targetdir "bin/dynamic/%{cfg.buildcfg}"
    files
    {
        "src/stromboli_device.c",
    }
    links
    {
        "GroundedDynamic",
    }

project "GroundedStatic"
    kind "StaticLib"
    targetdir "bin/static/%{cfg.buildcfg}"
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
    targetdir "bin/dynamic/%{cfg.buildcfg}"
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