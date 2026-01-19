#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <cstring> // Required for std::memset (memory setting)
#include <cmath>   // Required for sqrt (used in the sphere generation example)
#include <cstdint> 


// our new packed vertex struct fits the data more compactly, 
//x (6 bits), y (6 bits), z (6 bits), normal (3 bits), texture (11 bits)
struct PackedVertex {
    uint32_t data;
    // constructor to pack data on init
    PackedVertex(uint32_t x, uint32_t y, uint32_t z, uint32_t norm, uint32_t tex) {
        data = 0;
        data |= (x & 0x3f) << 0; // mask 6 bits, shift 0
        data |= (y & 0x3f) << 6; // mask 6 bits, shift 6
        data |= (z & 0x3f) << 12; // mask 6 bits, shift 12
        data |= (norm & 0x7) << 18;// mask 3 bits, shift 18
        data |= (tex & 0x7ff) << 21; // mask 11 bits, shift 21
    }
};


// this is the old method for packing, super inefficient, uses float (vec3 is 3 floats)
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord; 
};

// Standard Cubic Chunk Size
const int CHUNK_SIZE = 32; 
const float change_dist = 16.0f;

class Chunk {
public:
    Chunk() : chunkX(0), chunkY(0), chunkZ(0) {
    // std::memset is a fast way to set a block of memory to a specific value.
    // Here, we set the entire 'blocks' array to 0 (Air).
    // sizeof(blocks) calculates the total bytes: 32 * 32 * 32 * 1 byte = 32,768 bytes.
        std::memset(blocks, 0, sizeof(blocks));
}
    ~Chunk(){

    }

    // ------------------------------------------------------------------------
    // Function: Generate
    // Inputs:   x, y, z (int) - The chunk's coordinates in the world
    // Outputs:  void - Fills block data based on 3D noise/logic
    // ------------------------------------------------------------------------
    void Generate(int x, int y, int z){
    // Store the chunk's world coordinates so we can calculate absolute positions later.
    chunkX = x;
    chunkY = y;
    chunkZ = z;

    // Reset all blocks to Air (0) before generating new data.
    std::memset(blocks, 0, sizeof(blocks));

    // Iterate over every voxel in the 32x32x32 volume.
    // lx, ly, lz stands for "Local X, Local Y, Local Z"
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int ly = 0; ly < CHUNK_SIZE; ly++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    
                    // --- SPHERE GENERATION LOGIC ---
                    // We want to create a sphere in the center of the chunk.
                    // The center of a 32-block chunk is at index 16.
                    float cx = (float)lx - (float)CHUNK_SIZE/2;
                    float cy = (float)ly - (float)CHUNK_SIZE/2;
                    float cz = (float)lz - (float)CHUNK_SIZE/2;
                    
                    // Calculate distance from center: dist = sqrt(x^2 + y^2 + z^2)
                    float distanceFromCenter = sqrt(cx*cx + cy*cy + cz*cz);

                    // If the voxel is within 14 units of the center, make it solid.
                    // We use block ID 1 (which could mean "Stone" or "Dirt").
                    if (distanceFromCenter < change_dist) {
                        blocks[lx][ly][lz] = 1;
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Function: Mesh
    // Inputs:   vertices (std::vector<Vertex>&) - Output vector
    // Outputs:  void - Generates geometry for this 32x32x32 chunk
    // ------------------------------------------------------------------------
    void Mesh(std::vector<Vertex>& vertices) {
    // Loop through every single block in the chunk
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    
                    // Optimization: If the block is Air (0), it has no geometry. Skip it.
                    if (blocks[x][y][z] == 0) continue;

                    // --- FACE CULLING LOGIC ---
                    // For a solid block, we only draw a face if the neighbor in that 
                    // direction is transparent (Air). If the neighbor is solid, the face 
                    // is hidden inside the terrain, so we skip drawing it to save performance.

                    // Check Right Neighbor (x + 1) -> Draw Right Face (+X)
                    if (isTransparent(x + 1, y, z)) 
                        addFace(vertices, x, y, z, glm::vec3(1, 0, 0));

                    // Check Left Neighbor (x - 1) -> Draw Left Face (-X)
                    if (isTransparent(x - 1, y, z)) 
                        addFace(vertices, x, y, z, glm::vec3(-1, 0, 0));

                    // Check Top Neighbor (y + 1) -> Draw Top Face (+Y)
                    if (isTransparent(x, y + 1, z)) 
                        addFace(vertices, x, y, z, glm::vec3(0, 1, 0));

                    // Check Bottom Neighbor (y - 1) -> Draw Bottom Face (-Y)
                    if (isTransparent(x, y - 1, z)) 
                        addFace(vertices, x, y, z, glm::vec3(0, -1, 0));

                    // Check Front Neighbor (z + 1) -> Draw Front Face (+Z)
                    if (isTransparent(x, y, z + 1)) 
                        addFace(vertices, x, y, z, glm::vec3(0, 0, 1));

                    // Check Back Neighbor (z - 1) -> Draw Back Face (-Z)
                    if (isTransparent(x, y, z - 1)) 
                        addFace(vertices, x, y, z, glm::vec3(0, 0, -1));
                }
            }
        }
    }

private:
    int chunkX, chunkY, chunkZ;
    uint8_t blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]; // 32x32x32

    bool isTransparent(int x, int y, int z) const {
        // Bounds Checking:
        // If the coordinates are outside this chunk (e.g., -1 or 32), we currently 
        // assume it is empty air. In a full engine, you would query the *neighboring* // chunk object to see if there is actually a block there.
        if (x < 0 || x >= CHUNK_SIZE) return true;
        if (y < 0 || y >= CHUNK_SIZE) return true;
        if (z < 0 || z >= CHUNK_SIZE) return true;

        // If inside bounds, check the array. 0 means Air (Transparent).
        return blocks[x][y][z] == 0;
    }
    void addFace(std::vector<Vertex>& vertices, int x, int y, int z, const glm::vec3& normal) {
        // 1. Calculate World Position
        // We take the Chunk's offset (chunkX * 32) and add the local block pos (x).
        float wx = (float)(chunkX * CHUNK_SIZE + x);
        float wy = (float)(chunkY * CHUNK_SIZE + y);
        float wz = (float)(chunkZ * CHUNK_SIZE + z);

        // 2. Determine Orientation (Basis Vectors)
        // We know the 'normal' (direction out from the face). We need to find 
        // the 'up' and 'right' vectors to measure the width and height of the face.
        
        glm::vec3 upVector(0, 1, 0); // Default guessing 'up' is Y-axis.

        // Edge Case: If the face is pointing Up or Down (Normal is 0,1,0 or 0,-1,0),
        // then our 'upVector' cannot be parallel to it. We switch 'up' to Z-axis.
        if (glm::abs(normal.y) > 0.9f) {
            upVector = glm::vec3(0, 0, 1);
        }

        // Math: Cross Product finds a vector perpendicular to two others.
        // Right = Normal x Up (creates a vector pointing sideways along the face)
        glm::vec3 rightVector = glm::cross(normal, upVector);
        
        // Recalculate Up = Right x Normal (ensures all 3 are perfectly 90 degrees apart)
        upVector = glm::cross(rightVector, normal);

        // 3. Calculate Vertices
        // 'Center' is the middle of the voxel in world space.
        // + 0.5f moves us from the corner (integer coord) to the middle.
        glm::vec3 center = glm::vec3(wx, wy, wz) + glm::vec3(0.5f);

        // We start at the center, then move 0.5 units in the direction of the Normal
        // to reach the surface of the face. Then we move +/- 0.5 units along Right/Up
        // to reach the four corners.
        // P1 = Top-Right
        glm::vec3 p1 = center + (normal + rightVector + upVector) * 0.5f;
        // P2 = Bottom-Right
        glm::vec3 p2 = center + (normal - rightVector + upVector) * 0.5f;
        // P3 = Bottom-Left
        glm::vec3 p3 = center + (normal - rightVector - upVector) * 0.5f;
        // P4 = Top-Left
        glm::vec3 p4 = center + (normal + rightVector - upVector) * 0.5f;

        // 4. Texture Coordinates (UVs)
        // Currently set to 0,0 for all. In the future, you would set these based on
        // which corner of the texture atlas you want to read.
        glm::vec2 uv(0.0f, 0.0f);

        // 5. Push Triangles
        // OpenGL draws triangles. A square face needs 2 triangles.
        
        // Triangle 1 (P1 -> P2 -> P3)
        vertices.push_back({p1, normal, uv});
        vertices.push_back({p2, normal, uv});
        vertices.push_back({p3, normal, uv});

        // Triangle 2 (P1 -> P3 -> P4)
        vertices.push_back({p1, normal, uv}); // We reuse P1
        vertices.push_back({p3, normal, uv}); // We reuse P3
        vertices.push_back({p4, normal, uv});
    }
};