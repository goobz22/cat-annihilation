#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

namespace CatGame {

// Forward declarations
class LevelingSystem;

/**
 * Input sequence for combo detection
 */
enum class ComboInput {
    Attack,      // Primary attack button
    Special,     // Special attack button
    Jump,        // Jump button
    Dodge,       // Dodge/roll button
    Direction    // Directional input (forward, back, etc.)
};

/**
 * Combo move definition
 */
struct ComboMove {
    std::string name;                        // Combo name (e.g., "Spin Attack")
    std::string description;                 // Description of the combo
    int requiredLevel;                       // Weapon skill level required to unlock
    std::vector<ComboInput> inputSequence;   // Button sequence to trigger
    float damageMultiplier;                  // Damage multiplier for this combo
    float cooldown;                          // Cooldown after use (seconds)
    float executionWindow;                   // Time window to complete inputs (seconds)

    // Visual/animation data
    std::string animationName;               // Animation to play
    float animationDuration;                 // Animation length
    bool hasInvincibilityFrames;             // If true, player is invincible during combo

    // Optional special effects
    bool hasKnockback;                       // Knocks enemies back
    float knockbackForce;                    // Knockback strength
    bool hasAoE;                             // Area of Effect damage
    float aoeRadius;                         // AoE radius if applicable
};

/**
 * Currently executing combo state
 */
struct ActiveCombo {
    ComboMove move;
    float timeElapsed;
    bool isActive;
    bool isComplete;
};

/**
 * Input buffer for combo detection
 */
struct InputBuffer {
    std::vector<ComboInput> inputs;
    std::vector<float> inputTimestamps;
    static constexpr int MAX_BUFFER_SIZE = 10;
    static constexpr float INPUT_TIMEOUT = 0.5f; // Inputs older than this are cleared
};

/**
 * Combo System - Manages weapon combos and special attacks
 *
 * Features:
 * - Weapon-specific combo moves
 * - Input buffering and combo detection
 * - Skill-based combo unlocking
 * - Cooldown management
 * - Damage calculations with multipliers
 * - Special effects (knockback, AoE, etc.)
 */
class ComboSystem {
public:
    ComboSystem();
    ~ComboSystem() = default;

    /**
     * Initialize combo system
     * @param levelingSystem Reference to leveling system (for skill checks)
     */
    void initialize(LevelingSystem* levelingSystem);

    /**
     * Update per frame
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    // ========================================================================
    // INPUT HANDLING
    // ========================================================================

    /**
     * Register an input for combo detection
     * @param input Input type
     * @param currentTime Current game time
     */
    void registerInput(ComboInput input, float currentTime);

    /**
     * Clear input buffer
     */
    void clearInputs();

    /**
     * Check if input buffer matches a combo sequence
     * @param weaponType Current weapon type
     * @return Pointer to matched combo, or nullptr if no match
     */
    const ComboMove* checkForCombo(const std::string& weaponType);

    // ========================================================================
    // COMBO EXECUTION
    // ========================================================================

    /**
     * Start executing a combo
     * @param combo Combo to execute
     * @return True if combo started successfully
     */
    bool startCombo(const ComboMove& combo);

    /**
     * Get currently active combo
     * @return Pointer to active combo, or nullptr if none active
     */
    const ActiveCombo* getActiveCombo() const;

    /**
     * Check if a combo is currently being executed
     */
    bool isComboActive() const { return activeCombo_.isActive; }

    /**
     * Check if player is invincible due to combo
     */
    bool hasComboInvincibility() const;

    /**
     * Cancel current combo (e.g., if interrupted)
     */
    void cancelCombo();

    // ========================================================================
    // COMBO QUERIES
    // ========================================================================

    /**
     * Get all combos for a weapon type
     * @param weaponType Weapon type name
     * @return List of all combos for that weapon
     */
    std::vector<ComboMove> getCombosForWeapon(const std::string& weaponType) const;

    /**
     * Get unlocked combos for current weapon skill level
     * @param weaponType Weapon type name
     * @return List of unlocked combos
     */
    std::vector<ComboMove> getUnlockedCombos(const std::string& weaponType) const;

    /**
     * Check if a specific combo is unlocked
     * @param weaponType Weapon type
     * @param comboName Combo name
     * @return True if unlocked
     */
    bool isComboUnlocked(const std::string& weaponType, const std::string& comboName) const;

    /**
     * Get combo by name
     * @param weaponType Weapon type
     * @param comboName Combo name
     * @return Pointer to combo, or nullptr if not found
     */
    const ComboMove* getCombo(const std::string& weaponType, const std::string& comboName) const;

    // ========================================================================
    // COOLDOWN MANAGEMENT
    // ========================================================================

    /**
     * Check if combo is on cooldown
     * @param comboName Combo name
     * @return True if on cooldown
     */
    bool isOnCooldown(const std::string& comboName) const;

    /**
     * Get remaining cooldown time
     * @param comboName Combo name
     * @return Remaining cooldown in seconds, 0 if ready
     */
    float getCooldownRemaining(const std::string& comboName) const;

    /**
     * Get cooldown progress (0.0 - 1.0)
     * @param comboName Combo name
     * @return Progress where 1.0 = ready, 0.0 = just used
     */
    float getCooldownProgress(const std::string& comboName) const;

    // ========================================================================
    // CALLBACKS
    // ========================================================================

    /**
     * Set callback for combo execution
     * @param callback Function called with (comboName, damageMultiplier)
     */
    void setComboExecutedCallback(std::function<void(const std::string&, float)> callback) {
        onComboExecuted_ = callback;
    }

    /**
     * Set callback for combo unlock
     * @param callback Function called with (weaponType, comboName, level)
     */
    void setComboUnlockedCallback(std::function<void(const std::string&, const std::string&, int)> callback) {
        onComboUnlocked_ = callback;
    }

private:
    // Reference to leveling system
    LevelingSystem* levelingSystem_ = nullptr;

    // Combo definitions for each weapon
    std::map<std::string, std::vector<ComboMove>> weaponCombos_;

    // Input buffer
    InputBuffer inputBuffer_;

    // Active combo
    ActiveCombo activeCombo_;

    // Cooldown tracking
    std::map<std::string, float> comboCooldowns_;  // comboName -> remaining cooldown time
    std::map<std::string, float> comboMaxCooldowns_; // comboName -> max cooldown (for progress)

    // Callbacks
    std::function<void(const std::string&, float)> onComboExecuted_;
    std::function<void(const std::string&, const std::string&, int)> onComboUnlocked_;

    // Internal helpers
    void initializeSwordCombos();
    void initializeBowCombos();
    void initializeStaffCombos();
    void updateInputBuffer(float currentTime);
    void updateCooldowns(float deltaTime);
    void updateActiveCombo(float deltaTime);
    bool matchesComboSequence(const std::vector<ComboInput>& sequence) const;
};

/**
 * Predefined combo move creators
 */
namespace ComboMoves {
    // SWORD COMBOS
    ComboMove createBasicSlash();           // Level 1
    ComboMove createSpinAttack();           // Level 3
    ComboMove createDashStrike();           // Level 5
    ComboMove createWhirlwind();            // Level 7
    ComboMove createExecution();            // Level 10
    ComboMove createRisingSlash();          // Level 12
    ComboMove createGroundSlam();           // Level 15
    ComboMove createBladeDance();           // Level 18
    ComboMove createCriticalEdge();         // Level 20

    // BOW COMBOS
    ComboMove createQuickShot();            // Level 1
    ComboMove createDoubleShot();           // Level 3
    ComboMove createChargedShot();          // Level 5
    ComboMove createRainOfArrows();         // Level 7
    ComboMove createSniperShot();           // Level 10
    ComboMove createPiercingArrow();        // Level 12
    ComboMove createExplosiveArrow();       // Level 15
    ComboMove createArrowStorm();           // Level 18
    ComboMove createDeadEye();              // Level 20

    // STAFF COMBOS (Magic)
    ComboMove createMagicBolt();            // Level 1
    ComboMove createArcaneBlast();          // Level 3
    ComboMove createManaShield();           // Level 5
    ComboMove createMeteorStrike();         // Level 7
    ComboMove createArcaneNova();           // Level 10
    ComboMove createTimeStop();             // Level 12
    ComboMove createElementalFury();        // Level 15
    ComboMove createArcaneStorm();          // Level 18
    ComboMove createUltimateSpell();        // Level 20
}

} // namespace CatGame
