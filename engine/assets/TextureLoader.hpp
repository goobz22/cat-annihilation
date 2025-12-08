#pragma once

#include "AssetLoader.hpp"
#include <vector>
#include <string>
#include <memory>

namespace CatEngine {

enum class TextureFormat {
    R8,
    RG8,
    RGB8,
    RGBA8,
    R16F,
    RG16F,
    RGB16F,
    RGBA16F,
    R32F,
    RG32F,
    RGB32F,
    RGBA32F
};

enum class ColorSpace {
    Linear,
    sRGB
};

// Mip level data
struct MipLevel {
    std::vector<uint8_t> data;
    uint32_t width;
    uint32_t height;
};

// Texture asset
class Texture : public Asset {
public:
    AssetType GetType() const override { return AssetType::Texture; }

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    TextureFormat format = TextureFormat::RGBA8;
    ColorSpace colorSpace = ColorSpace::sRGB;

    std::vector<MipLevel> mipLevels;

    // Get base level data
    const uint8_t* GetData() const {
        return mipLevels.empty() ? nullptr : mipLevels[0].data.data();
    }

    size_t GetDataSize() const {
        return mipLevels.empty() ? 0 : mipLevels[0].data.size();
    }

    uint32_t GetMipLevelCount() const {
        return static_cast<uint32_t>(mipLevels.size());
    }
};

// Texture loader using stb_image
class TextureLoader {
public:
    // Load texture from file (PNG, JPG, TGA, HDR)
    static std::shared_ptr<Texture> Load(
        const std::string& path,
        bool generateMipmaps = true,
        bool forceSRGB = false,
        bool forceLinear = false
    );

    // Load HDR texture
    static std::shared_ptr<Texture> LoadHDR(const std::string& path, bool generateMipmaps = true);

    // Generate mipmaps from base level
    static void GenerateMipmaps(Texture& texture);

    // Calculate number of mip levels
    static uint32_t CalculateMipLevels(uint32_t width, uint32_t height);

private:
    // Determine color space based on usage and format
    static ColorSpace DetectColorSpace(const std::string& path, bool isHDR);

    // Box filter downsampling for mipmap generation
    static MipLevel GenerateMipLevel(const MipLevel& srcLevel, uint32_t channels);

    // Format detection
    static TextureFormat DetermineFormat(int channels, bool isHDR);
};

} // namespace CatEngine
