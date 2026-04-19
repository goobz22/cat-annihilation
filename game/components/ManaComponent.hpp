#pragma once

#include <algorithm>

// ManaComponent lives in its own header (rather than inside
// ElementalComponent.hpp) because ElementalComponent.hpp also redefines
// ElementalAffinityComponent with a schema that conflicts with the one in
// StoryComponents.hpp. Systems that only need mana accounting — like
// elemental_magic.cpp's canCastSpell / consumeMana path — should include this
// header instead of ElementalComponent.hpp to avoid the redefinition error.
// Once the two ElementalAffinityComponent schemas are unified, this header
// can be folded back into ElementalComponent.hpp.

namespace CatGame {

/**
 * ManaComponent — per-entity mana pool for spell casting.
 *
 * Entities that gate spells on mana attach this component and the magic
 * system reads it via hasMana / consume. Entities without the component
 * are treated as unconstrained casters (NPCs, scripted events) so the
 * magic system can fire spells from outside the normal player resource
 * loop.
 */
struct ManaComponent {
    int currentMana = 100;
    int maxMana = 100;
    float regenRate = 5.0f;  // Mana restored per second.
    float regenTimer = 0.0f; // Seconds since last regen tick.

    bool hasMana(int amount) const {
        return currentMana >= amount;
    }

    bool consume(int amount) {
        if (currentMana >= amount) {
            currentMana -= amount;
            return true;
        }
        return false;
    }

    void restore(int amount) {
        currentMana = std::min(currentMana + amount, maxMana);
    }

    void update(float dt) {
        regenTimer += dt;
        if (regenTimer >= 1.0f) {
            restore(static_cast<int>(regenRate));
            regenTimer = 0.0f;
        }
    }
};

} // namespace CatGame
