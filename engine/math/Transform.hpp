#ifndef ENGINE_TRANSFORM_HPP
#define ENGINE_TRANSFORM_HPP

#include "Vector.hpp"
#include "Matrix.hpp"
#include "Quaternion.hpp"

namespace Engine {

/**
 * Transform class combining position, rotation, and scale
 * Represents a 3D transformation in space
 */
class Transform {
public:
    vec3 position;
    Quaternion rotation;
    vec3 scale;

    // Constructors
    Transform()
        : position(0.0f, 0.0f, 0.0f)
        , rotation(Quaternion::identity())
        , scale(1.0f, 1.0f, 1.0f)
    {}

    Transform(const vec3& position)
        : position(position)
        , rotation(Quaternion::identity())
        , scale(1.0f, 1.0f, 1.0f)
    {}

    Transform(const vec3& position, const Quaternion& rotation)
        : position(position)
        , rotation(rotation)
        , scale(1.0f, 1.0f, 1.0f)
    {}

    Transform(const vec3& position, const Quaternion& rotation, const vec3& scale)
        : position(position)
        , rotation(rotation)
        , scale(scale)
    {}

    // Convert to matrix
    mat4 toMatrix() const {
        mat4 translation = mat4::translate(position);
        mat4 rot = rotation.toMatrix();
        mat4 scl = mat4::scale(scale);
        return translation * rot * scl;
    }

    // Convert from matrix (decompose)
    static Transform fromMatrix(const mat4& m) {
        Transform t;

        // Extract translation
        t.position = vec3(m[3][0], m[3][1], m[3][2]);

        // Extract scale
        vec3 col0(m[0][0], m[0][1], m[0][2]);
        vec3 col1(m[1][0], m[1][1], m[1][2]);
        vec3 col2(m[2][0], m[2][1], m[2][2]);

        t.scale = vec3(col0.length(), col1.length(), col2.length());

        // Remove scale from rotation matrix
        mat4 rotMatrix = m;
        if (t.scale.x > Math::EPSILON) {
            rotMatrix[0][0] /= t.scale.x;
            rotMatrix[0][1] /= t.scale.x;
            rotMatrix[0][2] /= t.scale.x;
        }
        if (t.scale.y > Math::EPSILON) {
            rotMatrix[1][0] /= t.scale.y;
            rotMatrix[1][1] /= t.scale.y;
            rotMatrix[1][2] /= t.scale.y;
        }
        if (t.scale.z > Math::EPSILON) {
            rotMatrix[2][0] /= t.scale.z;
            rotMatrix[2][1] /= t.scale.z;
            rotMatrix[2][2] /= t.scale.z;
        }

        // Extract rotation
        t.rotation = Quaternion::fromMatrix(rotMatrix);

        return t;
    }

    // Transform a point
    vec3 transformPoint(const vec3& point) const {
        vec3 scaled = vec3(point.x * scale.x, point.y * scale.y, point.z * scale.z);
        vec3 rotated = rotation.rotate(scaled);
        return rotated + position;
    }

    // Transform a direction (no translation)
    vec3 transformDirection(const vec3& direction) const {
        vec3 scaled = vec3(direction.x * scale.x, direction.y * scale.y, direction.z * scale.z);
        return rotation.rotate(scaled);
    }

    // Transform a normal (inverse transpose for non-uniform scaling)
    vec3 transformNormal(const vec3& normal) const {
        vec3 invScale(
            scale.x > Math::EPSILON ? 1.0f / scale.x : 0.0f,
            scale.y > Math::EPSILON ? 1.0f / scale.y : 0.0f,
            scale.z > Math::EPSILON ? 1.0f / scale.z : 0.0f
        );
        vec3 scaled = vec3(normal.x * invScale.x, normal.y * invScale.y, normal.z * invScale.z);
        return rotation.rotate(scaled).normalized();
    }

    // Inverse transform
    Transform inverse() const {
        Quaternion invRotation = rotation.inverse();
        vec3 invScale(
            scale.x > Math::EPSILON ? 1.0f / scale.x : 0.0f,
            scale.y > Math::EPSILON ? 1.0f / scale.y : 0.0f,
            scale.z > Math::EPSILON ? 1.0f / scale.z : 0.0f
        );
        vec3 invPosition = invRotation.rotate(vec3(-position.x * invScale.x,
                                                     -position.y * invScale.y,
                                                     -position.z * invScale.z));
        return Transform(invPosition, invRotation, invScale);
    }

    // Combine transforms (this * other)
    Transform operator*(const Transform& other) const {
        Transform result;
        result.scale = vec3(scale.x * other.scale.x, scale.y * other.scale.y, scale.z * other.scale.z);
        result.rotation = rotation * other.rotation;
        result.position = transformPoint(other.position);
        return result;
    }

    Transform& operator*=(const Transform& other) {
        *this = *this * other;
        return *this;
    }

    // Interpolation
    static Transform lerp(const Transform& a, const Transform& b, float t) {
        return Transform(
            vec3::lerp(a.position, b.position, t),
            Quaternion::slerp(a.rotation, b.rotation, t),
            vec3::lerp(a.scale, b.scale, t)
        );
    }

    // Direction vectors
    vec3 forward() const {
        return rotation.rotate(vec3(0.0f, 0.0f, -1.0f));
    }

    vec3 backward() const {
        return rotation.rotate(vec3(0.0f, 0.0f, 1.0f));
    }

    vec3 up() const {
        return rotation.rotate(vec3(0.0f, 1.0f, 0.0f));
    }

    vec3 down() const {
        return rotation.rotate(vec3(0.0f, -1.0f, 0.0f));
    }

    vec3 right() const {
        return rotation.rotate(vec3(1.0f, 0.0f, 0.0f));
    }

    vec3 left() const {
        return rotation.rotate(vec3(-1.0f, 0.0f, 0.0f));
    }

    // Rotation helpers
    void rotate(const vec3& axis, float angle) {
        rotation = Quaternion(axis, angle) * rotation;
        rotation.normalize();
    }

    void rotateAround(const vec3& point, const vec3& axis, float angle) {
        Quaternion q(axis, angle);
        vec3 offset = position - point;
        position = point + q.rotate(offset);
        rotation = q * rotation;
        rotation.normalize();
    }

    void lookAt(const vec3& target, const vec3& up = vec3(0.0f, 1.0f, 0.0f)) {
        vec3 direction = (target - position).normalized();
        rotation = Quaternion::lookRotation(direction, up);
    }

    // Translation helpers
    void translate(const vec3& translation) {
        position += translation;
    }

    void translate(float x, float y, float z) {
        position += vec3(x, y, z);
    }

    // Scale helpers
    void setScale(float uniform) {
        scale = vec3(uniform, uniform, uniform);
    }

    void setScale(const vec3& newScale) {
        scale = newScale;
    }

    void scaleBy(float factor) {
        scale *= factor;
    }

    void scaleBy(const vec3& factors) {
        scale.x *= factors.x;
        scale.y *= factors.y;
        scale.z *= factors.z;
    }

    // Reset to identity
    void reset() {
        position = vec3(0.0f, 0.0f, 0.0f);
        rotation = Quaternion::identity();
        scale = vec3(1.0f, 1.0f, 1.0f);
    }

    // Static factory
    static Transform identity() {
        return Transform();
    }
};

inline std::ostream& operator<<(std::ostream& os, const Transform& t) {
    return os << "Transform(pos: " << t.position
              << ", rot: " << t.rotation
              << ", scale: " << t.scale << ")";
}

} // namespace Engine

#endif // ENGINE_TRANSFORM_HPP
