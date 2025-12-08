#include "ModelLoader.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

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

    // Load buffers
    if (data.root.contains("buffers")) {
        for (const auto& bufferInfo : data.root["buffers"]) {
            std::string uri = bufferInfo["uri"];
            std::string bufferPath = data.baseDir + uri;

            std::ifstream bufferFile(bufferPath, std::ios::binary);
            if (!bufferFile.is_open()) {
                throw std::runtime_error("Failed to open buffer: " + bufferPath);
            }

            bufferFile.seekg(0, std::ios::end);
            size_t size = bufferFile.tellg();
            bufferFile.seekg(0, std::ios::beg);

            std::vector<uint8_t> bufferData(size);
            bufferFile.read(reinterpret_cast<char*>(bufferData.data()), size);
            bufferFile.close();

            data.buffers.push_back(std::move(bufferData));
        }
    }

    auto model = std::make_shared<Model>();
    model->path = path;

    ExtractMaterials(data, *model);
    ExtractMeshes(data, *model);
    ExtractNodes(data, *model);
    ExtractAnimations(data, *model);

    model->isLoaded = true;
    return model;
}

std::shared_ptr<Model> ModelLoader::LoadGLB(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    // Read GLB header
    uint32_t magic, version, length;
    file.read(reinterpret_cast<char*>(&magic), 4);
    file.read(reinterpret_cast<char*>(&version), 4);
    file.read(reinterpret_cast<char*>(&length), 4);

    if (magic != 0x46546C67) { // "glTF"
        throw std::runtime_error("Invalid GLB file: " + path);
    }

    GLTFData data;
    size_t lastSlash = path.find_last_of("/\\");
    data.baseDir = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";

    // Read chunks
    while (file.tellg() < static_cast<std::streampos>(length)) {
        uint32_t chunkLength, chunkType;
        file.read(reinterpret_cast<char*>(&chunkLength), 4);
        file.read(reinterpret_cast<char*>(&chunkType), 4);

        if (chunkType == 0x4E4F534A) { // "JSON"
            std::vector<char> jsonData(chunkLength);
            file.read(jsonData.data(), chunkLength);
            data.root = json::parse(std::string(jsonData.begin(), jsonData.end()));
        } else if (chunkType == 0x004E4942) { // "BIN"
            std::vector<uint8_t> binData(chunkLength);
            file.read(reinterpret_cast<char*>(binData.data()), chunkLength);
            data.buffers.push_back(std::move(binData));
        } else {
            // Skip unknown chunk
            file.seekg(chunkLength, std::ios::cur);
        }
    }

    file.close();

    auto model = std::make_shared<Model>();
    model->path = path;

    ExtractMaterials(data, *model);
    ExtractMeshes(data, *model);
    ExtractNodes(data, *model);
    ExtractAnimations(data, *model);

    model->isLoaded = true;
    return model;
}

void ModelLoader::ExtractMaterials(const GLTFData& data, Model& model) {
    if (!data.root.contains("materials")) {
        return;
    }

    for (const auto& matJson : data.root["materials"]) {
        Material material;

        if (matJson.contains("name")) {
            material.name = matJson["name"];
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

            // Textures
            if (pbr.contains("baseColorTexture")) {
                int texIndex = pbr["baseColorTexture"]["index"];
                if (data.root.contains("textures") && texIndex < data.root["textures"].size()) {
                    int imageIndex = data.root["textures"][texIndex]["source"];
                    if (data.root.contains("images") && imageIndex < data.root["images"].size()) {
                        material.baseColorTexture = data.baseDir + std::string(data.root["images"][imageIndex]["uri"]);
                    }
                }
            }

            if (pbr.contains("metallicRoughnessTexture")) {
                int texIndex = pbr["metallicRoughnessTexture"]["index"];
                if (data.root.contains("textures") && texIndex < data.root["textures"].size()) {
                    int imageIndex = data.root["textures"][texIndex]["source"];
                    if (data.root.contains("images") && imageIndex < data.root["images"].size()) {
                        material.metallicRoughnessTexture = data.baseDir + std::string(data.root["images"][imageIndex]["uri"]);
                    }
                }
            }
        }

        // Normal map
        if (matJson.contains("normalTexture")) {
            int texIndex = matJson["normalTexture"]["index"];
            if (data.root.contains("textures") && texIndex < data.root["textures"].size()) {
                int imageIndex = data.root["textures"][texIndex]["source"];
                if (data.root.contains("images") && imageIndex < data.root["images"].size()) {
                    material.normalTexture = data.baseDir + std::string(data.root["images"][imageIndex]["uri"]);
                }
            }
        }

        // Emissive
        if (matJson.contains("emissiveFactor")) {
            const auto& emissive = matJson["emissiveFactor"];
            material.emissiveFactor = glm::vec3(emissive[0], emissive[1], emissive[2]);
        }

        if (matJson.contains("emissiveTexture")) {
            int texIndex = matJson["emissiveTexture"]["index"];
            if (data.root.contains("textures") && texIndex < data.root["textures"].size()) {
                int imageIndex = data.root["textures"][texIndex]["source"];
                if (data.root.contains("images") && imageIndex < data.root["images"].size()) {
                    material.emissiveTexture = data.baseDir + std::string(data.root["images"][imageIndex]["uri"]);
                }
            }
        }

        if (matJson.contains("doubleSided")) {
            material.doubleSided = matJson["doubleSided"];
        }

        if (matJson.contains("alphaMode")) {
            material.alphaMode = matJson["alphaMode"];
        }

        if (matJson.contains("alphaCutoff")) {
            material.alphaCutoff = matJson["alphaCutoff"];
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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
                const auto& bufferView = bufferViews[accessor["bufferView"]];

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
    // Simple tangent generation (not MikkTSpace, but functional)
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
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

            // Read input (times)
            int inputAccessor = sampler["input"];
            const auto& inputAcc = accessors[inputAccessor];
            const auto& inputBV = bufferViews[inputAcc["bufferView"]];

            size_t inputCount = inputAcc["count"];
            size_t inputOffset = inputAcc.value("byteOffset", 0) + inputBV.value("byteOffset", 0);

            channel.times = ExtractBufferData<float>(
                data.buffers[inputBV["buffer"]].data(),
                inputOffset, inputCount, 0, sizeof(float)
            );

            // Read output (values)
            int outputAccessor = sampler["output"];
            const auto& outputAcc = accessors[outputAccessor];
            const auto& outputBV = bufferViews[outputAcc["bufferView"]];

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
