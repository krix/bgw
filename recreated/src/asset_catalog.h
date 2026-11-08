#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bgw {

struct SurfaceDefinition {
    std::string bitmap;
    int width = 0;
    int height = 0;
};

struct SpriteDefinition {
    int surface = 0;
    int width = 0;
    int height = 0;
    int columns = 0;
    int rows = 0;
    int originX = 0;
    int originY = 0;
};

struct StageDefinition {
    int stage = 0;
    int difficulty = 0;
    std::vector<int> values;
};

struct MapDefinition {
    std::vector<int32_t> metadata;
    std::vector<int32_t> binaryHeader;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint16_t> tiles;
    int columns = 0;
    int rows = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tileColumns = 0;
    int tileRows = 0;
    std::string bitmap;
};

struct AssetCatalog {
    std::vector<SurfaceDefinition> surfaces;
    std::vector<SpriteDefinition> sprites;
    std::vector<StageDefinition> stages;
    MapDefinition map;

    static AssetCatalog load(const std::string& dataFile,
                             const std::string& spriteFile,
                             const std::string& surfaceFile,
                             const std::string& stageFile,
                             const std::string& mapInfoFile,
                             const std::string& mapFile);

    // Reconstructs the level remap applied by FUN_00456BB4. The original reads
    // the current game level from PTR_DAT_0045A484; level 1 rewrites tiles 8
    // and 12 to 4, level 3 rewrites tiles 4 and 8 to 12, and level 2 leaves the
    // map unchanged.
    void applyLevelRemap(int gameLevel);
};

} // namespace bgw
