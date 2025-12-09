#pragma once

#include "elemental_magic.hpp"

namespace CatGame {
namespace SpellDefinitions {

/**
 * All spell definitions for the Elemental Magic System
 *
 * Total: 20 spells (5 per element)
 * Each element has spells at levels: 1, 3, 5, 7, 10 (ultimate)
 *
 * Elements:
 * - Water: Healing and control
 * - Air: Speed and lightning
 * - Earth: Defense and AOE
 * - Fire: Raw damage and DOT
 */

// ============================================================================
// WATER SPELLS - Healing, Control, and Utility
// ============================================================================

const ElementalSpell WATER_BOLT {
    .id = "water_bolt",
    .name = "Water Bolt",
    .element = ElementType::Water,
    .manaCost = 10,
    .cooldown = 1.0f,
    .damage = 20.0f,
    .range = 30.0f,
    .areaOfEffect = 0.0f,
    .particleEffect = "water_bolt",
    .soundEffect = "water_cast",
    .requiredLevel = 1
};

const ElementalSpell HEALING_RAIN {
    .id = "healing_rain",
    .name = "Healing Rain",
    .element = ElementType::Water,
    .manaCost = 30,
    .cooldown = 8.0f,
    .damage = 0.0f,
    .range = 20.0f,
    .areaOfEffect = 8.0f,
    .particleEffect = "healing_rain",
    .soundEffect = "water_heal",
    .requiredLevel = 3,
    .duration = 5.0f,
    .healAmount = 30.0f  // Total healing over duration (6 HP/sec)
};

const ElementalSpell TIDAL_WAVE {
    .id = "tidal_wave",
    .name = "Tidal Wave",
    .element = ElementType::Water,
    .manaCost = 50,
    .cooldown = 12.0f,
    .damage = 50.0f,
    .range = 25.0f,
    .areaOfEffect = 12.0f,
    .particleEffect = "tidal_wave",
    .soundEffect = "water_wave",
    .requiredLevel = 5,
    // Optional fields (must be in struct declaration order)
    .duration = 0.0f,
    .healAmount = 0.0f,
    .knockbackForce = 15.0f
};

const ElementalSpell ICE_PRISON {
    .id = "ice_prison",
    .name = "Ice Prison",
    .element = ElementType::Water,
    .manaCost = 40,
    .cooldown = 15.0f,
    .damage = 15.0f,
    .range = 20.0f,
    .areaOfEffect = 3.0f,
    .particleEffect = "ice_prison",
    .soundEffect = "water_freeze",
    .requiredLevel = 7,
    .duration = 3.0f  // Freeze duration
};

const ElementalSpell TSUNAMI {
    .id = "tsunami",
    .name = "Tsunami",
    .element = ElementType::Water,
    .manaCost = 100,
    .cooldown = 60.0f,
    .damage = 150.0f,
    .range = 40.0f,
    .areaOfEffect = 20.0f,
    .particleEffect = "tsunami",
    .soundEffect = "water_ultimate",
    .requiredLevel = 10,
    .duration = 0.0f,
    .healAmount = 0.0f,
    .knockbackForce = 25.0f,
    .speedMultiplier = 1.0f,
    .defenseMultiplier = 1.0f,
    .dotDamage = 0.0f,
    .isUltimate = true
};

// ============================================================================
// AIR SPELLS - Speed, Lightning, and Mobility
// ============================================================================

const ElementalSpell WIND_GUST {
    .id = "wind_gust",
    .name = "Wind Gust",
    .element = ElementType::Air,
    .manaCost = 8,
    .cooldown = 1.2f,
    .damage = 15.0f,
    .range = 25.0f,
    .areaOfEffect = 0.0f,
    .particleEffect = "wind_gust",
    .soundEffect = "air_cast",
    .requiredLevel = 1,
    .knockbackForce = 8.0f
};

const ElementalSpell HASTE {
    .id = "haste",
    .name = "Haste",
    .element = ElementType::Air,
    .manaCost = 25,
    .cooldown = 20.0f,
    .damage = 0.0f,
    .range = 0.0f,  // Self-cast
    .areaOfEffect = 0.0f,
    .particleEffect = "haste",
    .soundEffect = "air_buff",
    .requiredLevel = 3,
    .duration = 10.0f,
    .speedMultiplier = 1.5f
};

const ElementalSpell LIGHTNING_BOLT {
    .id = "lightning_bolt",
    .name = "Lightning Bolt",
    .element = ElementType::Air,
    .manaCost = 45,
    .cooldown = 5.0f,
    .damage = 60.0f,
    .range = 35.0f,
    .areaOfEffect = 2.0f,  // Chain lightning effect
    .particleEffect = "lightning_bolt",
    .soundEffect = "air_lightning",
    .requiredLevel = 5
};

const ElementalSpell TORNADO {
    .id = "tornado",
    .name = "Tornado",
    .element = ElementType::Air,
    .manaCost = 60,
    .cooldown = 18.0f,
    .damage = 40.0f,
    .range = 30.0f,
    .areaOfEffect = 10.0f,
    .particleEffect = "tornado",
    .soundEffect = "air_tornado",
    .requiredLevel = 7,
    .duration = 5.0f,
    .healAmount = 0.0f,
    .knockbackForce = 0.0f,
    .speedMultiplier = 1.0f,
    .defenseMultiplier = 1.0f,
    .dotDamage = 8.0f  // Additional DOT while in tornado
};

const ElementalSpell STORM_CALL {
    .id = "storm_call",
    .name = "Storm Call",
    .element = ElementType::Air,
    .manaCost = 100,
    .cooldown = 60.0f,
    .damage = 120.0f,
    .range = 50.0f,
    .areaOfEffect = 25.0f,
    .particleEffect = "storm_call",
    .soundEffect = "air_ultimate",
    .requiredLevel = 10,
    .duration = 8.0f,
    .healAmount = 0.0f,
    .knockbackForce = 0.0f,
    .speedMultiplier = 1.0f,
    .defenseMultiplier = 1.0f,
    .dotDamage = 15.0f,  // Continuous lightning strikes
    .isUltimate = true
};

// ============================================================================
// EARTH SPELLS - Defense, Durability, and Control
// ============================================================================

const ElementalSpell ROCK_THROW {
    .id = "rock_throw",
    .name = "Rock Throw",
    .element = ElementType::Earth,
    .manaCost = 12,
    .cooldown = 1.5f,
    .damage = 25.0f,
    .range = 25.0f,
    .areaOfEffect = 0.0f,
    .particleEffect = "rock_throw",
    .soundEffect = "earth_cast",
    .requiredLevel = 1
};

const ElementalSpell STONE_SKIN {
    .id = "stone_skin",
    .name = "Stone Skin",
    .element = ElementType::Earth,
    .manaCost = 30,
    .cooldown = 25.0f,
    .damage = 0.0f,
    .range = 0.0f,  // Self-cast
    .areaOfEffect = 0.0f,
    .particleEffect = "stone_skin",
    .soundEffect = "earth_buff",
    .requiredLevel = 3,
    .duration = 15.0f,
    .defenseMultiplier = 1.5f  // 50% damage reduction
};

const ElementalSpell EARTHQUAKE {
    .id = "earthquake",
    .name = "Earthquake",
    .element = ElementType::Earth,
    .manaCost = 55,
    .cooldown = 15.0f,
    .damage = 45.0f,
    .range = 0.0f,  // Centered on caster
    .areaOfEffect = 15.0f,
    .particleEffect = "earthquake",
    .soundEffect = "earth_quake",
    .requiredLevel = 5,
    .duration = 2.0f  // Stun duration
};

const ElementalSpell WALL_OF_STONE {
    .id = "wall_of_stone",
    .name = "Wall of Stone",
    .element = ElementType::Earth,
    .manaCost = 50,
    .cooldown = 20.0f,
    .damage = 0.0f,
    .range = 15.0f,
    .areaOfEffect = 0.0f,
    .particleEffect = "stone_wall",
    .soundEffect = "earth_wall",
    .requiredLevel = 7,
    .duration = 10.0f,
    .createBarrier = true
};

const ElementalSpell METEOR_STRIKE {
    .id = "meteor_strike",
    .name = "Meteor Strike",
    .element = ElementType::Earth,
    .manaCost = 100,
    .cooldown = 60.0f,
    .damage = 180.0f,
    .range = 50.0f,
    .areaOfEffect = 18.0f,
    .particleEffect = "meteor",
    .soundEffect = "earth_ultimate",
    .requiredLevel = 10,
    .duration = 2.0f,  // Stun duration
    .healAmount = 0.0f,
    .knockbackForce = 0.0f,
    .speedMultiplier = 1.0f,
    .defenseMultiplier = 1.0f,
    .dotDamage = 0.0f,
    .isUltimate = true
};

// ============================================================================
// FIRE SPELLS - Damage, Damage over Time, and Destruction
// ============================================================================

const ElementalSpell FIREBALL {
    .id = "fireball",
    .name = "Fireball",
    .element = ElementType::Fire,
    .manaCost = 15,
    .cooldown = 1.0f,
    .damage = 30.0f,
    .range = 30.0f,
    .areaOfEffect = 2.0f,  // Small AOE on impact
    .particleEffect = "fireball",
    .soundEffect = "fire_cast",
    .requiredLevel = 1
};

const ElementalSpell FLAME_SHIELD {
    .id = "flame_shield",
    .name = "Flame Shield",
    .element = ElementType::Fire,
    .manaCost = 35,
    .cooldown = 15.0f,
    .damage = 10.0f,  // Reflection damage
    .range = 0.0f,  // Self-cast
    .areaOfEffect = 0.0f,
    .particleEffect = "flame_shield",
    .soundEffect = "fire_buff",
    .requiredLevel = 3,
    .duration = 10.0f
};

const ElementalSpell INFERNO {
    .id = "inferno",
    .name = "Inferno",
    .element = ElementType::Fire,
    .manaCost = 50,
    .cooldown = 10.0f,
    .damage = 35.0f,  // Initial damage
    .range = 25.0f,
    .areaOfEffect = 10.0f,
    .particleEffect = "inferno",
    .soundEffect = "fire_inferno",
    .requiredLevel = 5,
    .duration = 5.0f,
    .dotDamage = 10.0f  // Burn damage per second
};

const ElementalSpell PHOENIX_STRIKE {
    .id = "phoenix_strike",
    .name = "Phoenix Strike",
    .element = ElementType::Fire,
    .manaCost = 45,
    .cooldown = 12.0f,
    .damage = 55.0f,
    .range = 20.0f,  // Dash distance
    .areaOfEffect = 5.0f,  // Fire trail width
    .particleEffect = "phoenix_strike",
    .soundEffect = "fire_dash",
    .requiredLevel = 7,
    .duration = 3.0f,
    .dotDamage = 5.0f  // Fire trail DOT
};

const ElementalSpell APOCALYPSE {
    .id = "apocalypse",
    .name = "Apocalypse",
    .element = ElementType::Fire,
    .manaCost = 100,
    .cooldown = 60.0f,
    .damage = 100.0f,  // Initial damage
    .range = 60.0f,  // Screen-wide
    .areaOfEffect = 30.0f,
    .particleEffect = "apocalypse",
    .soundEffect = "fire_ultimate",
    .requiredLevel = 10,
    .duration = 10.0f,
    .healAmount = 0.0f,
    .knockbackForce = 0.0f,
    .speedMultiplier = 1.0f,
    .defenseMultiplier = 1.0f,
    .dotDamage = 20.0f,  // Massive ongoing burn
    .isUltimate = true
};

// ============================================================================
// SPELL REGISTRY
// ============================================================================

/**
 * Get all spell definitions
 * @return Vector of all spells
 */
inline std::vector<ElementalSpell> getAllSpells() {
    return {
        // Water
        WATER_BOLT, HEALING_RAIN, TIDAL_WAVE, ICE_PRISON, TSUNAMI,
        // Air
        WIND_GUST, HASTE, LIGHTNING_BOLT, TORNADO, STORM_CALL,
        // Earth
        ROCK_THROW, STONE_SKIN, EARTHQUAKE, WALL_OF_STONE, METEOR_STRIKE,
        // Fire
        FIREBALL, FLAME_SHIELD, INFERNO, PHOENIX_STRIKE, APOCALYPSE
    };
}

/**
 * Get spells for a specific element
 * @param element Element type
 * @return Vector of spells for that element
 */
inline std::vector<ElementalSpell> getSpellsForElement(ElementType element) {
    switch (element) {
        case ElementType::Water:
            return {WATER_BOLT, HEALING_RAIN, TIDAL_WAVE, ICE_PRISON, TSUNAMI};
        case ElementType::Air:
            return {WIND_GUST, HASTE, LIGHTNING_BOLT, TORNADO, STORM_CALL};
        case ElementType::Earth:
            return {ROCK_THROW, STONE_SKIN, EARTHQUAKE, WALL_OF_STONE, METEOR_STRIKE};
        case ElementType::Fire:
            return {FIREBALL, FLAME_SHIELD, INFERNO, PHOENIX_STRIKE, APOCALYPSE};
        default:
            return {};
    }
}

} // namespace SpellDefinitions
} // namespace CatGame
