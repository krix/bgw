#include "asset_catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace bgw {
namespace {

std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [](unsigned char c) { return !std::isspace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [](unsigned char c) { return !std::isspace(c); }).base(), value.end());
    return value;
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        result.push_back(trim(field));
    }
    return result;
}

std::vector<std::vector<std::string>> readCsv(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open asset data: " + path);
    }
    std::vector<std::vector<std::string>> rows;
    for (std::string line; std::getline(input, line);) {
        if (!trim(line).empty()) {
            rows.push_back(fields(line));
        }
    }
    return rows;
}

int number(const std::string& value) {
    return std::stoi(trim(value));
}

std::vector<int> readMpi(const std::string& path, std::string& bitmap) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open map metadata: " + path);
    }
    std::vector<int> values;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (values.size() < 8) {
            values.push_back(number(line));
        } else {
            bitmap = line;
        }
    }
    return values;
}

} // namespace

AssetCatalog AssetCatalog::load(const std::string& dataFile,
                                const std::string& spriteFile,
                                const std::string& surfaceFile,
                                const std::string& stageFile,
                                const std::string& mapInfoFile,
                                const std::string& mapFile) {
    (void)dataFile;
    AssetCatalog catalog;

    for (const auto& row : readCsv(surfaceFile)) {
        if (row.size() >= 3) {
            catalog.surfaces.push_back({row[0], number(row[1]), number(row[2])});
        }
    }

    for (const auto& row : readCsv(spriteFile)) {
        if (row.size() >= 7) {
            catalog.sprites.push_back({number(row[0]), number(row[1]), number(row[2]),
                number(row[3]), number(row[4]), number(row[5]), number(row[6])});
        }
    }

    for (const auto& row : readCsv(stageFile)) {
        if (row.size() >= 2) {
            StageDefinition stage;
            stage.stage = number(row[0]);
            stage.difficulty = number(row[1]);
            for (std::size_t i = 2; i < row.size(); ++i) {
                stage.values.push_back(number(row[i]));
            }
            catalog.stages.push_back(std::move(stage));
        }
    }

    catalog.map.metadata = readMpi(mapInfoFile, catalog.map.bitmap);
    std::ifstream binary(mapFile, std::ios::binary);
    if (!binary) {
        throw std::runtime_error("cannot open map: " + mapFile);
    }
    catalog.map.payload.assign(std::istreambuf_iterator<char>(binary),
                               std::istreambuf_iterator<char>());
    if (catalog.map.payload.size() >= 24) {
        for (std::size_t offset = 0; offset < 24; offset += 4) {
            const auto b0 = catalog.map.payload[offset + 0];
            const auto b1 = catalog.map.payload[offset + 1];
            const auto b2 = catalog.map.payload[offset + 2];
            const auto b3 = catalog.map.payload[offset + 3];
            catalog.map.binaryHeader.push_back(
                static_cast<int32_t>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)));
        }
        // FUN_00456bb4 reads the first field as the tile-map width and stores
        // it as the layer stride; FUN_00450e60 indexes x + y * width. The map
        // is therefore 13 columns wide and 256 rows tall (a vertical shaft).
        catalog.map.columns = catalog.map.binaryHeader[0];
        catalog.map.rows = catalog.map.binaryHeader[1];
        catalog.map.tileWidth = catalog.map.binaryHeader[2];
        catalog.map.tileHeight = catalog.map.binaryHeader[3];
        catalog.map.tileColumns = catalog.map.binaryHeader[4];
        catalog.map.tileRows = catalog.map.binaryHeader[5];

        const std::size_t tileCount = static_cast<std::size_t>(catalog.map.columns) *
                                      static_cast<std::size_t>(catalog.map.rows);
        catalog.map.tiles.reserve(tileCount);
        for (std::size_t i = 0; i < tileCount; ++i) {
            const std::size_t offset = 24 + i * 2;
            const std::uint16_t low = offset < catalog.map.payload.size()
                ? catalog.map.payload[offset] : 0;
            const std::uint16_t high = offset + 1 < catalog.map.payload.size()
                ? catalog.map.payload[offset + 1] : 0;
            catalog.map.tiles.push_back(static_cast<std::uint16_t>(low | (high << 8)));
        }
    }
    return catalog;
}

void AssetCatalog::applyLevelRemap(int gameLevel) {
    for (std::uint16_t& tile : map.tiles) {
        if (gameLevel == 1) {
            if (tile == 8 || tile == 12) {
                tile = 4;
            }
        } else if (gameLevel == 3) {
            if (tile == 4 || tile == 8) {
                tile = 12;
            }
        }
    }
}

} // namespace bgw
