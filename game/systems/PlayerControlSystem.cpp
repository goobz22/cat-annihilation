#include "PlayerControlSystem.hpp"
#include "CombatSystem.hpp"
#include "../components/GameComponents.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Math.hpp"
#include <cmath>

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

    // Simple ground collision (assume ground at y = 0)
    if (transform->position.y <= 0.0f && movement->velocity.y < 0.0f) {
        transform->position.y = 0.0f;
        movement->land(0.0f);
    }

    // Update health component invincibility
    auto* health = ecs_->getComponent<HealthComponent>(playerEntity_);
    if (health) {
        health->updateInvincibility(dt);
    }

    // Update combat cooldown
    auto* combat = ecs_->getComponent<CombatComponent>(playerEntity_);
    if (combat) {
        combat->updateCooldown(dt);
    }
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
        // Start attack
        if (combat->startAttack()) {
            // Track combo if combat system is available
            if (combatSystem_) {
                // Heavy attack if shift is held
                std::string attackType = shiftHeld ? "H" : "L";
                combatSystem_->performAttack(playerEntity_, attackType);
            }
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

void PlayerControlSystem::updateCamera(float dt) {
    auto* transform = ecs_->getComponent<Engine::Transform>(playerEntity_);

    if (!transform) {
        return;
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
}

} // namespace CatGame
