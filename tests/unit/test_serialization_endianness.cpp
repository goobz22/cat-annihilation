/**
 * Unit tests for engine/core/serialization.hpp endianness contract.
 *
 * BinaryWriter/Reader write native-byte-order bytes with no per-field
 * swap. A static_assert at the top of serialization.hpp enforces
 * little-endian-host so a future big-endian target is REQUIRED to wire
 * up byte-swap helpers before it can build (rather than silently
 * shipping incompatible save files whose CRC32 still validates because
 * the integrity hash is computed on the same byte stream that the
 * loader will later deserialize wrong).
 *
 * The test below verifies the round-trip and the byte-order observable
 * on disk so a refactor that adds a byte-swap path (because somebody is
 * actually porting to BE) must keep round-trip behaviour identical on
 * the LE host.
 */

#include "catch.hpp"
#include "engine/core/serialization.hpp"
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using Engine::BinaryReader;
using Engine::BinaryWriter;

TEST_CASE("Host is little-endian (compile-time contract holds at runtime)",
          "[serialization][endianness]") {
    // Belt-and-braces runtime mirror of the static_assert in
    // serialization.hpp. If this ever fires we want a Catch2 failure with
    // the file:line, not a cryptic linker error.
    REQUIRE(std::endian::native == std::endian::little);
}

TEST_CASE("BinaryWriter writes uint32_t in little-endian byte order on disk",
          "[serialization][endianness]") {
    const auto tmpPath = std::filesystem::temp_directory_path() /
                         "cat_annihilation_endianness_test.bin";
    std::error_code cleanupErr;
    std::filesystem::remove(tmpPath, cleanupErr);

    {
        BinaryWriter writer(tmpPath.string());
        const uint32_t value = 0x12345678u;
        writer.write(value);
        writer.close();
    }

    // Read the raw bytes and assert the LE layout: lowest byte first.
    // This is the format the save loader on every shipping platform
    // expects; the static_assert above prevents a BE host from
    // accidentally producing the reversed layout.
    std::ifstream raw(tmpPath, std::ios::binary);
    std::vector<unsigned char> bytes(
        std::istreambuf_iterator<char>(raw), {});

    REQUIRE(bytes.size() == 4);
    REQUIRE(bytes[0] == 0x78);
    REQUIRE(bytes[1] == 0x56);
    REQUIRE(bytes[2] == 0x34);
    REQUIRE(bytes[3] == 0x12);

    std::filesystem::remove(tmpPath, cleanupErr);
}

TEST_CASE("BinaryReader round-trips primitive types byte-exact",
          "[serialization][endianness]") {
    const auto tmpPath = std::filesystem::temp_directory_path() /
                         "cat_annihilation_endianness_roundtrip.bin";
    std::error_code cleanupErr;
    std::filesystem::remove(tmpPath, cleanupErr);

    const int32_t  intIn   = -42;
    const uint64_t longIn  = 0x0123456789ABCDEFull;
    const float    floatIn = 3.14159f;

    {
        BinaryWriter writer(tmpPath.string());
        writer.write(intIn);
        writer.write(longIn);
        writer.write(floatIn);
        writer.close();
    }

    {
        BinaryReader reader(tmpPath.string());
        const auto intOut   = reader.read<int32_t>();
        const auto longOut  = reader.read<uint64_t>();
        const auto floatOut = reader.read<float>();
        reader.close();

        REQUIRE(intOut == intIn);
        REQUIRE(longOut == longIn);
        REQUIRE(floatOut == floatIn);
    }

    std::filesystem::remove(tmpPath, cleanupErr);
}
