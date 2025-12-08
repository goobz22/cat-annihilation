#include "AudioListener.hpp"

namespace CatEngine {

void AudioListener::setPosition(float x, float y, float z) {
    m_position = {x, y, z};
    alListener3f(AL_POSITION, x, y, z);
}

void AudioListener::setPosition(const std::array<float, 3>& position) {
    m_position = position;
    alListenerfv(AL_POSITION, position.data());
}

std::array<float, 3> AudioListener::getPosition() const {
    return m_position;
}

void AudioListener::setVelocity(float x, float y, float z) {
    m_velocity = {x, y, z};
    alListener3f(AL_VELOCITY, x, y, z);
}

void AudioListener::setVelocity(const std::array<float, 3>& velocity) {
    m_velocity = velocity;
    alListenerfv(AL_VELOCITY, velocity.data());
}

std::array<float, 3> AudioListener::getVelocity() const {
    return m_velocity;
}

void AudioListener::setOrientation(float forwardX, float forwardY, float forwardZ,
                                   float upX, float upY, float upZ) {
    m_orientation = {forwardX, forwardY, forwardZ, upX, upY, upZ};
    alListenerfv(AL_ORIENTATION, m_orientation.data());
}

void AudioListener::setOrientation(const std::array<float, 3>& forward,
                                   const std::array<float, 3>& up) {
    m_orientation = {forward[0], forward[1], forward[2], up[0], up[1], up[2]};
    alListenerfv(AL_ORIENTATION, m_orientation.data());
}

std::array<float, 6> AudioListener::getOrientation() const {
    return m_orientation;
}

void AudioListener::setGain(float gain) {
    m_gain = gain;
    alListenerf(AL_GAIN, gain);
}

float AudioListener::getGain() const {
    return m_gain;
}

} // namespace CatEngine
