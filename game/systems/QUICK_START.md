# Elemental Magic System - Quick Start Guide

## Integration Steps

### 1. Add to Build System

Add to your CMakeLists.txt:

```cmake
# Game systems
add_library(game_systems
    game/systems/elemental_magic.cpp
    # ... other systems
)

target_link_libraries(game_systems
    engine_ecs
    engine_cuda
    game_components
)

# CUDA particles
set_source_files_properties(
    engine/cuda/particles/elemental_particles.cu
    PROPERTIES CUDA_SOURCE_PROPERTY_FORMAT OBJ
)
```

### 2. Initialize in Game

```cpp
#include "game/systems/elemental_magic.hpp"
#include "game/components/ElementalComponent.hpp"

// In your game initialization:
auto particleSystem = std::make_shared<CatEngine::CUDA::ParticleSystem>(
    cudaContext,
    ParticleSystem::Config{.maxParticles = 500000}
);

auto magicSystem = std::make_unique<CatGame::ElementalMagicSystem>(
    particleSystem,
    15  // Priority
);

ecs->registerSystem(magicSystem.get());
```

### 3. Setup Player Entity

```cpp
using namespace CatGame;

// Create player
Entity player = ecs->createEntity();

// Add magic components
auto& mana = ecs->addComponent<ManaComponent>(player);
mana.maxMana = 200;
mana.currentMana = 200;
mana.regenRate = 10.0f;

auto& affinity = ecs->addComponent<ElementalAffinityComponent>(player);
affinity.preferredElement = ElementType::Fire;
affinity.setLevel(ElementType::Fire, 3);  // Start at level 3 for fire

auto& caster = ecs->addComponent<SpellCasterComponent>(player);
caster.equippedSpells[0] = "fireball";
caster.equippedSpells[1] = "flame_shield";

ecs->addComponent<ElementalResistanceComponent>(player);
ecs->addComponent<ElementalStatusComponent>(player);
```

### 4. Cast Spells

```cpp
// Get magic system
auto* magicSystem = ecs->getSystem<ElementalMagicSystem>();

// Cast spell at target position
vec3 targetPos = getMouseWorldPosition();
if (magicSystem->castSpell(player, "fireball", targetPos)) {
    LOG_INFO("Fireball cast!");
} else {
    LOG_WARN("Cannot cast - check mana/cooldown");
}

// Check cooldown
float cooldown = magicSystem->getSpellCooldownRemaining(player, "fireball");
if (cooldown > 0) {
    LOG_INFO("Fireball on cooldown: {:.1f}s", cooldown);
}
```

### 5. Handle Spell Progression

```cpp
// Award XP when player casts spells or defeats enemies
void onEnemyKilled(Entity enemy, ElementType damageType) {
    magicSystem->addElementalXP(player, damageType, 25);
}

// Check if new spells unlocked
int fireLevel = magicSystem->getElementalLevel(player, ElementType::Fire);
auto availableSpells = magicSystem->getAvailableSpells(player, ElementType::Fire);

// Display in UI
for (const auto& spellId : availableSpells) {
    const auto* spell = magicSystem->getSpell(spellId);
    LOG_INFO("Unlocked: {} (Level {})", spell->name, spell->requiredLevel);
}
```

### 6. Update Loop

```cpp
void updateGame(float deltaTime) {
    // Update mana regeneration
    for (Entity entity : entitiesWithMana) {
        auto& mana = ecs->getComponent<ManaComponent>(entity);
        mana.update(deltaTime);
    }

    // Update status effects
    for (Entity entity : entitiesWithStatus) {
        auto& status = ecs->getComponent<ElementalStatusComponent>(entity);
        status.update(deltaTime);

        // Apply effects
        if (status.isBurning) {
            applyBurnDamage(entity, deltaTime);
        }
        if (status.isFrozen) {
            freezeMovement(entity);
        }
    }

    // System update handles spell projectiles, effects, etc.
    ecs->update(deltaTime);
}
```

### 7. Render Spell Effects

```cpp
void renderSpellEffects() {
    // Get active spells
    auto& activeSpells = magicSystem->getActiveSpells();

    for (const auto& spell : activeSpells) {
        // Particle system automatically renders particles
        // Just need to bind correct shader based on element

        switch (spell.spell->element) {
            case ElementType::Water:
                bindShader("elemental_water");
                break;
            case ElementType::Fire:
                bindShader("elemental_fire");
                break;
            case ElementType::Air:
                bindShader("elemental_air");
                break;
            case ElementType::Earth:
                bindShader("elemental_earth");
                break;
        }

        // Render particles
        renderParticles(spell.particleEmitterId);
    }
}
```

## Example Spell Hotkey Setup

```cpp
void handleInput() {
    if (keyPressed(Key::Num1)) {
        magicSystem->castSpell(player, "fireball", getAimPosition());
    }
    if (keyPressed(Key::Num2)) {
        magicSystem->castSpell(player, "flame_shield", player);
    }
    if (keyPressed(Key::Num3)) {
        magicSystem->castSpell(player, "inferno", getAimPosition());
    }
    if (keyPressed(Key::Q)) {
        // Ultimate
        magicSystem->castSpell(player, "apocalypse", getAimPosition());
    }
}
```

## UI Integration Example

```cpp
void renderSpellUI() {
    auto& caster = ecs->getComponent<SpellCasterComponent>(player);
    auto& mana = ecs->getComponent<ManaComponent>(player);

    // Mana bar
    renderBar(
        manaBarPosition,
        (float)mana.currentMana / mana.maxMana,
        Color::Blue
    );

    // Spell icons with cooldowns
    for (int i = 0; i < 8; ++i) {
        const auto& spellId = caster.equippedSpells[i];
        if (spellId.empty()) continue;

        const auto* spell = magicSystem->getSpell(spellId);
        vec2 iconPos = getSpellIconPosition(i);

        // Draw icon
        renderSpellIcon(iconPos, spell);

        // Draw cooldown overlay
        float cooldown = caster.getCooldownRemaining(spellId);
        if (cooldown > 0) {
            float progress = cooldown / spell->cooldown;
            renderCooldownOverlay(iconPos, progress);
        }

        // Draw mana cost
        renderText(iconPos + vec2(0, 30), std::to_string(spell->manaCost));
    }
}
```

## Testing the System

```cpp
void testElementalMagic() {
    // Test all elements
    for (int i = 0; i < 4; ++i) {
        ElementType element = static_cast<ElementType>(i);

        // Get all spells for this element
        auto spells = magicSystem->getSpellsForElement(element);

        LOG_INFO("Testing {} element ({} spells)",
                getElementName(element), spells.size());

        for (const auto* spell : spells) {
            LOG_INFO("  - {} (Level {}, {} mana, {:.1f}s cooldown)",
                    spell->name, spell->requiredLevel,
                    spell->manaCost, spell->cooldown);
        }
    }

    // Test damage multipliers
    LOG_INFO("Water vs Fire: {:.1f}x",
            magicSystem->getElementalDamageMultiplier(
                ElementType::Water, ElementType::Fire));
}
```

## Common Issues

### Spells not casting?
- Check mana (use `mana.hasMana(cost)`)
- Check cooldown (use `getSpellCooldownRemaining()`)
- Check level requirement (use `getElementalLevel()`)

### Particles not showing?
- Verify particle system is initialized
- Check particle count limit (may be full)
- Ensure shaders are compiled and linked

### Performance issues?
- Reduce max particle count in config
- Lower emission rates in spell definitions
- Enable particle compaction
- Use LOD system for distant effects

## Next Steps

1. Create custom spell combinations
2. Add elemental terrain interactions
3. Implement spell interrupt mechanics
4. Add visual feedback for status effects
5. Create AI spell casting behavior
6. Balance damage values through playtesting

## Reference

- Full documentation: `ELEMENTAL_MAGIC_README.md`
- Spell definitions: `spell_definitions.hpp`
- Component reference: `../components/ElementalComponent.hpp`
