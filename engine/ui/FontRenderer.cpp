#include "FontRenderer.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <limits>

// stb_truetype - public domain TrueType font parser
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace Engine::UI {

// =============================================================================
// FontAtlas Implementation
// =============================================================================

FontAtlas::FontAtlas(u32 width, u32 height)
    : m_width(width)
    , m_height(height)
    , m_data(width * height, 0)
    , m_fontSize(32.0f)
    , m_lineHeight(32.0f)
    , m_ascent(0.0f)
    , m_descent(0.0f)
{
}

FontAtlas::~FontAtlas() = default;

const GlyphMetrics* FontAtlas::GetGlyph(u32 codepoint) const {
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end()) {
        return &it->second;
    }
    return nullptr;
}

void FontAtlas::AddGlyph(u32 codepoint, const GlyphMetrics& metrics) {
    m_glyphs[codepoint] = metrics;
}

float FontAtlas::GetKerning(u32 first, u32 second) const {
    u64 key = (static_cast<u64>(first) << 32) | static_cast<u64>(second);
    auto it = m_kerningPairs.find(key);
    if (it != m_kerningPairs.end()) {
        return it->second;
    }
    return 0.0f;
}

void FontAtlas::SetKerning(u32 first, u32 second, float kerning) {
    u64 key = (static_cast<u64>(first) << 32) | static_cast<u64>(second);
    m_kerningPairs[key] = kerning;
}

// =============================================================================
// FontRenderer Implementation
// =============================================================================

FontRenderer::FontRenderer() = default;
FontRenderer::~FontRenderer() = default;

std::shared_ptr<FontAtlas> FontRenderer::LoadFont(
    const std::string& filepath,
    float fontSize,
    u32 atlasWidth,
    u32 atlasHeight,
    u32 sdfPadding,
    u8 sdfOnEdgeValue
) {
    // Read font file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return nullptr;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<u8> fontData(fileSize);
    if (!file.read(reinterpret_cast<char*>(fontData.data()), fileSize)) {
        return nullptr;
    }

    return LoadFontFromMemory(fontData.data(), fontData.size(), fontSize, atlasWidth, atlasHeight, sdfPadding, sdfOnEdgeValue);
}

std::shared_ptr<FontAtlas> FontRenderer::LoadFontFromMemory(
    const u8* data,
    size_t dataSize,
    float fontSize,
    u32 atlasWidth,
    u32 atlasHeight,
    u32 sdfPadding,
    u8 sdfOnEdgeValue
) {
    if (!data || dataSize == 0) {
        return nullptr;
    }

    // Initialize stb_truetype
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, data, 0)) {
        return nullptr;
    }

    // Create atlas
    auto atlas = std::make_shared<FontAtlas>(atlasWidth, atlasHeight);
    atlas->SetFontSize(fontSize);

    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);

    float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
    atlas->SetAscent(ascent * scale);
    atlas->SetDescent(descent * scale);
    atlas->SetLineHeight((ascent - descent + lineGap) * scale);

    // Pack common ASCII characters (32-126) plus some extended characters
    std::vector<u32> codepoints;
    for (u32 cp = 32; cp <= 126; ++cp) {
        codepoints.push_back(cp);
    }
    // Add some common extended characters
    for (u32 cp = 160; cp <= 255; ++cp) {
        codepoints.push_back(cp);
    }

    // Simple atlas packing (left-to-right, top-to-bottom)
    u32 currentX = sdfPadding;
    u32 currentY = sdfPadding;
    u32 rowHeight = 0;

    for (u32 codepoint : codepoints) {
        int glyphIndex = stbtt_FindGlyphIndex(&fontInfo, codepoint);
        if (glyphIndex == 0) continue; // Glyph not found

        // Get glyph bitmap
        int width, height, xoff, yoff;
        u8* bitmap = stbtt_GetGlyphBitmap(&fontInfo, 0, scale, glyphIndex, &width, &height, &xoff, &yoff);
        if (!bitmap) continue;

        // Calculate SDF output size
        u32 sdfWidth = width + sdfPadding * 2;
        u32 sdfHeight = height + sdfPadding * 2;

        // Check if we need to move to next row
        if (currentX + sdfWidth > atlasWidth) {
            currentX = sdfPadding;
            currentY += rowHeight + sdfPadding;
            rowHeight = 0;
        }

        // Check if we're out of vertical space
        if (currentY + sdfHeight > atlasHeight) {
            stbtt_FreeBitmap(bitmap, nullptr);
            break;
        }

        // Generate SDF and copy to atlas
        std::vector<u8> sdfBuffer(sdfWidth * sdfHeight);
        GenerateGlyphSDF(bitmap, width, height, sdfBuffer.data(), sdfWidth, sdfHeight, sdfPadding, sdfOnEdgeValue);

        // Copy to atlas
        u8* atlasData = atlas->GetData();
        for (u32 y = 0; y < sdfHeight; ++y) {
            for (u32 x = 0; x < sdfWidth; ++x) {
                u32 atlasX = currentX + x;
                u32 atlasY = currentY + y;
                atlasData[atlasY * atlasWidth + atlasX] = sdfBuffer[y * sdfWidth + x];
            }
        }

        // Get glyph metrics
        int advance, lsb;
        stbtt_GetGlyphHMetrics(&fontInfo, glyphIndex, &advance, &lsb);

        GlyphMetrics metrics;
        metrics.codepoint = codepoint;
        metrics.advance = advance * scale;
        metrics.bearingX = xoff;
        metrics.bearingY = yoff;
        metrics.width = sdfWidth;
        metrics.height = sdfHeight;
        metrics.uvX = static_cast<float>(currentX) / atlasWidth;
        metrics.uvY = static_cast<float>(currentY) / atlasHeight;
        metrics.uvWidth = static_cast<float>(sdfWidth) / atlasWidth;
        metrics.uvHeight = static_cast<float>(sdfHeight) / atlasHeight;

        atlas->AddGlyph(codepoint, metrics);

        // Store kerning pairs for this glyph
        for (u32 otherCodepoint : codepoints) {
            int otherGlyphIndex = stbtt_FindGlyphIndex(&fontInfo, otherCodepoint);
            if (otherGlyphIndex != 0) {
                int kern = stbtt_GetGlyphKernAdvance(&fontInfo, glyphIndex, otherGlyphIndex);
                if (kern != 0) {
                    atlas->SetKerning(codepoint, otherCodepoint, kern * scale);
                }
            }
        }

        // Update position
        currentX += sdfWidth + sdfPadding;
        rowHeight = std::max(rowHeight, sdfHeight);

        stbtt_FreeBitmap(bitmap, nullptr);
    }

    return atlas;
}

void FontRenderer::GenerateGlyphSDF(
    const u8* bitmap,
    u32 width,
    u32 height,
    u8* output,
    u32 outputWidth,
    u32 outputHeight,
    u32 padding,
    u8 onEdgeValue
) {
    // Simple SDF generation using brute-force distance calculation
    // For production, consider using a more efficient algorithm (e.g., 8SSEDT)

    const float maxDistance = static_cast<float>(padding);

    for (u32 oy = 0; oy < outputHeight; ++oy) {
        for (u32 ox = 0; ox < outputWidth; ++ox) {
            // Map output pixel to input pixel
            int ix = static_cast<int>(ox) - static_cast<int>(padding);
            int iy = static_cast<int>(oy) - static_cast<int>(padding);

            float minDist = maxDistance;
            bool inside = false;

            // Check if we're inside the glyph
            if (ix >= 0 && ix < static_cast<int>(width) && iy >= 0 && iy < static_cast<int>(height)) {
                inside = bitmap[iy * width + ix] > 128;
            }

            // Find minimum distance to edge
            for (int y = -static_cast<int>(padding); y < static_cast<int>(height) + static_cast<int>(padding); ++y) {
                for (int x = -static_cast<int>(padding); x < static_cast<int>(width) + static_cast<int>(padding); ++x) {
                    bool pixelInside = false;
                    if (x >= 0 && x < static_cast<int>(width) && y >= 0 && y < static_cast<int>(height)) {
                        pixelInside = bitmap[y * width + x] > 128;
                    }

                    // Check if this is an edge pixel
                    if (pixelInside != inside) {
                        float dx = static_cast<float>(ix - x);
                        float dy = static_cast<float>(iy - y);
                        float dist = std::sqrt(dx * dx + dy * dy);
                        minDist = std::min(minDist, dist);
                    }
                }
            }

            // Normalize distance to 0-255 range
            float normalizedDist = minDist / maxDistance;
            normalizedDist = std::min(1.0f, normalizedDist);

            u8 value;
            if (inside) {
                // Inside: 128-255
                value = static_cast<u8>(onEdgeValue + normalizedDist * (255 - onEdgeValue));
            } else {
                // Outside: 0-128
                value = static_cast<u8>(onEdgeValue - normalizedDist * onEdgeValue);
            }

            output[oy * outputWidth + ox] = value;
        }
    }
}

TextLayout FontRenderer::LayoutText(
    const FontAtlas& atlas,
    const std::string& text,
    float fontSize,
    float maxWidth
) const {
    TextLayout layout;
    layout.baseline = atlas.GetAscent() * (fontSize / atlas.GetFontSize());

    float scale = GetFontScale(atlas, fontSize);
    float x = 0.0f;
    float y = layout.baseline;

    size_t offset = 0;
    u32 prevCodepoint = 0;

    while (offset < text.size()) {
        u32 codepoint = DecodeUTF8(text, offset);
        if (codepoint == 0) break;

        // Handle newline
        if (codepoint == '\n') {
            x = 0.0f;
            y += atlas.GetLineHeight() * scale;
            prevCodepoint = 0;
            continue;
        }

        // Get glyph
        const GlyphMetrics* glyph = atlas.GetGlyph(codepoint);
        if (!glyph) {
            // Try to use space as fallback
            glyph = atlas.GetGlyph(' ');
            if (!glyph) {
                prevCodepoint = codepoint;
                continue;
            }
        }

        // Apply kerning
        if (prevCodepoint != 0) {
            x += atlas.GetKerning(prevCodepoint, codepoint) * scale;
        }

        // Check word wrap
        if (maxWidth > 0.0f && x + glyph->width * scale > maxWidth) {
            x = 0.0f;
            y += atlas.GetLineHeight() * scale;
        }

        // Add glyph instance
        TextLayout::GlyphInstance instance;
        instance.position = vec2(x + glyph->bearingX * scale, y + glyph->bearingY * scale);
        instance.size = vec2(glyph->width * scale, glyph->height * scale);
        instance.uvMin = vec2(glyph->uvX, glyph->uvY);
        instance.uvMax = vec2(glyph->uvX + glyph->uvWidth, glyph->uvY + glyph->uvHeight);
        instance.codepoint = codepoint;

        layout.glyphs.push_back(instance);

        x += glyph->advance * scale;
        prevCodepoint = codepoint;
    }

    // Calculate bounds
    if (!layout.glyphs.empty()) {
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (const auto& glyph : layout.glyphs) {
            maxX = std::max(maxX, glyph.position.x + glyph.size.x);
            maxY = std::max(maxY, glyph.position.y + glyph.size.y);
        }
        layout.bounds = vec2(maxX, maxY);
    }

    return layout;
}

vec2 FontRenderer::MeasureText(
    const FontAtlas& atlas,
    const std::string& text,
    float fontSize
) const {
    float scale = GetFontScale(atlas, fontSize);
    float width = 0.0f;
    float height = atlas.GetLineHeight() * scale;

    float currentLineWidth = 0.0f;
    size_t offset = 0;
    u32 prevCodepoint = 0;

    while (offset < text.size()) {
        u32 codepoint = DecodeUTF8(text, offset);
        if (codepoint == 0) break;

        if (codepoint == '\n') {
            width = std::max(width, currentLineWidth);
            currentLineWidth = 0.0f;
            height += atlas.GetLineHeight() * scale;
            prevCodepoint = 0;
            continue;
        }

        const GlyphMetrics* glyph = atlas.GetGlyph(codepoint);
        if (!glyph) {
            glyph = atlas.GetGlyph(' ');
            if (!glyph) {
                prevCodepoint = codepoint;
                continue;
            }
        }

        if (prevCodepoint != 0) {
            currentLineWidth += atlas.GetKerning(prevCodepoint, codepoint) * scale;
        }

        currentLineWidth += glyph->advance * scale;
        prevCodepoint = codepoint;
    }

    width = std::max(width, currentLineWidth);
    return vec2(width, height);
}

void FontRenderer::RenderText(
    const FontAtlas& atlas,
    const std::string& text,
    const vec2& position,
    float fontSize,
    const vec4& color,
    std::vector<vec2>& outVertices,
    std::vector<vec2>& outUVs,
    std::vector<u16>& outIndices
) const {
    TextLayout layout = LayoutText(atlas, text, fontSize, 0.0f);

    u16 vertexOffset = static_cast<u16>(outVertices.size());

    for (const auto& glyph : layout.glyphs) {
        vec2 pos = position + glyph.position;

        // Generate quad vertices
        outVertices.push_back(pos);
        outVertices.push_back(vec2(pos.x + glyph.size.x, pos.y));
        outVertices.push_back(vec2(pos.x + glyph.size.x, pos.y + glyph.size.y));
        outVertices.push_back(vec2(pos.x, pos.y + glyph.size.y));

        // Generate UVs
        outUVs.push_back(glyph.uvMin);
        outUVs.push_back(vec2(glyph.uvMax.x, glyph.uvMin.y));
        outUVs.push_back(glyph.uvMax);
        outUVs.push_back(vec2(glyph.uvMin.x, glyph.uvMax.y));

        // Generate indices (two triangles per quad)
        outIndices.push_back(vertexOffset + 0);
        outIndices.push_back(vertexOffset + 1);
        outIndices.push_back(vertexOffset + 2);
        outIndices.push_back(vertexOffset + 0);
        outIndices.push_back(vertexOffset + 2);
        outIndices.push_back(vertexOffset + 3);

        vertexOffset += 4;
    }
}

u32 FontRenderer::DecodeUTF8(const std::string& text, size_t& offset) const {
    if (offset >= text.size()) return 0;

    u8 c = text[offset++];

    // Single byte (ASCII)
    if ((c & 0x80) == 0) {
        return c;
    }

    // Multi-byte sequence
    u32 codepoint = 0;
    u32 bytes = 0;

    if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        bytes = 1;
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        bytes = 2;
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        bytes = 3;
    } else {
        return 0; // Invalid UTF-8
    }

    for (u32 i = 0; i < bytes && offset < text.size(); ++i) {
        c = text[offset++];
        if ((c & 0xC0) != 0x80) {
            return 0; // Invalid continuation byte
        }
        codepoint = (codepoint << 6) | (c & 0x3F);
    }

    return codepoint;
}

float FontRenderer::GetFontScale(const FontAtlas& atlas, float targetSize) const {
    return targetSize / atlas.GetFontSize();
}

} // namespace Engine::UI
