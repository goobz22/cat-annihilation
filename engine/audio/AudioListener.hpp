#pragma once

#include <AL/al.h>
#include <array>

namespace CatEngine {

/**
 * @brief Represents the audio listener in 3D space
 *
 * The listener is typically attached to the camera or player.
 * There is only one listener in OpenAL, so this class manages
 * the global listener state.
 */
class AudioListener {
public:
    AudioListener() = default;
    ~AudioListener() = default;

    /**
     * @brief Set the position of the listener
     * @param x X coordinate
     * @param y Y coordinate
     * @param z Z coordinate
     */
    void setPosition(float x, float y, float z);

    /**
     * @brief Set the position of the listener
     * @param position Array containing [x, y, z]
     */
    void setPosition(const std::array<float, 3>& position);

    /**
     * @brief Get the current position
     */
    std::array<float, 3> getPosition() const;

    /**
     * @brief Set the velocity of the listener (for doppler effect)
     * @param x X velocity
     * @param y Y velocity
     * @param z Z velocity
     */
    void setVelocity(float x, float y, float z);

    /**
     * @brief Set the velocity of the listener
     * @param velocity Array containing [x, y, z]
     */
    void setVelocity(const std::array<float, 3>& velocity);

    /**
     * @brief Get the current velocity
     */
    std::array<float, 3> getVelocity() const;

    /**
     * @brief Set the orientation of the listener
     * @param forwardX Forward vector X
     * @param forwardY Forward vector Y
     * @param forwardZ Forward vector Z
     * @param upX Up vector X
     * @param upY Up vector Y
     * @param upZ Up vector Z
     */
    void setOrientation(float forwardX, float forwardY, float forwardZ,
                       float upX, float upY, float upZ);

    /**
     * @brief Set the orientation of the listener
     * @param forward Forward direction vector [x, y, z]
     * @param up Up direction vector [x, y, z]
     */
    void setOrientation(const std::array<float, 3>& forward,
                       const std::array<float, 3>& up);

    /**
     * @brief Get the current orientation
     * @return Array containing [forwardX, forwardY, forwardZ, upX, upY, upZ]
     */
    std::array<float, 6> getOrientation() const;

    /**
     * @brief Set the master gain (volume) for the listener
     * @param gain Volume level (0.0 to 1.0)
     */
    void setGain(float gain);

    /**
     * @brief Get the current master gain
     */
    float getGain() const;

private:
    std::array<float, 3> m_position = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> m_velocity = {0.0f, 0.0f, 0.0f};
    std::array<float, 6> m_orientation = {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}; // Forward: -Z, Up: +Y
    float m_gain = 1.0f;
};

} // namespace CatEngine
