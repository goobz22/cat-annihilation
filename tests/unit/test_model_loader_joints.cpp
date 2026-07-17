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

#include <array>
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
    // Per-vertex joint indices (3 vertices × 4 lanes). These are JOINT-SLOT
    // indices (positions in skin.joints), exactly what the glTF spec says
    // JOINTS_0 holds — NOT node indices.
    int joints[3][4] = {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 0, 3, 2}};
    // skin.joints — maps joint slot -> node index. Identity by default so
    // the componentType tests stay agnostic to the remap; the remap test
    // shuffles it.
    std::vector<int> skinJoints = {0, 1, 2, 3};
    // When true, the skin gains an inverseBindMatrices accessor whose slot-j
    // matrix carries translation x = -(j+1) — distinctive per slot so a test
    // can prove each IBM landed on the node skin.joints[j] points at.
    bool includeIbms = false;

    // When true, node 1 ("j1") carries an explicit column-major `matrix`
    // property instead of the default (no matrix -> identity localTransform).
    // Exercises the ExtractNodes matrix branch. `nodeMatrix` is stored in
    // glTF's required layout — COLUMN-MAJOR, element (row,col) at index
    // col*4+row (glTF 2.0 §3.7.3.1) — and is serialized in that same order.
    // The default value is deliberately ASYMMETRIC with a distinct
    // translation column: a symmetric or identity matrix is transpose-
    // invariant and would pass even a loader that transposes on load, so it
    // could never catch the transpose bug this fixture is here to pin.
    bool includeNodeMatrix = false;
    std::array<float, 16> nodeMatrix = {
        // column 0            column 1            column 2            column 3
        2.f, 0.f, 0.f, 0.f,  0.f, 3.f, 0.f, 0.f,  5.f, 0.f, 4.f, 0.f,  7.f, 8.f, 9.f, 1.f,
        // Notes on why these values catch a transpose:
        //   - index 8 (column 2, row 0) = 5 is off-diagonal; its mirror
        //     (column 0, row 2, index 2) = 0, so a transpose is observable.
        //   - indices 12/13/14 = translation (7,8,9) live in the LAST COLUMN;
        //     a transposed load scatters them into the bottom ROW instead.
    };

    // When >= 0, append this out-of-range index to the root node's children
    // array (nodes are 0..3, so 5 is OOB). Exercises the ExtractNodes child-
    // index bounds guard — without it, this is a controlled OOB heap write.
    int oobChildIndex = -1;
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

    // Optional inverse-bind-matrices section: one column-major mat4 per
    // joint slot, identity except translation.x = -(slot+1) so each matrix
    // is uniquely identifiable after the loader scatters them onto nodes.
    const size_t ibmsOffset = bin.size();
    if (spec.includeIbms) {
        for (size_t slot = 0; slot < spec.skinJoints.size(); ++slot) {
            glm::mat4 ibm{1.0f};
            ibm[3][0] = -static_cast<float>(slot + 1);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row) appendPod(bin, ibm[column][row]);
        }
    }
    const size_t ibmsLength = bin.size() - ibmsOffset;

    // The JSON chunk references the BIN sections above by byte offset.
    // Four nodes exist so every REMAPPED node index (max 3 whatever the
    // skin.joints permutation) is in-bounds for the one-bone-per-node
    // palette the entities build.
    const std::string jointsType = std::to_string(spec.jointsComponentType);
    const std::string weightsType = std::to_string(spec.weightsComponentType);
    const std::string weightsNormalized = spec.weightsComponentType == 5126 ? "" : ",\"normalized\":true";
    std::string skinJson = "\"joints\":[";
    for (size_t i = 0; i < spec.skinJoints.size(); ++i) {
        if (i) skinJson += ",";
        skinJson += std::to_string(spec.skinJoints[i]);
    }
    skinJson += "]";
    if (spec.includeIbms) skinJson += ",\"inverseBindMatrices\":4";

    // Node 1 ("j1") optionally carries an explicit `matrix`. The 16 floats
    // are serialized in the SAME order GlbSpec stores them, which IS glTF's
    // required column-major layout — so the on-disk JSON round-trips through
    // ExtractNodes exactly as the spec intends, with no re-index here.
    std::string node1Json = "{\"name\":\"j1\"";
    if (spec.includeNodeMatrix) {
        node1Json += ",\"matrix\":[";
        for (size_t element = 0; element < spec.nodeMatrix.size(); ++element) {
            if (element) node1Json += ",";
            node1Json += std::to_string(spec.nodeMatrix[element]);
        }
        node1Json += "]";
    }
    node1Json += "}";

    std::string json = std::string("{")
        + "\"asset\":{\"version\":\"2.0\"},"
        + "\"scene\":0,"
        + "\"scenes\":[{\"nodes\":[0]}],"
        + "\"nodes\":[{\"name\":\"root\",\"children\":[1,2,3"
        + (spec.oobChildIndex >= 0 ? "," + std::to_string(spec.oobChildIndex) : "")
        + "]}," + node1Json + ",{\"name\":\"j2\"},{\"name\":\"j3\"}],"
        + "\"skins\":[{" + skinJson + "}],"
        + "\"meshes\":[{\"name\":\"tri\",\"primitives\":[{"
        + "\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},\"indices\":3}]}],"
        + "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
        + "\"bufferViews\":["
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(positionsOffset) + ",\"byteLength\":36},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(jointsOffset) + ",\"byteLength\":" + std::to_string(jointsLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(weightsOffset) + ",\"byteLength\":" + std::to_string(weightsLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(indicesOffset) + ",\"byteLength\":6}"
        + (spec.includeIbms
               ? ",{\"buffer\":0,\"byteOffset\":" + std::to_string(ibmsOffset) +
                     ",\"byteLength\":" + std::to_string(ibmsLength) + "}"
               : "")
        + "],"
        + "\"accessors\":["
        + "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        + "{\"bufferView\":1,\"componentType\":" + jointsType + ",\"count\":3,\"type\":\"VEC4\"},"
        + "{\"bufferView\":2,\"componentType\":" + weightsType + ",\"count\":3,\"type\":\"VEC4\"" + weightsNormalized + "},"
        + "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        + (spec.includeIbms
               ? ",{\"bufferView\":4,\"componentType\":5126,\"count\":" +
                     std::to_string(spec.skinJoints.size()) + ",\"type\":\"MAT4\"}"
               : "")
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
            Catch::Matchers::Contains("200"));
}

TEST_CASE("JOINTS_0 slot indices remap through skin.joints to node indices",
          "[model-loader][joints][skin]") {
    // THE BUG THIS PINS (found 2026-07-17 by the headless harness — every
    // animated character rendered as a shredded mesh): glTF JOINTS_0 lanes
    // are indices into skin.joints (joint SLOTS), but the loader stored
    // them raw as if they were NODE indices. The shipped rigs have wildly
    // non-identity mappings (ember_leader.glb: joints=[34,33,20,19,...]),
    // so every vertex skinned against the wrong bones the moment a clip
    // played. Bind-pose renders hid it (that mode skips the palette).
    // The loader must remap slot -> node at load time so the node-indexed
    // downstream pipeline (skeleton bones == nodes) stays consistent.
    GlbSpec spec;
    spec.skinJoints = {2, 3, 1, 0};
    spec.includeIbms = true;
    TempGlb glb("cat_test_joints_remap.glb", spec);

    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    REQUIRE(model->skinJoints == std::vector<int>({2, 3, 1, 0}));
    const auto& vertices = model->meshes[0].vertices;
    REQUIRE(vertices.size() == 3);
    // Raw slots {0,1,2,3} -> nodes {2,3,1,0}, etc.
    CHECK(vertices[0].joints == glm::ivec4(2, 3, 1, 0));
    CHECK(vertices[1].joints == glm::ivec4(0, 1, 3, 2));
    CHECK(vertices[2].joints == glm::ivec4(3, 2, 0, 1));
}

TEST_CASE("skin inverseBindMatrices scatter onto the nodes skin.joints names",
          "[model-loader][joints][skin]") {
    // Companion half of the same 2026-07-17 outage: Node::inverseBindMatrix
    // was NEVER populated (identity for every bone), so even correctly
    // mapped joints would skin with palette = world(bone) * I — a double
    // transform. Slot j's fixture matrix carries translation.x = -(j+1);
    // after the scatter, node skin.joints[j] must hold it.
    GlbSpec spec;
    spec.skinJoints = {2, 3, 1, 0};
    spec.includeIbms = true;
    TempGlb glb("cat_test_skin_ibms.glb", spec);

    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    REQUIRE(model->nodes.size() == 4);
    CHECK(model->nodes[2].inverseBindMatrix[3][0] == Approx(-1.0f));  // slot 0
    CHECK(model->nodes[3].inverseBindMatrix[3][0] == Approx(-2.0f));  // slot 1
    CHECK(model->nodes[1].inverseBindMatrix[3][0] == Approx(-3.0f));  // slot 2
    CHECK(model->nodes[0].inverseBindMatrix[3][0] == Approx(-4.0f));  // slot 3
}

TEST_CASE("node matrix loads column-major without transposing", "[model-loader][nodes]") {
    // THE BUG THIS PINS (found 2026-07-17 in the parity audit): ExtractNodes
    // read the glTF node `matrix` as node.localTransform[c][r] = mat[r*4+c],
    // which TRANSPOSES the matrix. glTF 2.0 §3.7.3.1 stores `matrix` as a
    // 16-element COLUMN-MAJOR array (element (row,col) at index col*4+row),
    // and glm::mat4 is column-major with mat[col][row] indexing, so the
    // correct copy is localTransform[col][row] = mat[col*4+row].
    //
    // Every shipped asset that carries a node matrix carries the IDENTITY
    // matrix (props) while the rigs use TRS with no matrix, and
    // transpose(I) == I — so no shipped asset is mis-loaded today, but the
    // first asset authored with a real (rotated/translated) node matrix
    // would have loaded a corrupted bind pose that feeds skinning. The
    // fixture matrix is ASYMMETRIC with translation (7,8,9) in its last
    // column precisely so the transpose is observable: a symmetric matrix
    // would pass a broken loader unchanged.
    GlbSpec spec;
    spec.includeNodeMatrix = true;  // asymmetric matrix on node 1 ("j1")
    TempGlb glb("cat_test_node_matrix.glb", spec);

    auto model = CatEngine::ModelLoader::Load(glb.path.string());

    REQUIRE(model->nodes.size() == 4);
    const glm::mat4& transform = model->nodes[1].localTransform;

    // Translation lives in the LAST COLUMN of a column-major matrix, i.e.
    // localTransform[3][0..2]. The transposed loader put these three values
    // into the bottom ROW (localTransform[0..2][3]) instead, so each of
    // these fails on the old code (it read 0 there).
    CHECK(transform[3][0] == Approx(7.0f));  // translation.x
    CHECK(transform[3][1] == Approx(8.0f));  // translation.y
    CHECK(transform[3][2] == Approx(9.0f));  // translation.z

    // The asymmetric off-diagonal: glTF index 8 = (row 0, column 2) = 5.
    // A transpose would land it at localTransform[0][2] and leave [2][0]=0.
    CHECK(transform[2][0] == Approx(5.0f));

    // The bottom row must stay [0,0,0,1] for an affine node matrix. The
    // transposed loader wrote translation into [0][3]/[1][3]/[2][3], so this
    // block also fails on the old code.
    CHECK(transform[0][3] == Approx(0.0f));
    CHECK(transform[1][3] == Approx(0.0f));
    CHECK(transform[2][3] == Approx(0.0f));
    CHECK(transform[3][3] == Approx(1.0f));

    // Diagonal scale terms are transpose-invariant, but assert them so a
    // wholesale mis-read (e.g. off-by-one flattening) is also caught.
    CHECK(transform[0][0] == Approx(2.0f));
    CHECK(transform[1][1] == Approx(3.0f));
    CHECK(transform[2][2] == Approx(4.0f));
}

TEST_CASE("ExtractNodes rejects an out-of-range child index instead of an OOB write",
          "[model-loader][nodes][bounds]") {
    // THE BUG THIS PINS (2026-07-17 correctness audit): ExtractNodes wrote
    // `model.nodes[childIdx].parentIndex = i` with childIdx read straight
    // from the glTF `children` array and NO bounds check — the lone
    // unguarded index in the loader (JOINTS_0, skin joints, and triangle
    // indices all throw on OOB). model.nodes is sized once to the node count
    // and never grown, so a malformed/third-party glTF with a child index
    // past the node count was a controlled out-of-bounds HEAP WRITE (silent
    // corruption or SIGSEGV). The fix must convert that UB into the same
    // descriptive throw the sibling guards use, so the entities' load
    // try/catch degrades to a proxy cube.
    GlbSpec spec;                 // 4 nodes (root + j1 + j2 + j3)
    spec.oobChildIndex = 5;       // node 5 does not exist
    TempGlb glb("cat_test_node_oob_child.glb", spec);

    REQUIRE_THROWS_WITH(
        CatEngine::ModelLoader::Load(glb.path.string()),
        Catch::Matchers::Contains("child index") &&
            Catch::Matchers::Contains("5"));
}
