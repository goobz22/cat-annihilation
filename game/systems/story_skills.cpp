#include "story_skills.hpp"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace CatGame {

// ============================================================================
// StorySkills Implementation
// ============================================================================

int StorySkills::addSkillExperience(const std::string& skillName, int experience) {
    int* skillPtr = nullptr;

    // Map skill name to skill variable
    if (skillName == "hunting") skillPtr = &hunting;
    else if (skillName == "fighting") skillPtr = &fighting;
    else if (skillName == "healing") skillPtr = &healing;
    else if (skillName == "tracking") skillPtr = &tracking;
    else if (skillName == "swimming") skillPtr = &swimming;
    else if (skillName == "climbing") skillPtr = &climbing;
    else if (skillName == "stealth") skillPtr = &stealth;
    else if (skillName == "leadership") skillPtr = &leadership;
    else if (skillName == "mysticism") skillPtr = &mysticism;

    if (skillPtr) {
        *skillPtr = std::min(*skillPtr + experience, MAX_SKILL_LEVEL);
        return *skillPtr;
    }

    return 0;
}

int StorySkills::getSkillLevel(const std::string& skillName) const {
    if (skillName == "hunting") return hunting;
    if (skillName == "fighting") return fighting;
    if (skillName == "healing") return healing;
    if (skillName == "tracking") return tracking;
    if (skillName == "swimming") return swimming;
    if (skillName == "climbing") return climbing;
    if (skillName == "stealth") return stealth;
    if (skillName == "leadership") return leadership;
    if (skillName == "mysticism") return mysticism;
    return 0;
}

void StorySkills::setSkillLevel(const std::string& skillName, int level) {
    level = std::clamp(level, 0, MAX_SKILL_LEVEL);

    if (skillName == "hunting") hunting = level;
    else if (skillName == "fighting") fighting = level;
    else if (skillName == "healing") healing = level;
    else if (skillName == "tracking") tracking = level;
    else if (skillName == "swimming") swimming = level;
    else if (skillName == "climbing") climbing = level;
    else if (skillName == "stealth") stealth = level;
    else if (skillName == "leadership") leadership = level;
    else if (skillName == "mysticism") mysticism = level;
}

bool StorySkills::isMaster(const std::string& skillName) const {
    return getSkillLevel(skillName) >= MASTER_THRESHOLD;
}

int StorySkills::getTotalSkillPoints() const {
    return hunting + fighting + healing + tracking + swimming +
           climbing + stealth + leadership + mysticism;
}

const char* StorySkills::getSkillTier(int level) {
    if (level >= MASTER_THRESHOLD) return "Master";
    if (level >= EXPERT_THRESHOLD) return "Expert";
    if (level >= ADEPT_THRESHOLD) return "Adept";
    if (level >= NOVICE_THRESHOLD) return "Novice";
    return "Untrained";
}

float StorySkills::getHuntingDamageBonus() const {
    // 0% at level 0, up to 50% at level 100
    return 1.0f + (hunting / 200.0f);
}

float StorySkills::getFightingComboMultiplier() const {
    // 1.0x at level 0, up to 2.5x at level 100
    return 1.0f + (fighting / 100.0f) * 1.5f;
}

float StorySkills::getHealingEffectiveness() const {
    // 1.0x at level 0, up to 3.0x at level 100
    return 1.0f + (healing / 50.0f);
}

float StorySkills::getTrackingRange() const {
    // 20 units at level 0, up to 100 units at level 100
    return 20.0f + (tracking * 0.8f);
}

float StorySkills::getSwimmingSpeedMultiplier() const {
    // 0.5x at level 0, up to 1.5x at level 100
    return 0.5f + (swimming / 100.0f);
}

float StorySkills::getClimbingStaminaReduction() const {
    // 0% reduction at level 0, up to 80% at level 100
    return std::min(climbing / 125.0f, 0.8f);
}

float StorySkills::getStealthAggroReduction() const {
    // 0% reduction at level 0, up to 70% at level 100
    return std::min(stealth / 142.86f, 0.7f);
}

float StorySkills::getLeadershipAllyBonus() const {
    // 1.0x at level 0, up to 2.0x at level 100
    return 1.0f + (leadership / 100.0f);
}

float StorySkills::getMysticismElementalBonus() const {
    // 1.0x at level 0, up to 3.0x at level 100
    return 1.0f + (mysticism / 50.0f);
}

void StorySkills::reset() {
    hunting = fighting = healing = tracking = swimming =
    climbing = stealth = leadership = mysticism = 0;
}

std::string StorySkills::serialize() const {
    std::stringstream ss;
    ss << hunting << "," << fighting << "," << healing << ","
       << tracking << "," << swimming << "," << climbing << ","
       << stealth << "," << leadership << "," << mysticism;
    return ss.str();
}

bool StorySkills::deserialize(const std::string& data) {
    std::stringstream ss(data);
    char comma;

    if (!(ss >> hunting >> comma >> fighting >> comma >> healing >> comma
          >> tracking >> comma >> swimming >> comma >> climbing >> comma
          >> stealth >> comma >> leadership >> comma >> mysticism)) {
        return false;
    }

    // Clamp all values
    hunting = std::clamp(hunting, 0, MAX_SKILL_LEVEL);
    fighting = std::clamp(fighting, 0, MAX_SKILL_LEVEL);
    healing = std::clamp(healing, 0, MAX_SKILL_LEVEL);
    tracking = std::clamp(tracking, 0, MAX_SKILL_LEVEL);
    swimming = std::clamp(swimming, 0, MAX_SKILL_LEVEL);
    climbing = std::clamp(climbing, 0, MAX_SKILL_LEVEL);
    stealth = std::clamp(stealth, 0, MAX_SKILL_LEVEL);
    leadership = std::clamp(leadership, 0, MAX_SKILL_LEVEL);
    mysticism = std::clamp(mysticism, 0, MAX_SKILL_LEVEL);

    return true;
}

// ============================================================================
// SkillProgressionManager Implementation
// ============================================================================

SkillProgressionManager::SkillProgressionManager() {
    initializeDefaultUnlocks();
}

void SkillProgressionManager::onSkillLevelUp(void (*callback)(const SkillProgressionEvent&)) {
    levelUpCallback_ = callback;
}

SkillProgressionEvent SkillProgressionManager::processSkillGain(
    StorySkills& skills,
    const std::string& skillName,
    int xp) {

    SkillProgressionEvent event;
    event.skillName = skillName;
    event.previousLevel = skills.getSkillLevel(skillName);

    skills.addSkillExperience(skillName, xp);

    event.newLevel = skills.getSkillLevel(skillName);

    // Check if tier changed
    const char* oldTier = StorySkills::getSkillTier(event.previousLevel);
    const char* newTier = StorySkills::getSkillTier(event.newLevel);
    event.tierName = newTier;
    event.reachedNewTier = (oldTier != newTier);

    // Trigger callback if level changed
    if (event.newLevel > event.previousLevel && levelUpCallback_) {
        levelUpCallback_(event);
    }

    return event;
}

bool SkillProgressionManager::checkUnlocks(const StorySkills& skills) {
    bool anyUnlocked = false;

    for (auto& unlock : unlocks_) {
        if (!unlock.unlocked) {
            int level = skills.getSkillLevel(unlock.skillName);
            if (level >= unlock.requiredLevel) {
                unlock.unlocked = true;
                anyUnlocked = true;
            }
        }
    }

    return anyUnlocked;
}

void SkillProgressionManager::registerUnlock(
    const std::string& skillName,
    int requiredLevel,
    const std::string& description) {

    SkillUnlock unlock;
    unlock.skillName = skillName;
    unlock.requiredLevel = requiredLevel;
    unlock.unlockDescription = description;
    unlock.unlocked = false;

    unlocks_.push_back(unlock);
}

void SkillProgressionManager::initializeDefaultUnlocks() {
    // Hunting unlocks
    registerUnlock("hunting", 20, "Power Strike: Melee attacks deal 25% more damage");
    registerUnlock("hunting", 40, "Critical Hit: 15% chance to deal double damage");
    registerUnlock("hunting", 60, "Predator Instinct: Increased damage against wounded enemies");
    registerUnlock("hunting", 80, "Apex Hunter: All hunting bonuses doubled");

    // Fighting unlocks
    registerUnlock("fighting", 20, "Double Strike: Second attack in combo deals bonus damage");
    registerUnlock("fighting", 40, "Whirlwind: Spin attack hits multiple enemies");
    registerUnlock("fighting", 60, "Riposte: Perfect parry reflects damage");
    registerUnlock("fighting", 80, "Battle Trance: Extended combo mode with damage resistance");

    // Healing unlocks
    registerUnlock("healing", 20, "First Aid: Basic self-healing ability (cooldown 30s)");
    registerUnlock("healing", 40, "Rapid Recovery: Health regenerates faster out of combat");
    registerUnlock("healing", 60, "Healing Aura: Slowly heal nearby allies");
    registerUnlock("healing", 80, "Phoenix Spirit: Revive once per battle at 50% health");

    // Tracking unlocks
    registerUnlock("tracking", 20, "Keen Senses: Enemies appear on minimap");
    registerUnlock("tracking", 40, "Hunter's Mark: Mark one enemy to see through walls");
    registerUnlock("tracking", 60, "Tactical Awareness: See enemy health and level");
    registerUnlock("tracking", 80, "Omniscience: All enemies and items revealed on map");

    // Swimming unlocks
    registerUnlock("swimming", 20, "Aquatic Adaptation: Unlimited underwater breathing");
    registerUnlock("swimming", 40, "Dolphin Kick: Swim speed increased by 50%");
    registerUnlock("swimming", 60, "Whirlpool: Create water vortex that damages enemies");
    registerUnlock("swimming", 80, "Tidal Master: Control water currents and waves");

    // Climbing unlocks
    registerUnlock("climbing", 20, "Wall Climb: Climb vertical surfaces for short duration");
    registerUnlock("climbing", 40, "Parkour: Wall-run and vault over obstacles");
    registerUnlock("climbing", 60, "Sky Walker: Double jump and glide");
    registerUnlock("climbing", 80, "Gravity Defiance: Brief periods of wall-walking");

    // Stealth unlocks
    registerUnlock("stealth", 20, "Shadow Step: Reduced enemy detection range");
    registerUnlock("stealth", 40, "Silent Striker: Backstab attacks deal 3x damage");
    registerUnlock("stealth", 60, "Vanish: Become invisible for 5 seconds (cooldown 45s)");
    registerUnlock("stealth", 80, "Shadow Master: Permanent stealth when motionless");

    // Leadership unlocks
    registerUnlock("leadership", 20, "Rally: Inspire nearby allies (+25% damage)");
    registerUnlock("leadership", 40, "Commander's Presence: Allies take 25% less damage");
    registerUnlock("leadership", 60, "Tactical Genius: Coordinate allied attacks");
    registerUnlock("leadership", 80, "Legendary Leader: Recruit elite warrior companions");

    // Mysticism unlocks
    registerUnlock("mysticism", 20, "Elemental Touch: Basic elemental damage on attacks");
    registerUnlock("mysticism", 40, "Mystic Shield: Absorb elemental damage");
    registerUnlock("mysticism", 60, "Elemental Burst: Release powerful AoE elemental wave");
    registerUnlock("mysticism", 80, "Ascension: Transform into elemental avatar temporarily");
}

} // namespace CatGame
