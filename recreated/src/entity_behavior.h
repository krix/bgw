#pragma once

#include <cstdint>
#include <vector>

#include "data_layout.h"

namespace bgw {

struct EntityWorldState {
    int32_t scrollX = 0;
    int32_t scrollY = 0;
    int32_t playerInput = 0;
    int32_t cameraImpulseX = 0;
    int32_t cameraImpulseY = 0;
    bool rankingInput = false;
};

// Recovered from FUN_00454084. The type table at PTR_DAT_0045A498 holds one
// 0x34-byte record per entity type. The allocator copies the three leading
// fields; the remaining fields drive collision (FUN_004535C8) and culling
// (FUN_004539D4).
struct EntityTypeFields {
    int32_t field00 = 0;   // copied to record +0x1c
    int32_t field04 = 0;   // copied to record +0x20
    int32_t field08 = 0;   // copied to record +0x24
    int32_t hitFlag = 0;   // table +0x0c: 0 none, 1 kills player, 2 awards 1000
    int32_t offsetX = 0;   // table +0x10: collision box X inset
    int32_t offsetY = 0;   // table +0x14: collision box Y inset
    int32_t cullFlag = 0;  // table +0x28: when nonzero the object is never culled
    int32_t halfWidth = 16;
    int32_t halfHeight = 16;
};

struct EntityCallbacks {
    virtual ~EntityCallbacks() = default;
    virtual bool collides(EntityRecord& entity, int32_t dx, int32_t dy) = 0;
    // The binary derives the score from PTR_DAT_0045A42C and the current
    // entity/tile mode; the amount is intentionally not hard-coded here.
    virtual void onPlayerCollision(EntityRecord& entity) = 0;
    virtual void spawnEffect(EntityRecord& entity) = 0;
    virtual void enterMissState() = 0;
    virtual void enterClearState() = 0;
};

// Reconstructs the mode dispatch in FUN_00453E50. Modes 2-4 are directly
// recovered; mode 1 is the player routine FUN_00453B64 and remains partial
// because its collision helpers are separate QD/game functions.
void updateEntity(EntityRecord& entity, EntityWorldState& world,
                  EntityCallbacks& callbacks);

// Reconstructs the FUN_00454084 object slot allocator. Group 0 owns slot 0,
// group 1 owns slots 1..0x20, and group 2 owns slots 0x21..0x220.
class EntityPool {
public:
    static constexpr int kSlots = 0x221;
    static constexpr int kLastSingle = 0;
    static constexpr int kLastPlayer = 0x20;
    static constexpr int kLastEffect = 0x220;

    EntityPool();

    int spawn(int group, int typeIndex, int32_t xFixed, int32_t yFixed,
              int32_t xDelta, int32_t field70, int32_t sequence,
              const EntityTypeFields& typeDefinition);

    void update(EntityWorldState& world, EntityCallbacks& callbacks);
    void reset() {
        slots_.assign(kSlots, EntityRecord{});
        typeDefs_.assign(kSlots, EntityTypeFields{});
    }

    EntityRecord& slot(int index) { return slots_[index]; }
    const EntityRecord& slot(int index) const { return slots_[index]; }
    const std::vector<EntityRecord>& slots() const { return slots_; }
    const EntityTypeFields& typeFields(int index) const { return typeDefs_[index]; }

    // FUN_004525A4: rectangle overlap used by the entity collision routines.
    static bool aabbOverlap(int32_t leftA, int32_t topA, int32_t rightA, int32_t bottomA,
                            int32_t leftB, int32_t topB, int32_t rightB, int32_t bottomB);

private:
    std::vector<EntityRecord> slots_;
    std::vector<EntityTypeFields> typeDefs_;
};

} // namespace bgw
