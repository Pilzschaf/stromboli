#ifndef STROMBOLI_CUBEMAP_H
#define STROMBOLI_CUBEMAP_H

/*
 * Requires stb_image
 */

#include <stromboli/stromboli.h>

StromboliImage createCubemapFromEquirectangularPanorama(StromboliContext* context, VkFormat format, String8 filename);
StromboliImage createCubemapFromFaces(StromboliContext* context, VkFormat format, String8* filenames);

#endif // STROMBOLI_CUBEMAP_H