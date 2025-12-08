#pragma once

#include <AL/al.h>
#include <string>
#include <vector>
#include <cstdint>

namespace CatEngine {

/**
 * @brief Manages audio buffer data loaded from WAV files
 *
 * Wraps OpenAL buffer objects and provides WAV file loading
 * functionality without external dependencies.
 */
class AudioBuffer {
public:
    AudioBuffer();
    ~AudioBuffer();

    // Prevent copying
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;

    // Allow moving
    AudioBuffer(AudioBuffer&& other) noexcept;
    AudioBuffer& operator=(AudioBuffer&& other) noexcept;

    /**
     * @brief Load a WAV file into the audio buffer
     * @param filepath Path to the WAV file
     * @return true if successful, false otherwise
     */
    bool loadFromFile(const std::string& filepath);

    /**
     * @brief Load audio data directly into the buffer
     * @param data Raw audio data
     * @param size Size of the data in bytes
     * @param channels Number of channels (1=mono, 2=stereo)
     * @param sampleRate Sample rate in Hz
     * @param bitsPerSample Bits per sample (8 or 16)
     * @return true if successful, false otherwise
     */
    bool loadFromMemory(const void* data, size_t size, int channels,
                       int sampleRate, int bitsPerSample);

    /**
     * @brief Get the OpenAL buffer ID
     */
    ALuint getBufferID() const { return m_bufferID; }

    /**
     * @brief Check if buffer is valid
     */
    bool isValid() const { return m_bufferID != 0; }

    /**
     * @brief Get duration of the audio in seconds
     */
    float getDuration() const { return m_duration; }

    /**
     * @brief Get the number of channels
     */
    int getChannels() const { return m_channels; }

    /**
     * @brief Get the sample rate
     */
    int getSampleRate() const { return m_sampleRate; }

private:
    struct WAVHeader {
        // RIFF Header
        char riffID[4];          // "RIFF"
        uint32_t fileSize;       // File size - 8
        char riffType[4];        // "WAVE"

        // Format chunk
        char fmtID[4];           // "fmt "
        uint32_t fmtSize;        // Size of format chunk
        uint16_t audioFormat;    // Audio format (1 = PCM)
        uint16_t numChannels;    // Number of channels
        uint32_t sampleRate;     // Sample rate
        uint32_t byteRate;       // Byte rate
        uint16_t blockAlign;     // Block align
        uint16_t bitsPerSample;  // Bits per sample
    };

    struct DataChunk {
        char dataID[4];          // "data"
        uint32_t dataSize;       // Size of data
    };

    bool parseWAV(const std::vector<uint8_t>& fileData);
    ALenum getOpenALFormat(int channels, int bitsPerSample) const;
    void cleanup();

    ALuint m_bufferID = 0;
    float m_duration = 0.0f;
    int m_channels = 0;
    int m_sampleRate = 0;
};

} // namespace CatEngine
