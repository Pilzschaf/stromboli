#extension GL_EXT_nonuniform_qualifier : enable

// https://dev.to/gasim/implementing-bindless-design-in-vulkan-34no

//layout (set = 0, binding = 0) uniform image2D textures[];

// We always bind the bindless descriptor set to set = 0
#define BINDLESS_DESCRIPTOR_SET 0

#define BINDLESS_UNIFORM_BINDING 0
#define BINDLESS_STORAGE_BINDING 1
#define BINDLESS_IMAGE_BINDING 2
#define BINDLESS_SAMPLER_BINDING 3

#define GET_LAYOUT_VARIABLE_NAME(Name) u##Name##Register

// Register uniform
#define REGISTER_UNIFORM(Name, Struct) \
  layout(set = BINDLESS_DESCRIPTOR_SET, binding = BINDLESS_UNIFORM_BINDING) \
      uniform Name Struct GET_LAYOUT_VARIABLE_NAME(Name)[]

// Register storage buffer
#define REGISTER_BUFFER(Layout, BufferAccess, Name, Struct) \
  layout(Layout, set = BINDLESS_DESCRIPTOR_SET, binding = BINDLESS_STORAGE_BINDING) \
  BufferAccess buffer Name Struct GET_LAYOUT_VARIABLE_NAME(Name)[]

// Access a specific resource
#define GET_RESOURCE(Name, Index) GET_LAYOUT_VARIABLE_NAME(Name)[Index]

// Register empty resources
// to be compliant with the pipeline layout
// even if the shader does not use all the descriptors
REGISTER_UNIFORM(DummyUniform, { uint ignore; });
REGISTER_BUFFER(std430, readonly, DummyBuffer, { uint ignore; });


// Register textures
layout(set = BINDLESS_DESCRIPTOR_SET, binding = BINDLESS_IMAGE_BINDING, rgba8) uniform image2D uGlobalImages2D[];
layout(set = BINDLESS_DESCRIPTOR_SET, binding = BINDLESS_SAMPLER_BINDING) uniform sampler2D uGlobalSamplers2D[];
