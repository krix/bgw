#include "asset_catalog.h"
#include "game_state.h"

#include <chrono>
#include <cstdio>
#include <exception>

namespace {
class ConsoleRuntime final : public bgw::RuntimeApi {
public:
    void stopMusic() override {}
    void playMidi(const char*, bool) override {}
    bool musicFinished() override { return false; }
    void clearFrame() override {}
    void present() override {}
    void updateSprites() override {}
    void updateGameObjects() override {}
    void drawText(int, int, const char*, int) override {}
    // The console probe never reaches the ranking screens, so it does not
    // maintain a score table; report "does not place" and ignore name writes.
    int insertRankScore(int32_t) override { return -1; }
    void writeRankNameByte(int32_t, int32_t, char) override {}
    std::uint32_t timeGetTime() override {
        const auto now = std::chrono::steady_clock::now();
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count());
    }
    bool keyDown(int) override { return false; }

private:
    const std::chrono::steady_clock::time_point start_ =
        std::chrono::steady_clock::now();
};
}

int main() {
    try {
        const auto catalog = bgw::AssetCatalog::load(
            "data/000spr.dat", "data/001spr.dat", "data/001surf.dat",
            "data/stage.dat", "map/000mpi", "map/000.map");

        std::printf("BGW reconstruction prototype\n");
        std::printf("surfaces: %zu\n", catalog.surfaces.size());
        std::printf("sprites: %zu\n", catalog.sprites.size());
        std::printf("stages: %zu\n", catalog.stages.size());
        std::printf("map bytes: %zu\n", catalog.map.payload.size());
        std::printf("map header words: %zu\n", catalog.map.binaryHeader.size());
        std::printf("map grid: %dx%d tiles, tile size %dx%d, decoded tiles: %zu\n",
                    catalog.map.columns, catalog.map.rows,
                    catalog.map.tileWidth, catalog.map.tileHeight,
                    catalog.map.tiles.size());

        ConsoleRuntime runtime;
        bgw::GameStateMachine game(runtime);
        bgw::GameObjectState object;
        for (int i = 0; i < 4; ++i) {
            game.tick(object);
        }
        std::printf("state after startup: %d\n", static_cast<int>(game.state()));
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "native probe failed: %s\n", error.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "native probe failed: unknown exception\n");
        return 1;
    }
}
