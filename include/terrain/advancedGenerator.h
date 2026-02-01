#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <FastNoise/FastNoise.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ================================================================================================
// ADVANCED GENERATOR
// ================================================================================================
// This class handles the procedural generation of the game world.
// It uses multiple layers of noise (FastNoise) to generate:
// 1. Base rolling hills
// 2. Sharp mountains
// 3. Rare "Mega Peaks" (Everest-style mountains)
// 4. "Mega Craters" (Volcanic structures with rims)
// 5. Biomes (Temperature/Moisture maps)
// 6. 3D Caves (Simplex noise)
// ================================================================================================
class AdvancedGenerator : public ITerrainGenerator {
public:
    // --------------------------------------------------------------------------------------------
    // GENERATION SETTINGS
    // --------------------------------------------------------------------------------------------
    // These are the knobs and dials the user (or ImGui) can tweak to change the world.
    struct GenSettings {
        int seed = 1804289383;              // The DNA of the world. Same seed = Same world.
        float coordinateScale = 0.073f;     // "Zoom" level. Smaller = wider features. Larger = noisy/small features.
        
        // --- Base Terrain ---
        float minimumHeight = 20.0f;        // The absolute floor of the terrain (excluding craters).
        float hillAmplitude = 60.0f;        // How tall the rolling hills are.
        float hillFrequency = 5.0f;         // How often hills repeat (higher = erratic/bumpy).
        float mountainAmplitude = 25.0f;    // Added height for the secondary mountain layer.
        float mountainFrequency = 1.0f;     // Scale of the secondary mountains.
        
        // --- Mega Peaks (Everest) ---
        // These are rare, massive mountains that break the cloud layer.
        float megaPeakRarity = 0.6f;        // Controls the noise frequency for peak placement.
        float megaPeakHeight = 1200.0f;     // How tall these peaks can get (in blocks).
        float megaPeakThreshold = 0.7f;     // Noise value required to spawn a peak (0.0 to 1.0). Higher = Rarer.
        
        // --- Mega Craters / Volcanoes ---
        // Circular structures with a raised rim and a deep pit.
        float craterScale = 2.0f;           // Controls the width. Lower number = Massive craters.
        float craterDepth = 50.0f;          // How deep the hole is.
        float craterTriggerThreshold = 0.9f;// Trigger value (0.0 to 1.0). Lower = Craters everywhere.
        float craterRimWidth = 0.01f;       // Multiplier for how wide/high the rim around the crater is.
        float craterFloorLift = 250.0f;     // Lifts the entire crater structure up (creating a volcano cone).

        // --- World Limits ---
        int seaLevel = 30;                  // Y-level for water generation.
        int maxWorldHeight = 1024;          // Hard cap on geometry height.
        int bedrockDepth = 3;               // Unbreakable layer at the bottom.
        
        // --- Biome Settings ---
        float biomeMapScale = 0.1f;         // How large biome zones are (Temperature/Moisture map zoom).
        float temperatureScale = 0.005f;    // (Unused in main logic, but part of calculation).
        float rainfallScale = 0.005f;       // (Unused in main logic, but part of calculation).
        
        // --- Vegetation ---
        // Probabilities (1 in X chance) per block to spawn a tree in that biome.
        int treeChanceForest = 15;          // High density (1 in 15 blocks).
        int treeChancePlains = 100;         // Low density.
        int treeChanceDesert = 200;         // Very rare (Cactus).
    };

    // --------------------------------------------------------------------------------------------
    // CONSTRUCTORS
    // --------------------------------------------------------------------------------------------
    AdvancedGenerator() : m_settings(GenSettings()) { Init(); }
    AdvancedGenerator(int seed) { m_settings = GenSettings(); m_settings.seed = seed; Init(); }
    AdvancedGenerator(GenSettings settings) : m_settings(settings) { Init(); }

    // --------------------------------------------------------------------------------------------
    // INITIALIZATION
    // --------------------------------------------------------------------------------------------
    // Sets up the FastNoise node graph. This connects the noise modules together.
    // Called whenever settings change to re-seed or re-parameterize the noise engines.
    void Init() override {
        // 1. Base Height Noise (Fractal Perlin)
        // Used for the general rolling hills and base terrain shape.
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnHeightFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnHeightFractal->SetSource(fnPerlin);
        fnHeightFractal->SetOctaveCount(5);       // 5 layers of detail.
        fnHeightFractal->SetGain(0.5f);           // Roughness.
        fnHeightFractal->SetLacunarity(2.0f);     // Detail scaling.
        m_baseTerrainNoise = fnHeightFractal;

        // 2. Mountain Noise (Fractal Simplex)
        // Used to add "jaggedness" on top of the hills. Simplex looks sharper than Perlin.
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnMountainFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnMountainFractal->SetSource(fnSimplex);
        fnMountainFractal->SetOctaveCount(4);
        m_mountainNoise = fnMountainFractal;

        // 3. Biome & Feature Noises
        m_temperatureNoise = FastNoise::New<FastNoise::Perlin>();  // Hot vs Cold
        m_moistureNoise    = FastNoise::New<FastNoise::Perlin>();  // Wet vs Dry
        m_caveNoise        = FastNoise::New<FastNoise::Perlin>();  // 3D Cheese Caves
        m_megaPeakNoise    = FastNoise::New<FastNoise::Simplex>(); // Rare mountain placement
        m_craterNoise      = FastNoise::New<FastNoise::Simplex>(); // Rare crater placement
    }

    // --------------------------------------------------------------------------------------------
    // ASSET MANAGEMENT
    // --------------------------------------------------------------------------------------------
    // Returns list of textures required by this generator for the texture atlas.
    std::vector<std::string> GetTexturePaths() const override {
        //std::vector<std::string> textures(30, "resources/textures/dirt1.jpg");
        std::vector<std::string> texturePaths = {
            "resources/textures/grassTop.jpg",
            "resources/textures/mcdirt.jpg",   // ID 1
            "resources/textures/mcgrass.jpg",
            "resources/textures/snow1.jpg", 

        };
        return texturePaths;
    }

    // --------------------------------------------------------------------------------------------
    // UTILITY: PSEUDO-RANDOM HASH
    // --------------------------------------------------------------------------------------------
    // A deterministic random number generator.
    // INPUT: X, Z coordinates + Seed.
    // OUTPUT: A predictable integer.
    // PURPOSE: Used for placing trees. We don't want to use standard rand() because 
    // we need the tree to be in the exact same spot every time we load the chunk.
    inline int PseudoRandomHash(int x, int z, int seed) const {
        int h = seed + x * 374761393 + z * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h ^ (h >> 16));
    }

    // --------------------------------------------------------------------------------------------
    // GET HEIGHT (Single Point)
    // --------------------------------------------------------------------------------------------
    // Calculates the terrain surface height (Y) at a specific (X, Z) world coordinate.
    // This combines all 4 major noise layers (Base, Mountain, Peak, Crater).
    int GetHeight(float x, float z) const {
        // Normalize coordinates based on global scale
        float normalizedX = x * m_settings.coordinateScale;
        float normalizedZ = z * m_settings.coordinateScale;

        // --- Step 1: Basic Terrain ---
        // Combine rolling hills and jagged mountain noise.
        float baseHeightVal = m_baseTerrainNoise->GenSingle2D(normalizedX * m_settings.hillFrequency, normalizedZ * m_settings.hillFrequency, m_settings.seed);
        float mountainVal   = m_mountainNoise->GenSingle2D(normalizedX * 0.5f * m_settings.mountainFrequency, normalizedZ * 0.5f * m_settings.mountainFrequency, m_settings.seed + 1);
        
        // --- Step 2: Mega Peaks (Everest Logic) ---
        // We check a low-frequency noise map. If it crosses a threshold, we boost height massively.
        float megaPeakZone = m_megaPeakNoise->GenSingle2D(normalizedX * m_settings.megaPeakRarity * 0.1f, normalizedZ * m_settings.megaPeakRarity * 0.1f, m_settings.seed + 99);
        float megaPeakBoost = 0.0f;
        
        if (megaPeakZone > m_settings.megaPeakThreshold) {
            // Normalize the factor (0.0 to 1.0 based on how far past threshold we are)
            float factor = (megaPeakZone - m_settings.megaPeakThreshold) / (1.0f - m_settings.megaPeakThreshold);
            factor = factor * factor; // Square it for an exponential curve (sharper peak)
            megaPeakBoost = factor * m_settings.megaPeakHeight;
        }

        // --- Step 3: Mega Craters (Volcano Logic) ---
        float craterZone = m_craterNoise->GenSingle2D(normalizedX * m_settings.craterScale * 0.1f, normalizedZ * m_settings.craterScale * 0.1f, m_settings.seed + 55);
        float craterModifier = 0.0f;
        
        if (craterZone > m_settings.craterTriggerThreshold) {
            float factor = (craterZone - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
            
            // We use a Sine wave (0 -> 1 -> 0) to shape the crater.
            // The "Rim" and "Lift" rise up as we approach the center, then drop off.
            float sineShape = std::sin(factor * 3.14159f); 
            
            float lift = sineShape * m_settings.craterFloorLift;             // Raises the whole structure
            float rim  = sineShape * (m_settings.craterDepth * m_settings.craterRimWidth); // Adds a lip
            float dig  = (factor * factor) * m_settings.craterDepth;         // Digs out the middle
            
            craterModifier = lift + rim - dig;
        }

        // --- Step 4: Final Combination ---
        // Make mountains sharper by cubing the value (preserving sign? abs makes it positive peaks only)
        float mountainFactor = std::abs(mountainVal); 
        mountainFactor = mountainFactor * mountainFactor * mountainFactor; 

        float finalHeight = m_settings.minimumHeight 
                          + (baseHeightVal * m_settings.hillAmplitude) 
                          + (mountainFactor * m_settings.mountainAmplitude) 
                          + megaPeakBoost 
                          + craterModifier;

        return (int)std::clamp(finalHeight, 0.0f, (float)m_settings.maxWorldHeight);
    }

    // --------------------------------------------------------------------------------------------
    // GET HEIGHT BOUNDS (For Culling/Optimization)
    // --------------------------------------------------------------------------------------------
    // Returns the minimum and maximum possible height for a chunk column.
    // Used by the engine to determine if it needs to generate a chunk at all (Air skipping).
    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        // Currently returns global bounds. 
        // OPTIMIZATION TODO: Calculate bounds specific to this chunk's X/Z for better culling.
        minH = 0;
        maxH = m_settings.maxWorldHeight;
    }

    // --------------------------------------------------------------------------------------------
    // GET BLOCK (Single Voxel)
    // --------------------------------------------------------------------------------------------
    // basically a copy of the Chunk batch method for retreiving a block the player interacted with
    // Determines the block ID at a specific 3D coordinate.
    // lodScale: Level of Detail. 1 = Standard. 2, 4, 8 = Low detail (distant chunks).
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        int integerX = (int)std::floor(x);
        int integerZ = (int)std::floor(z);
        int worldY   = (int)std::floor(y);

        // 1. Get Surface Height
        int surfaceHeight = GetHeight(x, z);

        // 2. Bedrock Layer (Bottom of world)
        if (worldY <= m_settings.bedrockDepth) return 19; // ID 19: Adminium/Bedrock

        // 3. Biome Calculation
        float normalizedX = x * m_settings.coordinateScale;
        float normalizedZ = z * m_settings.coordinateScale;
        float tempVal = m_temperatureNoise->GenSingle2D(normalizedX * m_settings.biomeMapScale, normalizedZ * m_settings.biomeMapScale, m_settings.seed + 2);
        float moistVal = m_moistureNoise->GenSingle2D(normalizedX * m_settings.biomeMapScale, normalizedZ * m_settings.biomeMapScale, m_settings.seed + 3);
        
        // Adjust temperature based on height (It gets colder higher up)
        float adjustedTemp = tempVal - (surfaceHeight - m_settings.seaLevel) * 0.005f;
        
        uint8_t biomeID = 0; // 0: Plains
        if (adjustedTemp > 0.4f && moistVal < -0.2f) biomeID = 2;       // Desert
        else if (adjustedTemp < -0.3f || surfaceHeight > 220) biomeID = 3; // Snow/Peak
        else if (moistVal > 0.2f) biomeID = 1;                          // Forest

        // 4. Tree Logic (Only at LOD 1 for performance)
        // Checks neighboring blocks to see if a tree spawned there and if this block is part of it.
        if (lodScale == 1 && worldY > surfaceHeight) {
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    int neighborX = integerX + dx;
                    int neighborZ = integerZ + dz;
                    
                    int chance = 0; uint8_t treeType = 0;
                    if (biomeID == 1)      { chance = m_settings.treeChanceForest; treeType = 1; } // Oak
                    else if (biomeID == 0) { chance = m_settings.treeChancePlains; treeType = 1; } // Oak
                    else if (biomeID == 2) { chance = m_settings.treeChanceDesert; treeType = 3; } // Cactus
                    else if (biomeID == 3) { chance = 60; treeType = 2; }                          // Pine
                    
                    if (chance > 0) {
                         int neighborHeight = GetHeight((float)neighborX, (float)neighborZ);
                         // Trees only spawn on valid terrain (not underwater, not in space)
                         if (neighborHeight > m_settings.seaLevel && neighborHeight < 200) {
                            // Check the deterministic hash
                            if ((std::abs(PseudoRandomHash(neighborX, neighborZ, m_settings.seed)) % chance) == 0) {
                                int relativeY = worldY - neighborHeight;
                                int distSq = dx*dx + dz*dz;
                                
                                // Trunk Logic
                                if (distSq == 0) {
                                    if ((treeType == 1 || treeType == 2) && relativeY > 0 && relativeY < 6) return (treeType == 2) ? 15 : 13; // Log IDs
                                    if (treeType == 3 && relativeY > 0 && relativeY < 4) return 14; // Cactus Green
                                } 
                                // Leaves Logic
                                else {
                                    if (treeType == 1 && relativeY >= 4 && relativeY <= 6 && distSq <= 2) return 14; // Oak Leaves
                                    if (treeType == 2 && relativeY >= 3 && relativeY <= 6 && distSq <= 2) return 16; // Pine Leaves
                                }
                            }
                         }
                    }
                }
            }
        }

        // 5. Standard Terrain Fill (Air/Water)
        if (worldY > surfaceHeight) {
            if (worldY <= m_settings.seaLevel) return (biomeID == 3) ? 12 : 6; // Ice or Water
            return 0; // Air
        }

        // 6. Underground vs Surface Blocks
        int depth = surfaceHeight - worldY;
        uint8_t blockID = 3; // Default Stone
        
        // Topsoil layers
        if (depth < lodScale) {
            blockID = (biomeID == 2) ? 5 : (biomeID == 3 ? 4 : 1); // Sand, Snow, or Grass
        } else if (depth < (4 * lodScale)) {
            blockID = (biomeID == 2) ? 11 : (biomeID == 3 ? 4 : 2); // Sandstone, Snow, or Dirt
        }
        
        // High altitude override (Bare stone/snow on peaks)
        if (worldY > 220) {
            if (depth < lodScale) blockID = 4; // Snow block
            else if (depth < (10 * lodScale)) blockID = 12; // Ice
            else blockID = 19; // Stone/Rock
        }

        // --- VOLCANIC OVERRIDE (Physics Check) ---
        // If we are in a crater, replace surface blocks with obsidian/lava rock
        float craterZone = m_craterNoise->GenSingle2D(normalizedX * m_settings.craterScale * 0.1f, normalizedZ * m_settings.craterScale * 0.1f, m_settings.seed + 55);
        if (craterZone > m_settings.craterTriggerThreshold && worldY <= surfaceHeight) {
             if (blockID != 0 && blockID != 6 && blockID != 19) {
                 float factor = (craterZone - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
                 if (factor > 0.2f) blockID = 19; // Center = Obsidian
                 else blockID = 19;               // Rim = Red Stone Mix
             }
        }

        // 7. Cave Generation (3D Noise Carving)
        if (lodScale == 1 && worldY > m_settings.bedrockDepth) {
             float caveScale = 0.03f;
             float caveVal = m_caveNoise->GenSingle3D(x * caveScale, z * caveScale, y * caveScale, m_settings.seed);
             
             // Bias towards surface (fewer caves near surface to prevent breaking ground too often)
             int depthFromSurface = surfaceHeight - worldY;
             float surfaceBias = 0.0f;
             if (depthFromSurface < 8) surfaceBias = (8 - depthFromSurface) * 0.08f; 
             
             // If noise is high enough, cut air
             if ((caveVal - surfaceBias) > 0.4f) return 0;
        }

        return blockID; 
    }

    // --------------------------------------------------------------------------------------------
    // GENERATE CHUNK (Batch Processing)
    // --------------------------------------------------------------------------------------------
    // This is the High-Performance loop called by Task_Generate.
    // Instead of calling GetBlock() (which is slow) for every pixel, we generate noise
    // for the whole chunk at once using FastNoise's SIMD capabilities.
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        // STATIC THREAD_LOCAL BUFFERS
        // We reuse these vectors across function calls to avoid allocating memory 
        // 4096 times per second. This is a critical optimization.
        static thread_local std::vector<float> bufferHeightMap;
        static thread_local std::vector<float> bufferMountain;
        static thread_local std::vector<float> bufferMegaPeak;
        static thread_local std::vector<float> bufferCrater; 
        static thread_local std::vector<float> bufferTemperature;
        static thread_local std::vector<float> bufferMoisture;
        static thread_local std::vector<float> bufferCave3D;
        static thread_local std::vector<int>   mapFinalHeight; // Combined heightmap
        static thread_local std::vector<uint8_t> mapBiomeID; 
        static thread_local std::vector<uint8_t> mapTreeData;  

        const int PADDED_CHUNK_SIZE = CHUNK_SIZE_PADDED; // Include neighbors for smooth normals
        const int size2D = PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE;
        const int size3D = PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE;

        // Resize buffers only if this thread hasn't run before or size changed
        if (bufferHeightMap.size() != size2D) {
            bufferHeightMap.resize(size2D); bufferMountain.resize(size2D); bufferMegaPeak.resize(size2D); bufferCrater.resize(size2D);
            bufferTemperature.resize(size2D); bufferMoisture.resize(size2D);
            mapFinalHeight.resize(size2D); mapBiomeID.resize(size2D); mapTreeData.resize(size2D);
            bufferCave3D.resize(size3D);
        }

        float genScale = m_settings.coordinateScale;
        
        // Calculate World Coordinates for the corner of this chunk
        float worldStartX = (float)((cx * CHUNK_SIZE - 1) * lodScale);
        float worldStartZ = (float)((cz * CHUNK_SIZE - 1) * lodScale);
        float worldStartY = (float)((cy * CHUNK_SIZE - 1) * lodScale);
        float worldStep   = (float)lodScale;

        // --- PHASE 1: Generate Noise Maps (Batch) ---
        // FastNoise fills the arrays significantly faster than calling GenSingle2D in a loop.
        m_baseTerrainNoise->GenUniformGrid2D(bufferHeightMap.data(), worldStartX * genScale * m_settings.hillFrequency, worldStartZ * genScale * m_settings.hillFrequency, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.hillFrequency, worldStep * genScale * m_settings.hillFrequency, m_settings.seed);
        
        m_mountainNoise->GenUniformGrid2D(bufferMountain.data(), worldStartX * genScale * 0.5f * m_settings.mountainFrequency, worldStartZ * genScale * 0.5f * m_settings.mountainFrequency, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * 0.5f * m_settings.mountainFrequency, worldStep * genScale * 0.5f * m_settings.mountainFrequency, m_settings.seed + 1);
        
        m_megaPeakNoise->GenUniformGrid2D(bufferMegaPeak.data(), worldStartX * genScale * m_settings.megaPeakRarity * 0.1f, worldStartZ * genScale * m_settings.megaPeakRarity * 0.1f, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.megaPeakRarity * 0.1f, worldStep * genScale * m_settings.megaPeakRarity * 0.1f, m_settings.seed + 99);
        
        m_craterNoise->GenUniformGrid2D(bufferCrater.data(), worldStartX * genScale * m_settings.craterScale * 0.1f, worldStartZ * genScale * m_settings.craterScale * 0.1f, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.craterScale * 0.1f, worldStep * genScale * m_settings.craterScale * 0.1f, m_settings.seed + 55);
        
        m_temperatureNoise->GenUniformGrid2D(bufferTemperature.data(), worldStartX * genScale * m_settings.biomeMapScale, worldStartZ * genScale * m_settings.biomeMapScale, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.biomeMapScale, worldStep * genScale * m_settings.biomeMapScale, m_settings.seed + 2);
        
        m_moistureNoise->GenUniformGrid2D(bufferMoisture.data(), worldStartX * genScale * m_settings.biomeMapScale, worldStartZ * genScale * m_settings.biomeMapScale, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.biomeMapScale, worldStep * genScale * m_settings.biomeMapScale, m_settings.seed + 3);

        // --- PHASE 2: Process Heightmaps & Biomes ---
        for (int i = 0; i < size2D; i++) {
            // 2a. Calculate Mega Peak influence
            float megaVal = bufferMegaPeak[i];
            float megaBoost = 0.0f;
            if (megaVal > m_settings.megaPeakThreshold) {
                float factor = (megaVal - m_settings.megaPeakThreshold) / (1.0f - m_settings.megaPeakThreshold);
                megaBoost = (factor * factor) * m_settings.megaPeakHeight;
            }

            // 2b. Calculate Crater influence
            float craterVal = bufferCrater[i];
            float craterMod = 0.0f;
            if (craterVal > m_settings.craterTriggerThreshold) {
                float factor = (craterVal - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
                float shape = std::sin(factor * 3.14159f); 
                float lift = shape * m_settings.craterFloorLift;
                float rim = shape * (m_settings.craterDepth * m_settings.craterRimWidth);
                float dig = (factor * factor) * m_settings.craterDepth;
                craterMod = lift + rim - dig;
            }

            // 2c. Combine Heights
            float mntPower = std::pow(std::abs(bufferMountain[i]), 3);
            float finalH = m_settings.minimumHeight + (bufferHeightMap[i] * m_settings.hillAmplitude) + (mntPower * m_settings.mountainAmplitude) + megaBoost + craterMod;
            mapFinalHeight[i] = (int)std::clamp(finalH, 0.0f, (float)m_settings.maxWorldHeight);

            // 2d. Determine Biome
            float temp = bufferTemperature[i] - (mapFinalHeight[i] - m_settings.seaLevel) * 0.005f;
            float moist = bufferMoisture[i];

            uint8_t biome = 0; 
            if (temp > 0.4f && moist < -0.2f) biome = 2; // Desert
            else if (temp < -0.3f || mapFinalHeight[i] > 220) biome = 3; // Snow/Peak
            else if (moist > 0.2f) biome = 1; // Forest
            mapBiomeID[i] = biome;

            // 2e. Tree Placement Plan (Only LOD 1)
            // We store the tree type in a 2D map so the 3D loop can check "Is there a tree root at x,z?"
            mapTreeData[i] = 0;
            if (lodScale == 1) {
                int absX = (cx * CHUNK_SIZE - 1) + (i % PADDED_CHUNK_SIZE);
                int absZ = (cz * CHUNK_SIZE - 1) + (i / PADDED_CHUNK_SIZE);
                
                int chance = 0; uint8_t treeType = 0;
                if (biome == 1)      { chance = m_settings.treeChanceForest; treeType = 1; }
                else if (biome == 0) { chance = m_settings.treeChancePlains; treeType = 1; }
                else if (biome == 2) { chance = m_settings.treeChanceDesert; treeType = 3; }
                else if (biome == 3) { chance = 60; treeType = 2; }

                if (mapFinalHeight[i] > m_settings.seaLevel && mapFinalHeight[i] < 200 && chance > 0) {
                    if ((std::abs(PseudoRandomHash(absX, absZ, m_settings.seed)) % chance) == 0) {
                        mapTreeData[i] = treeType;
                    }
                }
            }
        }

        // --- PHASE 3: Generate 3D Noise (Caves) ---
        if (lodScale == 1) {
            float caveScale = 0.03f;
            m_caveNoise->GenUniformGrid3D(bufferCave3D.data(), 
                worldStartX * caveScale,   
                worldStartZ * caveScale,   
                worldStartY * caveScale,   
                PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE,                  
                caveScale, caveScale, caveScale,                 
                m_settings.seed);
        }

        // --- PHASE 4: Fill Voxels (The 3D Loop) ---
        uint8_t* voxels = chunk->voxels;
        
        for (int y = 0; y < PADDED_CHUNK_SIZE; y++) {
            int currentWorldY = (int)worldStartY + (y * lodScale);
            
            for (int z = 0; z < PADDED_CHUNK_SIZE; z++) {
                for (int x = 0; x < PADDED_CHUNK_SIZE; x++) {
                    int idx2D = x + (z * PADDED_CHUNK_SIZE);
                    int idx3D = x + (z * PADDED_CHUNK_SIZE) + (y * PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE); 
                    
                    int h = mapFinalHeight[idx2D];
                    uint8_t biome = mapBiomeID[idx2D];
                    uint8_t block = 0;

                    // A. Solid Ground Logic
                    if (currentWorldY <= m_settings.bedrockDepth && currentWorldY >= 0) {
                        block = 19; // Bedrock
                    } else if (currentWorldY <= h) {
                        block = 3; // Stone Base
                        int depth = h - currentWorldY;
                        
                        // Topsoil logic
                        if (depth < lodScale) {
                            block = (biome == 2) ? 5 : (biome == 3 ? 4 : 1);
                        } else if (depth < (4 * lodScale)) {
                            block = (biome == 2) ? 11 : (biome == 3 ? 4 : 2);
                        }
                        
                        // Mountain caps
                        if (currentWorldY > 220) {
                            if (depth < lodScale) block = 4;
                            else if (depth < (10 * lodScale)) block = 12;
                            else block = 19;
                        }

                        // Volcanic/Crater Surface Replacement
                        float craterVal = bufferCrater[idx2D];
                        if (craterVal > m_settings.craterTriggerThreshold && currentWorldY > m_settings.bedrockDepth) {
                             if (block != 0 && block != 6 && block != 19) {
                                 float factor = (craterVal - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
                                 if (factor > 0.2f) block = 19; // Obsidian
                                 else block = 19; // Rim Rock
                             }
                        }

                        // Apply Caves (Carve air out of stone)
                        if (lodScale == 1 && currentWorldY > m_settings.bedrockDepth) {
                             int depthFromSurface = h - currentWorldY;
                             float surfaceBias = 0.0f;
                             if (depthFromSurface < 8) surfaceBias = (8 - depthFromSurface) * 0.08f; 
                             
                             if ((bufferCave3D[idx3D] - surfaceBias) > 0.4f) block = 0;
                        }
                    } else if (currentWorldY <= m_settings.seaLevel) {
                        block = (biome == 3) ? 12 : 6; // Water or Ice
                    }

                    // B. Tree & Decoration Logic
                    if (lodScale == 1 && block == 0) {
                        // Check if THIS block is a tree trunk source
                        int treeTypeHere = mapTreeData[idx2D];
                        int relY = currentWorldY - h;
                        
                        // Verify we haven't been "caved out" under the tree
                        if (treeTypeHere > 0 && relY > 0) {
                            int localGroundY = h - (int)worldStartY;
                            if (localGroundY >= 0 && localGroundY < PADDED_CHUNK_SIZE) {
                                int idxGround = x + (z*PADDED_CHUNK_SIZE) + (localGroundY*PADDED_CHUNK_SIZE*PADDED_CHUNK_SIZE);
                                float biasAtSurface = 8.0f * 0.08f;
                                if ((bufferCave3D[idxGround] - biasAtSurface) > 0.4f) treeTypeHere = 0;
                            }
                        }

                        // Don't spawn trees on volcanic rock/craters
                        if (treeTypeHere > 0) {
                            if (bufferCrater[idx2D] > m_settings.craterTriggerThreshold) treeTypeHere = 0;
                            
                            // Place Trunk
                            if (treeTypeHere > 0) {
                                if ((treeTypeHere == 1 || treeTypeHere == 2) && relY > 0 && relY < 6) block = (treeTypeHere == 2) ? 15 : 13;
                                else if (treeTypeHere == 3 && relY > 0 && relY < 4) block = 14;
                            }
                        }
                        
                        // Check Neighbors for Leaves (if not a trunk)
                        if (block == 0) {
                            for (int nz = -1; nz <= 1; nz++) {
                                for (int nx = -1; nx <= 1; nx++) {
                                    if (nx == 0 && nz == 0) continue; // Skip self
                                    
                                    // Check bounds within the chunk array
                                    if (x+nx>=0 && x+nx<PADDED_CHUNK_SIZE && z+nz>=0 && z+nz<PADDED_CHUNK_SIZE) {
                                        int ni = (x+nx) + (z+nz)*PADDED_CHUNK_SIZE;
                                        int nTree = mapTreeData[ni];
                                        int nRelY = currentWorldY - mapFinalHeight[ni];
                                        
                                        // Oak Leaves
                                        if (nTree == 1 && nRelY >= 4 && nRelY <= 6) block = 14;
                                        // Pine Leaves
                                        else if (nTree == 2 && nRelY >= 3 && nRelY <= 6) block = 16;
                                    }
                                }
                            }
                        }
                    }
                    
                    // Final Write
                    voxels[idx3D] = block;
                }
            }
        }
    }
    
    // --------------------------------------------------------------------------------------------
    // GUI / DEBUG
    // --------------------------------------------------------------------------------------------
    // Renders the ImGui window for live-tuning the generator settings.
    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Advanced Gen Settings");
        bool changed = false;
        
        if (ImGui::CollapsingHeader("Global & Limits", ImGuiTreeNodeFlags_DefaultOpen)) {
            if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
            if(ImGui::SliderFloat("Scale", &m_settings.coordinateScale, 0.001f, 0.1f)) changed = true;
            if(ImGui::SliderInt("Max Height", &m_settings.maxWorldHeight, 50, 2048)) changed = true; 
            if(ImGui::SliderInt("Sea Level", &m_settings.seaLevel, 0, 200)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("Features & Biomes")) {
             if(ImGui::SliderFloat("Hill Freq", &m_settings.hillFrequency, 1.0f, 10.0f)) changed = true;
             if(ImGui::SliderFloat("Hill Amp", &m_settings.hillAmplitude, 10.0f, 200.0f)) changed = true;
             if(ImGui::SliderFloat("Mnt Freq", &m_settings.mountainFrequency, 0.1f, 5.0f)) changed = true;
             if(ImGui::SliderFloat("Mnt Amp", &m_settings.mountainAmplitude, 10.0f, 200.0f)) changed = true;
             if(ImGui::SliderFloat("Biome Size", &m_settings.biomeMapScale, 0.01f, 0.5f)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("Mega Craters")) {
            if(ImGui::SliderFloat("Crater Scale (Lower=Big)", &m_settings.craterScale, 0.01f, 2.0f)) changed = true;
            
            // UI Logic: Invert threshold so Slider represents "Chance"
            float rarityChance = 1.0f - m_settings.craterTriggerThreshold;
            if(ImGui::SliderFloat("Crater Chance", &rarityChance, 0.01f, 0.9f)) {
                m_settings.craterTriggerThreshold = 1.0f - rarityChance;
                changed = true;
            }

            if(ImGui::SliderFloat("Crater Depth", &m_settings.craterDepth, 1.0f, 500.0f)) changed = true;
            if(ImGui::SliderFloat("Crater Rim Height", &m_settings.craterRimWidth, 0.0f, 1.0f)) changed = true;
            if(ImGui::SliderFloat("Crater Lift (Volcano)", &m_settings.craterFloorLift, 0.0f, 2000.0f)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("Mega Peaks")) {
            if(ImGui::SliderFloat("Peak Amp", &m_settings.megaPeakHeight, 0.0f, 4000.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Rarity", &m_settings.megaPeakRarity, 0.01f, 1.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Thresh", &m_settings.megaPeakThreshold, 0.1f, 0.95f)) changed = true;
        }
        
        // If anything changed, re-initialize the noise generators with new settings
        if (changed) { m_dirty = true; Init(); }
#endif
    }

private:
    GenSettings m_settings;
    
    // FastNoise Smart Pointers (Auto-memory managed)
    FastNoise::SmartNode<> m_baseTerrainNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_temperatureNoise;
    FastNoise::SmartNode<> m_moistureNoise;
    FastNoise::SmartNode<> m_caveNoise;
    FastNoise::SmartNode<> m_megaPeakNoise; 
    FastNoise::SmartNode<> m_craterNoise; 
};