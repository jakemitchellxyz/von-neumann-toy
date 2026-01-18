#version 330 core

// ============================================================
// GLSL 3.30 Greedy Meshing Library
// ============================================================
// Bit-shifting greedy mesher logic optimized for GLSL 3.30 core profile
// Uses native uint and bitwise operators for maximum performance
// Matches Rust implementation logic for consistency

// Constants matching Rust code
const uint CHUNK_SIZE = 16u;
const uint CHUNK_SIZE_P = 18u; // Padded chunk size
const uint CHUNK_SIZE_P2 = 324u; // 18 * 18

// ============================================================
// 1. Face Culling Logic
// ============================================================
// Finds the "edges" where a solid block (1) is next to air (0)
// Replicates: col & !(col >> 1) and col & !(col << 1)

// Returns mask for ascending edges (solid block followed by air)
uint get_ascending_mask(uint col) {
    return col & ~(col >> 1u);
}

// Returns mask for descending edges (air followed by solid block)
uint get_descending_mask(uint col) {
    return col & ~(col << 1u);
}

// ============================================================
// 2. Ambient Occlusion (AO) Calculation
// ============================================================
// Samples 8 neighbors to create a bitmask of surrounding solid blocks
// Based on: ao_index |= 1u32 << ao_i

// Get AO offset direction based on index and axis
// This matches the 'match axis' block logic from Rust code
ivec3 get_ao_offset(uint index, int axis) {
    // AO directions relative to the face being rendered
    // Index 0-7 corresponds to the 8 corner/edge neighbors
    ivec3 offsets[8];
    
    if (axis == 0) { // X axis (faces along YZ plane)
        offsets[0] = ivec3(0, -1, -1);
        offsets[1] = ivec3(0, -1,  0);
        offsets[2] = ivec3(0, -1,  1);
        offsets[3] = ivec3(0,  0, -1);
        offsets[4] = ivec3(0,  0,  1);
        offsets[5] = ivec3(0,  1, -1);
        offsets[6] = ivec3(0,  1,  0);
        offsets[7] = ivec3(0,  1,  1);
    } else if (axis == 1) { // Y axis (faces along XZ plane)
        offsets[0] = ivec3(-1, 0, -1);
        offsets[1] = ivec3(-1, 0,  0);
        offsets[2] = ivec3(-1, 0,  1);
        offsets[3] = ivec3( 0, 0, -1);
        offsets[4] = ivec3( 0, 0,  1);
        offsets[5] = ivec3( 1, 0, -1);
        offsets[6] = ivec3( 1, 0,  0);
        offsets[7] = ivec3( 1, 0,  1);
    } else { // Z axis (faces along XY plane)
        offsets[0] = ivec3(-1, -1, 0);
        offsets[1] = ivec3(-1,  0, 0);
        offsets[2] = ivec3(-1,  1, 0);
        offsets[3] = ivec3( 0, -1, 0);
        offsets[4] = ivec3( 0,  1, 0);
        offsets[5] = ivec3( 1, -1, 0);
        offsets[6] = ivec3( 1,  0, 0);
        offsets[7] = ivec3( 1,  1, 0);
    }
    
    return offsets[index];
}

// Calculate AO mask for a voxel position
// Returns a 9-bit mask where each bit indicates if a neighbor is solid
uint calculate_ao_mask(ivec3 voxel_pos, int axis, sampler3D voxel_tex) {
    uint ao_mask = 0u;
    
    // Sample 8 neighbors around the face
    for (uint i = 0u; i < 8u; i++) {
        ivec3 offset = get_ao_offset(i, axis);
        ivec3 sample_pos = voxel_pos + offset;
        
        // Use texelFetch for integer coordinates (no interpolation)
        // Assumes voxel texture stores solidity in alpha channel
        vec4 sample = texelFetch(voxel_tex, sample_pos, 0);
        
        // If neighbor is solid, set the bit at that index
        if (sample.a > 0.5) {
            ao_mask |= (1u << i);
        }
    }
    
    return ao_mask;
}

// ============================================================
// 3. Data Packing
// ============================================================
// Replicates: let block_hash = ao_index | (block_type << 9)

// Pack AO index and block type into a single uint
// AO index uses bits 0-8 (9 bits), block type uses bits 9+ (23 bits available)
uint pack_voxel_data(uint ao_index, uint block_type) {
    return ao_index | (block_type << 9u);
}

// ============================================================
// 4. Data Unpacking
// ============================================================
// Replicates: let ao = block_ao & 0b111111111; let block_t = block_ao >> 9;

// Extract AO index from packed data (first 9 bits)
uint unpack_ao(uint packed_data) {
    return packed_data & 0x1FFu; // 0b111111111 = 0x1FF
}

// Extract block type from packed data (bits 9+)
uint unpack_block_type(uint packed_data) {
    return packed_data >> 9u;
}

// ============================================================
// 5. Binary Plane Traversal
// ============================================================
// Replicates: let y = col.trailing_zeros(); col &= col - 1;
// Efficiently finds the next set bit in a column

// Find the position of the least significant set bit (trailing zeros)
// Returns -1 if no bits are set, otherwise returns the bit position (0-31)
int find_trailing_zeros(uint value) {
    if (value == 0u) {
        return -1;
    }
    
    // Manual implementation of trailing zeros
    // Count how many times we can shift right before hitting a set bit
    int count = 0;
    uint temp = value;
    
    // Unroll loop for better performance (max 32 iterations)
    if ((temp & 0x0000FFFFu) == 0u) { count += 16; temp >>= 16u; }
    if ((temp & 0x000000FFu) == 0u) { count += 8; temp >>= 8u; }
    if ((temp & 0x0000000Fu) == 0u) { count += 4; temp >>= 4u; }
    if ((temp & 0x00000003u) == 0u) { count += 2; temp >>= 2u; }
    if ((temp & 0x00000001u) == 0u) { count += 1; }
    
    return count;
}

// Find next voxel position in column and clear the bit
// Modifies col in-place (like Rust's inout parameter)
// Returns -1 if no more bits are set, otherwise returns the Y position
int find_next_voxel_pos(inout uint col) {
    if (col == 0u) {
        return -1;
    }
    
    // Find the least significant set bit
    int y = find_trailing_zeros(col);
    
    // Clear the least significant bit: col &= col - 1
    col &= (col - 1u);
    
    return y;
}

// ============================================================
// 6. Voxel Query Helpers
// ============================================================

// Check if a voxel is solid at integer coordinates
// Uses texelFetch for precise integer sampling
bool is_voxel_solid(ivec3 pos, sampler3D voxel_tex) {
    // Clamp to valid range to avoid out-of-bounds access
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
        pos.x >= int(CHUNK_SIZE) || pos.y >= int(CHUNK_SIZE) || pos.z >= int(CHUNK_SIZE)) {
        return false;
    }
    
    vec4 sample = texelFetch(voxel_tex, pos, 0);
    return sample.a > 0.5; // Solid if alpha > 0.5
}

// Get voxel data at integer coordinates
// Returns packed data (AO mask | block_type << 9)
uint get_voxel_data(ivec3 pos, int axis, sampler3D voxel_tex) {
    if (!is_voxel_solid(pos, voxel_tex)) {
        return 0u; // Air voxels return 0
    }
    
    // Calculate AO mask for this voxel
    uint ao_mask = calculate_ao_mask(pos, axis, voxel_tex);
    
    // Get block type from texture (assuming it's stored in RGB channels)
    vec4 sample = texelFetch(voxel_tex, pos, 0);
    uint block_type = uint(sample.r * 255.0); // Convert from normalized float
    
    // Pack and return
    return pack_voxel_data(ao_mask, block_type);
}

// ============================================================
// 7. Greedy Quad Generation
// ============================================================

// Generate a quad for a greedy mesh face
// Returns quad vertices in world space (multiply by voxelSize to get final positions)
// axis: 0=X, 1=Y, 2=Z
// direction: -1 or +1 (negative or positive face direction)
// start_pos: starting position of the quad in voxel coordinates
// width: width of the quad along the first axis (in voxels)
// height: height of the quad along the second axis (in voxels)
// voxel_size: size of each voxel in world space
void generate_greedy_quad(int axis, int direction, ivec3 start_pos, int width, int height, float voxel_size,
                         out vec3 v0, out vec3 v1, out vec3 v2, out vec3 v3) {
    // Convert integer voxel coordinates to world space
    vec3 base = vec3(start_pos) * voxel_size;
    float w = float(width) * voxel_size;
    float h = float(height) * voxel_size;
    
    if (axis == 0) { // X-axis face (YZ plane)
        float x = base.x + (direction > 0 ? voxel_size : 0.0);
        v0 = vec3(x, base.y, base.z);
        v1 = vec3(x, base.y + h, base.z);
        v2 = vec3(x, base.y + h, base.z + w);
        v3 = vec3(x, base.y, base.z + w);
    } else if (axis == 1) { // Y-axis face (XZ plane)
        float y = base.y + (direction > 0 ? voxel_size : 0.0);
        v0 = vec3(base.x, y, base.z);
        v1 = vec3(base.x + w, y, base.z);
        v2 = vec3(base.x + w, y, base.z + h);
        v3 = vec3(base.x, y, base.z + h);
    } else { // Z-axis face (XY plane)
        float z = base.z + (direction > 0 ? voxel_size : 0.0);
        v0 = vec3(base.x, base.y, z);
        v1 = vec3(base.x + w, base.y, z);
        v2 = vec3(base.x + w, base.y + h, z);
        v3 = vec3(base.x, base.y + h, z);
    }
}
