#ifndef ENGINE_FRUSTUM_HPP
#define ENGINE_FRUSTUM_HPP

#include "Vector.hpp"
#include "Matrix.hpp"
#include "AABB.hpp"
#include <array>

namespace Engine {

/**
 * Plane in 3D space
 * Defined by normal and distance from origin
 */
struct Plane {
    vec3 normal;
    float d; // distance from origin

    Plane() : normal(0.0f, 1.0f, 0.0f), d(0.0f) {}
    Plane(const vec3& normal, float distance) : normal(normal), d(distance) {}
    Plane(const vec3& normal, const vec3& point) : normal(normal), d(-normal.dot(point)) {}
    Plane(const vec3& p0, const vec3& p1, const vec3& p2) {
        vec3 v1 = p1 - p0;
        vec3 v2 = p2 - p0;
        normal = v1.cross(v2).normalized();
        d = -normal.dot(p0);
    }

    // Normalize plane equation
    void normalize() {
        float len = normal.length();
        if (len > Math::EPSILON) {
            normal = normal / len;
            d /= len;
        }
    }

    Plane normalized() const {
        Plane p = *this;
        p.normalize();
        return p;
    }

    // Signed distance from point to plane (positive = in front)
    float signedDistance(const vec3& point) const {
        return normal.dot(point) + d;
    }

    float distanceToPoint(const vec3& point) const {
        return std::abs(signedDistance(point));
    }

    // Project point onto plane
    vec3 project(const vec3& point) const {
        return point - normal * signedDistance(point);
    }

    // Closest point on plane to a point
    vec3 closestPoint(const vec3& point) const {
        return project(point);
    }

    // Ray-plane intersection
    bool intersects(const Ray& ray, float& t) const {
        float denom = normal.dot(ray.direction);
        if (std::abs(denom) < Math::EPSILON) {
            return false; // Ray parallel to plane
        }
        t = -(normal.dot(ray.origin) + d) / denom;
        return t >= 0.0f;
    }
};

/**
 * View Frustum for culling
 * Contains 6 planes: near, far, left, right, top, bottom
 */
class Frustum {
public:
    enum PlaneIndex {
        NEAR = 0,
        FAR = 1,
        LEFT = 2,
        RIGHT = 3,
        TOP = 4,
        BOTTOM = 5
    };

    std::array<Plane, 6> planes;

    Frustum() = default;

    // Extract frustum planes from view-projection matrix
    void extract(const mat4& viewProjection) {
        // Left plane
        planes[LEFT].normal = vec3(
            viewProjection[0][3] + viewProjection[0][0],
            viewProjection[1][3] + viewProjection[1][0],
            viewProjection[2][3] + viewProjection[2][0]
        );
        planes[LEFT].d = viewProjection[3][3] + viewProjection[3][0];
        planes[LEFT].normalize();

        // Right plane
        planes[RIGHT].normal = vec3(
            viewProjection[0][3] - viewProjection[0][0],
            viewProjection[1][3] - viewProjection[1][0],
            viewProjection[2][3] - viewProjection[2][0]
        );
        planes[RIGHT].d = viewProjection[3][3] - viewProjection[3][0];
        planes[RIGHT].normalize();

        // Bottom plane
        planes[BOTTOM].normal = vec3(
            viewProjection[0][3] + viewProjection[0][1],
            viewProjection[1][3] + viewProjection[1][1],
            viewProjection[2][3] + viewProjection[2][1]
        );
        planes[BOTTOM].d = viewProjection[3][3] + viewProjection[3][1];
        planes[BOTTOM].normalize();

        // Top plane
        planes[TOP].normal = vec3(
            viewProjection[0][3] - viewProjection[0][1],
            viewProjection[1][3] - viewProjection[1][1],
            viewProjection[2][3] - viewProjection[2][1]
        );
        planes[TOP].d = viewProjection[3][3] - viewProjection[3][1];
        planes[TOP].normalize();

        // Near plane
        planes[NEAR].normal = vec3(
            viewProjection[0][3] + viewProjection[0][2],
            viewProjection[1][3] + viewProjection[1][2],
            viewProjection[2][3] + viewProjection[2][2]
        );
        planes[NEAR].d = viewProjection[3][3] + viewProjection[3][2];
        planes[NEAR].normalize();

        // Far plane
        planes[FAR].normal = vec3(
            viewProjection[0][3] - viewProjection[0][2],
            viewProjection[1][3] - viewProjection[1][2],
            viewProjection[2][3] - viewProjection[2][2]
        );
        planes[FAR].d = viewProjection[3][3] - viewProjection[3][2];
        planes[FAR].normalize();
    }

    // Point containment test
    bool contains(const vec3& point) const {
        for (const auto& plane : planes) {
            if (plane.signedDistance(point) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // Sphere containment test (returns true if any part is inside)
    bool intersectsSphere(const vec3& center, float radius) const {
        for (const auto& plane : planes) {
            if (plane.signedDistance(center) < -radius) {
                return false; // Completely outside
            }
        }
        return true;
    }

    // Sphere containment test (returns true only if completely inside)
    bool containsSphere(const vec3& center, float radius) const {
        for (const auto& plane : planes) {
            if (plane.signedDistance(center) < radius) {
                return false;
            }
        }
        return true;
    }

    // AABB intersection test
    bool intersectsAABB(const AABB& aabb) const {
        for (const auto& plane : planes) {
            // Get the positive vertex (vertex in the direction of the plane normal)
            vec3 pVertex(
                plane.normal.x >= 0.0f ? aabb.max.x : aabb.min.x,
                plane.normal.y >= 0.0f ? aabb.max.y : aabb.min.y,
                plane.normal.z >= 0.0f ? aabb.max.z : aabb.min.z
            );

            if (plane.signedDistance(pVertex) < 0.0f) {
                return false; // Completely outside
            }
        }
        return true;
    }

    // AABB containment test (returns true only if completely inside)
    bool containsAABB(const AABB& aabb) const {
        for (const auto& plane : planes) {
            // Get the negative vertex (vertex opposite to the plane normal)
            vec3 nVertex(
                plane.normal.x >= 0.0f ? aabb.min.x : aabb.max.x,
                plane.normal.y >= 0.0f ? aabb.min.y : aabb.max.y,
                plane.normal.z >= 0.0f ? aabb.min.z : aabb.max.z
            );

            if (plane.signedDistance(nVertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // OBB intersection test (oriented bounding box via AABB + transform)
    bool intersectsOBB(const AABB& aabb, const mat4& transform) const {
        // Get AABB corners and transform them
        auto corners = aabb.corners();
        AABB transformedAABB;
        for (const auto& corner : corners) {
            transformedAABB.expand(transform.transformPoint(corner));
        }
        return intersectsAABB(transformedAABB);
    }

    // Get frustum corners in world space
    std::array<vec3, 8> getCorners(const mat4& invViewProjection) const {
        // NDC corners of frustum
        std::array<vec4, 8> ndcCorners = {
            vec4(-1.0f, -1.0f, -1.0f, 1.0f), // Near bottom-left
            vec4( 1.0f, -1.0f, -1.0f, 1.0f), // Near bottom-right
            vec4(-1.0f,  1.0f, -1.0f, 1.0f), // Near top-left
            vec4( 1.0f,  1.0f, -1.0f, 1.0f), // Near top-right
            vec4(-1.0f, -1.0f,  1.0f, 1.0f), // Far bottom-left
            vec4( 1.0f, -1.0f,  1.0f, 1.0f), // Far bottom-right
            vec4(-1.0f,  1.0f,  1.0f, 1.0f), // Far top-left
            vec4( 1.0f,  1.0f,  1.0f, 1.0f)  // Far top-right
        };

        std::array<vec3, 8> worldCorners;
        for (size_t i = 0; i < 8; i++) {
            vec4 worldPos = invViewProjection * ndcCorners[i];
            worldPos = worldPos / worldPos.w; // Perspective divide
            worldCorners[i] = worldPos.xyz();
        }

        return worldCorners;
    }

    // Get an AABB that encloses the frustum
    AABB getEnclosingAABB(const mat4& invViewProjection) const {
        auto corners = getCorners(invViewProjection);
        AABB aabb;
        for (const auto& corner : corners) {
            aabb.expand(corner);
        }
        return aabb;
    }

    // Access planes
    const Plane& operator[](size_t index) const { return planes[index]; }
    Plane& operator[](size_t index) { return planes[index]; }

    // Static factory
    static Frustum fromMatrix(const mat4& viewProjection) {
        Frustum frustum;
        frustum.extract(viewProjection);
        return frustum;
    }

    static Frustum fromMatrices(const mat4& view, const mat4& projection) {
        return fromMatrix(projection * view);
    }
};

inline std::ostream& operator<<(std::ostream& os, const Plane& plane) {
    return os << "Plane(normal: " << plane.normal << ", d: " << plane.d << ")";
}

} // namespace Engine

#endif // ENGINE_FRUSTUM_HPP
