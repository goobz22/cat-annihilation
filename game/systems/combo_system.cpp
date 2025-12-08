#include "combo_system.hpp"
#include "leveling_system.hpp"
#include <algorithm>

namespace CatGame {

ComboSystem::ComboSystem() {
    activeCombo_.isActive = false;
    activeCombo_.isComplete = false;
}

void ComboSystem::initialize(LevelingSystem* levelingSystem) {
    levelingSystem_ = levelingSystem;

    // Initialize all weapon combos
    initializeSwordCombos();
    initializeBowCombos();
    initializeStaffCombos();

    // Clear state
    inputBuffer_.inputs.clear();
    inputBuffer_.inputTimestamps.clear();
    comboCooldowns_.clear();
    comboMaxCooldowns_.clear();
    activeCombo_.isActive = false;
}

void ComboSystem::update(float deltaTime) {
    updateCooldowns(deltaTime);
    updateActiveCombo(deltaTime);
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

void ComboSystem::registerInput(ComboInput input, float currentTime) {
    // Add to buffer
    inputBuffer_.inputs.push_back(input);
    inputBuffer_.inputTimestamps.push_back(currentTime);

    // Limit buffer size
    if (inputBuffer_.inputs.size() > InputBuffer::MAX_BUFFER_SIZE) {
        inputBuffer_.inputs.erase(inputBuffer_.inputs.begin());
        inputBuffer_.inputTimestamps.erase(inputBuffer_.inputTimestamps.begin());
    }

    // Clean old inputs
    updateInputBuffer(currentTime);
}

void ComboSystem::clearInputs() {
    inputBuffer_.inputs.clear();
    inputBuffer_.inputTimestamps.clear();
}

const ComboMove* ComboSystem::checkForCombo(const std::string& weaponType) {
    // Get unlocked combos for this weapon
    auto combos = getUnlockedCombos(weaponType);

    // Check each combo in order of priority (longer combos first)
    std::sort(combos.begin(), combos.end(), [](const ComboMove& a, const ComboMove& b) {
        return a.inputSequence.size() > b.inputSequence.size();
    });

    for (const auto& combo : combos) {
        // Check if on cooldown
        if (isOnCooldown(combo.name)) {
            continue;
        }

        // Check if input sequence matches
        if (matchesComboSequence(combo.inputSequence)) {
            return &combo;
        }
    }

    return nullptr;
}

// ============================================================================
// COMBO EXECUTION
// ============================================================================

bool ComboSystem::startCombo(const ComboMove& combo) {
    // Check cooldown
    if (isOnCooldown(combo.name)) {
        return false;
    }

    // Check if another combo is active
    if (activeCombo_.isActive) {
        return false;
    }

    // Start combo
    activeCombo_.move = combo;
    activeCombo_.timeElapsed = 0.0f;
    activeCombo_.isActive = true;
    activeCombo_.isComplete = false;

    // Set cooldown
    comboCooldowns_[combo.name] = combo.cooldown;
    comboMaxCooldowns_[combo.name] = combo.cooldown;

    // Clear input buffer
    clearInputs();

    // Trigger callback
    if (onComboExecuted_) {
        onComboExecuted_(combo.name, combo.damageMultiplier);
    }

    return true;
}

const ActiveCombo* ComboSystem::getActiveCombo() const {
    if (activeCombo_.isActive) {
        return &activeCombo_;
    }
    return nullptr;
}

bool ComboSystem::hasComboInvincibility() const {
    return activeCombo_.isActive && activeCombo_.move.hasInvincibilityFrames;
}

void ComboSystem::cancelCombo() {
    activeCombo_.isActive = false;
    activeCombo_.isComplete = false;
}

// ============================================================================
// COMBO QUERIES
// ============================================================================

std::vector<ComboMove> ComboSystem::getCombosForWeapon(const std::string& weaponType) const {
    auto it = weaponCombos_.find(weaponType);
    if (it != weaponCombos_.end()) {
        return it->second;
    }
    return std::vector<ComboMove>();
}

std::vector<ComboMove> ComboSystem::getUnlockedCombos(const std::string& weaponType) const {
    if (!levelingSystem_) {
        return std::vector<ComboMove>();
    }

    auto allCombos = getCombosForWeapon(weaponType);
    int weaponLevel = levelingSystem_->getWeaponLevel(weaponType);

    std::vector<ComboMove> unlocked;
    for (const auto& combo : allCombos) {
        if (weaponLevel >= combo.requiredLevel) {
            unlocked.push_back(combo);
        }
    }

    return unlocked;
}

bool ComboSystem::isComboUnlocked(const std::string& weaponType, const std::string& comboName) const {
    const ComboMove* combo = getCombo(weaponType, comboName);
    if (!combo || !levelingSystem_) {
        return false;
    }

    int weaponLevel = levelingSystem_->getWeaponLevel(weaponType);
    return weaponLevel >= combo->requiredLevel;
}

const ComboMove* ComboSystem::getCombo(const std::string& weaponType, const std::string& comboName) const {
    auto combos = getCombosForWeapon(weaponType);
    for (const auto& combo : combos) {
        if (combo.name == comboName) {
            return &combo;
        }
    }
    return nullptr;
}

// ============================================================================
// COOLDOWN MANAGEMENT
// ============================================================================

bool ComboSystem::isOnCooldown(const std::string& comboName) const {
    auto it = comboCooldowns_.find(comboName);
    if (it != comboCooldowns_.end()) {
        return it->second > 0.0f;
    }
    return false;
}

float ComboSystem::getCooldownRemaining(const std::string& comboName) const {
    auto it = comboCooldowns_.find(comboName);
    if (it != comboCooldowns_.end()) {
        return std::max(0.0f, it->second);
    }
    return 0.0f;
}

float ComboSystem::getCooldownProgress(const std::string& comboName) const {
    auto cdIt = comboCooldowns_.find(comboName);
    auto maxIt = comboMaxCooldowns_.find(comboName);

    if (cdIt == comboCooldowns_.end() || maxIt == comboMaxCooldowns_.end()) {
        return 1.0f; // Ready
    }

    float remaining = cdIt->second;
    float max = maxIt->second;

    if (max <= 0.0f) {
        return 1.0f;
    }

    return 1.0f - (remaining / max);
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void ComboSystem::initializeSwordCombos() {
    std::vector<ComboMove> swordCombos;

    swordCombos.push_back(ComboMoves::createBasicSlash());
    swordCombos.push_back(ComboMoves::createSpinAttack());
    swordCombos.push_back(ComboMoves::createDashStrike());
    swordCombos.push_back(ComboMoves::createWhirlwind());
    swordCombos.push_back(ComboMoves::createExecution());
    swordCombos.push_back(ComboMoves::createRisingSlash());
    swordCombos.push_back(ComboMoves::createGroundSlam());
    swordCombos.push_back(ComboMoves::createBladeDance());
    swordCombos.push_back(ComboMoves::createCriticalEdge());

    weaponCombos_["sword"] = swordCombos;
}

void ComboSystem::initializeBowCombos() {
    std::vector<ComboMove> bowCombos;

    bowCombos.push_back(ComboMoves::createQuickShot());
    bowCombos.push_back(ComboMoves::createDoubleShot());
    bowCombos.push_back(ComboMoves::createChargedShot());
    bowCombos.push_back(ComboMoves::createRainOfArrows());
    bowCombos.push_back(ComboMoves::createSniperShot());
    bowCombos.push_back(ComboMoves::createPiercingArrow());
    bowCombos.push_back(ComboMoves::createExplosiveArrow());
    bowCombos.push_back(ComboMoves::createArrowStorm());
    bowCombos.push_back(ComboMoves::createDeadEye());

    weaponCombos_["bow"] = bowCombos;
}

void ComboSystem::initializeStaffCombos() {
    std::vector<ComboMove> staffCombos;

    staffCombos.push_back(ComboMoves::createMagicBolt());
    staffCombos.push_back(ComboMoves::createArcaneBlast());
    staffCombos.push_back(ComboMoves::createManaShield());
    staffCombos.push_back(ComboMoves::createMeteorStrike());
    staffCombos.push_back(ComboMoves::createArcaneNova());
    staffCombos.push_back(ComboMoves::createTimeStop());
    staffCombos.push_back(ComboMoves::createElementalFury());
    staffCombos.push_back(ComboMoves::createArcaneStorm());
    staffCombos.push_back(ComboMoves::createUltimateSpell());

    weaponCombos_["staff"] = staffCombos;
}

void ComboSystem::updateInputBuffer(float currentTime) {
    // Remove inputs older than timeout
    while (!inputBuffer_.inputTimestamps.empty()) {
        float age = currentTime - inputBuffer_.inputTimestamps.front();
        if (age > InputBuffer::INPUT_TIMEOUT) {
            inputBuffer_.inputs.erase(inputBuffer_.inputs.begin());
            inputBuffer_.inputTimestamps.erase(inputBuffer_.inputTimestamps.begin());
        } else {
            break; // Rest are newer
        }
    }
}

void ComboSystem::updateCooldowns(float deltaTime) {
    for (auto& [comboName, cooldown] : comboCooldowns_) {
        if (cooldown > 0.0f) {
            cooldown -= deltaTime;
        }
    }
}

void ComboSystem::updateActiveCombo(float deltaTime) {
    if (!activeCombo_.isActive) {
        return;
    }

    activeCombo_.timeElapsed += deltaTime;

    // Check if combo animation is complete
    if (activeCombo_.timeElapsed >= activeCombo_.move.animationDuration) {
        activeCombo_.isComplete = true;
        activeCombo_.isActive = false;
    }
}

bool ComboSystem::matchesComboSequence(const std::vector<ComboInput>& sequence) const {
    if (sequence.empty()) {
        return false;
    }

    // Check if input buffer ends with the combo sequence
    if (inputBuffer_.inputs.size() < sequence.size()) {
        return false;
    }

    size_t bufferSize = inputBuffer_.inputs.size();
    size_t seqSize = sequence.size();

    for (size_t i = 0; i < seqSize; ++i) {
        size_t bufferIdx = bufferSize - seqSize + i;
        if (inputBuffer_.inputs[bufferIdx] != sequence[i]) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// COMBO MOVE DEFINITIONS
// ============================================================================

namespace ComboMoves {

// SWORD COMBOS

ComboMove createBasicSlash() {
    ComboMove combo;
    combo.name = "Basic Slash";
    combo.description = "A quick melee slash";
    combo.requiredLevel = 1;
    combo.inputSequence = {ComboInput::Attack};
    combo.damageMultiplier = 1.0f;
    combo.cooldown = 0.5f;
    combo.executionWindow = 1.0f;
    combo.animationName = "sword_slash";
    combo.animationDuration = 0.4f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createSpinAttack() {
    ComboMove combo;
    combo.name = "Spin Attack";
    combo.description = "Spin 360 degrees, hitting all nearby enemies";
    combo.requiredLevel = 3;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 1.5f;
    combo.cooldown = 3.0f;
    combo.executionWindow = 0.8f;
    combo.animationName = "sword_spin";
    combo.animationDuration = 0.8f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = true;
    combo.knockbackForce = 5.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 3.0f;
    return combo;
}

ComboMove createDashStrike() {
    ComboMove combo;
    combo.name = "Dash Strike";
    combo.description = "Dash forward with a powerful strike";
    combo.requiredLevel = 5;
    combo.inputSequence = {ComboInput::Direction, ComboInput::Attack};
    combo.damageMultiplier = 2.0f;
    combo.cooldown = 5.0f;
    combo.executionWindow = 0.6f;
    combo.animationName = "sword_dash";
    combo.animationDuration = 0.6f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 8.0f;
    combo.hasAoE = false;
    return combo;
}

ComboMove createWhirlwind() {
    ComboMove combo;
    combo.name = "Whirlwind";
    combo.description = "Extended spinning attack that pulls enemies in";
    combo.requiredLevel = 7;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 2.5f;
    combo.cooldown = 8.0f;
    combo.executionWindow = 1.2f;
    combo.animationName = "sword_whirlwind";
    combo.animationDuration = 1.5f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = false; // Pulls in instead
    combo.hasAoE = true;
    combo.aoeRadius = 5.0f;
    return combo;
}

ComboMove createExecution() {
    ComboMove combo;
    combo.name = "Execution";
    combo.description = "Devastating finisher that deals massive damage to low HP enemies";
    combo.requiredLevel = 10;
    combo.inputSequence = {ComboInput::Special, ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 4.0f; // 5x on enemies below 30% HP
    combo.cooldown = 15.0f;
    combo.executionWindow = 1.0f;
    combo.animationName = "sword_execution";
    combo.animationDuration = 1.2f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createRisingSlash() {
    ComboMove combo;
    combo.name = "Rising Slash";
    combo.description = "Upward slash that launches enemies into the air";
    combo.requiredLevel = 12;
    combo.inputSequence = {ComboInput::Jump, ComboInput::Attack};
    combo.damageMultiplier = 1.8f;
    combo.cooldown = 4.0f;
    combo.executionWindow = 0.7f;
    combo.animationName = "sword_rising";
    combo.animationDuration = 0.7f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 10.0f; // Vertical
    combo.hasAoE = false;
    return combo;
}

ComboMove createGroundSlam() {
    ComboMove combo;
    combo.name = "Ground Slam";
    combo.description = "Slam sword into ground creating shockwave";
    combo.requiredLevel = 15;
    combo.inputSequence = {ComboInput::Jump, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 3.0f;
    combo.cooldown = 10.0f;
    combo.executionWindow = 1.0f;
    combo.animationName = "sword_slam";
    combo.animationDuration = 1.0f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 12.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 8.0f;
    return combo;
}

ComboMove createBladeDance() {
    ComboMove combo;
    combo.name = "Blade Dance";
    combo.description = "Rapid succession of slashes in all directions";
    combo.requiredLevel = 18;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Attack, ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 3.5f;
    combo.cooldown = 12.0f;
    combo.executionWindow = 1.5f;
    combo.animationName = "sword_dance";
    combo.animationDuration = 2.0f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = false;
    combo.hasAoE = true;
    combo.aoeRadius = 4.0f;
    return combo;
}

ComboMove createCriticalEdge() {
    ComboMove combo;
    combo.name = "Critical Edge";
    combo.description = "Ultimate sword technique - guaranteed critical hit";
    combo.requiredLevel = 20;
    combo.inputSequence = {ComboInput::Special, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 5.0f; // Always crits
    combo.cooldown = 30.0f;
    combo.executionWindow = 1.5f;
    combo.animationName = "sword_ultimate";
    combo.animationDuration = 2.5f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = true;
    combo.knockbackForce = 20.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 10.0f;
    return combo;
}

// BOW COMBOS

ComboMove createQuickShot() {
    ComboMove combo;
    combo.name = "Quick Shot";
    combo.description = "Rapid arrow shot";
    combo.requiredLevel = 1;
    combo.inputSequence = {ComboInput::Attack};
    combo.damageMultiplier = 1.0f;
    combo.cooldown = 0.4f;
    combo.executionWindow = 0.8f;
    combo.animationName = "bow_quick";
    combo.animationDuration = 0.3f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createDoubleShot() {
    ComboMove combo;
    combo.name = "Double Shot";
    combo.description = "Fire two arrows in quick succession";
    combo.requiredLevel = 3;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 1.6f;
    combo.cooldown = 2.0f;
    combo.executionWindow = 0.6f;
    combo.animationName = "bow_double";
    combo.animationDuration = 0.6f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createChargedShot() {
    ComboMove combo;
    combo.name = "Charged Shot";
    combo.description = "Charge up for a powerful arrow";
    combo.requiredLevel = 5;
    combo.inputSequence = {ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 2.5f;
    combo.cooldown = 5.0f;
    combo.executionWindow = 2.0f;
    combo.animationName = "bow_charged";
    combo.animationDuration = 1.5f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 10.0f;
    combo.hasAoE = false;
    return combo;
}

ComboMove createRainOfArrows() {
    ComboMove combo;
    combo.name = "Rain of Arrows";
    combo.description = "Fire arrows into the sky to rain down on area";
    combo.requiredLevel = 7;
    combo.inputSequence = {ComboInput::Special, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 2.0f;
    combo.cooldown = 8.0f;
    combo.executionWindow = 1.5f;
    combo.animationName = "bow_rain";
    combo.animationDuration = 2.0f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = true;
    combo.aoeRadius = 8.0f;
    return combo;
}

ComboMove createSniperShot() {
    ComboMove combo;
    combo.name = "Sniper Shot";
    combo.description = "Precise long-range shot that ignores armor";
    combo.requiredLevel = 10;
    combo.inputSequence = {ComboInput::Direction, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 3.5f;
    combo.cooldown = 10.0f;
    combo.executionWindow = 2.0f;
    combo.animationName = "bow_sniper";
    combo.animationDuration = 1.8f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createPiercingArrow() {
    ComboMove combo;
    combo.name = "Piercing Arrow";
    combo.description = "Arrow that pierces through multiple enemies";
    combo.requiredLevel = 12;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Special};
    combo.damageMultiplier = 1.8f; // Per enemy hit
    combo.cooldown = 6.0f;
    combo.executionWindow = 1.0f;
    combo.animationName = "bow_piercing";
    combo.animationDuration = 0.8f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createExplosiveArrow() {
    ComboMove combo;
    combo.name = "Explosive Arrow";
    combo.description = "Arrow that explodes on impact";
    combo.requiredLevel = 15;
    combo.inputSequence = {ComboInput::Special, ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 2.8f;
    combo.cooldown = 12.0f;
    combo.executionWindow = 1.2f;
    combo.animationName = "bow_explosive";
    combo.animationDuration = 1.0f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 15.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 6.0f;
    return combo;
}

ComboMove createArrowStorm() {
    ComboMove combo;
    combo.name = "Arrow Storm";
    combo.description = "Rapid fire barrage of arrows";
    combo.requiredLevel = 18;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Attack, ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 3.2f;
    combo.cooldown = 15.0f;
    combo.executionWindow = 2.0f;
    combo.animationName = "bow_storm";
    combo.animationDuration = 2.5f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = true;
    combo.aoeRadius = 10.0f;
    return combo;
}

ComboMove createDeadEye() {
    ComboMove combo;
    combo.name = "Dead Eye";
    combo.description = "Mark multiple targets for instant headshots";
    combo.requiredLevel = 20;
    combo.inputSequence = {ComboInput::Special, ComboInput::Special, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 5.0f; // Per marked target
    combo.cooldown = 30.0f;
    combo.executionWindow = 3.0f;
    combo.animationName = "bow_ultimate";
    combo.animationDuration = 3.0f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

// STAFF COMBOS (Magic)

ComboMove createMagicBolt() {
    ComboMove combo;
    combo.name = "Magic Bolt";
    combo.description = "Basic magic projectile";
    combo.requiredLevel = 1;
    combo.inputSequence = {ComboInput::Attack};
    combo.damageMultiplier = 1.0f;
    combo.cooldown = 0.6f;
    combo.executionWindow = 1.0f;
    combo.animationName = "staff_bolt";
    combo.animationDuration = 0.5f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createArcaneBlast() {
    ComboMove combo;
    combo.name = "Arcane Blast";
    combo.description = "Powerful blast of arcane energy";
    combo.requiredLevel = 3;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 1.8f;
    combo.cooldown = 3.0f;
    combo.executionWindow = 0.8f;
    combo.animationName = "staff_blast";
    combo.animationDuration = 0.7f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 8.0f;
    combo.hasAoE = false;
    return combo;
}

ComboMove createManaShield() {
    ComboMove combo;
    combo.name = "Mana Shield";
    combo.description = "Create protective barrier that absorbs damage";
    combo.requiredLevel = 5;
    combo.inputSequence = {ComboInput::Special, ComboInput::Special};
    combo.damageMultiplier = 0.0f; // Defensive
    combo.cooldown = 15.0f;
    combo.executionWindow = 1.0f;
    combo.animationName = "staff_shield";
    combo.animationDuration = 0.5f;
    combo.hasInvincibilityFrames = true; // Effectively
    combo.hasKnockback = false;
    combo.hasAoE = false;
    return combo;
}

ComboMove createMeteorStrike() {
    ComboMove combo;
    combo.name = "Meteor Strike";
    combo.description = "Summon meteor from the sky";
    combo.requiredLevel = 7;
    combo.inputSequence = {ComboInput::Special, ComboInput::Attack, ComboInput::Attack};
    combo.damageMultiplier = 3.0f;
    combo.cooldown = 10.0f;
    combo.executionWindow = 1.5f;
    combo.animationName = "staff_meteor";
    combo.animationDuration = 2.0f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = true;
    combo.knockbackForce = 15.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 7.0f;
    return combo;
}

ComboMove createArcaneNova() {
    ComboMove combo;
    combo.name = "Arcane Nova";
    combo.description = "Explosive wave of magic in all directions";
    combo.requiredLevel = 10;
    combo.inputSequence = {ComboInput::Special, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 2.5f;
    combo.cooldown = 12.0f;
    combo.executionWindow = 1.2f;
    combo.animationName = "staff_nova";
    combo.animationDuration = 1.0f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = true;
    combo.knockbackForce = 10.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 8.0f;
    return combo;
}

ComboMove createTimeStop() {
    ComboMove combo;
    combo.name = "Time Stop";
    combo.description = "Freeze time for all enemies briefly";
    combo.requiredLevel = 12;
    combo.inputSequence = {ComboInput::Special, ComboInput::Dodge, ComboInput::Attack};
    combo.damageMultiplier = 1.0f;
    combo.cooldown = 20.0f;
    combo.executionWindow = 1.5f;
    combo.animationName = "staff_timestop";
    combo.animationDuration = 0.8f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = true;
    combo.aoeRadius = 20.0f; // Large radius
    return combo;
}

ComboMove createElementalFury() {
    ComboMove combo;
    combo.name = "Elemental Fury";
    combo.description = "Unleash all four elements at once";
    combo.requiredLevel = 15;
    combo.inputSequence = {ComboInput::Special, ComboInput::Attack, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 3.5f;
    combo.cooldown = 18.0f;
    combo.executionWindow = 2.0f;
    combo.animationName = "staff_elemental";
    combo.animationDuration = 2.5f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = true;
    combo.knockbackForce = 12.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 10.0f;
    return combo;
}

ComboMove createArcaneStorm() {
    ComboMove combo;
    combo.name = "Arcane Storm";
    combo.description = "Create a storm of magical projectiles";
    combo.requiredLevel = 18;
    combo.inputSequence = {ComboInput::Attack, ComboInput::Attack, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 4.0f;
    combo.cooldown = 20.0f;
    combo.executionWindow = 2.0f;
    combo.animationName = "staff_storm";
    combo.animationDuration = 3.0f;
    combo.hasInvincibilityFrames = false;
    combo.hasKnockback = false;
    combo.hasAoE = true;
    combo.aoeRadius = 12.0f;
    return combo;
}

ComboMove createUltimateSpell() {
    ComboMove combo;
    combo.name = "Cataclysm";
    combo.description = "Ultimate magical devastation";
    combo.requiredLevel = 20;
    combo.inputSequence = {ComboInput::Special, ComboInput::Special, ComboInput::Special, ComboInput::Attack};
    combo.damageMultiplier = 6.0f;
    combo.cooldown = 45.0f;
    combo.executionWindow = 3.0f;
    combo.animationName = "staff_ultimate";
    combo.animationDuration = 4.0f;
    combo.hasInvincibilityFrames = true;
    combo.hasKnockback = true;
    combo.knockbackForce = 25.0f;
    combo.hasAoE = true;
    combo.aoeRadius = 15.0f;
    return combo;
}

} // namespace ComboMoves

} // namespace CatGame
