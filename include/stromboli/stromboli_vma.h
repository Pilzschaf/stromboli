#ifndef STROMBOLI_VMA_H
#define STROMBOLI_VMA_H

#include "stromboli.h"

typedef struct StromboliVmaAllocator StromboliVmaAllocator;

bool stromboliVmaInit(StromboliContext* stromboli);
void stromboliVmaShutdown();
StromboliAllocationContext* stromboliGetVmaAllocator(); 
struct VmaAllocator_T* stromboliGetInternalVmaAllocator();

#endif // STROMBOLI_VMA_H
