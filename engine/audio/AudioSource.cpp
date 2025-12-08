#include "AudioSource.hpp"
#include <iostream>

namespace CatEngine {

AudioSource::AudioSource() {
    alGenSources(1, &m_sourceID);

    // Set default values
    alSourcef(m_sourceID, AL_PITCH, 1.0f);
    alSourcef(m_sourceID, AL_GAIN, 1.0f);
    alSource3f(m_sourceID, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(m_sourceID, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcei(m_sourceID, AL_LOOPING, AL_FALSE);
    alSourcef(m_sourceID, AL_ROLLOFF_FACTOR, 1.0f);
    alSourcef(m_sourceID, AL_REFERENCE_DISTANCE, 1.0f);
    alSourcef(m_sourceID, AL_MAX_DISTANCE, 1000.0f);
}

AudioSource::~AudioSource() {
    cleanup();
}

AudioSource::AudioSource(AudioSource&& other) noexcept
    : m_sourceID(other.m_sourceID)
    , m_buffer(std::move(other.m_buffer)) {
    other.m_sourceID = 0;
}

AudioSource& AudioSource::operator=(AudioSource&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_sourceID = other.m_sourceID;
        m_buffer = std::move(other.m_buffer);

        other.m_sourceID = 0;
    }
    return *this;
}

void AudioSource::setBuffer(std::shared_ptr<AudioBuffer> buffer) {
    m_buffer = buffer;
    if (buffer && buffer->isValid()) {
        alSourcei(m_sourceID, AL_BUFFER, buffer->getBufferID());
    } else {
        alSourcei(m_sourceID, AL_BUFFER, 0);
    }
}

void AudioSource::play() {
    alSourcePlay(m_sourceID);
}

void AudioSource::pause() {
    alSourcePause(m_sourceID);
}

void AudioSource::stop() {
    alSourceStop(m_sourceID);
}

void AudioSource::rewind() {
    alSourceRewind(m_sourceID);
}

bool AudioSource::isPlaying() const {
    ALint state;
    alGetSourcei(m_sourceID, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool AudioSource::isPaused() const {
    ALint state;
    alGetSourcei(m_sourceID, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

bool AudioSource::isStopped() const {
    ALint state;
    alGetSourcei(m_sourceID, AL_SOURCE_STATE, &state);
    return state == AL_STOPPED || state == AL_INITIAL;
}

void AudioSource::setLooping(bool loop) {
    alSourcei(m_sourceID, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

bool AudioSource::isLooping() const {
    ALint loop;
    alGetSourcei(m_sourceID, AL_LOOPING, &loop);
    return loop == AL_TRUE;
}

void AudioSource::setPosition(float x, float y, float z) {
    alSource3f(m_sourceID, AL_POSITION, x, y, z);
}

void AudioSource::setPosition(const std::array<float, 3>& position) {
    alSourcefv(m_sourceID, AL_POSITION, position.data());
}

std::array<float, 3> AudioSource::getPosition() const {
    std::array<float, 3> position;
    alGetSourcefv(m_sourceID, AL_POSITION, position.data());
    return position;
}

void AudioSource::setVelocity(float x, float y, float z) {
    alSource3f(m_sourceID, AL_VELOCITY, x, y, z);
}

void AudioSource::setVelocity(const std::array<float, 3>& velocity) {
    alSourcefv(m_sourceID, AL_VELOCITY, velocity.data());
}

std::array<float, 3> AudioSource::getVelocity() const {
    std::array<float, 3> velocity;
    alGetSourcefv(m_sourceID, AL_VELOCITY, velocity.data());
    return velocity;
}

void AudioSource::setPitch(float pitch) {
    alSourcef(m_sourceID, AL_PITCH, pitch);
}

float AudioSource::getPitch() const {
    float pitch;
    alGetSourcef(m_sourceID, AL_PITCH, &pitch);
    return pitch;
}

void AudioSource::setGain(float gain) {
    alSourcef(m_sourceID, AL_GAIN, gain);
}

float AudioSource::getGain() const {
    float gain;
    alGetSourcef(m_sourceID, AL_GAIN, &gain);
    return gain;
}

void AudioSource::setRolloffFactor(float rolloff) {
    alSourcef(m_sourceID, AL_ROLLOFF_FACTOR, rolloff);
}

float AudioSource::getRolloffFactor() const {
    float rolloff;
    alGetSourcef(m_sourceID, AL_ROLLOFF_FACTOR, &rolloff);
    return rolloff;
}

void AudioSource::setReferenceDistance(float distance) {
    alSourcef(m_sourceID, AL_REFERENCE_DISTANCE, distance);
}

float AudioSource::getReferenceDistance() const {
    float distance;
    alGetSourcef(m_sourceID, AL_REFERENCE_DISTANCE, &distance);
    return distance;
}

void AudioSource::setMaxDistance(float distance) {
    alSourcef(m_sourceID, AL_MAX_DISTANCE, distance);
}

float AudioSource::getMaxDistance() const {
    float distance;
    alGetSourcef(m_sourceID, AL_MAX_DISTANCE, &distance);
    return distance;
}

void AudioSource::setMinGain(float gain) {
    alSourcef(m_sourceID, AL_MIN_GAIN, gain);
}

float AudioSource::getMinGain() const {
    float gain;
    alGetSourcef(m_sourceID, AL_MIN_GAIN, &gain);
    return gain;
}

void AudioSource::setMaxGain(float gain) {
    alSourcef(m_sourceID, AL_MAX_GAIN, gain);
}

float AudioSource::getMaxGain() const {
    float gain;
    alGetSourcef(m_sourceID, AL_MAX_GAIN, &gain);
    return gain;
}

void AudioSource::setRelativeToListener(bool relative) {
    alSourcei(m_sourceID, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
}

bool AudioSource::isRelativeToListener() const {
    ALint relative;
    alGetSourcei(m_sourceID, AL_SOURCE_RELATIVE, &relative);
    return relative == AL_TRUE;
}

float AudioSource::getPlaybackPosition() const {
    float seconds;
    alGetSourcef(m_sourceID, AL_SEC_OFFSET, &seconds);
    return seconds;
}

void AudioSource::setPlaybackPosition(float seconds) {
    alSourcef(m_sourceID, AL_SEC_OFFSET, seconds);
}

void AudioSource::cleanup() {
    if (m_sourceID != 0) {
        alDeleteSources(1, &m_sourceID);
        m_sourceID = 0;
    }
}

} // namespace CatEngine
