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
#include <cstring>
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

TEST_CASE("decompressData rejects an odd compressed size instead of over-reading",
          "[serialization][security][decompress]") {
    // Round-3 audit (2026-07-17), MED. The RLE decompressData reads a
    // (byte,count) PAIR per iteration but checked `inPos < compressedSize` only
    // ONCE per iteration, so an ODD compressedSize entered a final iteration
    // that read compressedData[compressedSize] — one byte PAST the buffer —
    // from attacker-controlled save data and BEFORE the CRC32 gate in
    // loadFromFile. A valid RLE stream is always even (pair-encoded), so an odd
    // length is inherently corrupt and must fail gracefully, not over-read.
    //
    // The backing buffer is 1 byte LONGER than the declared compressedSize so
    // the pre-fix over-read lands on a known sentinel rather than true UB, while
    // still proving the loop reads past compressedSize. Layout: 'A',2 (a valid
    // pair) then a lone 'B' with NO paired count -> odd compressedSize = 3.
    std::vector<char> backing = {'A', 2, 'B', /*sentinel past compressedSize*/ 99};
    const size_t compressedSize = 3;  // odd
    const size_t originalSize = 3;

    // Post-fix: the loop requires a full pair, stops after 'A',2 (outPos=2 !=
    // originalSize=3) and throws "size mismatch". Pre-fix: it read backing[3]
    // (past compressedSize) as the count and completed originalSize WITHOUT
    // throwing, so this REQUIRE_THROWS fails first when the guard is reverted.
    REQUIRE_THROWS(Engine::decompressData(backing.data(), compressedSize, originalSize));

    // Positive control: a valid stream round-trips and its compressed size is
    // always even, so the guard never rejects a legitimate stream.
    const char original[] = {'X', 'X', 'X', 'Y', 'Z', 'Z'};
    size_t cs = 0;
    char* compressed = Engine::compressData(original, sizeof(original), cs);
    REQUIRE(cs % 2 == 0);
    char* roundtrip = Engine::decompressData(compressed, cs, sizeof(original));
    REQUIRE(std::memcmp(roundtrip, original, sizeof(original)) == 0);
    delete[] compressed;
    delete[] roundtrip;
}
