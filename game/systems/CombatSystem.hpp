#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "status_effects.hpp"
#include <vector>
#include <functional>

namespace CatGame {

// Forward declarations
struct BlockComponent;
struct DodgeComponent;
struct ComboComponent;
struct StatusEffectsComponent;

/**
 * Hit information for combat events
 */
struct HitInfo {
    CatEngine::Entity attacker;
    CatEngine::Entity target;
    float damage;
    float finalDamage;           // After all reductions
    Engine::vec3 hitPosition;
    Engine::vec3 hitDirection;
    bool wasBlocked = false;
    bool wasPerfectBlock = false;
    bool wasDodged = false;
    bool wasCritical = false;
    int comboStep = 0;
};

/**
 * Projectile data for ranged attacks
 */
struct Projectile {
    CatEngine::Entity owner;
    Engine::vec3 position;
    Engine::vec3 velocity;
    float damage;
    float lifetime;
    float maxLifetime = 5.0f;
    bool active = true;
};

/**
 * CombatSystem - Handles all combat interactions
 *
 * Responsibilities:
 * - Execute melee attacks with range checks
 * - Spawn and update projectiles for ranged attacks
 * - Handle damage calculations
 * - Perform hit detection (sphere overlap for melee)
 * - Trigger combat events (OnHit, OnKill)
 * - Manage active projectiles
 *
 * The system processes entities with CombatComponent and handles
 * attack execution based on weapon type and cooldown state.
 */
class CombatSystem : public CatEngine::System {
public:
    /**
     * Construct combat system
     * @param priority System execution priority
     */
    explicit CombatSystem(int priority = 10);

    /**
     * Initialize the system
     * @param ecs Pointer to ECS instance
     */
    void init(CatEngine::ECS* ecs) override;

    /**
     * Update combat system each frame
     * @param dt Delta time in seconds
     */
    void update(float dt) override;

    /**
     * Shutdown the system
     */
    void shutdown() override;

    /**
     * Get system name
     */
    const char* getName() const override { return "CombatSystem"; }

    /**
     * Register callback for hit events
     * @param callback Function to call when a hit occurs
     */
    void setOnHitCallback(std::function<void(const HitInfo&)> callback);

    /**
     * Register callback for kill events
     * @param callback Function to call when an entity is killed
     */
    void setOnKillCallback(std::function<void(CatEngine::Entity, CatEngine::Entity)> callback);

    /**
     * Spawn a projectile
     * @param owner Entity that spawned the projectile
     * @param position Starting position
     * @param direction Direction and speed
     * @param damage Damage to deal on hit
     * @return Entity ID of the spawned projectile
     */
    CatEngine::Entity spawnProjectile(
        CatEngine::Entity owner,
        const Engine::vec3& position,
        const Engine::vec3& direction,
        float damage
    );

    /**
     * Get all active projectiles
     * @return Vector of active projectiles
     */
    const std::vector<Projectile>& getProjectiles() const { return projectiles_; }

    /**
     * Clear all projectiles
     */
    void clearProjectiles();

    // ===== BLOCKING SYSTEM =====

    /**
     * Start blocking for an entity
     * @param entity Entity to start blocking
     * @return true if block started
     */
    bool startBlock(CatEngine::Entity entity);

    /**
     * End blocking for an entity
     * @param entity Entity to stop blocking
     */
    void endBlock(CatEngine::Entity entity);

    /**
     * Check if entity is blocking
     * @param entity Entity to check
     * @return true if blocking
     */
    bool isBlocking(CatEngine::Entity entity) const;

    /**
     * Apply block damage reduction
     * @param entity Entity blocking
     * @param incomingDamage Original damage
     * @param attackTiming Time of attack relative to block start
     * @return Reduced damage amount
     */
    float applyBlockDamageReduction(CatEngine::Entity entity, float incomingDamage, float attackTiming);

    /**
     * Check if block is perfect (within timing window)
     * @param entity Entity blocking
     * @param attackTiming Time of attack
     * @return true if perfect block
     */
    bool isPerfectBlock(CatEngine::Entity entity, float attackTiming) const;

    /**
     * Get block stamina for entity
     * @param entity Entity to check
     * @return Current stamina (0.0 to 100.0)
     */
    float getBlockStamina(CatEngine::Entity entity) const;

    // ===== DODGING SYSTEM =====

    /**
     * Start dodge for entity
     * @param entity Entity to dodge
     * @param direction Direction to dodge
     * @return true if dodge started
     */
    bool startDodge(CatEngine::Entity entity, const Engine::vec3& direction);

    /**
     * Check if entity can dodge
     * @param entity Entity to check
     * @return true if dodge is available
     */
    bool canDodge(CatEngine::Entity entity) const;

    /**
     * Check if entity is dodging
     * @param entity Entity to check
     * @return true if dodging
     */
    bool isDodging(CatEngine::Entity entity) const;

    /**
     * Check if entity is invincible (i-frames)
     * @param entity Entity to check
     * @return true if invincible
     */
    bool isInvincible(CatEngine::Entity entity) const;

    /**
     * Get dodge cooldown progress
     * @param entity Entity to check
     * @return Progress (0.0 to 1.0, 1.0 = ready)
     */
    float getDodgeCooldownProgress(CatEngine::Entity entity) const;

    // ===== COMBO SYSTEM =====

    /**
     * Perform attack with combo tracking
     * @param entity Entity attacking
     * @param attackType Attack type character (L/H/S)
     */
    void performAttack(CatEngine::Entity entity, const std::string& attackType);

    /**
     * Perform combo attack
     * @param entity Entity attacking
     */
    void performComboAttack(CatEngine::Entity entity);

    /**
     * Check if entity is in combo
     * @param entity Entity to check
     * @return true if in active combo
     */
    bool isInCombo(CatEngine::Entity entity) const;

    /**
     * Get combo step for entity
     * @param entity Entity to check
     * @return Current combo step (0-based)
     */
    int getComboStep(CatEngine::Entity entity) const;

    /**
     * Get current damage multiplier from combo
     * @param entity Entity to check
     * @return Damage multiplier
     */
    float getCurrentDamageMultiplier(CatEngine::Entity entity) const;

    // ===== ENHANCED DAMAGE SYSTEM =====

    /**
     * Calculate damage with all modifiers
     * @param attacker Attacker entity
     * @param defender Defender entity
     * @param attack Attack data
     * @return Final damage amount
     */
    float calculateDamage(
        CatEngine::Entity attacker,
        CatEngine::Entity defender,
        const AttackData& attack
    );

    /**
     * Apply damage with type and effects
     * @param target Target entity
     * @param damage Damage amount
     * @param type Damage type
     * @param attacker Source of damage
     */
    void applyDamageWithType(
        CatEngine::Entity target,
        float damage,
        DamageType type,
        CatEngine::Entity attacker
    );

    /**
     * Apply knockback to entity
     * @param target Target entity
     * @param direction Knockback direction
     * @param force Knockback force
     */
    void applyKnockback(
        CatEngine::Entity target,
        const Engine::vec3& direction,
        float force
    );

    /**
     * Apply stun to entity
     * @param target Target entity
     * @param duration Stun duration in seconds
     */
    void applyStun(CatEngine::Entity target, float duration);

    // ===== STATUS EFFECTS =====

    /**
     * Apply status effect to entity
     * @param target Target entity
     * @param effect Effect to apply
     */
    void applyStatusEffect(CatEngine::Entity target, const StatusEffect& effect);

    /**
     * Remove status effect from entity
     * @param target Target entity
     * @param type Type of effect to remove
     */
    void removeStatusEffect(CatEngine::Entity target, StatusEffectType type);

    /**
     * Get active effects on entity
     * @param target Target entity
     * @return Vector of active effects
     */
    std::vector<StatusEffect> getActiveEffects(CatEngine::Entity target) const;

    /**
     * Process status effect ticks (DOT/HOT)
     * @param dt Delta time
     */
    void processStatusEffects(float dt);

    // ===== EVENT CALLBACKS =====

    /**
     * Callback for damage dealt
     */
    std::function<void(CatEngine::Entity, float)> onDamageDealt;

    /**
     * Callback for damage taken
     */
    std::function<void(CatEngine::Entity, float)> onDamageTaken;

    /**
     * Callback for perfect block
     */
    std::function<void(CatEngine::Entity)> onPerfectBlock;

    /**
     * Callback for dodge
     */
    std::function<void(CatEngine::Entity)> onDodge;

    /**
     * Callback for combo hit
     */
    std::function<void(CatEngine::Entity, int)> onComboHit;

private:
    // Combat processing
    void processMeleeAttacks();
    void processProjectileAttacks();
    void updateProjectiles(float dt);
    void updateBlockStates(float dt);
    void updateDodgeStates(float dt);
    void updateComboStates(float dt);

    // Hit detection
    bool checkMeleeHit(
        const Engine::vec3& attackerPos,
        const Engine::vec3& targetPos,
        float attackRange
    ) const;

    bool checkProjectileHit(
        const Engine::vec3& projectilePos,
        const Engine::vec3& targetPos,
        float hitRadius = 1.0f
    ) const;

    // Damage application
    void applyDamage(
        CatEngine::Entity attacker,
        CatEngine::Entity target,
        float damage,
        const Engine::vec3& hitPosition
    );

    // Random number generation for crits
    float randomFloat(float min, float max);

    // Event callbacks
    std::function<void(const HitInfo&)> onHitCallback_;
    std::function<void(CatEngine::Entity, CatEngine::Entity)> onKillCallback_;

    // Active projectiles
    std::vector<Projectile> projectiles_;

    // Projectile parameters
    float projectileSpeed_ = 30.0f;
    float projectileHitRadius_ = 1.0f;

    // Game time (for perfect block timing)
    float gameTime_ = 0.0f;
};

} // namespace CatGame
