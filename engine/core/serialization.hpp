#ifndef ENGINE_CORE_SERIALIZATION_HPP
#define ENGINE_CORE_SERIALIZATION_HPP

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include "../math/Vector.hpp"
#include "../math/Quaternion.hpp"

namespace Engine {

/**
 * Binary writer for save file serialization
 * Provides type-safe methods to write data to a binary file
 * with support for compression and checksums
 */
class BinaryWriter {
public:
    /**
     * Create a binary writer for the specified file path
     * @param path File path to write to
     * @throws std::runtime_error if file cannot be opened
     */
    explicit BinaryWriter(const std::string& path);

    /**
     * Destructor - automatically closes the file
     */
    ~BinaryWriter();

    /**
     * Write raw binary data
     * @param data Pointer to data to write
     * @param size Number of bytes to write
     */
    void write(const void* data, size_t size);

    /**
     * Write a string (writes length prefix + data)
     * @param str String to write
     */
    void writeString(const std::string& str);

    /**
     * Write a 3D vector
     * @param v Vector to write
     */
    void writeVec3(const vec3& v);

    /**
     * Write a quaternion
     * @param q Quaternion to write
     */
    void writeQuat(const Quaternion& q);

    /**
     * Write a primitive type value
     * @param value Value to write
     */
    template<typename T>
    typename std::enable_if<std::is_arithmetic<T>::value, void>::type
    write(const T& value) {
        write(&value, sizeof(T));
    }

    /**
     * Write a vector of values
     * @param vec Vector to write
     */
    template<typename T>
    void writeVector(const std::vector<T>& vec) {
        uint32_t size = static_cast<uint32_t>(vec.size());
        write(size);
        for (const auto& item : vec) {
            writeItem(item);
        }
    }

    /**
     * Write a map
     * @param map Map to write
     */
    template<typename K, typename V>
    void writeMap(const std::map<K, V>& map) {
        uint32_t size = static_cast<uint32_t>(map.size());
        write(size);
        for (const auto& [key, value] : map) {
            writeItem(key);
            writeItem(value);
        }
    }

    /**
     * Close the file
     */
    void close();

    /**
     * Check if the writer is open
     * @return true if file is open
     */
    bool isOpen() const { return m_file.is_open(); }

    /**
     * Get the current write position
     * @return Current position in bytes
     */
    size_t tell() const;

    /**
     * Seek to a specific position
     * @param pos Position to seek to
     */
    void seek(size_t pos);

private:
    std::ofstream m_file;
    size_t m_bytesWritten;

    // Helper to write different types of items
    template<typename T>
    void writeItem(const T& item) {
        if constexpr (std::is_same_v<T, std::string>) {
            writeString(item);
        } else if constexpr (std::is_same_v<T, vec3>) {
            writeVec3(item);
        } else if constexpr (std::is_same_v<T, Quaternion>) {
            writeQuat(item);
        } else if constexpr (std::is_arithmetic_v<T>) {
            write(item);
        } else {
            // For complex types, assume they have a serialize method
            const_cast<T&>(item).serialize(*this);
        }
    }
};

/**
 * Binary reader for save file deserialization
 * Provides type-safe methods to read data from a binary file
 */
class BinaryReader {
public:
    /**
     * Create a binary reader for the specified file path
     * @param path File path to read from
     * @throws std::runtime_error if file cannot be opened
     */
    explicit BinaryReader(const std::string& path);

    /**
     * Destructor - automatically closes the file
     */
    ~BinaryReader();

    /**
     * Read raw binary data
     * @param data Pointer to buffer to read into
     * @param size Number of bytes to read
     */
    void read(void* data, size_t size);

    /**
     * Read a string
     * @return String read from file
     */
    std::string readString();

    /**
     * Read a 3D vector
     * @return Vector read from file
     */
    vec3 readVec3();

    /**
     * Read a quaternion
     * @return Quaternion read from file
     */
    Quaternion readQuat();

    /**
     * Read a primitive type value
     * @return Value read from file
     */
    template<typename T>
    typename std::enable_if<std::is_arithmetic<T>::value, T>::type
    read() {
        T value;
        read(&value, sizeof(T));
        return value;
    }

    /**
     * Read a vector of values
     * @return Vector read from file
     */
    template<typename T>
    std::vector<T> readVector() {
        uint32_t size = read<uint32_t>();
        std::vector<T> vec;
        vec.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            vec.push_back(readItem<T>());
        }
        return vec;
    }

    /**
     * Read a map
     * @return Map read from file
     */
    template<typename K, typename V>
    std::map<K, V> readMap() {
        uint32_t size = read<uint32_t>();
        std::map<K, V> map;
        for (uint32_t i = 0; i < size; ++i) {
            K key = readItem<K>();
            V value = readItem<V>();
            map[key] = value;
        }
        return map;
    }

    /**
     * Check if end of file is reached
     * @return true if at end of file
     */
    bool eof() const;

    /**
     * Close the file
     */
    void close();

    /**
     * Check if the reader is open
     * @return true if file is open
     */
    bool isOpen() const { return m_file.is_open(); }

    /**
     * Get the current read position
     * @return Current position in bytes
     */
    size_t tell() const;

    /**
     * Seek to a specific position
     * @param pos Position to seek to
     */
    void seek(size_t pos);

    /**
     * Get the total file size
     * @return File size in bytes
     */
    size_t size() const;

private:
    std::ifstream m_file;
    size_t m_bytesRead;
    size_t m_fileSize;

    // Helper to read different types of items
    template<typename T>
    T readItem() {
        if constexpr (std::is_same_v<T, std::string>) {
            return readString();
        } else if constexpr (std::is_same_v<T, vec3>) {
            return readVec3();
        } else if constexpr (std::is_same_v<T, Quaternion>) {
            return readQuat();
        } else if constexpr (std::is_arithmetic_v<T>) {
            return read<T>();
        } else {
            // For complex types, assume they have a deserialize method
            T item;
            item.deserialize(*this);
            return item;
        }
    }
};

/**
 * Calculate CRC32 checksum for data integrity verification
 * @param data Pointer to data
 * @param size Size of data in bytes
 * @return CRC32 checksum
 */
uint32_t calculateCRC32(const void* data, size_t size);

/**
 * Calculate CRC32 checksum for a file
 * @param path File path
 * @return CRC32 checksum
 */
uint32_t calculateFileCRC32(const std::string& path);

/**
 * Compress data using LZ4 compression
 * @param data Input data
 * @param size Input size
 * @param compressedSize Output: compressed size
 * @return Compressed data (caller must delete[])
 */
char* compressData(const char* data, size_t size, size_t& compressedSize);

/**
 * Decompress LZ4 compressed data
 * @param compressedData Compressed input data
 * @param compressedSize Compressed size
 * @param originalSize Original uncompressed size
 * @return Decompressed data (caller must delete[])
 */
char* decompressData(const char* compressedData, size_t compressedSize, size_t originalSize);

} // namespace Engine

#endif // ENGINE_CORE_SERIALIZATION_HPP
