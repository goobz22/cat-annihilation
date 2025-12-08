#pragma once

#include "../math/Vector.hpp"
#include <vector>
#include <memory>
#include <limits>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <cmath>

namespace CatEngine {

using Engine::vec3;

/**
 * A single waypoint in a path
 */
struct Waypoint {
    vec3 position;
    float radius = 1.0f;  // Acceptance radius

    Waypoint() = default;
    Waypoint(const vec3& pos, float rad = 1.0f) : position(pos), radius(rad) {}
};

/**
 * A path consisting of waypoints
 */
struct Path {
    std::vector<Waypoint> waypoints;
    size_t currentWaypoint = 0;
    bool isComplete = false;

    void reset() {
        currentWaypoint = 0;
        isComplete = false;
    }

    void clear() {
        waypoints.clear();
        reset();
    }

    bool isEmpty() const {
        return waypoints.empty();
    }

    size_t size() const {
        return waypoints.size();
    }

    const Waypoint* getCurrentWaypoint() const {
        if (isComplete || currentWaypoint >= waypoints.size()) {
            return nullptr;
        }
        return &waypoints[currentWaypoint];
    }

    bool advanceWaypoint() {
        if (currentWaypoint < waypoints.size() - 1) {
            currentWaypoint++;
            return true;
        }
        isComplete = true;
        return false;
    }

    void addWaypoint(const vec3& position, float radius = 1.0f) {
        waypoints.emplace_back(position, radius);
    }

    void reverse() {
        std::reverse(waypoints.begin(), waypoints.end());
        reset();
    }
};

/**
 * Triangle in navigation mesh
 */
struct NavTriangle {
    vec3 vertices[3];
    vec3 center;
    int neighbors[3] = {-1, -1, -1};  // Indices of adjacent triangles
    float cost = 1.0f;  // Movement cost multiplier
    int id = -1;

    NavTriangle() : center(vec3::zero()) {
        vertices[0] = vec3::zero();
        vertices[1] = vec3::zero();
        vertices[2] = vec3::zero();
    }

    NavTriangle(const vec3& v0, const vec3& v1, const vec3& v2)
        : center((v0 + v1 + v2) / 3.0f) {
        vertices[0] = v0;
        vertices[1] = v1;
        vertices[2] = v2;
    }

    bool contains(const vec3& point) const;
    vec3 closestPoint(const vec3& point) const;
};

/**
 * Navigation mesh for pathfinding
 */
class NavMesh {
public:
    NavMesh() = default;

    /**
     * Add a triangle to the navigation mesh
     */
    int addTriangle(const vec3& v0, const vec3& v1, const vec3& v2, float cost = 1.0f) {
        int id = static_cast<int>(triangles_.size());
        NavTriangle tri(v0, v1, v2);
        tri.cost = cost;
        tri.id = id;
        triangles_.push_back(tri);
        return id;
    }

    /**
     * Set neighbor relationship between triangles
     */
    void setNeighbor(int triangleId, int edge, int neighborId) {
        if (triangleId >= 0 && triangleId < triangles_.size() && edge >= 0 && edge < 3) {
            triangles_[triangleId].neighbors[edge] = neighborId;
        }
    }

    /**
     * Find triangle containing point
     */
    int findTriangle(const vec3& point) const {
        for (size_t i = 0; i < triangles_.size(); ++i) {
            if (triangles_[i].contains(point)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /**
     * Get closest point on navmesh to given point
     */
    vec3 getClosestPoint(const vec3& point) const {
        if (triangles_.empty()) {
            return point;
        }

        vec3 closest = triangles_[0].closestPoint(point);
        float minDist = (closest - point).lengthSquared();

        for (size_t i = 1; i < triangles_.size(); ++i) {
            vec3 candidate = triangles_[i].closestPoint(point);
            float dist = (candidate - point).lengthSquared();
            if (dist < minDist) {
                minDist = dist;
                closest = candidate;
            }
        }

        return closest;
    }

    /**
     * Get triangle by index
     */
    const NavTriangle* getTriangle(int index) const {
        if (index >= 0 && index < triangles_.size()) {
            return &triangles_[index];
        }
        return nullptr;
    }

    /**
     * Get number of triangles
     */
    size_t getTriangleCount() const {
        return triangles_.size();
    }

    /**
     * Clear all triangles
     */
    void clear() {
        triangles_.clear();
    }

private:
    std::vector<NavTriangle> triangles_;
};

/**
 * A* pathfinding on navigation mesh
 */
class Pathfinder {
public:
    explicit Pathfinder(const NavMesh* navMesh) : navMesh_(navMesh) {}

    /**
     * Find path from start to goal
     * @param start Starting position
     * @param goal Goal position
     * @return Path from start to goal (empty if no path found)
     */
    Path findPath(const vec3& start, const vec3& goal);

    /**
     * Set heuristic weight (higher = more greedy, less optimal but faster)
     */
    void setHeuristicWeight(float weight) { heuristicWeight_ = weight; }

private:
    struct Node {
        int triangleId;
        float g = std::numeric_limits<float>::infinity();  // Cost from start
        float h = 0.0f;  // Heuristic to goal
        float f = std::numeric_limits<float>::infinity();  // Total cost (g + h)
        int parent = -1;

        bool operator>(const Node& other) const {
            return f > other.f;
        }
    };

    float heuristic(const vec3& a, const vec3& b) const {
        return (b - a).length() * heuristicWeight_;
    }

    const NavMesh* navMesh_;
    float heuristicWeight_ = 1.0f;
};

/**
 * Steering behaviors for AI movement
 */
class SteeringBehaviors {
public:
    /**
     * Seek: Move toward target
     * @param position Current position
     * @param velocity Current velocity
     * @param target Target position
     * @param maxSpeed Maximum speed
     * @return Desired velocity
     */
    static vec3 seek(const vec3& position, const vec3& velocity,
                     const vec3& target, float maxSpeed) {
        vec3 desired = (target - position).normalized() * maxSpeed;
        return desired - velocity;
    }

    /**
     * Flee: Move away from target
     */
    static vec3 flee(const vec3& position, const vec3& velocity,
                     const vec3& target, float maxSpeed) {
        vec3 desired = (position - target).normalized() * maxSpeed;
        return desired - velocity;
    }

    /**
     * Arrive: Slow down when approaching target
     * @param slowingRadius Distance at which to start slowing down
     */
    static vec3 arrive(const vec3& position, const vec3& velocity,
                       const vec3& target, float maxSpeed, float slowingRadius = 5.0f) {
        vec3 toTarget = target - position;
        float distance = toTarget.length();

        if (distance < 0.001f) {
            return -velocity;  // Stop
        }

        float speed = maxSpeed;
        if (distance < slowingRadius) {
            speed = maxSpeed * (distance / slowingRadius);
        }

        vec3 desired = toTarget.normalized() * speed;
        return desired - velocity;
    }

    /**
     * Wander: Random movement
     */
    static vec3 wander(const vec3& position, const vec3& velocity,
                       float maxSpeed, float& wanderAngle, float deltaTime,
                       float wanderRadius = 2.0f, float wanderDistance = 4.0f,
                       float wanderJitter = 1.0f) {
        // Update wander angle with some randomness
        wanderAngle += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * wanderJitter * deltaTime;

        // Calculate circle center in front of agent
        vec3 forward = velocity.lengthSquared() > 0.001f ?
                      velocity.normalized() : vec3::forward();
        vec3 circleCenter = position + forward * wanderDistance;

        // Calculate displacement on circle
        vec3 displacement(
            std::cos(wanderAngle) * wanderRadius,
            0.0f,
            std::sin(wanderAngle) * wanderRadius
        );

        vec3 target = circleCenter + displacement;
        return seek(position, velocity, target, maxSpeed);
    }

    /**
     * Path following: Get steering toward next waypoint
     */
    static vec3 followPath(const vec3& position, const vec3& velocity,
                          Path& path, float maxSpeed, float arrivalRadius = 1.0f) {
        const Waypoint* waypoint = path.getCurrentWaypoint();
        if (!waypoint) {
            return vec3::zero();
        }

        // Check if we reached current waypoint
        float distSq = (waypoint->position - position).lengthSquared();
        if (distSq < waypoint->radius * waypoint->radius) {
            path.advanceWaypoint();
            waypoint = path.getCurrentWaypoint();
            if (!waypoint) {
                return vec3::zero();
            }
        }

        // Use arrive behavior for last waypoint, seek for others
        if (path.currentWaypoint == path.waypoints.size() - 1) {
            return arrive(position, velocity, waypoint->position, maxSpeed, arrivalRadius);
        } else {
            return seek(position, velocity, waypoint->position, maxSpeed);
        }
    }

    /**
     * Obstacle avoidance: Steer away from obstacles
     */
    static vec3 avoidObstacles(const vec3& position, const vec3& velocity,
                              const std::vector<vec3>& obstacles,
                              const std::vector<float>& obstacleRadii,
                              float maxSpeed, float detectionDistance = 5.0f) {
        vec3 forward = velocity.lengthSquared() > 0.001f ?
                      velocity.normalized() : vec3::forward();

        vec3 avoidance = vec3::zero();
        float closestDist = detectionDistance;

        for (size_t i = 0; i < obstacles.size(); ++i) {
            vec3 toObstacle = obstacles[i] - position;
            float dist = toObstacle.length();

            if (dist > detectionDistance) {
                continue;
            }

            // Check if obstacle is in front
            float dot = toObstacle.normalized().dot(forward);
            if (dot < 0.0f) {
                continue;
            }

            float radius = (i < obstacleRadii.size()) ? obstacleRadii[i] : 1.0f;
            if (dist < radius + closestDist) {
                closestDist = dist - radius;
                // Steer perpendicular to obstacle direction
                vec3 away = (position - obstacles[i]).normalized();
                avoidance = away * maxSpeed;
            }
        }

        return avoidance;
    }

    /**
     * Separation: Steer away from nearby agents
     */
    static vec3 separation(const vec3& position,
                          const std::vector<vec3>& neighbors,
                          float separationRadius = 3.0f) {
        vec3 steer = vec3::zero();
        int count = 0;

        for (const vec3& neighbor : neighbors) {
            float dist = (position - neighbor).length();
            if (dist > 0.001f && dist < separationRadius) {
                vec3 away = (position - neighbor).normalized();
                away = away / dist;  // Weight by distance
                steer += away;
                count++;
            }
        }

        if (count > 0) {
            steer = steer / static_cast<float>(count);
        }

        return steer;
    }

    /**
     * Cohesion: Steer toward center of nearby agents
     */
    static vec3 cohesion(const vec3& position, const vec3& velocity,
                        const std::vector<vec3>& neighbors,
                        float maxSpeed, float neighborRadius = 10.0f) {
        vec3 center = vec3::zero();
        int count = 0;

        for (const vec3& neighbor : neighbors) {
            float dist = (position - neighbor).length();
            if (dist < neighborRadius) {
                center += neighbor;
                count++;
            }
        }

        if (count > 0) {
            center = center / static_cast<float>(count);
            return seek(position, velocity, center, maxSpeed);
        }

        return vec3::zero();
    }

    /**
     * Alignment: Steer to match velocity of nearby agents
     */
    static vec3 alignment(const vec3& velocity,
                         const std::vector<vec3>& neighborVelocities,
                         float neighborRadius = 10.0f) {
        vec3 avgVelocity = vec3::zero();
        int count = 0;

        for (const vec3& vel : neighborVelocities) {
            avgVelocity += vel;
            count++;
        }

        if (count > 0) {
            avgVelocity = avgVelocity / static_cast<float>(count);
            return avgVelocity - velocity;
        }

        return vec3::zero();
    }
};

} // namespace CatEngine
