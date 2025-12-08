#ifndef ENGINE_QUATERNION_HPP
#define ENGINE_QUATERNION_HPP

#include "Vector.hpp"
#include "Matrix.hpp"
#include "Math.hpp"
#include <cmath>

namespace Engine {

/**
 * Quaternion for representing rotations
 * Stored as (x, y, z, w) where w is the scalar component
 */
struct alignas(16) Quaternion {
    union {
        struct { float x, y, z, w; };
        float data[4];
        vec4 v;
    };

    // Constructors
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Quaternion(const vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
    Quaternion(const vec3& axis, float angle) {
        float halfAngle = angle * 0.5f;
        float s = std::sin(halfAngle);
        vec3 normalizedAxis = axis.normalized();
        x = normalizedAxis.x * s;
        y = normalizedAxis.y * s;
        z = normalizedAxis.z * s;
        w = std::cos(halfAngle);
    }

    // Operators
    Quaternion operator*(const Quaternion& other) const {
        return Quaternion(
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y - x * other.z + y * other.w + z * other.x,
            w * other.z + x * other.y - y * other.x + z * other.w,
            w * other.w - x * other.x - y * other.y - z * other.z
        );
    }

    Quaternion operator+(const Quaternion& other) const {
        return Quaternion(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    Quaternion operator-(const Quaternion& other) const {
        return Quaternion(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    Quaternion operator*(float scalar) const {
        return Quaternion(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    Quaternion operator-() const {
        return Quaternion(-x, -y, -z, -w);
    }

    Quaternion& operator*=(const Quaternion& other) {
        *this = *this * other;
        return *this;
    }

    Quaternion& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar;
        return *this;
    }

    bool operator==(const Quaternion& other) const {
        return Math::approximately(x, other.x) &&
               Math::approximately(y, other.y) &&
               Math::approximately(z, other.z) &&
               Math::approximately(w, other.w);
    }

    bool operator!=(const Quaternion& other) const { return !(*this == other); }

    float& operator[](size_t i) { return data[i]; }
    const float& operator[](size_t i) const { return data[i]; }

    // Methods
    float length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    float lengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    Quaternion normalized() const {
        float len = length();
        return len > Math::EPSILON ? (*this * (1.0f / len)) : identity();
    }

    void normalize() {
        float len = length();
        if (len > Math::EPSILON) {
            float invLen = 1.0f / len;
            x *= invLen; y *= invLen; z *= invLen; w *= invLen;
        } else {
            *this = identity();
        }
    }

    Quaternion conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }

    Quaternion inverse() const {
        float lenSq = lengthSquared();
        if (lenSq < Math::EPSILON) return identity();
        return conjugate() * (1.0f / lenSq);
    }

    float dot(const Quaternion& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    // Rotate a vector
    vec3 rotate(const vec3& v) const {
        // v' = q * v * q^-1
        Quaternion qv(v.x, v.y, v.z, 0.0f);
        Quaternion result = (*this) * qv * conjugate();
        return vec3(result.x, result.y, result.z);
    }

    // Convert to rotation matrix
    mat4 toMatrix() const {
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        return mat4(
            vec4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f),
            vec4(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f),
            vec4(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    mat3 toMatrix3() const {
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        return mat3(
            vec3(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)),
            vec3(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)),
            vec3(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy))
        );
    }

    // Convert to Euler angles (in radians, XYZ order)
    vec3 toEuler() const {
        vec3 euler;

        // Roll (x-axis rotation)
        float sinr_cosp = 2.0f * (w * x + y * z);
        float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        euler.x = std::atan2(sinr_cosp, cosr_cosp);

        // Pitch (y-axis rotation)
        float sinp = 2.0f * (w * y - z * x);
        if (std::abs(sinp) >= 1.0f)
            euler.y = std::copysign(Math::HALF_PI, sinp); // Use 90 degrees if out of range
        else
            euler.y = std::asin(sinp);

        // Yaw (z-axis rotation)
        float siny_cosp = 2.0f * (w * z + x * y);
        float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        euler.z = std::atan2(siny_cosp, cosy_cosp);

        return euler;
    }

    // Get axis and angle
    void toAxisAngle(vec3& axis, float& angle) const {
        Quaternion q = normalized();
        angle = 2.0f * std::acos(q.w);
        float s = std::sqrt(1.0f - q.w * q.w);
        if (s < Math::EPSILON) {
            axis = vec3(1.0f, 0.0f, 0.0f);
        } else {
            axis = vec3(q.x / s, q.y / s, q.z / s);
        }
    }

    // Static factory methods
    static Quaternion identity() {
        return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }

    static Quaternion fromEuler(const vec3& euler) {
        return fromEuler(euler.x, euler.y, euler.z);
    }

    static Quaternion fromEuler(float pitch, float yaw, float roll) {
        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);

        return Quaternion(
            sr * cp * cy - cr * sp * sy,  // x
            cr * sp * cy + sr * cp * sy,  // y
            cr * cp * sy - sr * sp * cy,  // z
            cr * cp * cy + sr * sp * sy   // w
        );
    }

    static Quaternion fromAxisAngle(const vec3& axis, float angle) {
        return Quaternion(axis, angle);
    }

    static Quaternion fromMatrix(const mat4& m) {
        float trace = m[0][0] + m[1][1] + m[2][2];
        Quaternion q;

        if (trace > 0.0f) {
            float s = 0.5f / std::sqrt(trace + 1.0f);
            q.w = 0.25f / s;
            q.x = (m[1][2] - m[2][1]) * s;
            q.y = (m[2][0] - m[0][2]) * s;
            q.z = (m[0][1] - m[1][0]) * s;
        } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            float s = 2.0f * std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]);
            q.w = (m[1][2] - m[2][1]) / s;
            q.x = 0.25f * s;
            q.y = (m[1][0] + m[0][1]) / s;
            q.z = (m[2][0] + m[0][2]) / s;
        } else if (m[1][1] > m[2][2]) {
            float s = 2.0f * std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
            q.w = (m[2][0] - m[0][2]) / s;
            q.x = (m[1][0] + m[0][1]) / s;
            q.y = 0.25f * s;
            q.z = (m[2][1] + m[1][2]) / s;
        } else {
            float s = 2.0f * std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]);
            q.w = (m[0][1] - m[1][0]) / s;
            q.x = (m[2][0] + m[0][2]) / s;
            q.y = (m[2][1] + m[1][2]) / s;
            q.z = 0.25f * s;
        }

        return q.normalized();
    }

    // Look rotation (from direction and up vector)
    static Quaternion lookRotation(const vec3& forward, const vec3& up = vec3(0.0f, 1.0f, 0.0f)) {
        vec3 f = forward.normalized();
        vec3 r = up.cross(f).normalized();
        vec3 u = f.cross(r);

        mat4 m(1.0f);
        m[0] = vec4(r, 0.0f);
        m[1] = vec4(u, 0.0f);
        m[2] = vec4(f, 0.0f);

        return fromMatrix(m);
    }

    // Interpolation
    static Quaternion lerp(const Quaternion& a, const Quaternion& b, float t) {
        return (a * (1.0f - t) + b * t).normalized();
    }

    static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) {
        Quaternion qa = a.normalized();
        Quaternion qb = b.normalized();

        float dot = qa.dot(qb);

        // If the dot product is negative, slerp won't take the shorter path
        // Fix by reversing one quaternion
        if (dot < 0.0f) {
            qb = -qb;
            dot = -dot;
        }

        // If quaternions are very close, use linear interpolation
        if (dot > 0.9995f) {
            return lerp(qa, qb, t);
        }

        // Clamp dot to prevent numerical issues
        dot = Math::clamp(dot, -1.0f, 1.0f);

        float theta = std::acos(dot);
        float sinTheta = std::sin(theta);

        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;

        return qa * wa + qb * wb;
    }

    static Quaternion nlerp(const Quaternion& a, const Quaternion& b, float t) {
        // Faster but less accurate than slerp
        return lerp(a, b, t);
    }

    // Rotation between two vectors
    static Quaternion fromToRotation(const vec3& from, const vec3& to) {
        vec3 f = from.normalized();
        vec3 t = to.normalized();

        float dot = f.dot(t);

        // Vectors are parallel
        if (dot >= 0.999999f) {
            return identity();
        }

        // Vectors are opposite
        if (dot <= -0.999999f) {
            vec3 axis = vec3(1.0f, 0.0f, 0.0f).cross(f);
            if (axis.lengthSquared() < Math::EPSILON) {
                axis = vec3(0.0f, 1.0f, 0.0f).cross(f);
            }
            return Quaternion(axis.normalized(), Math::PI);
        }

        vec3 axis = f.cross(t);
        float s = std::sqrt((1.0f + dot) * 2.0f);
        float invS = 1.0f / s;

        return Quaternion(
            axis.x * invS,
            axis.y * invS,
            axis.z * invS,
            s * 0.5f
        ).normalized();
    }

    // Angle between quaternions
    static float angle(const Quaternion& a, const Quaternion& b) {
        float dot = Math::clamp(a.normalized().dot(b.normalized()), -1.0f, 1.0f);
        return std::acos(std::abs(dot)) * 2.0f;
    }
};

inline Quaternion operator*(float scalar, const Quaternion& q) {
    return q * scalar;
}

inline std::ostream& operator<<(std::ostream& os, const Quaternion& q) {
    return os << "Quaternion(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
}

} // namespace Engine

#endif // ENGINE_QUATERNION_HPP
