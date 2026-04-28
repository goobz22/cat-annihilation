/**
 * Unit tests for CatEngine::Base64DataUri.
 *
 * Why this file exists:
 *   Every shipping .gltf in assets/models/ (cat, dogs, trees, etc.) stores
 *   its mesh bytes inline as a base64 `data:` URI instead of a separate .bin
 *   sidecar. ModelLoader used to concatenate `baseDir + uri` and hand the
 *   result to std::ifstream — which failed noisily and silently dropped the
 *   model, so the running game showed placeholder geometry instead of the
 *   actual cat. The Base64DataUri helper fixed that. This test pins the
 *   decoder's behaviour so regressions would show up at `unit_tests` time
 *   rather than the next time someone launches the game and wonders why the
 *   player cat is invisible again.
 *
 * Coverage:
 *   1. Round-trip a known-value payload (RFC 4648 §10 test vectors) and
 *      assert the decoded bytes match the plaintext.
 *   2. Both forms of padding (`=` and `==`) decode to the right length.
 *   3. Line-wrapped base64 (CRLF/LF between groups) decodes correctly — some
 *      third-party pipelines emit wrapped payloads.
 *   4. `IsDataUri` discriminates between `data:` URIs and relative paths
 *      without allocating or throwing.
 *   5. The decoder rejects malformed input (not a data URI, missing comma,
 *      not declared base64, invalid char) with std::runtime_error so the
 *      caller's try/catch prints a clear diagnosis.
 *   6. A minimal glTF-shaped payload decodes to the same bytes the engine
 *      would see if it loaded an external .bin sidecar — this is the
 *      end-to-end guarantee ModelLoader.cpp relies on.
 */

#include "catch.hpp"
#include "engine/assets/Base64DataUri.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace CatEngine;

namespace {

// Small helper so the assertions below read `BytesEq({...}, decoded)` rather
// than having to spell out the vector comparison at every call site.
bool BytesEq(std::vector<uint8_t> expected, const std::vector<uint8_t>& actual) {
    return expected == actual;
}

}  // namespace

TEST_CASE("Base64DataUri::IsDataUri discriminates data URIs from paths",
          "[base64][data_uri]") {
    REQUIRE(Base64DataUri::IsDataUri("data:application/octet-stream;base64,AA=="));
    REQUIRE(Base64DataUri::IsDataUri("data:,"));  // shortest legal data URI
    REQUIRE_FALSE(Base64DataUri::IsDataUri("assets/models/cat.bin"));
    REQUIRE_FALSE(Base64DataUri::IsDataUri("http://example.com/a.bin"));
    REQUIRE_FALSE(Base64DataUri::IsDataUri(""));
    REQUIRE_FALSE(Base64DataUri::IsDataUri("data"));     // missing ':'
    REQUIRE_FALSE(Base64DataUri::IsDataUri("Data:"));    // case-sensitive per RFC
}

TEST_CASE("Base64DataUri round-trips RFC 4648 test vectors",
          "[base64][data_uri]") {
    // Vectors taken from RFC 4648 §10. Mime is irrelevant to the decode path,
    // but supplying ";base64" is required — the helper refuses non-base64
    // data URIs for security reasons documented in the header.

    SECTION("empty payload decodes to empty bytes") {
        auto bytes = Base64DataUri::DecodeBase64("data:application/octet-stream;base64,");
        REQUIRE(bytes.empty());
    }

    SECTION("\"f\" → \"Zg==\" (two-byte padding)") {
        auto bytes = Base64DataUri::DecodeBase64("data:text/plain;base64,Zg==");
        REQUIRE(BytesEq({'f'}, bytes));
    }

    SECTION("\"fo\" → \"Zm8=\" (one-byte padding)") {
        auto bytes = Base64DataUri::DecodeBase64("data:text/plain;base64,Zm8=");
        REQUIRE(BytesEq({'f', 'o'}, bytes));
    }

    SECTION("\"foo\" → \"Zm9v\" (no padding)") {
        auto bytes = Base64DataUri::DecodeBase64("data:text/plain;base64,Zm9v");
        REQUIRE(BytesEq({'f', 'o', 'o'}, bytes));
    }

    SECTION("\"foobar\" → \"Zm9vYmFy\"") {
        auto bytes = Base64DataUri::DecodeBase64(
            "data:application/octet-stream;base64,Zm9vYmFy");
        REQUIRE(BytesEq({'f', 'o', 'o', 'b', 'a', 'r'}, bytes));
    }
}

TEST_CASE("Base64DataUri tolerates whitespace in the payload",
          "[base64][data_uri]") {
    // Some generators wrap the base64 every 64 (MIME) or 76 (PEM) columns.
    // glTF does not, but refusing a valid RFC 4648 stream because it happens
    // to contain a newline would be gratuitously strict.
    auto bytes = Base64DataUri::DecodeBase64(
        "data:application/octet-stream;base64,Zm9v\nYmFy");
    REQUIRE(BytesEq({'f', 'o', 'o', 'b', 'a', 'r'}, bytes));

    auto bytesCrlf = Base64DataUri::DecodeBase64(
        "data:application/octet-stream;base64,Zm9v\r\nYmFy");
    REQUIRE(BytesEq({'f', 'o', 'o', 'b', 'a', 'r'}, bytesCrlf));

    auto bytesSpaces = Base64DataUri::DecodeBase64(
        "data:application/octet-stream;base64, Zm9v\tYmFy ");
    REQUIRE(BytesEq({'f', 'o', 'o', 'b', 'a', 'r'}, bytesSpaces));
}

TEST_CASE("Base64DataUri decodes binary payloads that would appear in glTF",
          "[base64][data_uri]") {
    // Four little-endian floats (0.0, 0.5, 1.0, -1.0) = 16 bytes.
    // Base64 of that byte sequence computed out-of-band and checked against
    // Python's `base64.b64encode`. The aim here is to verify the decoder
    // works on non-ASCII bytes — glTF buffers are raw float/index data, not
    // text, and a subtle off-by-one in the bit accumulator would produce
    // plausible-but-wrong floats that pass "size matches" but fail rendering.
    const std::vector<uint8_t> expectedBytes = {
        0x00, 0x00, 0x00, 0x00,  // 0.0f
        0x00, 0x00, 0x00, 0x3F,  // 0.5f
        0x00, 0x00, 0x80, 0x3F,  // 1.0f
        0x00, 0x00, 0x80, 0xBF,  // -1.0f
    };
    const std::string uri =
        "data:application/octet-stream;base64,AAAAAAAAAD8AAIA/AACAvw==";

    auto bytes = Base64DataUri::DecodeBase64(uri);
    REQUIRE(bytes.size() == expectedBytes.size());
    REQUIRE(BytesEq(expectedBytes, bytes));
}

TEST_CASE("Base64DataUri rejects malformed input with a clear error",
          "[base64][data_uri]") {
    SECTION("not a data URI") {
        REQUIRE_THROWS_AS(Base64DataUri::DecodeBase64("assets/cat.bin"),
                          std::runtime_error);
    }

    SECTION("missing comma separator") {
        REQUIRE_THROWS_AS(
            Base64DataUri::DecodeBase64("data:application/octet-stream;base64"),
            std::runtime_error);
    }

    SECTION("not a base64 URI (refuses percent-encoded text)") {
        REQUIRE_THROWS_AS(
            Base64DataUri::DecodeBase64("data:text/plain,hello%20world"),
            std::runtime_error);
    }

    SECTION("invalid base64 character in payload") {
        REQUIRE_THROWS_AS(
            Base64DataUri::DecodeBase64(
                "data:application/octet-stream;base64,Zm9v!!!"),
            std::runtime_error);
    }
}
