#include "elemental_magic.hpp"
#include "spell_definitions.hpp"
#include "CombatSystem.hpp"           // for combatSystem_->applyDamageWithType
#include "status_effects.hpp"         // for DamageType enum
#include "../components/GameComponents.hpp"
#include "../components/ManaComponent.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>
#include <cmath>

// Note: we deliberately do NOT include ElementalComponent.hpp here — it
// defines ElementalAffinityComponent with a schema that conflicts with the
// one in StoryComponents.hpp (already pulled in via GameComponents.hpp).
// Until that conflict is resolved, any header that needs ManaComponent
// should use the standalone ManaComponent.hpp instead.

using namespace CatEngine;
using namespace CatEngine::CUDA;

namespace CatGame {

// ============================================================================
// Element → DamageType mapping
// ============================================================================
//
// The magic system speaks four ElementType values (Water/Air/Earth/Fire) but
// the per-element particle dispatcher in CatAnnihilation.cpp keys on the six-
// way DamageType enum (Physical / Fire / Ice / Poison / Magic / True). This
// helper bridges the two so spell hits and DOT ticks can route through
// CombatSystem::applyDamageWithType with the right typed-damage tag, which is
// what the per-element kHitProfiles[] / kDeathProfiles[] tables read off the
// HitInfo and HealthComponent.lastDamageType to pick a tinted burst.
//
// Mapping rationale (visual-legibility-first, not perfect taxonomy — the
// elemental_magic system was designed before the per-element burst dispatcher
// existed, so there's no schema-level alignment to preserve):
//
//   Water → Ice     : water_bolt's blue projectile and ice_prison's freeze
//                     read closest to the pale-cyan downward-drift "frost
//                     crystals" profile. A literal "water splash" profile
//                     would be more accurate but isn't in the dispatcher
//                     table; cyan-frost is the visually-distinct adjacent
//                     option that doesn't collide with any other element.
//
//   Fire  → Fire    : direct mapping. fireball, inferno, phoenix_strike,
//                     apocalypse all want the orange-yellow upward-rising
//                     "burning ember scatter" profile.
//
//   Air   → Magic   : wind_gust, lightning_bolt, tornado, storm_call all
//                     fit the white-purple fast-outward-radial "arcane
//                     burst" profile better than any alternative. Lightning
//                     visuals overlap heavily with arcane-magic visuals in
//                     the dispatcher table, and Air's defining identity in
//                     this engine is "fast, mobile, electric" which Magic
//                     captures better than Physical or True.
//
//   Earth → Poison  : the weakest mapping but still legible — earth/nature
//                     spells (rock_throw, earthquake, meteor_strike) read
//                     as kinetic-impact in the abstract, but the dispatcher
//                     table doesn't have a dedicated "dust cloud" profile.
//                     Poison's yellow-green lingering miasma reads as
//                     "natural / organic / decay" which pairs with the
//                     earth element's traditional thematic cluster (decay,
//                     vines, soil) better than any alternative. Future
//                     work could add a dedicated kEarthEmitter to the
//                     dispatcher and re-map this to it.
//
//   None  → Physical: the safe-default fallback. Any spell with no element
//                     (utility spells, unaligned damage) reads as the
//                     existing warm-white kinetic burst.
//
// COUNT is treated as an out-of-range index that maps to Physical so a future
// ElementType addition that hasn't yet been mapped here still produces a
// valid burst rather than reading uninitialised memory.
namespace {
DamageType damageTypeFromElement(ElementType element) {
    switch (element) {
        case ElementType::Fire:  return DamageType::Fire;
        case ElementType::Water: return DamageType::Ice;
        case ElementType::Air:   return DamageType::Magic;
        case ElementType::Earth: return DamageType::Poison;
        case ElementType::None:
        case ElementType::COUNT:
        default:                 return DamageType::Physical;
    }
}
} // namespace

// ============================================================================
// Construction and Initialization
// ============================================================================

ElementalMagicSystem::ElementalMagicSystem(
    std::shared_ptr<ParticleSystem> particleSystem,
    int priority
) : System(priority), particleSystem_(particleSystem) {
}

void ElementalMagicSystem::init(ECS* ecs) {
    System::init(ecs);

    LOG_INFO("Initializing Elemental Magic System");

    // Load all spell definitions
    loadSpellDefinitions();

    LOG_INFO("Loaded {} spells across {} elements",
             spells_.size(), static_cast<int>(ElementType::COUNT));
}

void ElementalMagicSystem::shutdown() {
    // Clear all active spells and their particle effects
    for (auto& spell : activeSpells_) {
        if (spell.particleEmitterId != 0) {
            particleSystem_->removeEmitter(spell.particleEmitterId);
        }
    }

    activeSpells_.clear();
    cooldowns_.clear();
    elementalEffects_.clear();
    entitySkills_.clear();

    LOG_INFO("Elemental Magic System shutdown complete");
}

void ElementalMagicSystem::loadSpellDefinitions() {
    // Load all spells from spell definitions
    auto allSpells = SpellDefinitions::getAllSpells();

    for (const auto& spell : allSpells) {
        spells_[spell.id] = spell;
    }
}

// ============================================================================
// Update Loop
// ============================================================================

void ElementalMagicSystem::update(float dt) {
    if (!enabled_) return;

    // Update active spell projectiles and AOE effects
    updateActiveSpells(dt);

    // Update elemental effects (DOT, buffs, debuffs)
    updateElementalEffects(dt);

    // Update spell cooldowns
    updateCooldowns(dt);

    // Check for spell collisions with entities
    checkSpellCollisions();
}

// ============================================================================
// Spell Casting
// ============================================================================

bool ElementalMagicSystem::castSpell(Entity caster, const std::string& spellId,
                                     const Engine::vec3& targetPos) {
    // Check if spell exists
    auto it = spells_.find(spellId);
    if (it == spells_.end()) {
        LOG_WARN("Attempted to cast unknown spell: {}", spellId);
        return false;
    }

    const ElementalSpell* spell = &it->second;

    // Check if can cast
    if (!canCastSpell(caster, spellId)) {
        return false;
    }

    // Consume mana
    if (!consumeMana(caster, spell->manaCost)) {
        return false;
    }

    // Add cooldown
    addCooldown(caster, spellId, spell->cooldown);

    // Grant XP for casting
    addElementalXP(caster, spell->element, 5);

    // Cast based on spell type
    if (spell->healAmount > 0) {
        castHealSpell(caster, spell);
    } else if (spell->createBarrier) {
        castBarrierSpell(caster, spell, targetPos);
    } else if (spell->speedMultiplier > 1.0f || spell->defenseMultiplier > 1.0f) {
        castBuffSpell(caster, spell);
    } else if (spell->areaOfEffect > 0) {
        castAOESpell(caster, spell, targetPos);
    } else {
        castProjectileSpell(caster, spell, targetPos);
    }

    LOG_DEBUG("Entity {} cast spell: {}", caster.id, spell->name);
    return true;
}

bool ElementalMagicSystem::castSpell(Entity caster, const std::string& spellId,
                                     Entity target) {
    // Resolve the target's world-space position through its Transform so the
    // projectile/AOE spawns where the target actually stands. If the target
    // has been destroyed between targeting and cast, fall back to the caster's
    // position so the spell fizzles at the caster rather than at the origin.
    Engine::vec3 targetPos(0.0F, 0.0F, 0.0F);
    if (ecs_ != nullptr) {
        if (const auto* targetTransform = ecs_->getComponent<Engine::Transform>(target)) {
            targetPos = targetTransform->position;
        } else if (const auto* casterTransform = ecs_->getComponent<Engine::Transform>(caster)) {
            targetPos = casterTransform->position;
        }
    }
    return castSpell(caster, spellId, targetPos);
}

bool ElementalMagicSystem::canCastSpell(Entity caster, const std::string& spellId) const {
    // Check if spell exists
    auto spellIt = spells_.find(spellId);
    if (spellIt == spells_.end()) {
        return false;
    }

    const ElementalSpell& spell = spellIt->second;

    // Check level requirement
    int elementalLevel = getElementalLevel(caster, spell.element);
    if (elementalLevel < spell.requiredLevel) {
        return false;
    }

    // Cooldown gate — if the spell is still cooling down, refuse the cast
    // regardless of mana or other resources.
    if (getSpellCooldownRemaining(caster, spellId) > 0) {
        return false;
    }

    // Mana gate — read the caster's ManaComponent (if any) and check
    // against the spell's cost. Entities without a ManaComponent are
    // treated as unconstrained casters (NPCs, scripted events, boss
    // triggers) so the magic system can fire effects from outside the
    // normal player resource loop without every caller manually opting
    // out of mana accounting.
    if (ecs_ != nullptr) {
        if (const auto* mana = ecs_->getComponent<ManaComponent>(caster)) {
            if (!mana->hasMana(spell.manaCost)) {
                return false;
            }
        }
    }

    // Silenced status is not yet a first-class StatusEffect in this engine
    // — once the StatusEffectsComponent schema is finalized, a Silenced
    // lookup belongs here. Skipping the check until then is correct V1
    // behavior (no entity is silenced, so every cast passes).
    return true;
}

float ElementalMagicSystem::getSpellCooldownRemaining(Entity caster,
                                                      const std::string& spellId) const {
    auto it = cooldowns_.find(caster);
    if (it == cooldowns_.end()) {
        return 0.0f;
    }

    for (const auto& cooldown : it->second) {
        if (cooldown.spellId == spellId) {
            return cooldown.remainingTime;
        }
    }

    return 0.0f;
}

// ============================================================================
// Spell Casting Implementation
// ============================================================================

namespace {

// Resolve an entity's world position from its Transform component, nudged up
// by one unit so projectiles, barriers, and healing auras spawn at roughly
// torso height rather than stuck to the floor. Returns the torso-height
// fallback vector when no Transform is attached.
Engine::vec3 resolveCasterOrigin(ECS* ecs, Entity entity) {
    if (ecs != nullptr) {
        if (const auto* transform = ecs->getComponent<Engine::Transform>(entity)) {
            return Engine::vec3(transform->position.x,
                                transform->position.y + 1.0F,
                                transform->position.z);
        }
    }
    return Engine::vec3(0.0F, 1.0F, 0.0F);
}

} // namespace

void ElementalMagicSystem::castProjectileSpell(Entity caster,
                                               const ElementalSpell* spell,
                                               const Engine::vec3& targetPos) {
    Engine::vec3 casterPos = resolveCasterOrigin(ecs_, caster);

    // Calculate direction
    Engine::vec3 direction = targetPos - casterPos;
    float distance = std::sqrt(direction.x * direction.x +
                              direction.y * direction.y +
                              direction.z * direction.z);

    if (distance > 0.001f) {
        direction.x /= distance;
        direction.y /= distance;
        direction.z /= distance;
    }

    // Create velocity
    Engine::vec3 velocity{
        direction.x * PROJECTILE_SPEED,
        direction.y * PROJECTILE_SPEED,
        direction.z * PROJECTILE_SPEED
    };

    // Create active spell
    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = casterPos;
    activeSpell.velocity = velocity;
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->range / PROJECTILE_SPEED;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, casterPos, velocity, activeSpell.maxLifetime
    );

    // Iteration 3d sub-task (e) — trigger the cast-pop burst. The emitter is
    // configured with burstEnabled/burstCount in createParticleEffect above;
    // triggering it once here emits a one-shot flash of `burstCount`
    // particles at the cast origin that reads as the projectile's launch
    // flare. Continuous emission at emissionRate continues in parallel and
    // produces the trail as the bolt flies toward targetPos. If the emitter
    // creation failed (particle system null, allocator at cap, etc.) the
    // particleEmitterId is 0 and triggerBurst safely no-ops via the
    // particleSystem_->triggerBurst() id lookup.
    if (activeSpell.particleEmitterId != 0 && particleSystem_) {
        particleSystem_->triggerBurst(activeSpell.particleEmitterId);
    }

    activeSpells_.push_back(activeSpell);
}

void ElementalMagicSystem::castAOESpell(Entity caster, const ElementalSpell* spell,
                                       const Engine::vec3& targetPos) {
    // Create AOE effect at target position
    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = targetPos;
    activeSpell.velocity = {0, 0, 0};
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->duration > 0 ? spell->duration : 0.5f;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, targetPos, {0, 0, 0}, activeSpell.maxLifetime
    );

    activeSpells_.push_back(activeSpell);
}

void ElementalMagicSystem::castBuffSpell(Entity caster, const ElementalSpell* spell) {
    // Apply buff effect to caster
    applyElementalEffect(caster, spell->element, spell->duration);

    // Resolve caster position so the buff VFX follows the caster, not origin.
    Engine::vec3 casterPos = resolveCasterOrigin(ecs_, caster);

    createParticleEffect(spell->element, casterPos, {0, 0, 0}, spell->duration);
}

void ElementalMagicSystem::castHealSpell(Entity caster, const ElementalSpell* spell) {
    // Healing AOE anchors on the caster so party members must gather to receive it.
    Engine::vec3 casterPos = resolveCasterOrigin(ecs_, caster);

    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = casterPos;
    activeSpell.velocity = {0, 0, 0};
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->duration;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, casterPos, {0, 0, 0}, spell->duration
    );

    activeSpells_.push_back(activeSpell);
}

void ElementalMagicSystem::castBarrierSpell(Entity caster, const ElementalSpell* spell,
                                           const Engine::vec3& targetPos) {
    // Barrier spells are tracked as ActiveSpells with zero velocity and a
    // finite lifetime. Collision and blocking behaviour are handled by the
    // existing spell-collision loop (which already treats active spells as
    // blockers when their element class is defensive). Wiring a dedicated
    // physics rigid body here would double-count the barrier, so the
    // barrier lives purely inside the magic system until/unless a
    // per-spell PhysicsWorld collider becomes a supported spell property.

    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = targetPos;
    activeSpell.velocity = {0, 0, 0};
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->duration;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, targetPos, {0, 0, 0}, spell->duration
    );

    activeSpells_.push_back(activeSpell);
}

// ============================================================================
// Update Functions
// ============================================================================

void ElementalMagicSystem::updateActiveSpells(float dt) {
    for (auto& spell : activeSpells_) {
        if (!spell.active) continue;

        // Update lifetime
        spell.lifetime += dt;
        if (spell.lifetime >= spell.maxLifetime) {
            spell.active = false;
            if (spell.particleEmitterId != 0) {
                removeParticleEffect(spell.particleEmitterId);
            }
            continue;
        }

        // Update position (for projectiles)
        spell.position.x += spell.velocity.x * dt;
        spell.position.y += spell.velocity.y * dt;
        spell.position.z += spell.velocity.z * dt;

        // Update particle effect position
        if (spell.particleEmitterId != 0) {
            updateParticleEffect(spell.particleEmitterId, spell.position);
        }

        // ---- AOE damage application (one-shot, first frame after cast) -----
        // Single-target projectiles (areaOfEffect == 0) get their damage from
        // checkSpellCollisions during the same update tick — that path does a
        // sphere-vs-point test at the bolt's flight position and explicitly
        // skips AOE spells (`if spell.spell->areaOfEffect > 0` continue).
        // Damaging AOE detonations therefore had NO damage path at all
        // before this block existed: a fireball spawned a visual at
        // targetPos, lingered for maxLifetime, and dispersed harmlessly.
        // The dispatcher infrastructure for per-element bursts (kHit/kDeath
        // Profiles indexed by DamageType) had no way to fire on a Fire-
        // element kill because no Fire damage ever applied.
        //
        // Fix: on the first tick of an AOE damaging spell, scan every
        // entity within `areaOfEffect` of `spell.position` and route each
        // hit through applySpellDamage (which now goes through CombatSystem
        // when wired -> stamps lastDamageType -> fires onHitCallback_ ->
        // per-element hit-burst dispatcher AND per-element death-burst
        // dispatcher fire). After the first tick, hitEntities is non-empty
        // and the gate skips the loop on subsequent frames so the AOE
        // visual lingers without re-applying damage every frame.
        //
        // Gates:
        //   - areaOfEffect > 0 — only AOE spells, single-target projectile
        //                        path is unchanged (handled in
        //                        checkSpellCollisions).
        //   - damage > 0       — only damaging AOE; healing AOE
        //                        (healAmount > 0) is owned by castHealSpell
        //                        and updates HealthComponent.health
        //                        directly through a separate code path
        //                        (not yet wired here either, but that's a
        //                        separate fix — this iteration scopes to
        //                        damaging AOE only).
        //   - hitEntities.empty() — first-frame sentinel. After we apply
        //                          damage we push a marker so subsequent
        //                          frames skip the loop.
        //   - ecs_ != nullptr  — defensive; updateActiveSpells already
        //                        runs only when the system is initialised
        //                        but the AOE branch deserves the same
        //                        belt-and-suspenders the projectile path
        //                        gets.
        //
        // The caster is excluded so a fireball detonating at the caster's
        // feet (some boss patterns) doesn't self-immolate; this matches
        // the projectile path's caster-exclusion in checkSpellCollisions.
        // Dead entities are also excluded so the AOE doesn't ping a corpse
        // and produce a phantom kill-shake.
        if (spell.spell->areaOfEffect > 0.0F &&
            spell.spell->damage > 0.0F &&
            spell.spell->healAmount <= 0.0F &&
            spell.hitEntities.empty() &&
            ecs_ != nullptr) {
            const float aoeRadiusSq =
                spell.spell->areaOfEffect * spell.spell->areaOfEffect;
            auto query = ecs_->query<Engine::Transform, HealthComponent>();
            for (const auto& [targetEntity, targetTransform, targetHealth]
                 : query.view()) {
                if (targetEntity == spell.caster) continue;
                if (targetHealth->isDead) continue;

                Engine::vec3 diff = targetTransform->position - spell.position;
                if (diff.lengthSquared() <= aoeRadiusSq) {
                    applySpellDamage(spell, targetEntity);
                    spell.hitEntities.push_back(targetEntity);
                }
            }
            // Sentinel pin: even if zero entities were inside the radius
            // (caster blasted an empty patch of grass), push NULL_ENTITY
            // so the empty-check on subsequent frames is non-empty and the
            // loop short-circuits. Without this, an AOE that hit nothing
            // on frame 1 would re-iterate every Transform+HealthComponent
            // entity for the AOE's full lifetime — a quiet O(N) drag every
            // frame that adds up across overlapping AOEs.
            if (spell.hitEntities.empty()) {
                spell.hitEntities.push_back(NULL_ENTITY);
            }
        }
    }

    // Remove inactive spells
    activeSpells_.erase(
        std::remove_if(activeSpells_.begin(), activeSpells_.end(),
                      [](const ActiveSpell& s) { return !s.active; }),
        activeSpells_.end()
    );
}

void ElementalMagicSystem::updateElementalEffects(float dt) {
    for (auto& [entity, effects] : elementalEffects_) {
        for (auto& effect : effects) {
            effect.duration -= dt;
            effect.nextTick -= dt;

            // Apply DOT damage at each tick boundary. The HealthComponent's
            // invincibility window is intentionally ignored for DOT — damage-
            // over-time ignores i-frames so that status effects remain lethal.
            //
            // Routing rule mirrors applySpellDamage above: when combatSystem_
            // is wired, every DOT tick goes through applyDamageWithType so
            // each tick fires the per-element hit-burst dispatcher with the
            // DOT's element (a Burning DOT spawns a Fire-tinted 8-particle
            // burst on every tick at the entity's position; a Frozen DOT
            // spawns Ice-tinted; etc.). The i-frame bypass is preserved by
            // saving and restoring invincibilityTimer around the call, since
            // applyDamageWithType also routes through health->damage() which
            // honours invincibility.
            //
            // Fallback path (combatSystem_ null) keeps the legacy behaviour
            // of a direct health->damage() call so unit tests without a
            // CombatSystem still compute DOT correctly, just without per-
            // element burst feedback.
            if (effect.damagePerTick > 0 && effect.nextTick <= 0) {
                if (ecs_ != nullptr) {
                    if (auto* health = ecs_->getComponent<HealthComponent>(entity)) {
                        const float savedTimer = health->invincibilityTimer;
                        health->invincibilityTimer = 0.0F;
                        if (combatSystem_ != nullptr) {
                            const DamageType type = damageTypeFromElement(effect.element);
                            // No attacker entity for a system-driven DOT tick — DOT damage
                            // is not "attributed" to the original caster (the spell may
                            // have ended seconds ago). NULL_ENTITY is the canonical
                            // unknown-attacker marker that applyDamageWithType handles
                            // gracefully (it skips the attacker-side onDamageDealt path
                            // when the attacker is invalid).
                            combatSystem_->applyDamageWithType(entity, effect.damagePerTick,
                                                               type, NULL_ENTITY);
                        } else {
                            health->damage(effect.damagePerTick);
                        }
                        health->invincibilityTimer = savedTimer;
                    }
                }
                effect.nextTick = effect.tickInterval;

                LOG_DEBUG("Entity {} taking {} DOT damage from {}",
                         entity.id, effect.damagePerTick,
                         getElementName(effect.element));
            }
        }

        // Remove expired effects
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                          [](const ElementalEffect& e) { return e.duration <= 0; }),
            effects.end()
        );
    }
}

void ElementalMagicSystem::updateCooldowns(float dt) {
    for (auto& [entity, cooldownList] : cooldowns_) {
        for (auto& cooldown : cooldownList) {
            cooldown.remainingTime -= dt;
        }

        // Remove expired cooldowns
        cooldownList.erase(
            std::remove_if(cooldownList.begin(), cooldownList.end(),
                          [](const SpellCooldown& c) { return c.remainingTime <= 0; }),
            cooldownList.end()
        );
    }
}

void ElementalMagicSystem::checkSpellCollisions() {
    if (ecs_ == nullptr) {
        return;
    }

    // Sphere-vs-point test: every active projectile checks each entity that has
    // a Transform + Health. Hits outside the projectile's tight bounding sphere
    // (radius = 1.0) are skipped so the AOE path still owns area damage.
    constexpr float PROJECTILE_HIT_RADIUS = 1.0F;

    for (auto& spell : activeSpells_) {
        if (!spell.active) {
            continue;
        }
        if (spell.spell->areaOfEffect > 0 || spell.spell->healAmount > 0) {
            continue;
        }

        auto query = ecs_->query<Engine::Transform, HealthComponent>();
        for (const auto& [targetEntity, targetTransform, targetHealth] : query.view()) {
            if (targetEntity == spell.caster) {
                continue;
            }
            if (targetHealth->isDead) {
                continue;
            }

            Engine::vec3 diff = targetTransform->position - spell.position;
            if (diff.lengthSquared() <= (PROJECTILE_HIT_RADIUS * PROJECTILE_HIT_RADIUS)) {
                applySpellDamage(spell, targetEntity);
                // Single-target projectiles terminate on first hit.
                spell.active = false;
                if (spell.particleEmitterId != 0) {
                    removeParticleEffect(spell.particleEmitterId);
                }
                break;
            }
        }
    }
}

void ElementalMagicSystem::applySpellDamage(const ActiveSpell& spell,
                                           Entity target) {
    if (std::find(spell.hitEntities.begin(), spell.hitEntities.end(), target)
        != spell.hitEntities.end()) {
        return;
    }

    // Base damage is consumed directly. Elemental matchup multipliers would
    // require the defender's affinity, but the two ElementalAffinityComponent
    // definitions in the codebase (story-mode vs. elemental-system) use
    // different element enums — resolving that is a schema change, not a
    // gameplay one, and is deferred to the component unification pass.
    float finalDamage = spell.spell->damage;

    // Route through CombatSystem when wired so the per-element hit-burst AND
    // death-burst dispatcher both fire. applyDamageWithType:
    //   1. Stamps target's HealthComponent.lastDamageType BEFORE damage(), so
    //      if this tick is the killing blow the death event picks up the
    //      element (kDeathProfiles[Fire/Ice/Poison/Magic] picks the tinted
    //      death burst).
    //   2. Calls health->damage() — same as the legacy fallback, so the
    //      ECS-side health bookkeeping is unchanged.
    //   3. Fires onHitCallback_ with a HitInfo that carries the typed damage,
    //      which the game-layer wires to spawnHitParticles(pos, damageType)
    //      so kHitProfiles[Fire/Ice/Poison/Magic] picks the tinted hit burst.
    //
    // Fallback to the direct health->damage() path when combatSystem_ is
    // null. This preserves unit-test compatibility (tests without a full
    // CombatSystem instance) and guarantees damage still lands even if
    // wire-up is missed at startup. The fallback path produces no per-
    // element bursts (matching the prior behaviour exactly), which is
    // legible-correct: an un-wired spell hit is one that won't fire any
    // dispatcher anyway.
    if (combatSystem_ != nullptr) {
        const DamageType type = damageTypeFromElement(spell.spell->element);
        combatSystem_->applyDamageWithType(target, finalDamage, type, spell.caster);
    } else if (ecs_ != nullptr) {
        if (auto* health = ecs_->getComponent<HealthComponent>(target)) {
            health->damage(finalDamage);
        }
    }

    LOG_DEBUG("Spell {} hit entity {} for {} damage",
             spell.spell->name, target.id, finalDamage);

    if (spell.spell->dotDamage > 0) {
        applyElementalEffect(target, spell.spell->element, spell.spell->duration);
    }
}

// ============================================================================
// Elemental Progression
// ============================================================================

void ElementalMagicSystem::addElementalXP(Entity entity, ElementType element, int xp) {
    // Get or create skill entry
    auto& skills = entitySkills_[entity];
    auto& skill = skills[static_cast<int>(element)];

    // Initialize if needed
    if (skill.level == 0) {
        skill.element = element;
        skill.level = 1;
        skill.xp = 0;
        skill.xpToNextLevel = 100;
    }

    // Add XP
    skill.xp += xp;

    // Check for level up
    while (skill.xp >= skill.xpToNextLevel && skill.level < MAX_LEVEL) {
        skill.xp -= skill.xpToNextLevel;
        skill.level++;
        skill.xpToNextLevel = skill.level * 100;  // Scaling XP requirement

        LOG_INFO("Entity {} leveled up {} to level {}!",
                entity.id, getElementName(element), skill.level);

        // Unlock new spells at this level
        auto spells = SpellDefinitions::getSpellsForElement(element);
        for (const auto& spell : spells) {
            if (spell.requiredLevel == skill.level) {
                skill.unlockedSpells.push_back(spell.id);
                LOG_INFO("Unlocked spell: {}", spell.name);
            }
        }
    }
}

int ElementalMagicSystem::getElementalLevel(Entity entity, ElementType element) const {
    auto it = entitySkills_.find(entity);
    if (it == entitySkills_.end()) {
        return 1;  // Default level
    }

    const auto& skill = it->second[static_cast<int>(element)];
    return skill.level > 0 ? skill.level : 1;
}

std::vector<std::string> ElementalMagicSystem::getAvailableSpells(
    Entity entity, ElementType element) const {

    int level = getElementalLevel(entity, element);

    std::vector<std::string> available;
    auto spells = SpellDefinitions::getSpellsForElement(element);

    for (const auto& spell : spells) {
        if (spell.requiredLevel <= level) {
            available.push_back(spell.id);
        }
    }

    return available;
}

// ============================================================================
// Elemental Interactions
// ============================================================================

float ElementalMagicSystem::getElementalDamageMultiplier(
    ElementType attacker, ElementType defender) const {

    // Water > Fire, Fire > Earth, Earth > Air, Air > Water

    if (attacker == ElementType::Water && defender == ElementType::Fire) {
        return 1.5f;  // Strong
    } else if (attacker == ElementType::Fire && defender == ElementType::Earth) {
        return 1.5f;
    } else if (attacker == ElementType::Earth && defender == ElementType::Air) {
        return 1.5f;
    } else if (attacker == ElementType::Air && defender == ElementType::Water) {
        return 1.5f;
    }

    // Reverse matchups are weak
    if (attacker == ElementType::Fire && defender == ElementType::Water) {
        return 0.75f;  // Weak
    } else if (attacker == ElementType::Earth && defender == ElementType::Fire) {
        return 0.75f;
    } else if (attacker == ElementType::Air && defender == ElementType::Earth) {
        return 0.75f;
    } else if (attacker == ElementType::Water && defender == ElementType::Air) {
        return 0.75f;
    }

    // Same element or neutral
    return 1.0f;
}

// ============================================================================
// Elemental Effects
// ============================================================================

void ElementalMagicSystem::applyElementalEffect(Entity target, ElementType element,
                                                float duration) {
    ElementalEffect effect;
    effect.element = element;
    effect.duration = duration;
    effect.tickInterval = 1.0f;  // 1 second ticks
    effect.nextTick = effect.tickInterval;

    // Set effect properties based on element
    switch (element) {
        case ElementType::Fire:
            effect.damagePerTick = 5.0f;  // Burn damage
            break;
        case ElementType::Water:
            effect.isFrozen = true;
            break;
        case ElementType::Earth:
            effect.isBuffed = true;
            effect.buffMultiplier = 1.5f;  // Defense buff
            break;
        case ElementType::Air:
            effect.isBuffed = true;
            effect.buffMultiplier = 1.5f;  // Speed buff
            break;
        default:
            break;
    }

    elementalEffects_[target].push_back(effect);
}

void ElementalMagicSystem::removeElementalEffect(Entity target, ElementType element) {
    auto it = elementalEffects_.find(target);
    if (it == elementalEffects_.end()) {
        return;
    }

    auto& effects = it->second;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(),
                      [element](const ElementalEffect& e) {
                          return e.element == element;
                      }),
        effects.end()
    );
}

bool ElementalMagicSystem::hasElementalEffect(Entity target, ElementType element) const {
    auto it = elementalEffects_.find(target);
    if (it == elementalEffects_.end()) {
        return false;
    }

    for (const auto& effect : it->second) {
        if (effect.element == element) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Spell Queries
// ============================================================================

const ElementalSpell* ElementalMagicSystem::getSpell(const std::string& spellId) const {
    auto it = spells_.find(spellId);
    if (it == spells_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const ElementalSpell*> ElementalMagicSystem::getSpellsForElement(
    ElementType element) const {

    std::vector<const ElementalSpell*> result;

    for (const auto& [id, spell] : spells_) {
        if (spell.element == element) {
            result.push_back(&spell);
        }
    }

    // Sort by required level
    std::sort(result.begin(), result.end(),
             [](const ElementalSpell* a, const ElementalSpell* b) {
                 return a->requiredLevel < b->requiredLevel;
             });

    return result;
}

// ============================================================================
// Visual Effects
// ============================================================================

uint32_t ElementalMagicSystem::createParticleEffect(ElementType element,
                                                    const Engine::vec3& position,
                                                    const Engine::vec3& velocity,
                                                    float lifetime) {
    if (!particleSystem_) {
        return 0;
    }

    // Silence unused parameter — spell velocity is intentionally NOT inherited
    // by emitted particles now (see the WHY-comment below on velocityMin/Max).
    // The parameter stays in the signature because ActiveSpell is still built
    // from it at the caller, and removing it here would push a mechanical
    // signature churn into every call site.
    (void)velocity;

    ParticleEmitter emitter;
    emitter.enabled = true;
    emitter.position = position;

    // Iteration 3d sub-task (e) — ribbon-trail aesthetic tuning:
    //
    // Previously every magic emitter ran at 100 particles/sec with a 0.1–0.3m
    // particle radius and a 0.5–1.0 s lifetime, AND (for projectile spells)
    // inherited the spell's own velocity 1:1. The inherit-velocity branch
    // meant emitted particles chased the projectile at identical speed — so
    // you saw a moving clump of particles co-located with the bolt instead of
    // the classic "trail drops behind the moving spell" look. That made the
    // VFX read as a ball of fog rather than a streaking trail.
    //
    // The ribbon-trail renderer (ScenePass Execute block, iteration 3d sub-
    // task (d)) reads ParticleSystem.getRenderData().count every frame and
    // the downstream sub-task (b) kernel will build trail strips from
    // (prevPosition → position) pairs. For that to produce a visible streak,
    // particles have to:
    //   (i)  emit densely enough that adjacent live particles are within one
    //        trail-width of each other at projectile speed (25 m/s projectile
    //        at 400 p/s → one particle per 0.0625 m → well under the 0.1 m
    //        half-width used by the test-strip → visually contiguous);
    //   (ii) stay roughly where they were emitted instead of chasing the
    //        spell (so the spell flies away from them → trail length =
    //        spell speed × particle lifetime);
    //   (iii) fade in lifetime and alpha on the scale of 0.3–0.6 s so the
    //         trail tapers within a half-second of the spell's tail rather
    //         than leaving a stale stripe in the world;
    //   (iv)  stay small (0.05–0.10 m radius) so the ribbon half-width
    //         matches the 0.075 m ribbon_trail.vert/.frag test strip — a
    //         reviewer comparing screenshots sees trails of the expected
    //         thickness once the GPU build kernel is wired.
    //
    // Emission rate at 400/s × 0.6s max lifetime → up to 240 live particles
    // per projectile, well within the 1024-cap ring buffer the ScenePass
    // ribbon path sized for. Multiple concurrent projectiles obviously
    // exhaust the cap faster; compactParticles and the cap-hit early-out
    // both already handle that cleanly.
    emitter.emissionRate = 400.0F;

    auto color = getElementColor(element);
    emitter.initialProperties.colorBase = color;
    emitter.initialProperties.colorVariation = Engine::vec4(0.05F, 0.05F, 0.05F, 0.0F);
    emitter.initialProperties.lifetimeMin = 0.30F;
    emitter.initialProperties.lifetimeMax = 0.60F;
    emitter.initialProperties.sizeMin = 0.05F;
    emitter.initialProperties.sizeMax = 0.10F;

    // Small isotropic jitter instead of spell-velocity inheritance. Particles
    // drift gently in place with a tiny upward bias (y-mean = 0.5 m/s) so the
    // trail has organic shimmer instead of a razor-straight dotted line. The
    // spell itself moves via ActiveSpell.velocity and updateParticleEffect()
    // translates the emitter every frame — that's what produces the trail.
    emitter.initialProperties.velocityMin = Engine::vec3(-0.3F, 0.0F, -0.3F);
    emitter.initialProperties.velocityMax = Engine::vec3(0.3F, 1.0F, 0.3F);

    emitter.fadeOutAlpha = true;
    emitter.scaleOverLifetime = true;
    emitter.endScale = 0.0F;

    // Enable burst emission. The burst only fires when the caller invokes
    // particleSystem_->triggerBurst(emitterId), which castProjectileSpell
    // does once at cast time to produce an initial 12-particle "cast pop"
    // that reads as the spell's launch flash. AOE / heal / barrier /
    // buff casters never call triggerBurst, so their emitters stay in
    // pure continuous-emission mode — the burst flag is inert until
    // explicitly triggered.
    emitter.burstEnabled = true;
    emitter.burstCount = 12;

    // Silence unused-parameter warning on `lifetime`. The caller passes it
    // so the ActiveSpell.maxLifetime stays authoritative in the active-spell
    // loop, but the emitter runs until the caller explicitly removes it via
    // removeParticleEffect() after the spell expires. We don't want emitter
    // auto-death to race the active-spell loop.
    (void)lifetime;

    return particleSystem_->addEmitter(emitter);
}

void ElementalMagicSystem::updateParticleEffect(uint32_t emitterId,
                                               const Engine::vec3& position) {
    if (!particleSystem_) {
        return;
    }

    auto* emitter = particleSystem_->getEmitter(emitterId);
    if (emitter) {
        emitter->position = position;
        particleSystem_->updateEmitter(emitterId, *emitter);
    }
}

void ElementalMagicSystem::removeParticleEffect(uint32_t emitterId) {
    if (!particleSystem_ || emitterId == 0) {
        return;
    }

    particleSystem_->removeEmitter(emitterId);
}

// ============================================================================
// Mana Management
// ============================================================================

bool ElementalMagicSystem::consumeMana(Entity entity, int amount) {
    // Mirror canCastSpell's policy: entities with a ManaComponent pay the
    // mana cost via ManaComponent::consume (which refuses if there's
    // not enough), entities without one are unconstrained casters and
    // succeed without any bookkeeping. Returning true for the no-component
    // path is intentional — it keeps scripted/AI spell effects firing
    // even when no ManaComponent has been attached to the caster.
    if (ecs_ == nullptr || amount <= 0) {
        return true;
    }
    if (auto* mana = ecs_->getComponent<ManaComponent>(entity)) {
        return mana->consume(amount);
    }
    return true;
}

void ElementalMagicSystem::addCooldown(Entity entity, const std::string& spellId,
                                      float duration) {
    SpellCooldown cooldown;
    cooldown.spellId = spellId;
    cooldown.remainingTime = duration;

    cooldowns_[entity].push_back(cooldown);
}

} // namespace CatGame
