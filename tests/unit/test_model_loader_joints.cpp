// test_model_loader_joints.cpp — pins the glTF JOINTS_0 / WEIGHTS_0
// componentType contract in ModelLoader::ExtractMeshes.
//
// THE BUG THIS PINS (found 2026-07-16, live in every rigged Meshy asset):
// glTF stores JOINTS_0 as UNSIGNED_BYTE (5121) or UNSIGNED_SHORT (5123),
// but the loader extracted it with ExtractBufferData<glm::ivec4> — a raw
// 16-byte-per-vertex memcpy that is only correct for UNSIGNED_INT (5125).
// Reading a tightly-packed u8vec4 stream as i32x4 packs FOUR vertices'
// joints into one lane: bytes (0,1,2,3) become the single index 0x03020100
// = 50462976. The load-time joint-bound validation then (correctly)
// rejected the garbage — e.g. the shipped cat asset died with
//   "JOINTS_0 vertex 59 lane 0 references node 7706 but model only
//    declares 37 nodes"
// which took the player cat AND all four dog variants down to placeholder
// boxes in one stroke. Every rigged GLB in assets/models/*/rigged/ uses
// u8 joints (verified against the raw accessor JSON on 2026-07-16), so
// this was a total skinned-asset outage, not an edge case.
//
// The fixture builds minimal-but-valid GLB files from scratch (header +
// JSON chunk + BIN chunk) so the test is hermetic: it must not depend on
// the multi-megabyte art GLBs, which are gitignored and absent on CI.

#include "catch.hpp"

#include "assets/ModelLoader.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Appends a little-endian POD value to a byte vector. GLB is little-endian
// by spec; every dev/CI box this project targets is little-endian x86-64,
// so a memcpy of the host representation is the on-disk representation.
template <typename T>
void appendPod(std::vector<uint8_t>& bytes, const T& value) {
    const auto* raw = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

// Builds a complete GLB (glTF 2.0 binary container) holding one triangle
// with POSITION + JOINTS_0 + WEIGHTS_0 attributes and a 4-node scene, then
// writes it to `path`. The caller controls the joint/weight encodings so
// each test section can exercise a different componentType:
//
//   jointsComponentType : 5121 (u8), 5123 (u16), or 5125 (u32)
//   weightsComponentType: 5126 (f32) or 5121 (u8, normalized)
//
// Layout of the BIN chunk (all sections 4-byte aligned by construction):
//   positions  — 3 × vec3 f32              (36 bytes)
//   joints     — 3 × 4 lanes of the chosen width, padded to 4
//   weights    — 3 × 4 lanes of the chosen width, padded to 4
//   indices    — 3 × u16, padded to 4
struct GlbSpec {
    int jointsComponentType = 5121;
    int weightsComponentType = 5126;
    // Per-vertex joint indices (3 vertices × 4 lanes).
    int joints[3][4] = {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 0, 3, 2}};
};

void writeTestGlb(const std::filesystem::path& path, const GlbSpec& spec) {
    std::vector<uint8_t> bin;

    // Positions: unit triangle. Values are irrelevant to the joint logic
    // but POSITION is required for the loader to size the vertex array.
    const float positions[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const size_t positionsOffset = bin.size();
    for (float f : positions) appendPod(bin, f);

    // Joints in the requested width, tightly packed (byteStride omitted),
    // exactly how Meshy/Blender exporters emit them.
    const size_t jointsOffset = bin.size();
    for (const auto& vertexJoints : spec.joints) {
        for (int lane : vertexJoints) {
            if (spec.jointsComponentType == 5121) appendPod(bin, static_cast<uint8_t>(lane));
            else if (spec.jointsComponentType == 5123) appendPod(bin, static_cast<uint16_t>(lane));
            else appendPod(bin, static_cast<uint32_t>(lane));
        }
    }
    while (bin.size() % 4 != 0) bin.push_back(0);
    const size_t jointsLength = bin.size() - jointsOffset;

    // Weights: lane 0 carries full weight. In the u8-normalized encoding
    // 255 maps to 1.0 per the glTF normalized-accessor rules.
    const size_t weightsOffset = bin.size();
    for (int v = 0; v < 3; ++v) {
        for (int lane = 0; lane < 4; ++lane) {
            if (spec.weightsComponentType == 5126) appendPod(bin, lane == 0 ? 1.0f : 0.0f);
            else appendPod(bin, static_cast<uint8_t>(lane == 0 ? 255 : 0));
        }
    }
    while (bin.size() % 4 != 0) bin.push_back(0);
    const size_t weightsLength = bin.size() - weightsOffset;

    const size_t indicesOffset = bin.size();
    for (uint16_t i : {uint16_t{0}, uint16_t{1}, uint16_t{2}}) appendPod(bin, i);
    while (bin.size() % 4 != 0) bin.push_back(0);

    // The JSON chunk references the BIN sections above by byte offset.
    // Four nodes exist so every joint index in the default spec (max 3)
    // is in-bounds — the loader validates JOINTS_0 lanes against the node
    // count because skins aren't extracted yet and the bone palette is
    // sized one-bone-per-node.
    const std::string jointsType = std::to_string(spec.jointsComponentType);
    const std::string weightsType = std::to_string(spec.weightsComponentType);
    const std::string weightsNormalized = spec.weightsComponentType == 5126 ? "" : ",\"normalized\":true";
    std::string json = std::string("{")
        + "\"asset\":{\"version\":\"2.0\"},"
        + "\"scene\":0,"
        + "\"scenes\":[{\"nodes\":[0]}],"
        + "\"nodes\":[{\"name\":\"root\",\"children\":[1,2,3]},{\"name\":\"j1\"},{\"name\":\"j2\"},{\"name\":\"j3\"}],"
        + "\"skins\":[{\"joints\":[0,1,2,3]}],"
        + "\"meshes\":[{\"name\":\"tri\",\"primitives\":[{"
        + "\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},\"indices\":3}]}],"
        + "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
        + "\"bufferViews\":["
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(positionsOffset) + ",\"byteLength\":36},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(jointsOffset) + ",\"byteLength\":" + std::to_string(jointsLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(weightsOffset) + ",\"byteLength\":" + std::to_string(weightsLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(indicesOffset) + ",\"byteLength\":6}"
        + "],"
        + "\"accessors\":["
        + "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        + "{\"bufferView\":1,\"componentType\":" + jointsType + ",\"count\":3,\"type\":\"VEC4\"},"
        + "{\"bufferView\":2,\"componentType\":" + weightsType + ",\"count\":3,\"type\":\"VEC4\"" + weightsNormalized + "},"
        + "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        + "]}";
    while (json.size() % 4 != 0) json.push_back(' ');

    // GLB container: 12-byte header, then JSON chunk, then BIN chunk.
    std::vector<uint8_t> glb;
    appendPod(glb, uint32_t{0x46546C67});  // magic "glTF"
    appendPod(glb, uint32_t{2});           // container version
    appendPod(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    appendPod(glb, static_cast<uint32_t>(json.size()));
    appendPod(glb, uint32_t{0x4E4F534A});  // chunk type "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    appendPod(glb, static_cast<uint32_t>(bin.size()));
    appendPod(glb, uint32_t{0x004E4942});  // chunk type "BIN"
    glb.insert(glb.end(), bin.begin(), bin.end());

    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
}

// RAII wrapper so a REQUIRE failure mid-section can't leak temp GLBs into
// the temp dir across runs.
struct TempGlb {
    std::filesystem::path path;
    explicit TempGlb(const char* name, const GlbSpec& spec)
        : path(std::filesystem::temp_directory_path() / name) {
        writeTestGlb(path, spec);
    }
    ~TempGlb() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

}  // namespace

TEST_CASE("JOINTS_0 u8 (the Meshy rig encoding) parses to exact per-vertex indices", "[model-loader][joints]") {
    GlbSpec spec;  // defaults: u8 joints, f32 weights
    TempGlb glb("cat_test_joints_u8.glb", spec);

    // Before the componentType-aware fix this throws
    //   "JOINTS_0 vertex 0 lane 0 references node 50462976 ..."
    // because bytes (0,1,2,3) are read as one 32-bit index.
    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    REQUIRE(model->meshes.size() == 1);
    const auto& vertices = model->meshes[0].vertices;
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[0].joints == glm::ivec4(0, 1, 2, 3));
    CHECK(vertices[1].joints == glm::ivec4(3, 2, 1, 0));
    CHECK(vertices[2].joints == glm::ivec4(1, 0, 3, 2));
    // f32 weights must survive unchanged alongside the widened joints.
    CHECK(vertices[0].weights.x == Approx(1.0f));
    CHECK(vertices[0].weights.y == Approx(0.0f));
}

TEST_CASE("JOINTS_0 u16 parses to exact per-vertex indices", "[model-loader][joints]") {
    GlbSpec spec;
    spec.jointsComponentType = 5123;
    TempGlb glb("cat_test_joints_u16.glb", spec);

    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    const auto& vertices = model->meshes[0].vertices;
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[0].joints == glm::ivec4(0, 1, 2, 3));
    CHECK(vertices[2].joints == glm::ivec4(1, 0, 3, 2));
}

TEST_CASE("JOINTS_0 u32 keeps working (the one width the old reader handled)", "[model-loader][joints]") {
    GlbSpec spec;
    spec.jointsComponentType = 5125;
    TempGlb glb("cat_test_joints_u32.glb", spec);

    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    const auto& vertices = model->meshes[0].vertices;
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[1].joints == glm::ivec4(3, 2, 1, 0));
}

TEST_CASE("WEIGHTS_0 normalized u8 converts to [0,1] floats", "[model-loader][joints]") {
    GlbSpec spec;
    spec.weightsComponentType = 5121;  // normalized u8: 255 -> 1.0
    TempGlb glb("cat_test_weights_u8.glb", spec);

    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    const auto& vertices = model->meshes[0].vertices;
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[0].weights.x == Approx(1.0f));
    CHECK(vertices[0].weights.y == Approx(0.0f));
    CHECK(vertices[0].weights.z == Approx(0.0f));
    CHECK(vertices[0].weights.w == Approx(0.0f));
}

TEST_CASE("genuinely out-of-range joint index still refuses to load", "[model-loader][joints]") {
    // The load-time bound check exists to keep a corrupt skin from feeding
    // the shader's fixed 256-bone palette an OOB index (undefined reads or
    // device-lost on the GPU path). The componentType fix must not soften
    // it: a u8 joint index of 200 against a 4-node model is real corruption
    // and must still throw.
    GlbSpec spec;
    spec.joints[1][2] = 200;
    TempGlb glb("cat_test_joints_oob.glb", spec);

    REQUIRE_THROWS_WITH(
        CatEngine::ModelLoader::Load(glb.path.string()),
        Catch::Matchers::Contains("JOINTS_0") &&
            Catch::Matchers::Contains("references node 200"));
}
