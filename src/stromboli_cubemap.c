#include <stromboli/stromboli_cubemap.h>
#include <grounded/memory/grounded_arena.h>
#include <grounded/math/grounded_math.h>
#include <grounded/threading/grounded_threading.h>

#include <stb/stb_image.h>

StromboliImage createCubemapFromEquirectangularPanorama(StromboliContext* context, VkFormat format, String8 filename) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    int width, height, comp;
    u8* data = stbi_load(str8GetCstr(scratch, filename), &width, &height, &comp, 4);
    u32 cubemapSize = 1024;
    StromboliImage result = stromboliImageCreate(context, cubemapSize, cubemapSize, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &(struct StromboliImageParameters) {
        .cubemap = true,
        .layerCount = 6,
    });

    StromboliUploadContext uploadContext = createUploadContext(context, &context->graphicsQueues[0], 0);
    u64 size = cubemapSize * cubemapSize * 4;
    u32* faceData = ARENA_PUSH_ARRAY(scratch, cubemapSize * cubemapSize, u32);
    for(u32 face = 0; face < 6; ++face) {
        for (u32 y = 0; y < cubemapSize; ++y) {
            for (u32 x = 0; x < cubemapSize; ++x) {
                float u = (x + 0.5f) / cubemapSize;
                float v = (y + 0.5f) / cubemapSize;
                float nx = 2.0f * u - 1.0f;
                float ny = 2.0f * v - 1.0f;

                vec3 dir;
                switch (face) {
                    case 0: dir = VEC3(1, -ny, -nx); break; // +X
                    case 1: dir = VEC3(-1, -ny, nx); break; // -X
                    case 2: dir = VEC3(nx, 1, ny); break; // +Y
                    case 3: dir = VEC3(nx, -1, -ny); break; // -Y
                    case 4: dir = VEC3(nx, -ny, 1); break; // +Z
                    case 5: dir = VEC3(-nx, -ny, -1); break; // -Z
                }
                dir = v3Normalize(dir);

                float phi = atan2(dir.z, dir.x);
                float theta = acos(dir.y);
                float u2 = (phi + PI32) / (2.0f * PI32);
                float v2 = theta / PI32;

                // Sample panorama at u2, v2
                u32 sampleX = CLAMP(0, (u32)(u2 * width), width - 1);
                u32 sampleY = CLAMP(0, (u32)(v2 * height), height - 1);
                u32 pixel = ((u32*)data)[sampleX + sampleY * width];
                faceData[x + y * cubemapSize] = pixel;
            }
        }
        
        stromboliUploadDataToImageSubregion(context, &result, faceData, size, cubemapSize, cubemapSize, 1, 0, face, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, &uploadContext);
    }

    destroyUploadContext(context, &uploadContext);

    arenaEndTemp(temp);
    return result;
}

StromboliImage createCubemapFromFaces(StromboliContext* context, VkFormat format, String8* filenames) {
    MemoryArena* scratch = threadContextGetScratch(0);
    ArenaTempMemory temp = arenaBeginTemp(scratch);

    int width, height, comp;
    u8* data = stbi_load(str8GetCstr(scratch, filenames[0]), &width, &height, &comp, 4);

    StromboliImage result = stromboliImageCreate(context, width, height, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &(struct StromboliImageParameters) {
        .cubemap = true,
        .layerCount = 6,
    });

    StromboliUploadContext uploadContext = createUploadContext(context, &context->graphicsQueues[0], 0);
    for(u32 face = 0; face < 6; ++face) {
        if(!data) {
            data = stbi_load(str8GetCstr(scratch, filenames[face]), &width, &height, &comp, 4);
        }
        u64 size = width * height * 4;
        stromboliUploadDataToImageSubregion(context, &result, data, size, width, height, 1, 0, face, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, &uploadContext);
        stbi_image_free(data);
        data = 0;
    }
    destroyUploadContext(context, &uploadContext);

    arenaEndTemp(temp);
    return result;
}