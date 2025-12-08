#include "AudioBuffer.hpp"
#include <fstream>
#include <cstring>
#include <iostream>

namespace CatEngine {

AudioBuffer::AudioBuffer() {
    alGenBuffers(1, &m_bufferID);
}

AudioBuffer::~AudioBuffer() {
    cleanup();
}

AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
    : m_bufferID(other.m_bufferID)
    , m_duration(other.m_duration)
    , m_channels(other.m_channels)
    , m_sampleRate(other.m_sampleRate) {
    other.m_bufferID = 0;
    other.m_duration = 0.0f;
    other.m_channels = 0;
    other.m_sampleRate = 0;
}

AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_bufferID = other.m_bufferID;
        m_duration = other.m_duration;
        m_channels = other.m_channels;
        m_sampleRate = other.m_sampleRate;

        other.m_bufferID = 0;
        other.m_duration = 0.0f;
        other.m_channels = 0;
        other.m_sampleRate = 0;
    }
    return *this;
}

bool AudioBuffer::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open audio file: " << filepath << std::endl;
        return false;
    }

    // Get file size
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read entire file into memory
    std::vector<uint8_t> fileData(size);
    if (!file.read(reinterpret_cast<char*>(fileData.data()), size)) {
        std::cerr << "Failed to read audio file: " << filepath << std::endl;
        return false;
    }

    return parseWAV(fileData);
}

bool AudioBuffer::loadFromMemory(const void* data, size_t size, int channels,
                                 int sampleRate, int bitsPerSample) {
    if (!data || size == 0) {
        std::cerr << "Invalid audio data" << std::endl;
        return false;
    }

    ALenum format = getOpenALFormat(channels, bitsPerSample);
    if (format == 0) {
        std::cerr << "Unsupported audio format" << std::endl;
        return false;
    }

    alBufferData(m_bufferID, format, data, static_cast<ALsizei>(size), sampleRate);

    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error loading buffer: " << error << std::endl;
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;

    // Calculate duration
    int bytesPerSample = bitsPerSample / 8;
    int totalSamples = size / (channels * bytesPerSample);
    m_duration = static_cast<float>(totalSamples) / static_cast<float>(sampleRate);

    return true;
}

bool AudioBuffer::parseWAV(const std::vector<uint8_t>& fileData) {
    if (fileData.size() < sizeof(WAVHeader) + sizeof(DataChunk)) {
        std::cerr << "WAV file too small" << std::endl;
        return false;
    }

    size_t offset = 0;

    // Read RIFF header
    WAVHeader header;
    std::memcpy(&header, fileData.data() + offset, sizeof(WAVHeader));
    offset += sizeof(WAVHeader);

    // Validate RIFF header
    if (std::memcmp(header.riffID, "RIFF", 4) != 0 ||
        std::memcmp(header.riffType, "WAVE", 4) != 0) {
        std::cerr << "Invalid WAV file format" << std::endl;
        return false;
    }

    // Validate format
    if (std::memcmp(header.fmtID, "fmt ", 4) != 0) {
        std::cerr << "Missing format chunk" << std::endl;
        return false;
    }

    if (header.audioFormat != 1) {
        std::cerr << "Only PCM format is supported" << std::endl;
        return false;
    }

    // Skip any extra format bytes
    if (header.fmtSize > 16) {
        offset += (header.fmtSize - 16);
    }

    // Find data chunk (skip any other chunks)
    DataChunk dataChunk;
    bool foundData = false;

    while (offset + sizeof(DataChunk) <= fileData.size()) {
        std::memcpy(&dataChunk, fileData.data() + offset, sizeof(DataChunk));

        if (std::memcmp(dataChunk.dataID, "data", 4) == 0) {
            foundData = true;
            offset += sizeof(DataChunk);
            break;
        }

        // Skip this chunk
        offset += 8; // ID + size fields
        if (offset + 4 <= fileData.size()) {
            uint32_t chunkSize;
            std::memcpy(&chunkSize, fileData.data() + offset - 4, 4);
            offset += chunkSize;
        } else {
            break;
        }
    }

    if (!foundData) {
        std::cerr << "No data chunk found in WAV file" << std::endl;
        return false;
    }

    // Validate data size
    if (offset + dataChunk.dataSize > fileData.size()) {
        std::cerr << "Invalid data chunk size" << std::endl;
        return false;
    }

    // Load the audio data into OpenAL buffer
    const void* audioData = fileData.data() + offset;

    return loadFromMemory(audioData, dataChunk.dataSize,
                         header.numChannels, header.sampleRate,
                         header.bitsPerSample);
}

ALenum AudioBuffer::getOpenALFormat(int channels, int bitsPerSample) const {
    if (channels == 1) {
        if (bitsPerSample == 8) {
            return AL_FORMAT_MONO8;
        } else if (bitsPerSample == 16) {
            return AL_FORMAT_MONO16;
        }
    } else if (channels == 2) {
        if (bitsPerSample == 8) {
            return AL_FORMAT_STEREO8;
        } else if (bitsPerSample == 16) {
            return AL_FORMAT_STEREO16;
        }
    }
    return 0;
}

void AudioBuffer::cleanup() {
    if (m_bufferID != 0) {
        alDeleteBuffers(1, &m_bufferID);
        m_bufferID = 0;
    }
}

} // namespace CatEngine
