# Cat Annihilation AI System

A comprehensive AI system for the Cat Annihilation game engine, featuring behavior trees, navigation meshes, pathfinding, and steering behaviors.

## Overview

The AI system consists of several interconnected components:

- **AISystem**: ECS system that manages all AI updates
- **BehaviorTree**: Hierarchical decision-making system
- **BTNode**: Base class and various node types for behavior trees
- **Blackboard**: Key-value storage for AI state
- **Navigation**: NavMesh, pathfinding (A*), and steering behaviors

## File Structure

```
engine/ai/
├── AISystem.hpp/cpp       - Main ECS system for AI updates
├── BehaviorTree.hpp/cpp   - Behavior tree implementation
├── BTNode.hpp             - Behavior tree node types (header-only)
├── Blackboard.hpp/cpp     - State storage for AI
└── Navigation.hpp/cpp     - Pathfinding and steering
```

## Quick Start

### 1. Initialize AI System

```cpp
#include "engine/ai/AISystem.hpp"

// Create ECS and add AI system
ECS ecs;
auto* aiSystem = ecs.createSystem<AISystem>(100); // priority 100

// Create and set navigation mesh
auto navMesh = std::make_shared<NavMesh>();
// Add triangles to navmesh...
navMesh->addTriangle(vec3(0,0,0), vec3(10,0,0), vec3(5,0,10));

aiSystem->setNavMesh(navMesh);
```

### 2. Create an AI Entity

```cpp
// Create entity
Entity enemy = ecs.createEntity();

// Add required components
ecs.addComponent(enemy, TransformComponent{vec3(0, 0, 0)});
ecs.addComponent(enemy, VelocityComponent{});
ecs.addComponent(enemy, NavigationComponent{});

// Add AI component with behavior tree
auto& ai = ecs.emplaceComponent<AIComponent>(enemy);
ai.behaviorTree = AIFactory::createChaseAI(playerEntity);
ai.enabled = true;
ai.priority = 10; // Higher priority = updates first
```

### 3. Update Each Frame

```cpp
// In your game loop
float deltaTime = 0.016f; // 60 FPS
ecs.update(deltaTime);
```

## Behavior Trees

### Using the Builder API

```cpp
BehaviorTreeBuilder builder;

auto tree = builder
    .selector()
        // Try to attack if enemy is close
        .sequence()
            .condition([](Blackboard& bb) {
                float* dist = bb.get<float>("distanceToEnemy");
                return dist && *dist < 5.0f;
            }, "EnemyInRange")
            .action([](float dt, Blackboard& bb) {
                // Attack enemy
                return BTStatus::Success;
            }, "Attack")
        .end()

        // Otherwise, patrol
        .action([](float dt, Blackboard& bb) {
            // Patrol behavior
            return BTStatus::Running;
        }, "Patrol")
    .end()
    .build();
```

### Node Types

**Composite Nodes:**
- `Selector`: Try children until one succeeds (OR)
- `Sequence`: Run children until one fails (AND)
- `Parallel`: Run all children simultaneously

**Decorator Nodes:**
- `Inverter`: Flip success/failure
- `Repeater`: Repeat child N times
- `Succeeder`: Always succeed
- `UntilFail`: Repeat until failure

**Leaf Nodes:**
- `Action`: Execute custom logic
- `Condition`: Check blackboard value
- `Wait`: Wait for duration

## Blackboard

### Basic Usage

```cpp
Blackboard blackboard;

// Set values
blackboard.set("health", 100.0f);
blackboard.set("position", vec3(10, 0, 5));
blackboard.set("isAlerted", true);

// Get values
float* health = blackboard.get<float>("health");
if (health) {
    *health -= 10.0f;
}

// Get with default
float speed = blackboard.getOrDefault("speed", 5.0f);

// Check existence
if (blackboard.has("target")) {
    // ...
}
```

### Scoped Blackboards

```cpp
ScopedBlackboard scoped;

// Set in local scope
scoped.local()->set("myState", 42);

// Set in team scope (shared by team)
scoped.team()->set("teamGoal", vec3(100, 0, 100));

// Set in global scope (shared by all)
scoped.global()->set("playerPosition", vec3(0, 0, 0));

// Hierarchical lookup (local -> team -> global)
const vec3* pos = scoped.get<vec3>("playerPosition");
```

### Observers

```cpp
// Watch for any changes
size_t id = blackboard.addObserver([](const std::string& key) {
    std::cout << "Value changed: " << key << std::endl;
});

// Watch specific key
blackboard.addObserver("health", [](const std::string& key) {
    std::cout << "Health changed!" << std::endl;
});

// Remove observer
blackboard.removeObserver(id);
```

## Navigation & Pathfinding

### Creating a NavMesh

```cpp
auto navMesh = std::make_shared<NavMesh>();

// Add triangles
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

// Set neighbors (for pathfinding)
navMesh->setNeighbor(tri0, 1, tri1); // Edge 1 of tri0 connects to tri1
navMesh->setNeighbor(tri1, 0, tri0);

// Give to AI system
aiSystem->setNavMesh(navMesh);
```

### Pathfinding

```cpp
// Find path
vec3 start(0, 0, 0);
vec3 goal(15, 0, 8);
Path path = aiSystem->findPath(start, goal);

// Set entity to follow path
aiSystem->setEntityPath(enemyEntity, path);

// Or manually
auto* nav = ecs.getComponent<NavigationComponent>(enemyEntity);
if (nav) {
    nav->currentPath = path;
    nav->hasPath = true;
}
```

### Steering Behaviors

All steering behaviors are static methods in `SteeringBehaviors` class:

```cpp
using SteeringBehaviors as SB;

vec3 position(10, 0, 5);
vec3 velocity(1, 0, 0);
vec3 target(20, 0, 10);
float maxSpeed = 5.0f;

// Seek: Move toward target
vec3 seekForce = SB::seek(position, velocity, target, maxSpeed);

// Flee: Move away from target
vec3 fleeForce = SB::flee(position, velocity, target, maxSpeed);

// Arrive: Slow down when approaching
vec3 arriveForce = SB::arrive(position, velocity, target, maxSpeed, 10.0f);

// Wander: Random movement
float wanderAngle = 0.0f;
vec3 wanderForce = SB::wander(position, velocity, maxSpeed, wanderAngle, dt);

// Path following
vec3 followForce = SB::followPath(position, velocity, path, maxSpeed);

// Flocking behaviors
std::vector<vec3> neighbors = { vec3(9, 0, 5), vec3(11, 0, 6) };
vec3 separation = SB::separation(position, neighbors, 3.0f);
vec3 cohesion = SB::cohesion(position, velocity, neighbors, maxSpeed);

std::vector<vec3> neighborVels = { vec3(1, 0, 0.5f), vec3(0.8f, 0, 0.3f) };
vec3 alignment = SB::alignment(velocity, neighborVels);
```

## AI Factory

Pre-built behavior trees for common AI patterns:

```cpp
// Chase AI - follows target entity
auto chaseTree = AIFactory::createChaseAI(playerEntity);

// Patrol AI - moves between waypoints
std::vector<vec3> waypoints = {
    vec3(0, 0, 0),
    vec3(10, 0, 0),
    vec3(10, 0, 10),
    vec3(0, 0, 10)
};
auto patrolTree = AIFactory::createPatrolAI(waypoints);

// Wander AI - random movement
auto wanderTree = AIFactory::createWanderAI();

// Guard AI - patrols until enemy detected, then chases
auto guardTree = AIFactory::createGuardAI(
    playerEntity,
    waypoints,
    15.0f  // detection range
);

// Flee AI - runs away from target
auto fleeTree = AIFactory::createFleeAI(playerEntity, 20.0f);
```

## Advanced Features

### Priority-Based Processing

Entities closer to a reference position (usually the player) update first:

```cpp
// Enable priority processing (enabled by default)
aiSystem->setPriorityProcessing(true);

// Set reference position (e.g., player position)
aiSystem->setReferencePosition(playerPosition);

// Set entity priority (higher = more important)
auto& ai = ecs.getComponent<AIComponent>(bossEntity);
ai.priority = 100; // Boss always updates first
```

### Update Intervals

Control how often an AI updates (for performance):

```cpp
auto& ai = ecs.getComponent<AIComponent>(entity);
ai.updateInterval = 0.0f;   // Every frame (default)
ai.updateInterval = 0.1f;   // 10 times per second
ai.updateInterval = 0.5f;   // 2 times per second
```

### Debug Mode

```cpp
// Enable debug visualization
aiSystem->setDebugMode(true);

// Get debug info from behavior tree
auto debugInfo = behaviorTree.getDebugInfo();
std::cout << "Current node: " << debugInfo.currentNodeName << std::endl;
std::cout << "Status: " << (int)debugInfo.lastStatus << std::endl;
std::cout << "Running time: " << debugInfo.totalTime << std::endl;
```

## Complete Example: Enemy AI

```cpp
// Create enemy entity
Entity enemy = ecs.createEntity();

// Add components
ecs.addComponent(enemy, TransformComponent{vec3(50, 0, 50)});
ecs.addComponent(enemy, VelocityComponent{});

auto& nav = ecs.addComponent(enemy, NavigationComponent{});
nav.maxSpeed = 8.0f;
nav.separationWeight = 0.8f;  // Strong separation from other enemies

// Create behavior tree
BehaviorTreeBuilder builder;
auto tree = builder
    .selector()
        // Attack if player is very close
        .sequence()
            .condition([](Blackboard& bb) {
                vec3* pos = bb.get<vec3>("position");
                vec3* playerPos = bb.get<vec3>("playerPosition");
                if (!pos || !playerPos) return false;

                float dist = (*pos - *playerPos).length();
                return dist < 3.0f;
            }, "PlayerInAttackRange")
            .action([](float dt, Blackboard& bb) {
                // Attack player
                bb.set("attacking", true);
                return BTStatus::Success;
            }, "Attack")
        .end()

        // Chase if player is in detection range
        .sequence()
            .condition([](Blackboard& bb) {
                vec3* pos = bb.get<vec3>("position");
                vec3* playerPos = bb.get<vec3>("playerPosition");
                if (!pos || !playerPos) return false;

                float dist = (*pos - *playerPos).length();
                return dist < 20.0f;
            }, "PlayerDetected")
            .action([](float dt, Blackboard& bb) {
                // Path toward player
                vec3* playerPos = bb.get<vec3>("playerPosition");
                if (playerPos) {
                    bb.set("targetPosition", *playerPos);
                    return BTStatus::Running;
                }
                return BTStatus::Failure;
            }, "ChasePlayer")
        .end()

        // Default: Patrol
        .action([](float dt, Blackboard& bb) {
            // Wander around
            return BTStatus::Running;
        }, "Patrol")
    .end()
    .build();

// Add AI component
auto& ai = ecs.emplaceComponent<AIComponent>(enemy);
ai.behaviorTree = std::move(tree);
ai.enabled = true;
ai.priority = 5;
ai.updateInterval = 0.1f;  // Update 10 times per second

// Set up blackboard with initial values
ai.blackboard.set("attackDamage", 10.0f);
ai.blackboard.set("detectionRange", 20.0f);
```

## Performance Tips

1. **Use update intervals** for distant/unimportant AI
2. **Enable priority processing** to focus on nearby enemies
3. **Limit pathfinding** - cache paths, only recalculate when needed
4. **Use simple behaviors** for background NPCs
5. **Batch spatial queries** - the system does this automatically for flocking

## Integration with Physics

The AI system updates positions directly, but can integrate with physics:

```cpp
// In your physics system, after AI updates:
auto query = ecs.query<TransformComponent, VelocityComponent, RigidBodyComponent>();
for (auto [entity, transform, vel, rb] : query.view()) {
    // Apply velocity to physics body
    rb.setLinearVelocity(vel.velocity);
}
```

## Architecture Notes

- Uses C++20 features (concepts, ranges where beneficial)
- Header-only templates for performance (BTNode, Blackboard)
- SIMD-optimized vec3 (from engine/math/Vector.hpp)
- ECS-friendly design (components are POD or move-only)
- Memory-efficient (entity data is packed in component pools)

## Dependencies

- `engine/ecs/` - Entity Component System
- `engine/math/` - Vector math (vec3 with SSE)
- C++20 compiler
- STL (vector, unordered_map, any, functional, memory, queue)

## Thread Safety

Current implementation is **not thread-safe**. For multithreading:
- Each thread should have its own AISystem instance
- Use separate blackboards per thread
- Synchronize access to shared NavMesh

## Future Enhancements

- **Goal-Oriented Action Planning (GOAP)**
- **Utility AI** for decision-making
- **Influence maps** for tactical positioning
- **Formation movement**
- **Cover system**
- **Hierarchical pathfinding** for large worlds
- **Dynamic obstacle avoidance** with RVO
- **Machine learning integration**

## License

Part of the Cat Annihilation game engine.
