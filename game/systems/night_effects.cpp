#include "night_effects.hpp"
#include "../components/MovementComponent.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/math/Noise.hpp"
#include "../../engine/ecs/Component.hpp"
#include <algorithm>
#include <cmath>

namespace CatGame {

// Static Perlin noise generator for flickering
static Engine::Noise::Perlin s_perlin;

// Component for tracking position
struct Transform {
    Engine::vec3 position;
    Engine::vec3 rotation;
    Engine::vec3 scale;
};

// Component for tracking velocity (for glow sticks)
struct Velocity {
    Engine::vec3 linear;
    Engine::vec3 angular;
};

NightEffectsSystem::NightEffectsSystem(DayNightCycleSystem* dayCycle, int priority)
    : CatEngine::System(priority)
    , dayCycle_(dayCycle)
{
}

// ============================================================================
// Lifecycle
// ============================================================================

void NightEffectsSystem::init(CatEngine::ECS* ecs) {
    CatEngine::System::init(ecs);

    // Components are auto-registered when first used via addComponent/emplaceComponent

    activeLightSources_.clear();
    nocturnalEnemies_.clear();
    glowSticks_.clear();

    lastFrameWasNight_ = dayCycle_ != nullptr && dayCycle_->isNight();
}

void NightEffectsSystem::update(float dt) {
    if (dayCycle_ == nullptr) {
        return;
    }

    timeAccumulator_ += dt;

    // Update light flickering
    updateLightFlickering(dt);

    // Update player visibility
    updatePlayerVisibility(dt);

    // Update nocturnal enemies
    updateNocturnalEnemies(dt);

    // Update glow sticks
    updateGlowSticks(dt);

    // Check for day/night transitions
    bool isNightNow = dayCycle_->isNight();
    if (lastFrameWasNight_ && !isNightNow) {
        // Dawn - despawn nocturnal enemies
        despawnNocturnalEnemies();
    }
    lastFrameWasNight_ = isNightNow;
}

// ============================================================================
// Light Source Management
// ============================================================================

CatEngine::Entity NightEffectsSystem::createCampfire(const Engine::vec3& position, bool isSafeZone) {
    auto entity = ecs_->createEntity();

    // Add transform
    Transform transform;
    transform.position = position;
    transform.rotation = Engine::vec3(0.0f);
    transform.scale = Engine::vec3(1.0f);
    ecs_->addComponent(entity, transform);

    // Add light source
    NightLightSource light;
    light.type = LightSourceType::Campfire;
    light.color = Engine::vec3(1.0f, 0.6f, 0.3f);  // Orange firelight
    light.intensity = 2.0f;
    light.radius = 15.0f;
    light.isActive = true;
    light.flickerAmount = 0.3f;
    light.flickerSpeed = 2.0f;
    light.isSafeZone = isSafeZone;
    ecs_->addComponent(entity, light);

    activeLightSources_.push_back(entity);

    return entity;
}

void NightEffectsSystem::givePlayerTorch(CatEngine::Entity player) {
    if (!ecs_->hasComponent<NightLightSource>(player)) {
        NightLightSource light;
        light.type = LightSourceType::Torch;
        light.color = Engine::vec3(1.0f, 0.7f, 0.4f);
        light.intensity = 1.5f;
        light.radius = 12.0f;
        light.isActive = true;
        light.flickerAmount = 0.2f;
        light.flickerSpeed = 3.0f;
        ecs_->addComponent(player, light);

        activeLightSources_.push_back(player);
    }
}

void NightEffectsSystem::givePlayerFlashlight(CatEngine::Entity player) {
    if (!ecs_->hasComponent<NightLightSource>(player)) {
        NightLightSource light;
        light.type = LightSourceType::Flashlight;
        light.color = Engine::vec3(1.0f, 1.0f, 0.95f);  // White/cool light
        light.intensity = 2.5f;
        light.radius = 25.0f;
        light.isActive = true;
        light.flickerAmount = 0.0f;  // No flicker
        light.coneAngle = 30.0f;
        light.coneAttenuation = 0.9f;
        ecs_->addComponent(player, light);

        activeLightSources_.push_back(player);
    }
}

void NightEffectsSystem::togglePlayerLight(CatEngine::Entity player) {
    if (ecs_->hasComponent<NightLightSource>(player)) {
        auto* light = ecs_->getComponent<NightLightSource>(player);
        if (light != nullptr) {
            light->isActive = !light->isActive;
        }
    }
}

CatEngine::Entity NightEffectsSystem::throwGlowStick(const Engine::vec3& position, const Engine::vec3& velocity) {
    auto entity = ecs_->createEntity();

    // Add transform
    Transform transform;
    transform.position = position;
    transform.rotation = Engine::vec3(0.0f);
    transform.scale = Engine::vec3(0.1f);
    ecs_->addComponent(entity, transform);

    // Add velocity
    Velocity vel;
    vel.linear = velocity;
    vel.angular = Engine::vec3(
        static_cast<float>(rand() % 1000) / 1000.0f * 10.0f - 5.0f,
        static_cast<float>(rand() % 1000) / 1000.0f * 10.0f - 5.0f,
        static_cast<float>(rand() % 1000) / 1000.0f * 10.0f - 5.0f
    );
    ecs_->addComponent(entity, vel);

    // Add light source
    NightLightSource light;
    light.type = LightSourceType::GlowStick;
    light.color = Engine::vec3(0.3f, 1.0f, 0.3f);  // Green glow
    light.intensity = 1.0f;
    light.radius = 8.0f;
    light.isActive = true;
    light.flickerAmount = 0.0f;
    ecs_->addComponent(entity, light);

    activeLightSources_.push_back(entity);
    glowSticks_.push_back(entity);

    return entity;
}

// ============================================================================
// Enemy Behavior
// ============================================================================

void NightEffectsSystem::makeEnemyNocturnal(CatEngine::Entity enemy, float nightVisionRange) {
    if (!ecs_->hasComponent<NocturnalEnemy>(enemy)) {
        NocturnalEnemy nocturnal;
        nocturnal.nightVisionRange = nightVisionRange;
        nocturnal.dayVisionRange = nightVisionRange * 0.3f;
        nocturnal.despawnAtDawn = true;
        nocturnal.aggressionBonus = 0.5f;
        ecs_->addComponent(enemy, nocturnal);

        nocturnalEnemies_.push_back(enemy);
    }
}

float NightEffectsSystem::getEnemyVisionRange(CatEngine::Entity enemy) const {
    if (!ecs_->hasComponent<NocturnalEnemy>(enemy)) {
        // Regular enemy - reduced vision at night
        return dayCycle_->isNight() ? 15.0f : 25.0f;
    }

    const auto* nocturnal = ecs_->getComponent<NocturnalEnemy>(enemy);
    if (nocturnal == nullptr) {
        return 15.0f;
    }
    return dayCycle_->isNight() ? nocturnal->nightVisionRange : nocturnal->dayVisionRange;
}

float NightEffectsSystem::getEnemyAggressionMultiplier(CatEngine::Entity enemy) const {
    if (!ecs_->hasComponent<NocturnalEnemy>(enemy)) {
        return 1.0f;
    }

    if (!dayCycle_->isNight()) {
        return 0.5f;  // Nocturnal enemies are less aggressive during day
    }

    const auto* nocturnal = ecs_->getComponent<NocturnalEnemy>(enemy);
    if (nocturnal == nullptr) {
        return 1.0f;
    }
    return 1.0f + nocturnal->aggressionBonus;
}

bool NightEffectsSystem::shouldDespawnEnemy(CatEngine::Entity enemy) const {
    if (!ecs_->hasComponent<NocturnalEnemy>(enemy)) {
        return false;
    }

    const auto* nocturnal = ecs_->getComponent<NocturnalEnemy>(enemy);
    if (nocturnal == nullptr) {
        return false;
    }
    return nocturnal->despawnAtDawn && !dayCycle_->isNight();
}

// ============================================================================
// Player Visibility
// ============================================================================

void NightEffectsSystem::setupPlayerVisibility(CatEngine::Entity player) {
    if (!ecs_->hasComponent<PlayerVisibility>(player)) {
        PlayerVisibility visibility;
        visibility.baseVisibility = 1.0f;
        visibility.detectionRangeDay = 25.0f;
        visibility.detectionRangeNight = 12.0f;
        ecs_->addComponent(player, visibility);
    }
}

float NightEffectsSystem::getPlayerVisibility(CatEngine::Entity player) const {
    if (!ecs_->hasComponent<PlayerVisibility>(player)) {
        return 1.0f;
    }

    const auto* visibility = ecs_->getComponent<PlayerVisibility>(player);
    if (visibility == nullptr) {
        return 1.0f;
    }
    return visibility->currentVisibility;
}

float NightEffectsSystem::getPlayerDetectionRange(CatEngine::Entity player) const {
    if (!ecs_->hasComponent<PlayerVisibility>(player)) {
        return dayCycle_->isNight() ? 12.0f : 25.0f;
    }

    const auto* visibility = ecs_->getComponent<PlayerVisibility>(player);
    if (visibility == nullptr) {
        return dayCycle_->isNight() ? 12.0f : 25.0f;
    }

    float baseRange = dayCycle_->isNight() ?
        visibility->detectionRangeNight :
        visibility->detectionRangeDay;

    // Visibility affects detection range
    return baseRange * visibility->currentVisibility;
}

bool NightEffectsSystem::isPlayerInSafeZone(CatEngine::Entity player, float* distanceToSafeZone) const {
    if (!ecs_->hasComponent<Transform>(player)) {
        return false;
    }

    const auto* playerTransform = ecs_->getComponent<Transform>(player);
    if (playerTransform == nullptr) {
        return false;
    }

    float nearestDistance = std::numeric_limits<float>::max();
    bool inSafeZone = false;

    // Check all campfires
    for (const auto& lightEntity : activeLightSources_) {
        if (!ecs_->hasComponent<NightLightSource>(lightEntity)) {
            continue;
        }

        const auto* light = ecs_->getComponent<NightLightSource>(lightEntity);
        if (light == nullptr || light->type != LightSourceType::Campfire || !light->isSafeZone) {
            continue;
        }

        if (!ecs_->hasComponent<Transform>(lightEntity)) {
            continue;
        }

        const auto* lightTransform = ecs_->getComponent<Transform>(lightEntity);
        if (lightTransform == nullptr) {
            continue;
        }

        Engine::vec3 diff = playerTransform->position - lightTransform->position;
        float distance = diff.length();

        if (distance < light->radius) {
            inSafeZone = true;
        }

        if (distance < nearestDistance) {
            nearestDistance = distance;
        }
    }

    if (distanceToSafeZone != nullptr) {
        *distanceToSafeZone = nearestDistance;
    }

    return inSafeZone;
}

// ============================================================================
// Queries
// ============================================================================

CatEngine::Entity NightEffectsSystem::getNearestLightSource(const Engine::vec3& position, float maxDistance) const {
    CatEngine::Entity nearest = CatEngine::NULL_ENTITY;
    float nearestDistance = maxDistance;

    for (const auto& lightEntity : activeLightSources_) {
        if (!ecs_->hasComponent<Transform>(lightEntity)) {
            continue;
        }

        const auto* transform = ecs_->getComponent<Transform>(lightEntity);
        if (transform == nullptr) {
            continue;
        }

        Engine::vec3 diff = position - transform->position;
        float distance = diff.length();

        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = lightEntity;
        }
    }

    return nearest;
}

float NightEffectsSystem::getIlluminationAt(const Engine::vec3& position) const {
    float totalIllumination = 0.0f;

    // Add ambient illumination from day/night cycle
    float ambientIllumination = dayCycle_->getSunIntensity() / 20.0f;  // Normalize to 0-1
    totalIllumination += ambientIllumination;

    // Add moon illumination at night
    if (dayCycle_->isNight()) {
        totalIllumination += dayCycle_->getMoonBrightness() * 0.1f;
    }

    // Add light source illumination
    for (const auto& lightEntity : activeLightSources_) {
        if (!ecs_->hasComponent<NightLightSource>(lightEntity) ||
            !ecs_->hasComponent<Transform>(lightEntity)) {
            continue;
        }

        const auto* light = ecs_->getComponent<NightLightSource>(lightEntity);
        if (light == nullptr || !light->isActive) {
            continue;
        }

        const auto* transform = ecs_->getComponent<Transform>(lightEntity);
        if (transform == nullptr) {
            continue;
        }

        Engine::vec3 diff = position - transform->position;
        float distance = diff.length();

        if (distance < light->radius) {
            float attenuation = 1.0f - (distance / light->radius);
            attenuation = attenuation * attenuation;  // Square falloff
            totalIllumination += light->intensity * attenuation;
        }
    }

    return std::min(totalIllumination, 1.0f);
}

// ============================================================================
// Update Methods
// ============================================================================

void NightEffectsSystem::updateLightFlickering(float /*dt*/) {
    // Flicker computation is a no-op until a renderer-side consumer exists
    // (see the deferred-feature note in night_effects.hpp). We still walk
    // the active-light list so the method has a shape ready to restore
    // once a Scene → LightManager bridge lands — but the actual per-frame
    // multiplier write is gone because nothing was reading it.
    (void)s_perlin;
    (void)timeAccumulator_;
    for (const auto& lightEntity : activeLightSources_) {
        (void)lightEntity;
    }
}

void NightEffectsSystem::updatePlayerVisibility(float dt) {
    // Find player entities with visibility component
    auto query = ecs_->query<PlayerVisibility, Transform>();

    for (auto [entity, visibility, transform] : query.view()) {
        // Base visibility affected by time of day
        visibility->baseVisibility = dayCycle_->isNight() ? 0.3f : 1.0f;

        // Light source modifier
        visibility->lightSourceModifier = calculateLightModifier(transform->position);

        // Movement modifier
        visibility->movementModifier = calculateMovementModifier(entity, dt);

        // Calculate total visibility
        visibility->currentVisibility = visibility->baseVisibility +
                                      visibility->lightSourceModifier +
                                      visibility->movementModifier;

        // Apply stealth bonus from day/night cycle
        visibility->currentVisibility *= (1.0f - dayCycle_->getStealthBonus());

        // Clamp to [0, 1]
        visibility->currentVisibility = std::clamp(visibility->currentVisibility, 0.0f, 1.0f);
    }
}

void NightEffectsSystem::updateNocturnalEnemies(float /*dt*/) {
    // The per-frame aggression-multiplier cache was removed (see the
    // deferred-feature note in night_effects.hpp). Iteration stays so a
    // future AI hook can subscribe here without a structural change —
    // today it just keeps despawn tracking honest by skipping dead
    // entities for symmetry with other passes over nocturnalEnemies_.
    for (const auto& enemy : nocturnalEnemies_) {
        if (!ecs_->isAlive(enemy)) {
            continue;
        }
    }
}

void NightEffectsSystem::despawnNocturnalEnemies() {
    std::vector<CatEngine::Entity> toRemove;

    for (const auto& enemy : nocturnalEnemies_) {
        if (!ecs_->isAlive(enemy)) {
            continue;
        }

        if (shouldDespawnEnemy(enemy)) {
            // Despawn with particle effect (in production)
            ecs_->destroyEntity(enemy);
            toRemove.push_back(enemy);
        }
    }

    // Remove despawned enemies from tracking list
    for (const auto& enemy : toRemove) {
        auto it = std::find(nocturnalEnemies_.begin(), nocturnalEnemies_.end(), enemy);
        if (it != nocturnalEnemies_.end()) {
            nocturnalEnemies_.erase(it);
        }
    }
}

void NightEffectsSystem::updateGlowSticks(float dt) {
    std::vector<CatEngine::Entity> toRemove;

    for (const auto& glowStick : glowSticks_) {
        if (!ecs_->isAlive(glowStick) ||
            !ecs_->hasComponent<Transform>(glowStick) ||
            !ecs_->hasComponent<Velocity>(glowStick)) {
            toRemove.push_back(glowStick);
            continue;
        }

        auto* transform = ecs_->getComponent<Transform>(glowStick);
        auto* velocity = ecs_->getComponent<Velocity>(glowStick);
        if (transform == nullptr || velocity == nullptr) {
            toRemove.push_back(glowStick);
            continue;
        }

        // Apply gravity
        velocity->linear.y += -9.81f * dt;

        // Update position
        transform->position = transform->position + velocity->linear * dt;

        // Update rotation
        transform->rotation = transform->rotation + velocity->angular * dt;

        // Ground collision (simplified)
        if (transform->position.y <= 0.0f) {
            transform->position.y = 0.0f;
            velocity->linear.y = 0.0f;
            velocity->linear = velocity->linear * 0.3f;  // Friction
            velocity->angular = velocity->angular * 0.5f;
        }
    }

    // Remove invalid glow sticks
    for (const auto& glowStick : toRemove) {
        auto it = std::find(glowSticks_.begin(), glowSticks_.end(), glowStick);
        if (it != glowSticks_.end()) {
            glowSticks_.erase(it);
        }

        auto lightIt = std::find(activeLightSources_.begin(), activeLightSources_.end(), glowStick);
        if (lightIt != activeLightSources_.end()) {
            activeLightSources_.erase(lightIt);
        }
    }
}

float NightEffectsSystem::calculateLightModifier(const Engine::vec3& position) const {
    float modifier = 0.0f;

    // Being near light sources increases visibility
    for (const auto& lightEntity : activeLightSources_) {
        if (!ecs_->hasComponent<NightLightSource>(lightEntity) ||
            !ecs_->hasComponent<Transform>(lightEntity)) {
            continue;
        }

        const auto* light = ecs_->getComponent<NightLightSource>(lightEntity);
        if (light == nullptr || !light->isActive) {
            continue;
        }

        const auto* transform = ecs_->getComponent<Transform>(lightEntity);
        if (transform == nullptr) {
            continue;
        }

        Engine::vec3 diff = position - transform->position;
        float distance = diff.length();

        if (distance < light->radius) {
            float attenuation = 1.0f - (distance / light->radius);
            modifier += light->intensity * attenuation * 0.3f;  // Scale to reasonable range
        }
    }

    return std::min(modifier, 0.7f);  // Cap at 0.7 additional visibility
}

float NightEffectsSystem::calculateMovementModifier(CatEngine::Entity entity, float /*dt*/) const {
    // Moving entities are easier to spot at night. The modifier is additive
    // to baseVisibility so it must stay small (max 0.3) to avoid swamping the
    // lighting contribution. Below the walk threshold an entity is treated as
    // stationary and gets no penalty.
    if (ecs_ == nullptr) {
        return 0.0F;
    }

    const auto* movement = ecs_->getComponent<MovementComponent>(entity);
    if (movement == nullptr) {
        return 0.0F;
    }

    constexpr float WALK_THRESHOLD = 0.5F;   // units/sec — ignore idle jitter
    constexpr float RUN_THRESHOLD  = 6.0F;   // units/sec — full penalty at run
    constexpr float MAX_MODIFIER   = 0.30F;

    const float speed = movement->getCurrentSpeed();
    if (speed <= WALK_THRESHOLD) {
        return 0.0F;
    }

    const float t = std::clamp(
        (speed - WALK_THRESHOLD) / (RUN_THRESHOLD - WALK_THRESHOLD),
        0.0F, 1.0F);
    return MAX_MODIFIER * t;
}

// Per-frame multiplier queries removed — see the deferred-feature note in
// night_effects.hpp. getEnemyAggressionMultiplier() (the pull-style API)
// is still available for callers that already hold a NightEffectsSystem*
// and want to compute the value on demand.

} // namespace CatGame
