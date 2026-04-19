#include "AISystem.hpp"
#include "../core/Logger.hpp"
#include <algorithm>
#include <cmath>

namespace CatEngine {

// ============================================================================
// AISystem Implementation
// ============================================================================

AISystem::AISystem(int priority)
    : System(priority) {
    globalBlackboard_ = std::make_shared<Blackboard>();
}

void AISystem::init(ECS* ecs) {
    System::init(ecs);
}

void AISystem::shutdown() {
    navMesh_.reset();
    pathfinder_.reset();
    globalBlackboard_.reset();
    teamBlackboards_.clear();
}

void AISystem::update(float dt) {
    if (!enabled_ || !ecs_) {
        return;
    }

    // Update in order: Behavior Trees -> Navigation -> Steering
    updateBehaviorTrees(dt);
    updateNavigation(dt);
    updateSteering(dt);
}

void AISystem::updateBehaviorTrees(float dt) {
    // Gather AI entities
    std::vector<AIEntityData> entities = gatherAIEntities();

    // Update each entity's behavior tree
    for (auto& data : entities) {
        AIComponent* ai = data.aiComponent;

        if (!ai || !ai->enabled || !ai->behaviorTree || !ai->behaviorTree->hasRoot()) {
            continue;
        }

        // Check update interval
        ai->timeSinceUpdate += dt;
        if (ai->updateInterval > 0.0f && ai->timeSinceUpdate < ai->updateInterval) {
            continue;
        }

        float actualDt = ai->timeSinceUpdate;
        ai->timeSinceUpdate = 0.0f;

        // Set up blackboard hierarchy
        if (globalBlackboard_) {
            ai->blackboard.global()->setParent(globalBlackboard_.get());
        }

        // Update current entity position in blackboard
        if (data.transform) {
            ai->blackboard.set("position", data.transform->position);
        }
        if (data.velocity) {
            ai->blackboard.set("velocity", data.velocity->velocity);
        }

        // Store reference position in blackboard
        ai->blackboard.set("referencePosition", referencePosition_);

        // Publish the ECS and the owning entity into the local blackboard so
        // factory-built lambdas can resolve target transforms without needing
        // to close over system-level pointers at construction time.
        ai->blackboard.local()->set("ecs", ecs_);
        ai->blackboard.local()->set("self", data.entity);

        // Tick behavior tree
        ai->behaviorTree->tick(actualDt, *ai->blackboard.local());

        if (debugMode_) {
            auto debugInfo = ai->behaviorTree->getDebugInfo();
            Engine::Logger::debug(
                "[AI] Entity {} status={} time={:.3f}s path='{}'\n{}",
                data.entity.id,
                static_cast<int>(debugInfo.lastStatus),
                debugInfo.totalTime,
                debugInfo.currentNodeName,
                debugInfo.structuredText);
        }
    }
}

void AISystem::updateNavigation(float dt) {
    if (!ecs_) {
        return;
    }

    // Query entities with navigation components
    auto query = ecs_->query<NavigationComponent, TransformComponent>();

    for (auto [entity, nav, transform] : query.view()) {
        if (!nav->hasPath || nav->currentPath.isEmpty()) {
            nav->reachedDestination = true;
            nav->desiredVelocity = vec3::zero();
            continue;
        }

        // Update path following
        vec3 velocity = vec3::zero();
        if (auto* vel = ecs_->getComponent<VelocityComponent>(entity)) {
            velocity = vel->velocity;
        }

        // Calculate steering toward path
        nav->desiredVelocity = SteeringBehaviors::followPath(
            transform->position,
            velocity,
            nav->currentPath,
            nav->maxSpeed,
            nav->arrivalRadius
        );

        // Check if path is complete
        if (nav->currentPath.isComplete) {
            nav->hasPath = false;
            nav->reachedDestination = true;
        } else {
            nav->reachedDestination = false;
        }
    }
}

void AISystem::updateSteering(float dt) {
    if (!ecs_) {
        return;
    }

    // Query entities with navigation and velocity
    auto query = ecs_->query<NavigationComponent, VelocityComponent, TransformComponent>();

    // Gather neighbor positions for flocking behaviors
    std::vector<vec3> allPositions;
    std::vector<vec3> allVelocities;

    for (auto [entity, nav, vel, transform] : query.view()) {
        allPositions.push_back(transform->position);
        allVelocities.push_back(vel->velocity);
    }

    // Update steering for each entity
    size_t index = 0;
    for (auto [entity, nav, vel, transform] : query.view()) {
        vec3 steering = nav->desiredVelocity * nav->seekWeight;

        // Add separation (avoid crowding neighbors)
        if (nav->separationWeight > 0.001f) {
            vec3 separation = SteeringBehaviors::separation(
                transform->position, allPositions, 3.0f
            );
            steering += separation * nav->separationWeight;
        }

        // Add cohesion (move toward center of neighbors)
        if (nav->cohesionWeight > 0.001f) {
            vec3 cohesion = SteeringBehaviors::cohesion(
                transform->position, vel->velocity, allPositions, vel->maxSpeed, 10.0f
            );
            steering += cohesion * nav->cohesionWeight;
        }

        // Add alignment (match neighbor velocities)
        if (nav->alignmentWeight > 0.001f) {
            vec3 alignment = SteeringBehaviors::alignment(
                vel->velocity, allVelocities, 10.0f
            );
            steering += alignment * nav->alignmentWeight;
        }

        // Apply steering force
        vec3 acceleration = steering;
        if (acceleration.lengthSquared() > vel->acceleration * vel->acceleration) {
            acceleration = acceleration.normalized() * vel->acceleration;
        }

        // Update velocity
        vel->velocity += acceleration * dt;

        // Clamp to max speed
        if (vel->velocity.lengthSquared() > vel->maxSpeed * vel->maxSpeed) {
            vel->velocity = vel->velocity.normalized() * vel->maxSpeed;
        }

        // Update position
        transform->position += vel->velocity * dt;

        index++;
    }
}

std::vector<AISystem::AIEntityData> AISystem::gatherAIEntities() {
    std::vector<AIEntityData> entities;

    if (!ecs_) {
        return entities;
    }

    // Query entities with AI components
    auto query = ecs_->query<AIComponent>();

    for (auto [entity, ai] : query.view()) {
        AIEntityData data;
        data.entity = entity;
        data.aiComponent = ai;
        data.navComponent = ecs_->getComponent<NavigationComponent>(entity);
        data.transform = ecs_->getComponent<TransformComponent>(entity);
        data.velocity = ecs_->getComponent<VelocityComponent>(entity);

        data.distanceToReference = calculatePriority(entity, ai, data.transform);

        entities.push_back(data);
    }

    // Sort by priority if enabled
    if (priorityProcessing_) {
        std::sort(entities.begin(), entities.end(),
            [](const AIEntityData& a, const AIEntityData& b) {
                // Higher priority first, then closer entities
                if (a.aiComponent->priority != b.aiComponent->priority) {
                    return a.aiComponent->priority > b.aiComponent->priority;
                }
                return a.distanceToReference < b.distanceToReference;
            });
    }

    return entities;
}

float AISystem::calculatePriority(Entity entity, const AIComponent* ai,
                                  const TransformComponent* transform) {
    if (!transform) {
        return std::numeric_limits<float>::max();
    }

    // Calculate distance to reference position (usually player)
    float distance = (transform->position - referencePosition_).length();

    // Apply priority modifier
    float priorityModifier = static_cast<float>(ai->priority);

    // Lower distance + higher priority = lower value (processed first)
    return distance - (priorityModifier * 10.0f);
}

Path AISystem::findPath(const vec3& start, const vec3& goal) {
    if (!pathfinder_) {
        return Path();
    }

    return pathfinder_->findPath(start, goal);
}

void AISystem::setEntityPath(Entity entity, const Path& path) {
    if (!ecs_) {
        return;
    }

    auto* nav = ecs_->getComponent<NavigationComponent>(entity);
    if (nav) {
        nav->currentPath = path;
        nav->hasPath = !path.isEmpty();
        nav->reachedDestination = false;
    }
}

// ============================================================================
// AIFactory Implementation
// ============================================================================

std::unique_ptr<BehaviorTree> AIFactory::createChaseAI(Entity targetEntity) {
    BehaviorTreeBuilder builder;

    return builder
        .sequence()
            .condition([targetEntity](Blackboard& bb) {
                // Check if target exists and is in range
                Entity* target = bb.get<Entity>("target");
                if (!target || target->id != targetEntity.id) {
                    bb.set("target", targetEntity);
                }
                return true;
            }, "FindTarget")
            .action([](float dt, Blackboard& bb) {
                Entity* target = bb.get<Entity>("target");
                vec3* selfPos = bb.get<vec3>("position");
                ECS** ecsPtr = bb.get<ECS*>("ecs");
                if (!target || !selfPos || !ecsPtr || !*ecsPtr) {
                    return BTStatus::Failure;
                }

                auto* tx = (*ecsPtr)->getComponent<TransformComponent>(*target);
                if (!tx) {
                    return BTStatus::Failure;
                }

                bb.set("target_position", tx->position);

                vec3 toTarget = tx->position - *selfPos;
                float distSq = toTarget.lengthSquared();
                if (distSq < 0.0001f) {
                    bb.set("desired_velocity", vec3::zero());
                    return BTStatus::Success;
                }

                // Produce a unit desired-velocity vector; the steering stage
                // scales it by the entity's maxSpeed and blends in flocking.
                vec3 desired = toTarget.normalized();
                bb.set("desired_velocity", desired);
                return BTStatus::Running;
            }, "ChaseTarget")
        .end()
        .build();
}

std::unique_ptr<BehaviorTree> AIFactory::createPatrolAI(const std::vector<vec3>& waypoints) {
    BehaviorTreeBuilder builder;

    return builder
        .sequence()
            .action([waypoints](float dt, Blackboard& bb) {
                // Initialize waypoints if not set
                if (!bb.has("waypointIndex")) {
                    bb.set("waypointIndex", 0);
                    bb.set("waypoints", waypoints);
                }

                int* index = bb.get<int>("waypointIndex");
                if (!index || waypoints.empty()) {
                    return BTStatus::Failure;
                }

                // Get current waypoint
                vec3 currentWaypoint = waypoints[*index];

                // Check if reached waypoint
                vec3* position = bb.get<vec3>("position");
                if (position) {
                    float distSq = (*position - currentWaypoint).lengthSquared();
                    if (distSq < 4.0f) {  // Within 2 units
                        // Move to next waypoint
                        *index = (*index + 1) % waypoints.size();
                        bb.set("waypointIndex", *index);
                    }
                }

                return BTStatus::Running;
            }, "Patrol")
        .end()
        .build();
}

std::unique_ptr<BehaviorTree> AIFactory::createWanderAI() {
    BehaviorTreeBuilder builder;

    return builder
        .action([](float dt, Blackboard& bb) {
            // Simple wander behavior
            if (!bb.has("wanderAngle")) {
                bb.set("wanderAngle", 0.0f);
            }

            float* angle = bb.get<float>("wanderAngle");
            if (angle) {
                // Update wander angle
                *angle += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * dt;
                bb.set("wanderAngle", *angle);
            }

            return BTStatus::Running;
        }, "Wander")
        .build();
}

std::unique_ptr<BehaviorTree> AIFactory::createGuardAI(Entity targetEntity,
                                                       const std::vector<vec3>& waypoints,
                                                       float detectionRange) {
    BehaviorTreeBuilder builder;

    return builder
        .selector()
            // Try to chase if target is nearby
            .sequence()
                .condition([targetEntity, detectionRange](Blackboard& bb) {
                    vec3* position = bb.get<vec3>("position");
                    ECS** ecsPtr = bb.get<ECS*>("ecs");
                    if (!position || !ecsPtr || !*ecsPtr) {
                        return false;
                    }

                    auto* tx = (*ecsPtr)->getComponent<TransformComponent>(targetEntity);
                    if (!tx) {
                        return false;
                    }

                    float distSq = (tx->position - *position).lengthSquared();
                    if (distSq > detectionRange * detectionRange) {
                        return false;
                    }

                    bb.set("target", targetEntity);
                    bb.set("target_position", tx->position);
                    return true;
                }, "DetectTarget")
                .action([](float dt, Blackboard& bb) {
                    vec3* selfPos = bb.get<vec3>("position");
                    vec3* targetPos = bb.get<vec3>("target_position");
                    if (!selfPos || !targetPos) {
                        return BTStatus::Failure;
                    }

                    vec3 toTarget = *targetPos - *selfPos;
                    if (toTarget.lengthSquared() < 0.0001f) {
                        bb.set("desired_velocity", vec3::zero());
                        return BTStatus::Success;
                    }

                    bb.set("desired_velocity", toTarget.normalized());
                    return BTStatus::Running;
                }, "ChaseTarget")
            .end()
            // Otherwise patrol
            .action([waypoints](float dt, Blackboard& bb) {
                // Patrol waypoints
                if (!bb.has("waypointIndex")) {
                    bb.set("waypointIndex", 0);
                }

                int* index = bb.get<int>("waypointIndex");
                vec3* position = bb.get<vec3>("position");

                if (index && position && !waypoints.empty()) {
                    vec3 wp = waypoints[*index];
                    float distSq = (*position - wp).lengthSquared();
                    if (distSq < 4.0f) {
                        *index = (*index + 1) % waypoints.size();
                        bb.set("waypointIndex", *index);
                    }
                }

                return BTStatus::Running;
            }, "Patrol")
        .end()
        .build();
}

std::unique_ptr<BehaviorTree> AIFactory::createFleeAI(Entity targetEntity, float fleeDistance) {
    BehaviorTreeBuilder builder;

    return builder
        .sequence()
            .condition([targetEntity, fleeDistance](Blackboard& bb) {
                vec3* position = bb.get<vec3>("position");
                ECS** ecsPtr = bb.get<ECS*>("ecs");
                if (!position || !ecsPtr || !*ecsPtr) {
                    return false;
                }

                auto* tx = (*ecsPtr)->getComponent<TransformComponent>(targetEntity);
                if (!tx) {
                    return false;
                }

                float distSq = (tx->position - *position).lengthSquared();
                if (distSq > fleeDistance * fleeDistance) {
                    return false;
                }

                bb.set("target", targetEntity);
                bb.set("target_position", tx->position);
                bb.set("fleeDistance", fleeDistance);
                return true;
            }, "CheckThreat")
            .action([](float dt, Blackboard& bb) {
                vec3* selfPos = bb.get<vec3>("position");
                vec3* targetPos = bb.get<vec3>("target_position");
                if (!selfPos || !targetPos) {
                    return BTStatus::Failure;
                }

                // Flee = unit vector pointing away from the threat. Steering
                // stage scales into world-space velocity.
                vec3 away = *selfPos - *targetPos;
                if (away.lengthSquared() < 0.0001f) {
                    bb.set("desired_velocity", vec3::zero());
                    return BTStatus::Success;
                }

                bb.set("desired_velocity", away.normalized());
                return BTStatus::Running;
            }, "Flee")
        .end()
        .build();
}

} // namespace CatEngine
