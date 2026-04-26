#include "PlayerControlSystem.hpp"
#include "CombatSystem.hpp"
#include "elemental_magic.hpp"
#include "../components/GameComponents.hpp"
#include "../components/EnemyComponent.hpp"
#include "../world/Terrain.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <limits>
#include <string>

namespace CatGame {

PlayerControlSystem::PlayerControlSystem(Engine::Input* input, int priority)
    : System(priority)
    , input_(input)
    , playerEntity_(CatEngine::NULL_ENTITY)
{
}

void PlayerControlSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);
}

void PlayerControlSystem::shutdown() {
    // Cleanup if needed
}

void PlayerControlSystem::update(float dt) {
    if (!isEnabled() || !controlEnabled_) {
        return;
    }

    // Validate player entity
    if (!ecs_->isAlive(playerEntity_)) {
        return;
    }

    // Update current time for double-tap detection
    currentTime_ += dt;

    if (autoplayMode_) {
        // Autoplay branch: the AI policy owns movement + attack for this
        // frame. Mouse-look is still driven by raw mouse delta so a human
        // can frame the camera while the cat fights on its own — zero mouse
        // movement from an unattended headless run is a no-op, so this is
        // safe either way. Block/dodge/jump are intentionally skipped: the
        // smoke run doesn't exercise those paths, and invoking them without
        // the double-tap state machine would create spurious dodge rolls.
        processMouseLook(dt);
        updateAutoplay(dt);
        updateCamera(dt);
        return;
    }

    // Process all input
    processMouseLook(dt);
    processBlockInput();        // Check blocking first (affects movement)
    processDodgeInput(dt);       // Check dodge input
    processMovementInput(dt);
    processJumpInput();
    processAttackInput();

    // Update camera
    updateCamera(dt);
}

void PlayerControlSystem::setPlayerEntity(CatEngine::Entity entity) {
    playerEntity_ = entity;
}

void PlayerControlSystem::setCameraOffset(const Engine::vec3& offset) {
    cameraOffset_ = offset;
}

void PlayerControlSystem::setMouseSensitivity(float sensitivity) {
    mouseSensitivity_ = sensitivity;
}

void PlayerControlSystem::setControlEnabled(bool enabled) {
    controlEnabled_ = enabled;
}

void PlayerControlSystem::setCombatSystem(CombatSystem* combatSystem) {
    combatSystem_ = combatSystem;
}

void PlayerControlSystem::setCinematicOrbit(bool enabled, float yawRateRadPerSec) {
    // Idempotent setter — flipping the gate at any frame boundary is safe
    // because updateCamera() reads the field fresh each frame. We don't
    // snap cameraYaw_ to a specific starting value when enabling: whatever
    // the camera was last pointing at becomes the orbit's t=0 phase, which
    // is the smoothest visual transition into orbiting (no yaw teleport
    // when the flag flips).
    cinematicOrbitEnabled_ = enabled;
    cinematicOrbitYawRate_ = yawRateRadPerSec;
}

void PlayerControlSystem::setElementalMagicSystem(ElementalMagicSystem* magicSystem) {
    magicSystem_ = magicSystem;
}

Engine::Transform PlayerControlSystem::getCameraTransform() const {
    Engine::Transform transform;
    transform.position = cameraPosition_;

    // Create rotation from yaw and pitch
    Engine::Quaternion yawRot = Engine::Quaternion(Engine::vec3(0.0f, 1.0f, 0.0f), cameraYaw_);
    Engine::Quaternion pitchRot = Engine::Quaternion(Engine::vec3(1.0f, 0.0f, 0.0f), cameraPitch_);
    transform.rotation = yawRot * pitchRot;

    return transform;
}

void PlayerControlSystem::processMovementInput(float dt) {
    auto* movement = ecs_->getComponent<MovementComponent>(playerEntity_);
    auto* transform = ecs_->getComponent<Engine::Transform>(playerEntity_);

    if (!movement || !transform) {
        return;
    }

    // Get input axes
    Engine::vec3 inputDirection(0.0f, 0.0f, 0.0f);

    if (input_->isKeyDown(Engine::Input::Key::W)) {
        inputDirection.z -= 1.0f;
    }
    if (input_->isKeyDown(Engine::Input::Key::S)) {
        inputDirection.z += 1.0f;
    }
    if (input_->isKeyDown(Engine::Input::Key::A)) {
        inputDirection.x -= 1.0f;
    }
    if (input_->isKeyDown(Engine::Input::Key::D)) {
        inputDirection.x += 1.0f;
    }

    // Normalize input direction
    float inputMagnitude = inputDirection.length();

    if (inputMagnitude > movementDeadzone_) {
        inputDirection = inputDirection.normalized();

        // Transform input direction based on camera yaw
        Engine::Quaternion cameraRotation = Engine::Quaternion(
            Engine::vec3(0.0f, 1.0f, 0.0f),
            cameraYaw_
        );
        Engine::vec3 worldDirection = cameraRotation.rotate(inputDirection);

        // Apply movement
        movement->applyMovement(worldDirection, dt);

        // Rotate player to face movement direction
        if (movement->isMoving()) {
            Engine::vec3 moveDir = movement->getMovementDirection();
            if (moveDir.length() > 0.01f) {
                float targetYaw = std::atan2(moveDir.x, -moveDir.z);
                transform->rotation = Engine::Quaternion(
                    Engine::vec3(0.0f, 1.0f, 0.0f),
                    targetYaw
                );
            }
        }
    } else {
        // No input, apply deceleration
        movement->applyDeceleration(dt);
    }

    // Apply gravity
    movement->applyGravity(GRAVITY, dt);

    // Update position based on velocity
    transform->position += movement->velocity * dt;

    // Ground collision — follow the terrain heightfield when one is wired,
    // otherwise fall back to the y=0 plane so standalone tests still work.
    float groundY = 0.0f;
    if (terrain_ != nullptr) {
        groundY = terrain_->getHeightAt(transform->position.x, transform->position.z);
    }
    if (transform->position.y <= groundY && movement->velocity.y <= 0.0f) {
        transform->position.y = groundY;
        movement->land(groundY);
    }

    // Update health component invincibility
    auto* health = ecs_->getComponent<HealthComponent>(playerEntity_);
    if (health) {
        health->updateInvincibility(dt);
    }

    // NOTE: CombatComponent::attackCooldown is ticked by CombatSystem::update,
    // not here. Ticking it from PlayerControlSystem (which runs at priority 0,
    // before CombatSystem at priority 10) caused the first-frame hit-detection
    // window inside processMeleeAttacks to be missed by one sub-frame of drift:
    // startAttack() sets attackCooldown = cooldownDuration, PlayerControlSystem
    // then subtracted dt (~0.0167 s at 60 fps), and the melee pass's
    // "attackCooldown >= cooldownDuration - 0.016" guard just barely tripped,
    // causing the processor to conclude "already processed, skip" on the same
    // frame the attack began — so no damage was ever applied. Letting
    // CombatSystem own the tick (which runs AFTER processMeleeAttacks) keeps
    // attackCooldown at its full value for the one frame processMeleeAttacks
    // needs to see it, and the subsequent frames correctly report "already
    // processed".
}

void PlayerControlSystem::processMouseLook(float dt) {
    // Get mouse delta
    Engine::f64 mouseDx, mouseDy;
    input_->getMouseDelta(mouseDx, mouseDy);

    // Update camera rotation
    cameraYaw_ -= static_cast<float>(mouseDx) * mouseSensitivity_;
    cameraPitch_ -= static_cast<float>(mouseDy) * mouseSensitivity_;

    // Clamp pitch
    cameraPitch_ = std::max(cameraMinPitch_, std::min(cameraMaxPitch_, cameraPitch_));

    // Wrap yaw to -PI to PI
    while (cameraYaw_ > Engine::Math::PI) {
        cameraYaw_ -= 2.0f * Engine::Math::PI;
    }
    while (cameraYaw_ < -Engine::Math::PI) {
        cameraYaw_ += 2.0f * Engine::Math::PI;
    }
}

void PlayerControlSystem::processJumpInput() {
    auto* movement = ecs_->getComponent<MovementComponent>(playerEntity_);

    if (!movement) {
        return;
    }

    if (input_->isKeyPressed(Engine::Input::Key::Space)) {
        movement->jump();
    }
}

void PlayerControlSystem::processAttackInput() {
    auto* combat = ecs_->getComponent<CombatComponent>(playerEntity_);

    if (!combat) {
        return;
    }

    // Check if blocking (right mouse button now blocks instead of attacking)
    bool isBlocking = input_->isMouseButtonDown(Engine::Input::MouseButton::Right);
    if (isBlocking) {
        return;  // Can't attack while blocking
    }

    // Left mouse button for attacks
    bool attackInput = input_->isMouseButtonPressed(Engine::Input::MouseButton::Left);

    // Shift + Left mouse for heavy attacks
    bool shiftHeld = input_->isKeyDown(Engine::Input::Key::LeftShift) ||
                     input_->isKeyDown(Engine::Input::Key::RightShift);

    if (attackInput) {
        if (combat->startAttack() && combatSystem_ != nullptr) {
            std::string attackType = shiftHeld ? "H" : "L";
            combatSystem_->performAttack(playerEntity_, attackType);
        }
    }
}

void PlayerControlSystem::processBlockInput() {
    if (!combatSystem_) {
        return;  // Need combat system to handle blocking
    }

    // Right mouse button to block
    bool blockInput = input_->isMouseButtonDown(Engine::Input::MouseButton::Right);

    if (blockInput) {
        combatSystem_->startBlock(playerEntity_);
    } else {
        combatSystem_->endBlock(playerEntity_);
    }
}

void PlayerControlSystem::processDodgeInput(float dt) {
    if (!combatSystem_) {
        return;  // Need combat system to handle dodging
    }

    // Can only dodge if available
    if (!combatSystem_->canDodge(playerEntity_)) {
        return;
    }

    // Check for double-tap on movement keys
    Engine::vec3 dodgeDirection(0.0f, 0.0f, 0.0f);
    bool shouldDodge = false;

    // W key - forward dodge
    if (input_->isKeyPressed(Engine::Input::Key::W)) {
        if (currentTime_ - doubleTapW_.lastTapTime < doubleTapW_.doubleTapWindow) {
            dodgeDirection.z = -1.0f;
            shouldDodge = true;
        }
        doubleTapW_.lastTapTime = currentTime_;
    }

    // S key - backward dodge
    if (input_->isKeyPressed(Engine::Input::Key::S)) {
        if (currentTime_ - doubleTapS_.lastTapTime < doubleTapS_.doubleTapWindow) {
            dodgeDirection.z = 1.0f;
            shouldDodge = true;
        }
        doubleTapS_.lastTapTime = currentTime_;
    }

    // A key - left dodge
    if (input_->isKeyPressed(Engine::Input::Key::A)) {
        if (currentTime_ - doubleTapA_.lastTapTime < doubleTapA_.doubleTapWindow) {
            dodgeDirection.x = -1.0f;
            shouldDodge = true;
        }
        doubleTapA_.lastTapTime = currentTime_;
    }

    // D key - right dodge
    if (input_->isKeyPressed(Engine::Input::Key::D)) {
        if (currentTime_ - doubleTapD_.lastTapTime < doubleTapD_.doubleTapWindow) {
            dodgeDirection.x = 1.0f;
            shouldDodge = true;
        }
        doubleTapD_.lastTapTime = currentTime_;
    }

    if (shouldDodge) {
        // Transform dodge direction based on camera yaw
        Engine::Quaternion cameraRotation = Engine::Quaternion(
            Engine::vec3(0.0f, 1.0f, 0.0f),
            cameraYaw_
        );
        Engine::vec3 worldDirection = cameraRotation.rotate(dodgeDirection);

        // Start dodge
        combatSystem_->startDodge(playerEntity_, worldDirection);
    }
}

void PlayerControlSystem::updateAutoplay(float dt) {
    auto* movement = ecs_->getComponent<MovementComponent>(playerEntity_);
    auto* transform = ecs_->getComponent<Engine::Transform>(playerEntity_);
    auto* combat = ecs_->getComponent<CombatComponent>(playerEntity_);
    auto* health = ecs_->getComponent<HealthComponent>(playerEntity_);

    if (!movement || !transform) {
        return;
    }

    // --- Target acquisition -------------------------------------------------
    // Scan every live entity tagged <EnemyComponent, Transform, HealthComponent>
    // and pick the nearest non-dead one by XZ distance. The query is rebuilt
    // per frame on purpose: enemy counts are O(10s) in this arena-survival
    // build (wave caps at ~15 on a 2xx wave budget), so a tiny O(N) scan is
    // cheaper than standing up a spatial acceleration structure for a smoke-
    // test path. If wave budgets ever grow we'd swap this for the physics
    // broadphase's AABB grid, but that's premature here.
    CatEngine::Entity target = CatEngine::NULL_ENTITY;
    Engine::vec3 targetPos(0.0f, 0.0f, 0.0f);
    float bestDistSq = std::numeric_limits<float>::max();

    const Engine::vec3 selfPos = transform->position;
    auto enemyQuery = ecs_->query<EnemyComponent, Engine::Transform, HealthComponent>();
    for (auto [entity, enemyComp, enemyTransform, enemyHealth] : enemyQuery.view()) {
        // Skip self (belt-and-suspenders — the player shouldn't have an
        // EnemyComponent, but we'd rather no-op than lock onto ourselves if
        // a future refactor accidentally tags the cat).
        if (entity == playerEntity_) continue;
        if (enemyHealth->isDead) continue;

        const float dx = enemyTransform->position.x - selfPos.x;
        const float dz = enemyTransform->position.z - selfPos.z;
        const float distSq = dx * dx + dz * dz;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            target = entity;
            targetPos = enemyTransform->position;
        }
    }

    // --- Spell-cast cadence bookkeeping -------------------------------------
    // autoplayCastCooldown_ counts down every frame independent of whether
    // we actually cast this tick — keeping the decrement unconditional here
    // means a frame where we skip casting for any reason (no target, out of
    // mana, magic system null) doesn't permanently stall the cadence.
    // Clamped at zero so it doesn't underflow across long idle stretches.
    if (autoplayCastCooldown_ > 0.0F) {
        autoplayCastCooldown_ -= dt;
        if (autoplayCastCooldown_ < 0.0F) {
            autoplayCastCooldown_ = 0.0F;
        }
    }

    // --- Movement + attack policy -------------------------------------------
    // Always run the bookkeeping tail (cooldowns, gravity, ground collision)
    // even when no target is found, otherwise a wave gap would leave the cat
    // hanging in the air if it was mid-jump. The branch above the tail only
    // decides how we steer while a target exists.
    if (target != CatEngine::NULL_ENTITY && combat) {
        const Engine::vec3 toTarget = targetPos - selfPos;
        const float distXZ = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        // Attack-range threshold: the combat component's own attackRange is
        // the authoritative number (shifts when equipping Sword vs Bow vs
        // Staff). Subtract a small epsilon so we commit to the swing just
        // *inside* range instead of stuttering at the exact boundary.
        const float engageRange = std::max(0.1f, combat->attackRange - 0.2f);

        // Face the target before moving/attacking. Without this, a melee
        // swing's forward-cone test would miss on the first frame after
        // choosing a new target. atan2(x, -z) matches processMovementInput's
        // convention so the player's yaw stays consistent whether a human or
        // the AI is driving.
        if (toTarget.length() > 0.01f) {
            const float targetYaw = std::atan2(toTarget.x, -toTarget.z);
            transform->rotation = Engine::Quaternion(
                Engine::vec3(0.0f, 1.0f, 0.0f),
                targetYaw
            );
        }

        if (distXZ > engageRange) {
            // Chase: normalize the horizontal vector and feed it through the
            // same applyMovement used by keyboard input. This keeps the
            // acceleration curve identical — no teleport, no infinite-speed
            // AI — so the autoplay cat moves exactly like a player holding W
            // toward the enemy.
            Engine::vec3 dirXZ(toTarget.x, 0.0f, toTarget.z);
            const float dirLen = dirXZ.length();
            if (dirLen > 0.001f) {
                dirXZ = dirXZ / dirLen;
                movement->applyMovement(dirXZ, dt);
            } else {
                movement->applyDeceleration(dt);
            }

            // Autoplay spell cast: while chasing (not yet in melee range)
            // lob a level-1 elemental projectile at the target every
            // kAutoplayCastInterval seconds, CYCLING through all four
            // elements so each cast exercises a different per-element
            // burst profile (Ice / Magic / Poison / Fire) instead of
            // visually replaying the same Water → Ice burst on every
            // cast. We cast only from the chase branch (not the engage
            // branch) because ranged spells are thematically what a cat
            // would throw at a still-distant dog — close-range combat
            // belongs to the melee swing. Range gate (28 m) is below the
            // tightest spell range in the cycle (water_bolt = 30 m,
            // wind_gust = 25 m, rock_throw = 25 m, fireball = 30 m, so
            // wind_gust / rock_throw need the gate at <= 25 m to land
            // safely). Tightening the gate to 24 m leaves a 1 m travel
            // buffer for every spell. Elevation of the cast point to
            // torso height is handled by resolveCasterOrigin inside
            // ElementalMagicSystem::castProjectileSpell.
            //
            // canCastSpell() inside castSpell() gates on level (default
            // 1 for every element, all four cycle spells require level 1
            // → passes), cooldown (each spell has its own 1.0–1.5 s
            // cooldown but our 2.5 s outer cadence is wider so the spell
            // cooldown never gates us), and mana (cat has no
            // ManaComponent → treated as unconstrained, passes). So the
            // call site only needs the magic system pointer and an in-
            // range target, both of which we verify before invoking.
            //
            // Cycle index is INCREMENTED ON CAST (not on this branch
            // entry) so cycle progress only advances when a cast actually
            // landed — a frame where canCastSpell() refused (rare: spell
            // cooldown still active because the same spell got cast on
            // the previous cycle pass) leaves the index pointing at the
            // same spell so the next opportunity retries it. This is
            // round-robin-fair across long sessions.
            if (magicSystem_ != nullptr &&
                autoplayCastCooldown_ <= 0.0F &&
                distXZ <= 24.0F) {
                // 4-spell rotation. Indexed by autoplayCastIndex_ % 4 so
                // the index can monotonically grow without wrapping
                // bookkeeping; uint32_t overflow at ~4 billion casts is
                // not a concern (40 s playtest at 2.5 s cadence is 16
                // casts; a continuous session would need 700 years to
                // overflow).
                static constexpr const char* kAutoplayCycleSpells[4] = {
                    "water_bolt",   // Water → Ice    (pale-cyan frost)
                    "wind_gust",    // Air   → Magic  (white-purple radial)
                    "rock_throw",   // Earth → Poison (yellow-green miasma)
                    "fireball",     // Fire  → Fire   (orange-yellow sparks)
                };
                const char* spellId = kAutoplayCycleSpells[autoplayCastIndex_ % 4];

                // LEAD THE TARGET — projectiles fly at PROJECTILE_SPEED
                // (25 m/s) and need real wall-clock time to traverse mid-
                // range distances. A target chasing the player at ~5 m/s
                // covers ~5 m in the ~1 s flight time of a 24 m bolt, so
                // aiming at targetPos misses the 1 m projectile hit-sphere
                // by ~5 m and the bolt overflies. Reading the target's
                // MovementComponent.velocity (XZ chase velocity from the
                // dog AI) and offsetting targetPos by `velocity * (distXZ
                // / kProjectileTravelSpeed)` re-aims at where the target
                // WILL be when the bolt arrives. Linear lead only — covers
                // the common "target running toward player" case in steady
                // state; a target abruptly turning mid-flight still gets
                // missed, which is the legibility floor: the cat looks
                // like it's predicting the dog's movement, not magically
                // homing on the dog. Pre-prediction cap: at most one
                // distXZ of lead (i.e. lead can't double the aim distance)
                // so a sprinting target near the engage gate doesn't fling
                // the aim point off-map.
                //
                // AOE branch (fireball) ALSO benefits — the AOE radius
                // (2 m for fireball) is wider than the lead error, but a
                // fast target gets the same prediction so the detonation
                // catches the entity inside its blast radius rather than
                // behind it.
                //
                // PROJECTILE_SPEED constant duplication: ElementalMagic-
                // System::PROJECTILE_SPEED is private (game/systems/
                // elemental_magic.hpp:383). A public getter is overkill
                // for one callsite; if the speed ever changes, both this
                // file and that header must be updated together.
                static constexpr float kProjectileTravelSpeed = 25.0F;
                Engine::vec3 leadPos = targetPos;
                if (auto* targetMovement =
                        ecs_->getComponent<MovementComponent>(target)) {
                    const float leadTime = distXZ / kProjectileTravelSpeed;
                    // Cap the lead so an unusually fast target doesn't
                    // launch the aim point off-map — at distXZ ~24 m and
                    // dog speed ~5 m/s, leadTime ~0.96 s and offset ~5 m,
                    // well below the cap. The cap fires only on edge
                    // cases (knocked-back enemy with abnormal velocity).
                    const float maxLead = distXZ; // never aim past 2x current distance
                    Engine::vec3 leadOffset(
                        targetMovement->velocity.x * leadTime,
                        0.0F, // y-velocity is 0 in steady state (gravity-snapped)
                        targetMovement->velocity.z * leadTime
                    );
                    const float offsetLen = std::sqrt(
                        leadOffset.x * leadOffset.x +
                        leadOffset.z * leadOffset.z);
                    if (offsetLen > maxLead && offsetLen > 0.0F) {
                        const float scale = maxLead / offsetLen;
                        leadOffset.x *= scale;
                        leadOffset.z *= scale;
                    }
                    leadPos.x += leadOffset.x;
                    leadPos.z += leadOffset.z;
                }
                const bool cast = magicSystem_->castSpell(
                    playerEntity_, std::string(spellId), leadPos);
                if (cast) {
                    autoplayCastCooldown_ = kAutoplayCastInterval;
                    ++autoplayCastIndex_;
                }
            }
        } else {
            // In engage range: stop advancing and swing. We still decelerate
            // (not `stop()`) so a short overshoot bleeds off naturally rather
            // than teleport-stopping the cat on the millisecond it crosses
            // the range boundary.
            movement->applyDeceleration(dt);

            // Delegate attack to CombatSystem so damage dispatch, animation
            // state, and combo-counter updates all go through the same path
            // a human click would trigger. startAttack() guards the cooldown
            // internally; we check the return so we don't double-log or
            // double-perform on a frame where the cooldown isn't ready yet.
            if (combat->startAttack() && combatSystem_ != nullptr) {
                combatSystem_->performAttack(playerEntity_, "L");
            }
        }
    } else {
        // No target: idle decelerate. The cat stays put between waves
        // instead of drifting, which also keeps the camera stable for demos.
        movement->applyDeceleration(dt);
    }

    // --- Physics tail (shared with processMovementInput) --------------------
    // Gravity + ground follow + invincibility tick. These must run every frame
    // regardless of the AI's steering choice above — a missed applyGravity
    // would leave the cat floating if WaveSystem spawns it above the terrain,
    // and a missed updateInvincibility would let i-frames from the last hit
    // linger forever.
    //
    // Combat cooldown is intentionally NOT ticked here; CombatSystem owns that
    // tick. See the note in processAttackInput() for the sub-frame race that
    // motivated moving the decrement to CombatSystem::update.
    movement->applyGravity(GRAVITY, dt);
    transform->position += movement->velocity * dt;

    float groundY = 0.0f;
    if (terrain_ != nullptr) {
        groundY = terrain_->getHeightAt(transform->position.x, transform->position.z);
    }
    if (transform->position.y <= groundY && movement->velocity.y <= 0.0f) {
        transform->position.y = groundY;
        movement->land(groundY);
    }

    if (health) {
        health->updateInvincibility(dt);
    }
}

void PlayerControlSystem::updateCamera(float dt) {
    auto* transform = ecs_->getComponent<Engine::Transform>(playerEntity_);

    if (!transform) {
        return;
    }

    // Cinematic orbit camera (gated by setCinematicOrbit()).
    //
    // We advance cameraYaw_ deterministically by yawRate * dt so the
    // camera revolves around the player at the existing cameraOffset_
    // distance (the rotated-offset math below treats cameraYaw_ as the
    // azimuth, so any monotonic source for that field produces a clean
    // orbit). cameraPitch_ is left untouched: in autoplay the user may
    // have configured a specific pitch via earlier mouse input or the
    // pitched 0.0 default, and the orbit is purely about azimuth — yaw
    // and pitch decouple cleanly because the rotation is yaw-then-pitch.
    //
    // Yaw is wrapped into [-2π, 2π] so a long capture (e.g. an unattended
    // overnight run) doesn't accumulate cameraYaw_ to a value where float
    // precision starts coarsening the resulting sin/cos calls. 2π is the
    // natural period of the rotation quaternion below, so the wrap is
    // a mathematical no-op that keeps the magnitude bounded — the
    // orientation produced by Q(yaw) and Q(yaw - 2π) is bit-identical
    // up to the quaternion's double-cover symmetry, and the rotated
    // offset vec3 from rotate() is invariant under the double cover.
    //
    // We intentionally DO NOT touch the cameraBobPhase_ accumulator
    // here: at the orbit's typical default rate (0.5 rad/s) the cat is
    // mostly idle, the bob amplitudeScale is normSpeed^2 ≈ 0, and the
    // bob contribution to cameraPosition_ is sub-mm. Leaving the bob
    // path on means a chase moment during orbit still shakes correctly,
    // which sells "the cat is sprinting under a cinematic-camera POV"
    // rather than "the cat is hovering inside a CG rig".
    if (cinematicOrbitEnabled_) {
        cameraYaw_ += cinematicOrbitYawRate_ * dt;
        // Wrap into [-2π, 2π]. fmod-like reduction without pulling in
        // <cmath>::fmod here because we already have the symbolic 2π
        // available via Engine::Math::PI — keeps the include surface
        // local and the arithmetic exact under IEEE-754 (a fmod call
        // could introduce a one-ulp drift over a multi-hour capture).
        const float twoPi = 2.0F * Engine::Math::PI;
        if (cameraYaw_ > twoPi) {
            cameraYaw_ -= twoPi;
        } else if (cameraYaw_ < -twoPi) {
            cameraYaw_ += twoPi;
        }
    }

    // Calculate camera rotation
    Engine::Quaternion yawRot = Engine::Quaternion(Engine::vec3(0.0f, 1.0f, 0.0f), cameraYaw_);
    Engine::Quaternion pitchRot = Engine::Quaternion(Engine::vec3(1.0f, 0.0f, 0.0f), cameraPitch_);
    Engine::Quaternion cameraRotation = yawRot * pitchRot;

    // Calculate camera forward direction
    cameraForward_ = cameraRotation.rotate(Engine::vec3(0.0f, 0.0f, -1.0f));

    // Calculate desired camera position
    Engine::vec3 rotatedOffset = cameraRotation.rotate(cameraOffset_);
    Engine::vec3 desiredCameraPosition = transform->position + rotatedOffset;

    // Smooth camera follow
    cameraPosition_ = Engine::vec3::lerp(
        cameraPosition_,
        desiredCameraPosition,
        cameraFollowSpeed_ * dt
    );

    // ----------------------------------------------------------------------
    // Camera bob synced to player locomotion speed.
    //
    // WHY: A mathematically smooth follow camera on top of a mostly-static
    // scene reads as "lifeless" — even now that the player cat breathes
    // (idle clip baked into all 17 cat GLBs in the 2026-04-25 rig batch),
    // the FRAMING is glassy and hides that motion. Real walking shakes the
    // viewer's head; even a few-cm vertical bob plus a centimetre lateral
    // sway sells "the player is moving" without touching any rendering
    // code or asset pipeline. This is the cheapest visible-delta lever
    // remaining per the SHIP-THE-CAT user directive (2026-04-24 18:58)
    // and runs in O(1) per frame — two sin calls and a handful of adds.
    //
    // FREQUENCY model: a normal walk is ~2 steps/s, but each step pumps the
    // head once on heel-strike, so the vertical bob frequency is 2x the
    // step rate. Walk → ~4 Hz, run → ~6 Hz. Linear blend:
    //   bobFreqHz = 4.0 + 2.0 * normSpeed   (normSpeed in [0,1])
    //
    // AMPLITUDE model: scales as normSpeed^2 — squaring is intentional so
    // an idle player has zero contribution (no jitter when standing still),
    // a gentle walk reads as subtle motion, and a sprint sells real
    // exertion. Linear scaling makes a slow walk look like a low-effort
    // jog, which felt wrong in early experimentation.
    //
    // PEAK amplitudes were chosen as sub-1 % of the 2.8 m camera distance:
    //   vertical max  = 4 cm  (1.4 % of 2.8 m)
    //   lateral max   = 2 cm  (0.7 % of 2.8 m)
    // Visible as motion in the third-person follow framing, invisible as
    // displacement (won't clip through walls, won't skew the lookAt anchor
    // in CatAnnihilation::render which still targets player.position).
    //
    // LATERAL bob runs at HALF the vertical frequency (left-foot, right-
    // foot, left-foot) so the head trace approximates a figure-8 — the
    // classic walking-cycle motion pattern, not a metronome ping-pong.
    //
    // PHASE wrap: at long bob frequencies, accumulating raw radians for
    // hours would lose float precision in sin(). Wrap at 1024 * 2*PI to
    // keep the value bounded — sin is 2*PI-periodic so any multiple of
    // 2*PI subtracted is mathematically identity, no visible glitch.
    //
    // FALLBACK: when MovementComponent is absent (e.g. the player entity
    // hasn't been bound yet during early-frame initialization, or in a
    // unit test that builds the system in isolation), the bob is skipped
    // and the camera behaves as if the player were idle — i.e. unchanged
    // from the pre-bob behaviour. Defensive null-check, no allocation,
    // no exception.
    auto* movement = ecs_->getComponent<MovementComponent>(playerEntity_);
    if (movement != nullptr) {
        const float currentSpeed = movement->getCurrentSpeed();
        // Guard against a zero/negative maxSpeed — divide-by-zero would
        // produce a NaN that propagates through sin() and silently corrupts
        // the camera. Realistic configs always set maxSpeed > 0, but a unit
        // test or deserialized save file could trip this.
        const float maxSpeedSafe = movement->maxSpeed > 0.001f
            ? movement->maxSpeed
            : 1.0f;
        const float normSpeedRaw = currentSpeed / maxSpeedSafe;
        const float normSpeed = normSpeedRaw < 0.0f
            ? 0.0f
            : (normSpeedRaw > 1.0f ? 1.0f : normSpeedRaw);
        const float amplitudeScale = normSpeed * normSpeed;

        const float bobFreqHz = 4.0f + 2.0f * normSpeed;
        cameraBobPhase_ += dt * bobFreqHz * 2.0f * Engine::Math::PI;
        const float kPhaseWrap = 2.0f * Engine::Math::PI * 1024.0f;
        if (cameraBobPhase_ > kPhaseWrap) {
            cameraBobPhase_ -= kPhaseWrap;
        }

        const float bobYAmplitudeM = 0.04f * amplitudeScale;        // 4 cm peak
        const float bobLateralAmplitudeM = 0.02f * amplitudeScale;  // 2 cm peak

        const float bobY = bobYAmplitudeM * std::sin(cameraBobPhase_);
        // Lateral runs at half the vertical phase rate — see the figure-8
        // note above. Using sin (not cos) keeps lateral and vertical in the
        // same phase family so the figure-8 has a consistent handedness.
        const float bobLateral =
            bobLateralAmplitudeM * std::sin(cameraBobPhase_ * 0.5f);

        // Lateral offset is along camera-right (the +X local axis after
        // applying the same yaw+pitch rotation that built the camera
        // position). Computing it from cameraRotation rather than a fixed
        // world vector means the bob stays screen-relative — looking left
        // doesn't flip the bob direction in the player's view.
        const Engine::vec3 cameraRight =
            cameraRotation.rotate(Engine::vec3(1.0f, 0.0f, 0.0f));

        cameraPosition_.y += bobY;
        cameraPosition_ += cameraRight * bobLateral;
    }

    // ----------------------------------------------------------------------
    // Camera punch on melee attack — cinematic kick that fires the moment
    // the player triggers an attack.
    //
    // WHY (the SHIP-THE-CAT scoreboard, 2026-04-24): a smooth follow camera
    // with the new bob already on top is "alive" but still treats every
    // moment equally. Combat is the high-energy beat of the game; the
    // camera should AMPLIFY that energy, not dampen it by tracking
    // mechanically through the strike. A short punch back + up frames the
    // cat at the moment of impact — the cat is still in the centre of the
    // screen because lookAt is anchored on player.position downstream, but
    // the EXTRA distance creates negative space around the cat that reads
    // visually as "stop, look at this hit" rather than "next routine
    // animation tick".
    //
    // DETECTION: CombatComponent::startAttack() spikes attackCooldown from
    // ~0 to getCooldownDuration() in one frame. A frame-to-frame jump in
    // attackCooldown is the single most reliable, system-local signal that
    // a fresh attack just started, AND it's already on a component the
    // player owns. The jump threshold (0.05s) is well below the smallest
    // possible cooldown duration (sword: 0.667s) and well above any normal
    // per-frame countdown drift (one frame at 60 fps is 0.0167s, and
    // CombatSystem decrements at most that much per tick). Setting the
    // threshold inside [frame_dt, smallest_cooldown_duration) is robust
    // against the typical 30-120 fps range without false-positives.
    //
    // ENVELOPE: asymmetric triangle — peak at 20% through the punch, falls
    // off over the remaining 80%. Peak-fast / fall-slow matches how a real
    // camera operator reacts to impact (whip back, ease in). A symmetric
    // bell-curve (sin(pi * phase)^2) felt too smooth; a square pulse felt
    // too cheap (camera teleports). The piecewise linear envelope here is
    // the cheapest closed-form thing that reads as "physical".
    //
    // PEAK AMPLITUDES: 10 cm back along camera-back, 4 cm up.
    //   • 10 cm against the 2.8 m camera-back distance is 3.6% of follow
    //     distance — visible as motion in third-person without clipping the
    //     near plane (0.1 m, see cameraOffset_ documentation in the header).
    //   • 4 cm up against the 1.2 m camera height is 3.3% — pairs with the
    //     back motion to give a slight tilt-up read on the cat.
    // Both in lock-step (same envelope value, different scales) so the
    // motion looks like ONE coordinated kick, not two independent jitters.
    //
    // DURATION: 0.25 s — long enough to read as a deliberate beat, short
    // enough to never feel like input lag (the player's next attack input
    // is still on the cooldown clock anyway, sword cooldown 0.667s gives
    // ~3x punch duration of pre-next-attack room).
    //
    // FALLBACK: when CombatComponent is absent (player entity not yet
    // bound, or test harness builds the system in isolation) the punch is
    // skipped and the camera behaves identically to the pre-punch state.
    auto* combat = ecs_->getComponent<CombatComponent>(playerEntity_);
    if (combat != nullptr) {
        constexpr float kPunchDurationS = 0.25f;
        constexpr float kPunchPeakBackM = 0.10f;
        constexpr float kPunchPeakUpM   = 0.04f;
        constexpr float kAttackJumpThresholdS = 0.05f;
        constexpr float kEnvelopePeakPhase = 0.20f;

        // Detect attack start: cooldown jumped UP frame-to-frame. Floor of
        // 0.05s sits comfortably between per-frame countdown drift
        // (1/60 = 0.0167s) and the smallest weapon cooldown (sword 0.667s).
        const float cooldownDelta = combat->attackCooldown - prevAttackCooldown_;
        if (cooldownDelta > kAttackJumpThresholdS) {
            cameraPunchTimer_ = kPunchDurationS;
        }
        prevAttackCooldown_ = combat->attackCooldown;

        if (cameraPunchTimer_ > 0.0f) {
            cameraPunchTimer_ -= dt;
            if (cameraPunchTimer_ < 0.0f) {
                cameraPunchTimer_ = 0.0f;
            }

            // phase: 0 at the start of the punch, 1 at the end.
            const float phase = 1.0f - (cameraPunchTimer_ / kPunchDurationS);

            // Asymmetric triangle envelope. Rise (0..kEnvelopePeakPhase) is
            // fast: divide by the small denominator. Fall (peak..1) is slow:
            // divide by the large denominator. The branch is essentially
            // free vs. a single sin() call and gives us the directly-tunable
            // peak position kEnvelopePeakPhase.
            float envelope;
            if (phase <= kEnvelopePeakPhase) {
                envelope = phase / kEnvelopePeakPhase;                // 0 -> 1
            } else {
                envelope = 1.0f - (phase - kEnvelopePeakPhase)
                                / (1.0f - kEnvelopePeakPhase);        // 1 -> 0
            }

            // Apply the kick along camera-back (negative cameraForward_)
            // and world-up. Camera-back rather than world-back because the
            // player can yaw the camera freely and we want the kick to push
            // BEHIND the camera in screen-space, not toward a fixed world
            // direction. Lifting along world-up rather than camera-up keeps
            // the punch readable even at extreme pitch — at high pitch a
            // camera-up kick would push the camera backward in world space,
            // doubling the back-kick instead of giving a vertical lift.
            const Engine::vec3 cameraBack = cameraForward_ * -1.0f;
            cameraPosition_ += cameraBack * (envelope * kPunchPeakBackM);
            cameraPosition_.y += envelope * kPunchPeakUpM;
        }
    }
}

} // namespace CatGame
