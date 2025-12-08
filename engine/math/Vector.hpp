#ifndef ENGINE_VECTOR_HPP
#define ENGINE_VECTOR_HPP

#include "Math.hpp"
#include <xmmintrin.h> // SSE
#include <smmintrin.h> // SSE4.1
#include <cmath>
#include <iostream>

namespace Engine {

// ============================================================================
// vec2
// ============================================================================
struct vec2 {
    float x, y;

    vec2() : x(0.0f), y(0.0f) {}
    vec2(float scalar) : x(scalar), y(scalar) {}
    vec2(float x, float y) : x(x), y(y) {}

    // Operators
    vec2 operator+(const vec2& other) const { return vec2(x + other.x, y + other.y); }
    vec2 operator-(const vec2& other) const { return vec2(x - other.x, y - other.y); }
    vec2 operator*(float scalar) const { return vec2(x * scalar, y * scalar); }
    vec2 operator/(float scalar) const { return vec2(x / scalar, y / scalar); }
    vec2 operator-() const { return vec2(-x, -y); }

    vec2& operator+=(const vec2& other) { x += other.x; y += other.y; return *this; }
    vec2& operator-=(const vec2& other) { x -= other.x; y -= other.y; return *this; }
    vec2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
    vec2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }

    bool operator==(const vec2& other) const {
        return Math::approximately(x, other.x) && Math::approximately(y, other.y);
    }
    bool operator!=(const vec2& other) const { return !(*this == other); }

    float& operator[](size_t i) { return (&x)[i]; }
    const float& operator[](size_t i) const { return (&x)[i]; }

    // Methods
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }

    vec2 normalized() const {
        float len = length();
        return len > Math::EPSILON ? *this / len : vec2(0.0f);
    }

    void normalize() {
        float len = length();
        if (len > Math::EPSILON) {
            x /= len;
            y /= len;
        }
    }

    float dot(const vec2& other) const { return x * other.x + y * other.y; }
    float cross(const vec2& other) const { return x * other.y - y * other.x; }

    vec2 perpendicular() const { return vec2(-y, x); }

    static vec2 lerp(const vec2& a, const vec2& b, float t) {
        return a + (b - a) * t;
    }
};

inline vec2 operator*(float scalar, const vec2& v) { return v * scalar; }

// ============================================================================
// vec3 (SIMD optimized where beneficial)
// ============================================================================
struct alignas(16) vec3 {
    union {
        struct { float x, y, z; };
        float data[3];
    };
    float _padding; // For alignment

    vec3() : x(0.0f), y(0.0f), z(0.0f), _padding(0.0f) {}
    vec3(float scalar) : x(scalar), y(scalar), z(scalar), _padding(0.0f) {}
    vec3(float x, float y, float z) : x(x), y(y), z(z), _padding(0.0f) {}
    vec3(const vec2& v, float z) : x(v.x), y(v.y), z(z), _padding(0.0f) {}

    // SIMD-accelerated operations
    vec3 operator+(const vec3& other) const {
        __m128 a = _mm_load_ps(&x);
        __m128 b = _mm_load_ps(&other.x);
        __m128 result = _mm_add_ps(a, b);
        vec3 ret;
        _mm_store_ps(&ret.x, result);
        return ret;
    }

    vec3 operator-(const vec3& other) const {
        __m128 a = _mm_load_ps(&x);
        __m128 b = _mm_load_ps(&other.x);
        __m128 result = _mm_sub_ps(a, b);
        vec3 ret;
        _mm_store_ps(&ret.x, result);
        return ret;
    }

    vec3 operator*(float scalar) const {
        __m128 a = _mm_load_ps(&x);
        __m128 s = _mm_set1_ps(scalar);
        __m128 result = _mm_mul_ps(a, s);
        vec3 ret;
        _mm_store_ps(&ret.x, result);
        return ret;
    }

    vec3 operator/(float scalar) const {
        __m128 a = _mm_load_ps(&x);
        __m128 s = _mm_set1_ps(scalar);
        __m128 result = _mm_div_ps(a, s);
        vec3 ret;
        _mm_store_ps(&ret.x, result);
        return ret;
    }

    // Component-wise multiplication
    vec3 operator*(const vec3& other) const {
        return vec3(x * other.x, y * other.y, z * other.z);
    }

    vec3 operator-() const { return vec3(-x, -y, -z); }

    vec3& operator+=(const vec3& other) {
        __m128 a = _mm_load_ps(&x);
        __m128 b = _mm_load_ps(&other.x);
        __m128 result = _mm_add_ps(a, b);
        _mm_store_ps(&x, result);
        return *this;
    }

    vec3& operator-=(const vec3& other) {
        __m128 a = _mm_load_ps(&x);
        __m128 b = _mm_load_ps(&other.x);
        __m128 result = _mm_sub_ps(a, b);
        _mm_store_ps(&x, result);
        return *this;
    }

    vec3& operator*=(float scalar) {
        __m128 a = _mm_load_ps(&x);
        __m128 s = _mm_set1_ps(scalar);
        __m128 result = _mm_mul_ps(a, s);
        _mm_store_ps(&x, result);
        return *this;
    }

    vec3& operator/=(float scalar) {
        __m128 a = _mm_load_ps(&x);
        __m128 s = _mm_set1_ps(scalar);
        __m128 result = _mm_div_ps(a, s);
        _mm_store_ps(&x, result);
        return *this;
    }

    bool operator==(const vec3& other) const {
        return Math::approximately(x, other.x) &&
               Math::approximately(y, other.y) &&
               Math::approximately(z, other.z);
    }
    bool operator!=(const vec3& other) const { return !(*this == other); }

    float& operator[](size_t i) { return data[i]; }
    const float& operator[](size_t i) const { return data[i]; }

    // Methods
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }

    vec3 normalized() const {
        float len = length();
        return len > Math::EPSILON ? *this / len : vec3(0.0f);
    }

    void normalize() {
        float len = length();
        if (len > Math::EPSILON) {
            *this /= len;
        }
    }

    float dot(const vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    vec3 cross(const vec3& other) const {
        return vec3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    vec3 reflect(const vec3& normal) const {
        return *this - normal * (2.0f * dot(normal));
    }

    vec3 refract(const vec3& normal, float eta) const {
        float dotNI = dot(normal);
        float k = 1.0f - eta * eta * (1.0f - dotNI * dotNI);
        if (k < 0.0f) return vec3(0.0f);
        return *this * eta - normal * (eta * dotNI + std::sqrt(k));
    }

    static vec3 lerp(const vec3& a, const vec3& b, float t) {
        return a + (b - a) * t;
    }

    // Common vectors
    static const vec3 zero() { return vec3(0.0f, 0.0f, 0.0f); }
    static const vec3 one() { return vec3(1.0f, 1.0f, 1.0f); }
    static const vec3 up() { return vec3(0.0f, 1.0f, 0.0f); }
    static const vec3 down() { return vec3(0.0f, -1.0f, 0.0f); }
    static const vec3 right() { return vec3(1.0f, 0.0f, 0.0f); }
    static const vec3 left() { return vec3(-1.0f, 0.0f, 0.0f); }
    static const vec3 forward() { return vec3(0.0f, 0.0f, -1.0f); }
    static const vec3 back() { return vec3(0.0f, 0.0f, 1.0f); }
};

inline vec3 operator*(float scalar, const vec3& v) { return v * scalar; }

// ============================================================================
// vec4 (SIMD optimized)
// ============================================================================
struct alignas(16) vec4 {
    union {
        struct { float x, y, z, w; };
        float data[4];
        __m128 simd;
    };

    vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    vec4(const vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
    vec4(__m128 s) : simd(s) {}

    // SIMD-accelerated operations
    vec4 operator+(const vec4& other) const {
        return vec4(_mm_add_ps(simd, other.simd));
    }

    vec4 operator-(const vec4& other) const {
        return vec4(_mm_sub_ps(simd, other.simd));
    }

    vec4 operator*(float scalar) const {
        return vec4(_mm_mul_ps(simd, _mm_set1_ps(scalar)));
    }

    vec4 operator/(float scalar) const {
        return vec4(_mm_div_ps(simd, _mm_set1_ps(scalar)));
    }

    vec4 operator-() const {
        return vec4(_mm_sub_ps(_mm_setzero_ps(), simd));
    }

    vec4& operator+=(const vec4& other) {
        simd = _mm_add_ps(simd, other.simd);
        return *this;
    }

    vec4& operator-=(const vec4& other) {
        simd = _mm_sub_ps(simd, other.simd);
        return *this;
    }

    vec4& operator*=(float scalar) {
        simd = _mm_mul_ps(simd, _mm_set1_ps(scalar));
        return *this;
    }

    vec4& operator/=(float scalar) {
        simd = _mm_div_ps(simd, _mm_set1_ps(scalar));
        return *this;
    }

    bool operator==(const vec4& other) const {
        return Math::approximately(x, other.x) &&
               Math::approximately(y, other.y) &&
               Math::approximately(z, other.z) &&
               Math::approximately(w, other.w);
    }
    bool operator!=(const vec4& other) const { return !(*this == other); }

    float& operator[](size_t i) { return data[i]; }
    const float& operator[](size_t i) const { return data[i]; }

    // Methods
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float lengthSquared() const { return x * x + y * y + z * z + w * w; }

    vec4 normalized() const {
        float len = length();
        return len > Math::EPSILON ? *this / len : vec4(0.0f);
    }

    void normalize() {
        float len = length();
        if (len > Math::EPSILON) {
            *this /= len;
        }
    }

    float dot(const vec4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    vec3 xyz() const { return vec3(x, y, z); }

    static vec4 lerp(const vec4& a, const vec4& b, float t) {
        return a + (b - a) * t;
    }
};

inline vec4 operator*(float scalar, const vec4& v) { return v * scalar; }

// Stream operators for debugging
inline std::ostream& operator<<(std::ostream& os, const vec2& v) {
    return os << "vec2(" << v.x << ", " << v.y << ")";
}

inline std::ostream& operator<<(std::ostream& os, const vec3& v) {
    return os << "vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
}

inline std::ostream& operator<<(std::ostream& os, const vec4& v) {
    return os << "vec4(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
}

} // namespace Engine

#endif // ENGINE_VECTOR_HPP
