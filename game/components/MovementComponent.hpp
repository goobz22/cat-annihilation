#pragma once

#include "../../engine/math/Vector.hpp"

namespace CatGame {

/**
 * Movement component for entities that can move in 3D space
 *
 * Features:
 * - Velocity and speed control
 * - Acceleration and deceleration
 * - Ground state tracking
 * - Jump mechanics
 * - Gravity support
 */
struct MovementComponent {
    // Movement parameters
    float moveSpeed = 10.0f;              // Base movement speed (units per second)
    float maxSpeed = 20.0f;               // Maximum allowed speed
    float acceleration = 50.0f;           // Acceleration rate
    float deceleration = 30.0f;           // Deceleration rate when no input

    // Current movement state
    Engine::vec3 velocity = Engine::vec3(0.0f, 0.0f, 0.0f);

    // Jump parameters
    float jumpForce = 15.0f;              // Initial jump velocity
    float gravityMultiplier = 2.0f;       // Multiplier for gravity effect
    bool isGrounded = true;               // Is the entity on the ground?

    // Movement modifiers
    float speedModifier = 1.0f;           // Multiplicative speed modifier (buffs/debuffs)
    bool canMove = true;                  // Can the entity move? (stunned, rooted, etc.)
    bool canJump = true;                  // Can the entity jump?

    /**
     * Apply movement input (normalized direction)
     * @param direction Desired movement direction (should be normalized)
     * @param dt Delta time in seconds
     */
    void applyMovement(const Engine::vec3& direction, float dt) {
        if (!canMove) {
            return;
        }

        // Calculate desired velocity
        float currentSpeed = moveSpeed * speedModifier;
        Engine::vec3 desiredVelocity = direction * currentSpeed;

        // Apply acceleration towards desired velocity (XZ plane only)
        Engine::vec3 horizontalVelocity(velocity.x, 0.0f, velocity.z);
        Engine::vec3 desiredHorizontal(desiredVelocity.x, 0.0f, desiredVelocity.z);

        // Interpolate towards desired velocity
        Engine::vec3 newHorizontal = Engine::vec3::lerp(
            horizontalVelocity,
            desiredHorizontal,
            acceleration * dt
        );

        // Clamp to max speed
        float horizontalSpeed = newHorizontal.length();
        if (horizontalSpeed > maxSpeed * speedModifier) {
            newHorizontal = newHorizontal.normalized() * (maxSpeed * speedModifier);
        }

        // Apply to velocity
        velocity.x = newHorizontal.x;
        velocity.z = newHorizontal.z;
    }

    /**
     * Apply deceleration when no input
     * @param dt Delta time in seconds
     */
    void applyDeceleration(float dt) {
        // Decelerate horizontal movement
        Engine::vec3 horizontalVelocity(velocity.x, 0.0f, velocity.z);
        float horizontalSpeed = horizontalVelocity.length();

        if (horizontalSpeed > 0.01f) {
            float newSpeed = std::max(0.0f, horizontalSpeed - deceleration * dt);
            Engine::vec3 decelDirection = horizontalVelocity.normalized();
            velocity.x = decelDirection.x * newSpeed;
            velocity.z = decelDirection.z * newSpeed;
        } else {
            velocity.x = 0.0f;
            velocity.z = 0.0f;
        }
    }

    /**
     * Apply gravity to vertical velocity
     * @param gravity Gravity acceleration (typically negative)
     * @param dt Delta time in seconds
     */
    void applyGravity(float gravity, float dt) {
        if (!isGrounded) {
            velocity.y += gravity * gravityMultiplier * dt;
        }
    }

    /**
     * Perform a jump
     * @return true if jump was executed
     */
    bool jump() {
        if (isGrounded && canJump) {
            velocity.y = jumpForce;
            isGrounded = false;
            return true;
        }
        return false;
    }

    /**
     * Force the entity to land on the ground
     * @param groundY Y position of the ground
     */
    void land(float groundY = 0.0f) {
        isGrounded = true;
        velocity.y = 0.0f;
    }

    /**
     * Get current movement speed (magnitude of horizontal velocity)
     * @return Current horizontal speed
     */
    float getCurrentSpeed() const {
        return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    }

    /**
     * Get movement direction (normalized horizontal velocity)
     * @return Normalized direction vector, or zero if not moving
     */
    Engine::vec3 getMovementDirection() const {
        Engine::vec3 horizontal(velocity.x, 0.0f, velocity.z);
        float speed = horizontal.length();
        return speed > 0.01f ? horizontal.normalized() : Engine::vec3(0.0f, 0.0f, 0.0f);
    }

    /**
     * Stop all movement immediately
     */
    void stop() {
        velocity = Engine::vec3(0.0f, 0.0f, 0.0f);
    }

    /**
     * Stop horizontal movement only
     */
    void stopHorizontal() {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
    }

    /**
     * Check if entity is moving horizontally
     * @param threshold Minimum speed to consider as moving
     * @return true if moving faster than threshold
     */
    bool isMoving(float threshold = 0.1f) const {
        return getCurrentSpeed() > threshold;
    }
};

} // namespace CatGame
