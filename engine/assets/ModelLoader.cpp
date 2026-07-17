#include "ModelLoader.hpp"
#include "Base64DataUri.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
    // Order matters: ExtractSkin writes IBMs onto the nodes ExtractNodes
    // created, and ExtractMeshes' JOINTS_0 remap reads the skinJoints table
    // ExtractSkin filled — see the ExtractSkin declaration.
    try { ExtractMaterials(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMaterials", ex); }
    try { ExtractNodes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractNodes", ex); }
    try { ExtractSkin(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractSkin", ex); }
    try { ExtractMeshes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMeshes", ex); }
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
    // Order matters: ExtractSkin writes IBMs onto the nodes ExtractNodes
    // created, and ExtractMeshes' JOINTS_0 remap reads the skinJoints table
    // ExtractSkin filled — see the ExtractSkin declaration.
    try { ExtractMaterials(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMaterials", ex); }
    try { ExtractNodes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractNodes", ex); }
    try { ExtractSkin(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractSkin", ex); }
    try { ExtractMeshes(data, *model); }
    catch (const std::exception& ex) { rethrowStage("ExtractMeshes", ex); }
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

    // RAII guard for stb's malloc'd buffer.
    //
    // WHY this exists: the old code path was
    //     auto img = std::make_shared<BaseColorImage>();
    //     ... img->rgba.resize(byteCount);  // can throw std::bad_alloc
    //     std::memcpy(...);
    //     stbi_image_free(decoded);
    // If `make_shared` or `rgba.resize` (which allocates byteCount bytes —
    // up to 16 MB for a 2048² baseColor) threw before reaching the explicit
    // stbi_image_free, the decoded buffer leaked. Wrapping the raw pointer
    // in a unique_ptr with a custom deleter makes the free happen on any
    // exit path, including throw — RAII as required by the engine's
    // C++20-discipline rule (cat-annihilation/CLAUDE.md §engine).
    struct StbiFreeDeleter {
        void operator()(stbi_uc* p) const noexcept {
            if (p != nullptr) stbi_image_free(p);
        }
    };
    std::unique_ptr<stbi_uc, StbiFreeDeleter> decodedGuard(decoded);

    auto img = std::make_shared<BaseColorImage>();
    img->width = w;
    img->height = h;
    const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    // Copy from stb's malloc'd buffer into the shared_ptr-owned vector
    // so the stb allocation can be released as soon as we leave this
    // function. The vector lives for the lifetime of the Model (or until
    // Step 2 explicitly resets the shared_ptr after upload), and using
    // std::vector means the GPU uploader gets a contiguous .data()
    // pointer with a known size and can hand it straight to
    // vkCmdCopyBufferToImage via a staging buffer fill loop.
    img->rgba.resize(byteCount);
    std::memcpy(img->rgba.data(), decoded, byteCount);
    // decodedGuard frees on scope exit; no explicit stbi_image_free needed.

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

namespace {

// glTF componentType constants (glTF 2.0 spec §3.6.2.2). Named so the
// dispatch code below reads as the spec table instead of magic numbers.
constexpr int kGltfUnsignedByte = 5121;
constexpr int kGltfUnsignedShort = 5123;
constexpr int kGltfUnsignedInt = 5125;
constexpr int kGltfFloat = 5126;

// Guard for attributes the engine only consumes as 32-bit floats
// (POSITION / NORMAL / TANGENT / TEXCOORD_*). ExtractBufferData is a raw
// memcpy with no notion of the source component width, so a non-float
// accessor on these paths would be silently reinterpreted as garbage
// geometry — the exact failure shape that took out every rigged Meshy
// asset when u8 JOINTS_0 lanes were read as i32 (2026-07-16). Refusing to
// load converts a subtle visual corruption into an actionable load error
// naming the attribute and the offending encoding.
void RequireFloatComponents(const nlohmann::json& accessor,
                            const char* attributeName,
                            const std::string& meshName) {
    const int componentType = accessor["componentType"];
    if (componentType != kGltfFloat) {
        throw std::runtime_error(
            "mesh '" + meshName + "': " + attributeName +
            " uses componentType " + std::to_string(componentType) +
            " but this loader only supports FLOAT (5126) for " +
            attributeName + "; re-export the asset with float attributes");
    }
}

// Widens per-vertex JOINTS_0 lanes into the 32-bit ivec4 lanes
// Vertex::joints uses. glTF permits UNSIGNED_BYTE and UNSIGNED_SHORT for
// joints; UNSIGNED_INT is accepted too for robustness against
// non-conforming exporters. Per-element memcpy (rather than one bulk copy)
// is what makes the width conversion correct: the Meshy/Blender rig
// pipeline emits tightly-packed u8vec4 streams, and bulk-copying those as
// i32x4 packed four vertices' joints into one lane — indices like
// 0x03020100 — which the bound check below then rejected, so the player
// cat and all four dog variants fell back to placeholder boxes.
// `stride == 0` means tightly packed per spec; glTF requires accessor
// byteOffsets be aligned to the component size, so the memcpy never reads
// past a misaligned boundary.
template <typename SourceLaneT>
std::vector<glm::ivec4> WidenJointLanes(const uint8_t* src, size_t count, size_t stride) {
    const size_t step = stride == 0 ? 4 * sizeof(SourceLaneT) : stride;
    std::vector<glm::ivec4> result(count);
    for (size_t i = 0; i < count; ++i) {
        SourceLaneT lanes[4];
        std::memcpy(lanes, src + i * step, sizeof(lanes));
        result[i] = glm::ivec4(static_cast<int>(lanes[0]), static_cast<int>(lanes[1]),
                               static_cast<int>(lanes[2]), static_cast<int>(lanes[3]));
    }
    return result;
}

std::vector<glm::ivec4> ExtractJointIndices(const uint8_t* bufferData, size_t offset,
                                            size_t count, size_t stride, int componentType,
                                            const std::string& meshName) {
    const uint8_t* src = bufferData + offset;
    switch (componentType) {
        case kGltfUnsignedByte:  return WidenJointLanes<uint8_t>(src, count, stride);
        case kGltfUnsignedShort: return WidenJointLanes<uint16_t>(src, count, stride);
        case kGltfUnsignedInt:   return WidenJointLanes<uint32_t>(src, count, stride);
        default:
            throw std::runtime_error(
                "mesh '" + meshName + "': JOINTS_0 componentType " +
                std::to_string(componentType) +
                " is not a legal glTF joint encoding (expected 5121, 5123, or 5125)");
    }
}

// Converts normalized fixed-point WEIGHTS_0 lanes to floats. glTF's
// normalized-accessor rule maps [0, numeric-max] onto [0.0, 1.0], so 255
// (u8) and 65535 (u16) both decode to exactly 1.0 — preserving the
// "weights sum to 1" invariant the skinning palette relies on.
template <typename SourceLaneT>
std::vector<glm::vec4> NormalizeWeightLanes(const uint8_t* src, size_t count, size_t stride) {
    constexpr float scale = 1.0f / static_cast<float>(std::numeric_limits<SourceLaneT>::max());
    const size_t step = stride == 0 ? 4 * sizeof(SourceLaneT) : stride;
    std::vector<glm::vec4> result(count);
    for (size_t i = 0; i < count; ++i) {
        SourceLaneT lanes[4];
        std::memcpy(lanes, src + i * step, sizeof(lanes));
        result[i] = glm::vec4(lanes[0] * scale, lanes[1] * scale,
                              lanes[2] * scale, lanes[3] * scale);
    }
    return result;
}

std::vector<glm::vec4> ExtractWeights(const uint8_t* bufferData, size_t offset,
                                      size_t count, size_t stride, int componentType,
                                      const std::string& meshName) {
    const uint8_t* src = bufferData + offset;
    switch (componentType) {
        case kGltfFloat: {
            // Already the in-memory format — copy honoring the stride.
            const size_t step = stride == 0 ? sizeof(glm::vec4) : stride;
            std::vector<glm::vec4> result(count);
            for (size_t i = 0; i < count; ++i) {
                std::memcpy(&result[i], src + i * step, sizeof(glm::vec4));
            }
            return result;
        }
        case kGltfUnsignedByte:  return NormalizeWeightLanes<uint8_t>(src, count, stride);
        case kGltfUnsignedShort: return NormalizeWeightLanes<uint16_t>(src, count, stride);
        default:
            throw std::runtime_error(
                "mesh '" + meshName + "': WEIGHTS_0 componentType " +
                std::to_string(componentType) +
                " is not a legal glTF weight encoding (expected 5126, or normalized 5121/5123)");
    }
}

} // namespace

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
                RequireFloatComponents(accessor, "POSITION", mesh.name);
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
                RequireFloatComponents(accessor, "NORMAL", mesh.name);
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
                RequireFloatComponents(accessor, "TANGENT", mesh.name);
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
                RequireFloatComponents(accessor, "TEXCOORD_0", mesh.name);
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
                RequireFloatComponents(accessor, "TEXCOORD_1", mesh.name);
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

            // Read joints.
            //
            // glTF stores JOINTS_0 as UNSIGNED_BYTE (componentType 5121) or
            // UNSIGNED_SHORT (5123) — the Meshy/Blender rig pipeline behind
            // every shipped character emits tightly-packed u8vec4 streams.
            // ExtractJointIndices dispatches on the accessor's declared
            // componentType and widens each lane individually into the
            // 32-bit ivec4 lanes Vertex::joints uses (the historical bulk
            // memcpy that assumed i32 lanes garbled every u8/u16 asset;
            // tests/unit/test_model_loader_joints.cpp pins the contract).
            //
            // WHY the bound is data.root["nodes"].size() (or its absence
            // -> the implied palette size of zero): JOINTS_0 references
            // glTF node indices via the skin's `joints` array. The current
            // loader doesn't extract skins yet (Model::skinJoints is
            // declared but never populated), so the bone palette uploaded
            // by ScenePass is sized to nodes.size() — one bone per node.
            // That makes nodes.size() the authoritative upper bound. The
            // skinned.vert shader reads from a fixed 256-bone array
            // (shaders/geometry/skinned.vert l.41) with NO bounds check,
            // so an OOB index there reads undefined memory in the
            // descriptor set and surfaces as a vertex flying off to
            // infinity OR an instant GPU device-lost. ScenePass.cpp:2087
            // clamps to bone 0 defensively, but the GPU path through
            // skinned.vert does not — so we MUST surface this at load
            // time rather than rely on the GPU side to mask a corrupt
            // asset.
            if (attributes.contains("JOINTS_0")) {
                int accessorIdx = attributes["JOINTS_0"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto joints = ExtractJointIndices(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, accessor["componentType"], mesh.name
                );

                // JOINTS_0 lanes are indices into skin.joints (joint
                // SLOTS), per the glTF spec — NOT node indices. Remap each
                // lane through the skinJoints table ExtractSkin filled so
                // the stored values ARE node indices, matching the
                // node-indexed skeleton/palette the entities build. Storing
                // the raw slot indices here (the pre-2026-07-17 behaviour)
                // shredded every animated character: the shipped rigs'
                // joints arrays are heavily permuted, so each vertex
                // followed the wrong bones. Skinned meshes with no skin at
                // all fall back to treating lanes as node indices — the
                // only self-consistent reading such a (nonstandard) file
                // permits.
                const bool hasSkin = !model.skinJoints.empty();
                const size_t jointBound = hasSkin
                                              ? model.skinJoints.size()
                                              : (data.root.contains("nodes")
                                                     ? data.root["nodes"].size()
                                                     : 0u);

                for (size_t i = 0; i < count; ++i) {
                    glm::ivec4 jointIdx = joints[i];
                    for (int lane = 0; lane < 4; ++lane) {
                        const int idx = jointIdx[lane];
                        if (idx < 0 || static_cast<size_t>(idx) >= jointBound) {
                            throw std::runtime_error(
                                "mesh '" + mesh.name + "': JOINTS_0 vertex " +
                                std::to_string(i) + " lane " +
                                std::to_string(lane) + " references " +
                                (hasSkin ? "joint slot " : "node ") +
                                std::to_string(idx) +
                                " but the model only declares " +
                                std::to_string(jointBound) +
                                (hasSkin ? " skin joints" : " nodes") +
                                " (asset has a corrupt skin or stale joint "
                                "indices after a node-array trim)");
                        }
                        if (hasSkin) {
                            jointIdx[lane] = model.skinJoints[idx];
                        }
                    }
                    mesh.vertices[i].joints = jointIdx;
                }
            }

            // Read weights. Same componentType dispatch as joints: raw
            // floats pass through, normalized u8/u16 fixed-point decodes to
            // [0, 1] so the "weights sum to 1" skinning invariant survives
            // whichever encoding the exporter chose.
            if (attributes.contains("WEIGHTS_0")) {
                int accessorIdx = attributes["WEIGHTS_0"];
                const auto& accessor = accessors[accessorIdx];
                const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

                size_t count = accessor["count"];
                size_t offset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
                size_t stride = bufferView.value("byteStride", 0);

                auto weights = ExtractWeights(
                    data.buffers[bufferView["buffer"]].data(),
                    offset, count, stride, accessor["componentType"], mesh.name
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
            // glTF 2.0 §3.7.3.1 stores a node's `matrix` as a 16-element
            // COLUMN-MAJOR array: the element at logical (row, col) lives at
            // flat index col*4 + row. glm::mat4 is ALSO column-major and its
            // operator[] selects a COLUMN, so `localTransform[col][row]` names
            // that same logical element. The correct, layout-preserving copy is
            // therefore localTransform[col][row] = mat[col*4 + row] — a straight
            // memcpy-order transfer, NOT a re-index.
            //
            // WHY this deserves a comment (and a pinned regression test): the
            // previous code read mat[row*4 + col], which silently TRANSPOSED
            // every non-identity node matrix. It went unnoticed because every
            // `matrix` a shipped asset actually carries is the identity (props
            // use identity node matrices; the rigs use TRS with no matrix at
            // all), and transpose(I) == I. The first asset authored with a real
            // node matrix — any rotation or a non-origin translation — would
            // have loaded a corrupted bind pose: e.g. translation, which glTF
            // stores in the last column (indices 12/13/14), would have been
            // scattered into the bottom row instead. node.localTransform feeds
            // the skeleton bind pose (see CatEntity/DogEntity fromMatrix), so a
            // transpose here corrupts skinning downstream.
            const auto& mat = nodeJson["matrix"];
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    node.localTransform[column][row] = mat[column * 4 + row];
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
                // Bounds-check the asset-controlled child index BEFORE the
                // operator[] write below. model.nodes was sized once to the
                // node count and never grown, so an out-of-range (or
                // negative) child from a malformed/third-party glTF was a
                // controlled out-of-bounds HEAP WRITE at
                // &nodes[childIdx].parentIndex — silent corruption or a
                // SIGSEGV, and it slipped through because this was the one
                // index in the loader that didn't validate (JOINTS_0, skin
                // joints, and triangle indices all throw on OOB). Throw the
                // same way so the entities' load-failure try/catch turns it
                // into a proxy-cube fallback instead of UB. (2026-07-17
                // correctness audit.)
                if (childIdx < 0 ||
                    static_cast<size_t>(childIdx) >= model.nodes.size()) {
                    throw std::runtime_error(
                        "node " + std::to_string(i) + " references child index " +
                        std::to_string(childIdx) + " but the model only declares " +
                        std::to_string(model.nodes.size()) +
                        " nodes (corrupt glTF node graph or stale child index "
                        "after a node-array trim)");
                }
                node.children.push_back(childIdx);
                model.nodes[childIdx].parentIndex = static_cast<int>(i);
            }
        }
    }
}

void ModelLoader::ExtractSkin(const GLTFData& data, Model& model) {
    // Skins were previously never parsed at all, which broke skinning in
    // two compounding ways (found 2026-07-17 via the headless harness's
    // gameplay screenshots — every animated character rendered shredded):
    //   1. JOINTS_0 lanes are indices into skin.joints (joint SLOTS), but
    //      with no skinJoints table the mesh extractor stored them raw as
    //      node indices. The shipped rigs' joints arrays are heavily
    //      permuted (ember_leader.glb: [34,33,20,19,...]), so every vertex
    //      followed the wrong bones the moment a clip played.
    //   2. Node::inverseBindMatrix stayed at its identity default for
    //      every bone, so the palette was world(bone) * I — a double
    //      transform even with correct indices.
    // Bind-pose renders (--disable-gpu-skinning) hid both: that mode
    // draws raw vertices and never consults the palette.
    // tests/unit/test_model_loader_joints.cpp [skin] pins both halves.
    if (!data.root.contains("skins")) {
        return;
    }
    const auto& skinsJson = data.root["skins"];
    if (skinsJson.empty()) {
        return;
    }
    // Every asset this engine ships (Meshy + retopo pipeline) has exactly
    // one skin; the entities also build exactly one skeleton per model.
    // A multi-skin file would need per-primitive skin resolution via the
    // node graph — refuse loudly instead of silently mis-skinning.
    if (skinsJson.size() > 1) {
        throw std::runtime_error("model declares " +
                                 std::to_string(skinsJson.size()) +
                                 " skins; only one skin per model is supported");
    }
    const auto& skinJson = skinsJson[0];

    const auto& jointsJson = skinJson["joints"];
    model.skinJoints.reserve(jointsJson.size());
    for (const auto& jointNode : jointsJson) {
        const int nodeIndex = jointNode.get<int>();
        if (nodeIndex < 0 ||
            static_cast<size_t>(nodeIndex) >= model.nodes.size()) {
            throw std::runtime_error("skin joint references node " +
                                     std::to_string(nodeIndex) +
                                     " but model only declares " +
                                     std::to_string(model.nodes.size()) +
                                     " nodes");
        }
        model.skinJoints.push_back(nodeIndex);
    }

    if (skinJson.contains("inverseBindMatrices")) {
        const auto& accessors = data.root["accessors"];
        const auto& bufferViews = data.root["bufferViews"];
        const auto& accessor = accessors[skinJson["inverseBindMatrices"].get<int>()];
        const auto& bufferView = bufferViews[accessor["bufferView"].get<int>()];

        const size_t count = accessor["count"];
        if (count != model.skinJoints.size()) {
            throw std::runtime_error(
                "skin inverseBindMatrices count " + std::to_string(count) +
                " != joint count " + std::to_string(model.skinJoints.size()));
        }
        const size_t offset =
            accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
        auto ibms = ExtractBufferData<glm::mat4>(
            data.buffers[bufferView["buffer"]].data(), offset, count, 0,
            sizeof(glm::mat4));

        // Scatter slot-indexed IBMs onto the nodes the joints table names,
        // so the node-indexed skeleton the entities build (bone i == node i)
        // picks the right matrix without knowing about joint slots.
        for (size_t slot = 0; slot < count; ++slot) {
            model.nodes[static_cast<size_t>(model.skinJoints[slot])]
                .inverseBindMatrix = ibms[slot];
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
