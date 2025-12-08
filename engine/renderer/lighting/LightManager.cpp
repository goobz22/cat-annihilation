#include "LightManager.hpp"
#include "../../math/AABB.hpp"
#include <cstring>
#include <algorithm>

namespace Engine::Renderer {

LightManager::LightManager() {
    // Reserve space for maximum lights
    m_directionalLights.resize(MAX_DIRECTIONAL_LIGHTS);
    m_pointLights.resize(MAX_POINT_LIGHTS);
    m_spotLights.resize(MAX_SPOT_LIGHTS);

    // Reserve GPU light array
    m_gpuLights.reserve(MAX_TOTAL_LIGHTS);
}

LightManager::~LightManager() {
    shutdown();
}

bool LightManager::initialize(CatEngine::RHI::IRHIBuffer* lightBuffer) {
    if (m_initialized) {
        return true;
    }

    if (!lightBuffer) {
        return false;
    }

    m_lightBuffer = lightBuffer;
    m_initialized = true;
    m_dirty = true;

    return true;
}

void LightManager::shutdown() {
    clear();
    m_lightBuffer = nullptr;
    m_initialized = false;
}

// ============================================================================
// Light Management
// ============================================================================

LightHandle LightManager::addDirectionalLight(const DirectionalLight& light) {
    if (m_directionalLightCount >= MAX_DIRECTIONAL_LIGHTS) {
        return LightHandle{}; // Invalid handle
    }

    // Find first inactive slot
    for (uint32_t i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i) {
        if (!m_directionalLights[i].active) {
            m_directionalLights[i].light = light;
            m_directionalLights[i].active = true;
            m_directionalLights[i].generation = m_nextGeneration++;
            m_directionalLightCount++;
            m_dirty = true;

            LightHandle handle;
            handle.index = i;
            handle.generation = m_directionalLights[i].generation;
            return handle;
        }
    }

    return LightHandle{}; // Should never reach here
}

LightHandle LightManager::addPointLight(const PointLight& light) {
    if (m_pointLightCount >= MAX_POINT_LIGHTS) {
        return LightHandle{}; // Invalid handle
    }

    // Find first inactive slot
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (!m_pointLights[i].active) {
            m_pointLights[i].light = light;
            m_pointLights[i].active = true;
            m_pointLights[i].generation = m_nextGeneration++;
            m_pointLightCount++;
            m_dirty = true;

            LightHandle handle;
            handle.index = i;
            handle.generation = m_pointLights[i].generation;
            return handle;
        }
    }

    return LightHandle{}; // Should never reach here
}

LightHandle LightManager::addSpotLight(const SpotLight& light) {
    if (m_spotLightCount >= MAX_SPOT_LIGHTS) {
        return LightHandle{}; // Invalid handle
    }

    // Find first inactive slot
    for (uint32_t i = 0; i < MAX_SPOT_LIGHTS; ++i) {
        if (!m_spotLights[i].active) {
            m_spotLights[i].light = light;
            m_spotLights[i].active = true;
            m_spotLights[i].generation = m_nextGeneration++;
            m_spotLightCount++;
            m_dirty = true;

            LightHandle handle;
            handle.index = i;
            handle.generation = m_spotLights[i].generation;
            return handle;
        }
    }

    return LightHandle{}; // Should never reach here
}

void LightManager::removeLight(LightHandle handle) {
    if (!handle.isValid()) {
        return;
    }

    // Try to remove from directional lights
    if (handle.index < MAX_DIRECTIONAL_LIGHTS &&
        m_directionalLights[handle.index].active &&
        m_directionalLights[handle.index].generation == handle.generation) {
        m_directionalLights[handle.index].active = false;
        m_directionalLightCount--;
        m_dirty = true;
        return;
    }

    // Try to remove from point lights
    if (handle.index < MAX_POINT_LIGHTS &&
        m_pointLights[handle.index].active &&
        m_pointLights[handle.index].generation == handle.generation) {
        m_pointLights[handle.index].active = false;
        m_pointLightCount--;
        m_dirty = true;
        return;
    }

    // Try to remove from spot lights
    if (handle.index < MAX_SPOT_LIGHTS &&
        m_spotLights[handle.index].active &&
        m_spotLights[handle.index].generation == handle.generation) {
        m_spotLights[handle.index].active = false;
        m_spotLightCount--;
        m_dirty = true;
        return;
    }
}

void LightManager::updateDirectionalLight(LightHandle handle, const DirectionalLight& light) {
    if (validateHandle(handle, m_directionalLights)) {
        m_directionalLights[handle.index].light = light;
        m_dirty = true;
    }
}

void LightManager::updatePointLight(LightHandle handle, const PointLight& light) {
    if (validateHandle(handle, m_pointLights)) {
        m_pointLights[handle.index].light = light;
        m_dirty = true;
    }
}

void LightManager::updateSpotLight(LightHandle handle, const SpotLight& light) {
    if (validateHandle(handle, m_spotLights)) {
        m_spotLights[handle.index].light = light;
        m_dirty = true;
    }
}

void LightManager::clear() {
    for (auto& entry : m_directionalLights) {
        entry.active = false;
    }
    for (auto& entry : m_pointLights) {
        entry.active = false;
    }
    for (auto& entry : m_spotLights) {
        entry.active = false;
    }

    m_directionalLightCount = 0;
    m_pointLightCount = 0;
    m_spotLightCount = 0;
    m_gpuLights.clear();
    m_visibleLightIndices.clear();
    m_dirty = true;
}

// ============================================================================
// Light Queries
// ============================================================================

const DirectionalLight* LightManager::getDirectionalLight(LightHandle handle) const {
    if (validateHandle(handle, m_directionalLights)) {
        return &m_directionalLights[handle.index].light;
    }
    return nullptr;
}

DirectionalLight* LightManager::getDirectionalLight(LightHandle handle) {
    if (validateHandle(handle, m_directionalLights)) {
        return &m_directionalLights[handle.index].light;
    }
    return nullptr;
}

const PointLight* LightManager::getPointLight(LightHandle handle) const {
    if (validateHandle(handle, m_pointLights)) {
        return &m_pointLights[handle.index].light;
    }
    return nullptr;
}

PointLight* LightManager::getPointLight(LightHandle handle) {
    if (validateHandle(handle, m_pointLights)) {
        return &m_pointLights[handle.index].light;
    }
    return nullptr;
}

const SpotLight* LightManager::getSpotLight(LightHandle handle) const {
    if (validateHandle(handle, m_spotLights)) {
        return &m_spotLights[handle.index].light;
    }
    return nullptr;
}

SpotLight* LightManager::getSpotLight(LightHandle handle) {
    if (validateHandle(handle, m_spotLights)) {
        return &m_spotLights[handle.index].light;
    }
    return nullptr;
}

// ============================================================================
// GPU Upload
// ============================================================================

bool LightManager::uploadToGPU() {
    if (!m_dirty) {
        return false;
    }

    forceUploadToGPU();
    return true;
}

void LightManager::forceUploadToGPU() {
    if (!m_initialized || !m_lightBuffer) {
        return;
    }

    // Rebuild GPU light array
    rebuildGPULights();

    // Upload to GPU buffer
    if (!m_gpuLights.empty()) {
        size_t dataSize = m_gpuLights.size() * sizeof(GPULight);
        m_lightBuffer->UpdateData(m_gpuLights.data(), dataSize, 0);
    }

    m_dirty = false;
}

void LightManager::rebuildGPULights() {
    m_gpuLights.clear();

    // Add directional lights
    for (const auto& entry : m_directionalLights) {
        if (entry.active) {
            m_gpuLights.push_back(GPULight::fromDirectional(entry.light));
        }
    }

    // Add point lights
    for (const auto& entry : m_pointLights) {
        if (entry.active) {
            m_gpuLights.push_back(GPULight::fromPoint(entry.light));
        }
    }

    // Add spot lights
    for (const auto& entry : m_spotLights) {
        if (entry.active) {
            m_gpuLights.push_back(GPULight::fromSpot(entry.light));
        }
    }
}

// ============================================================================
// CPU-side Frustum Culling
// ============================================================================

void LightManager::cullLights(const Frustum& frustum) {
    m_visibleLightIndices.clear();
    uint32_t lightIndex = 0;

    // Directional lights are always visible (infinite range)
    for (const auto& entry : m_directionalLights) {
        if (entry.active) {
            m_visibleLightIndices.push_back(lightIndex);
        }
        lightIndex++;
    }

    // Cull point lights based on sphere-frustum intersection
    for (const auto& entry : m_pointLights) {
        if (entry.active) {
            // Check if light's influence sphere intersects frustum
            if (frustum.intersectsSphere(entry.light.position, entry.light.radius)) {
                m_visibleLightIndices.push_back(lightIndex);
            }
        }
        lightIndex++;
    }

    // Cull spot lights based on cone-frustum intersection
    // Simplified: use sphere test with radius
    for (const auto& entry : m_spotLights) {
        if (entry.active) {
            // Simplified culling: use sphere test
            // More accurate would be cone-frustum intersection
            if (frustum.intersectsSphere(entry.light.position, entry.light.radius)) {
                m_visibleLightIndices.push_back(lightIndex);
            }
        }
        lightIndex++;
    }
}

// ============================================================================
// Private Methods
// ============================================================================

template<typename T>
bool LightManager::validateHandle(const LightHandle& handle, const std::vector<LightEntry<T>>& entries) const {
    if (!handle.isValid()) {
        return false;
    }

    if (handle.index >= entries.size()) {
        return false;
    }

    const auto& entry = entries[handle.index];
    return entry.active && entry.generation == handle.generation;
}

// Explicit template instantiations
template bool LightManager::validateHandle(const LightHandle&, const std::vector<LightEntry<DirectionalLight>>&) const;
template bool LightManager::validateHandle(const LightHandle&, const std::vector<LightEntry<PointLight>>&) const;
template bool LightManager::validateHandle(const LightHandle&, const std::vector<LightEntry<SpotLight>>&) const;

} // namespace Engine::Renderer
