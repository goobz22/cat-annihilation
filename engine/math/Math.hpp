#ifndef ENGINE_MATH_HPP
#define ENGINE_MATH_HPP

#include <cmath>
#include <algorithm>
#include <limits>

namespace Engine {
namespace Math {

// Constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float INV_PI = 0.31830988618379067154f;
constexpr float DEG_TO_RAD = 0.01745329251994329577f;
constexpr float RAD_TO_DEG = 57.2957795130823208768f;
constexpr float EPSILON = 1e-6f;
constexpr float INFINITY_F = std::numeric_limits<float>::infinity();

// Utility functions
template<typename T>
inline T clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}

template<typename T>
inline T lerp(T a, T b, float t) {
    return a + (b - a) * t;
}

template<typename T>
inline T smoothstep(T edge0, T edge1, T x) {
    T t = clamp((x - edge0) / (edge1 - edge0), T(0), T(1));
    return t * t * (T(3) - T(2) * t);
}

inline float radians(float degrees) {
    return degrees * DEG_TO_RAD;
}

inline float degrees(float radians) {
    return radians * RAD_TO_DEG;
}

inline bool approximately(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

inline float sign(float x) {
    return (x > 0.0f) - (x < 0.0f);
}

inline float fract(float x) {
    return x - std::floor(x);
}

inline float mod(float x, float y) {
    return x - y * std::floor(x / y);
}

template<typename T>
inline T min3(T a, T b, T c) {
    return std::min(a, std::min(b, c));
}

template<typename T>
inline T max3(T a, T b, T c) {
    return std::max(a, std::max(b, c));
}

// Fast inverse square root (Quake III algorithm)
inline float fastInvSqrt(float x) {
    float halfx = 0.5f * x;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - halfx * x * x);
    return x;
}

// Safe division
inline float safeDivide(float numerator, float denominator, float fallback = 0.0f) {
    return std::abs(denominator) > EPSILON ? numerator / denominator : fallback;
}

} // namespace Math
} // namespace Engine

#endif // ENGINE_MATH_HPP
