#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/core/Input.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/Transform.hpp"

namespace CatGame {

class Terrain;


/**
 * PlayerControlSystem - Handles player input and controls
 *
 * Responsibilities:
 * - Process WASD movement input
 * - Handle mouse look for camera rotation
 * - Third-person camera management
 * - Jump input handling
 * - Attack input handling
 * - Smooth camera follow
 *
 * The system queries for the player entity and updates its movement
 * and combat components based on player input.
 */
class PlayerControlSystem : public CatEngine::System {
public:
    /**
     * Construct player control system
     * @param input Reference to the input system
     * @param priority System execution priority
     */
    explicit PlayerControlSystem(Engine::Input* input, int priority = 0);

    /**
     * Initialize the system
     * @param ecs Pointer to ECS instance
     */
    void init(CatEngine::ECS* ecs) override;

    /**
     * Update player controls each frame
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
    const char* getName() const override { return "PlayerControlSystem"; }

    /**
     * Set the player entity to control
     * @param entity The player entity
     */
    void setPlayerEntity(CatEngine::Entity entity);

    /**
     * Get the player entity
     * @return Player entity
     */
    CatEngine::Entity getPlayerEntity() const { return playerEntity_; }

    /**
     * Get camera position
     * @return Camera position in world space
     */
    Engine::vec3 getCameraPosition() const { return cameraPosition_; }

    /**
     * Get camera forward direction
     * @return Camera forward direction
     */
    Engine::vec3 getCameraForward() const { return cameraForward_; }

    /**
     * Get camera transform
     * @return Camera transform
     */
    Engine::Transform getCameraTransform() const;

    /**
     * Set camera offset from player
     * @param offset Camera offset in local space
     */
    void setCameraOffset(const Engine::vec3& offset);

    /**
     * Set mouse sensitivity
     * @param sensitivity Mouse sensitivity multiplier
     */
    void setMouseSensitivity(float sensitivity);

    /**
     * Enable or disable player control
     * @param enabled If true, player can control the entity
     */
    void setControlEnabled(bool enabled);

    /**
     * Set combat system reference for blocking/dodging
     * @param combatSystem Pointer to combat system
     */
    void setCombatSystem(class CombatSystem* combatSystem);

    /**
     * Set terrain reference so the player stays on the heightfield instead of
     * clipping against the old hard-coded y=0 ground plane.
     */
    void setTerrain(const Terrain* terrain) { terrain_ = terrain; }

private:
    // Input processing
    void processMovementInput(float dt);
    void processMouseLook(float dt);
    void processJumpInput();
    void processAttackInput();
    void processBlockInput();
    void processDodgeInput(float dt);

    // Camera management
    void updateCamera(float dt);

    // Dodge double-tap detection
    struct DoubleTapState {
        float lastTapTime = -1.0f;
        float doubleTapWindow = 0.3f;  // 300ms window for double-tap
    };

    DoubleTapState doubleTapW_;
    DoubleTapState doubleTapA_;
    DoubleTapState doubleTapS_;
    DoubleTapState doubleTapD_;
    float currentTime_ = 0.0f;

    // System state
    Engine::Input* input_ = nullptr;
    CatEngine::Entity playerEntity_;
    bool controlEnabled_ = true;
    class CombatSystem* combatSystem_ = nullptr;
    const Terrain* terrain_ = nullptr;

    // Camera parameters
    Engine::vec3 cameraOffset_ = Engine::vec3(0.0f, 5.0f, 10.0f);  // Default third-person offset
    Engine::vec3 cameraPosition_ = Engine::vec3(0.0f, 0.0f, 0.0f);
    Engine::vec3 cameraForward_ = Engine::vec3(0.0f, 0.0f, -1.0f);
    float cameraYaw_ = 0.0f;       // Horizontal rotation (radians)
    float cameraPitch_ = -0.3f;    // Vertical rotation (radians)
    float mouseSensitivity_ = 0.002f;
    float cameraFollowSpeed_ = 10.0f;
    float cameraMinPitch_ = -1.4f;  // ~-80 degrees
    float cameraMaxPitch_ = 0.0f;   // 0 degrees (horizontal)

    // Movement parameters
    float movementDeadzone_ = 0.1f;

    // Gravity constant
    static constexpr float GRAVITY = -30.0f;  // m/s^2
};

} // namespace CatGame
