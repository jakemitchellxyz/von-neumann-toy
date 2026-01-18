#version 450
#extension GL_EXT_scalar_block_layout : require

// ============================================================
// Greedy Meshing Geometry Shader
// ============================================================
// Takes voxel data and generates optimized quads using greedy meshing
// This shader should be included after greedy-meshing.glsl

// Input: Point primitive (represents a chunk or region to mesh)
// Geometry shader input layout qualifier
layout(points) in;

// Geometry shader output layout qualifier
layout(triangle_strip, max_vertices = 4) out;

// Uniforms - Samplers (require binding qualifiers for Vulkan)
layout(binding = 0) uniform sampler3D uVoxelTexture; // 3D texture containing voxel data

// Uniform block for non-opaque uniforms (required for Vulkan)
layout(set = 0, binding = 1, scalar) uniform Uniforms
{
    mat4 uMVP;          // Model-View-Projection matrix
    float uVoxelSize;   // Size of each voxel in world space
    ivec3 uChunkOffset; // Offset of this chunk in voxel space
    int uAxis;          // Current axis being meshed (0=X, 1=Y, 2=Z)
};

// Output to fragment shader
layout(location = 0) out vec3 gWorldPos;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec2 gTexCoord;
layout(location = 3) out float gAO; // Ambient occlusion factor

// Include greedy meshing functions
// Note: In a real implementation, this would be included via string concatenation
// For now, we assume the functions are available

// Simplified version - in practice, you'd include greedy-meshing.glsl here
// For demonstration, we'll use a simplified greedy meshing approach

void main()
{
    // Get chunk position from input point
    ivec3 chunk_pos = ivec3(gl_in[0].gl_Position.xyz);

    // Process each slice along the current axis
    int axis = uAxis;
    int slice = chunk_pos[axis];

    // For each direction (-1 and +1)
    for (int dir = -1; dir <= 1; dir += 2)
    {
        // Create a mask for this slice
        // In a full implementation, you'd iterate through all positions
        // and use bitwise operations to find quads

        // Simplified: generate a single quad per chunk for demonstration
        // Real implementation would scan the slice and merge adjacent voxels

        ivec3 quad_start = chunk_pos;
        int quad_width = 1;
        int quad_height = 1;

        // Generate quad vertices
        vec3 v0, v1, v2, v3;
        generate_greedy_quad(axis, dir, quad_start, quad_width, quad_height, v0, v1, v2, v3);

        // Calculate normal based on axis and direction
        vec3 normal;
        if (axis == 0)
        {
            normal = vec3(float(dir), 0.0, 0.0);
        }
        else if (axis == 1)
        {
            normal = vec3(0.0, float(dir), 0.0);
        }
        else
        {
            normal = vec3(0.0, 0.0, float(dir));
        }

        // Emit quad as two triangles
        // Triangle 1: v0 -> v1 -> v2
        gWorldPos = v0 * uVoxelSize;
        gNormal = normal;
        gTexCoord = vec2(0.0, 0.0);
        gAO = 1.0; // TODO: Calculate from AO mask
        gl_Position = uMVP * vec4(gWorldPos, 1.0);
        EmitVertex();

        gWorldPos = v1 * uVoxelSize;
        gNormal = normal;
        gTexCoord = vec2(0.0, 1.0);
        gAO = 1.0;
        gl_Position = uMVP * vec4(gWorldPos, 1.0);
        EmitVertex();

        gWorldPos = v2 * uVoxelSize;
        gNormal = normal;
        gTexCoord = vec2(1.0, 1.0);
        gAO = 1.0;
        gl_Position = uMVP * vec4(gWorldPos, 1.0);
        EmitVertex();

        gWorldPos = v3 * uVoxelSize;
        gNormal = normal;
        gTexCoord = vec2(1.0, 0.0);
        gAO = 1.0;
        gl_Position = uMVP * vec4(gWorldPos, 1.0);
        EmitVertex();

        EndPrimitive();
    }
}
