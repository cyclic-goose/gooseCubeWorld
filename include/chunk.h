#pragma once
#include <vector>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord; 
};

// Standard Cubic Chunk Size
const int CHUNK_SIZE = 32; 

class Chunk {
public:
    Chunk();
    ~Chunk();

    // ------------------------------------------------------------------------
    // Function: Generate
    // Inputs:   x, y, z (int) - The chunk's coordinates in the world
    // Outputs:  void - Fills block data based on 3D noise/logic
    // ------------------------------------------------------------------------
    void Generate(int x, int y, int z);

    // ------------------------------------------------------------------------
    // Function: Mesh
    // Inputs:   vertices (std::vector<Vertex>&) - Output vector
    // Outputs:  void - Generates geometry for this 32x32x32 chunk
    // ------------------------------------------------------------------------
    void Mesh(std::vector<Vertex>& vertices);

private:
    int chunkX, chunkY, chunkZ;
    uint8_t blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]; // 32x32x32

    bool isTransparent(int x, int y, int z) const;
    void addFace(std::vector<Vertex>& vertices, int x, int y, int z, const glm::vec3& normal);
};