#ifndef ENGINE_MATRIX_HPP
#define ENGINE_MATRIX_HPP

#include "Vector.hpp"
#include "Math.hpp"
#include <cstring>

namespace Engine {

// ============================================================================
// mat3 (3x3 matrix, column-major)
// ============================================================================
struct mat3 {
    vec3 columns[3];

    mat3() {
        columns[0] = vec3(1.0f, 0.0f, 0.0f);
        columns[1] = vec3(0.0f, 1.0f, 0.0f);
        columns[2] = vec3(0.0f, 0.0f, 1.0f);
    }

    mat3(float diagonal) {
        columns[0] = vec3(diagonal, 0.0f, 0.0f);
        columns[1] = vec3(0.0f, diagonal, 0.0f);
        columns[2] = vec3(0.0f, 0.0f, diagonal);
    }

    mat3(const vec3& c0, const vec3& c1, const vec3& c2) {
        columns[0] = c0;
        columns[1] = c1;
        columns[2] = c2;
    }

    vec3& operator[](size_t i) { return columns[i]; }
    const vec3& operator[](size_t i) const { return columns[i]; }

    mat3 operator*(const mat3& other) const {
        mat3 result(0.0f);
        for (int col = 0; col < 3; col++) {
            for (int row = 0; row < 3; row++) {
                result[col][row] =
                    columns[0][row] * other[col][0] +
                    columns[1][row] * other[col][1] +
                    columns[2][row] * other[col][2];
            }
        }
        return result;
    }

    vec3 operator*(const vec3& v) const {
        return vec3(
            columns[0].x * v.x + columns[1].x * v.y + columns[2].x * v.z,
            columns[0].y * v.x + columns[1].y * v.y + columns[2].y * v.z,
            columns[0].z * v.x + columns[1].z * v.y + columns[2].z * v.z
        );
    }

    mat3 transposed() const {
        return mat3(
            vec3(columns[0].x, columns[1].x, columns[2].x),
            vec3(columns[0].y, columns[1].y, columns[2].y),
            vec3(columns[0].z, columns[1].z, columns[2].z)
        );
    }

    float determinant() const {
        return columns[0].x * (columns[1].y * columns[2].z - columns[2].y * columns[1].z) -
               columns[1].x * (columns[0].y * columns[2].z - columns[2].y * columns[0].z) +
               columns[2].x * (columns[0].y * columns[1].z - columns[1].y * columns[0].z);
    }

    mat3 inverse() const {
        float det = determinant();
        if (std::abs(det) < Math::EPSILON) return mat3(1.0f);

        float invDet = 1.0f / det;
        mat3 result;

        result[0][0] = (columns[1].y * columns[2].z - columns[2].y * columns[1].z) * invDet;
        result[0][1] = (columns[2].y * columns[0].z - columns[0].y * columns[2].z) * invDet;
        result[0][2] = (columns[0].y * columns[1].z - columns[1].y * columns[0].z) * invDet;
        result[1][0] = (columns[2].x * columns[1].z - columns[1].x * columns[2].z) * invDet;
        result[1][1] = (columns[0].x * columns[2].z - columns[2].x * columns[0].z) * invDet;
        result[1][2] = (columns[1].x * columns[0].z - columns[0].x * columns[1].z) * invDet;
        result[2][0] = (columns[1].x * columns[2].y - columns[2].x * columns[1].y) * invDet;
        result[2][1] = (columns[2].x * columns[0].y - columns[0].x * columns[2].y) * invDet;
        result[2][2] = (columns[0].x * columns[1].y - columns[1].x * columns[0].y) * invDet;

        return result;
    }

    static mat3 identity() { return mat3(1.0f); }

    static mat3 scale(const vec2& s) {
        return mat3(
            vec3(s.x, 0.0f, 0.0f),
            vec3(0.0f, s.y, 0.0f),
            vec3(0.0f, 0.0f, 1.0f)
        );
    }

    static mat3 rotate(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return mat3(
            vec3(c, s, 0.0f),
            vec3(-s, c, 0.0f),
            vec3(0.0f, 0.0f, 1.0f)
        );
    }

    static mat3 translate(const vec2& t) {
        return mat3(
            vec3(1.0f, 0.0f, 0.0f),
            vec3(0.0f, 1.0f, 0.0f),
            vec3(t.x, t.y, 1.0f)
        );
    }
};

// ============================================================================
// mat4 (4x4 matrix, column-major)
// ============================================================================
struct alignas(16) mat4 {
    vec4 columns[4];

    mat4() {
        columns[0] = vec4(1.0f, 0.0f, 0.0f, 0.0f);
        columns[1] = vec4(0.0f, 1.0f, 0.0f, 0.0f);
        columns[2] = vec4(0.0f, 0.0f, 1.0f, 0.0f);
        columns[3] = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    mat4(float diagonal) {
        columns[0] = vec4(diagonal, 0.0f, 0.0f, 0.0f);
        columns[1] = vec4(0.0f, diagonal, 0.0f, 0.0f);
        columns[2] = vec4(0.0f, 0.0f, diagonal, 0.0f);
        columns[3] = vec4(0.0f, 0.0f, 0.0f, diagonal);
    }

    mat4(const vec4& c0, const vec4& c1, const vec4& c2, const vec4& c3) {
        columns[0] = c0;
        columns[1] = c1;
        columns[2] = c2;
        columns[3] = c3;
    }

    vec4& operator[](size_t i) { return columns[i]; }
    const vec4& operator[](size_t i) const { return columns[i]; }

    mat4 operator*(const mat4& other) const {
        mat4 result(0.0f);
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                result[col][row] =
                    columns[0][row] * other[col][0] +
                    columns[1][row] * other[col][1] +
                    columns[2][row] * other[col][2] +
                    columns[3][row] * other[col][3];
            }
        }
        return result;
    }

    vec4 operator*(const vec4& v) const {
        return vec4(
            columns[0].x * v.x + columns[1].x * v.y + columns[2].x * v.z + columns[3].x * v.w,
            columns[0].y * v.x + columns[1].y * v.y + columns[2].y * v.z + columns[3].y * v.w,
            columns[0].z * v.x + columns[1].z * v.y + columns[2].z * v.z + columns[3].z * v.w,
            columns[0].w * v.x + columns[1].w * v.y + columns[2].w * v.z + columns[3].w * v.w
        );
    }

    vec3 transformPoint(const vec3& p) const {
        vec4 result = (*this) * vec4(p, 1.0f);
        return result.xyz() / result.w;
    }

    vec3 transformVector(const vec3& v) const {
        vec4 result = (*this) * vec4(v, 0.0f);
        return result.xyz();
    }

    mat4 transposed() const {
        return mat4(
            vec4(columns[0].x, columns[1].x, columns[2].x, columns[3].x),
            vec4(columns[0].y, columns[1].y, columns[2].y, columns[3].y),
            vec4(columns[0].z, columns[1].z, columns[2].z, columns[3].z),
            vec4(columns[0].w, columns[1].w, columns[2].w, columns[3].w)
        );
    }

    mat4 inverse() const {
        float m[16];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                m[i * 4 + j] = columns[i][j];
            }
        }

        float inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
               + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
               - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
               + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
                - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
               - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
               + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
               - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
                + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
               + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
               - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
                + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
                - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
               - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
               + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
                - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
                + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::abs(det) < Math::EPSILON) return mat4(1.0f);

        det = 1.0f / det;
        mat4 result;
        for (int i = 0; i < 16; i++) {
            inv[i] *= det;
        }

        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                result[i][j] = inv[i * 4 + j];
            }
        }

        return result;
    }

    static mat4 identity() { return mat4(1.0f); }

    // Transformation matrices
    static mat4 translate(const vec3& t) {
        mat4 result(1.0f);
        result[3] = vec4(t, 1.0f);
        return result;
    }

    static mat4 scale(const vec3& s) {
        return mat4(
            vec4(s.x, 0.0f, 0.0f, 0.0f),
            vec4(0.0f, s.y, 0.0f, 0.0f),
            vec4(0.0f, 0.0f, s.z, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    static mat4 rotateX(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return mat4(
            vec4(1.0f, 0.0f, 0.0f, 0.0f),
            vec4(0.0f, c, s, 0.0f),
            vec4(0.0f, -s, c, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    static mat4 rotateY(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return mat4(
            vec4(c, 0.0f, -s, 0.0f),
            vec4(0.0f, 1.0f, 0.0f, 0.0f),
            vec4(s, 0.0f, c, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    static mat4 rotateZ(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return mat4(
            vec4(c, s, 0.0f, 0.0f),
            vec4(-s, c, 0.0f, 0.0f),
            vec4(0.0f, 0.0f, 1.0f, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    static mat4 rotate(const vec3& axis, float angle) {
        vec3 a = axis.normalized();
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1.0f - c;

        return mat4(
            vec4(t * a.x * a.x + c,       t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y, 0.0f),
            vec4(t * a.x * a.y - s * a.z, t * a.y * a.y + c,       t * a.y * a.z + s * a.x, 0.0f),
            vec4(t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c,       0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    // Camera matrices
    static mat4 perspective(float fovy, float aspect, float near, float far) {
        float tanHalfFovy = std::tan(fovy / 2.0f);
        mat4 result(0.0f);
        result[0][0] = 1.0f / (aspect * tanHalfFovy);
        result[1][1] = 1.0f / tanHalfFovy;
        result[2][2] = -(far + near) / (far - near);
        result[2][3] = -1.0f;
        result[3][2] = -(2.0f * far * near) / (far - near);
        return result;
    }

    static mat4 perspectiveInfinite(float fovy, float aspect, float near) {
        float tanHalfFovy = std::tan(fovy / 2.0f);
        mat4 result(0.0f);
        result[0][0] = 1.0f / (aspect * tanHalfFovy);
        result[1][1] = 1.0f / tanHalfFovy;
        result[2][2] = -1.0f;
        result[2][3] = -1.0f;
        result[3][2] = -2.0f * near;
        return result;
    }

    static mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        mat4 result(1.0f);
        result[0][0] = 2.0f / (right - left);
        result[1][1] = 2.0f / (top - bottom);
        result[2][2] = -2.0f / (far - near);
        result[3][0] = -(right + left) / (right - left);
        result[3][1] = -(top + bottom) / (top - bottom);
        result[3][2] = -(far + near) / (far - near);
        return result;
    }

    static mat4 lookAt(const vec3& eye, const vec3& center, const vec3& up) {
        vec3 f = (center - eye).normalized();
        vec3 s = f.cross(up).normalized();
        vec3 u = s.cross(f);

        mat4 result(1.0f);
        result[0][0] = s.x;
        result[1][0] = s.y;
        result[2][0] = s.z;
        result[0][1] = u.x;
        result[1][1] = u.y;
        result[2][1] = u.z;
        result[0][2] = -f.x;
        result[1][2] = -f.y;
        result[2][2] = -f.z;
        result[3][0] = -s.dot(eye);
        result[3][1] = -u.dot(eye);
        result[3][2] = f.dot(eye);
        return result;
    }
};

} // namespace Engine

#endif // ENGINE_MATRIX_HPP
