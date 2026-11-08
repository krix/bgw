#include "entity_behavior.h"

#include <algorithm>

namespace bgw {

namespace {
void applyWorldMotion(EntityRecord& entity, const EntityWorldState& world) {
    entity.xFixed -= world.scrollX;
    entity.yFixed -= world.scrollY;
}

// FUN_00454214: clear the two type fields, drop the render link, and mark the
// slot free.
void deactivateEntity(EntityRecord& entity) {
    entity.active = 0;
    entity.typeField74 = 0;
    entity.typeField78 = 0;
    entity.linkIndex = -1;
}
}

EntityPool::EntityPool() : slots_(kSlots), typeDefs_(kSlots) {}

// FUN_004525A4: axis-aligned rectangle overlap. Layout is (left, top, right,
// bottom); touching edges do not count as an overlap.
bool EntityPool::aabbOverlap(int32_t leftA, int32_t topA, int32_t rightA, int32_t bottomA,
                             int32_t leftB, int32_t topB, int32_t rightB, int32_t bottomB) {
    if (rightA <= leftB || leftA >= rightB) return false;
    if (bottomA <= topB || topA >= bottomB) return false;
    return true;
}

int EntityPool::spawn(int group, int typeIndex, int32_t xFixed, int32_t yFixed,
                      int32_t xDelta, int32_t field70, int32_t sequence,
                      const EntityTypeFields& typeDefinition) {
    int first = 0;
    int last = kLastSingle;
    if (group == 1) {
        first = kLastSingle + 1;
        last = kLastPlayer;
    } else if (group == 2) {
        first = kLastPlayer + 1;
        last = kLastEffect;
    }

    for (int index = first; index <= last; ++index) {
        EntityRecord& slot = slots_[index];
        if (slot.active != 0) continue;
        slot = {};
        slot.slotIndex = index;
        slot.sequence = sequence;
        slot.active = 1;
        slot.type = typeIndex;
        slot.linkIndex = -1;
        slot.spriteIndex = 0;
        slot.xFixed = xFixed;
        slot.yFixed = yFixed;
        slot.xDelta = xDelta;
        slot.yDelta = 0;
        slot.field70 = field70;
        slot.typeField74 = 1;
        slot.typeField78 = 1;
        slot.visible = 1;
        slot.cooldown = 0;
        // FUN_00454084 copies the type-table fields as:
        //   +0x14 (spriteIndex) <- table +0x00 (base frame in the flat bank)
        //   +0x74              <- table +0x08
        //   +0x78              <- table +0x04
        // The caller then overwrites +0x18 (animFrame) with the frame offset
        // (score index for type 3, tile value-4 for type 2).
        slot.spriteIndex = typeDefinition.field00;
        slot.animFrame = 0;
        slot.typeField74 = typeDefinition.field08;
        slot.typeField78 = typeDefinition.field04;
        typeDefs_[index] = typeDefinition;
        return index;
    }
    return -1;
}

void EntityPool::update(EntityWorldState& world, EntityCallbacks& callbacks) {
    for (EntityRecord& entity : slots_) {
        if (entity.active == 0) continue;
        updateEntity(entity, world, callbacks);
        if (entity.active == 0) continue;

        // FUN_004539D4: cull the object once its centre leaves the band
        // [-4*half, 4*half + screen] on either axis. Objects whose type sets
        // the +0x28 flag are exempt.
        const EntityTypeFields& fields = typeDefs_[entity.slotIndex];
        if (fields.cullFlag == 0) {
            const int32_t x = entity.xFixed >> 5;
            const int32_t y = entity.yFixed >> 5;
            if (x < -4 * fields.halfWidth || x > fields.halfWidth * 4 + 0x140 ||
                y < -4 * fields.halfHeight || y > fields.halfHeight * 4 + 0xf0) {
                deactivateEntity(entity);
                continue;
            }
        }
        // FUN_00453E50: the cooldown ticks down every frame for live objects.
        if (entity.cooldown != 0) {
            --entity.cooldown;
        }
    }
}

void updateEntity(EntityRecord& entity, EntityWorldState& world,
                  EntityCallbacks& callbacks) {
    (void)callbacks;
    switch (entity.type) {
    case 1:
        // FUN_00453B64: player/control behavior. The binary resets the
        // displacement fields, advances animation, applies directional input,
        // and enters state 4 on a collision or leaving the playfield. The
        // native collision probes run in update(); FUN_00453E50 only performs
        // FUN_004535C8 entity-vs-player tests for slots above 0x20, so no
        // generic collision test belongs here.
        entity.xDelta = 0;
        entity.yDelta = 0;
        entity.animationTick++;
        if (world.playerInput != 0) {
            entity.direction = world.playerInput;
        }
        applyWorldMotion(entity, world);
        break;

    case 2:
        // FUN_00453AA0: gravity/falling object.
        entity.yDelta = std::min(entity.yDelta + 4, 0xc0);
        entity.xFixed -= world.scrollX;
        entity.yFixed += entity.yDelta - world.scrollY;
        break;

    case 3:
        // FUN_00453AD4: the score popup. It starts with a 60-frame cooldown
        // and simply deactivates through FUN_00454214 once the cooldown
        // reaches zero. It does not test collision or spawn anything, so the
        // old per-frame spawnEffect() call (which replayed break.wav forever)
        // is gone.
        if (entity.initialized == 0) {
            entity.initialized = 1;
            entity.cooldown = 0x3c;
        }
        if (entity.cooldown == 0) {
            deactivateEntity(entity);
            return;
        }
        applyWorldMotion(entity, world);
        break;

    case 4:
        // FUN_00453B14: the jump puff. It cycles the two-frame animation
        // every five ticks, then deactivates as soon as +0x18 exceeds 1.
        if (entity.initialized == 0) {
            entity.initialized = 1;
            entity.cooldown = 0x3c;
        }
        ++entity.animationTick;
        if (entity.animationTick > 4) {
            entity.animationTick = 0;
            ++entity.animFrame;
            if (entity.animFrame > 1) {
                deactivateEntity(entity);
                return;
            }
        }
        applyWorldMotion(entity, world);
        break;

    default:
        applyWorldMotion(entity, world);
        break;
    }
}

} // namespace bgw
