#include "ModelLoader.hpp"
#include "Base64DataUri.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

// stb_image is the same library TextureLoader.cpp implements (with the
// STB_IMAGE_IMPLEMENTATION macro). Including without that macro pulls in
// just the declarations — we re-use the implementation that already lives
// in the binary, no second copy. We only call stbi_load_from_memory and
// stbi_image_free here, both of which are pure C functions over the
// callback table stb_image declares — safe to share across translation
// units in this single-binary build.
#include <stb_image.h>

using json = nlohmann::json;

namespace CatEngine {

// Internal glTF data structures
struct ModelLoader::GLTFData {
    json root;
    std::vector<std::vector<uint8_t>> buffers;
    std::string baseDir;
};

std::shared_ptr<Model> ModelLoader::Load(const std::string& path) {
    // Determine file type by extension
    if (path.ends_with(".glb")) {
        return LoadGLB(path);
    } else if (path.ends_with(".gltf")) {
        return LoadGLTF(path);
    } else {
        throw std::runtime_error("Unsupported model format: " + path);
    }
}

std::shared_ptr<Model> ModelLoader::LoadGLTF(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    GLTFData data;
    data.root = json::parse(content);

    // Extract base directory for relative paths
    size_t lastSlash = path.find_last_of("/\\");
    data.baseDir = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";

    // Load buffers.
    //
    // glTF 2.0 §3.6.1.1 allows two buffer URI flavours:
    //   (1) A relative path to an external .bin sidecar (classic two-file gltf).
    //   (2) An inline base64 `data:` URI embedding the buffer directly in the
    //       JSON. Every .gltf shipped in assets/models/ uses flavour (2) — our
    //       generator produces self-contained single-file assets.
    //
    // WHY both branches are required: the cat + dog + props the game ships with
    // are all flavour (2), so without the data-URI decoder the player is a
    // placeholder cube on every playtest. But the engine also needs to load
    // third-party models authored in Blender/Maya, which emit flavour (1) by
    // default. A real glTF loader handles both; dropping either path would
    // quietly break one of the two asset pipelines.
    //
    // WHY the `byteLength` sanity check below (when the field is present):
    // the RFC-4648 decoder in Base64DataUri is intentionally tolerant of
    // whitespace and strips `=` padding, so a malformed URI could decode to
    // fewer bytes than the glTF JSON claims. ExtractMeshes would then index
    // past the end of `data.buffers[…]` — deterministic out-of-bounds UB.
    // Catching the size mismatch here gives a clear "buffer 0: expected N
    // bytes, decoded M" error instead of a segfault deep in vertex extract.
    if (data.root.contains("buffers")) {
        for (size_t bufferIndex = 0; bufferIndex < data.root["buffers"].size(); ++bufferIndex) {
            const auto& bufferInfo = data.root["buffers"][bufferIndex];
            std::string uri = bufferInfo.value("uri", std::string());

            std::vector<uint8_t> bufferData;

            if (Base64DataUri::IsDataUri(uri)) {
                // Flavour (2): inline base64 `data:` URI. Decode directly —
                // the decoder is header-only and dependency-free, so this
                // path is testable in the no-GPU Catch2 build.
                bufferData = Base64DataUri::DecodeBase64(uri);
            } else {
                // Flavour (1): external sidecar file relative to the .gltf's
                // directory. The empty-uri case (GLB embeds the bin chunk
                // directly and LoadGLB handles it separately) should never
                // reach this branch, but guard anyway: an empty uri would
                // try to open the asset directory itself and the ifstream
                // would fail with the same "Failed to open buffer" message.
                std::string bufferPath = data.baseDir + uri;

                std::ifstream bufferFile(bufferPath, std::ios::binary);
                if (!bufferFile.is_open()) {
                    throw std::runtime_error("Failed to open buffer: " + bufferPath);
                }

                bufferFile.seekg(0, std::ios::end);
                size_t fileSize = bufferFile.tellg();
                bufferFile.seekg(0, std::ios::beg);

                bufferData.resize(fileSize);
                bufferFile.read(reinterpret_cast<char*>(bufferData.data()), fileSize);
                bufferFile.close();
            }

            // glTF 2.0 §3.6.1.1: `byteLength` is REQUIRED on every buffer.
            // If it's present, treat it as the authoritative size — a
            // decoded-vs-declared mismatch is a broken asset and we'd rather
            // surface it here than corrupt mesh indexing downstream.
            if (bufferInfo.contains("byteLength")) {
                size_t declaredLength = bufferInfo["byteLength"].get<size_t>();
                if (bufferData.size() < declaredLength) {
                    throw std::runtime_error(
                        "glTF buffer " + std::to_string(bufferIndex) +
                        ": decoded " + std::to_string(bufferData.size()) +
                        " bytes but header declared " +
                        std::to_string(declaredLength) + " bytes");
                }
                // If decoded > declared we trim to the declared length —
                // some base64 encoders (notably older Python tooling) emit
                // an extra null byte of alignment padding. Trimming matches
                // the spec-compliant interpretation and keeps ExtractMeshes'
                // bounds-checked indexing sound.
                if (bufferData.size() > declaredLength) {
                    bufferData.resize(declaredLength);
                }
            }

            data.buffers.push_back(std::move(bufferData));
        }
    }

    auto model = std::make_shared<Model>();
    model->path = path;

    // WHY the per-stage try/catch blocks: each Extract* call has its own JSON
    // traversal pattern (materials → textures vs meshes → accessor/bufferView
    // vs nodes → transform/children vs animations → sampler/channel). When
    // any one of them throws (e.g. a schema-drift issue like an array where
    // an object was expected), the raw nlohmann::json exception mentions
    // only "cannot use operator[] with a string argument with array" —
    // which doesn't tell the caller whether to look at the materials block
    // or the animations block. Rethrowing with the stage name pinned to the
    // message collapses diagnosis from "unreadable stack dive" to a single
    // log line. Robust-WHY policy in cat-annihilation/CLAUDE.md §engine
    // demands comments on non-trivial logic — this is the diagnostic
    // scaffolding that makes "why doesn't my model load" debuggable.
    auto rethrowStage = [&path](const char* stage, const std::exception& ex) {
        throw std::runtime_error(
            std::string(stage) + " failed for '" + path + "': " + ex.what());
    };
    try { ExtractMaterials(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMaterials", ex); }
    try { ExtractMeshes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMeshes", ex); }
    try { ExtractNodes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractNodes", ex); }
    try { ExtractAnimations(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractAnimations", ex); }

    model->isLoaded = true;
    return model;
}

std::shared_ptr<Model> ModelLoader::LoadGLB(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    // glTF 2.0 §3.2 Binary container header: 12 bytes total.
    //   uint32 magic    = 0x46546C67  ("glTF" little-endian)
    //   uint32 version  = 2
    //   uint32 length   = total byte length of the GLB container
    // The magic is checked first; anything else means we're looking at a
    // random binary and should surface a useful error rather than fall
    // through into garbage chunk reads.
    uint32_t magic, version, length;
    file.read(reinterpret_cast<char*>(&magic), 4);
    file.read(reinterpret_cast<char*>(&version), 4);
    file.read(reinterpret_cast<char*>(&length), 4);

    if (magic != 0x46546C67) { // "glTF"
        throw std::runtime_error("Invalid GLB file: " + path);
    }
    if (version != 2) {
        // GLB v1 had a fundamentally different chunk layout; refuse rather
        // than silently decode wrong offsets. Meshy always emits v2.
        throw std::runtime_error(
            "Unsupported GLB version " + std::to_string(version) + " in: " + path);
    }

    GLTFData data;
    size_t lastSlash = path.find_last_of("/\\");
    data.baseDir = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";

    // glTF 2.0 §3.2: the body is a sequence of chunks laid out as
    //   uint32 chunkLength (bytes of chunk data, not including this header)
    //   uint32 chunkType   (FourCC: 0x4E4F534A="JSON", 0x004E4942="BIN\0")
    //   byte[chunkLength]  chunk data (padded to 4-byte alignment with 0x20
    //                      for JSON / 0x00 for BIN)
    // The JSON chunk MUST appear first and is REQUIRED. The BIN chunk is
    // optional but the only way Meshy ships geometry — so in practice we
    // always see exactly one JSON + one BIN for real assets.
    bool sawJsonChunk = false;
    while (file.tellg() < static_cast<std::streampos>(length)) {
        uint32_t chunkLength, chunkType;
        file.read(reinterpret_cast<char*>(&chunkLength), 4);
        file.read(reinterpret_cast<char*>(&chunkType), 4);
        if (!file) {
            throw std::runtime_error(
                "GLB truncated while reading chunk header in: " + path);
        }

        if (chunkType == 0x4E4F534A) { // "JSON"
            // WHY we tolerate trailing padding bytes: the spec requires JSON
            // chunks be padded to 4-byte alignment with 0x20 (space), which
            // is ASCII whitespace and therefore safe for json::parse to
            // ignore. But some older Meshy exports pad with 0x00 which
            // json::parse rejects as "unexpected null". Strip any trailing
            // 0x00/0x20 bytes before parsing so both flavours work.
            std::vector<char> jsonData(chunkLength);
            file.read(jsonData.data(), chunkLength);
            size_t effectiveLength = chunkLength;
            while (effectiveLength > 0 &&
                   (jsonData[effectiveLength - 1] == 0x00 ||
                    jsonData[effectiveLength - 1] == 0x20)) {
                --effectiveLength;
            }
            try {
                data.root = json::parse(std::string(jsonData.begin(),
                                                    jsonData.begin() + effectiveLength));
            } catch (const std::exception& ex) {
                throw std::runtime_error(
                    "GLB JSON chunk parse failed for '" + path + "': " + ex.what());
            }
            sawJsonChunk = true;
        } else if (chunkType == 0x004E4942) { // "BIN"
            // Binary chunks are raw buffer bytes — trailing 0x00 padding is
            // inside the allocation and won't cause downstream indexing to
            // misread, because every accessor's bufferView bounds are set
            // from the JSON header rather than the chunk size.
            std::vector<uint8_t> binData(chunkLength);
            file.read(reinterpret_cast<char*>(binData.data()), chunkLength);
            data.buffers.push_back(std::move(binData));
        } else {
            // Skip unknown chunk (spec permits forward-compat chunks).
            file.seekg(chunkLength, std::ios::cur);
        }
    }

    file.close();

    if (!sawJsonChunk) {
        throw std::runtime_error("GLB file missing JSON chunk: " + path);
    }

    auto model = std::make_shared<Model>();
    model->path = path;

    // WHY staged try/catch: see LoadGLTF for the long rationale. In one
    // sentence: each Extract* call has its own JSON traversal shape, so the
    // raw nlohmann::json exception doesn't tell the caller which stage
    // failed. Pin the stage name to the rethrown message so CatEntity /
    // DogEntity's catch blocks log something actionable instead of a stack
    // dive into json internals. Without this the first Meshy GLB that had
    // any schema quirk was effectively undebuggable.
    auto rethrowStage = [&path](const char* stage, const std::exception& ex) {
        throw std::runtime_error(
            std::string(stage) + " failed for '" + path + "': " + ex.what());
    };
    try { ExtractMaterials(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMaterials", ex); }
    try { ExtractMeshes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMeshes", ex); }
    try { ExtractNodes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractNodes", ex); }
    try { ExtractAnimations(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractAnimations", ex); }

    model->isLoaded = true;
    return model;
}

// Resolve the on-disk texture path for a glTF image reference. Returns
// empty string when the image uses a non-uri source (e.g. embedded in a
// GLB bufferView, or absent/null — which is how Meshy ships texture data).
//
// WHY this helper exists: before it, six ExtractMaterials sites
// unconditionally dereferenced `images[i]["uri"]` as a string. That works
// for the hand-authored .gltf placeholders where every image has a real
// file URI sibling, but it blows up on every Meshy .glb because Meshy
// embeds textures in the BIN chunk (`bufferView` + `mimeType`, no `uri`).
// nlohmann::json's operator[] on a missing key yields a null value, and
// casting that null to std::string throws json.exception.type_error.302 —
// the exact failure that kept ember_leader.glb from loading on the first
// playtest after it was wired. Funneling all six sites through this
// helper means GLB materials decode cleanly (textures remain unresolved
// strings and the downstream material layer falls back to
// baseColorFactor), and the .gltf path continues to work unchanged.
//
// Future work: load the embedded image bytes from bufferView and hand
// them to the texture uploader, so Meshy materials actually render with
// their diffuse maps rather than a flat base-colour tint.
static std::string ResolveImageTexturePath(
    const nlohmann::json& root,
    const std::string& baseDir,
    int texIndex
) {
    if (texIndex < 0 ||
        !root.contains("textures") ||
        static_cast<size_t>(texIndex) >= root["textures"].size()) {
        return {};
    }
    const auto& tex = root["textures"][texIndex];
    if (!tex.contains("source")) {
        return {};
    }
    int imageIndex = tex["source"].get<int>();
    if (imageIndex < 0 ||
        !root.contains("images") ||
        static_cast<size_t>(imageIndex) >= root["images"].size()) {
        return {};
    }
    const auto& image = root["images"][imageIndex];
    if (!image.contains("uri") || !image["uri"].is_string()) {
        // GLB-embedded image: bufferView + mimeType. Texture bytes live
        // in the .glb buffer — the material layer currently can't load
        // those yet, so return empty and let the mesh render with the
        // solid baseColorFactor. The mesh is still visible; the textures
        // are just missing.
        return {};
    }
    return baseDir + image["uri"].get<std::string>();
}

// Decode the bufferView-embedded image bytes for a glTF texture index
// into a CPU-side RGBA8 BaseColorImage that the caller retains for both
// (a) average-tone fallback colour computation and (b) eventual GPU
// upload (Step 2 of the PBR pipeline; see BaseColorImage in the header
// for the full why-block).
//
// WHY this is split from the averaging step (which used to be a single
// monolithic "decode + average + free" helper):
//
//   The previous monolith decoded with stb_image, walked the pixels to
//   produce one glm::vec4 average, and freed the decoded buffer before
//   returning. That collapsed the only chance to capture the texture
//   into a single "snapshot tint" — the data was gone before any GPU
//   uploader could see it, so Step 2 (real PBR sampling) would have
//   needed to decode each JPEG a SECOND time at upload. Splitting into
//   "decode -> shared_ptr<BaseColorImage>" + "average over BaseColorImage"
//   lets one decode feed both the immediate fallback factor (still
//   useful when the renderer hasn't bound a sampler yet, in unit-test
//   builds with no Vulkan, and as the alpha channel of the cube-proxy
//   path) AND the future GPU upload — for free, with no extra I/O.
//
// Returns nullptr when extraction failed at any guarded step (texture
// index OOB, no bufferView, bufferView OOB, stb_image decode failed).
// The caller treats nullptr as "no embedded image — leave the GLB-
// authored baseColorFactor in place".
static std::shared_ptr<BaseColorImage> DecodeEmbeddedBaseColorImage(
    const nlohmann::json& root,
    const std::vector<std::vector<uint8_t>>& buffers,
    int texIndex)
{
    if (texIndex < 0 ||
        !root.contains("textures") ||
        static_cast<size_t>(texIndex) >= root["textures"].size()) {
        return nullptr;
    }
    const auto& tex = root["textures"][texIndex];
    if (!tex.contains("source")) {
        return nullptr;
    }
    int imageIndex = tex["source"].get<int>();
    if (imageIndex < 0 ||
        !root.contains("images") ||
        static_cast<size_t>(imageIndex) >= root["images"].size()) {
        return nullptr;
    }
    const auto& image = root["images"][imageIndex];
    // Only handle the bufferView path here. URI-backed images are already
    // resolved to file paths by ResolveImageTexturePath and should be
    // loaded by the texture cache via TextureLoader::Load(path), not
    // decoded a second time here.
    if (!image.contains("bufferView") || !image["bufferView"].is_number_integer()) {
        return nullptr;
    }
    int bvIndex = image["bufferView"].get<int>();
    if (!root.contains("bufferViews") ||
        bvIndex < 0 ||
        static_cast<size_t>(bvIndex) >= root["bufferViews"].size()) {
        return nullptr;
    }
    const auto& bv = root["bufferViews"][bvIndex];
    if (!bv.contains("buffer") || !bv.contains("byteLength")) {
        return nullptr;
    }
    int bufferIdx = bv["buffer"].get<int>();
    if (bufferIdx < 0 ||
        static_cast<size_t>(bufferIdx) >= buffers.size()) {
        return nullptr;
    }
    const auto& buffer = buffers[bufferIdx];
    const size_t bvOffset =
        bv.contains("byteOffset") ? bv["byteOffset"].get<size_t>() : 0;
    const size_t bvLength = bv["byteLength"].get<size_t>();
    if (bvOffset + bvLength > buffer.size()) {
        return nullptr;
    }
    const uint8_t* imageBytes = buffer.data() + bvOffset;

    // Decode via stb_image — handles JPEG (Meshy ships baseColor as JPEG
    // for compression, normal/orm/emissive as PNG with alpha). Forcing 4
    // channels means the decode result is RGBA8 regardless of source
    // format, which (a) simplifies the averaging loop, (b) matches the
    // VK_FORMAT_R8G8B8A8_SRGB layout the Step 2 uploader will use, and
    // (c) avoids needing to special-case 3-channel JPEGs in any consumer.
    int w = 0, h = 0, comp = 0;
    constexpr int kForceRGBA = 4;
    stbi_uc* decoded = stbi_load_from_memory(
        imageBytes,
        static_cast<int>(bvLength),
        &w, &h, &comp, kForceRGBA);
    if (decoded == nullptr || w <= 0 || h <= 0) {
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return nullptr;
    }

    auto img = std::make_shared<BaseColorImage>();
    img->width = w;
    img->height = h;
    const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    // Copy from stb's malloc'd buffer into the shared_ptr-owned vector
    // so we can free stb's allocation immediately. The vector lives for
    // the lifetime of the Model (or until Step 2 explicitly resets the
    // shared_ptr after upload), and using std::vector means the GPU
    // uploader gets a contiguous .data() pointer with a known size and
    // can hand it straight to vkCmdCopyBufferToImage via a staging
    // buffer fill loop.
    img->rgba.resize(byteCount);
    std::memcpy(img->rgba.data(), decoded, byteCount);
    stbi_image_free(decoded);

    // Diagnostic label — short enough to fit on one playtest log line
    // alongside the surrounding model-load chatter, but specific enough
    // that a `grep [ModelLoader] cached baseColor` against a multi-cat
    // playtest names every distinct image dimension/index combination.
    std::ostringstream oss;
    oss << "image[" << imageIndex << "] " << w << "x" << h;
    img->sourceLabel = oss.str();
    return img;
}

// Compute the alpha-weighted average RGB of a CPU-side BaseColorImage
// and return it as a vec4 with .a == 1.0 on success or vec4(-1) on
// degenerate input (zero-area image, all-zero alpha).
//
// WHY alpha-weighted rather than a flat sum: Meshy baseColor JPEGs are
// opaque (alpha 255 everywhere) so for the current asset library this
// is mathematically identical to a flat sum. But future asset paths
// (PNG with punch-through alpha for fur silhouettes; the same texture
// re-purposed for foliage) ship pixels with alpha 0 or partial alpha,
// and weighting prevents fully-transparent pixels from dragging the
// dominant tone toward black. The +1 in `weighted` keeps every pixel
// contributing at least minimally so the formula degenerates to a
// flat average for fully-transparent images instead of dividing by
// zero — useful purely as a safety rail; transparent baseColor textures
// are not a real production case.
//
// WHY we don't sRGB-decode before averaging: the existing tint path
// (MeshSubmissionSystem -> baseColorFactor -> entity.frag) treats the
// factor as already-in-output-colourspace and does no gamma work, so
// averaging in sRGB matches what the existing renderer expects. The
// future per-fragment sampling path (Step 4) will sample from a
// VK_FORMAT_R8G8B8A8_SRGB image so the GPU does the linearisation —
// at which point the average tint computed here becomes a fallback for
// the cube-proxy path only.
//
// Cost: O(width * height). 4-16 M ops per cat at typical Meshy
// resolutions, runs once at load time, no allocation.
static glm::vec4 ComputeAverageBaseColor(const BaseColorImage& img) {
    const size_t pixelCount =
        static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    if (pixelCount == 0 || img.rgba.size() < pixelCount * 4) {
        return glm::vec4(-1.0F);
    }
    uint64_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
    const uint8_t* p = img.rgba.data();
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t r = p[i * 4 + 0];
        const uint8_t g = p[i * 4 + 1];
        const uint8_t b = p[i * 4 + 2];
        const uint8_t a = p[i * 4 + 3];
        const uint32_t weighted = static_cast<uint32_t>(a) + 1u;
        sumR += static_cast<uint64_t>(r) * weighted;
        sumG += static_cast<uint64_t>(g) * weighted;
        sumB += static_cast<uint64_t>(b) * weighted;
        sumA += weighted;
    }
    if (sumA == 0) {
        return glm::vec4(-1.0F);
    }
    const float invDenom = 1.0F / (255.0F * static_cast<float>(sumA));
    return glm::vec4(
        static_cast<float>(sumR) * invDenom,
        static_cast<float>(sumG) * invDenom,
        static_cast<float>(sumB) * invDenom,
        1.0F);
}

void ModelLoader::ExtractMaterials(const GLTFData& data, Model& model) {
    if (!data.root.contains("materials")) {
        return;
    }

    // Per-extraction dedup cache for decoded baseColor images. Two
    // materials in the same model can reference the same texture index
    // (some Meshy exports do this for the body+limb material split),
    // and decoding a 16 MB JPEG twice would burn ~5 ms per duplicate +
    // 16 MB of redundant heap. The shared_ptr stored on each Material
    // refers into this map, so lifetime is correct as long as at least
    // one Material survives. The map itself is local to this call —
    // it's a load-time scratch structure, not a long-lived cache.
    std::unordered_map<int, std::shared_ptr<BaseColorImage>> imageCache;

    for (const auto& matJson : data.root["materials"]) {
        Material material;

        // WHY the is_string check instead of an unguarded assignment: Meshy
        // sometimes emits `"name": null` for unnamed PBR materials. The raw
        // `material.name = matJson["name"]` path then throws 302 "type must
        // be string, but is null", which aborted the entire material
        // extraction for the whole model. An unnamed material is not a
        // load-fatal error — just leave the name empty.
        if (matJson.contains("name") && matJson["name"].is_string()) {
            material.name = matJson["name"].get<std::string>();
        }

        // PBR metallic roughness
        if (matJson.contains("pbrMetallicRoughness")) {
            const auto& pbr = matJson["pbrMetallicRoughness"];

            if (pbr.contains("baseColorFactor")) {
                const auto& color = pbr["baseColorFactor"];
                material.baseColorFactor = glm::vec4(color[0], color[1], color[2], color[3]);
            }

            if (pbr.contains("metallicFactor")) {
                material.metallicFactor = pbr["metallicFactor"];
            }

            if (pbr.contains("roughnessFactor")) {
                material.roughnessFactor = pbr["roughnessFactor"];
            }

            // Textures — resolved through ResolveImageTexturePath so GLB
            // bufferView-backed images don't throw; see the helper's WHY
            // comment at the top of this file.
            if (pbr.contains("baseColorTexture") && pbr["baseColorTexture"].contains("index")) {
                const int baseColorTexIdx =
                    pbr["baseColorTexture"]["index"].get<int>();
                material.baseColorTexture = ResolveImageTexturePath(
                    data.root, data.baseDir, baseColorTexIdx);

                // GLB-embedded baseColor (URI empty after Resolve...): pull
                // the bufferView bytes, decode with stb_image into a CPU-
                // side BaseColorImage retained on the Material, and ALSO
                // average it to overwrite baseColorFactor.
                //
                // WHY both outputs from one decode (Step 1 of the PBR
                // texture pipeline; see BaseColorImage in ModelLoader.hpp
                // for the full why-block):
                //
                //   - The retained image is what Step 2's GPU uploader
                //     will sample from to produce a real PBR baseColor
                //     texture per Model. Without it, Step 2 would have
                //     to re-decode every JPEG at upload time.
                //
                //   - The average factor is still useful right now: the
                //     existing MeshSubmissionSystem tier-2 fallback
                //     (tintOverride -> baseColorFactor -> grey) feeds it
                //     into the per-entity tint that today's entity.frag
                //     consumes. Until Step 4 flips the shader to sample
                //     the real texture, the average is the visible-cat
                //     differentiator (ember rigs land warm/orange,
                //     frost cool/white-grey, mist cool greys, etc).
                //
                // Failure paths are silent — DecodeEmbeddedBaseColorImage
                // returns nullptr (image URI-backed, bufferView OOB, or
                // stb decode failure), and we leave both
                // baseColorImageCpu null and baseColorFactor at the
                // GLB-authored value. The mesh still renders, just with
                // the parsed factor instead of an asset-derived one.
                if (material.baseColorTexture.empty()) {
                    // Per-extraction dedup: if a sibling material already
                    // decoded this same texture index in this Model,
                    // re-use the shared_ptr instead of decoding twice.
                    std::shared_ptr<BaseColorImage> img;
                    auto cacheIt = imageCache.find(baseColorTexIdx);
                    if (cacheIt != imageCache.end()) {
                        img = cacheIt->second;
                    } else {
                        img = DecodeEmbeddedBaseColorImage(
                            data.root, data.buffers, baseColorTexIdx);
                        if (img) {
                            imageCache.emplace(baseColorTexIdx, img);
                        }
                    }
                    if (img) {
                        material.baseColorImageCpu = img;
                        const glm::vec4 avg = ComputeAverageBaseColor(*img);
                        if (avg.a > 0.0F) {
                            material.baseColorFactor = avg;
                        }
                        // One-line load-time diagnostic — grep
                        // [ModelLoader] cached baseColor against a
                        // multi-cat playtest gives a snapshot of every
                        // distinct decoded asset and its dominant tone.
                        std::cout << "[ModelLoader] cached baseColor "
                                  << img->sourceLabel
                                  << " avg=(" << avg.r
                                  << "," << avg.g
                                  << "," << avg.b << ")"
                                  << std::endl;
                    }
                }
            }

            if (pbr.contains("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"].contains("index")) {
                material.metallicRoughnessTexture = ResolveImageTexturePath(
                    data.root, data.baseDir, pbr["metallicRoughnessTexture"]["index"].get<int>());
            }
        }

        // Normal map
        if (matJson.contains("normalTexture") && matJson["normalTexture"].contains("index")) {
            material.normalTexture = ResolveImageTexturePath(
                data.root, data.baseDir, matJson["normalTexture"]["index"].get<int>());
        }

        // Emissive
        if (matJson.contains("emissiveFactor")) {
            const auto& emissive = matJson["emissiveFactor"];
            material.emissiveFactor = glm::vec3(emissive[0], emissive[1], emissive[2]);
        }

        if (matJson.contains("emissiveTexture") && matJson["emissiveTexture"].contains("index")) {
            material.emissiveTexture = ResolveImageTexturePath(
                data.root, data.baseDir, matJson["emissiveTexture"]["index"].get<int>());
        }

        if (matJson.contains("doubleSided") && matJson["doubleSided"].is_boolean()) {
            material.doubleSided = matJson["doubleSided"].get<bool>();
        }

        if (matJson.contains("alphaMode") && matJson["alphaMode"].is_string()) {
            material.alphaMode = matJson["alphaMode"].get<std::string>();
        }

        if (matJson.contains("alphaCutoff") && matJson["alphaCutoff"].is_number()) {
            material.alphaCutoff = matJson["alphaCutoff"].get<float>();
        }

        model.materials.push_back(material);
    }
}

template<typename T>
std::vector<T> ModelLoader::ExtractBufferData(
    const uint8_t* bufferData,
    size_t offset,
    size_t count,
    size_t stride,
    size_t componentSize
) {
    std::vector<T> result(count);
    const uint8_t* src = bufferData + offset;

    if (stride == 0 || stride == componentSize) {
        // Tightly packed
        std::memcpy(result.data(), src, count * componentSize);
    } else {
        // Strided data
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(&result[i], src + i * stride, componentSize);
        }
    }

    return result;
}

void ModelLoader::ExtractMeshes(const GLTFData& data, Model& model) {
    if (!data.root.contains("meshes")) {
        return;
    }

    // WHY the `.get<int>()` idiom on every bufferView/accessor index below:
    //
    // nlohmann::json's operator[] is a template, not a pair of fixed
    // overloads, and the template SFINAE-dispatches based on how the key
    // type converts. When you pass a `nlohmann::json` value directly
    // (e.g. `bufferViews[accessor["bufferView"]]`), the compiler sees the
    // argument has `operator std::string()` and can route to the string-key
    // path — which at runtime throws
    //   "[json.exception.type_error.305] cannot use operator[] with a
    //    string argument with array"
    // because the array doesn't have string keys. This was the second bug
    // (after the base64 data URI one) that kept every cat.gltf from loading.
    // Extracting the int explicitly with `.get<int>()` forces the
    // integer-index overload and makes the intent unambiguous.
    const auto& accessors = data.root["accessors"];
    const auto& bufferViews = data.root["bufferViews"];

    for (const auto& meshJson : data.root["meshes"]) {
        for (const auto& primitive : meshJson["primitives"]) {
            Mesh mesh;

            if (meshJson.contains("name")) {
                mesh.name = meshJson["name"];
            }

            // Get material index
            if (primitive.contains("material")) {
                mesh.materialIndex = primitive["material"];
            }

            const auto& attributes = primitive["attributes"];

            // Read positions
            if (attributes.contains("POSITION")) {
                int accessorIdx = attributes["POSITION"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto positions = ExtractBufferData<glm::vec3>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::vec3)
                );

                mesh.vertices.resize(count);
                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].position = positions[i];
                }
            }

            // Read normals
            if (attributes.contains("NORMAL")) {
                int accessorIdx = attributes["NORMAL"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto normals = ExtractBufferData<glm::vec3>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::vec3)
                );

                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].normal = normals[i];
                }
            }

            // Read tangents
            bool hasTangents = false;
            if (attributes.contains("TANGENT")) {
                int accessorIdx = attributes["TANGENT"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto tangents = ExtractBufferData<glm::vec4>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::vec4)
                );

                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].tangent = tangents[i];
                }
                hasTangents = true;
            }

            // Read UV0
            if (attributes.contains("TEXCOORD_0")) {
                int accessorIdx = attributes["TEXCOORD_0"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto uvs = ExtractBufferData<glm::vec2>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::vec2)
                );

                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].texcoord0 = uvs[i];
                }
            }

            // Read UV1
            if (attributes.contains("TEXCOORD_1")) {
                int accessorIdx = attributes["TEXCOORD_1"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto uvs = ExtractBufferData<glm::vec2>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::vec2)
                );

                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].texcoord1 = uvs[i];
                }
            }

            // Read joints
            if (attributes.contains("JOINTS_0")) {
                int accessorIdx = attributes["JOINTS_0"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto joints = ExtractBufferData<glm::ivec4>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::ivec4)
                );

                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].joints = joints[i];
                }
            }

            // Read weights
            if (attributes.contains("WEIGHTS_0")) {
                int accessorIdx = attributes["WEIGHTS_0"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto weights = ExtractBufferData<glm::vec4>(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, sizeof(glm::vec4)
                );

                for (size_t i = 0; i < count; ++i) {
                    mesh.vertices[i].weights = weights[i];
                }
            }

            // Read indices
            if (primitive.contains("indices")) {
                int accessorIdx = primitive["indices"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                int componentType = accessor["componentType"];

                if (componentType == 5123) { // UNSIGNED_SHORT
                    auto indices16 = ExtractBufferData<uint16_t>(
                        data.buffers[bufferView["buffer"]].data(),
                        offset, count, 0, sizeof(uint16_t)
                    );
                    mesh.indices.resize(count);
                    for (size_t i = 0; i < count; ++i) {
                        mesh.indices[i] = indices16[i];
                    }
                } else if (componentType == 5125) { // UNSIGNED_INT
                    mesh.indices = ExtractBufferData<uint32_t>(
                        data.buffers[bufferView["buffer"]].data(),
                        offset, count, 0, sizeof(uint32_t)
                    );
                } else if (componentType == 5121) { // UNSIGNED_BYTE
                    auto indices8 = ExtractBufferData<uint8_t>(
                        data.buffers[bufferView["buffer"]].data(),
                        offset, count, 0, sizeof(uint8_t)
                    );
                    mesh.indices.resize(count);
                    for (size_t i = 0; i < count; ++i) {
                        mesh.indices[i] = indices8[i];
                    }
                }
            }

            // Validate index buffer against vertex count BEFORE any downstream
            // pass touches it. ExtractBufferData is an unchecked memcpy — if
            // the glTF authors the bufferView offsets incorrectly (which happens
            // with hand-built or legacy-tool-produced assets; see the
            // shipping cat.gltf's bufferView 2 for a concrete regression case),
            // the "indices" are just random bytes that happen to lie at that
            // offset. Feeding those into GenerateTangents causes
            // `vertices[i0]` with i0 in the 5-digit range — undefined access
            // into std::vector storage, which is an instant SIGSEGV with zero
            // stack context in Release builds. Fail loud here with the name +
            // index that violated the invariant so CatEntity::loadModel's
            // catch block surfaces a usable diagnostic.
            const size_t vertexCount = mesh.vertices.size();
            for (size_t triIndex = 0; triIndex < mesh.indices.size(); ++triIndex) {
                if (mesh.indices[triIndex] >= vertexCount) {
                    throw std::runtime_error(
                        "mesh '" + mesh.name + "': index " +
                        std::to_string(mesh.indices[triIndex]) + " at position " +
                        std::to_string(triIndex) + " exceeds vertex count " +
                        std::to_string(vertexCount) +
                        " (asset has misaligned bufferView byteOffset or corrupt buffer)");
                }
            }

            // Generate tangents if not present
            if (!hasTangents && !mesh.vertices.empty() && !mesh.indices.empty()) {
                GenerateTangents(mesh);
            }

            CalculateBounds(mesh);
            model.meshes.push_back(mesh);
        }
    }
}

void ModelLoader::GenerateTangents(Mesh& mesh) {
    // Precondition: ExtractMeshes validates indices-in-range BEFORE calling
    // us, so direct vector operator[] is safe here. If a caller ever invokes
    // this on an unvalidated mesh, the corruption surface is OOB reads in the
    // vertex array, not a SIGSEGV on indices.size() % 3 != 0 — so also guard
    // against a truncated triangle by rounding down to whole triangles.
    const size_t wholeTriangleIndexCount = (mesh.indices.size() / 3) * 3;
    for (size_t i = 0; i < wholeTriangleIndexCount; i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        Vertex& v0 = mesh.vertices[i0];
        Vertex& v1 = mesh.vertices[i1];
        Vertex& v2 = mesh.vertices[i2];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;
        glm::vec2 deltaUV1 = v1.texcoord0 - v0.texcoord0;
        glm::vec2 deltaUV2 = v2.texcoord0 - v0.texcoord0;

        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(det) < 1e-6f) {
            // Degenerate UV, use arbitrary tangent
            glm::vec3 tangent = glm::normalize(glm::cross(v0.normal, glm::vec3(0, 1, 0)));
            if (glm::length(tangent) < 0.1f) {
                tangent = glm::normalize(glm::cross(v0.normal, glm::vec3(1, 0, 0)));
            }
            v0.tangent = v1.tangent = v2.tangent = glm::vec4(tangent, 1.0f);
        } else {
            float f = 1.0f / det;
            glm::vec3 tangent;
            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
            tangent = glm::normalize(tangent);

            // Gram-Schmidt orthogonalize and set handedness
            for (auto* v : {&v0, &v1, &v2}) {
                glm::vec3 t = glm::normalize(tangent - v->normal * glm::dot(v->normal, tangent));
                glm::vec3 bitangent = glm::cross(v->normal, tangent);
                float handedness = (glm::dot(bitangent, glm::cross(v->normal, t)) < 0.0f) ? -1.0f : 1.0f;
                v->tangent = glm::vec4(t, handedness);
            }
        }
    }
}

void ModelLoader::CalculateBounds(Mesh& mesh) {
    if (mesh.vertices.empty()) {
        return;
    }

    mesh.boundsMin = mesh.vertices[0].position;
    mesh.boundsMax = mesh.vertices[0].position;

    for (const auto& vertex : mesh.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, vertex.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, vertex.position);
    }
}

void ModelLoader::ExtractNodes(const GLTFData& data, Model& model) {
    if (!data.root.contains("nodes")) {
        return;
    }

    const auto& nodesJson = data.root["nodes"];
    model.nodes.resize(nodesJson.size());

    for (size_t i = 0; i < nodesJson.size(); ++i) {
        const auto& nodeJson = nodesJson[i];
        Node& node = model.nodes[i];

        if (nodeJson.contains("name")) {
            node.name = nodeJson["name"];
        }

        // Transform
        if (nodeJson.contains("matrix")) {
            const auto& mat = nodeJson["matrix"];
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    node.localTransform[c][r] = mat[r * 4 + c];
                }
            }
        } else {
            glm::vec3 translation(0.0f);
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scale(1.0f);

            if (nodeJson.contains("translation")) {
                const auto& t = nodeJson["translation"];
                translation = glm::vec3(t[0], t[1], t[2]);
            }

            if (nodeJson.contains("rotation")) {
                const auto& r = nodeJson["rotation"];
                rotation = glm::quat(r[3], r[0], r[1], r[2]); // w, x, y, z
            }

            if (nodeJson.contains("scale")) {
                const auto& s = nodeJson["scale"];
                scale = glm::vec3(s[0], s[1], s[2]);
            }

            node.localTransform = glm::translate(glm::mat4(1.0f), translation) *
                                  glm::mat4_cast(rotation) *
                                  glm::scale(glm::mat4(1.0f), scale);
        }

        if (nodeJson.contains("mesh")) {
            node.meshIndex = nodeJson["mesh"];
        }

        if (nodeJson.contains("children")) {
            for (int childIdx : nodeJson["children"]) {
                node.children.push_back(childIdx);
                model.nodes[childIdx].parentIndex = static_cast<int>(i);
            }
        }
    }
}

void ModelLoader::ExtractAnimations(const GLTFData& data, Model& model) {
    if (!data.root.contains("animations")) {
        return;
    }

    const auto& accessors = data.root["accessors"];
    const auto& bufferViews = data.root["bufferViews"];

    for (const auto& animJson : data.root["animations"]) {
        Animation animation;

        if (animJson.contains("name")) {
            animation.name = animJson["name"];
        }

        for (const auto& channelJson : animJson["channels"]) {
            AnimationChannel channel;
            channel.nodeIndex = channelJson["target"]["node"];
            channel.path = channelJson["target"]["path"];

            int samplerIdx = channelJson["sampler"];
            const auto& sampler = animJson["samplers"][samplerIdx];

            // Read input (times). Same `.get<int>()` rationale as in
            // ExtractMeshes (see the long comment there): passing a raw
            // nlohmann::json to another json array's operator[] can
            // SFINAE-dispatch to the string-key overload and throw at
            // runtime. Explicit int conversion makes the intent safe.
            int inputAccessor = sampler["input"];
            const auto& inputAcc = accessors[inputAccessor];
            const auto& inputBV = bufferViews[inputAcc["bufferView"].get<int>()];

            size_t inputCount = inputAcc["count"];
            size_t inputOffset = inputAcc.value("byteOffset", 0) + inputBV.value("byteOffset", 0);

            channel.times = ExtractBufferData<float>(
                data.buffers[inputBV["buffer"]].data(),
                inputOffset, inputCount, 0, sizeof(float)
            );

            // Read output (values)
            int outputAccessor = sampler["output"];
            const auto& outputAcc = accessors[outputAccessor];
            const auto& outputBV = bufferViews[outputAcc["bufferView"].get<int>()];

            size_t outputCount = outputAcc["count"];
            size_t outputOffset = outputAcc.value("byteOffset", 0) + outputBV.value("byteOffset", 0);

            if (channel.path == "translation" || channel.path == "scale") {
                auto values = ExtractBufferData<glm::vec3>(
                    data.buffers[outputBV["buffer"]].data(),
                    outputOffset, outputCount, 0, sizeof(glm::vec3)
                );

                if (channel.path == "translation") {
                    channel.translations = values;
                } else {
                    channel.scales = values;
                }
            } else if (channel.path == "rotation") {
                auto values = ExtractBufferData<glm::vec4>(
                    data.buffers[outputBV["buffer"]].data(),
                    outputOffset, outputCount, 0, sizeof(glm::vec4)
                );

                channel.rotations.resize(outputCount);
                for (size_t i = 0; i < outputCount; ++i) {
                    channel.rotations[i] = glm::quat(values[i].w, values[i].x, values[i].y, values[i].z);
                }
            }

            animation.channels.push_back(channel);

            // Update animation duration
            if (!channel.times.empty()) {
                animation.duration = std::max(animation.duration, channel.times.back());
            }
        }

        model.animations.push_back(animation);
    }
}

} // namespace CatEngine
