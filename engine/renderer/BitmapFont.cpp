#include "BitmapFont.hpp"

#include "../rhi/RHIBuffer.hpp"
#include "../rhi/RHICommandBuffer.hpp"
#include "../rhi/RHITexture.hpp"

#include <cstring>
#include <iostream>
#include <cmath>

namespace CatEngine::Renderer {

// Simple 5x7 bitmap font data for ASCII 32-127
// Each character is encoded as 7 bytes (rows), each byte has 5 bits (columns)
static const uint8_t FONT_5x7[96][7] = {
    // Space (32)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
    // " (34)
    {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00},
    // # (35)
    {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A},
    // $ (36)
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04},
    // % (37)
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},
    // & (38)
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D},
    // ' (39)
    {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00},
    // ( (40)
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},
    // ) (41)
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
    // * (42)
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00},
    // + (43)
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
    // , (44)
    {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08},
    // - (45)
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    // . (46)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
    // / (47)
    {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00},
    // 0 (48)
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    // 1 (49)
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // 2 (50)
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    // 3 (51)
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    // 4 (52)
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    // 5 (53)
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    // 6 (54)
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    // 7 (55)
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    // 8 (56)
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    // 9 (57)
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    // : (58)
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
    // ; (59)
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08},
    // < (60)
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
    // = (61)
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
    // > (62)
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},
    // ? (63)
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    // @ (64)
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E},
    // A (65)
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    // B (66)
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    // C (67)
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    // D (68)
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
    // E (69)
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    // F (70)
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    // G (71)
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    // H (72)
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    // I (73)
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // J (74)
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    // K (75)
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    // L (76)
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    // M (77)
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    // N (78)
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
    // O (79)
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    // P (80)
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    // Q (81)
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    // R (82)
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    // S (83)
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    // T (84)
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    // U (85)
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    // V (86)
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    // W (87)
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    // X (88)
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    // Y (89)
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    // Z (90)
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    // [ (91)
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E},
    // \ (92)
    {0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00},
    // ] (93)
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E},
    // ^ (94)
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00},
    // _ (95)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    // ` (96)
    {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00},
    // a (97)
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F},
    // b (98)
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},
    // c (99)
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E},
    // d (100)
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F},
    // e (101)
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E},
    // f (102)
    {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08},
    // g (103)
    {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E},
    // h (104)
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11},
    // i (105)
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E},
    // j (106)
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C},
    // k (107)
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12},
    // l (108)
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // m (109)
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11},
    // n (110)
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11},
    // o (111)
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
    // p (112)
    {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10},
    // q (113)
    {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01},
    // r (114)
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},
    // s (115)
    {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E},
    // t (116)
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06},
    // u (117)
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},
    // v (118)
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04},
    // w (119)
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A},
    // x (120)
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11},
    // y (121)
    {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E},
    // z (122)
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F},
    // { (123)
    {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02},
    // | (124)
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    // } (125)
    {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08},
    // ~ (126)
    {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00},
    // DEL (127) - empty
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

bool BitmapFont::Initialize(RHI::IRHI* rhi, uint32_t fontSize) {
    rhi_ = rhi;
    fontSize_ = fontSize;
    lineHeight_ = static_cast<float>(fontSize) * 1.2f;

    const uint32_t cellWidth = ATLAS_WIDTH / CHARS_PER_ROW;
    const uint32_t cellHeight = ATLAS_HEIGHT / CHAR_ROWS;

    // Rasterize the 5x7 source font into the 32x(ATLAS_HEIGHT/CHAR_ROWS) cell
    // grid using integer replication. Keeps glyph edges crisp at integer pixel
    // scales without introducing sampling blur at V1.
    m_atlasPixels.assign(static_cast<size_t>(ATLAS_WIDTH) * ATLAS_HEIGHT, 0);

    constexpr uint32_t GLYPH_SRC_W = 5;
    constexpr uint32_t GLYPH_SRC_H = 7;
    const uint32_t pixelScaleX = cellWidth / GLYPH_SRC_W;
    const uint32_t pixelScaleY = cellHeight / GLYPH_SRC_H;
    const uint32_t glyphPixelW = GLYPH_SRC_W * pixelScaleX;
    const uint32_t glyphPixelH = GLYPH_SRC_H * pixelScaleY;
    const uint32_t padX = (cellWidth - glyphPixelW) / 2;
    const uint32_t padY = (cellHeight - glyphPixelH) / 2;

    for (int i = 0; i < 96; ++i) {
        const char c = static_cast<char>(32 + i);
        const uint32_t col = static_cast<uint32_t>(i) % CHARS_PER_ROW;
        const uint32_t row = static_cast<uint32_t>(i) / CHARS_PER_ROW;
        const uint32_t cellOriginX = col * cellWidth + padX;
        const uint32_t cellOriginY = row * cellHeight + padY;

        const uint8_t (&rows)[7] = FONT_5x7[i];
        for (uint32_t gy = 0; gy < GLYPH_SRC_H; ++gy) {
            // Source bitmask stores column 0 in bit 4 (leftmost of the 5-wide
            // glyph); bits 0..4 map to columns 4..0 respectively.
            const uint8_t mask = rows[gy];
            for (uint32_t gx = 0; gx < GLYPH_SRC_W; ++gx) {
                const bool on = (mask >> (GLYPH_SRC_W - 1 - gx)) & 1u;
                if (!on) {
                    continue;
                }
                // Integer pixel replication. Each "on" source texel is
                // splatted into a pixelScaleX * pixelScaleY block of 0xFF
                // bytes in the atlas. We deliberately do NOT blend or
                // antialias between source texels for two reasons:
                //
                //   1. Glyph edges stay crisp — the font renderer samples
                //      this atlas with VK_FILTER_LINEAR at arbitrary UI
                //      scales. Pre-blurring the source with bilinear
                //      interpolation during rasterization would stack
                //      filters at draw time (source blur * sampler blur)
                //      and produce mushy glyphs at 1:1 screen-space
                //      scales. Feeding the sampler a sharp-edge mask lets
                //      the GPU do the right amount of filtering for the
                //      current screen scale and no more.
                //
                //   2. 5x7 source glyphs integer-upscaled to a cell are
                //      pixel-aligned by construction. Any non-integer
                //      scaling would put partial coverage on edge pixels,
                //      which a 1-byte R8_UNORM atlas cannot represent
                //      without dithering artefacts. The scale factors
                //      pixelScaleX/Y are computed as integer divisions
                //      above specifically to guarantee this.
                for (uint32_t sy = 0; sy < pixelScaleY; ++sy) {
                    for (uint32_t sx = 0; sx < pixelScaleX; ++sx) {
                        const uint32_t px = cellOriginX + gx * pixelScaleX + sx;
                        const uint32_t py = cellOriginY + gy * pixelScaleY + sy;
                        m_atlasPixels[py * ATLAS_WIDTH + px] = 0xFFu;
                    }
                }
            }
        }

        GlyphInfo glyph{};
        glyph.u0 = static_cast<float>(col * cellWidth) / static_cast<float>(ATLAS_WIDTH);
        glyph.v0 = static_cast<float>(row * cellHeight) / static_cast<float>(ATLAS_HEIGHT);
        glyph.u1 = static_cast<float>((col + 1) * cellWidth) / static_cast<float>(ATLAS_WIDTH);
        glyph.v1 = static_cast<float>((row + 1) * cellHeight) / static_cast<float>(ATLAS_HEIGHT);
        glyph.width = static_cast<float>(fontSize) * 0.6f;
        glyph.height = static_cast<float>(fontSize);
        glyph.xOffset = 0.0f;
        glyph.yOffset = 0.0f;
        glyph.xAdvance = glyph.width;

        glyphs_[c] = glyph;
    }

    if (!rhi_) {
        std::cout << "[BitmapFont] Font initialized (CPU-only, no RHI) with "
                  << glyphs_.size() << " glyphs, fontSize=" << fontSize << "\n";
        return true;
    }

    // Upload the rasterized atlas to the GPU as an R8_UNORM sampled image.
    RHI::TextureDesc atlasDesc{};
    atlasDesc.type = RHI::TextureType::Texture2D;
    atlasDesc.format = RHI::TextureFormat::R8_UNORM;
    atlasDesc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
    atlasDesc.width = ATLAS_WIDTH;
    atlasDesc.height = ATLAS_HEIGHT;
    atlasDesc.depth = 1;
    atlasDesc.mipLevels = 1;
    atlasDesc.arrayLayers = 1;
    atlasDesc.sampleCount = 1;
    atlasDesc.debugName = "BitmapFontAtlas";

    m_atlasTexture = rhi_->CreateTexture(atlasDesc);
    if (!m_atlasTexture) {
        std::cerr << "[BitmapFont] Failed to create atlas texture\n";
        return false;
    }

    const uint64_t atlasByteSize =
        static_cast<uint64_t>(ATLAS_WIDTH) * static_cast<uint64_t>(ATLAS_HEIGHT);

    RHI::BufferDesc stagingDesc{};
    stagingDesc.size = atlasByteSize;
    stagingDesc.usage = RHI::BufferUsage::Staging | RHI::BufferUsage::TransferSrc;
    stagingDesc.memoryProperties =
        RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    stagingDesc.debugName = "BitmapFontAtlasStaging";

    RHI::IRHIBuffer* stagingBuffer = rhi_->CreateBuffer(stagingDesc);
    if (!stagingBuffer) {
        std::cerr << "[BitmapFont] Failed to create staging buffer\n";
        rhi_->DestroyTexture(m_atlasTexture);
        m_atlasTexture = nullptr;
        return false;
    }

    void* mapped = rhi_->MapBuffer(stagingBuffer);
    if (!mapped) {
        std::cerr << "[BitmapFont] Failed to map staging buffer\n";
        rhi_->DestroyBuffer(stagingBuffer);
        rhi_->DestroyTexture(m_atlasTexture);
        m_atlasTexture = nullptr;
        return false;
    }
    std::memcpy(mapped, m_atlasPixels.data(), static_cast<size_t>(atlasByteSize));
    rhi_->UnmapBuffer(stagingBuffer);

    RHI::IRHICommandBuffer* cmd = rhi_->CreateCommandBuffer();
    if (!cmd) {
        std::cerr << "[BitmapFont] Failed to create upload command buffer\n";
        rhi_->DestroyBuffer(stagingBuffer);
        rhi_->DestroyTexture(m_atlasTexture);
        m_atlasTexture = nullptr;
        return false;
    }

    // Synchronous upload path: the atlas is tiny (256 KiB at 512x512x1B) and
    // only uploaded once at init time, so a single WaitIdle is cheaper than
    // threading a per-frame staging ring for this resource.
    //
    // CopyBufferToTexture handles the image layout transitions internally
    // (UNDEFINED -> TRANSFER_DST_OPTIMAL around the copy, then
    // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL afterwards). We do
    // NOT emit any barriers here: the atlas is usable by samplers as soon
    // as the submitted work completes. Prior to the VulkanCommandBuffer
    // fix this path produced a texture left in UNDEFINED, and the first
    // sample read was undefined behaviour — do not reintroduce that by
    // assuming the copy leaves the image in TRANSFER_DST_OPTIMAL.
    cmd->Begin();
    cmd->CopyBufferToTexture(stagingBuffer, m_atlasTexture,
                             /*mipLevel*/ 0, /*arrayLayer*/ 0,
                             ATLAS_WIDTH, ATLAS_HEIGHT, /*depth*/ 1);
    cmd->End();

    rhi_->Submit(&cmd, 1);
    rhi_->WaitIdle();

    rhi_->DestroyCommandBuffer(cmd);
    rhi_->DestroyBuffer(stagingBuffer);

    std::cout << "[BitmapFont] Font initialized with " << glyphs_.size()
              << " glyphs, fontSize=" << fontSize
              << ", atlas=" << ATLAS_WIDTH << "x" << ATLAS_HEIGHT << " R8\n";

    return true;
}

void BitmapFont::Cleanup() {
    if (rhi_ && m_atlasTexture) {
        rhi_->DestroyTexture(m_atlasTexture);
    }
    m_atlasTexture = nullptr;
    m_atlasPixels.clear();
    m_atlasPixels.shrink_to_fit();
    glyphs_.clear();
    rhi_ = nullptr;
}

const GlyphInfo* BitmapFont::GetGlyph(char c) const {
    auto it = glyphs_.find(c);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    // Return space for unknown characters
    it = glyphs_.find(' ');
    if (it != glyphs_.end()) {
        return &it->second;
    }
    return nullptr;
}

float BitmapFont::CalculateTextWidth(const char* text) const {
    if (!text) return 0.0f;
    
    float width = 0.0f;
    for (const char* p = text; *p; ++p) {
        const GlyphInfo* glyph = GetGlyph(*p);
        if (glyph) {
            width += glyph->xAdvance;
        }
    }
    return width;
}

} // namespace CatEngine::Renderer

