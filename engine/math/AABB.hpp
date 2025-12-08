#ifndef ENGINE_AABB_HPP
#define ENGINE_AABB_HPP

#include "Vector.hpp"
#include "Matrix.hpp"
#include "Ray.hpp"
#include <algorithm>
#include <array>

namespace Engine {

// Forward declaration
struct Ray;

/**
 * Axis-Aligned Bounding Box
 * Defined by minimum and maximum corners
 */
struct AABB {
    vec3 min;
    vec3 max;

    // Constructors
    AABB() : min(Math::INFINITY_F), max(-Math::INFINITY_F) {}

    AABB(const vec3& min, const vec3& max) : min(min), max(max) {}

    AABB(const vec3& center, float radius)
        : min(center - vec3(radius))
        , max(center + vec3(radius))
    {}

    // Properties
    vec3 center() const {
        return (min + max) * 0.5f;
    }

    vec3 extents() const {
        return (max - min) * 0.5f;
    }

    vec3 size() const {
        return max - min;
    }

    float volume() const {
        vec3 s = size();
        return s.x * s.y * s.z;
    }

    float surfaceArea() const {
        vec3 s = size();
        return 2.0f * (s.x * s.y + s.y * s.z + s.z * s.x);
    }

    // Get all 8 corners
    std::array<vec3, 8> corners() const {
        return {
            vec3(min.x, min.y, min.z),
            vec3(max.x, min.y, min.z),
            vec3(min.x, max.y, min.z),
            vec3(max.x, max.y, min.z),
            vec3(min.x, min.y, max.z),
            vec3(max.x, min.y, max.z),
            vec3(min.x, max.y, max.z),
            vec3(max.x, max.y, max.z)
        };
    }

    // Expand to include point
    void expand(const vec3& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    // Expand to include another AABB
    void expand(const AABB& other) {
        min.x = std::min(min.x, other.min.x);
        min.y = std::min(min.y, other.min.y);
        min.z = std::min(min.z, other.min.z);
        max.x = std::max(max.x, other.max.x);
        max.y = std::max(max.y, other.max.y);
        max.z = std::max(max.z, other.max.z);
    }

    // Grow by amount in all directions
    void grow(float amount) {
        min -= vec3(amount);
        max += vec3(amount);
    }

    void grow(const vec3& amount) {
        min -= amount;
        max += amount;
    }

    // Check if valid (min <= max)
    bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    // Reset to empty
    void reset() {
        min = vec3(Math::INFINITY_F);
        max = vec3(-Math::INFINITY_F);
    }

    // Point containment
    bool contains(const vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    // AABB containment
    bool contains(const AABB& other) const {
        return other.min.x >= min.x && other.max.x <= max.x &&
               other.min.y >= min.y && other.max.y <= max.y &&
               other.min.z >= min.z && other.max.z <= max.z;
    }

    // AABB-AABB intersection test
    bool intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    // Get intersection with another AABB
    AABB intersection(const AABB& other) const {
        if (!intersects(other)) return AABB();

        return AABB(
            vec3(
                std::max(min.x, other.min.x),
                std::max(min.y, other.min.y),
                std::max(min.z, other.min.z)
            ),
            vec3(
                std::min(max.x, other.max.x),
                std::min(max.y, other.max.y),
                std::min(max.z, other.max.z)
            )
        );
    }

    // Union with another AABB
    AABB unionWith(const AABB& other) const {
        return AABB(
            vec3(
                std::min(min.x, other.min.x),
                std::min(min.y, other.min.y),
                std::min(min.z, other.min.z)
            ),
            vec3(
                std::max(max.x, other.max.x),
                std::max(max.y, other.max.y),
                std::max(max.z, other.max.z)
            )
        );
    }

    // Ray-AABB intersection test (returns true if hit, outputs distance)
    bool intersects(const Ray& ray, float& tMin, float& tMax) const {
        return ray.intersectsAABB(min, max, tMin, tMax);
    }

    // Ray-AABB fast intersection test (no distance output)
    bool intersects(const Ray& ray) const {
        float tMin, tMax;
        return ray.intersectsAABB(min, max, tMin, tMax);
    }

    // Closest point on AABB to a point
    vec3 closestPoint(const vec3& point) const {
        return vec3(
            Math::clamp(point.x, min.x, max.x),
            Math::clamp(point.y, min.y, max.y),
            Math::clamp(point.z, min.z, max.z)
        );
    }

    // Distance from point to AABB
    float distance(const vec3& point) const {
        vec3 closest = closestPoint(point);
        return (point - closest).length();
    }

    float distanceSquared(const vec3& point) const {
        vec3 closest = closestPoint(point);
        return (point - closest).lengthSquared();
    }

    // Transform AABB by matrix
    AABB transformed(const mat4& transform) const {
        auto c = corners();
        AABB result;
        for (const auto& corner : c) {
            result.expand(transform.transformPoint(corner));
        }
        return result;
    }

    // Sphere containment
    bool containsSphere(const vec3& center, float radius) const {
        vec3 closest = closestPoint(center);
        float distSq = (center - closest).lengthSquared();
        return distSq <= radius * radius;
    }

    bool intersectsSphere(const vec3& center, float radius) const {
        float distSq = distanceSquared(center);
        return distSq <= radius * radius;
    }

    // Operators
    bool operator==(const AABB& other) const {
        return min == other.min && max == other.max;
    }

    bool operator!=(const AABB& other) const {
        return !(*this == other);
    }

    // Static helpers
    static AABB fromCenterExtents(const vec3& center, const vec3& extents) {
        return AABB(center - extents, center + extents);
    }

    static AABB fromPoints(const vec3* points, size_t count) {
        if (count == 0) return AABB();

        AABB result;
        for (size_t i = 0; i < count; i++) {
            result.expand(points[i]);
        }
        return result;
    }

    static AABB empty() {
        return AABB();
    }

    static AABB infinite() {
        return AABB(
            vec3(-Math::INFINITY_F),
            vec3(Math::INFINITY_F)
        );
    }
};

inline std::ostream& operator<<(std::ostream& os, const AABB& aabb) {
    return os << "AABB(min: " << aabb.min << ", max: " << aabb.max << ")";
}

} // namespace Engine

#endif // ENGINE_AABB_HPP
