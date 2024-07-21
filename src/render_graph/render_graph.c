#include <stromboli/stromboli_render_graph.h>

// I need the swapchain output image view to be able to record the rendering commands. Two options:
// 1. Make a beginRendering call which acquires swapchain image
// 2. Always render to offscreen buffers and then just blit the specified one to the swapchain
// I think that variant 2 might be better as this means that all attachments go through the same functions

// Render Graph does not overlap the execution of one graph with the next. However there might still be an overlap between
// Recording and Execution which would make double buffering necessary.

#include "render_graph_build.c"
#include "render_graph_compile.c"
#include "render_graph_execute.c"
