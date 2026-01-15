#include "chunk.h"
#include <cstring> // Required for std::memset (memory setting)
#include <cmath>   // Required for sqrt (used in the sphere generation example)

// ------------------------------------------------------------------------
// Function: Constructor
// Description: Initializes a new empty chunk object.
// ------------------------------------------------------------------------
Chunk::Chunk() : chunkX(0), chunkY(0), chunkZ(0) {
    // std::memset is a fast way to set a block of memory to a specific value.
    // Here, we set the entire 'blocks' array to 0 (Air).
    // sizeof(blocks) calculates the total bytes: 32 * 32 * 32 * 1 byte = 32,768 bytes.
    std::memset(blocks, 0, sizeof(blocks));
}

// ------------------------------------------------------------------------
// Function: Destructor
// Description: Cleans up resources. 
// Note: Since we are using a static array (blocks[32][32][32]), the memory 
// is automatically managed when the object goes out of scope. 
// If we used 'new uint8_t[...]', we would need 'delete[]' here.
// ------------------------------------------------------------------------
Chunk::~Chunk() {}

// ------------------------------------------------------------------------
// Function: Generate
// Inputs:   x, y, z (int) - The global coordinates of this chunk in the world.
//                           If CHUNK_SIZE is 32, (0,0,0) is origin, (1,0,0) is x=32.
// Description: Fills the block array with a test pattern (a sphere).
// ------------------------------------------------------------------------
void Chunk::Generate(int x, int y, int z) {
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
                float cx = (float)lx - 16.0f;
                float cy = (float)ly - 16.0f;
                float cz = (float)lz - 16.0f;
                
                // Calculate distance from center: dist = sqrt(x^2 + y^2 + z^2)
                float distanceFromCenter = sqrt(cx*cx + cy*cy + cz*cz);

                // If the voxel is within 14 units of the center, make it solid.
                // We use block ID 1 (which could mean "Stone" or "Dirt").
                if (distanceFromCenter < 14.0f) {
                    blocks[lx][ly][lz] = 1;
                }
            }
        }
    }
}

// ------------------------------------------------------------------------
// Function: isTransparent
// Inputs:   x, y, z (int) - Local coordinates to check.
// Outputs:  bool - Returns true if the block is Air or outside the chunk.
// Description: Used by the meshing algorithm to decide if a face should be drawn.
//              If a block is solid but covered by another solid block, it's invisible.
// ------------------------------------------------------------------------
bool Chunk::isTransparent(int x, int y, int z) const {
    // Bounds Checking:
    // If the coordinates are outside this chunk (e.g., -1 or 32), we currently 
    // assume it is empty air. In a full engine, you would query the *neighboring* // chunk object to see if there is actually a block there.
    if (x < 0 || x >= CHUNK_SIZE) return true;
    if (y < 0 || y >= CHUNK_SIZE) return true;
    if (z < 0 || z >= CHUNK_SIZE) return true;

    // If inside bounds, check the array. 0 means Air (Transparent).
    return blocks[x][y][z] == 0;
}

// ------------------------------------------------------------------------
// Function: Mesh
// Inputs:   vertices (std::vector<Vertex>&) - The list to push new triangles into.
// Description: Scans the voxel data and creates geometry for visible faces.
//              This is the "Greedy Meshing" or "Face Culling" pass.
// ------------------------------------------------------------------------
void Chunk::Mesh(std::vector<Vertex>& vertices) {
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

// ------------------------------------------------------------------------
// Function: addFace
// Inputs:   vertices - Reference to the mesh vector
//           x, y, z  - Local coordinates of the block
//           normal   - Direction this face is pointing (e.g., 0,1,0 is Up)
// Description: Calculates the 4 corner positions of a square face and adds 
//              two triangles (6 vertices) to the mesh.
// ------------------------------------------------------------------------
void Chunk::addFace(std::vector<Vertex>& vertices, int x, int y, int z, const glm::vec3& normal) {
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