/**
 * Complete AI System Integration Example
 *
 * This example demonstrates how to set up and use the AI system
 * in the Cat Annihilation game engine.
 */

#include "AISystem.hpp"
#include "../ecs/ECS.hpp"

using namespace CatEngine;
using Engine::vec3;

// ============================================================================
// Example 1: Basic Setup
// ============================================================================

void example_basic_setup() {
    // Create ECS
    ECS ecs;

    // Add AI system with priority 100
    auto* aiSystem = ecs.createSystem<AISystem>(100);

    // Create a simple navigation mesh (triangle grid)
    auto navMesh = std::make_shared<NavMesh>();

    // Add some triangles to form a simple walkable area
    int tri0 = navMesh->addTriangle(
        vec3(0, 0, 0),
        vec3(10, 0, 0),
        vec3(5, 0, 10)
    );

    int tri1 = navMesh->addTriangle(
        vec3(10, 0, 0),
        vec3(20, 0, 0),
        vec3(15, 0, 10)
    );

    // Connect triangles for pathfinding
    navMesh->setNeighbor(tri0, 1, tri1);
    navMesh->setNeighbor(tri1, 0, tri0);

    // Give navmesh to AI system
    aiSystem->setNavMesh(navMesh);

    // Game loop
    float deltaTime = 1.0f / 60.0f;
    for (int frame = 0; frame < 1000; ++frame) {
        ecs.update(deltaTime);
    }
}

// ============================================================================
// Example 2: Creating AI Enemies
// ============================================================================

void example_create_enemies(ECS& ecs, Entity playerEntity) {
    // Enemy spawn positions
    std::vector<vec3> spawnPositions = {
        vec3(10, 0, 10),
        vec3(20, 0, 15),
        vec3(-5, 0, 8),
        vec3(15, 0, -10)
    };

    for (const vec3& spawnPos : spawnPositions) {
        // Create enemy entity
        Entity enemy = ecs.createEntity();

        // Add transform
        ecs.addComponent(enemy, TransformComponent{spawnPos});

        // Add velocity for movement
        auto& vel = ecs.addComponent(enemy, VelocityComponent{});
        vel.maxSpeed = 6.0f;
        vel.acceleration = 15.0f;

        // Add navigation
        auto& nav = ecs.addComponent(enemy, NavigationComponent{});
        nav.maxSpeed = 6.0f;
        nav.arrivalRadius = 2.0f;
        nav.separationWeight = 0.7f;  // Keep distance from other enemies
        nav.cohesionWeight = 0.3f;    // Stay somewhat grouped

        // Create chase AI behavior
        auto& ai = ecs.emplaceComponent<AIComponent>(enemy);
        ai.behaviorTree = AIFactory::createChaseAI(playerEntity);
        ai.enabled = true;
        ai.priority = 5;
        ai.updateInterval = 0.1f;  // Update 10 times per second

        // Initialize blackboard
        ai.blackboard.set("health", 100.0f);
        ai.blackboard.set("attackDamage", 10.0f);
        ai.blackboard.set("detectionRange", 25.0f);
    }
}

// ============================================================================
// Example 3: Custom Behavior Tree
// ============================================================================

std::unique_ptr<BehaviorTree> createBossAI(Entity playerEntity) {
    BehaviorTreeBuilder builder;

    return builder
        .selector()
            // Phase 1: Enraged (low health)
            .sequence()
                .condition([](Blackboard& bb) {
                    float* health = bb.get<float>("health");
                    return health && *health < 30.0f;
                }, "LowHealth")
                .selector()
                    // Try special attack
                    .sequence()
                        .condition([](Blackboard& bb) {
                            float* cooldown = bb.get<float>("specialAttackCooldown");
                            return !cooldown || *cooldown <= 0.0f;
                        }, "SpecialReady")
                        .action([](float dt, Blackboard& bb) {
                            // Execute special attack
                            bb.set("specialAttackCooldown", 5.0f);
                            bb.set("isAttacking", true);
                            return BTStatus::Success;
                        }, "SpecialAttack")
                    .end()
                    // Otherwise aggressive chase
                    .action([](float dt, Blackboard& bb) {
                        bb.set("speed", 10.0f);  // Fast!
                        return BTStatus::Running;
                    }, "AggressiveChase")
                .end()
            .end()

            // Phase 2: Normal combat
            .sequence()
                .condition([](Blackboard& bb) {
                    vec3* pos = bb.get<vec3>("position");
                    vec3* playerPos = bb.getHierarchical<vec3>("playerPosition");
                    if (!pos || !playerPos) return false;

                    float dist = (*pos - *playerPos).length();
                    return dist < 5.0f;  // Player is close
                }, "PlayerInRange")
                .selector()
                    // Melee attack
                    .sequence()
                        .condition([](Blackboard& bb) {
                            float* cooldown = bb.get<float>("attackCooldown");
                            return !cooldown || *cooldown <= 0.0f;
                        }, "AttackReady")
                        .action([](float dt, Blackboard& bb) {
                            bb.set("attackCooldown", 2.0f);
                            bb.set("isAttacking", true);
                            return BTStatus::Success;
                        }, "MeleeAttack")
                    .end()
                    // Circle strafe
                    .action([](float dt, Blackboard& bb) {
                        bb.set("isStrafing", true);
                        return BTStatus::Running;
                    }, "CircleStrafe")
                .end()
            .end()

            // Phase 3: Patrol/Search
            .action([](float dt, Blackboard& bb) {
                // Patrol behavior
                if (!bb.has("patrolIndex")) {
                    bb.set("patrolIndex", 0);
                }
                return BTStatus::Running;
            }, "Patrol")
        .end()
        .build();
}

void example_boss_enemy(ECS& ecs, Entity playerEntity) {
    Entity boss = ecs.createEntity();

    // Boss transform
    ecs.addComponent(boss, TransformComponent{vec3(50, 0, 50)});

    // Boss movement
    auto& vel = ecs.addComponent(boss, VelocityComponent{});
    vel.maxSpeed = 8.0f;
    vel.acceleration = 20.0f;

    // Boss navigation
    auto& nav = ecs.addComponent(boss, NavigationComponent{});
    nav.maxSpeed = 8.0f;
    nav.arrivalRadius = 3.0f;

    // Boss AI
    auto& ai = ecs.emplaceComponent<AIComponent>(boss);
    ai.behaviorTree = createBossAI(playerEntity);
    ai.enabled = true;
    ai.priority = 100;  // Highest priority - always updates first
    ai.updateInterval = 0.0f;  // Update every frame

    // Boss stats
    ai.blackboard.set("health", 500.0f);
    ai.blackboard.set("maxHealth", 500.0f);
    ai.blackboard.set("attackDamage", 25.0f);
    ai.blackboard.set("attackCooldown", 0.0f);
    ai.blackboard.set("specialAttackCooldown", 0.0f);
}

// ============================================================================
// Example 4: Pathfinding
// ============================================================================

void example_pathfinding(ECS& ecs) {
    auto* aiSystem = ecs.getSystem<AISystem>();
    if (!aiSystem) return;

    Entity entity = ecs.createEntity();
    ecs.addComponent(entity, TransformComponent{vec3(0, 0, 0)});
    ecs.addComponent(entity, NavigationComponent{});

    // Find path from entity to goal
    vec3 start = vec3(0, 0, 0);
    vec3 goal = vec3(50, 0, 50);

    Path path = aiSystem->findPath(start, goal);

    if (!path.isEmpty()) {
        // Set entity to follow the path
        aiSystem->setEntityPath(entity, path);

        // Or manually
        auto* nav = ecs.getComponent<NavigationComponent>(entity);
        if (nav) {
            nav->currentPath = path;
            nav->hasPath = true;
            nav->reachedDestination = false;
        }
    }
}

// ============================================================================
// Example 5: Flocking Behavior
// ============================================================================

void example_flocking(ECS& ecs, int boidCount = 50) {
    for (int i = 0; i < boidCount; ++i) {
        Entity boid = ecs.createEntity();

        // Random start position
        float x = (rand() % 100) - 50.0f;
        float z = (rand() % 100) - 50.0f;
        ecs.addComponent(boid, TransformComponent{vec3(x, 0, z)});

        // Velocity
        auto& vel = ecs.addComponent(boid, VelocityComponent{});
        vel.maxSpeed = 5.0f;

        // Navigation with flocking weights
        auto& nav = ecs.addComponent(boid, NavigationComponent{});
        nav.maxSpeed = 5.0f;
        nav.separationWeight = 1.0f;   // Strong separation
        nav.cohesionWeight = 0.5f;     // Moderate cohesion
        nav.alignmentWeight = 0.8f;    // Strong alignment

        // Simple wander AI
        auto& ai = ecs.emplaceComponent<AIComponent>(boid);
        ai.behaviorTree = AIFactory::createWanderAI();
        ai.enabled = true;
        ai.updateInterval = 0.05f;  // 20 updates per second
    }
}

// ============================================================================
// Example 6: Global and Team Blackboards
// ============================================================================

void example_shared_state(ECS& ecs, Entity playerEntity) {
    auto* aiSystem = ecs.getSystem<AISystem>();
    if (!aiSystem) return;

    // Set global state (available to all AI)
    auto* globalBB = aiSystem->getGlobalBlackboard();
    if (globalBB) {
        globalBB->set("playerEntity", playerEntity);
        globalBB->set("playerPosition", vec3(0, 0, 0));
        globalBB->set("alertLevel", 0);  // 0 = calm, 1 = alerted, 2 = combat
        globalBB->set("reinforcementsCalled", false);
    }

    // Create team of enemies that share state
    for (int i = 0; i < 5; ++i) {
        Entity enemy = ecs.createEntity();
        ecs.addComponent(enemy, TransformComponent{vec3(i * 5.0f, 0, 10)});
        ecs.addComponent(enemy, VelocityComponent{});
        ecs.addComponent(enemy, NavigationComponent{});

        auto& ai = ecs.emplaceComponent<AIComponent>(enemy);

        // Set team-shared data
        ai.blackboard.team()->set("teamId", 1);
        ai.blackboard.team()->set("formationCenter", vec3(10, 0, 10));
        ai.blackboard.team()->set("tacticalMode", std::string("defensive"));

        // Create AI that uses shared state
        BehaviorTreeBuilder builder;
        ai.behaviorTree = builder
            .sequence()
                .condition([](Blackboard& bb) {
                    // Check global alert level
                    int* alertLevel = bb.getHierarchical<int>("alertLevel");
                    return alertLevel && *alertLevel > 0;
                }, "IsAlerted")
                .action([](float dt, Blackboard& bb) {
                    // Combat behavior
                    return BTStatus::Running;
                }, "CombatMode")
            .end()
            .build();

        ai.enabled = true;
    }
}

// ============================================================================
// Example 7: Advanced Behavior with Decorators
// ============================================================================

std::unique_ptr<BehaviorTree> createAdvancedPatrolAI() {
    BehaviorTreeBuilder builder;

    // Note: This example shows manual tree construction
    auto root = std::make_unique<BTSelector>();

    // Branch 1: Combat if enemy detected
    {
        auto combatSequence = std::make_unique<BTSequence>();

        combatSequence->addChild(std::make_unique<BTCondition>(
            [](Blackboard& bb) {
                return bb.has("enemyDetected") &&
                       bb.getOrDefault("enemyDetected", false);
            },
            "EnemyDetected"
        ));

        combatSequence->addChild(std::make_unique<BTAction>(
            [](float dt, Blackboard& bb) {
                return BTStatus::Running;
            },
            "EngageCombat"
        ));

        root->addChild(std::move(combatSequence));
    }

    // Branch 2: Patrol with timeout
    {
        auto patrolSequence = std::make_unique<BTSequence>();

        // Repeater: patrol 3 times before succeeding
        auto repeater = std::make_unique<BTRepeater>(
            std::make_unique<BTAction>(
                [](float dt, Blackboard& bb) {
                    // Patrol logic
                    return BTStatus::Success;
                },
                "PatrolWaypoint"
            ),
            3  // Repeat 3 times
        );

        patrolSequence->addChild(std::move(repeater));

        // After 3 patrols, wait for 2 seconds
        patrolSequence->addChild(std::make_unique<BTWait>(2.0f));

        root->addChild(std::move(patrolSequence));
    }

    // Create tree and set root
    auto tree = std::make_unique<BehaviorTree>();
    tree->setRoot(std::move(root));

    return tree;
}

// ============================================================================
// Example 8: Steering Behaviors
// ============================================================================

void example_steering_behaviors(ECS& ecs) {
    // The AI system automatically applies steering behaviors
    // based on NavigationComponent settings

    Entity entity = ecs.createEntity();
    auto& transform = ecs.addComponent(entity, TransformComponent{vec3(0, 0, 0)});
    auto& vel = ecs.addComponent(entity, VelocityComponent{});
    auto& nav = ecs.addComponent(entity, NavigationComponent{});

    // Configure steering weights
    nav.seekWeight = 1.0f;          // Move toward target
    nav.separationWeight = 0.8f;    // Avoid crowding
    nav.cohesionWeight = 0.3f;      // Stay with group
    nav.alignmentWeight = 0.5f;     // Match group direction
    nav.avoidanceWeight = 2.0f;     // Avoid obstacles (high priority)

    // Set a target path
    nav.currentPath.addWaypoint(vec3(10, 0, 0));
    nav.currentPath.addWaypoint(vec3(20, 0, 10));
    nav.currentPath.addWaypoint(vec3(30, 0, 0));
    nav.hasPath = true;

    // The AI system will automatically:
    // 1. Follow the path using seek/arrive
    // 2. Avoid other entities using separation
    // 3. Match nearby entity velocities using alignment
    // 4. Stay near the group center using cohesion
}

// ============================================================================
// Example 9: Complete Game Integration
// ============================================================================

class GameWorld {
public:
    void init() {
        // Create AI system
        aiSystem_ = ecs_.createSystem<AISystem>(100);

        // Create navigation mesh
        navMesh_ = std::make_shared<NavMesh>();
        buildNavMesh();
        aiSystem_->setNavMesh(navMesh_);

        // Create player
        player_ = ecs_.createEntity();
        ecs_.addComponent(player_, TransformComponent{vec3(0, 0, 0)});

        // Create enemies
        spawnEnemies();

        // Set reference position for priority processing
        aiSystem_->setReferencePosition(vec3(0, 0, 0));
    }

    void update(float deltaTime) {
        // Update player position in global blackboard
        auto* playerTransform = ecs_.getComponent<TransformComponent>(player_);
        if (playerTransform) {
            auto* globalBB = aiSystem_->getGlobalBlackboard();
            if (globalBB) {
                globalBB->set("playerPosition", playerTransform->position);
                globalBB->set("playerEntity", player_);
            }

            // Update reference position for AI prioritization
            aiSystem_->setReferencePosition(playerTransform->position);
        }

        // Update all systems (including AI)
        ecs_.update(deltaTime);
    }

private:
    void buildNavMesh() {
        // Build navigation mesh from level geometry
        // This is a simplified example
        for (int x = 0; x < 10; ++x) {
            for (int z = 0; z < 10; ++z) {
                float x0 = x * 10.0f;
                float z0 = z * 10.0f;
                float x1 = (x + 1) * 10.0f;
                float z1 = (z + 1) * 10.0f;

                // Two triangles per grid cell
                int tri0 = navMesh_->addTriangle(
                    vec3(x0, 0, z0),
                    vec3(x1, 0, z0),
                    vec3(x0, 0, z1)
                );

                int tri1 = navMesh_->addTriangle(
                    vec3(x1, 0, z0),
                    vec3(x1, 0, z1),
                    vec3(x0, 0, z1)
                );

                // Set neighbors
                navMesh_->setNeighbor(tri0, 1, tri1);
                navMesh_->setNeighbor(tri1, 2, tri0);
            }
        }
    }

    void spawnEnemies() {
        example_create_enemies(ecs_, player_);
        example_boss_enemy(ecs_, player_);
        example_flocking(ecs_, 20);
    }

    ECS ecs_;
    AISystem* aiSystem_ = nullptr;
    std::shared_ptr<NavMesh> navMesh_;
    Entity player_;
};

// ============================================================================
// Main
// ============================================================================

int main() {
    GameWorld world;
    world.init();

    // Game loop
    const float dt = 1.0f / 60.0f;  // 60 FPS
    for (int frame = 0; frame < 10000; ++frame) {
        world.update(dt);
    }

    return 0;
}
