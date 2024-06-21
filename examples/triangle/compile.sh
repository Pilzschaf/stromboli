#!/bin/bash

# ignore empty patterns eg. if not files with the specified extension exist
shopt -s nullglob

for file in *.comp; do
    glslangValidator -g -V --target-env vulkan1.0 "$file" -o "${file}.spv"
done

for file in *.vert; do
    glslangValidator -g -V --target-env vulkan1.0 "$file" -o "${file}.spv"
done

for file in *.frag; do
    glslangValidator -g -V --target-env vulkan1.0 "$file" -o "${file}.spv"
done

for file in *.tese; do
    glslangValidator -g -V --target-env vulkan1.0 "$file" -o "${file}.spv"
done

for file in *.tesc; do
    glslangValidator -g -V --target-env vulkan1.0 "$file" -o "${file}.spv"
done
