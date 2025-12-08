# Leveling System Documentation

Complete weapon skill and cat leveling system for Cat Annihilation CUDA/Vulkan engine.

## Overview

The leveling system provides:
- **Cat Leveling**: 50 levels with stat growth and ability unlocks
- **Weapon Skills**: Individual progression for Sword, Bow, and Staff (20 levels each)
- **Elemental Magic**: Fire, Water, Earth, Air skills (15 levels each)
- **Combo System**: Skill-based combo unlocks with input buffering
- **Special Abilities**: Five powerful cat abilities unlocked at milestone levels

## Files Created

### Core System Files
1. **xp_tables.hpp** - XP requirements, stat growth formulas, and reward values
2. **leveling_system.hpp** - Main leveling system interface
3. **leveling_system.cpp** - Complete implementation
4. **combo_system.hpp** - Combo move definitions and input detection
5. **combo_system.cpp** - Combo system implementation with 27 unique combos

## Cat Leveling System

### Stat Growth

Base stats at Level 1:
- **Max Health**: 100 HP
- **Attack**: 10
- **Defense**: 5
- **Speed**: 10
- **Crit Chance**: 5%
- **Dodge Chance**: 5%

Growth per level:
- **Health**: +10 per level → 590 HP at level 50
- **Attack**: +2 per level → 108 attack at level 50
- **Defense**: +1 per level → 54 defense at level 50
- **Speed**: +1 per level → 59 speed at level 50
- **Crit Chance**: +0.1% per level → 9.9% at level 50
- **Dodge Chance**: +0.1% per level → 9.9% at level 50

### XP Requirements

Exponential growth formula: `XP = 100 * (1.5 ^ (level - 1))`

Key milestones:
- **Level 5**: 500 XP
- **Level 10**: 3,750 XP
- **Level 15**: 28,485 XP
- **Level 20**: 216,311 XP
- **Level 25**: 1,642,617 XP
- **Level 50**: 41,478,014,561 XP (max level)

### Ability Unlocks

#### Level 5: Regeneration
- Passive HP regeneration out of combat
- Restores 1% max HP per second
- Activates 3 seconds after leaving combat

#### Level 10: Agility
- **Double Jump**: Press jump while in air
- **Faster Dodge**: 50% faster dodge roll
- **Speed Boost**: 10% movement speed increase

#### Level 15: Nine Lives
- Revive once per battle at 50% HP
- Automatically activates when HP reaches 0
- Resets at the start of each new battle

#### Level 20: Predator Instinct
- See enemy HP bars
- Display enemy level
- Reveal enemy weaknesses and resistances
- Enhanced tactical awareness

#### Level 25: Alpha Strike
- Critical hits deal **3x damage** instead of 1.5x
- Massively increases burst damage potential
- Stacks with weapon skill crit bonuses

## Weapon Skill System

### Progression

Each weapon (Sword, Bow, Staff) has independent skill levels (1-20).

Growth formula: `XP = 50 * (1.4 ^ (level - 1))`

Bonuses per level:
- **Damage**: +5% per level → +95% at level 20
- **Attack Speed**: +2% per level → +38% at level 20
- **Crit Chance**: +0.2% per level → +3.8% at level 20

### XP Gain

Weapon XP is earned by dealing damage:
- Formula: `XP = damage * 0.1` (min 5, max 20 per hit)
- Example: Deal 100 damage = 10 XP

### Weapon-Specific Stats

**Sword (Level 1 base)**
- Damage: 25
- Attack Speed: 1.5 attacks/sec
- Range: 3.0 units

**Bow (Level 1 base)**
- Damage: 30
- Attack Speed: 1.2 attacks/sec
- Range: 15.0 units

**Staff (Level 1 base)**
- Damage: 40
- Attack Speed: 0.8 attacks/sec
- Range: 10.0 units

## Elemental Magic System

### Elements

Four elemental types, each with unique effects:

1. **Fire**: Damage over time (burning)
2. **Water**: Slowing effects (freezing)
3. **Earth**: Shields and defense
4. **Air**: Knockback and movement

### Progression

Each element levels independently (1-15).

Growth formula: `XP = 50 * (1.4 ^ (level - 1))` (same as weapons)

Bonuses per level:
- **Elemental Damage**: +8% per level → +112% at level 15
- **Effect Duration**: +5% per level → +70% at level 15
- **AoE Radius**: +3% per level → +42% at level 15

## Combo System

### Overview

The combo system provides 27 unique special attacks across three weapon types.

Features:
- **Input Buffering**: 0.5 second input window
- **Skill-Based Unlocking**: Combos unlock as weapon skills level up
- **Cooldown Management**: Each combo has individual cooldown
- **Special Effects**: AoE, knockback, invincibility frames

### Input Types

```cpp
enum class ComboInput {
    Attack,      // Primary attack button
    Special,     // Special attack button
    Jump,        // Jump button
    Dodge,       // Dodge/roll button
    Direction    // Directional input
};
```

### Sword Combos (9 total)

| Level | Combo Name | Input Sequence | Damage Mult | Cooldown |
|-------|-----------|----------------|-------------|----------|
| 1 | Basic Slash | Attack | 1.0x | 0.5s |
| 3 | Spin Attack | Attack → Attack | 1.5x | 3s |
| 5 | Dash Strike | Direction → Attack | 2.0x | 5s |
| 7 | Whirlwind | Attack → Special → Attack | 2.5x | 8s |
| 10 | Execution | Special → Attack → Attack | 4.0x | 15s |
| 12 | Rising Slash | Jump → Attack | 1.8x | 4s |
| 15 | Ground Slam | Jump → Special → Attack | 3.0x | 10s |
| 18 | Blade Dance | Attack × 4 | 3.5x | 12s |
| 20 | Critical Edge | Special × 2 → Attack | 5.0x | 30s |

### Bow Combos (9 total)

| Level | Combo Name | Input Sequence | Damage Mult | Cooldown |
|-------|-----------|----------------|-------------|----------|
| 1 | Quick Shot | Attack | 1.0x | 0.4s |
| 3 | Double Shot | Attack → Attack | 1.6x | 2s |
| 5 | Charged Shot | Special → Attack | 2.5x | 5s |
| 7 | Rain of Arrows | Special × 2 → Attack | 2.0x | 8s |
| 10 | Sniper Shot | Direction → Special → Attack | 3.5x | 10s |
| 12 | Piercing Arrow | Attack → Special | 1.8x | 6s |
| 15 | Explosive Arrow | Special → Attack × 2 | 2.8x | 12s |
| 18 | Arrow Storm | Attack × 4 | 3.2x | 15s |
| 20 | Dead Eye | Special × 3 → Attack | 5.0x | 30s |

### Staff Combos (9 total)

| Level | Combo Name | Input Sequence | Damage Mult | Cooldown |
|-------|-----------|----------------|-------------|----------|
| 1 | Magic Bolt | Attack | 1.0x | 0.6s |
| 3 | Arcane Blast | Attack → Attack | 1.8x | 3s |
| 5 | Mana Shield | Special × 2 | 0.0x (defensive) | 15s |
| 7 | Meteor Strike | Special → Attack × 2 | 3.0x | 10s |
| 10 | Arcane Nova | Special × 2 → Attack | 2.5x | 12s |
| 12 | Time Stop | Special → Dodge → Attack | 1.0x | 20s |
| 15 | Elemental Fury | Special → Attack × 2 | 3.5x | 18s |
| 18 | Arcane Storm | Attack × 2 → Special → Attack | 4.0x | 20s |
| 20 | Cataclysm | Special × 3 → Attack | 6.0x | 45s |

## Integration Guide

### Basic Setup

```cpp
#include "game/systems/leveling_system.hpp"
#include "game/systems/combo_system.hpp"

// Create systems
CatGame::LevelingSystem levelingSystem;
CatGame::ComboSystem comboSystem;

// Initialize
levelingSystem.initialize();
comboSystem.initialize(&levelingSystem);

// Link them
levelingSystem.setComboSystem(&comboSystem);
```

### Per-Frame Update

```cpp
void update(float deltaTime) {
    levelingSystem.update(deltaTime);
    comboSystem.update(deltaTime);

    // Apply regeneration if unlocked
    if (levelingSystem.hasAbility("regeneration")) {
        levelingSystem.applyRegeneration(deltaTime, playerHealth, maxHealth);
    }
}
```

### Combat Integration

```cpp
// When player deals damage
void onPlayerAttack(const std::string& weaponType, float damageDealt) {
    // Award weapon XP based on damage
    bool leveledUp = levelingSystem.addWeaponXPFromDamage(weaponType, damageDealt);

    if (leveledUp) {
        // Show level up notification
        showWeaponLevelUpUI(weaponType, levelingSystem.getWeaponLevel(weaponType));
    }

    // Apply weapon skill multipliers to damage
    float damageBonus = levelingSystem.getWeaponDamageMultiplier(weaponType);
    float finalDamage = damageDealt * damageBonus;
}

// When killing an enemy
void onEnemyKilled(EnemyType type) {
    int xp = 0;
    switch (type) {
        case EnemyType::SmallDog:
            xp = EnemyXPRewards::SMALL_DOG;
            break;
        case EnemyType::Boss:
            xp = EnemyXPRewards::BOSS;
            break;
    }

    bool leveledUp = levelingSystem.addXP(xp);
    if (leveledUp) {
        showLevelUpUI(levelingSystem.getLevel());
    }
}
```

### Combo Detection

```cpp
// Register player inputs
void onPlayerInput(InputType input) {
    ComboInput comboInput;

    switch (input) {
        case InputType::LeftClick:
            comboInput = ComboInput::Attack;
            break;
        case InputType::RightClick:
            comboInput = ComboInput::Special;
            break;
        case InputType::Space:
            comboInput = ComboInput::Jump;
            break;
        // ... etc
    }

    comboSystem.registerInput(comboInput, getCurrentTime());

    // Check for combo match
    const ComboMove* combo = comboSystem.checkForCombo(currentWeapon);
    if (combo) {
        comboSystem.startCombo(*combo);
        executeCombo(*combo);
    }
}

// Execute combo
void executeCombo(const ComboMove& combo) {
    // Calculate damage with multiplier
    float baseDamage = getWeaponDamage();
    float comboDamage = baseDamage * combo.damageMultiplier;

    // Apply weapon skill bonus
    float skillBonus = levelingSystem.getWeaponDamageMultiplier(currentWeapon);
    float finalDamage = comboDamage * skillBonus;

    // Check for invincibility frames
    if (combo.hasInvincibilityFrames) {
        setPlayerInvincible(combo.animationDuration);
    }

    // Apply AoE if applicable
    if (combo.hasAoE) {
        dealAoEDamage(playerPosition, combo.aoeRadius, finalDamage);
    }

    // Play animation
    playAnimation(combo.animationName, combo.animationDuration);
}
```

### Nine Lives Implementation

```cpp
// In damage handling
void takeDamage(float damage) {
    playerHealth -= damage;

    if (playerHealth <= 0.0f) {
        // Check for Nine Lives
        if (levelingSystem.canRevive()) {
            levelingSystem.useRevive(playerHealth, maxHealth);
            playReviveAnimation();
            showReviveUI();
            return; // Don't die
        }

        // Player dies
        onPlayerDeath();
    }
}

// At start of new battle/wave
void startNewBattle() {
    levelingSystem.resetRevive(); // Allow Nine Lives again
    levelingSystem.enterCombat();
}
```

### UI Integration

```cpp
// Display cat level and XP
void renderPlayerUI() {
    int level = levelingSystem.getLevel();
    int xp = levelingSystem.getXP();
    int xpNeeded = levelingSystem.getXPToNextLevel();
    float xpProgress = levelingSystem.getXPProgress();

    // Draw XP bar
    drawProgressBar(xpProgress, "Level " + std::to_string(level));
    drawText(std::to_string(xp) + " / " + std::to_string(xpNeeded) + " XP");

    // Draw stats
    auto stats = levelingSystem.getStats();
    drawStat("HP", stats.maxHealth);
    drawStat("Attack", stats.attack);
    drawStat("Defense", stats.defense);
    drawStat("Speed", stats.speed);
}

// Display weapon skills
void renderWeaponSkillUI() {
    for (const std::string& weapon : {"sword", "bow", "staff"}) {
        int level = levelingSystem.getWeaponLevel(weapon);
        float damage = levelingSystem.getWeaponDamageMultiplier(weapon);

        drawWeaponSkill(weapon, level, damage);
    }
}

// Display unlocked combos
void renderComboListUI() {
    auto combos = comboSystem.getUnlockedCombos(currentWeapon);

    for (const auto& combo : combos) {
        bool onCooldown = comboSystem.isOnCooldown(combo.name);
        float cooldownProgress = comboSystem.getCooldownProgress(combo.name);

        drawCombo(combo.name, combo.description, onCooldown, cooldownProgress);
    }
}
```

### Callbacks Setup

```cpp
// Cat level up notification
levelingSystem.setLevelUpCallback([](int newLevel) {
    showNotification("Level Up!", "You are now level " + std::to_string(newLevel));
    playSound("level_up.wav");
});

// Ability unlock notification
levelingSystem.setAbilityUnlockCallback([](const std::string& abilityName, int level) {
    showNotification("New Ability!", abilityName + " unlocked at level " + std::to_string(level));
    playSound("ability_unlock.wav");
});

// Weapon skill level up
levelingSystem.setWeaponLevelUpCallback([](const std::string& weapon, int level) {
    showNotification("Weapon Skill Up!", weapon + " is now level " + std::to_string(level));
});

// Combo executed
comboSystem.setComboExecutedCallback([](const std::string& comboName, float multiplier) {
    showComboText(comboName + "! " + std::to_string(multiplier) + "x damage");
});
```

## XP Sources Summary

### Enemy Kills
- Small Dog: 10 XP
- Medium Dog: 25 XP
- Large Dog: 50 XP
- Elite Dog: 100 XP
- Boss: 500 XP

### Weapon XP
- 5-20 XP per hit (based on damage dealt)
- Formula: `XP = clamp(damage * 0.1, 5, 20)`

### Quests
- Simple Quest: 50 XP
- Medium Quest: 150 XP
- Hard Quest: 500 XP
- Epic Quest: 1000 XP

### Discoveries
- New Location: 25 XP
- Secret Area: 100 XP
- Treasure Found: 50 XP

## Performance Considerations

- **Update Frequency**: Call `update()` every frame (typically 60 FPS)
- **Memory**: Minimal heap allocations; most data is stack-based
- **Thread Safety**: Not thread-safe; use from game thread only
- **Save Data**: Serialize `CatStats` and `WeaponSkills` for save games

## Testing Utilities

```cpp
// For testing: Set level directly
void setLevelForTesting(int level) {
    auto& stats = levelingSystem.getStatsRef();
    stats.level = level;
    levelingSystem.recalculateStats();
    levelingSystem.checkAbilityUnlocks();
}

// For testing: Grant XP
void grantTestXP(int amount) {
    levelingSystem.addXP(amount);
}

// For testing: Max out weapon skill
void maxWeaponSkill(const std::string& weapon) {
    for (int i = 0; i < 20; ++i) {
        levelingSystem.addWeaponXP(weapon, 999999);
    }
}
```

## Future Enhancements

Potential additions:
1. **Talent Trees**: Branching upgrade paths
2. **Prestige System**: Reset level for permanent bonuses
3. **Weapon Mastery**: Special bonuses at max level
4. **Combo Customization**: Player-defined combo inputs
5. **Elemental Combos**: Combine elements for unique effects
6. **Pet Companions**: Level up battle cats that fight alongside you

## License

Part of the Cat Annihilation CUDA/Vulkan game engine.
