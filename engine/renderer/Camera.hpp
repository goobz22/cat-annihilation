#pragma once

#include "../math/Vector.hpp"
#include "../math/Matrix.hpp"
#include "../math/Quaternion.hpp"
#include "../math/Frustum.hpp"
#include "../math/Ray.hpp"

namespace CatEngine::Renderer {

/**
 * Camera projection type
 */
enum class ProjectionType {
    Perspective,
    Orthographic
};

/**
 * Camera class
 * Handles view and projection matrices, frustum culling, and ray casting
 */
class Camera {
public:
    Camera() {
        UpdateViewMatrix();
        UpdateProjectionMatrix();
    }

    ~Camera() = default;

    // ========================================================================
    // Transform
    // ========================================================================

    /**
     * Set camera position
     */
    void SetPosition(const Engine::vec3& pos) {
        position = pos;
        viewMatrixDirty = true;
    }

    /**
     * Get camera position
     */
    const Engine::vec3& GetPosition() const {
        return position;
    }

    /**
     * Set camera rotation (quaternion)
     */
    void SetRotation(const Engine::Quaternion& rot) {
        rotation = rot;
        viewMatrixDirty = true;
    }

    /**
     * Get camera rotation
     */
    const Engine::Quaternion& GetRotation() const {
        return rotation;
    }

    /**
     * Set camera rotation (Euler angles in radians)
     */
    void SetRotationEuler(const Engine::vec3& euler) {
        rotation = Engine::Quaternion::fromEuler(euler);
        viewMatrixDirty = true;
    }

    /**
     * Look at a target position
     */
    void LookAt(const Engine::vec3& target, const Engine::vec3& up = Engine::vec3::up()) {
        Engine::vec3 forward = (target - position).normalized();
        rotation = Engine::Quaternion::lookRotation(forward, up);
        viewMatrixDirty = true;
    }

    /**
     * Get forward direction vector
     */
    Engine::vec3 GetForward() const {
        return rotation.rotate(Engine::vec3(0.0f, 0.0f, -1.0f));
    }

    /**
     * Get right direction vector
     */
    Engine::vec3 GetRight() const {
        return rotation.rotate(Engine::vec3(1.0f, 0.0f, 0.0f));
    }

    /**
     * Get up direction vector
     */
    Engine::vec3 GetUp() const {
        return rotation.rotate(Engine::vec3(0.0f, 1.0f, 0.0f));
    }

    // ========================================================================
    // Perspective Projection
    // ========================================================================

    /**
     * Set perspective projection
     * @param fov Field of view in radians
     * @param aspect Aspect ratio (width / height)
     * @param near Near clip plane
     * @param far Far clip plane
     */
    void SetPerspective(float fov, float aspect, float near, float far) {
        projectionType = ProjectionType::Perspective;
        fieldOfView = fov;
        aspectRatio = aspect;
        nearPlane = near;
        farPlane = far;
        projectionMatrixDirty = true;
    }

    /**
     * Set field of view (in radians)
     */
    void SetFieldOfView(float fov) {
        fieldOfView = fov;
        projectionMatrixDirty = true;
    }

    /**
     * Get field of view (in radians)
     */
    float GetFieldOfView() const {
        return fieldOfView;
    }

    /**
     * Set aspect ratio
     */
    void SetAspectRatio(float aspect) {
        aspectRatio = aspect;
        projectionMatrixDirty = true;
    }

    /**
     * Get aspect ratio
     */
    float GetAspectRatio() const {
        return aspectRatio;
    }

    // ========================================================================
    // Orthographic Projection
    // ========================================================================

    /**
     * Set orthographic projection
     * @param left Left plane
     * @param right Right plane
     * @param bottom Bottom plane
     * @param top Top plane
     * @param near Near plane
     * @param far Far plane
     */
    void SetOrthographic(float left, float right, float bottom, float top, float near, float far) {
        projectionType = ProjectionType::Orthographic;
        orthoLeft = left;
        orthoRight = right;
        orthoBottom = bottom;
        orthoTop = top;
        nearPlane = near;
        farPlane = far;
        projectionMatrixDirty = true;
    }

    /**
     * Set orthographic projection (centered)
     * @param width Orthographic width
     * @param height Orthographic height
     * @param near Near plane
     * @param far Far plane
     */
    void SetOrthographicCentered(float width, float height, float near, float far) {
        SetOrthographic(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, near, far);
    }

    // ========================================================================
    // Clip Planes
    // ========================================================================

    /**
     * Set near and far clip planes
     */
    void SetClipPlanes(float near, float far) {
        nearPlane = near;
        farPlane = far;
        projectionMatrixDirty = true;
    }

    /**
     * Get near clip plane
     */
    float GetNearPlane() const {
        return nearPlane;
    }

    /**
     * Get far clip plane
     */
    float GetFarPlane() const {
        return farPlane;
    }

    // ========================================================================
    // Temporal Anti-Aliasing (TAA)
    // ========================================================================

    /**
     * Set jitter for TAA (in pixels, typically [-0.5, 0.5])
     */
    void SetJitter(const Engine::vec2& jitter) {
        this->jitter = jitter;
        projectionMatrixDirty = true;
    }

    /**
     * Get current jitter
     */
    const Engine::vec2& GetJitter() const {
        return jitter;
    }

    /**
     * Clear jitter
     */
    void ClearJitter() {
        jitter = Engine::vec2(0.0f);
        projectionMatrixDirty = true;
    }

    // ========================================================================
    // Matrices
    // ========================================================================

    /**
     * Get view matrix
     */
    const Engine::mat4& GetViewMatrix() {
        if (viewMatrixDirty) {
            UpdateViewMatrix();
        }
        return viewMatrix;
    }

    /**
     * Get projection matrix
     */
    const Engine::mat4& GetProjectionMatrix() {
        if (projectionMatrixDirty) {
            UpdateProjectionMatrix();
        }
        return projectionMatrix;
    }

    /**
     * Get view-projection matrix
     */
    Engine::mat4 GetViewProjectionMatrix() {
        return GetProjectionMatrix() * GetViewMatrix();
    }

    /**
     * Get inverse view matrix
     */
    Engine::mat4 GetInverseViewMatrix() {
        return GetViewMatrix().inverse();
    }

    /**
     * Get inverse projection matrix
     */
    Engine::mat4 GetInverseProjectionMatrix() {
        return GetProjectionMatrix().inverse();
    }

    /**
     * Get inverse view-projection matrix
     */
    Engine::mat4 GetInverseViewProjectionMatrix() {
        return GetViewProjectionMatrix().inverse();
    }

    // ========================================================================
    // Frustum Culling
    // ========================================================================

    /**
     * Extract frustum from current view-projection matrix
     */
    Engine::Frustum ExtractFrustum() {
        Engine::mat4 vp = GetViewProjectionMatrix();
        return Engine::Frustum::fromMatrix(vp);
    }

    /**
     * Get frustum (cached)
     */
    const Engine::Frustum& GetFrustum() {
        if (viewMatrixDirty || projectionMatrixDirty) {
            frustum = ExtractFrustum();
        }
        return frustum;
    }

    // ========================================================================
    // Ray Casting / Picking
    // ========================================================================

    /**
     * Create a ray from screen coordinates
     * @param screenX Screen X coordinate
     * @param screenY Screen Y coordinate
     * @param screenWidth Screen width in pixels
     * @param screenHeight Screen height in pixels
     */
    Engine::Ray ScreenPointToRay(float screenX, float screenY, float screenWidth, float screenHeight) {
        // Convert screen coordinates to NDC [-1, 1]
        float ndcX = (2.0f * screenX) / screenWidth - 1.0f;
        float ndcY = 1.0f - (2.0f * screenY) / screenHeight;

        // Get inverse view-projection matrix
        Engine::mat4 invVP = GetInverseViewProjectionMatrix();

        // Near plane point
        Engine::vec4 nearPoint = invVP * Engine::vec4(ndcX, ndcY, -1.0f, 1.0f);
        nearPoint = nearPoint / nearPoint.w;

        // Far plane point
        Engine::vec4 farPoint = invVP * Engine::vec4(ndcX, ndcY, 1.0f, 1.0f);
        farPoint = farPoint / farPoint.w;

        // Create ray from near to far
        Engine::vec3 origin = nearPoint.xyz();
        Engine::vec3 direction = (farPoint.xyz() - nearPoint.xyz()).normalized();

        return Engine::Ray(origin, direction);
    }

    /**
     * Create a ray from normalized screen coordinates [0, 1]
     */
    Engine::Ray NormalizedScreenPointToRay(float normalizedX, float normalizedY, float screenWidth, float screenHeight) {
        return ScreenPointToRay(normalizedX * screenWidth, normalizedY * screenHeight, screenWidth, screenHeight);
    }

    /**
     * Convert world position to screen coordinates
     * @param worldPos World space position
     * @param screenWidth Screen width in pixels
     * @param screenHeight Screen height in pixels
     * @return Screen coordinates (x, y) and depth (z)
     */
    Engine::vec3 WorldToScreen(const Engine::vec3& worldPos, float screenWidth, float screenHeight) {
        Engine::vec4 clipPos = GetViewProjectionMatrix() * Engine::vec4(worldPos, 1.0f);

        // Perspective divide
        if (clipPos.w != 0.0f) {
            clipPos = clipPos / clipPos.w;
        }

        // NDC to screen coordinates
        float screenX = (clipPos.x + 1.0f) * 0.5f * screenWidth;
        float screenY = (1.0f - clipPos.y) * 0.5f * screenHeight;
        float depth = clipPos.z;

        return Engine::vec3(screenX, screenY, depth);
    }

    // ========================================================================
    // Projection Type
    // ========================================================================

    /**
     * Get projection type
     */
    ProjectionType GetProjectionType() const {
        return projectionType;
    }

    /**
     * Check if camera uses perspective projection
     */
    bool IsPerspective() const {
        return projectionType == ProjectionType::Perspective;
    }

    /**
     * Check if camera uses orthographic projection
     */
    bool IsOrthographic() const {
        return projectionType == ProjectionType::Orthographic;
    }

private:
    // Transform
    Engine::vec3 position = Engine::vec3(0.0f, 0.0f, 5.0f);
    Engine::Quaternion rotation = Engine::Quaternion::identity();

    // Perspective projection parameters
    float fieldOfView = Engine::Math::HALF_PI;  // 90 degrees
    float aspectRatio = 16.0f / 9.0f;

    // Orthographic projection parameters
    float orthoLeft = -10.0f;
    float orthoRight = 10.0f;
    float orthoBottom = -10.0f;
    float orthoTop = 10.0f;

    // Common projection parameters
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    ProjectionType projectionType = ProjectionType::Perspective;

    // TAA jitter (in pixels)
    Engine::vec2 jitter = Engine::vec2(0.0f);

    // Cached matrices
    Engine::mat4 viewMatrix;
    Engine::mat4 projectionMatrix;
    bool viewMatrixDirty = true;
    bool projectionMatrixDirty = true;

    // Cached frustum
    Engine::Frustum frustum;

    /**
     * Update view matrix from position and rotation
     */
    void UpdateViewMatrix() {
        // Build view matrix from position and rotation
        Engine::mat4 rotationMatrix = rotation.toMatrix();
        Engine::mat4 translationMatrix = Engine::mat4::translate(-position);
        viewMatrix = rotationMatrix.transposed() * translationMatrix;
        viewMatrixDirty = false;
    }

    /**
     * Update projection matrix from projection parameters
     */
    void UpdateProjectionMatrix() {
        if (projectionType == ProjectionType::Perspective) {
            projectionMatrix = Engine::mat4::perspective(fieldOfView, aspectRatio, nearPlane, farPlane);

            // Apply jitter for TAA
            if (jitter.x != 0.0f || jitter.y != 0.0f) {
                float jitterX = 2.0f * jitter.x / aspectRatio;  // Adjust for aspect ratio
                float jitterY = 2.0f * jitter.y;
                projectionMatrix[2][0] += jitterX;
                projectionMatrix[2][1] += jitterY;
            }
        } else {
            projectionMatrix = Engine::mat4::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, nearPlane, farPlane);

            // Apply jitter for TAA (orthographic)
            if (jitter.x != 0.0f || jitter.y != 0.0f) {
                float width = orthoRight - orthoLeft;
                float height = orthoTop - orthoBottom;
                float jitterX = 2.0f * jitter.x / width;
                float jitterY = 2.0f * jitter.y / height;
                projectionMatrix[3][0] += jitterX;
                projectionMatrix[3][1] += jitterY;
            }
        }

        projectionMatrixDirty = false;
    }
};

/**
 * FPS Camera Controller
 * Utility class for first-person camera controls
 */
class FPSCameraController {
public:
    FPSCameraController(Camera* camera) : camera(camera) {}

    void Update(float deltaTime) {
        // This is a placeholder for FPS camera controls
        // Actual implementation would integrate with input system
    }

    void Rotate(float yaw, float pitch) {
        currentYaw += yaw * sensitivity;
        currentPitch += pitch * sensitivity;

        // Clamp pitch
        currentPitch = Engine::Math::clamp(currentPitch, -Engine::Math::HALF_PI + 0.1f, Engine::Math::HALF_PI - 0.1f);

        // Update camera rotation
        Engine::Quaternion yawRot = Engine::Quaternion::fromAxisAngle(Engine::vec3(0.0f, 1.0f, 0.0f), currentYaw);
        Engine::Quaternion pitchRot = Engine::Quaternion::fromAxisAngle(Engine::vec3(1.0f, 0.0f, 0.0f), currentPitch);
        camera->SetRotation(yawRot * pitchRot);
    }

    void Move(const Engine::vec3& direction) {
        Engine::vec3 forward = camera->GetForward();
        Engine::vec3 right = camera->GetRight();
        Engine::vec3 up = Engine::vec3(0.0f, 1.0f, 0.0f);

        Engine::vec3 movement = forward * direction.z + right * direction.x + up * direction.y;
        camera->SetPosition(camera->GetPosition() + movement * moveSpeed);
    }

    float moveSpeed = 5.0f;
    float sensitivity = 0.002f;

private:
    Camera* camera;
    float currentYaw = 0.0f;
    float currentPitch = 0.0f;
};

} // namespace CatEngine::Renderer
