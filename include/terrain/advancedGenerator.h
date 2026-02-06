#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <FastNoise/FastNoise.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ================================================================================================
// ADVANCED GENERATOR v2 — Minecraft-Style Biome System
// ================================================================================================
//
// BIOME SYSTEM:
//   Uses 4 noise axes: Temperature, Moisture, Continentalness, Erosion
//   These are combined to select from ~15 distinct biomes, similar to Minecraft 1.18+.
//
// BIOME TABLE (biome ID → name):
//   0  = Plains
//   1  = Forest
//   2  = Desert
//   3  = Snowy Plains
//   4  = Birch Forest
//   5  = Taiga (Spruce)
//   6  = Swamp
//   7  = Jungle
//   8  = Savanna
//   9  = Badlands (Mesa)
//   10 = Dark Forest
//   11 = Flower Forest
//   12 = Beach
//   13 = Stony Shore
//   14 = Ocean
//   15 = Frozen Ocean
//   16 = Mountain Meadow
//   17 = Snowy Taiga
//
// BLOCK IDS (texture slot → description):
//   1  = Grass Top
//   2  = Dirt
//   3  = Grass Side
//   4  = Stone
//   5  = Oak Log
//   6  = Water
//   7  = Snow Block
//   8  = Ice
//   9  = Oak Leaves
//   10 = Obsidian / Bedrock
//   11 = Spruce Log
//   12 = Spruce Log Top
//   13 = Gold Ore / Deepslate
//   14 = Sand
//   15 = Sandstone
//   16 = Gravel
//   17 = Clay
//   18 = Red Sand (Badlands)
//   19 = Terracotta
//   20 = Birch Log
//   21 = Birch Leaves
//   22 = Jungle Log
//   23 = Jungle Leaves
//   24 = Dark Oak Log
//   25 = Dark Oak Leaves
//   26 = Spruce Leaves
//   27 = Podzol
//   28 = Mycelium (placeholder for swamp grass)
//   29 = Packed Ice
//   30 = Cactus
//   31 = Coal Ore
//   32 = Iron Ore
//   33 = Diamond Ore
//   34 = Cobblestone
//   35 = Mossy Cobblestone
//   36 = Flower (poppy/dandelion)
// ================================================================================================

class AdvancedGenerator : public ITerrainGenerator {
public:

    // ============================================================================================
    // BIOME ENUM
    // ============================================================================================
    enum Biome : uint8_t {
        BIOME_PLAINS          = 0,
        BIOME_FOREST          = 1,
        BIOME_DESERT          = 2,
        BIOME_SNOWY_PLAINS    = 3,
        BIOME_BIRCH_FOREST    = 4,
        BIOME_TAIGA           = 5,
        BIOME_SWAMP           = 6,
        BIOME_JUNGLE          = 7,
        BIOME_SAVANNA         = 8,
        BIOME_BADLANDS        = 9,
        BIOME_DARK_FOREST     = 10,
        BIOME_FLOWER_FOREST   = 11,
        BIOME_BEACH           = 12,
        BIOME_STONY_SHORE     = 13,
        BIOME_OCEAN           = 14,
        BIOME_FROZEN_OCEAN    = 15,
        BIOME_MOUNTAIN_MEADOW = 16,
        BIOME_SNOWY_TAIGA     = 17,
        BIOME_COUNT           = 18
    };

    static const char* BiomeName(uint8_t b) {
        static const char* names[] = {
            "Plains", "Forest", "Desert", "Snowy Plains", "Birch Forest",
            "Taiga", "Swamp", "Jungle", "Savanna", "Badlands",
            "Dark Forest", "Flower Forest", "Beach", "Stony Shore",
            "Ocean", "Frozen Ocean", "Mountain Meadow", "Snowy Taiga"
        };
        if (b < BIOME_COUNT) return names[b];
        return "Unknown";
    }

    // ============================================================================================
    // BLOCK IDS
    // ============================================================================================
    enum BlockID : uint8_t {
        AIR              = 0,
        GRASS_TOP        = 1,
        DIRT             = 2,
        GRASS_SIDE       = 3,   // unused in voxels, kept for texture compat
        STONE            = 4,
        OAK_LOG          = 5,
        WATER            = 6,
        SNOW_BLOCK       = 7,
        ICE              = 8,
        OAK_LEAVES       = 9,
        BEDROCK          = 10,
        SPRUCE_LOG       = 11,
        SPRUCE_LOG_TOP   = 12,
        DEEPSLATE        = 13,
        SAND             = 14,
        SANDSTONE        = 15,
        GRAVEL           = 16,
        CLAY             = 17,
        RED_SAND         = 18,
        TERRACOTTA       = 19,
        BIRCH_LOG        = 20,
        BIRCH_LEAVES     = 21,
        JUNGLE_LOG       = 22,
        JUNGLE_LEAVES    = 23,
        DARK_OAK_LOG     = 24,
        DARK_OAK_LEAVES  = 25,
        SPRUCE_LEAVES    = 26,
        PODZOL           = 27,
        SWAMP_GRASS      = 28,
        PACKED_ICE       = 29,
        CACTUS           = 30,
        COAL_ORE         = 31,
        IRON_ORE         = 32,
        DIAMOND_ORE      = 33,
        COBBLESTONE      = 34,
        MOSSY_COBBLE     = 35,
        FLOWER           = 36,
        GLASS_RED        = 37,
        GLASS_BLUE       = 38,
        OAK_LOG_TOP      = 39,
        BIRCH_LOG_TOP    = 40,
        JUNGLE_LOG_TOP   = 41,
        DARK_OAK_LOG_TOP = 42,
        CACTUS_TOP       = 43,
        SANDSTONE_BLOCK  = 44,
        DRY_GRASS        = 45,
        GOLD_ORE         = 46,
        SNOW_SIDE        = 47,
        COPPER_ORE       = 48,
        RIVER_WATER      = 49,
        BLOCK_COUNT      = 50
    };

    // ============================================================================================
    // SETTINGS
    // ============================================================================================
    struct GenSettings {
        int   seed              = 1804289383;
        float coordinateScale   = 0.05f;        // Broader features for long render distance

        // --- Base Terrain ---
        float minimumHeight     = 80.0f;        // High base so caves have room below surface
        float hillAmplitude     = 50.0f;
        float hillFrequency     = 3.0f;         // Lower = broader rolling hills
        float mountainAmplitude = 120.0f;
        float mountainFrequency = 0.8f;

        // --- Continentalness (land vs ocean) ---
        float continentScale    = 0.015f;       // Large continents
        float continentThreshold = -0.1f;       // Slightly more land than ocean
        float oceanDepth        = 30.0f;
        float beachWidth        = 0.06f;

        // --- Erosion (flatness vs mountains) ---
        float erosionScale      = 0.03f;
        float erosionPower      = 1.8f;

        // --- Mega Peaks ---
        float megaPeakRarity    = 0.4f;
        float megaPeakHeight    = 600.0f;
        float megaPeakThreshold = 0.75f;

        // --- World Limits ---
        int   seaLevel          = 64;
        int   maxWorldHeight    = 2048;
        int   bedrockDepth      = 3;
        int   deepslateLevel    = 24;

        // --- Biome Noise ---
        float temperatureScale  = 0.04f;        // Large biome regions
        float moistureScale     = 0.045f;
        float weirdnessScale    = 0.035f;

        // --- Cave System ---
        float caveScale         = 0.025f;       // Slightly larger cave pockets
        float caveThreshold     = 0.28f;        // Lower = caves visible (was 0.42!)
        float caveSurfaceBias   = 0.1f;
        int   caveSurfaceDepth  = 10;
        float spaghettiScale    = 0.012f;
        float spaghettiThresh   = 0.7f;         // Lower = more tunnels (was 0.85!)

        // --- Ore Generation ---
        float oreScale          = 0.08f;
        float coalChance        = 0.06f;
        float ironChance        = 0.08f;
        float goldChance        = 0.09f;
        float diamondChance     = 0.095f;
        float copperChance      = 0.07f;
        int   diamondMaxY       = 24;
        int   goldMaxY          = 40;
        int   copperMaxY        = 56;
        int   ironMaxY          = 72;
        int   coalMaxY          = 140;

        // --- Vegetation ---
        int   treeChanceForest      = 55;
        int   treeChancePlains      = 140;
        int   treeChanceDesert      = 350;
        int   treeChanceTaiga       = 45;
        int   treeChanceJungle      = 28;
        int   treeChanceDarkForest  = 22;
        int   treeChanceBirch       = 50;
        int   treeChanceSavanna     = 160;
        int   treeChanceSwamp       = 90;
        int   treeChanceSnowyTaiga  = 55;
        int   treeChanceFlower      = 65;
        int   flowerChance          = 12;
        int   maxTreeLOD            = 2;

        // --- Badlands Layering ---
        float badlandsLayerScale    = 0.15f;
        int   badlandsStripeLayers  = 6;
    };


    // ============================================================================================
    // CONSTRUCTORS
    // ============================================================================================
    AdvancedGenerator() : m_settings(GenSettings()) { Init(); }
    AdvancedGenerator(int seed) { m_settings = GenSettings(); m_settings.seed = seed; Init(); }
    AdvancedGenerator(GenSettings settings) : m_settings(settings) { Init(); }

    // ============================================================================================
    // INIT
    // ============================================================================================
    void Init() override {
        // Base terrain: FBm Perlin
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnHeightFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnHeightFractal->SetSource(fnPerlin);
        fnHeightFractal->SetOctaveCount(5);
        fnHeightFractal->SetGain(0.5f);
        fnHeightFractal->SetLacunarity(2.0f);
        m_baseTerrainNoise = fnHeightFractal;

        // Mountain: FBm Simplex
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnMountainFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnMountainFractal->SetSource(fnSimplex);
        fnMountainFractal->SetOctaveCount(4);
        m_mountainNoise = fnMountainFractal;

        // Biome axes
        m_temperatureNoise   = FastNoise::New<FastNoise::Perlin>();
        m_moistureNoise      = FastNoise::New<FastNoise::Perlin>();
        m_weirdnessNoise     = FastNoise::New<FastNoise::Perlin>();

        // Continentalness & Erosion
        auto fnContinent = FastNoise::New<FastNoise::Simplex>();
        auto fnContFBm   = FastNoise::New<FastNoise::FractalFBm>();
        fnContFBm->SetSource(fnContinent);
        fnContFBm->SetOctaveCount(3);
        fnContFBm->SetGain(0.4f);
        m_continentNoise = fnContFBm;

        m_erosionNoise = FastNoise::New<FastNoise::Perlin>();

        // Caves (cheese + spaghetti)
        m_caveNoise      = FastNoise::New<FastNoise::Perlin>();
        m_spaghettiNoise = FastNoise::New<FastNoise::Simplex>();

        // Features
        m_megaPeakNoise = FastNoise::New<FastNoise::Simplex>();

        // Ores
        m_oreNoise = FastNoise::New<FastNoise::Perlin>();

        // Badlands stripes
        m_badlandsNoise = FastNoise::New<FastNoise::Perlin>();
    }

    // ============================================================================================
    // TEXTURES
    // ============================================================================================
    std::vector<std::string> GetTexturePaths() const override {
        // MoreLikeMinecraft texture pack (Minetest naming convention)
        // Download: https://github.com/nebulimity/MoreLikeMinecraft
        // Place into: resources/textures/MoreLikeMinecraft/
        const std::string B = "resources/textures/MoreLikeMinecraft/default/";
        const std::string F = "resources/textures/MoreLikeMinecraft/flowers/";
        std::vector<std::string> texturePaths = {
            B + "default_grass.png",              // ID  1: GRASS_TOP
            B + "default_dirt.png",               // ID  2: DIRT
            B + "default_grass_side.png",         // ID  3: GRASS_SIDE (texture-only)
            B + "default_stone.png",              // ID  4: STONE
            B + "default_tree.png",               // ID  5: OAK_LOG (bark)
            B + "default_water.png",              // ID  6: WATER
            B + "default_snow.png",               // ID  7: SNOW_BLOCK
            B + "default_ice.png",                // ID  8: ICE
            B + "default_leaves.png",             // ID  9: OAK_LEAVES
            B + "default_obsidian.png",           // ID 10: BEDROCK
            B + "default_pine_tree.png",          // ID 11: SPRUCE_LOG (bark)
            B + "default_pine_tree_top.png",      // ID 12: SPRUCE_LOG_TOP (cross-section)
            B + "default_obsidian.png",           // ID 13: DEEPSLATE (reuse obsidian)
            B + "default_sand.png",               // ID 14: SAND
            B + "default_sandstone.png",          // ID 15: SANDSTONE
            B + "default_gravel.png",             // ID 16: GRAVEL
            B + "default_clay.png",               // ID 17: CLAY
            B + "default_sand.png",               // ID 18: RED_SAND (fallback: sand)
            B + "default_stone.png",              // ID 19: TERRACOTTA (fallback: stone)
            B + "default_aspen_tree.png",         // ID 20: BIRCH_LOG (bark)
            B + "default_aspen_leaves.png",       // ID 21: BIRCH_LEAVES
            B + "default_jungletree.png",         // ID 22: JUNGLE_LOG (bark)
            B + "default_jungleleaves.png",       // ID 23: JUNGLE_LEAVES
            B + "default_acacia_tree.png",        // ID 24: DARK_OAK_LOG (bark)
            B + "default_acacia_leaves.png",      // ID 25: DARK_OAK_LEAVES
            B + "default_leaves.png",             // ID 26: SPRUCE_LEAVES (fallback: oak leaves)
            B + "default_dirt.png",               // ID 27: PODZOL (using dirt)
            B + "default_grass.png",              // ID 28: SWAMP_GRASS (fallback: grass)
            B + "default_ice.png",                // ID 29: PACKED_ICE (reuse ice)
            B + "default_cactus_side.png",        // ID 30: CACTUS (side)
            B + "default_mineral_coal.png",       // ID 31: COAL_ORE
            B + "default_mineral_iron.png",       // ID 32: IRON_ORE
            B + "default_mineral_diamond.png",    // ID 33: DIAMOND_ORE
            B + "default_cobble.png",             // ID 34: COBBLESTONE
            B + "default_mossycobble.png",        // ID 35: MOSSY_COBBLE
            F + "flowers_rose.png",               // ID 36: FLOWER
            B + "default_obsidian_glass.png",     // ID 37: GLASS_RED
            B + "default_glass.png",              // ID 38: GLASS_BLUE
            B + "default_tree_top.png",           // ID 39: OAK_LOG_TOP (cross-section)
            B + "default_aspen_tree_top.png",     // ID 40: BIRCH_LOG_TOP
            B + "default_jungletree_top.png",     // ID 41: JUNGLE_LOG_TOP
            B + "default_acacia_tree_top.png",    // ID 42: DARK_OAK_LOG_TOP
            B + "default_cactus_top.png",         // ID 43: CACTUS_TOP
            B + "default_sandstone.png",          // ID 44: SANDSTONE_BLOCK (fallback: sandstone)
            B + "default_grass.png",              // ID 45: DRY_GRASS (fallback: grass)
            B + "default_mineral_gold.png",       // ID 46: GOLD_ORE
            B + "default_snow.png",               // ID 47: SNOW_SIDE
            B + "default_mineral_iron.png",       // ID 48: COPPER_ORE (fallback: iron ore)
            B + "default_river_water.png",        // ID 49: RIVER_WATER
        };
        return texturePaths;
    }

    // ============================================================================================
    // HASH UTILITIES
    // ============================================================================================
    inline int PseudoRandomHash(int x, int z, int seed) const {
        int h = seed + x * 374761393 + z * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h ^ (h >> 16));
    }

    inline int PseudoRandomHash3D(int x, int y, int z, int seed) const {
        int h = seed + x * 374761393 + y * 668265263 + z * 432352357;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h ^ (h >> 16));
    }

    inline float HashFloat(int x, int z, int seed) const {
        int val = PseudoRandomHash(x, z, seed);
        return (val & 0xFFFF) / 65535.0f;
    }

    // ============================================================================================
    // BIOME SELECTION
    // ============================================================================================
    // Given temperature, moisture, continentalness, erosion, weirdness, and height
    // select the biome ID. This mirrors Minecraft's multi-parameter biome placement.
    uint8_t SelectBiome(float temp, float moist, float continent, float erosion,
                        float weirdness, int surfaceHeight, int seaLevel) const {

        // Ocean determination: low continentalness
        bool isOcean = (continent < m_settings.continentThreshold);
        bool isBeach = (!isOcean && continent < m_settings.continentThreshold + m_settings.beachWidth
                        && surfaceHeight <= seaLevel + 3);

        if (isOcean) {
            if (temp < -0.3f) return BIOME_FROZEN_OCEAN;
            return BIOME_OCEAN;
        }

        if (isBeach && surfaceHeight <= seaLevel + 3) {
            return BIOME_BEACH;
        }

        // High altitude override
        bool isHighAlt = (surfaceHeight > seaLevel + 160) || (erosion < -0.5f && surfaceHeight > seaLevel + 80);
        if (isHighAlt) {
            if (temp < -0.2f) return BIOME_SNOWY_TAIGA;
            return BIOME_MOUNTAIN_MEADOW;
        }

        // Stony shore (steep coast)
        if (continent < m_settings.continentThreshold + m_settings.beachWidth * 2.0f
            && erosion < -0.3f && surfaceHeight > seaLevel + 5) {
            return BIOME_STONY_SHORE;
        }

        // Main biome grid (temperature × moisture with weirdness variants)
        // ─────────────────────────────────────────────
        // Temperature axis:  <-0.5 frozen | -0.5..0 cold | 0..0.4 temperate | >0.4 hot
        // Moisture axis:     <-0.3 dry | -0.3..0.2 med | >0.2 wet

        if (temp < -0.5f) {
            // FROZEN
            if (moist > 0.2f)   return BIOME_SNOWY_TAIGA;
            return BIOME_SNOWY_PLAINS;
        }

        if (temp < 0.0f) {
            // COLD
            if (moist > 0.3f)   return BIOME_TAIGA;
            if (moist > 0.0f)   return (weirdness > 0.3f) ? BIOME_DARK_FOREST : BIOME_TAIGA;
            return BIOME_PLAINS;
        }

        if (temp < 0.4f) {
            // TEMPERATE
            if (moist > 0.4f)   return (weirdness > 0.2f) ? BIOME_DARK_FOREST : BIOME_FOREST;
            if (moist > 0.1f)   return (weirdness > 0.4f) ? BIOME_FLOWER_FOREST : BIOME_BIRCH_FOREST;
            if (moist > -0.2f)  return BIOME_PLAINS;
            return (weirdness > 0.3f) ? BIOME_SAVANNA : BIOME_PLAINS;
        }

        // HOT
        if (moist < -0.3f) {
            if (weirdness > 0.3f) return BIOME_BADLANDS;
            return BIOME_DESERT;
        }
        if (moist < 0.0f)  return BIOME_SAVANNA;
        if (moist > 0.3f)  return BIOME_JUNGLE;
        return (weirdness < -0.2f) ? BIOME_SWAMP : BIOME_SAVANNA;
    }

    // ============================================================================================
    // SURFACE BLOCK SELECTION PER BIOME
    // ============================================================================================
    struct SurfaceBlocks {
        uint8_t topBlock;
        uint8_t fillerBlock;
        int     fillerDepth;    // how many blocks below surface use filler
        uint8_t underwaterBlock;
    };

    SurfaceBlocks GetSurfaceBlocks(uint8_t biome) const {
        switch (biome) {
            case BIOME_PLAINS:          return { GRASS_TOP,  DIRT,       4, DIRT };
            case BIOME_FOREST:          return { GRASS_TOP,  DIRT,       4, DIRT };
            case BIOME_DESERT:          return { SAND,       SANDSTONE,  6, SAND };
            case BIOME_SNOWY_PLAINS:    return { SNOW_BLOCK, DIRT,       4, GRAVEL };
            case BIOME_BIRCH_FOREST:    return { GRASS_TOP,  DIRT,       4, DIRT };
            case BIOME_TAIGA:           return { PODZOL,     DIRT,       4, GRAVEL };
            case BIOME_SWAMP:           return { SWAMP_GRASS,CLAY,       3, CLAY };
            case BIOME_JUNGLE:          return { GRASS_TOP,  DIRT,       5, DIRT };
            case BIOME_SAVANNA:         return { DRY_GRASS,   DIRT,       3, DIRT };
            case BIOME_BADLANDS:        return { RED_SAND,   TERRACOTTA, 8, RED_SAND };
            case BIOME_DARK_FOREST:     return { GRASS_TOP,  DIRT,       4, DIRT };
            case BIOME_FLOWER_FOREST:   return { GRASS_TOP,  DIRT,       4, DIRT };
            case BIOME_BEACH:           return { SAND,       SAND,       5, SAND };
            case BIOME_STONY_SHORE:     return { STONE,      STONE,      8, GRAVEL };
            case BIOME_OCEAN:           return { GRAVEL,     DIRT,       3, GRAVEL };
            case BIOME_FROZEN_OCEAN:    return { GRAVEL,     DIRT,       3, GRAVEL };
            case BIOME_MOUNTAIN_MEADOW: return { GRASS_TOP,  STONE,      2, GRAVEL };
            case BIOME_SNOWY_TAIGA:     return { SNOW_BLOCK, PODZOL,     3, GRAVEL };
            default:                    return { GRASS_TOP,  DIRT,       4, DIRT };
        }
    }

    // ============================================================================================
    // TREE PROPERTIES PER BIOME
    // ============================================================================================
    struct TreeProps {
        int     chance;         // 1/N per-block chance
        uint8_t logBlock;
        uint8_t leafBlock;
        int     baseHeight;
        int     heightVariance;
        float   canopyRadius;
        bool    isCactus;       // Special handling for desert
        bool    tallVariant;    // Tall jungle/dark oak trees
    };

    TreeProps GetTreeProps(uint8_t biome) const {
        switch (biome) {
            case BIOME_FOREST:          return { m_settings.treeChanceForest,     OAK_LOG,     OAK_LEAVES,      5, 3, 2.5f, false, false };
            case BIOME_BIRCH_FOREST:    return { m_settings.treeChanceBirch,      BIRCH_LOG,   BIRCH_LEAVES,    6, 2, 2.2f, false, false };
            case BIOME_TAIGA:           return { m_settings.treeChanceTaiga,      SPRUCE_LOG,  SPRUCE_LEAVES,   7, 3, 2.0f, false, false };
            case BIOME_DARK_FOREST:     return { m_settings.treeChanceDarkForest, DARK_OAK_LOG,DARK_OAK_LEAVES, 6, 3, 3.5f, false, true  };
            case BIOME_JUNGLE:          return { m_settings.treeChanceJungle,     JUNGLE_LOG,  JUNGLE_LEAVES,   8, 6, 3.0f, false, true  };
            case BIOME_SAVANNA:         return { m_settings.treeChanceSavanna,    OAK_LOG,     OAK_LEAVES,      5, 2, 3.5f, false, false };
            case BIOME_SWAMP:           return { m_settings.treeChanceSwamp,      OAK_LOG,     OAK_LEAVES,      4, 2, 3.0f, false, false };
            case BIOME_SNOWY_TAIGA:     return { m_settings.treeChanceSnowyTaiga, SPRUCE_LOG,  SPRUCE_LEAVES,   6, 3, 2.0f, false, false };
            case BIOME_FLOWER_FOREST:   return { m_settings.treeChanceFlower,     BIRCH_LOG,   BIRCH_LEAVES,    5, 2, 2.2f, false, false };
            case BIOME_PLAINS:          return { m_settings.treeChancePlains,     OAK_LOG,     OAK_LEAVES,      5, 3, 2.5f, false, false };
            case BIOME_SNOWY_PLAINS:    return { 200,                             SPRUCE_LOG,  SPRUCE_LEAVES,   5, 2, 2.0f, false, false };
            case BIOME_DESERT:          return { m_settings.treeChanceDesert,     CACTUS,      AIR,             3, 2, 0.0f, true,  false };
            case BIOME_MOUNTAIN_MEADOW: return { 200,                             SPRUCE_LOG,  SPRUCE_LEAVES,   4, 2, 1.8f, false, false };
            default:                    return { 0, AIR, AIR, 0, 0, 0.0f, false, false };
        }
    }

    // ============================================================================================
    // HEIGHT CALCULATION
    // ============================================================================================
    int GetHeight(float x, float z) const {
        float normalizedX = x * m_settings.coordinateScale;
        float normalizedZ = z * m_settings.coordinateScale;

        float baseHeightVal = m_baseTerrainNoise->GenSingle2D(
            normalizedX * m_settings.hillFrequency,
            normalizedZ * m_settings.hillFrequency, m_settings.seed);
        float mountainVal = m_mountainNoise->GenSingle2D(
            normalizedX * 0.5f * m_settings.mountainFrequency,
            normalizedZ * 0.5f * m_settings.mountainFrequency, m_settings.seed + 1);

        // Continentalness
        float continent = m_continentNoise->GenSingle2D(
            normalizedX * m_settings.continentScale,
            normalizedZ * m_settings.continentScale, m_settings.seed + 10);

        // Erosion modulates mountain influence
        float erosion = m_erosionNoise->GenSingle2D(
            normalizedX * m_settings.erosionScale,
            normalizedZ * m_settings.erosionScale, m_settings.seed + 11);

        // Mega peaks
        float megaPeakZone = m_megaPeakNoise->GenSingle2D(
            normalizedX * m_settings.megaPeakRarity * 0.1f,
            normalizedZ * m_settings.megaPeakRarity * 0.1f, m_settings.seed + 99);
        float megaPeakBoost = 0.0f;
        if (megaPeakZone > m_settings.megaPeakThreshold) {
            float factor = (megaPeakZone - m_settings.megaPeakThreshold) / (1.0f - m_settings.megaPeakThreshold);
            megaPeakBoost = (factor * factor) * m_settings.megaPeakHeight;
        }


        // Mountain factor modulated by erosion
        float mountainFactor = std::abs(mountainVal);
        mountainFactor = mountainFactor * mountainFactor * mountainFactor;
        float erosionMul = std::clamp((erosion + 1.0f) * 0.5f, 0.0f, 1.0f);
        erosionMul = std::pow(erosionMul, m_settings.erosionPower);
        mountainFactor *= erosionMul;

        // Ocean depth
        float oceanDip = 0.0f;
        if (continent < m_settings.continentThreshold) {
            float depth = (m_settings.continentThreshold - continent) / (m_settings.continentThreshold + 1.0f);
            oceanDip = -depth * m_settings.oceanDepth;
        }

        float finalHeight = m_settings.minimumHeight
                          + (baseHeightVal * m_settings.hillAmplitude)
                          + (mountainFactor * m_settings.mountainAmplitude)
                          + megaPeakBoost
                          + oceanDip;

        return (int)std::clamp(finalHeight, 0.0f, (float)m_settings.maxWorldHeight);
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        minH = 0;
        maxH = m_settings.maxWorldHeight;
    }

    // ============================================================================================
    // GET BLOCK (slow single access — collision/raycast fallback)
    // ============================================================================================
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        int worldY = (int)std::floor(y);
        int surfaceHeight = GetHeight(x, z);

        if (worldY <= m_settings.bedrockDepth) return BEDROCK;

        float normalizedX = x * m_settings.coordinateScale;
        float normalizedZ = z * m_settings.coordinateScale;

        float temp   = m_temperatureNoise->GenSingle2D(normalizedX * m_settings.temperatureScale, normalizedZ * m_settings.temperatureScale, m_settings.seed + 2);
        float moist  = m_moistureNoise->GenSingle2D(normalizedX * m_settings.moistureScale, normalizedZ * m_settings.moistureScale, m_settings.seed + 3);
        float weird  = m_weirdnessNoise->GenSingle2D(normalizedX * m_settings.weirdnessScale, normalizedZ * m_settings.weirdnessScale, m_settings.seed + 4);
        float contin = m_continentNoise->GenSingle2D(normalizedX * m_settings.continentScale, normalizedZ * m_settings.continentScale, m_settings.seed + 10);
        float erosion = m_erosionNoise->GenSingle2D(normalizedX * m_settings.erosionScale, normalizedZ * m_settings.erosionScale, m_settings.seed + 11);

        float adjustedTemp = temp - std::max(0, surfaceHeight - m_settings.seaLevel) * 0.003f;
        uint8_t biome = SelectBiome(adjustedTemp, moist, contin, erosion, weird, surfaceHeight, m_settings.seaLevel);
        SurfaceBlocks sb = GetSurfaceBlocks(biome);

        // Above surface
        if (worldY > surfaceHeight) {
            if (worldY <= m_settings.seaLevel) {
                if (biome == BIOME_FROZEN_OCEAN || biome == BIOME_SNOWY_PLAINS || biome == BIOME_SNOWY_TAIGA) {
                    if (worldY == m_settings.seaLevel) return ICE;
                }
                return WATER;
            }
            return AIR;
        }

        // Underground
        int depth = surfaceHeight - worldY;
        uint8_t block = (worldY < m_settings.deepslateLevel) ? DEEPSLATE : STONE;

        if (depth < lodScale) {
            block = (surfaceHeight <= m_settings.seaLevel) ? sb.underwaterBlock : sb.topBlock;
        } else if (depth < (sb.fillerDepth * lodScale)) {
            block = sb.fillerBlock;
        }

        // Snow cap at high altitude
        if (surfaceHeight > m_settings.seaLevel + 160) {
            if (depth < lodScale) block = SNOW_BLOCK;
            else if (depth < (10 * lodScale)) block = ICE;
        }

        // Badlands striping
        if (biome == BIOME_BADLANDS && depth >= lodScale && depth < sb.fillerDepth * lodScale) {
            float stripe = m_badlandsNoise->GenSingle2D(0.0f, (float)worldY * m_settings.badlandsLayerScale, m_settings.seed + 77);
            if (stripe > 0.3f) block = RED_SAND;
            else if (stripe > 0.0f) block = TERRACOTTA;
        }


        // Caves
        if (lodScale == 1 && worldY > m_settings.bedrockDepth) {
            float caveVal = m_caveNoise->GenSingle3D(x * m_settings.caveScale, z * m_settings.caveScale, y * m_settings.caveScale, m_settings.seed);
            int depthFromSurface = surfaceHeight - worldY;
            float surfaceBias = 0.0f;
            if (depthFromSurface < m_settings.caveSurfaceDepth)
                surfaceBias = (m_settings.caveSurfaceDepth - depthFromSurface) * m_settings.caveSurfaceBias;
            if ((caveVal - surfaceBias) > m_settings.caveThreshold) return AIR;

            // Spaghetti caves
            float spag = m_spaghettiNoise->GenSingle3D(x * m_settings.spaghettiScale, z * m_settings.spaghettiScale, y * m_settings.spaghettiScale, m_settings.seed + 50);
            if (std::abs(spag) > m_settings.spaghettiThresh) return AIR;
        }

        // Ores (only at LOD 1)
        if (lodScale == 1 && block == STONE && worldY > m_settings.bedrockDepth) {
            float oreVal = m_oreNoise->GenSingle3D(x * m_settings.oreScale, z * m_settings.oreScale, y * m_settings.oreScale, m_settings.seed + 200);
            float oreVal2 = m_oreNoise->GenSingle3D(x * m_settings.oreScale * 1.5f, z * m_settings.oreScale * 1.5f, y * m_settings.oreScale * 1.5f, m_settings.seed + 201);

            if (worldY < m_settings.diamondMaxY && oreVal > (1.0f - m_settings.diamondChance * 2.0f)) block = DIAMOND_ORE;
            else if (worldY < m_settings.goldMaxY && oreVal2 > (1.0f - m_settings.goldChance * 2.0f)) block = GOLD_ORE;
            else if (worldY < m_settings.copperMaxY && oreVal > (1.0f - m_settings.copperChance * 2.0f)) block = COPPER_ORE;
            else if (worldY < m_settings.ironMaxY && oreVal > (1.0f - m_settings.ironChance * 2.0f)) block = IRON_ORE;
            else if (worldY < m_settings.coalMaxY && oreVal2 > (1.0f - m_settings.coalChance * 2.0f)) block = COAL_ORE;
        }

        return block;
    }

    // ============================================================================================
    // GENERATE CHUNK (Optimized Batch)
    // ============================================================================================
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        static thread_local std::vector<float> bufferHeightMap;
        static thread_local std::vector<float> bufferMountain;
        static thread_local std::vector<float> bufferMegaPeak;
        static thread_local std::vector<float> bufferTemperature;
        static thread_local std::vector<float> bufferMoisture;
        static thread_local std::vector<float> bufferWeirdness;
        static thread_local std::vector<float> bufferContinent;
        static thread_local std::vector<float> bufferErosion;
        static thread_local std::vector<float> bufferCave3D;
        static thread_local std::vector<float> bufferSpaghetti3D;
        static thread_local std::vector<float> bufferOre3D;
        static thread_local std::vector<float> bufferOre3D_B;
        static thread_local std::vector<int>   mapFinalHeight;
        static thread_local std::vector<uint8_t> mapBiomeID;
        static thread_local std::vector<uint8_t> mapTreeData;

        const int PADDED = CHUNK_SIZE_PADDED;
        const int size2D = PADDED * PADDED;
        const int size3D = PADDED * PADDED * PADDED;

        // Resize once
        if ((int)bufferHeightMap.size() != size2D) {
            bufferHeightMap.resize(size2D); bufferMountain.resize(size2D);
            bufferMegaPeak.resize(size2D);  
            bufferTemperature.resize(size2D); bufferMoisture.resize(size2D);
            bufferWeirdness.resize(size2D); bufferContinent.resize(size2D);
            bufferErosion.resize(size2D);
            mapFinalHeight.resize(size2D); mapBiomeID.resize(size2D); mapTreeData.resize(size2D);
            bufferCave3D.resize(size3D); bufferSpaghetti3D.resize(size3D);
            bufferOre3D.resize(size3D); bufferOre3D_B.resize(size3D);
        }

        float genScale   = m_settings.coordinateScale;
        float worldStartX = (float)((cx * CHUNK_SIZE - 1) * lodScale);
        float worldStartZ = (float)((cz * CHUNK_SIZE - 1) * lodScale);
        float worldStartY = (float)((cy * CHUNK_SIZE - 1) * lodScale);
        float worldStep   = (float)lodScale;

        // ──────────────────────────────────────────────
        // Phase 1: Batch Noise Generation
        // ──────────────────────────────────────────────
        auto gen2D = [&](FastNoise::SmartNode<>& noise, float* buf, float freqMul, int seedOff) {
            noise->GenUniformGrid2D(buf,
                worldStartX * genScale * freqMul, worldStartZ * genScale * freqMul,
                PADDED, PADDED,
                worldStep * genScale * freqMul, worldStep * genScale * freqMul,
                m_settings.seed + seedOff);
        };

        gen2D(m_baseTerrainNoise, bufferHeightMap.data(), m_settings.hillFrequency, 0);
        gen2D(m_mountainNoise,    bufferMountain.data(),  0.5f * m_settings.mountainFrequency, 1);
        gen2D(m_megaPeakNoise,    bufferMegaPeak.data(),  m_settings.megaPeakRarity * 0.1f, 99);
        gen2D(m_temperatureNoise, bufferTemperature.data(), m_settings.temperatureScale, 2);
        gen2D(m_moistureNoise,    bufferMoisture.data(),  m_settings.moistureScale, 3);
        gen2D(m_weirdnessNoise,   bufferWeirdness.data(), m_settings.weirdnessScale, 4);
        gen2D(m_continentNoise,   bufferContinent.data(), m_settings.continentScale, 10);
        gen2D(m_erosionNoise,     bufferErosion.data(),   m_settings.erosionScale, 11);

        // Phase 1b: 3D noise (caves, ores)
        if (lodScale == 1) {
            m_caveNoise->GenUniformGrid3D(bufferCave3D.data(),
                worldStartX * m_settings.caveScale, worldStartZ * m_settings.caveScale, worldStartY * m_settings.caveScale,
                PADDED, PADDED, PADDED,
                m_settings.caveScale, m_settings.caveScale, m_settings.caveScale,
                m_settings.seed);

            m_spaghettiNoise->GenUniformGrid3D(bufferSpaghetti3D.data(),
                worldStartX * m_settings.spaghettiScale, worldStartZ * m_settings.spaghettiScale, worldStartY * m_settings.spaghettiScale,
                PADDED, PADDED, PADDED,
                m_settings.spaghettiScale, m_settings.spaghettiScale, m_settings.spaghettiScale,
                m_settings.seed + 50);

            m_oreNoise->GenUniformGrid3D(bufferOre3D.data(),
                worldStartX * m_settings.oreScale, worldStartZ * m_settings.oreScale, worldStartY * m_settings.oreScale,
                PADDED, PADDED, PADDED,
                m_settings.oreScale, m_settings.oreScale, m_settings.oreScale,
                m_settings.seed + 200);

            m_oreNoise->GenUniformGrid3D(bufferOre3D_B.data(),
                worldStartX * m_settings.oreScale * 1.5f, worldStartZ * m_settings.oreScale * 1.5f, worldStartY * m_settings.oreScale * 1.5f,
                PADDED, PADDED, PADDED,
                m_settings.oreScale * 1.5f, m_settings.oreScale * 1.5f, m_settings.oreScale * 1.5f,
                m_settings.seed + 201);
        }

        // ──────────────────────────────────────────────
        // Phase 2: Heights, Biomes, Trees
        // ──────────────────────────────────────────────
        for (int i = 0; i < size2D; i++) {
            // Mega peaks
            float megaBoost = 0.0f;
            if (bufferMegaPeak[i] > m_settings.megaPeakThreshold) {
                float factor = (bufferMegaPeak[i] - m_settings.megaPeakThreshold) / (1.0f - m_settings.megaPeakThreshold);
                megaBoost = (factor * factor) * m_settings.megaPeakHeight;
            }


            // Mountain with erosion
            float mntPower = std::pow(std::abs(bufferMountain[i]), 3.0f);
            float erosionMul = std::clamp((bufferErosion[i] + 1.0f) * 0.5f, 0.0f, 1.0f);
            erosionMul = std::pow(erosionMul, m_settings.erosionPower);
            mntPower *= erosionMul;

            // Ocean dip
            float oceanDip = 0.0f;
            if (bufferContinent[i] < m_settings.continentThreshold) {
                float depth = (m_settings.continentThreshold - bufferContinent[i]) / (m_settings.continentThreshold + 1.0f);
                oceanDip = -depth * m_settings.oceanDepth;
            }

            float finalH = m_settings.minimumHeight
                         + (bufferHeightMap[i] * m_settings.hillAmplitude)
                         + (mntPower * m_settings.mountainAmplitude)
                         + megaBoost  + oceanDip;
            mapFinalHeight[i] = (int)std::clamp(finalH, 0.0f, (float)m_settings.maxWorldHeight);

            // Biome selection
            float adjTemp = bufferTemperature[i] - std::max(0, mapFinalHeight[i] - m_settings.seaLevel) * 0.003f;
            mapBiomeID[i] = SelectBiome(adjTemp, bufferMoisture[i], bufferContinent[i],
                                        bufferErosion[i], bufferWeirdness[i],
                                        mapFinalHeight[i], m_settings.seaLevel);

            // Tree placement
            mapTreeData[i] = 0;
            if (lodScale <= m_settings.maxTreeLOD) {
                int absX = (int)worldStartX + (i % PADDED) * lodScale;
                int absZ = (int)worldStartZ + (i / PADDED) * lodScale;
                uint8_t biome = mapBiomeID[i];
                TreeProps tp = GetTreeProps(biome);

                if (tp.chance > 0 && mapFinalHeight[i] > m_settings.seaLevel && mapFinalHeight[i] < m_settings.seaLevel + 180) {
                    int effectiveChance = tp.chance * lodScale;
                    if ((std::abs(PseudoRandomHash(absX, absZ, m_settings.seed)) % effectiveChance) == 0) {
                        mapTreeData[i] = biome + 1; // Store biome+1 so 0 = no tree
                    }
                }
            }
        }

        // ──────────────────────────────────────────────
        // Phase 3: Voxel Fill
        // ──────────────────────────────────────────────
        uint8_t* voxels = chunk->voxels;

        for (int y = 0; y < PADDED; y++) {
            int currentWorldY = (int)worldStartY + (y * lodScale);

            for (int z = 0; z < PADDED; z++) {
                for (int x = 0; x < PADDED; x++) {
                    int idx2D = x + (z * PADDED);
                    int idx3D = x + (z * PADDED) + (y * PADDED * PADDED);

                    int h = mapFinalHeight[idx2D];
                    uint8_t biome = mapBiomeID[idx2D];
                    SurfaceBlocks sb = GetSurfaceBlocks(biome);
                    uint8_t block = AIR;

                    // Bedrock
                    if (currentWorldY <= m_settings.bedrockDepth && currentWorldY >= 0) {
                        block = BEDROCK;
                    }
                    // Below surface
                    else if (currentWorldY <= h) {
                        block = (currentWorldY < m_settings.deepslateLevel) ? DEEPSLATE : STONE;
                        int depth = h - currentWorldY;

                        // Surface
                        if (depth < lodScale) {
                            block = (h <= m_settings.seaLevel) ? sb.underwaterBlock : sb.topBlock;
                        } else if (depth < (sb.fillerDepth * lodScale)) {
                            block = sb.fillerBlock;
                        }

                        // Snow cap
                        if (h > m_settings.seaLevel + 160) {
                            if (depth < lodScale) block = SNOW_BLOCK;
                            else if (depth < (10 * lodScale)) block = ICE;
                        }

                        // Badlands striping
                        if (biome == BIOME_BADLANDS && depth >= lodScale && depth < sb.fillerDepth * lodScale) {
                            // Use Y-based pattern for horizontal bands
                            float stripe = std::sin((float)currentWorldY * m_settings.badlandsLayerScale);
                            if (stripe > 0.3f) block = RED_SAND;
                            else if (stripe > -0.1f) block = TERRACOTTA;
                        }

                        // Volcanic

                        // Caves (LOD 1 only)
                        if (lodScale == 1 && currentWorldY > m_settings.bedrockDepth) {
                            int depthFromSurface = h - currentWorldY;
                            float surfaceBias = 0.0f;
                            if (depthFromSurface < m_settings.caveSurfaceDepth)
                                surfaceBias = (m_settings.caveSurfaceDepth - depthFromSurface) * m_settings.caveSurfaceBias;

                            // Cheese caves
                            if ((bufferCave3D[idx3D] - surfaceBias) > m_settings.caveThreshold)
                                block = AIR;

                            // Spaghetti caves
                            if (block != AIR && std::abs(bufferSpaghetti3D[idx3D]) > m_settings.spaghettiThresh)
                                block = AIR;
                        }

                        // Ores (LOD 1, only in stone)
                        if (lodScale == 1 && block == STONE && currentWorldY > m_settings.bedrockDepth) {
                            float oreA = bufferOre3D[idx3D];
                            float oreB = bufferOre3D_B[idx3D];

                            if (currentWorldY < m_settings.diamondMaxY && oreA > (1.0f - m_settings.diamondChance * 2.0f))
                                block = DIAMOND_ORE;
                            else if (currentWorldY < m_settings.goldMaxY && oreB > (1.0f - m_settings.goldChance * 2.0f))
                                block = GOLD_ORE;
                            else if (currentWorldY < m_settings.copperMaxY && oreA > (1.0f - m_settings.copperChance * 2.0f))
                                block = COPPER_ORE;
                            else if (currentWorldY < m_settings.ironMaxY && oreA > (1.0f - m_settings.ironChance * 2.0f))
                                block = IRON_ORE;
                            else if (currentWorldY < m_settings.coalMaxY && oreB > (1.0f - m_settings.coalChance * 2.0f))
                                block = COAL_ORE;
                        }
                    }
                    // Water/Ice
                    else if (currentWorldY <= m_settings.seaLevel) {
                        if ((biome == BIOME_FROZEN_OCEAN || biome == BIOME_SNOWY_PLAINS || biome == BIOME_SNOWY_TAIGA)
                            && currentWorldY == m_settings.seaLevel) {
                            block = ICE;
                        } else {
                            block = WATER;
                        }
                    }

                    voxels[idx3D] = block;
                }
            }
        }

        // ──────────────────────────────────────────────
        // Phase 4: Vegetation (Push-based)
        // ──────────────────────────────────────────────
        if (lodScale <= m_settings.maxTreeLOD) {
            for (int z = 0; z < PADDED; z++) {
                for (int x = 0; x < PADDED; x++) {
                    int idx2D = x + (z * PADDED);
                    uint8_t treeCode = mapTreeData[idx2D];

                    if (treeCode == 0) continue;

                    uint8_t biome = treeCode - 1;
                    TreeProps tp = GetTreeProps(biome);
                    if (tp.chance == 0) continue;

                    int rootH = mapFinalHeight[idx2D];
                    int localRootY = (rootH - (int)worldStartY) / lodScale;
                    if (localRootY >= PADDED) continue;

                    int absX = (int)worldStartX + (x * lodScale);
                    int absZ = (int)worldStartZ + (z * lodScale);
                    int treeSeed = PseudoRandomHash(absX, absZ, m_settings.seed);

                    // --- CACTUS ---
                    if (tp.isCactus) {
                        int cactusH = tp.baseHeight + (std::abs(treeSeed) % (tp.heightVariance + 1));
                        for (int th = 1; th <= cactusH; th++) {
                            int vy = localRootY + th;
                            if (vy >= 0 && vy < PADDED) {
                                int vIdx = x + z * PADDED + vy * PADDED * PADDED;
                                if (voxels[vIdx] == AIR) voxels[vIdx] = CACTUS;
                            }
                        }
                        continue;
                    }

                    // --- TREES ---
                    int treeHeight = tp.baseHeight + (std::abs(treeSeed) % (tp.heightVariance + 1));

                    // Trunk
                    for (int th = 1; th < treeHeight - 1; th++) {
                        int vy = localRootY + th;
                        if (vy >= 0 && vy < PADDED) {
                            int vIdx = x + z * PADDED + vy * PADDED * PADDED;
                            if (voxels[vIdx] == AIR || voxels[vIdx] == WATER)
                                voxels[vIdx] = tp.logBlock;
                        }
                    }

                    // Canopy
                    if (tp.leafBlock == AIR) continue; // No leaves (shouldn't happen for non-cactus)
                    int leavesStart = treeHeight - 4;
                    int rad = (int)std::ceil(tp.canopyRadius) + 1;

                    // Spruce/taiga: conical shape
                    bool isConical = (tp.logBlock == SPRUCE_LOG);

                    for (int ly = leavesStart; ly <= treeHeight; ly++) {
                        int vy = localRootY + ly;
                        if (vy < 0 || vy >= PADDED) continue;

                        float currentRadius;
                        if (isConical) {
                            // Conical: wide at bottom, narrow at top
                            float t = (float)(ly - leavesStart) / (float)(treeHeight - leavesStart);
                            currentRadius = tp.canopyRadius * (1.0f - t * 0.8f);
                            if (ly == treeHeight) currentRadius = 0.5f; // tip
                        } else {
                            // Round blob — centered around 60-80% tree height
                            float progress = (float)(ly - leavesStart) / (float)(treeHeight - leavesStart + 1);
                            float maxRad = tp.canopyRadius + (std::abs(treeSeed) % 100) / 200.0f;
                            // Bell curve centered at progress=0.4 (middle-low of canopy)
                            float radiusShape = 1.0f - std::pow((progress - 0.4f) * 2.0f, 2.0f);
                            currentRadius = maxRad * std::max(0.3f, radiusShape);
                        }

                        float currentRadSq = currentRadius * currentRadius;

                        for (int lz = -rad; lz <= rad; lz++) {
                            for (int lx = -rad; lx <= rad; lx++) {
                                int vx = x + lx;
                                int vz = z + lz;
                                if (vx < 0 || vx >= PADDED || vz < 0 || vz >= PADDED) continue;

                                if ((float)(lx * lx + lz * lz) < currentRadSq) {
                                    bool isEdge = (float)(lx * lx + lz * lz) > (currentRadSq * 0.6f);
                                    if (!isEdge || (PseudoRandomHash3D(
                                            (int)worldStartX + vx * lodScale,
                                            (int)worldStartY + vy * lodScale,
                                            (int)worldStartZ + vz * lodScale,
                                            m_settings.seed) % 100 < 70)) {
                                        int vIdx = vx + vz * PADDED + vy * PADDED * PADDED;
                                        if (voxels[vIdx] == AIR)
                                            voxels[vIdx] = tp.leafBlock;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Flower placement (Flower Forest biome, LOD 1 only)
            if (lodScale == 1) {
                for (int z = 1; z < PADDED - 1; z++) {
                    for (int x = 1; x < PADDED - 1; x++) {
                        int idx2D = x + z * PADDED;
                        if (mapBiomeID[idx2D] != BIOME_FLOWER_FOREST) continue;
                        int absX = (int)worldStartX + x;
                        int absZ = (int)worldStartZ + z;
                        if ((std::abs(PseudoRandomHash(absX, absZ, m_settings.seed + 500)) % m_settings.flowerChance) != 0) continue;

                        int h = mapFinalHeight[idx2D];
                        if (h <= m_settings.seaLevel) continue;
                        int localY = (h - (int)worldStartY) / lodScale + 1;
                        if (localY >= 0 && localY < PADDED) {
                            int vIdx = x + z * PADDED + localY * PADDED * PADDED;
                            if (voxels[vIdx] == AIR)
                                voxels[vIdx] = FLOWER;
                        }
                    }
                }
            }
        }
    }

    // ============================================================================================
    // IMGUI — ALL SETTINGS EXPOSED
    // ============================================================================================
    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "Advanced Generator v2");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "18 Biomes | Caves | Ores | Vegetation");
        ImGui::Separator();

        bool changed = false;

        // ─── GLOBAL ───
        if (ImGui::CollapsingHeader("Global & World Limits", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragInt("Seed", &m_settings.seed))                                    changed = true;
            if (ImGui::SliderFloat("Coordinate Scale", &m_settings.coordinateScale, 0.001f, 0.2f, "%.4f")) changed = true;
            if (ImGui::SliderInt("Sea Level", &m_settings.seaLevel, 0, 200))                 changed = true;
            if (ImGui::SliderInt("Max World Height", &m_settings.maxWorldHeight, 128, 4096))  changed = true;
            if (ImGui::SliderFloat("Minimum Height", &m_settings.minimumHeight, 0.0f, 200.0f)) changed = true;
            if (ImGui::SliderInt("Bedrock Depth", &m_settings.bedrockDepth, 1, 10))           changed = true;
            if (ImGui::SliderInt("Deepslate Level", &m_settings.deepslateLevel, 1, 64))       changed = true;
        }

        // ─── BASE TERRAIN ───
        if (ImGui::CollapsingHeader("Base Terrain")) {
            if (ImGui::SliderFloat("Hill Amplitude", &m_settings.hillAmplitude, 5.0f, 200.0f))   changed = true;
            if (ImGui::SliderFloat("Hill Frequency", &m_settings.hillFrequency, 0.5f, 15.0f))    changed = true;
            if (ImGui::SliderFloat("Mountain Amplitude", &m_settings.mountainAmplitude, 5.0f, 500.0f)) changed = true;
            if (ImGui::SliderFloat("Mountain Frequency", &m_settings.mountainFrequency, 0.1f, 5.0f))   changed = true;
        }

        // ─── CONTINENTALNESS ───
        if (ImGui::CollapsingHeader("Continentalness (Land/Ocean)")) {
            if (ImGui::SliderFloat("Continent Scale", &m_settings.continentScale, 0.001f, 0.1f, "%.4f")) changed = true;
            if (ImGui::SliderFloat("Continent Threshold", &m_settings.continentThreshold, -0.5f, 0.5f)) changed = true;
            if (ImGui::SliderFloat("Ocean Depth", &m_settings.oceanDepth, 5.0f, 200.0f))     changed = true;
            if (ImGui::SliderFloat("Beach Width", &m_settings.beachWidth, 0.01f, 0.3f))      changed = true;
            ImGui::TextWrapped("Threshold: below = ocean, above = land. Scale controls size of continents.");
        }

        // ─── EROSION ───
        if (ImGui::CollapsingHeader("Erosion (Flatness)")) {
            if (ImGui::SliderFloat("Erosion Scale", &m_settings.erosionScale, 0.005f, 0.2f, "%.4f")) changed = true;
            if (ImGui::SliderFloat("Erosion Power", &m_settings.erosionPower, 0.5f, 5.0f))   changed = true;
            ImGui::TextWrapped("Erosion modulates mountain height. Higher power = more extreme flat/mountainous transitions.");
        }

        // ─── BIOME NOISE ───
        if (ImGui::CollapsingHeader("Biome Climate Axes")) {
            if (ImGui::SliderFloat("Temperature Scale", &m_settings.temperatureScale, 0.01f, 0.3f, "%.3f")) changed = true;
            if (ImGui::SliderFloat("Moisture Scale", &m_settings.moistureScale, 0.01f, 0.3f, "%.3f"))       changed = true;
            if (ImGui::SliderFloat("Weirdness Scale", &m_settings.weirdnessScale, 0.01f, 0.3f, "%.3f"))     changed = true;
            ImGui::TextWrapped("These control the size of biome regions. Smaller = bigger biomes.");
        }

        // ─── CAVES ───
        if (ImGui::CollapsingHeader("Cave System")) {
            ImGui::Text("Cheese Caves (large openings):");
            if (ImGui::SliderFloat("Cave Scale", &m_settings.caveScale, 0.005f, 0.1f, "%.4f"))   changed = true;
            if (ImGui::SliderFloat("Cave Threshold", &m_settings.caveThreshold, 0.1f, 0.8f))     changed = true;
            if (ImGui::SliderFloat("Surface Bias", &m_settings.caveSurfaceBias, 0.0f, 0.2f))     changed = true;
            if (ImGui::SliderInt("Surface Depth", &m_settings.caveSurfaceDepth, 1, 20))           changed = true;
            ImGui::Separator();
            ImGui::Text("Spaghetti Caves (tunnels):");
            if (ImGui::SliderFloat("Spaghetti Scale", &m_settings.spaghettiScale, 0.005f, 0.05f, "%.4f")) changed = true;
            if (ImGui::SliderFloat("Spaghetti Threshold", &m_settings.spaghettiThresh, 0.5f, 0.99f))      changed = true;
        }

        // ─── ORES ───
        if (ImGui::CollapsingHeader("Ore Generation")) {
            if (ImGui::SliderFloat("Ore Noise Scale", &m_settings.oreScale, 0.01f, 0.2f, "%.3f")) changed = true;
            ImGui::Separator();
            ImGui::Text("Coal:");
            if (ImGui::SliderFloat("Coal Chance", &m_settings.coalChance, 0.01f, 0.2f, "%.3f"))   changed = true;
            if (ImGui::SliderInt("Coal Max Y", &m_settings.coalMaxY, 16, 256))                     changed = true;
            ImGui::Text("Iron:");
            if (ImGui::SliderFloat("Iron Chance", &m_settings.ironChance, 0.01f, 0.2f, "%.3f"))   changed = true;
            if (ImGui::SliderInt("Iron Max Y", &m_settings.ironMaxY, 16, 128))                     changed = true;
            ImGui::Text("Gold:");
            if (ImGui::SliderFloat("Gold Chance", &m_settings.goldChance, 0.01f, 0.15f, "%.3f"))  changed = true;
            if (ImGui::SliderInt("Gold Max Y", &m_settings.goldMaxY, 8, 64))                       changed = true;
            ImGui::Text("Copper:");
            if (ImGui::SliderFloat("Copper Chance", &m_settings.copperChance, 0.01f, 0.2f, "%.3f")) changed = true;
            if (ImGui::SliderInt("Copper Max Y", &m_settings.copperMaxY, 8, 96))                   changed = true;
            ImGui::Text("Diamond:");
            if (ImGui::SliderFloat("Diamond Chance", &m_settings.diamondChance, 0.005f, 0.15f, "%.4f")) changed = true;
            if (ImGui::SliderInt("Diamond Max Y", &m_settings.diamondMaxY, 4, 32))                       changed = true;
        }

        // ─── MEGA PEAKS ───
        if (ImGui::CollapsingHeader("Mega Peaks")) {
            if (ImGui::SliderFloat("Peak Height", &m_settings.megaPeakHeight, 0.0f, 4000.0f))     changed = true;
            if (ImGui::SliderFloat("Peak Rarity", &m_settings.megaPeakRarity, 0.01f, 1.0f))       changed = true;
            if (ImGui::SliderFloat("Peak Threshold", &m_settings.megaPeakThreshold, 0.1f, 0.95f)) changed = true;
        }

        // ─── VEGETATION ───
        if (ImGui::CollapsingHeader("Vegetation")) {
            if (ImGui::SliderInt("Max Tree LOD", &m_settings.maxTreeLOD, 1, 8))                   changed = true;
            ImGui::Separator();
            ImGui::Text("Tree Density (1/N, lower = denser):");
            if (ImGui::SliderInt("Forest", &m_settings.treeChanceForest, 5, 200))                  changed = true;
            if (ImGui::SliderInt("Plains", &m_settings.treeChancePlains, 20, 500))                 changed = true;
            if (ImGui::SliderInt("Birch Forest", &m_settings.treeChanceBirch, 5, 200))             changed = true;
            if (ImGui::SliderInt("Taiga", &m_settings.treeChanceTaiga, 5, 200))                    changed = true;
            if (ImGui::SliderInt("Dark Forest", &m_settings.treeChanceDarkForest, 5, 100))         changed = true;
            if (ImGui::SliderInt("Jungle", &m_settings.treeChanceJungle, 5, 100))                  changed = true;
            if (ImGui::SliderInt("Savanna", &m_settings.treeChanceSavanna, 20, 500))               changed = true;
            if (ImGui::SliderInt("Swamp", &m_settings.treeChanceSwamp, 10, 300))                   changed = true;
            if (ImGui::SliderInt("Snowy Taiga", &m_settings.treeChanceSnowyTaiga, 10, 200))        changed = true;
            if (ImGui::SliderInt("Flower Forest", &m_settings.treeChanceFlower, 10, 200))          changed = true;
            if (ImGui::SliderInt("Desert (Cactus)", &m_settings.treeChanceDesert, 50, 1000))       changed = true;
            ImGui::Separator();
            if (ImGui::SliderInt("Flower Density (1/N)", &m_settings.flowerChance, 3, 100))        changed = true;
        }

        // ─── BADLANDS ───
        if (ImGui::CollapsingHeader("Badlands")) {
            if (ImGui::SliderFloat("Layer Scale", &m_settings.badlandsLayerScale, 0.01f, 1.0f, "%.3f")) changed = true;
            if (ImGui::SliderInt("Stripe Layers", &m_settings.badlandsStripeLayers, 1, 20))              changed = true;
        }

        if (changed) {
            m_dirty = true;
            Init();
        }
#endif
    }

private:
    GenSettings m_settings;

    // Noise nodes
    FastNoise::SmartNode<> m_baseTerrainNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_temperatureNoise;
    FastNoise::SmartNode<> m_moistureNoise;
    FastNoise::SmartNode<> m_weirdnessNoise;
    FastNoise::SmartNode<> m_continentNoise;
    FastNoise::SmartNode<> m_erosionNoise;
    FastNoise::SmartNode<> m_caveNoise;
    FastNoise::SmartNode<> m_spaghettiNoise;
    FastNoise::SmartNode<> m_megaPeakNoise;
    FastNoise::SmartNode<> m_oreNoise;
    FastNoise::SmartNode<> m_badlandsNoise;
};