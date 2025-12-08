#pragma once

#include "../ecs/System.hpp"
#include "../ecs/Entity.hpp"
#include "../ecs/ECS.hpp"
#include "../math/Vector.hpp"
#include "BehaviorTree.hpp"
#include "Navigation.hpp"
#include "Blackboard.hpp"

#include <memory>
#include <vector>
#include <unordered_map>

namespace CatEngine {

using Engine::vec3;

/**
 * AI Component - Attached to entities with AI behavior
 */
struct AIComponent {
    std::unique_ptr<BehaviorTree> behaviorTree;
    ScopedBlackboard blackboard;
    bool enabled = true;
    float updateInterval = 0.0f;  // 0 = update every frame
    float timeSinceUpdate = 0.0f;
    int priority = 0;  // Higher priority AI updates first

    AIComponent() = default;

    // Move-only to handle unique_ptr
    AIComponent(AIComponent&&) = default;
    AIComponent& operator=(AIComponent&&) = default;
    AIComponent(const AIComponent&) = delete;
    AIComponent& operator=(const AIComponent&) = delete;
};

/**
 * Navigation Component - For pathfinding and movement
 */
struct NavigationComponent {
    Path currentPath;
    vec3 desiredVelocity = vec3::zero();
    float maxSpeed = 5.0f;
    float arrivalRadius = 1.0f;
    bool hasPath = false;
    bool reachedDestination = true;

    // Steering behavior weights
    float seekWeight = 1.0f;
    float separationWeight = 0.5f;
    float cohesionWeight = 0.3f;
    float alignmentWeight = 0.3f;
    float avoidanceWeight = 2.0f;

    // Wander state
    float wanderAngle = 0.0f;

    NavigationComponent() = default;
};

/**
 * Transform Component - Position and orientation
 * (Should match your existing transform component)
 */
struct TransformComponent {
    vec3 position = vec3::zero();
    vec3 rotation = vec3::zero();
    vec3 scale = vec3::one();

    TransformComponent() = default;
    TransformComponent(const vec3& pos) : position(pos) {}
};

/**
 * Velocity Component - For physics/movement
 */
struct VelocityComponent {
    vec3 velocity = vec3::zero();
    float maxSpeed = 5.0f;
    float acceleration = 10.0f;

    VelocityComponent() = default;
};

/**
 * AI System - Updates all AI components
 * Processes behavior trees, pathfinding, and steering behaviors
 */
class AISystem : public System {
public:
    explicit AISystem(int priority = 100);
    ~AISystem() override = default;

    /**
     * Update all AI entities
     */
    void update(float dt) override;

    /**
     * Initialize system
     */
    void init(ECS* ecs) override;

    /**
     * Shutdown system
     */
    void shutdown() override;

    /**
     * Get system name
     */
    const char* getName() const override { return "AISystem"; }

    /**
     * Set shared navigation mesh
     */
    void setNavMesh(std::shared_ptr<NavMesh> navMesh) {
        navMesh_ = navMesh;
        pathfinder_ = std::make_unique<Pathfinder>(navMesh_.get());
    }

    /**
     * Get navigation mesh
     */
    NavMesh* getNavMesh() const {
        return navMesh_.get();
    }

    /**
     * Get pathfinder
     */
    Pathfinder* getPathfinder() const {
        return pathfinder_.get();
    }

    /**
     * Set global blackboard (shared by all AI)
     */
    void setGlobalBlackboard(std::shared_ptr<Blackboard> blackboard) {
        globalBlackboard_ = blackboard;
    }

    /**
     * Get global blackboard
     */
    Blackboard* getGlobalBlackboard() const {
        return globalBlackboard_.get();
    }

    /**
     * Find path from start to goal
     */
    Path findPath(const vec3& start, const vec3& goal);

    /**
     * Set entity to follow path
     */
    void setEntityPath(Entity entity, const Path& path);

    /**
     * Enable/disable priority-based processing
     */
    void setPriorityProcessing(bool enabled) {
        priorityProcessing_ = enabled;
    }

    /**
     * Set reference position for priority calculation (usually player position)
     */
    void setReferencePosition(const vec3& position) {
        referencePosition_ = position;
    }

    /**
     * Enable/disable debug visualization
     */
    void setDebugMode(bool enabled) {
        debugMode_ = enabled;
    }

    /**
     * Get debug mode state
     */
    bool isDebugMode() const {
        return debugMode_;
    }

private:
    struct AIEntityData {
        Entity entity;
        AIComponent* aiComponent;
        NavigationComponent* navComponent;
        TransformComponent* transform;
        VelocityComponent* velocity;
        float distanceToReference;
    };

    /**
     * Update behavior trees
     */
    void updateBehaviorTrees(float dt);

    /**
     * Update navigation and pathfinding
     */
    void updateNavigation(float dt);

    /**
     * Update steering behaviors
     */
    void updateSteering(float dt);

    /**
     * Gather and sort AI entities by priority
     */
    std::vector<AIEntityData> gatherAIEntities();

    /**
     * Calculate priority for entity
     */
    float calculatePriority(Entity entity, const AIComponent* ai,
                           const TransformComponent* transform);

    std::shared_ptr<NavMesh> navMesh_;
    std::unique_ptr<Pathfinder> pathfinder_;
    std::shared_ptr<Blackboard> globalBlackboard_;

    vec3 referencePosition_ = vec3::zero();
    bool priorityProcessing_ = true;
    bool debugMode_ = false;

    // Team blackboards (shared by entities on same team)
    std::unordered_map<int, std::shared_ptr<Blackboard>> teamBlackboards_;
};

/**
 * AI System Factory - Helper to create common AI setups
 */
class AIFactory {
public:
    /**
     * Create a simple chase AI that follows a target
     */
    static std::unique_ptr<BehaviorTree> createChaseAI(Entity targetEntity);

    /**
     * Create a patrol AI that moves between waypoints
     */
    static std::unique_ptr<BehaviorTree> createPatrolAI(const std::vector<vec3>& waypoints);

    /**
     * Create a wander AI that moves randomly
     */
    static std::unique_ptr<BehaviorTree> createWanderAI();

    /**
     * Create a guard AI that patrols until detecting enemy, then chases
     */
    static std::unique_ptr<BehaviorTree> createGuardAI(Entity targetEntity,
                                                       const std::vector<vec3>& waypoints,
                                                       float detectionRange = 10.0f);

    /**
     * Create a flee AI that runs away from target
     */
    static std::unique_ptr<BehaviorTree> createFleeAI(Entity targetEntity, float fleeDistance = 15.0f);
};

} // namespace CatEngine
