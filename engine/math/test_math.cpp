/**
 * Math Library Compilation Test
 *
 * This file tests that all math library headers compile correctly
 * and demonstrates basic usage of each component.
 *
 * Compile with:
 * g++ -std=c++20 -msse4.1 -O2 -o test_math test_math.cpp
 */

#include "Math.hpp"
#include "Vector.hpp"
#include "Matrix.hpp"
#include "Quaternion.hpp"
#include "Transform.hpp"
#include "AABB.hpp"
#include "Frustum.hpp"
#include "Ray.hpp"
#include "Noise.hpp"

#include <iostream>

using namespace Engine;

int main() {
    std::cout << "=== Math Library Test ===" << std::endl;

    // Test Math utilities
    std::cout << "\n[Math]" << std::endl;
    std::cout << "PI = " << Math::PI << std::endl;
    std::cout << "radians(90) = " << Math::radians(90.0f) << std::endl;
    std::cout << "lerp(0, 10, 0.5) = " << Math::lerp(0.0f, 10.0f, 0.5f) << std::endl;

    // Test Vectors
    std::cout << "\n[Vector]" << std::endl;
    vec3 v1(1.0f, 2.0f, 3.0f);
    vec3 v2(4.0f, 5.0f, 6.0f);
    vec3 v3 = v1 + v2;
    std::cout << v1 << " + " << v2 << " = " << v3 << std::endl;
    std::cout << "v1.length() = " << v1.length() << std::endl;
    std::cout << "v1.dot(v2) = " << v1.dot(v2) << std::endl;
    std::cout << "v1.cross(v2) = " << v1.cross(v2) << std::endl;

    // Test Matrix
    std::cout << "\n[Matrix]" << std::endl;
    mat4 projection = mat4::perspective(Math::radians(45.0f), 16.0f/9.0f, 0.1f, 100.0f);
    mat4 view = mat4::lookAt(vec3(0, 0, 5), vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 model = mat4::translate(vec3(1, 2, 3));
    std::cout << "Created projection, view, and model matrices" << std::endl;

    // Test Quaternion
    std::cout << "\n[Quaternion]" << std::endl;
    Quaternion q1 = Quaternion::fromEuler(0.0f, Math::radians(45.0f), 0.0f);
    vec3 euler = q1.toEuler();
    std::cout << "Quaternion from euler angles: " << q1 << std::endl;
    std::cout << "Back to euler: " << euler << std::endl;

    Quaternion q2 = Quaternion::fromEuler(0.0f, Math::radians(90.0f), 0.0f);
    Quaternion q3 = Quaternion::slerp(q1, q2, 0.5f);
    std::cout << "Slerp result: " << q3 << std::endl;

    // Test Transform
    std::cout << "\n[Transform]" << std::endl;
    Transform transform;
    transform.position = vec3(1, 2, 3);
    transform.rotation = Quaternion::fromEuler(0.0f, Math::radians(45.0f), 0.0f);
    transform.scale = vec3(2, 2, 2);
    vec3 point = transform.transformPoint(vec3(1, 0, 0));
    std::cout << "Transform: " << transform << std::endl;
    std::cout << "Transformed point: " << point << std::endl;

    // Test AABB
    std::cout << "\n[AABB]" << std::endl;
    AABB box1(vec3(-1, -1, -1), vec3(1, 1, 1));
    AABB box2(vec3(0, 0, 0), vec3(2, 2, 2));
    std::cout << "box1: " << box1 << std::endl;
    std::cout << "box2: " << box2 << std::endl;
    std::cout << "Intersects: " << (box1.intersects(box2) ? "yes" : "no") << std::endl;
    std::cout << "box1 contains (0,0,0): " << (box1.contains(vec3(0,0,0)) ? "yes" : "no") << std::endl;

    // Test Ray
    std::cout << "\n[Ray]" << std::endl;
    Ray ray(vec3(0, 0, 0), vec3(0, 0, -1));
    float rayT;
    bool hit = ray.intersectsSphere(vec3(0, 0, -5), 1.0f, rayT);
    std::cout << "Ray: " << ray << std::endl;
    std::cout << "Intersects sphere: " << (hit ? "yes" : "no") << std::endl;
    if (hit) std::cout << "Distance: " << rayT << std::endl;

    // Test Frustum
    std::cout << "\n[Frustum]" << std::endl;
    mat4 vp = projection * view;
    Frustum frustum = Frustum::fromMatrix(vp);
    bool inFrustum = frustum.intersectsAABB(box1);
    std::cout << "AABB in frustum: " << (inFrustum ? "yes" : "no") << std::endl;

    // Test Noise
    std::cout << "\n[Noise]" << std::endl;
    Noise::Perlin perlin;
    float perlinValue = perlin.noise(1.5f, 2.3f, 3.7f);
    std::cout << "Perlin noise(1.5, 2.3, 3.7) = " << perlinValue << std::endl;

    Noise::Simplex simplex;
    float simplexValue = simplex.noise(1.5f, 2.3f, 3.7f);
    std::cout << "Simplex noise(1.5, 2.3, 3.7) = " << simplexValue << std::endl;

    float octaveValue = perlin.octave(1.5f, 2.3f, 3.7f, 4);
    std::cout << "Perlin octave noise = " << octaveValue << std::endl;

    std::cout << "\n=== All tests completed successfully! ===" << std::endl;

    return 0;
}
