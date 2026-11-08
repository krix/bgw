#pragma once

#include <array>
#include <cstdint>

namespace bgw {

// Recovered from FUN_0045720C. The original table is ten records at
// PTR_DAT_0045A304, each record occupying three 32-bit values.
struct ScoreEntry {
    int32_t score = 0;
    int32_t secondary = 0;
    int32_t nameOrFlags = 0;
};

struct ScoreTable {
    std::array<ScoreEntry, 10> entries{};
    int32_t insertionThreshold = 0;
};

// Recovered from FUN_00454084, FUN_00453B64, FUN_00453AA0, FUN_00453AD4,
// FUN_00453B14, FUN_00452774 and FUN_00453224. Field offsets now match the
// binary exactly: position is at +0x60/+0x64, the solid-contact counter is at
// +0x40, jump energy at +0x44, and the collision count at +0x4c.
#pragma pack(push, 1)
struct EntityRecord {
    int32_t slotIndex;                   // +0x00
    int32_t sequence;                    // +0x04
    std::uint8_t active;                 // +0x08
    std::uint8_t unknown09[3];
    int32_t type;                        // +0x0c
    int32_t linkIndex;                   // +0x10
    int32_t spriteIndex;                 // +0x14
    int32_t animFrame;                   // +0x18
    int32_t initialized;                 // +0x1c
    std::uint8_t unknown20[0x1c];
    int32_t animationTick;               // +0x3c
    int32_t contactFrames;               // +0x40
    int32_t jumpEnergy;                  // +0x44
    int32_t direction;                   // +0x48
    int32_t collisionCount;              // +0x4c
    std::uint8_t unknown50[0xc];
    int32_t cooldown;                    // +0x5c
    int32_t xFixed;                      // +0x60
    int32_t yFixed;                      // +0x64
    int32_t xDelta;                      // +0x68
    int32_t yDelta;                      // +0x6c
    int32_t field70;                     // +0x70
    int32_t typeField74;                 // +0x74
    int32_t typeField78;                 // +0x78
    std::uint8_t visible;                // +0x7c
    std::uint8_t unknown7d[3];
    std::uint32_t halfW;                 // +0x80
    std::uint32_t halfH;                 // +0x84
};
#pragma pack(pop)

static_assert(sizeof(EntityRecord) == 0x88, "entity stride recovered from binary");

// FUN_00454084 indexes this table at PTR_DAT_0045A498. The first recovered
// records are type 1 = player and types 2..4 = collision/effect entities;
// exact field meanings remain incomplete.
#pragma pack(push, 1)
struct EntityTypeDefinition {
    int32_t field00 = 0;
    int32_t field04 = 0;
    int32_t field08 = 0;
    int32_t field0c = 0;
    int32_t field10 = 0;
    int32_t field14 = 0;
    int32_t field18 = 0;
    int32_t field1c = 0;
    int32_t field20 = 0;
    int32_t field24 = 0;
    int32_t field28 = 0;
    int32_t field2c = 0;
    int32_t field30 = 0;
};
#pragma pack(pop)

static_assert(sizeof(EntityTypeDefinition) == 0x34, "entity type stride recovered from binary");

struct TileMapLayout {
    int32_t columns = 0;                // +0x6020, horizontal stride (256)
    int32_t rows = 0;                   // +0x6024, vertical count (13)
    int32_t tileWidth = 0;              // +0x6028
    int32_t tileHeight = 0;             // +0x602c
    int32_t scrollX = 0;                // +0x6034
    int32_t scrollY = 0;                // +0x6038
    int32_t wrapWidth = 0;              // +0x60a0
    int32_t wrapHeight = 0;             // +0x60a4
};

} // namespace bgw
