#include "TextureLoader.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace CatEngine {

std::shared_ptr<Texture> TextureLoader::Load(
    const std::string& path,
    bool generateMipmaps,
    bool forceSRGB,
    bool forceLinear
) {
    // Check if it's an HDR file
    if (path.ends_with(".hdr")) {
        return LoadHDR(path, generateMipmaps);
    }

    int width, height, channels;
    uint8_t* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        throw std::runtime_error("Failed to load texture: " + path + " - " + stbi_failure_reason());
    }

    auto texture = std::make_shared<Texture>();
    texture->path = path;
    texture->width = static_cast<uint32_t>(width);
    texture->height = static_cast<uint32_t>(height);
    texture->channels = static_cast<uint32_t>(channels);
    texture->format = DetermineFormat(channels, false);

    // Determine color space
    if (forceLinear) {
        texture->colorSpace = ColorSpace::Linear;
    } else if (forceSRGB) {
        texture->colorSpace = ColorSpace::sRGB;
    } else {
        texture->colorSpace = DetectColorSpace(path, false);
    }

    // Create base mip level
    MipLevel baseMip;
    baseMip.width = texture->width;
    baseMip.height = texture->height;
    baseMip.data.resize(width * height * channels);
    std::memcpy(baseMip.data.data(), data, baseMip.data.size());

    texture->mipLevels.push_back(std::move(baseMip));

    stbi_image_free(data);

    // Generate mipmaps if requested
    if (generateMipmaps && (width > 1 || height > 1)) {
        GenerateMipmaps(*texture);
    }

    texture->isLoaded = true;
    return texture;
}

std::shared_ptr<Texture> TextureLoader::LoadHDR(const std::string& path, bool generateMipmaps) {
    int width, height, channels;
    float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        throw std::runtime_error("Failed to load HDR texture: " + path + " - " + stbi_failure_reason());
    }

    auto texture = std::make_shared<Texture>();
    texture->path = path;
    texture->width = static_cast<uint32_t>(width);
    texture->height = static_cast<uint32_t>(height);
    texture->channels = static_cast<uint32_t>(channels);
    texture->format = DetermineFormat(channels, true);
    texture->colorSpace = ColorSpace::Linear; // HDR is always linear

    // Create base mip level
    MipLevel baseMip;
    baseMip.width = texture->width;
    baseMip.height = texture->height;
    size_t dataSize = width * height * channels * sizeof(float);
    baseMip.data.resize(dataSize);
    std::memcpy(baseMip.data.data(), data, dataSize);

    texture->mipLevels.push_back(std::move(baseMip));

    stbi_image_free(data);

    // Generate mipmaps if requested
    if (generateMipmaps && (width > 1 || height > 1)) {
        GenerateMipmaps(*texture);
    }

    texture->isLoaded = true;
    return texture;
}

void TextureLoader::GenerateMipmaps(Texture& texture) {
    if (texture.mipLevels.empty()) {
        return;
    }

    uint32_t mipCount = CalculateMipLevels(texture.width, texture.height);

    for (uint32_t i = 1; i < mipCount; ++i) {
        MipLevel mip = GenerateMipLevel(texture.mipLevels.back(), texture.channels);

        // Stop if we've reached 1x1
        if (mip.width == 0 || mip.height == 0) {
            break;
        }

        texture.mipLevels.push_back(std::move(mip));
    }
}

uint32_t TextureLoader::CalculateMipLevels(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    uint32_t maxDim = std::max(width, height);

    while (maxDim > 1) {
        maxDim >>= 1;
        levels++;
    }

    return levels;
}

ColorSpace TextureLoader::DetectColorSpace(const std::string& path, bool isHDR) {
    if (isHDR) {
        return ColorSpace::Linear;
    }

    // Convert path to lowercase for comparison
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    // Linear space textures (typically)
    if (lowerPath.find("normal") != std::string::npos ||
        lowerPath.find("roughness") != std::string::npos ||
        lowerPath.find("metallic") != std::string::npos ||
        lowerPath.find("metalness") != std::string::npos ||
        lowerPath.find("height") != std::string::npos ||
        lowerPath.find("displacement") != std::string::npos ||
        lowerPath.find("ao") != std::string::npos ||
        lowerPath.find("occlusion") != std::string::npos) {
        return ColorSpace::Linear;
    }

    // sRGB textures (typically)
    if (lowerPath.find("color") != std::string::npos ||
        lowerPath.find("albedo") != std::string::npos ||
        lowerPath.find("diffuse") != std::string::npos ||
        lowerPath.find("emissive") != std::string::npos) {
        return ColorSpace::sRGB;
    }

    // Default to sRGB for color textures
    return ColorSpace::sRGB;
}

MipLevel TextureLoader::GenerateMipLevel(const MipLevel& srcLevel, uint32_t channels) {
    MipLevel dstLevel;
    dstLevel.width = std::max(1u, srcLevel.width / 2);
    dstLevel.height = std::max(1u, srcLevel.height / 2);

    // Determine if we're dealing with float data (HDR)
    bool isFloat = srcLevel.data.size() == (srcLevel.width * srcLevel.height * channels * sizeof(float));

    if (isFloat) {
        // HDR float downsampling
        dstLevel.data.resize(dstLevel.width * dstLevel.height * channels * sizeof(float));
        float* dst = reinterpret_cast<float*>(dstLevel.data.data());
        const float* src = reinterpret_cast<const float*>(srcLevel.data.data());

        for (uint32_t y = 0; y < dstLevel.height; ++y) {
            for (uint32_t x = 0; x < dstLevel.width; ++x) {
                uint32_t srcX = x * 2;
                uint32_t srcY = y * 2;

                // Box filter: average 2x2 pixels
                for (uint32_t c = 0; c < channels; ++c) {
                    float sum = 0.0f;
                    int samples = 0;

                    for (uint32_t dy = 0; dy < 2 && (srcY + dy) < srcLevel.height; ++dy) {
                        for (uint32_t dx = 0; dx < 2 && (srcX + dx) < srcLevel.width; ++dx) {
                            uint32_t srcIdx = ((srcY + dy) * srcLevel.width + (srcX + dx)) * channels + c;
                            sum += src[srcIdx];
                            samples++;
                        }
                    }

                    uint32_t dstIdx = (y * dstLevel.width + x) * channels + c;
                    dst[dstIdx] = sum / static_cast<float>(samples);
                }
            }
        }
    } else {
        // LDR byte downsampling
        dstLevel.data.resize(dstLevel.width * dstLevel.height * channels);
        uint8_t* dst = dstLevel.data.data();
        const uint8_t* src = srcLevel.data.data();

        for (uint32_t y = 0; y < dstLevel.height; ++y) {
            for (uint32_t x = 0; x < dstLevel.width; ++x) {
                uint32_t srcX = x * 2;
                uint32_t srcY = y * 2;

                // Box filter: average 2x2 pixels
                for (uint32_t c = 0; c < channels; ++c) {
                    uint32_t sum = 0;
                    int samples = 0;

                    for (uint32_t dy = 0; dy < 2 && (srcY + dy) < srcLevel.height; ++dy) {
                        for (uint32_t dx = 0; dx < 2 && (srcX + dx) < srcLevel.width; ++dx) {
                            uint32_t srcIdx = ((srcY + dy) * srcLevel.width + (srcX + dx)) * channels + c;
                            sum += src[srcIdx];
                            samples++;
                        }
                    }

                    uint32_t dstIdx = (y * dstLevel.width + x) * channels + c;
                    dst[dstIdx] = static_cast<uint8_t>(sum / samples);
                }
            }
        }
    }

    return dstLevel;
}

TextureFormat TextureLoader::DetermineFormat(int channels, bool isHDR) {
    if (isHDR) {
        switch (channels) {
            case 1: return TextureFormat::R32F;
            case 2: return TextureFormat::RG32F;
            case 3: return TextureFormat::RGB32F;
            case 4: return TextureFormat::RGBA32F;
            default: return TextureFormat::RGBA32F;
        }
    } else {
        switch (channels) {
            case 1: return TextureFormat::R8;
            case 2: return TextureFormat::RG8;
            case 3: return TextureFormat::RGB8;
            case 4: return TextureFormat::RGBA8;
            default: return TextureFormat::RGBA8;
        }
    }
}

} // namespace CatEngine
