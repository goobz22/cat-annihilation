#include "serialization.hpp"
#include <cstring>
#include <algorithm>
#include <iostream>

// Simple logging helpers
namespace {
    void logInfo(const std::string& msg) {
        std::cout << "[INFO] " << msg << std::endl;
    }

    void logError(const std::string& msg) {
        std::cerr << "[ERROR] " << msg << std::endl;
    }
}

namespace Engine {

// ============================================================================
// BinaryWriter Implementation
// ============================================================================

BinaryWriter::BinaryWriter(const std::string& path)
    : m_bytesWritten(0)
{
    m_file.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!m_file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }
    logInfo("BinaryWriter: Opened file for writing: " + path);
}

BinaryWriter::~BinaryWriter() {
    if (m_file.is_open()) {
        close();
    }
}

void BinaryWriter::write(const void* data, size_t size) {
    if (!m_file.is_open()) {
        throw std::runtime_error("BinaryWriter: File is not open");
    }

    m_file.write(static_cast<const char*>(data), size);
    if (!m_file.good()) {
        throw std::runtime_error("BinaryWriter: Failed to write data");
    }
    m_bytesWritten += size;
}

void BinaryWriter::writeString(const std::string& str) {
    uint32_t length = static_cast<uint32_t>(str.length());
    write(length);
    if (length > 0) {
        write(str.c_str(), length);
    }
}

void BinaryWriter::writeVec3(const vec3& v) {
    write(v.x);
    write(v.y);
    write(v.z);
}

void BinaryWriter::writeQuat(const Quaternion& q) {
    write(q.x);
    write(q.y);
    write(q.z);
    write(q.w);
}

void BinaryWriter::close() {
    if (m_file.is_open()) {
        m_file.close();
        logInfo("BinaryWriter: Closed file, wrote " + std::to_string(m_bytesWritten) + " bytes");
    }
}

size_t BinaryWriter::tell() const {
    if (!m_file.is_open()) {
        return 0;
    }
    return static_cast<size_t>(const_cast<std::ofstream&>(m_file).tellp());
}

void BinaryWriter::seek(size_t pos) {
    if (!m_file.is_open()) {
        throw std::runtime_error("BinaryWriter: File is not open");
    }
    m_file.seekp(pos);
}

// ============================================================================
// BinaryReader Implementation
// ============================================================================

BinaryReader::BinaryReader(const std::string& path)
    : m_bytesRead(0)
    , m_fileSize(0)
{
    m_file.open(path, std::ios::binary | std::ios::in);
    if (!m_file.is_open()) {
        throw std::runtime_error("Failed to open file for reading: " + path);
    }

    // Get file size
    m_file.seekg(0, std::ios::end);
    m_fileSize = static_cast<size_t>(m_file.tellg());
    m_file.seekg(0, std::ios::beg);

    logInfo("BinaryReader: Opened file for reading: " + path + " (" + std::to_string(m_fileSize) + " bytes)");
}

BinaryReader::~BinaryReader() {
    if (m_file.is_open()) {
        close();
    }
}

void BinaryReader::read(void* data, size_t size) {
    if (!m_file.is_open()) {
        throw std::runtime_error("BinaryReader: File is not open");
    }

    m_file.read(static_cast<char*>(data), size);
    if (!m_file.good() && !m_file.eof()) {
        throw std::runtime_error("BinaryReader: Failed to read data");
    }
    m_bytesRead += size;
}

std::string BinaryReader::readString() {
    uint32_t length = read<uint32_t>();
    if (length == 0) {
        return "";
    }

    std::string str(length, '\0');
    read(&str[0], length);
    return str;
}

vec3 BinaryReader::readVec3() {
    vec3 v;
    v.x = read<float>();
    v.y = read<float>();
    v.z = read<float>();
    return v;
}

Quaternion BinaryReader::readQuat() {
    Quaternion q;
    q.x = read<float>();
    q.y = read<float>();
    q.z = read<float>();
    q.w = read<float>();
    return q;
}

bool BinaryReader::eof() const {
    return m_file.eof() || (m_bytesRead >= m_fileSize);
}

void BinaryReader::close() {
    if (m_file.is_open()) {
        m_file.close();
        logInfo("BinaryReader: Closed file, read " + std::to_string(m_bytesRead) + " bytes");
    }
}

size_t BinaryReader::tell() const {
    if (!m_file.is_open()) {
        return 0;
    }
    return static_cast<size_t>(const_cast<std::ifstream&>(m_file).tellg());
}

void BinaryReader::seek(size_t pos) {
    if (!m_file.is_open()) {
        throw std::runtime_error("BinaryReader: File is not open");
    }
    m_file.seekg(pos);
}

size_t BinaryReader::size() const {
    return m_fileSize;
}

// ============================================================================
// CRC32 Implementation
// ============================================================================

// CRC32 lookup table
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void initCRC32Table() {
    if (crc32_table_initialized) {
        return;
    }

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

uint32_t calculateCRC32(const void* data, size_t size) {
    initCRC32Table();

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < size; i++) {
        uint8_t index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
    }

    return crc ^ 0xFFFFFFFF;
}

uint32_t calculateFileCRC32(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for CRC32 calculation: " + path);
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Read entire file
    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    return calculateCRC32(buffer.data(), fileSize);
}

// ============================================================================
// Compression — byte-level RLE
// ============================================================================
//
// Save files in this engine are small (typically a few KB of entity state
// plus quest/dialog flags), and the data is dominated by long runs of
// zeros/defaults across unused component fields. Byte-level RLE gives
// decent compression on that workload without pulling in a third-party
// dependency (LZ4, zlib), and its implementation fits in under a hundred
// lines. If save files ever get large enough that RLE's ~2x ratio hurts,
// swap this routine (and its matching decompress) for LZ4 — the
// compressedSize/data contract at the call boundary stays the same.

char* compressData(const char* data, size_t size, size_t& compressedSize) {
    // Run-Length Encoding: emit (byte, count) pairs where count is capped
    // at 255 so a single run fits in one uint8_t. A random-looking byte
    // stream produces 2:1 *expansion*, not compression, but save data in
    // this engine has enough repetition that the average case comes out
    // substantially smaller than the original.

    std::vector<char> compressed;
    compressed.reserve(size); // Worst case is same size

    size_t i = 0;
    while (i < size) {
        char current = data[i];
        uint8_t count = 1;

        // Count consecutive bytes (max 255)
        while (i + count < size && data[i + count] == current && count < 255) {
            count++;
        }

        compressed.push_back(current);
        compressed.push_back(static_cast<char>(count));

        i += count;
    }

    compressedSize = compressed.size();
    char* result = new char[compressedSize];
    std::memcpy(result, compressed.data(), compressedSize);

    logInfo("Compression: " + std::to_string(size) + " bytes -> " +
                 std::to_string(compressedSize) + " bytes (" +
                 std::to_string(100.0f * compressedSize / size) + "%)");

    return result;
}

char* decompressData(const char* compressedData, size_t compressedSize, size_t originalSize) {
    // Decompress RLE data
    char* result = new char[originalSize];
    size_t outPos = 0;
    size_t inPos = 0;

    while (inPos < compressedSize && outPos < originalSize) {
        char byte = compressedData[inPos++];
        uint8_t count = static_cast<uint8_t>(compressedData[inPos++]);

        for (uint8_t i = 0; i < count && outPos < originalSize; i++) {
            result[outPos++] = byte;
        }
    }

    if (outPos != originalSize) {
        delete[] result;
        throw std::runtime_error("Decompression failed: size mismatch");
    }

    logInfo("Decompression: " + std::to_string(compressedSize) + " bytes -> " +
                 std::to_string(originalSize) + " bytes");

    return result;
}

} // namespace Engine
