#include "AudioEngine.hpp"
#include <iostream>
#include <algorithm>

namespace CatEngine {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize(const char* deviceName) {
    if (m_initialized) {
        std::cerr << "Audio engine already initialized" << std::endl;
        return true;
    }

    // Open audio device
    m_device = alcOpenDevice(deviceName);
    if (!m_device) {
        std::cerr << "Failed to open audio device" << std::endl;
        return false;
    }

    // Create context
    m_context = alcCreateContext(m_device, nullptr);
    if (!m_context) {
        std::cerr << "Failed to create audio context" << std::endl;
        alcCloseDevice(m_device);
        m_device = nullptr;
        return false;
    }

    // Make context current
    if (!alcMakeContextCurrent(m_context)) {
        std::cerr << "Failed to make audio context current" << std::endl;
        alcDestroyContext(m_context);
        alcCloseDevice(m_device);
        m_context = nullptr;
        m_device = nullptr;
        return false;
    }

    // Set default distance model
    setDistanceModel(m_distanceModel);

    // Set default doppler settings
    alDopplerFactor(1.0f);
    alSpeedOfSound(343.3f); // Speed of sound in air at 20°C

    // Initialize listener to default position
    m_listener.setPosition(0.0f, 0.0f, 0.0f);
    m_listener.setOrientation({0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    m_listener.setVelocity(0.0f, 0.0f, 0.0f);

    m_initialized = true;

    std::cout << "Audio engine initialized successfully" << std::endl;
    std::cout << "Device: " << getCurrentDeviceName() << std::endl;
    std::cout << "Vendor: " << alGetString(AL_VENDOR) << std::endl;
    std::cout << "Renderer: " << alGetString(AL_RENDERER) << std::endl;
    std::cout << "Version: " << alGetString(AL_VERSION) << std::endl;

    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) {
        return;
    }

    // Stop all sources
    m_mixer.stopAll();

    // Clear all sources
    m_oneShotSources.clear();
    m_sources.clear();

    // Clear all buffers
    m_buffers.clear();

    // Destroy context
    if (m_context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(m_context);
        m_context = nullptr;
    }

    // Close device
    if (m_device) {
        alcCloseDevice(m_device);
        m_device = nullptr;
    }

    m_initialized = false;
    std::cout << "Audio engine shut down" << std::endl;
}

void AudioEngine::setDistanceModel(DistanceModel model) {
    m_distanceModel = model;
    alDistanceModel(convertDistanceModel(model));
}

void AudioEngine::setSpeedOfSound(float speed) {
    alSpeedOfSound(speed);
}

float AudioEngine::getSpeedOfSound() const {
    return alGetFloat(AL_SPEED_OF_SOUND);
}

void AudioEngine::setDopplerFactor(float factor) {
    alDopplerFactor(factor);
}

float AudioEngine::getDopplerFactor() const {
    return alGetFloat(AL_DOPPLER_FACTOR);
}

std::shared_ptr<AudioBuffer> AudioEngine::loadBuffer(const std::string& name, const std::string& filepath) {
    // Check if already loaded
    auto it = m_buffers.find(name);
    if (it != m_buffers.end()) {
        std::cout << "Buffer '" << name << "' already loaded" << std::endl;
        return it->second;
    }

    // Create and load buffer
    auto buffer = std::make_shared<AudioBuffer>();
    if (!buffer->loadFromFile(filepath)) {
        std::cerr << "Failed to load audio buffer: " << filepath << std::endl;
        return nullptr;
    }

    m_buffers[name] = buffer;
    std::cout << "Loaded audio buffer: " << name << " (" << buffer->getDuration() << "s)" << std::endl;
    return buffer;
}

std::shared_ptr<AudioBuffer> AudioEngine::getBuffer(const std::string& name) {
    auto it = m_buffers.find(name);
    if (it != m_buffers.end()) {
        return it->second;
    }
    return nullptr;
}

void AudioEngine::unloadBuffer(const std::string& name) {
    m_buffers.erase(name);
}

std::shared_ptr<AudioSource> AudioEngine::createSource() {
    auto source = std::make_shared<AudioSource>();
    m_sources.push_back(source);
    return source;
}

std::shared_ptr<AudioSource> AudioEngine::createSource(const std::string& bufferName) {
    auto source = createSource();
    auto buffer = getBuffer(bufferName);
    if (buffer) {
        source->setBuffer(buffer);
    }
    return source;
}

std::shared_ptr<AudioSource> AudioEngine::playSound(const std::string& bufferName,
                                                    const std::array<float, 3>& position,
                                                    float gain,
                                                    AudioMixer::Channel channel) {
    auto buffer = getBuffer(bufferName);
    if (!buffer) {
        std::cerr << "Buffer not found: " << bufferName << std::endl;
        return nullptr;
    }

    auto source = std::make_shared<AudioSource>();
    source->setBuffer(buffer);
    source->setPosition(position);
    source->setGain(gain);
    source->setLooping(false);

    // Register with mixer
    m_mixer.registerSource(source, channel);

    // Play
    source->play();

    // Add to one-shot sources for automatic cleanup
    m_oneShotSources.push_back(source);

    return source;
}

std::shared_ptr<AudioSource> AudioEngine::playSound2D(const std::string& bufferName,
                                                      float gain,
                                                      AudioMixer::Channel channel) {
    auto buffer = getBuffer(bufferName);
    if (!buffer) {
        std::cerr << "Buffer not found: " << bufferName << std::endl;
        return nullptr;
    }

    auto source = std::make_shared<AudioSource>();
    source->setBuffer(buffer);
    source->setGain(gain);
    source->setLooping(false);
    source->setRelativeToListener(true);
    source->setPosition(0.0f, 0.0f, 0.0f);

    // Register with mixer
    m_mixer.registerSource(source, channel);

    // Play
    source->play();

    // Add to one-shot sources for automatic cleanup
    m_oneShotSources.push_back(source);

    return source;
}

void AudioEngine::update() {
    // Remove finished one-shot sources
    m_oneShotSources.erase(
        std::remove_if(m_oneShotSources.begin(), m_oneShotSources.end(),
            [this](const std::shared_ptr<AudioSource>& source) {
                if (source->isStopped()) {
                    m_mixer.unregisterSource(source);
                    return true;
                }
                return false;
            }),
        m_oneShotSources.end()
    );

    // Clean up expired weak pointers in sources list
    m_sources.erase(
        std::remove_if(m_sources.begin(), m_sources.end(),
            [](const std::shared_ptr<AudioSource>& source) {
                return source.use_count() == 1; // Only we hold a reference
            }),
        m_sources.end()
    );
}

std::vector<std::string> AudioEngine::getAvailableDevices() const {
    std::vector<std::string> devices;

    // Check if enumeration is supported
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT") == AL_TRUE) {
        const char* deviceList = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);

        // Device list is a series of null-terminated strings, ending with a double null
        while (deviceList && *deviceList) {
            devices.emplace_back(deviceList);
            deviceList += devices.back().length() + 1;
        }
    }

    return devices;
}

std::string AudioEngine::getCurrentDeviceName() const {
    if (m_device) {
        const char* name = alcGetString(m_device, ALC_DEVICE_SPECIFIER);
        return name ? name : "Unknown";
    }
    return "No device";
}

bool AudioEngine::checkError(const std::string& context) const {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL Error";
        if (!context.empty()) {
            std::cerr << " (" << context << ")";
        }
        std::cerr << ": ";

        switch (error) {
            case AL_INVALID_NAME:
                std::cerr << "Invalid name";
                break;
            case AL_INVALID_ENUM:
                std::cerr << "Invalid enum";
                break;
            case AL_INVALID_VALUE:
                std::cerr << "Invalid value";
                break;
            case AL_INVALID_OPERATION:
                std::cerr << "Invalid operation";
                break;
            case AL_OUT_OF_MEMORY:
                std::cerr << "Out of memory";
                break;
            default:
                std::cerr << "Unknown error (" << error << ")";
                break;
        }
        std::cerr << std::endl;
        return true;
    }

    // Check context errors
    if (m_device) {
        ALCenum alcError = alcGetError(m_device);
        if (alcError != ALC_NO_ERROR) {
            std::cerr << "OpenAL Context Error";
            if (!context.empty()) {
                std::cerr << " (" << context << ")";
            }
            std::cerr << ": ";

            switch (alcError) {
                case ALC_INVALID_DEVICE:
                    std::cerr << "Invalid device";
                    break;
                case ALC_INVALID_CONTEXT:
                    std::cerr << "Invalid context";
                    break;
                case ALC_INVALID_ENUM:
                    std::cerr << "Invalid enum";
                    break;
                case ALC_INVALID_VALUE:
                    std::cerr << "Invalid value";
                    break;
                case ALC_OUT_OF_MEMORY:
                    std::cerr << "Out of memory";
                    break;
                default:
                    std::cerr << "Unknown error (" << alcError << ")";
                    break;
            }
            std::cerr << std::endl;
            return true;
        }
    }

    return false;
}

ALenum AudioEngine::convertDistanceModel(DistanceModel model) const {
    switch (model) {
        case DistanceModel::None:
            return AL_NONE;
        case DistanceModel::Inverse:
            return AL_INVERSE_DISTANCE;
        case DistanceModel::InverseClamped:
            return AL_INVERSE_DISTANCE_CLAMPED;
        case DistanceModel::Linear:
            return AL_LINEAR_DISTANCE;
        case DistanceModel::LinearClamped:
            return AL_LINEAR_DISTANCE_CLAMPED;
        case DistanceModel::Exponential:
            return AL_EXPONENT_DISTANCE;
        case DistanceModel::ExponentialClamped:
            return AL_EXPONENT_DISTANCE_CLAMPED;
        default:
            return AL_INVERSE_DISTANCE_CLAMPED;
    }
}

} // namespace CatEngine
