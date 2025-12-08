#pragma once

/**
 * Mock Renderer - Fake renderer for testing without GPU
 *
 * This mock provides a fake renderer implementation that allows
 * testing of game systems without actual rendering.
 */

#include "engine/math/Vector.hpp"
#include "engine/math/Matrix.hpp"
#include <vector>

namespace MockRenderer {

/**
 * Mock mesh data
 */
struct MockMesh {
    std::vector<Engine::vec3> vertices;
    std::vector<uint32_t> indices;
    bool uploaded = false;
};

/**
 * Mock texture data
 */
struct MockTexture {
    int width = 0;
    int height = 0;
    int channels = 0;
    bool uploaded = false;
};

/**
 * Mock material data
 */
struct MockMaterial {
    Engine::vec3 albedo = {1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    int textureId = -1;
};

/**
 * Mock renderer class
 */
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool initialize() { initialized_ = true; return true; }
    void shutdown() { initialized_ = false; }
    bool isInitialized() const { return initialized_; }

    // Frame operations
    void beginFrame() { frameCount_++; }
    void endFrame() {}
    void clear() {}

    // Mesh operations
    int createMesh(const std::vector<Engine::vec3>& vertices, const std::vector<uint32_t>& indices) {
        MockMesh mesh;
        mesh.vertices = vertices;
        mesh.indices = indices;
        mesh.uploaded = true;
        meshes_.push_back(mesh);
        return static_cast<int>(meshes_.size() - 1);
    }

    void destroyMesh(int meshId) {
        if (meshId >= 0 && meshId < static_cast<int>(meshes_.size())) {
            meshes_[meshId].uploaded = false;
        }
    }

    // Texture operations
    int createTexture(int width, int height, int channels, const void* data) {
        MockTexture texture;
        texture.width = width;
        texture.height = height;
        texture.channels = channels;
        texture.uploaded = true;
        textures_.push_back(texture);
        return static_cast<int>(textures_.size() - 1);
    }

    void destroyTexture(int textureId) {
        if (textureId >= 0 && textureId < static_cast<int>(textures_.size())) {
            textures_[textureId].uploaded = false;
        }
    }

    // Material operations
    int createMaterial(const MockMaterial& material) {
        materials_.push_back(material);
        return static_cast<int>(materials_.size() - 1);
    }

    // Draw operations
    void drawMesh(int meshId, const Engine::mat4& transform, int materialId = 0) {
        drawCallCount_++;
    }

    void drawLine(const Engine::vec3& start, const Engine::vec3& end, const Engine::vec3& color) {
        drawCallCount_++;
    }

    void drawText(const char* text, const Engine::vec2& position, const Engine::vec3& color) {
        drawCallCount_++;
    }

    // Statistics
    int getFrameCount() const { return frameCount_; }
    int getDrawCallCount() const { return drawCallCount_; }
    void resetStats() { drawCallCount_ = 0; }

private:
    bool initialized_ = false;
    int frameCount_ = 0;
    int drawCallCount_ = 0;
    std::vector<MockMesh> meshes_;
    std::vector<MockTexture> textures_;
    std::vector<MockMaterial> materials_;
};

} // namespace MockRenderer
