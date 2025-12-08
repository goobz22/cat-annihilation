#ifndef ENGINE_RAY_HPP
#define ENGINE_RAY_HPP

#include "Vector.hpp"
#include "Math.hpp"

namespace Engine {

// Forward declaration
struct AABB;

/**
 * Ray for raycasting
 * Defined by origin and direction
 */
struct Ray {
    vec3 origin;
    vec3 direction; // Should be normalized

    // Constructors
    Ray() : origin(0.0f), direction(0.0f, 0.0f, -1.0f) {}

    Ray(const vec3& origin, const vec3& direction)
        : origin(origin)
        , direction(direction.normalized())
    {}

    // Get point along ray at parameter t
    vec3 at(float t) const {
        return origin + direction * t;
    }

    vec3 pointAt(float distance) const {
        return at(distance);
    }

    // Get closest point on ray to a point
    vec3 closestPoint(const vec3& point) const {
        vec3 toPoint = point - origin;
        float t = toPoint.dot(direction);
        return at(std::max(0.0f, t));
    }

    // Distance from ray to point
    float distance(const vec3& point) const {
        vec3 closest = closestPoint(point);
        return (point - closest).length();
    }

    float distanceSquared(const vec3& point) const {
        vec3 closest = closestPoint(point);
        return (point - closest).lengthSquared();
    }

    // Sphere intersection
    bool intersectsSphere(const vec3& center, float radius, float& t) const {
        vec3 oc = origin - center;
        float a = direction.dot(direction);
        float b = 2.0f * oc.dot(direction);
        float c = oc.dot(oc) - radius * radius;
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant < 0.0f) {
            return false;
        }

        float sqrtD = std::sqrt(discriminant);
        float t0 = (-b - sqrtD) / (2.0f * a);
        float t1 = (-b + sqrtD) / (2.0f * a);

        if (t0 > 0.0f) {
            t = t0;
            return true;
        } else if (t1 > 0.0f) {
            t = t1;
            return true;
        }

        return false;
    }

    bool intersectsSphere(const vec3& center, float radius) const {
        float t;
        return intersectsSphere(center, radius, t);
    }

    // Plane intersection
    bool intersectsPlane(const vec3& planeNormal, float planeDistance, float& t) const {
        float denom = direction.dot(planeNormal);

        if (std::abs(denom) < Math::EPSILON) {
            return false; // Ray parallel to plane
        }

        t = -(origin.dot(planeNormal) + planeDistance) / denom;
        return t >= 0.0f;
    }

    bool intersectsPlane(const vec3& planePoint, const vec3& planeNormal, float& t) const {
        float denom = direction.dot(planeNormal);

        if (std::abs(denom) < Math::EPSILON) {
            return false; // Ray parallel to plane
        }

        t = (planePoint - origin).dot(planeNormal) / denom;
        return t >= 0.0f;
    }

    // Triangle intersection (Möller-Trumbore algorithm)
    bool intersectsTriangle(const vec3& v0, const vec3& v1, const vec3& v2,
                           float& t, vec3& barycentric) const {
        vec3 edge1 = v1 - v0;
        vec3 edge2 = v2 - v0;

        vec3 h = direction.cross(edge2);
        float a = edge1.dot(h);

        if (std::abs(a) < Math::EPSILON) {
            return false; // Ray parallel to triangle
        }

        float f = 1.0f / a;
        vec3 s = origin - v0;
        float u = f * s.dot(h);

        if (u < 0.0f || u > 1.0f) {
            return false;
        }

        vec3 q = s.cross(edge1);
        float v = f * direction.dot(q);

        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }

        t = f * edge2.dot(q);

        if (t > Math::EPSILON) {
            barycentric = vec3(1.0f - u - v, u, v);
            return true;
        }

        return false;
    }

    bool intersectsTriangle(const vec3& v0, const vec3& v1, const vec3& v2, float& t) const {
        vec3 barycentric;
        return intersectsTriangle(v0, v1, v2, t, barycentric);
    }

    bool intersectsTriangle(const vec3& v0, const vec3& v1, const vec3& v2) const {
        float t;
        return intersectsTriangle(v0, v1, v2, t);
    }

    // AABB intersection (defined here to avoid circular dependency)
    bool intersectsAABB(const vec3& aabbMin, const vec3& aabbMax, float& tMin, float& tMax) const {
        vec3 invDir(
            std::abs(direction.x) > Math::EPSILON ? 1.0f / direction.x : Math::INFINITY_F,
            std::abs(direction.y) > Math::EPSILON ? 1.0f / direction.y : Math::INFINITY_F,
            std::abs(direction.z) > Math::EPSILON ? 1.0f / direction.z : Math::INFINITY_F
        );

        vec3 t0 = (aabbMin - origin) * invDir;
        vec3 t1 = (aabbMax - origin) * invDir;

        vec3 tNear = vec3(
            std::min(t0.x, t1.x),
            std::min(t0.y, t1.y),
            std::min(t0.z, t1.z)
        );

        vec3 tFar = vec3(
            std::max(t0.x, t1.x),
            std::max(t0.y, t1.y),
            std::max(t0.z, t1.z)
        );

        tMin = std::max(tNear.x, std::max(tNear.y, tNear.z));
        tMax = std::min(tFar.x, std::min(tFar.y, tFar.z));

        return tMax >= tMin && tMax >= 0.0f;
    }

    bool intersectsAABB(const vec3& aabbMin, const vec3& aabbMax) const {
        float tMin, tMax;
        return intersectsAABB(aabbMin, aabbMax, tMin, tMax);
    }

    // Transform ray
    Ray transformed(const mat4& transform) const {
        vec3 newOrigin = transform.transformPoint(origin);
        vec3 newDirection = transform.transformVector(direction);
        return Ray(newOrigin, newDirection);
    }

    // Operators
    bool operator==(const Ray& other) const {
        return origin == other.origin && direction == other.direction;
    }

    bool operator!=(const Ray& other) const {
        return !(*this == other);
    }

    // Static helpers
    static Ray fromPoints(const vec3& start, const vec3& end) {
        return Ray(start, (end - start).normalized());
    }

    static Ray fromScreenPoint(const vec2& screenPos, const vec2& screenSize,
                               const mat4& viewProjection) {
        // Convert screen coordinates to NDC
        vec2 ndc(
            (2.0f * screenPos.x) / screenSize.x - 1.0f,
            1.0f - (2.0f * screenPos.y) / screenSize.y
        );

        // Unproject to world space
        mat4 invVP = viewProjection.inverse();
        vec4 rayClip(ndc.x, ndc.y, -1.0f, 1.0f);
        vec4 rayEye = invVP * rayClip;

        if (std::abs(rayEye.w) > Math::EPSILON) {
            rayEye = rayEye / rayEye.w;
        }

        vec3 origin = rayEye.xyz();

        rayClip = vec4(ndc.x, ndc.y, 1.0f, 1.0f);
        rayEye = invVP * rayClip;

        if (std::abs(rayEye.w) > Math::EPSILON) {
            rayEye = rayEye / rayEye.w;
        }

        vec3 end = rayEye.xyz();
        vec3 direction = (end - origin).normalized();

        return Ray(origin, direction);
    }
};

inline std::ostream& operator<<(std::ostream& os, const Ray& ray) {
    return os << "Ray(origin: " << ray.origin << ", direction: " << ray.direction << ")";
}

} // namespace Engine

#endif // ENGINE_RAY_HPP
