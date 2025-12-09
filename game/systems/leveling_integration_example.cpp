/**
 * Leveling System Integration Example
 *
 * This file demonstrates how to integrate the leveling and combo systems
 * into the Cat Annihilation game. Copy and adapt this code for your needs.
 */

#include "leveling_system.hpp"
#include "combo_system.hpp"
#include "xp_tables.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/CombatComponent.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/core/Logger.hpp"

namespace CatGame {

/**
 * Example game class showing complete integration
 */
class GameWithLeveling {
public:
    GameWithLeveling() {
        // Constructor
    }

    void initialize() {
        // Initialize leveling system
        levelingSystem_.initialize();
        comboSystem_.initialize(&levelingSystem_);
        levelingSystem_.setComboSystem(&comboSystem_);

        // Setup callbacks
        setupCallbacks();

        // Player stats
        playerHealth_ = 100.0f;
        maxPlayerHealth_ = 100.0f;
        currentWeapon_ = "sword";
        gameTime_ = 0.0f;
    }

    void update(float deltaTime) {
        gameTime_ += deltaTime;

        // Update systems
        levelingSystem_.update(deltaTime);
        comboSystem_.update(deltaTime);

        // Apply regeneration if out of combat
        if (levelingSystem_.hasAbility("regeneration")) {
            const auto& stats = levelingSystem_.getStats();
            levelingSystem_.applyRegeneration(deltaTime, playerHealth_, stats.maxHealth);
        }

        // Sync max health with leveling system
        const auto& stats = levelingSystem_.getStats();
        maxPlayerHealth_ = static_cast<float>(stats.maxHealth);

        // Update UI
        updateUI();
    }

    // ========================================================================
    // COMBAT EXAMPLE
    // ========================================================================

    void handlePlayerAttack() {
        // Check if combo system detected a combo
        const ComboMove* combo = comboSystem_.checkForCombo(currentWeapon_);

        if (combo && !comboSystem_.isOnCooldown(combo->name)) {
            // Execute combo
            executeCombo(*combo);
        } else {
            // Regular attack
            executeRegularAttack();
        }
    }

    void executeCombo(const ComboMove& combo) {
        // Start combo (sets cooldown)
        if (!comboSystem_.startCombo(combo)) {
            return; // Combo failed to start
        }

        // Calculate base damage
        float baseDamage = getWeaponBaseDamage(currentWeapon_);

        // Apply combo multiplier
        float comboDamage = baseDamage * combo.damageMultiplier;

        // Apply weapon skill multiplier
        float skillMultiplier = levelingSystem_.getWeaponDamageMultiplier(currentWeapon_);
        float finalDamage = comboDamage * skillMultiplier;

        // Apply crit chance
        float critChance = levelingSystem_.getEffectiveCritChance() +
                          levelingSystem_.getWeaponCritBonus(currentWeapon_);

        if (rollCriticalHit(critChance)) {
            float critMultiplier = levelingSystem_.getCritMultiplier();
            finalDamage *= critMultiplier;
            showCriticalHitEffect();
        }

        // Deal damage to enemies
        if (combo.hasAoE) {
            dealAoEDamage(combo.aoeRadius, finalDamage);
        } else {
            dealSingleTargetDamage(finalDamage);
        }

        // Knockback effect
        if (combo.hasKnockback) {
            applyKnockback(combo.knockbackForce);
        }

        // Player invincibility during combo
        if (combo.hasInvincibilityFrames) {
            playerInvincible_ = true;
            invincibilityDuration_ = combo.animationDuration;
        }

        // Visual feedback
        playComboAnimation(combo);
        showComboNameUI(combo.name);
    }

    void executeRegularAttack() {
        float baseDamage = getWeaponBaseDamage(currentWeapon_);
        float skillMultiplier = levelingSystem_.getWeaponDamageMultiplier(currentWeapon_);
        float finalDamage = baseDamage * skillMultiplier;

        // Roll for crit
        float critChance = levelingSystem_.getEffectiveCritChance() +
                          levelingSystem_.getWeaponCritBonus(currentWeapon_);

        if (rollCriticalHit(critChance)) {
            finalDamage *= levelingSystem_.getCritMultiplier();
            showCriticalHitEffect();
        }

        dealSingleTargetDamage(finalDamage);
    }

    void onEnemyHit(float damageDealt) {
        // Award weapon XP for hitting enemy
        bool weaponLeveledUp = levelingSystem_.addWeaponXPFromDamage(currentWeapon_, damageDealt);

        if (weaponLeveledUp) {
            int newLevel = levelingSystem_.getWeaponLevel(currentWeapon_);
            // UI notification handled by callback
        }

        // Enter combat state (disables regeneration temporarily)
        levelingSystem_.enterCombat();
        timeSinceLastCombat_ = 0.0f;
    }

    void onEnemyKilled(const std::string& enemyType) {
        // Award cat XP based on enemy type
        int xpReward = 0;

        if (enemyType == "small_dog") {
            xpReward = EnemyXPRewards::SMALL_DOG;
        } else if (enemyType == "medium_dog") {
            xpReward = EnemyXPRewards::MEDIUM_DOG;
        } else if (enemyType == "large_dog") {
            xpReward = EnemyXPRewards::LARGE_DOG;
        } else if (enemyType == "elite_dog") {
            xpReward = EnemyXPRewards::ELITE_DOG;
        } else if (enemyType == "boss") {
            xpReward = EnemyXPRewards::BOSS;
        }

        bool catLeveledUp = levelingSystem_.addXP(xpReward);

        // Show XP gain UI
        showXPGainUI(xpReward);
    }

    void onWaveComplete() {
        // Award quest XP for completing wave
        levelingSystem_.addXP(QuestXPRewards::MEDIUM_QUEST);

        // Exit combat (allows regeneration after delay)
        levelingSystem_.exitCombat();
    }

    void onLocationDiscovered() {
        // Award discovery XP
        levelingSystem_.addXP(DiscoveryXPRewards::LOCATION);
        showDiscoveryUI("New Location Discovered!");
    }

    // ========================================================================
    // DAMAGE HANDLING
    // ========================================================================

    void takeDamage(float damage) {
        // Check for combo invincibility
        if (comboSystem_.hasComboInvincibility()) {
            return; // Invincible during combo
        }

        // Check for dodge (Agility ability)
        if (levelingSystem_.hasAbility("agility")) {
            float dodgeChance = levelingSystem_.getStats().dodgeChance;
            if (rollDodge(dodgeChance)) {
                showDodgeEffect();
                return; // Dodged!
            }
        }

        // Apply defense reduction
        int defense = levelingSystem_.getEffectiveDefense();
        float damageReduction = std::min(0.8f, defense * 0.01f); // Max 80% reduction
        float reducedDamage = damage * (1.0f - damageReduction);

        // Take damage
        playerHealth_ -= reducedDamage;

        // Check for death
        if (playerHealth_ <= 0.0f) {
            // Try Nine Lives revival
            if (levelingSystem_.canRevive()) {
                levelingSystem_.useRevive(playerHealth_, maxPlayerHealth_);
                showNineLivesReviveEffect();
                playReviveSound();
                return; // Revived!
            }

            // Player dies
            onPlayerDeath();
        }
    }

    // ========================================================================
    // INPUT HANDLING
    // ========================================================================

    void onInputEvent(const std::string& inputType) {
        ComboInput comboInput;

        if (inputType == "attack") {
            comboInput = ComboInput::Attack;
        } else if (inputType == "special") {
            comboInput = ComboInput::Special;
        } else if (inputType == "jump") {
            comboInput = ComboInput::Jump;
            // Handle double jump if Agility unlocked
            if (levelingSystem_.hasDoubleJump() && isInAir_ && !hasDoubleJumped_) {
                performDoubleJump();
                hasDoubleJumped_ = true;
            }
        } else if (inputType == "dodge") {
            comboInput = ComboInput::Dodge;
            // Faster dodge with Agility
            float dodgeSpeed = 1.0f * levelingSystem_.getDodgeSpeedMultiplier();
            performDodge(dodgeSpeed);
        } else if (inputType == "direction") {
            comboInput = ComboInput::Direction;
        }

        // Register input for combo detection
        comboSystem_.registerInput(comboInput, gameTime_);
    }

    void onWeaponSwitch(const std::string& newWeapon) {
        currentWeapon_ = newWeapon;
        // Clear combo input buffer when switching weapons
        comboSystem_.clearInputs();
    }

    // ========================================================================
    // ELEMENTAL MAGIC EXAMPLE
    // ========================================================================

    void castElementalSpell(ElementType element) {
        if (currentWeapon_ != "staff") {
            return; // Can only cast with staff
        }

        // Calculate spell damage with elemental bonus
        float baseDamage = getWeaponBaseDamage("staff");
        float elementalMultiplier = levelingSystem_.getElementalDamageMultiplier(element);
        float spellDamage = baseDamage * elementalMultiplier;

        // Apply effect duration bonus
        float baseDuration = 5.0f;
        float durationMultiplier = levelingSystem_.getElementalDurationMultiplier(element);
        float effectDuration = baseDuration * durationMultiplier;

        // Apply AoE radius bonus
        float baseRadius = 5.0f;
        float radiusMultiplier = levelingSystem_.getElementalRadiusMultiplier(element);
        float aoeRadius = baseRadius * radiusMultiplier;

        // Cast spell with bonuses
        switch (element) {
            case ElementType::Fire:
                castFireSpell(spellDamage, effectDuration, aoeRadius);
                break;
            case ElementType::Water:
                castWaterSpell(spellDamage, effectDuration, aoeRadius);
                break;
            case ElementType::Earth:
                castEarthSpell(spellDamage, effectDuration, aoeRadius);
                break;
            case ElementType::Air:
                castAirSpell(spellDamage, effectDuration, aoeRadius);
                break;
            default:
                break;
        }

        // Award elemental XP
        int elementalXP = calculateWeaponXP(spellDamage);
        levelingSystem_.addElementalXP(element, elementalXP);
    }

    // ========================================================================
    // UI RENDERING
    // ========================================================================

    void updateUI() {
        // Display cat stats
        const auto& stats = levelingSystem_.getStats();

        renderPlayerStats(
            stats.level,
            levelingSystem_.getXP(),
            levelingSystem_.getXPToNextLevel(),
            levelingSystem_.getXPProgress(),
            playerHealth_,
            stats.maxHealth,
            stats.attack,
            stats.defense,
            stats.speed
        );

        // Display abilities
        renderAbilities(
            levelingSystem_.hasAbility("regeneration"),
            levelingSystem_.hasAbility("agility"),
            levelingSystem_.hasAbility("nineLives"),
            levelingSystem_.hasAbility("predatorInstinct"),
            levelingSystem_.hasAbility("alphaStrike")
        );

        // Display weapon skills
        renderWeaponSkills(
            levelingSystem_.getWeaponLevel("sword"),
            levelingSystem_.getWeaponLevel("bow"),
            levelingSystem_.getWeaponLevel("staff"),
            levelingSystem_.getWeaponDamageMultiplier(currentWeapon_)
        );

        // Display unlocked combos
        auto combos = comboSystem_.getUnlockedCombos(currentWeapon_);
        for (const auto& combo : combos) {
            renderCombo(
                combo.name,
                combo.description,
                comboSystem_.isOnCooldown(combo.name),
                comboSystem_.getCooldownProgress(combo.name)
            );
        }

        // Display elemental skill levels (if using staff)
        if (currentWeapon_ == "staff") {
            renderElementalSkills(
                levelingSystem_.getElementalLevel(ElementType::Fire),
                levelingSystem_.getElementalLevel(ElementType::Water),
                levelingSystem_.getElementalLevel(ElementType::Earth),
                levelingSystem_.getElementalLevel(ElementType::Air)
            );
        }

        // Predator Instinct: Show enemy details
        if (levelingSystem_.canSeeEnemyDetails()) {
            renderEnemyHealthBars();
            renderEnemyLevels();
            renderEnemyWeaknesses();
        }
    }

    // ========================================================================
    // BATTLE MANAGEMENT
    // ========================================================================

    void startBattle() {
        // Reset Nine Lives at start of battle
        levelingSystem_.resetRevive();
        levelingSystem_.enterCombat();
    }

    void endBattle() {
        levelingSystem_.exitCombat();
    }

    // ========================================================================
    // SAVE/LOAD
    // ========================================================================

    void saveProgress(const std::string& filepath) {
        // Save leveling data
        const auto& stats = levelingSystem_.getStats();
        const auto& weaponSkills = levelingSystem_.getWeaponSkills();

        // Serialize to file (pseudo-code)
        // SaveFile file(filepath);
        // file.write("cat_level", stats.level);
        // file.write("cat_xp", stats.xp);
        // file.write("sword_level", weaponSkills.sword.level);
        // file.write("bow_level", weaponSkills.bow.level);
        // file.write("staff_level", weaponSkills.staff.level);
        // ... etc
    }

    void loadProgress(const std::string& filepath) {
        // Load and restore leveling data
        // LoadFile file(filepath);
        // int catLevel = file.read<int>("cat_level");
        // auto& stats = levelingSystem_.getStatsRef();
        // stats.level = catLevel;
        // levelingSystem_.recalculateStats();
        // levelingSystem_.checkAbilityUnlocks();
        // ... etc
    }

private:
    // Systems
    LevelingSystem levelingSystem_;
    ComboSystem comboSystem_;

    // Player state
    float playerHealth_;
    float maxPlayerHealth_;
    std::string currentWeapon_;
    float gameTime_;
    float timeSinceLastCombat_;
    bool playerInvincible_;
    float invincibilityDuration_;
    bool isInAir_;
    bool hasDoubleJumped_;

    // ========================================================================
    // CALLBACKS
    // ========================================================================

    void setupCallbacks() {
        // Cat level up
        levelingSystem_.setLevelUpCallback([this](int newLevel) {
            showLevelUpNotification("Level Up!", "You reached level " + std::to_string(newLevel));
            playSound("level_up.wav");

            // Sync max health
            const auto& stats = levelingSystem_.getStats();
            maxPlayerHealth_ = static_cast<float>(stats.maxHealth);
            playerHealth_ = maxPlayerHealth_; // Restore health on level up
        });

        // Ability unlock
        levelingSystem_.setAbilityUnlockCallback([this](const std::string& abilityName, int level) {
            showAbilityUnlockNotification(abilityName, level);
            playSound("ability_unlock.wav");
        });

        // Weapon skill level up
        levelingSystem_.setWeaponLevelUpCallback([this](const std::string& weaponType, int newLevel) {
            showWeaponSkillUpNotification(weaponType, newLevel);
            playSound("skill_up.wav");
        });

        // Combo executed
        comboSystem_.setComboExecutedCallback([this](const std::string& comboName, float multiplier) {
            showComboExecutedUI(comboName, multiplier);
            playSound("combo.wav");
        });
    }

    // ========================================================================
    // HELPER FUNCTIONS (Placeholders)
    // ========================================================================

    float getWeaponBaseDamage(const std::string& weapon) {
        if (weapon == "sword") return 25.0f;
        if (weapon == "bow") return 30.0f;
        if (weapon == "staff") return 40.0f;
        return 10.0f;
    }

    bool rollCriticalHit(float critChance) {
        return (rand() % 10000) < (critChance * 10000);
    }

    bool rollDodge(float dodgeChance) {
        return (rand() % 10000) < (dodgeChance * 10000);
    }

    // Placeholder functions (implement with actual game logic)
    void dealAoEDamage(float radius, float damage) {
        // Find all enemies within radius of player and apply damage
        // This would typically query the ECS for entities with HealthComponent within range
        Engine::Logger::debug("AOE Damage: {} in {}m radius", damage, radius);
    }

    void dealSingleTargetDamage(float damage) {
        // Apply damage to the currently targeted enemy
        // Uses raycasting or target lock system to find the target
        Engine::Logger::debug("Single Target Damage: {}", damage);
    }

    void applyKnockback(float force) {
        // Apply knockback force to enemies hit by the attack
        // Modifies velocity component of affected entities
        Engine::Logger::debug("Knockback applied with force: {}", force);
    }

    void playComboAnimation(const ComboMove& combo) {
        // Trigger the animation system to play the combo animation
        // Sets animation state and blends between poses
        Engine::Logger::debug("Playing combo animation: {}", combo.name);
    }

    void showComboNameUI(const std::string& name) {
        // Display combo name with stylized text effect on screen
        // Fades in, holds, then fades out
        Engine::Logger::debug("Showing combo UI: {}", name);
    }

    void showCriticalHitEffect() {
        // Spawn particle effects and screen flash for critical hit
        // Also plays impact sound
        Engine::Logger::debug("Critical hit effect!");
    }

    void showDodgeEffect() {
        // Display dodge visual (afterimage/blur) and play dodge sound
        Engine::Logger::debug("Dodge effect!");
    }

    void showNineLivesReviveEffect() {
        // Dramatic revival effect with particles, screen effects
        // Displays "Nine Lives!" text on screen
        Engine::Logger::debug("Nine Lives revive effect!");
    }

    void performDoubleJump() {
        // Apply upward velocity for second jump
        // Spawn jump particles at player feet
        Engine::Logger::debug("Double jump performed");
    }

    void performDodge(float speed) {
        // Apply dodge velocity in movement direction
        // Grant brief invincibility frames
        Engine::Logger::debug("Dodge performed with speed multiplier: {}", speed);
    }

    void castFireSpell(float damage, float duration, float radius) {
        // Spawn fire projectile/effect, apply burn DOT to enemies
        Engine::Logger::debug("Fire spell: {} damage, {}s duration, {}m radius", damage, duration, radius);
    }

    void castWaterSpell(float damage, float duration, float radius) {
        // Spawn water projectile/effect, apply slow to enemies
        Engine::Logger::debug("Water spell: {} damage, {}s duration, {}m radius", damage, duration, radius);
    }

    void castEarthSpell(float damage, float duration, float radius) {
        // Spawn earth barrier/projectile, apply stun to enemies
        Engine::Logger::debug("Earth spell: {} damage, {}s duration, {}m radius", damage, duration, radius);
    }

    void castAirSpell(float damage, float duration, float radius) {
        // Spawn wind gust/tornado, apply knockback to enemies
        Engine::Logger::debug("Air spell: {} damage, {}s duration, {}m radius", damage, duration, radius);
    }

    void onPlayerDeath() {
        // Handle player death: stop time, show death screen, respawn options
        Engine::Logger::info("Player has died!");
    }

    void playSound(const std::string& sound) {
        // Queue sound effect to audio system
        Engine::Logger::debug("Playing sound: {}", sound);
    }

    void playReviveSound() {
        // Play dramatic revival sound effect
        playSound("revive.wav");
    }

    void showXPGainUI(int xp) {
        // Display floating "+XP" text near player
        Engine::Logger::debug("XP gained: +{}", xp);
    }

    void showDiscoveryUI(const std::string& text) {
        // Display discovery banner at top of screen
        Engine::Logger::info("Discovery: {}", text);
    }

    void showLevelUpNotification(const std::string& title, const std::string& message) {
        // Display level up popup with fanfare
        Engine::Logger::info("{}: {}", title, message);
    }

    void showAbilityUnlockNotification(const std::string& ability, int level) {
        // Display ability unlock popup with icon and description
        Engine::Logger::info("Ability Unlocked at Lv.{}: {}", level, ability);
    }

    void showWeaponSkillUpNotification(const std::string& weapon, int level) {
        // Display weapon skill level up notification
        Engine::Logger::info("{} Mastery increased to Lv.{}", weapon, level);
    }

    void showComboExecutedUI(const std::string& combo, float multiplier) {
        // Display combo name with damage multiplier
        Engine::Logger::debug("Combo: {} (x{})", combo, multiplier);
    }

    void renderPlayerStats(int level, int xp, int xpNeeded, float progress, float hp, int maxHp, int atk, int def, int spd) {
        // Render player stats HUD panel
        // Shows level, XP bar, health bar, and stat values
        (void)level; (void)xp; (void)xpNeeded; (void)progress;
        (void)hp; (void)maxHp; (void)atk; (void)def; (void)spd;
    }

    void renderAbilities(bool regen, bool agility, bool nineLives, bool predator, bool alpha) {
        // Render ability icons with active/inactive states
        (void)regen; (void)agility; (void)nineLives; (void)predator; (void)alpha;
    }

    void renderWeaponSkills(int swordLvl, int bowLvl, int staffLvl, float damageBonus) {
        // Render weapon skill levels and current damage bonus
        (void)swordLvl; (void)bowLvl; (void)staffLvl; (void)damageBonus;
    }

    void renderCombo(const std::string& name, const std::string& desc, bool cooldown, float progress) {
        // Render combo in combo list with cooldown indicator
        (void)name; (void)desc; (void)cooldown; (void)progress;
    }

    void renderElementalSkills(int fire, int water, int earth, int air) {
        // Render elemental skill levels with element icons
        (void)fire; (void)water; (void)earth; (void)air;
    }

    void renderEnemyHealthBars() {
        // Render health bars above enemies (requires Predator Instinct)
    }

    void renderEnemyLevels() {
        // Render enemy level indicators (requires Predator Instinct)
    }

    void renderEnemyWeaknesses() {
        // Render enemy weakness icons (requires Predator Instinct)
    }
};

} // namespace CatGame
