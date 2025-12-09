#include "leveling_system.hpp"
#include "combo_system.hpp"
#include <algorithm>
#include <cmath>

namespace CatGame {

LevelingSystem::LevelingSystem() {
    // Initialize default stats
    stats_.level = 1;
    stats_.xp = 0;
    stats_.xpToNextLevel = getCatXPToNextLevel(1);
    stats_.maxHealth = 100;
    stats_.attack = 10;
    stats_.defense = 5;
    stats_.speed = 10;
    stats_.critChance = 0.05f;
    stats_.critMultiplier = 1.5f;
    stats_.dodgeChance = 0.05f;

    // Initialize weapon skills
    weaponSkills_.sword.xpToNextLevel = getWeaponSkillXPToNextLevel(1);
    weaponSkills_.bow.xpToNextLevel = getWeaponSkillXPToNextLevel(1);
    weaponSkills_.staff.xpToNextLevel = getWeaponSkillXPToNextLevel(1);

    // Initialize elemental magic skills
    weaponSkills_.elementalMagic[ElementType::Fire] = WeaponSkill();
    weaponSkills_.elementalMagic[ElementType::Water] = WeaponSkill();
    weaponSkills_.elementalMagic[ElementType::Earth] = WeaponSkill();
    weaponSkills_.elementalMagic[ElementType::Air] = WeaponSkill();

    for (auto& [element, skill] : weaponSkills_.elementalMagic) {
        skill.xpToNextLevel = getElementalXPToNextLevel(1);
    }
}

void LevelingSystem::initialize() {
    // Reset to initial state
    stats_ = CatStats();
    weaponSkills_ = WeaponSkills();

    stats_.xpToNextLevel = getCatXPToNextLevel(1);
    weaponSkills_.sword.xpToNextLevel = getWeaponSkillXPToNextLevel(1);
    weaponSkills_.bow.xpToNextLevel = getWeaponSkillXPToNextLevel(1);
    weaponSkills_.staff.xpToNextLevel = getWeaponSkillXPToNextLevel(1);

    // Re-initialize elemental skills
    weaponSkills_.elementalMagic[ElementType::Fire] = WeaponSkill();
    weaponSkills_.elementalMagic[ElementType::Water] = WeaponSkill();
    weaponSkills_.elementalMagic[ElementType::Earth] = WeaponSkill();
    weaponSkills_.elementalMagic[ElementType::Air] = WeaponSkill();

    for (auto& [element, skill] : weaponSkills_.elementalMagic) {
        skill.xpToNextLevel = getElementalXPToNextLevel(1);
    }

    inCombat_ = false;
    timeSinceCombat_ = 0.0f;
}

void LevelingSystem::update(float deltaTime) {
    // Track time since last combat (for regeneration)
    if (!inCombat_) {
        timeSinceCombat_ += deltaTime;
    }
}

// ============================================================================
// CAT LEVELING
// ============================================================================

bool LevelingSystem::addXP(int amount) {
    if (amount <= 0) return false;
    if (stats_.level >= 50) return false; // Max level

    stats_.xp += amount;

    // Check for level up (can level up multiple times if enough XP)
    bool leveledUp = false;
    while (stats_.xp >= stats_.xpToNextLevel && stats_.level < 50) {
        stats_.xp -= stats_.xpToNextLevel;
        levelUp();
        leveledUp = true;
    }

    return leveledUp;
}

void LevelingSystem::levelUp() {
    if (stats_.level >= 50) return; // Max level

    stats_.level++;
    recalculateStats();

    // Update XP requirement for next level
    int nextLevelXP = getCatXPToNextLevel(stats_.level);
    if (nextLevelXP > 0) {
        stats_.xpToNextLevel = nextLevelXP;
    }

    // Check for ability unlocks
    checkAbilityUnlocks();

    // Trigger callback
    if (onLevelUp_) {
        onLevelUp_(stats_.level);
    }
}

float LevelingSystem::getXPProgress() const {
    if (stats_.xpToNextLevel <= 0) {
        return 1.0f; // Max level
    }
    return static_cast<float>(stats_.xp) / static_cast<float>(stats_.xpToNextLevel);
}

// ============================================================================
// CAT STATS
// ============================================================================

void LevelingSystem::recalculateStats() {
    // Calculate stats based on level
    int level = stats_.level;

    // Base stats growth
    stats_.maxHealth = 100 + (level - 1) * statGrowth_.healthPerLevel;
    stats_.attack = 10 + (level - 1) * statGrowth_.attackPerLevel;
    stats_.defense = 5 + (level - 1) * statGrowth_.defensePerLevel;
    stats_.speed = 10 + (level - 1) * statGrowth_.speedPerLevel;

    // Derived stats growth
    stats_.critChance = 0.05f + (level - 1) * statGrowth_.critChancePerLevel;
    stats_.dodgeChance = 0.05f + (level - 1) * statGrowth_.dodgeChancePerLevel;

    // Crit multiplier increases with Alpha Strike
    if (stats_.abilities.alphaStrike) {
        stats_.critMultiplier = 3.0f;
    } else {
        stats_.critMultiplier = 1.5f;
    }
}

int LevelingSystem::getEffectiveAttack() const {
    // Could add temporary buffs here in the future
    return stats_.attack;
}

int LevelingSystem::getEffectiveDefense() const {
    // Could add temporary buffs here in the future
    return stats_.defense;
}

int LevelingSystem::getEffectiveSpeed() const {
    // Agility ability increases speed
    int baseSpeed = stats_.speed;
    if (stats_.abilities.agility) {
        baseSpeed = static_cast<int>(baseSpeed * 1.1f); // 10% speed boost
    }
    return baseSpeed;
}

float LevelingSystem::getEffectiveCritChance() const {
    // Base crit chance + weapon skill bonuses
    return stats_.critChance;
}

// ============================================================================
// ABILITIES
// ============================================================================

bool LevelingSystem::hasAbility(const std::string& abilityName) const {
    if (abilityName == "regeneration") return stats_.abilities.regeneration;
    if (abilityName == "agility") return stats_.abilities.agility;
    if (abilityName == "nineLives") return stats_.abilities.nineLives;
    if (abilityName == "predatorInstinct") return stats_.abilities.predatorInstinct;
    if (abilityName == "alphaStrike") return stats_.abilities.alphaStrike;
    return false;
}

void LevelingSystem::checkAbilityUnlocks() {
    bool newAbilityUnlocked = false;

    // Regeneration (Level 5)
    if (stats_.level >= abilityUnlocks_.regeneration && !stats_.abilities.regeneration) {
        stats_.abilities.regeneration = true;
        newAbilityUnlocked = true;
        if (onAbilityUnlock_) {
            onAbilityUnlock_("Regeneration", stats_.level);
        }
    }

    // Agility (Level 10)
    if (stats_.level >= abilityUnlocks_.agility && !stats_.abilities.agility) {
        stats_.abilities.agility = true;
        newAbilityUnlocked = true;
        if (onAbilityUnlock_) {
            onAbilityUnlock_("Agility", stats_.level);
        }
    }

    // Nine Lives (Level 15)
    if (stats_.level >= abilityUnlocks_.nineLives && !stats_.abilities.nineLives) {
        stats_.abilities.nineLives = true;
        newAbilityUnlocked = true;
        if (onAbilityUnlock_) {
            onAbilityUnlock_("Nine Lives", stats_.level);
        }
    }

    // Predator Instinct (Level 20)
    if (stats_.level >= abilityUnlocks_.predatorInstinct && !stats_.abilities.predatorInstinct) {
        stats_.abilities.predatorInstinct = true;
        newAbilityUnlocked = true;
        if (onAbilityUnlock_) {
            onAbilityUnlock_("Predator Instinct", stats_.level);
        }
    }

    // Alpha Strike (Level 25)
    if (stats_.level >= abilityUnlocks_.alphaStrike && !stats_.abilities.alphaStrike) {
        stats_.abilities.alphaStrike = true;
        stats_.critMultiplier = 3.0f; // Update crit multiplier
        newAbilityUnlocked = true;
        if (onAbilityUnlock_) {
            onAbilityUnlock_("Alpha Strike", stats_.level);
        }
    }
}

void LevelingSystem::applyRegeneration(float deltaTime, float& currentHealth, float maxHealth) {
    // Only regenerate if ability is unlocked and out of combat
    if (!stats_.abilities.regeneration) return;
    if (inCombat_) return;
    if (timeSinceCombat_ < REGEN_DELAY) return;
    if (currentHealth >= maxHealth) return;

    // Regenerate 1% of max HP per second
    float regenAmount = (maxHealth * 0.01f) * deltaTime;
    currentHealth = std::min(maxHealth, currentHealth + regenAmount);
}

bool LevelingSystem::canRevive() const {
    return stats_.abilities.nineLives && !stats_.abilities.nineLivesUsed;
}

void LevelingSystem::useRevive(float& currentHealth, float maxHealth) {
    if (!canRevive()) return;

    // Revive at 50% HP
    currentHealth = maxHealth * 0.5f;
    stats_.abilities.nineLivesUsed = true;
}

void LevelingSystem::resetRevive() {
    // Reset at the start of each battle
    stats_.abilities.nineLivesUsed = false;
}

float LevelingSystem::getCritMultiplier() const {
    return stats_.critMultiplier;
}

// ============================================================================
// WEAPON SKILLS
// ============================================================================

bool LevelingSystem::addWeaponXP(const std::string& weaponType, int amount) {
    if (amount <= 0) return false;

    WeaponSkill* skill = weaponSkills_.getSkill(weaponType);
    if (!skill) return false;
    if (skill->level >= 20) return false; // Max weapon level

    skill->xp += amount;

    // Check for level up
    bool leveledUp = false;
    while (skill->xp >= skill->xpToNextLevel && skill->level < 20) {
        skill->xp -= skill->xpToNextLevel;
        levelUpWeaponSkill(*skill, weaponType);
        leveledUp = true;
    }

    return leveledUp;
}

bool LevelingSystem::addWeaponXPFromDamage(const std::string& weaponType, float damageDealt) {
    int xp = calculateWeaponXP(damageDealt);

    // Track statistics
    WeaponSkill* skill = weaponSkills_.getSkill(weaponType);
    if (skill) {
        skill->totalHits++;
        skill->totalDamageDealt += static_cast<int>(damageDealt);
    }

    return addWeaponXP(weaponType, xp);
}

void LevelingSystem::levelUpWeaponSkill(WeaponSkill& skill, const std::string& weaponType) {
    if (skill.level >= 20) return; // Max level

    skill.level++;

    // Recalculate skill bonuses
    WeaponSkillGrowth growth;
    skill.damageMultiplier = 1.0f + (skill.level - 1) * growth.damagePerLevel;
    skill.speedMultiplier = 1.0f + (skill.level - 1) * growth.speedPerLevel;
    skill.critBonus = (skill.level - 1) * growth.critBonusPerLevel;

    // Update XP requirement for next level
    int nextLevelXP = getWeaponSkillXPToNextLevel(skill.level);
    if (nextLevelXP > 0) {
        skill.xpToNextLevel = nextLevelXP;
    }

    // Notify combo system of skill level up (unlocks new combos)
    if (comboSystem_) {
        // Combo system will handle unlocking combos based on skill level
    }

    // Trigger callback
    if (onWeaponLevelUp_) {
        onWeaponLevelUp_(weaponType, skill.level);
    }
}

int LevelingSystem::getWeaponLevel(const std::string& weaponType) const {
    const WeaponSkill* skill = weaponSkills_.getSkill(weaponType);
    return skill ? skill->level : 1;
}

float LevelingSystem::getWeaponDamageMultiplier(const std::string& weaponType) const {
    const WeaponSkill* skill = weaponSkills_.getSkill(weaponType);
    return skill ? skill->damageMultiplier : 1.0f;
}

float LevelingSystem::getWeaponSpeedMultiplier(const std::string& weaponType) const {
    const WeaponSkill* skill = weaponSkills_.getSkill(weaponType);
    return skill ? skill->speedMultiplier : 1.0f;
}

float LevelingSystem::getWeaponCritBonus(const std::string& weaponType) const {
    const WeaponSkill* skill = weaponSkills_.getSkill(weaponType);
    return skill ? skill->critBonus : 0.0f;
}

// ============================================================================
// ELEMENTAL MAGIC SKILLS
// ============================================================================

bool LevelingSystem::addElementalXP(ElementType element, int amount) {
    if (amount <= 0) return false;
    if (element == ElementType::None) return false;

    auto it = weaponSkills_.elementalMagic.find(element);
    if (it == weaponSkills_.elementalMagic.end()) return false;

    WeaponSkill& skill = it->second;
    if (skill.level >= 15) return false; // Max elemental level

    skill.xp += amount;

    // Check for level up
    bool leveledUp = false;
    while (skill.xp >= skill.xpToNextLevel && skill.level < 15) {
        skill.xp -= skill.xpToNextLevel;
        levelUpElementalSkill(skill, element);
        leveledUp = true;
    }

    return leveledUp;
}

void LevelingSystem::levelUpElementalSkill(WeaponSkill& skill, ElementType element) {
    if (skill.level >= 15) return; // Max level

    skill.level++;

    // Recalculate elemental bonuses
    ElementalSkillGrowth growth;
    skill.damageMultiplier = 1.0f + (skill.level - 1) * growth.damagePerLevel;
    // Note: speedMultiplier used for duration, critBonus used for radius
    skill.speedMultiplier = 1.0f + (skill.level - 1) * growth.durationPerLevel;
    skill.critBonus = (skill.level - 1) * growth.radiusPerLevel;

    // Update XP requirement for next level
    int nextLevelXP = getElementalXPToNextLevel(skill.level);
    if (nextLevelXP > 0) {
        skill.xpToNextLevel = nextLevelXP;
    }

    // Trigger callback
    if (onWeaponLevelUp_) {
        std::string elementName = elementTypeToString(element);
        onWeaponLevelUp_(elementName + " Magic", skill.level);
    }
}

int LevelingSystem::getElementalLevel(ElementType element) const {
    if (element == ElementType::None) return 1;

    auto it = weaponSkills_.elementalMagic.find(element);
    if (it != weaponSkills_.elementalMagic.end()) {
        return it->second.level;
    }
    return 1;
}

float LevelingSystem::getElementalDamageMultiplier(ElementType element) const {
    if (element == ElementType::None) return 1.0f;

    auto it = weaponSkills_.elementalMagic.find(element);
    if (it != weaponSkills_.elementalMagic.end()) {
        return it->second.damageMultiplier;
    }
    return 1.0f;
}

float LevelingSystem::getElementalDurationMultiplier(ElementType element) const {
    if (element == ElementType::None) return 1.0f;

    auto it = weaponSkills_.elementalMagic.find(element);
    if (it != weaponSkills_.elementalMagic.end()) {
        return it->second.speedMultiplier; // Reusing speedMultiplier for duration
    }
    return 1.0f;
}

float LevelingSystem::getElementalRadiusMultiplier(ElementType element) const {
    if (element == ElementType::None) return 1.0f;

    auto it = weaponSkills_.elementalMagic.find(element);
    if (it != weaponSkills_.elementalMagic.end()) {
        return 1.0f + it->second.critBonus; // Reusing critBonus for radius multiplier
    }
    return 1.0f;
}

// ============================================================================
// COMBO SYSTEM INTEGRATION
// ============================================================================

std::vector<std::string> LevelingSystem::getUnlockedCombos(const std::string& weaponType) const {
    // This will query the combo system if available
    if (comboSystem_) {
        int skillLevel = getWeaponLevel(weaponType);
        // Combo system handles which combos are unlocked at each level
        // Return empty for now, combo system will implement
    }
    return std::vector<std::string>();
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

ElementType LevelingSystem::stringToElementType(const std::string& elementName) const {
    if (elementName == "fire" || elementName == "Fire") return ElementType::Fire;
    if (elementName == "water" || elementName == "Water") return ElementType::Water;
    if (elementName == "earth" || elementName == "Earth") return ElementType::Earth;
    if (elementName == "air" || elementName == "Air") return ElementType::Air;
    return ElementType::None;
}

std::string LevelingSystem::elementTypeToString(ElementType element) const {
    switch (element) {
        case ElementType::Fire: return "Fire";
        case ElementType::Water: return "Water";
        case ElementType::Earth: return "Earth";
        case ElementType::Air: return "Air";
        default: return "None";
    }
}

void LevelingSystem::setLevel(int level) {
    stats_.level = level;
    stats_.xpToNextLevel = static_cast<int>(getCatXPToNextLevel(level));
    recalculateStats();
    checkAbilityUnlocks();
}

void LevelingSystem::setXP(int xp) {
    stats_.xp = xp;
}

} // namespace CatGame
