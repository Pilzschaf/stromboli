#ifndef STROMBOLI_GEOMETRY_H
#define STROMBOLI_GEOMETRY_H

static const float stromboliCubeLineVertices[] = {
    -1, -1, -1, 1, -1, -1,  // bottom
    1, -1, -1,  1, -1, 1,
    1, -1, 1,   -1, -1, 1,
    -1, -1, 1,  -1, -1, -1,

    -1, 1, -1,  1, 1, -1,  // top
    1, 1, -1,   1, 1, 1,
    1, 1, 1,    -1, 1, 1,
    -1, 1, 1,   -1, 1, -1,

    -1, -1, -1, -1, 1, -1, // sides
    1, -1, -1,  1, 1, -1,
    1, -1, 1,   1, 1, 1,
    -1, -1, 1,  -1, 1, 1,
};

// http://www.songho.ca/opengl/gl_vertexarray.html
static const float stromboliCubeVertices[] = {
    1, 1, 1,  -1, 1, 1,  -1,-1, 1,      // v0-v1-v2 (front)
    -1,-1, 1,   1,-1, 1,   1, 1, 1,      // v2-v3-v0

    1, 1, 1,   1,-1, 1,   1,-1,-1,      // v0-v3-v4 (right)
    1,-1,-1,   1, 1,-1,   1, 1, 1,      // v4-v5-v0

    1, 1, 1,   1, 1,-1,  -1, 1,-1,      // v0-v5-v6 (top)
    -1, 1,-1,  -1, 1, 1,   1, 1, 1,      // v6-v1-v0

    -1, 1, 1,  -1, 1,-1,  -1,-1,-1,      // v1-v6-v7 (left)
    -1,-1,-1,  -1,-1, 1,  -1, 1, 1,      // v7-v2-v1

    -1,-1,-1,   1,-1,-1,   1,-1, 1,      // v7-v4-v3 (bottom)
    1,-1, 1,  -1,-1, 1,  -1,-1,-1,      // v3-v2-v7

    1,-1,-1,  -1,-1,-1,  -1, 1,-1,      // v4-v7-v6 (back)
    -1, 1,-1,   1, 1,-1,   1,-1,-1
};    // v6-v5-v4

#endif // STROMBOLI_GEOMETRY_H