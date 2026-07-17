#include "HUD.hpp"
#include "../audio/GameAudio.hpp"
#include "../config/WebParityConfig.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Game {

// ============================================================================
// HUD drawing helpers (file-local)
//
// The native HUD reproduces the web survival HUD (src/components/ui/*) with
// Dear ImGui draw-list primitives. The OpenSans font atlas carries NO emoji
// glyphs, so every web emoji icon (🐱 cat, ❤️ heart, 💧 water drop, ⚔️ swords,
// 🏹 bow, 🛡️ shield) is APPROXIMATED here with hand-drawn shapes tinted to the
// web item colour. Each helper documents what it approximates; the shapes are
// intentionally minimal (a few primitives) so they stay legible at the 64px
// slot / pill scale rather than trying to be faithful glyphs.
// ============================================================================
namespace {

namespace WebParity = CatGame::WebParity;

// A web ColorSwatch (sRGB bytes) -> an ImGui packed colour. ImGui composites
// in swapchain space, so the web hex bytes are used raw (no srgb->linear).
inline ImU32 swatchColor(const WebParity::ColorSwatch& swatch, int alpha = 255) {
    return IM_COL32(swatch.red, swatch.green, swatch.blue, alpha);
}

// 🐱 — a cat head: a round face plus two triangular ears. Stands in for the
// web CatStats "🐱 Lv.N" glyph.
void drawCatIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float earHalf = size * 0.45F;
    // Ears (drawn first so the face circle overlaps their base).
    draw->AddTriangleFilled(ImVec2(center.x - size * 0.75F, center.y - size * 0.2F),
                            ImVec2(center.x - size * 0.15F, center.y - size * 0.2F),
                            ImVec2(center.x - earHalf, center.y - size * 0.95F), color);
    draw->AddTriangleFilled(ImVec2(center.x + size * 0.75F, center.y - size * 0.2F),
                            ImVec2(center.x + size * 0.15F, center.y - size * 0.2F),
                            ImVec2(center.x + earHalf, center.y - size * 0.95F), color);
    draw->AddCircleFilled(ImVec2(center.x, center.y + size * 0.05F), size * 0.6F, color);
}

// ❤️ — a heart: two lobes over a downward triangle. Web CatStats health icon.
void drawHeartIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddCircleFilled(ImVec2(center.x - size * 0.28F, center.y - size * 0.12F), size * 0.34F, color);
    draw->AddCircleFilled(ImVec2(center.x + size * 0.28F, center.y - size * 0.12F), size * 0.34F, color);
    draw->AddTriangleFilled(ImVec2(center.x - size * 0.58F, center.y + size * 0.02F),
                            ImVec2(center.x + size * 0.58F, center.y + size * 0.02F),
                            ImVec2(center.x, center.y + size * 0.7F), color);
}

// 💧 — a water drop: a rounded body under a pointed apex. Hotbar slot 1.
void drawDropIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddCircleFilled(ImVec2(center.x, center.y + size * 0.25F), size * 0.55F, color);
    draw->AddTriangleFilled(ImVec2(center.x, center.y - size * 0.85F),
                            ImVec2(center.x - size * 0.5F, center.y + size * 0.2F),
                            ImVec2(center.x + size * 0.5F, center.y + size * 0.2F), color);
}

// ⚔️ — crossed swords: two blades forming an X with short crossguards. Slot 2.
void drawSwordsIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddLine(ImVec2(center.x - size, center.y + size), ImVec2(center.x + size, center.y - size), color, 3.0F);
    draw->AddLine(ImVec2(center.x - size, center.y - size), ImVec2(center.x + size, center.y + size), color, 3.0F);
    // Small crossguards near the two lower hilts.
    draw->AddLine(ImVec2(center.x - size * 1.1F, center.y + size * 0.5F),
                  ImVec2(center.x - size * 0.4F, center.y + size * 1.05F), color, 2.0F);
    draw->AddLine(ImVec2(center.x + size * 1.1F, center.y + size * 0.5F),
                  ImVec2(center.x + size * 0.4F, center.y + size * 1.05F), color, 2.0F);
}

// 🏹 — a bow: a C-shaped arc, a straight string, and a nocked arrow. Slot 3.
void drawBowIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->PathArcTo(ImVec2(center.x - size * 0.55F, center.y), size * 1.35F, -0.85F, 0.85F, 16);
    draw->PathStroke(color, 0, 2.5F);
    // Bowstring: chord across the arc's open (right) side.
    draw->AddLine(ImVec2(center.x + size * 0.45F, center.y - size * 0.95F),
                  ImVec2(center.x + size * 0.45F, center.y + size * 0.95F), color, 1.5F);
    // Arrow shaft + head pointing right.
    draw->AddLine(ImVec2(center.x - size * 0.7F, center.y), ImVec2(center.x + size * 1.0F, center.y), color, 1.5F);
    draw->AddLine(ImVec2(center.x + size * 1.0F, center.y), ImVec2(center.x + size * 0.55F, center.y - size * 0.35F), color, 1.5F);
    draw->AddLine(ImVec2(center.x + size * 1.0F, center.y), ImVec2(center.x + size * 0.55F, center.y + size * 0.35F), color, 1.5F);
}

// 🛡️ — a shield: a heater-shield pentagon, filled with a subtle border. Slot 4.
void drawShieldIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const ImVec2 points[5] = {
        ImVec2(center.x - size * 0.7F, center.y - size * 0.8F),
        ImVec2(center.x + size * 0.7F, center.y - size * 0.8F),
        ImVec2(center.x + size * 0.7F, center.y + size * 0.2F),
        ImVec2(center.x, center.y + size * 0.95F),
        ImVec2(center.x - size * 0.7F, center.y + size * 0.2F),
    };
    draw->AddConvexPolyFilled(points, 5, color);
    draw->AddPolyline(points, 5, IM_COL32(0, 0, 0, 120), ImDrawFlags_Closed, 1.0F);
}

// Project a world point through a view-projection matrix to ImGui screen
// pixels. Returns false when the point is at/behind the camera (clip.w <= 0),
// in which case outScreen is untouched. Kept self-contained so the HUD owns
// the same projection the scene camera uses (Camera::UpdateViewMatrix +
// mat4::perspective). NDC->pixel uses the GL/three.js "up is up" mapping
// (screenY grows downward), which matches the engine's flipped-viewport scene
// render; outClipW carries the perspective w for distance-scaling the bar.
bool projectWorldPoint(const Engine::mat4& viewProj, const Engine::vec3& world,
                       float screenWidth, float screenHeight,
                       ImVec2& outScreen, float& outClipW) {
    const Engine::vec4 clip = viewProj * Engine::vec4(world, 1.0F);
    if (clip.w <= 0.0001F) {
        return false;  // behind or on the camera plane — not on screen
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    outScreen = ImVec2((ndcX + 1.0F) * 0.5F * screenWidth,
                       (1.0F - ndcY) * 0.5F * screenHeight);
    outClipW = clip.w;
    return true;
}

} // namespace

HUD::HUD(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

HUD::~HUD() {
    shutdown();
}

bool HUD::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("HUD already initialized");
        return true;
    }

    // Initialize with default values
    m_currentHealth = 100.0F;
    m_maxHealth = 100.0F;
    m_currentWave = 1;
    m_remainingEnemies = 0;
    m_totalEnemies = 0;
    m_score = 0;
    m_combo = 0;

    m_initialized = true;
    Engine::Logger::info("HUD initialized successfully");
    return true;
}

void HUD::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_damageIndicators.clear();
    m_damageNumbers.clear();

    m_initialized = false;
    Engine::Logger::info("HUD shutdown");
}

void HUD::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update damage indicators
    updateDamageIndicators(deltaTime);

    // Update damage numbers
    updateDamageNumbers(deltaTime);

    // Update notifications
    updateNotifications(deltaTime);

    // Update low health warning pulse
    if (m_lowHealthWarning) {
        m_lowHealthPulse += deltaTime * 4.0F;
    }

    // Update combo display timer
    if (m_combo > 0) {
        m_comboDisplayTime += deltaTime;
    } else {
        m_comboDisplayTime = 0.0F;
    }
}

void HUD::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized) {
        return;
    }

    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // All HUD rendering goes through Dear ImGui now — proper fonts, drawlist
    // primitives, and immediate-mode layout. UIPass quads aren't used.
    (void)uiPass;
    if (m_imguiLayer == nullptr) {
        return;
    }

    const float width = static_cast<float>(screenWidth);
    const float height = static_cast<float>(screenHeight);
    const float healthRatio = (m_maxHealth > 0.0F)
        ? std::clamp(m_currentHealth / m_maxHealth, 0.0F, 1.0F)
        : 0.0F;

    ImFont* regular = m_imguiLayer->GetRegularFont();
    ImFont* bold = m_imguiLayer->GetBoldFont();

    // Full-screen transparent overlay for the HUD so we can place widgets anywhere.
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));

    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;

    ImGui::Begin("##HUDOverlay", nullptr, kOverlayFlags);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // The whole in-game HUD is a 1:1 rebuild of the web survival HUD
    // (src/components/ui/*). Layout + colours cite WebParityConfig.hpp (the
    // "In-game HUD" block), which cites the web CSS/TSX line-by-line. Under
    // WebParity::kEnabled the native flavor (bottom-left HP stack, yellow
    // WAVE text, permanent dog counter, SCORE, combo) is REPLACED by the web
    // layout; the old flavor is preserved on the !kEnabled branch below.
    namespace WP = CatGame::WebParity;
    ImFont* title = m_imguiLayer->GetTitleFont();

    if (WP::kEnabled) {
        // ================================================================
        // Bottom-centre STATUS PILL (web CatStats.tsx + ui.css
        // .cat-stats-container). One dark rounded bar holding four
        // separator-divided segments: cat "Lv.N" (orange) | XP bar +
        // "cur/next" text | heart + "HP/max" (red) | "Next Lv.N: ability".
        // ================================================================
        const float pillH = WP::kHudPillHeight;
        const float pillMidPad = 16.0F;   // ui.css:22 .cat-stats-section padding 0 16px

        // -- Measure each segment's content so the pill can be sized to fit
        //    and centred (the web bar is shrink-to-content, not fixed width).
        char levelText[32];
        std::snprintf(levelText, sizeof(levelText), "Lv.%u", m_catLevel);
        char hpText[48];
        std::snprintf(hpText, sizeof(hpText), "%d/%d",
                      static_cast<int>(std::floor(m_currentHealth)),
                      static_cast<int>(m_maxHealth));

        // Reconstruct the pill's ABSOLUTE XP text ("0/104" on a fresh run)
        // from the cat level + the 0..1 progress, via the same catXpForLevel
        // curve the web store uses (CatStats.tsx:33-36,140). No extra plumbing.
        const int catLevel = static_cast<int>(std::max<uint32_t>(m_catLevel, 1));
        const float xpProg = std::clamp(m_xpProgress, 0.0F, 1.0F);
        const float xpCurTotal = (catLevel <= 1) ? 0.0F : WP::catXpForLevel(catLevel);
        const float xpNextTotal = WP::catXpForLevel(catLevel + 1);
        const float xpAbsolute = xpCurTotal + xpProg * (xpNextTotal - xpCurTotal);
        char xpText[48];
        std::snprintf(xpText, sizeof(xpText), "%d/%d",
                      static_cast<int>(std::lround(xpAbsolute)),
                      static_cast<int>(std::lround(xpNextTotal)));

        if (bold != nullptr) { ImGui::PushFont(bold); }
        const ImVec2 levelSize = ImGui::CalcTextSize(levelText);
        const ImVec2 hpSize = ImGui::CalcTextSize(hpText);
        if (bold != nullptr) { ImGui::PopFont(); }
        if (regular != nullptr) { ImGui::PushFont(regular); }
        const ImVec2 xpTextSize = ImGui::CalcTextSize(xpText);
        const ImVec2 nextSize = m_abilityLine.empty()
            ? ImVec2(0.0F, 0.0F) : ImGui::CalcTextSize(m_abilityLine.c_str());
        if (regular != nullptr) { ImGui::PopFont(); }

        const float iconW = 20.0F;      // drawn cat / heart glyph reserve
        const float iconGap = 6.0F;     // ui.css:75 gap:6px
        const float xpColW = 120.0F;    // ui.css:44 .cat-xp-container min-width:120px
        const float nextMaxW = 200.0F;  // ui.css:141 .cat-next-ability max-width ~150-200

        const float seg1W = iconW + iconGap + levelSize.x;
        const float seg2W = std::max(xpColW, xpTextSize.x);
        const float seg3W = iconW + iconGap + hpSize.x;
        const bool hasNext = !m_abilityLine.empty();
        const float seg4W = hasNext ? std::min(nextSize.x, nextMaxW) : 0.0F;

        auto segTotal = [&](float contentW) { return contentW + pillMidPad * 2.0F; };
        float pillW = segTotal(seg1W) + segTotal(seg2W) + segTotal(seg3W);
        if (hasNext) { pillW += segTotal(seg4W); }
        const int sepCount = hasNext ? 3 : 2;
        pillW += static_cast<float>(sepCount);  // 1px separators

        const float pillX = (width - pillW) * 0.5F;
        const float pillY = height - WP::kHudPillBottomMargin - pillH;
        const float pillMidY = pillY + pillH * 0.5F;

        // Pill background + 1px light border (ui.css:9,11,14).
        draw->AddRectFilled(ImVec2(pillX, pillY), ImVec2(pillX + pillW, pillY + pillH),
                            swatchColor(WP::kHudPillBg, WP::kHudPillBgAlpha),
                            WP::kHudPillCornerRadius);
        draw->AddRect(ImVec2(pillX, pillY), ImVec2(pillX + pillW, pillY + pillH),
                      IM_COL32(255, 255, 255, 26), WP::kHudPillCornerRadius, 0, 1.0F);

        float cursorX = pillX;
        auto drawSeparator = [&](float atX) {
            draw->AddLine(ImVec2(atX, pillY + 8.0F), ImVec2(atX, pillY + pillH - 8.0F),
                          IM_COL32(255, 255, 255, 26), 1.0F);
        };

        // -- Segment 1: cat icon + "Lv.N" (orange #ff6b35, ui.css:32).
        {
            const float cx = cursorX + pillMidPad + iconW * 0.5F;
            drawCatIcon(draw, ImVec2(cx, pillMidY), 9.0F, swatchColor(WP::kHudCatLevelColor));
            if (bold != nullptr) { ImGui::PushFont(bold); }
            draw->AddText(ImVec2(cursorX + pillMidPad + iconW + iconGap, pillMidY - levelSize.y * 0.5F),
                          swatchColor(WP::kHudCatLevelColor), levelText);
            if (bold != nullptr) { ImGui::PopFont(); }
            cursorX += segTotal(seg1W);
        }
        drawSeparator(cursorX); cursorX += 1.0F;

        // -- Segment 2: XP bar (track #374151, amber gradient #fbbf24->#f59e0b,
        //    ui.css:50/58) with the "cur/next" text centred beneath it.
        {
            const float barX = cursorX + pillMidPad;
            const float barW = seg2W;
            const float barH = WP::kHudXpBarHeight;
            const float barTop = pillMidY - barH - 4.0F;
            draw->AddRectFilled(ImVec2(barX, barTop), ImVec2(barX + barW, barTop + barH),
                                swatchColor(WP::kHudXpTrackColor), 3.0F);
            if (xpProg > 0.0F) {
                const float fillR = barX + barW * xpProg;
                draw->AddRectFilledMultiColor(
                    ImVec2(barX, barTop), ImVec2(fillR, barTop + barH),
                    swatchColor(WP::kHudXpFillStart), swatchColor(WP::kHudXpFillEnd),
                    swatchColor(WP::kHudXpFillEnd), swatchColor(WP::kHudXpFillStart));
            }
            draw->AddRect(ImVec2(barX, barTop), ImVec2(barX + barW, barTop + barH),
                          IM_COL32(75, 85, 99, 255), 3.0F, 0, 1.0F);  // #4b5563 (ui.css:53)
            if (regular != nullptr) { ImGui::PushFont(regular); }
            draw->AddText(ImVec2(barX + (barW - xpTextSize.x) * 0.5F, barTop + barH + 3.0F),
                          swatchColor(WP::kHudXpTextColor), xpText);
            if (regular != nullptr) { ImGui::PopFont(); }
            cursorX += segTotal(seg2W);
        }
        drawSeparator(cursorX); cursorX += 1.0F;

        // -- Segment 3: heart icon + "HP/max" (red #ef4444, ui.css:83).
        {
            const float cx = cursorX + pillMidPad + iconW * 0.5F;
            drawHeartIcon(draw, ImVec2(cx, pillMidY), 9.0F, swatchColor(WP::kHudHealthColor));
            if (bold != nullptr) { ImGui::PushFont(bold); }
            draw->AddText(ImVec2(cursorX + pillMidPad + iconW + iconGap, pillMidY - hpSize.y * 0.5F),
                          swatchColor(WP::kHudHealthColor), hpText);
            if (bold != nullptr) { ImGui::PopFont(); }
            cursorX += segTotal(seg3W);
        }

        // -- Segment 4: "Next Lv.N: <ability>" hint (muted grey #9ca3af,
        //    ui.css:138). Composed by the game layer (m_abilityLine); clipped
        //    to the segment so a long ability name can't overrun the pill.
        if (hasNext) {
            drawSeparator(cursorX); cursorX += 1.0F;
            const float textX = cursorX + pillMidPad;
            if (regular != nullptr) { ImGui::PushFont(regular); }
            draw->PushClipRect(ImVec2(textX, pillY), ImVec2(textX + nextMaxW, pillY + pillH), true);
            draw->AddText(ImVec2(textX, pillMidY - nextSize.y * 0.5F),
                          swatchColor(WP::kHudNextAbilityColor), m_abilityLine.c_str());
            draw->PopClipRect();
            if (regular != nullptr) { ImGui::PopFont(); }
        }

        // ================================================================
        // HOTBAR (web InventoryHotbar.tsx + inventory.css). 9 dark squares
        // below the pill; slots 1-4 seed water-spell / sword / bow / shield
        // (gameStore.ts initialInventory), 5-9 empty. Active slot gets a
        // coloured ring + a size pop, and the active item name prints to its
        // right. Icons are hand-drawn (no emoji glyphs) — see the helpers.
        // ================================================================
        const float slotSize = WP::kHudHotbarSlotSize;
        const float slotGap = WP::kHudHotbarSlotGap;
        const int slotCount = WP::kHotbarSlotCount;
        const float hotbarW = slotCount * slotSize + (slotCount - 1) * slotGap;
        const float hotbarX = (width - hotbarW) * 0.5F;
        const float slotTop = height - WP::kHudHotbarBottomMargin - slotSize;
        const int activeIndex = m_activeWeaponName.empty()
            ? -1 : static_cast<int>(m_activeWeaponSlot) - 1;

        for (int slot = 0; slot < slotCount; ++slot) {
            const float sx = hotbarX + slot * (slotSize + slotGap);
            const bool active = (slot == activeIndex);

            // The active slot's ring colour is the item's tint (web active-slot
            // theming, inventory.css:33-36) — teal for the water drop, silver
            // for sword/shield, brown for bow; empty active slots fall back to
            // the amber active border (inventory.css:27).
            WP::ColorSwatch itemTint = WP::kHudHotbarActiveBorder;
            switch (slot) {
                case 0: itemTint = WP::kHudItemWater;  break;
                case 1: itemTint = WP::kHudItemSword;  break;
                case 2: itemTint = WP::kHudItemBow;    break;
                case 3: itemTint = WP::kHudItemShield; break;
                default: break;
            }

            // Active slot pops slightly larger (web transform: scale(1.1)).
            const float pop = active ? 3.0F : 0.0F;
            const ImVec2 slotMin(sx - pop, slotTop - pop);
            const ImVec2 slotMax(sx + slotSize + pop, slotTop + slotSize + pop);
            draw->AddRectFilled(slotMin, slotMax,
                                swatchColor(active ? WP::kHudHotbarActiveSlotBg : WP::kHudHotbarSlotBg),
                                WP::kHudHotbarSlotRadius);
            draw->AddRect(slotMin, slotMax,
                          swatchColor(active ? itemTint : WP::kHudHotbarInactiveBorder),
                          WP::kHudHotbarSlotRadius, 0, active ? 3.0F : 2.0F);

            const ImVec2 iconC(sx + slotSize * 0.5F, slotTop + slotSize * 0.5F - 4.0F);
            const float iconS = 13.0F;
            switch (slot) {
                case 0: drawDropIcon(draw, iconC, iconS, swatchColor(WP::kHudItemWater));   break;
                case 1: drawSwordsIcon(draw, iconC, iconS, swatchColor(WP::kHudItemSword)); break;
                case 2: drawBowIcon(draw, iconC, iconS, swatchColor(WP::kHudItemBow));      break;
                case 3: drawShieldIcon(draw, iconC, iconS, swatchColor(WP::kHudItemShield));break;
                default: {
                    // Empty slot: big muted number centred (inventory.css:57 #4b5563).
                    char num[4];
                    std::snprintf(num, sizeof(num), "%d", slot + 1);
                    if (bold != nullptr) { ImGui::PushFont(bold); }
                    const ImVec2 numSize = ImGui::CalcTextSize(num);
                    draw->AddText(ImVec2(iconC.x - numSize.x * 0.5F, iconC.y - numSize.y * 0.5F),
                                  swatchColor(WP::kHudSlotNumberColor), num);
                    if (bold != nullptr) { ImGui::PopFont(); }
                    break;
                }
            }

            // White key label at the slot's bottom edge (inventory.css:61-68).
            char key[4];
            std::snprintf(key, sizeof(key), "%d", slot + 1);
            if (regular != nullptr) { ImGui::PushFont(regular); }
            const ImVec2 keySize = ImGui::CalcTextSize(key);
            draw->AddText(ImVec2(sx + slotSize * 0.5F - keySize.x * 0.5F,
                                 slotTop + slotSize - keySize.y - 2.0F),
                          IM_COL32(255, 255, 255, 235), key);
            if (regular != nullptr) { ImGui::PopFont(); }
        }

        // Active item name to the right of the strip (inventory.css:71-79).
        if (!m_activeWeaponName.empty()) {
            if (bold != nullptr) { ImGui::PushFont(bold); }
            const ImVec2 nameSize = ImGui::CalcTextSize(m_activeWeaponName.c_str());
            draw->AddText(ImVec2(hotbarX + hotbarW + 16.0F, slotTop + slotSize * 0.5F - nameSize.y * 0.5F),
                          IM_COL32(255, 255, 255, 245), m_activeWeaponName.c_str());
            if (bold != nullptr) { ImGui::PopFont(); }
        }

        // ================================================================
        // WEAPON-SKILL CARD (web WeaponSkills.tsx + ui.css). Bottom-right
        // (ui.css:170-171 bottom:6rem right:1rem — confirmed by web_03; the
        // task's "top-right" wording would DIVERGE from the reference, so the
        // web position wins). Shows the ACTIVE weapon's skill only; the shield
        // has no skill (web returns null), so the card hides for it.
        // ================================================================
        {
            const char* skillTitle = nullptr;
            WP::ColorSwatch skillColor = WP::kHudWeaponWaterColor;
            if (m_activeWeaponName == "Water Spell") {
                skillTitle = "Water Magic"; skillColor = WP::kHudWeaponWaterColor;
            } else if (m_activeWeaponName == "Sword") {
                skillTitle = "Sword"; skillColor = WP::kHudWeaponSwordColor;
            } else if (m_activeWeaponName == "Bow") {
                skillTitle = "Bow"; skillColor = WP::kHudWeaponBowColor;
            }

            if (skillTitle != nullptr) {
                // Live skill numbers come from setActiveWeaponSkill (INTEGRATION:
                // fed from LevelingSystem for the active weapon/element). When
                // not yet fed (level <= 0), fall back to the web fresh-run start
                // state — level 1, 0 XP, next-level at weaponXpForLevel(2) = 132 —
                // so a new run's card reads exactly like web_03 ("Water Magic
                // Level 1", "0 / 132 XP", "132 XP to level 2") out of the box.
                const int wLevel = (m_weaponSkillLevel > 0) ? m_weaponSkillLevel : 1;
                const int wCur = (m_weaponSkillLevel > 0) ? m_weaponSkillCurrentXp : 0;
                const int wNext = (m_weaponSkillLevel > 0)
                    ? m_weaponSkillXpToNext : static_cast<int>(WP::weaponXpForLevel(2));

                // Progress fraction, web-exact (WeaponSkills.tsx:33-36): the bar
                // spans only the CURRENT level's XP window, so subtract the
                // level's cumulative floor via weaponXpForLevel.
                const float curLevelXp = (wLevel <= 1) ? 0.0F : WP::weaponXpForLevel(wLevel);
                const float need = static_cast<float>(wNext) - curLevelXp;
                const float into = static_cast<float>(wCur) - curLevelXp;
                const float pct = (need > 0.0F) ? std::clamp(into / need, 0.0F, 1.0F) : 0.0F;

                char titleText[64];
                std::snprintf(titleText, sizeof(titleText), "%s Level %d", skillTitle, wLevel);
                char xpLine[64];
                std::snprintf(xpLine, sizeof(xpLine), "%d / %d XP", wCur, wNext);
                char nextLine[64];
                std::snprintf(nextLine, sizeof(nextLine), "%d XP to level %d",
                              std::max(0, wNext - wCur), wLevel + 1);

                if (bold != nullptr) { ImGui::PushFont(bold); }
                const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
                if (bold != nullptr) { ImGui::PopFont(); }
                if (regular != nullptr) { ImGui::PushFont(regular); }
                const ImVec2 xpLineSize = ImGui::CalcTextSize(xpLine);
                const ImVec2 nextLineSize = ImGui::CalcTextSize(nextLine);
                if (regular != nullptr) { ImGui::PopFont(); }

                const float cardPad = 16.0F;
                const float cardW = std::max(WP::kHudWeaponPanelMinWidth, titleSize.x + cardPad * 2.0F);
                const float barH = 10.0F;
                const float cardH = cardPad + titleSize.y + 12.0F + barH + 8.0F +
                                    xpLineSize.y + 6.0F + nextLineSize.y + cardPad;
                const float cardX = width - 16.0F - cardW;
                const float cardBottom = height - WP::kHudPillBottomMargin;  // bottom:6rem
                const float cardY = cardBottom - cardH;

                draw->AddRectFilled(ImVec2(cardX, cardY), ImVec2(cardX + cardW, cardY + cardH),
                                    IM_COL32(0, 0, 0, 230), WP::kHudWeaponPanelRadius);  // ui.css:172
                draw->AddRect(ImVec2(cardX, cardY), ImVec2(cardX + cardW, cardY + cardH),
                              swatchColor(skillColor, 90), WP::kHudWeaponPanelRadius, 0, 2.0F);  // ui.css border rgba(color,0.3)

                float rowY = cardY + cardPad;
                if (bold != nullptr) { ImGui::PushFont(bold); }
                draw->AddText(ImVec2(cardX + (cardW - titleSize.x) * 0.5F, rowY),
                              swatchColor(skillColor), titleText);
                if (bold != nullptr) { ImGui::PopFont(); }
                rowY += titleSize.y + 12.0F;

                const float barX = cardX + cardPad;
                const float barW = cardW - cardPad * 2.0F;
                draw->AddRectFilled(ImVec2(barX, rowY), ImVec2(barX + barW, rowY + barH),
                                    IM_COL32(55, 65, 81, 204), 6.0F);  // rgba(55,65,81,0.8) ui.css:241
                if (pct > 0.0F) {
                    draw->AddRectFilled(ImVec2(barX, rowY), ImVec2(barX + barW * pct, rowY + barH),
                                        swatchColor(skillColor), 6.0F);
                }
                rowY += barH + 8.0F;

                if (regular != nullptr) { ImGui::PushFont(regular); }
                draw->AddText(ImVec2(cardX + (cardW - xpLineSize.x) * 0.5F, rowY),
                              swatchColor(WP::kHudXpTextColor), xpLine);  // #d1d5db ui.css:279
                rowY += xpLineSize.y + 6.0F;
                draw->AddText(ImVec2(cardX + (cardW - nextLineSize.x) * 0.5F, rowY),
                              swatchColor(WP::kHudNextAbilityColor), nextLine);  // #9ca3af ui.css:288
                if (regular != nullptr) { ImGui::PopFont(); }
            }
        }

        // ================================================================
        // WAVE BANNER (web WaveDisplay.tsx .wave-display-counter): big white
        // "ROUND N" over a "SURVIVE THE HORDE" subtitle, permanent, top:16px.
        // The native pre-parity "Dogs remaining: X/Y" counter has no web
        // counterpart during play, so it is removed under parity.
        // ================================================================
        {
            char roundText[32];
            std::snprintf(roundText, sizeof(roundText), "ROUND %u", m_currentWave);
            ImFont* bannerFont = (title != nullptr) ? title : bold;
            if (bannerFont != nullptr) { ImGui::PushFont(bannerFont); }
            const ImVec2 roundSize = ImGui::CalcTextSize(roundText);
            draw->AddText(ImVec2((width - roundSize.x) * 0.5F, WP::kHudWaveBannerTopMargin),
                          swatchColor(WP::kHudWaveTitleColor), roundText);
            if (bannerFont != nullptr) { ImGui::PopFont(); }

            const char* subtitle = "SURVIVE THE HORDE";
            if (bold != nullptr) { ImGui::PushFont(bold); }
            const ImVec2 subSize = ImGui::CalcTextSize(subtitle);
            draw->AddText(ImVec2((width - subSize.x) * 0.5F,
                                 WP::kHudWaveBannerTopMargin + roundSize.y + 4.0F),
                          swatchColor(WP::kHudWaveSubtitleColor), subtitle);
            if (bold != nullptr) { ImGui::PopFont(); }
        }
    } else {
        // ================================================================
        // Pre-parity NATIVE-FLAVOR HUD (WebParity::kEnabled == false): the
        // original bottom-left HP stack, yellow WAVE text + permanent dog
        // counter, top-right SCORE, and combo. Preserved verbatim behind the
        // flag so flipping parity off restores the native look for experiments.
        // ================================================================
        const float healthBarWidth = 360.0F;
        const float healthBarHeight = 32.0F;
        const float healthBarX = 32.0F;
        const float healthBarY = height - healthBarHeight - 120.0F;
        {
            const ImU32 bgColor = IM_COL32(20, 20, 30, 200);
            const ImU32 fillColor = (healthRatio < 0.3F)
                ? IM_COL32(230, 60, 50, 230)
                : IM_COL32(60, 200, 90, 230);
            const ImU32 borderColor = IM_COL32(255, 255, 255, 160);

            draw->AddRectFilled(ImVec2(healthBarX, healthBarY),
                                ImVec2(healthBarX + healthBarWidth, healthBarY + healthBarHeight), bgColor, 6.0F);
            draw->AddRectFilled(ImVec2(healthBarX, healthBarY),
                                ImVec2(healthBarX + healthBarWidth * healthRatio, healthBarY + healthBarHeight), fillColor, 6.0F);
            draw->AddRect(ImVec2(healthBarX, healthBarY),
                          ImVec2(healthBarX + healthBarWidth, healthBarY + healthBarHeight), borderColor, 6.0F, 0, 2.0F);

            if (regular != nullptr) { ImGui::PushFont(regular); }
            char label[64];
            std::snprintf(label, sizeof(label), "HP  %d / %d",
                          static_cast<int>(m_currentHealth), static_cast<int>(m_maxHealth));
            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(healthBarX + (healthBarWidth - labelSize.x) * 0.5F,
                                 healthBarY + (healthBarHeight - labelSize.y) * 0.5F),
                          IM_COL32(255, 255, 255, 240), label);
            if (regular != nullptr) { ImGui::PopFont(); }
        }

        if (!m_activeWeaponName.empty()) {
            if (bold != nullptr) { ImGui::PushFont(bold); }
            char weaponText[80];
            std::snprintf(weaponText, sizeof(weaponText), "%s  [%u]",
                          m_activeWeaponName.c_str(), m_activeWeaponSlot);
            const ImVec2 weaponSize = ImGui::CalcTextSize(weaponText);
            const float weaponY = healthBarY - weaponSize.y - 8.0F;
            draw->AddText(ImVec2(healthBarX, weaponY), IM_COL32(251, 191, 36, 245), weaponText);
            if (bold != nullptr) { ImGui::PopFont(); }
        }

        {
            const float xpRowY = healthBarY + healthBarHeight + 10.0F;
            const float xpBarHeight = 14.0F;
            if (bold != nullptr) { ImGui::PushFont(bold); }
            char levelLabel[32];
            std::snprintf(levelLabel, sizeof(levelLabel), "LVL %u", m_catLevel);
            const ImVec2 levelSize = ImGui::CalcTextSize(levelLabel);
            draw->AddText(ImVec2(healthBarX, xpRowY + (xpBarHeight - levelSize.y) * 0.5F),
                          IM_COL32(255, 107, 53, 245), levelLabel);
            if (bold != nullptr) { ImGui::PopFont(); }

            const float xpBarX = healthBarX + levelSize.x + 12.0F;
            const float xpBarRight = healthBarX + healthBarWidth;
            const float xpBarWidth = xpBarRight - xpBarX;
            const float xpRatio = std::clamp(m_xpProgress, 0.0F, 1.0F);
            const ImU32 xpTrackColor = IM_COL32(55, 65, 81, 220);
            const ImU32 xpBorderColor = IM_COL32(255, 255, 255, 120);
            draw->AddRectFilled(ImVec2(xpBarX, xpRowY),
                                ImVec2(xpBarRight, xpRowY + xpBarHeight), xpTrackColor, 4.0F);
            if (xpRatio > 0.0F) {
                const float xpFillRight = xpBarX + xpBarWidth * xpRatio;
                const ImU32 xpFillLeft = IM_COL32(251, 191, 36, 235);
                const ImU32 xpFillEnd = IM_COL32(245, 158, 11, 235);
                draw->AddRectFilledMultiColor(ImVec2(xpBarX, xpRowY),
                                              ImVec2(xpFillRight, xpRowY + xpBarHeight),
                                              xpFillLeft, xpFillEnd, xpFillEnd, xpFillLeft);
            }
            draw->AddRect(ImVec2(xpBarX, xpRowY),
                          ImVec2(xpBarRight, xpRowY + xpBarHeight), xpBorderColor, 4.0F, 0, 1.5F);
            if (!m_abilityLine.empty()) {
                const float abilityRowY = xpRowY + xpBarHeight + 6.0F;
                draw->AddText(ImVec2(healthBarX, abilityRowY),
                              IM_COL32(156, 163, 175, 220), m_abilityLine.c_str());
            }
        }

        {
            if (bold != nullptr) { ImGui::PushFont(bold); }
            char waveText[64];
            std::snprintf(waveText, sizeof(waveText), "WAVE  %u", m_currentWave);
            const ImVec2 waveSize = ImGui::CalcTextSize(waveText);
            draw->AddText(ImVec2((width - waveSize.x) * 0.5F, 20.0F),
                          IM_COL32(255, 220, 80, 255), waveText);
            if (bold != nullptr) { ImGui::PopFont(); }
            if (regular != nullptr) { ImGui::PushFont(regular); }
            char enemyText[64];
            std::snprintf(enemyText, sizeof(enemyText), "Dogs remaining: %u / %u",
                          m_remainingEnemies, m_totalEnemies);
            const ImVec2 enemySize = ImGui::CalcTextSize(enemyText);
            draw->AddText(ImVec2((width - enemySize.x) * 0.5F, 20.0F + waveSize.y + 4.0F),
                          IM_COL32(220, 220, 230, 220), enemyText);
            if (regular != nullptr) { ImGui::PopFont(); }
        }

        {
            if (bold != nullptr) { ImGui::PushFont(bold); }
            char scoreText[64];
            std::snprintf(scoreText, sizeof(scoreText), "SCORE  %u", m_score);
            const ImVec2 scoreSize = ImGui::CalcTextSize(scoreText);
            draw->AddText(ImVec2(width - scoreSize.x - 24.0F, 20.0F),
                          IM_COL32(255, 255, 255, 240), scoreText);
            if (bold != nullptr) { ImGui::PopFont(); }
        }

        if (m_combo > 1) {
            if (regular != nullptr) { ImGui::PushFont(regular); }
            char comboText[48];
            std::snprintf(comboText, sizeof(comboText), "Combo x%u", m_combo);
            const ImVec2 comboSize = ImGui::CalcTextSize(comboText);
            draw->AddText(ImVec2(width - comboSize.x - 24.0F, 60.0F),
                          IM_COL32(255, 180, 40, 230), comboText);
            if (regular != nullptr) { ImGui::PopFont(); }
        }
    }

    // ---------------------------------------- Enemy overhead health bars
    // Web LocalEnemySystem.tsx:483-494: a billboarded #333 track + health-tiered
    // fill floating 1.5 world-units above every LIVING dog. Projected here with
    // the fed camera view-projection and drawn on the foreground draw list so the
    // bars sit over the 3D dogs. Rendered only when the game layer has fed a
    // camera and at least one enemy this frame (INTEGRATION: see setEnemyBarCamera
    // / addEnemyBar); empty otherwise, so this is inert until wired.
    if (m_enemyBarCameraValid && !m_enemyBars.empty()) {
        const Engine::mat4 view =
            m_enemyBarCamera.rotation.toMatrix().transposed() *
            Engine::mat4::translate(Engine::vec3(-m_enemyBarCamera.position.x,
                                                 -m_enemyBarCamera.position.y,
                                                 -m_enemyBarCamera.position.z));
        const Engine::mat4 proj = Engine::mat4::perspective(
            m_enemyBarFovY, m_enemyBarAspect, m_enemyBarNear, m_enemyBarFar);
        const Engine::mat4 viewProj = proj * view;
        const Engine::vec3 camRight = m_enemyBarCamera.right();
        const float halfW = WP::kHudEnemyBarWorldWidth * 0.5F;
        ImDrawList* fg = ImGui::GetForegroundDrawList();

        for (const auto& bar : m_enemyBars) {
            const Engine::vec3 worldTop(bar.worldPosition.x,
                                        bar.worldPosition.y + WP::kHudEnemyBarWorldHeight,
                                        bar.worldPosition.z);
            const Engine::vec3 leftWorld(worldTop.x - camRight.x * halfW,
                                         worldTop.y - camRight.y * halfW,
                                         worldTop.z - camRight.z * halfW);
            const Engine::vec3 rightWorld(worldTop.x + camRight.x * halfW,
                                          worldTop.y + camRight.y * halfW,
                                          worldTop.z + camRight.z * halfW);

            ImVec2 centerScreen, leftScreen, rightScreen;
            float clipW = 0.0F;
            if (!projectWorldPoint(viewProj, worldTop, width, height, centerScreen, clipW) ||
                !projectWorldPoint(viewProj, leftWorld, width, height, leftScreen, clipW) ||
                !projectWorldPoint(viewProj, rightWorld, width, height, rightScreen, clipW)) {
                continue;  // off-screen / behind the camera
            }

            const float barPixW = std::max(6.0F, std::fabs(rightScreen.x - leftScreen.x));
            const float barPixH = std::max(2.0F,
                barPixW * (WP::kHudEnemyBarWorldBgHeight / WP::kHudEnemyBarWorldWidth));
            const float barLeft = centerScreen.x - barPixW * 0.5F;
            const float barTop = centerScreen.y - barPixH * 0.5F;

            // Background track (#333333, tsx:486).
            fg->AddRectFilled(ImVec2(barLeft, barTop), ImVec2(barLeft + barPixW, barTop + barPixH),
                              swatchColor(WP::kHudEnemyBarBg));
            // Health-tiered fill (tsx:491-492), left-anchored, slightly inset.
            const float ratio = std::clamp(bar.healthRatio, 0.0F, 1.0F);
            const WP::ColorSwatch fillSwatch =
                (ratio > WP::kHudEnemyBarHighThreshold) ? WP::kHudEnemyBarHigh
                : (ratio > WP::kHudEnemyBarMidThreshold) ? WP::kHudEnemyBarMid
                                                         : WP::kHudEnemyBarLow;
            const float fgInset = barPixH * 0.15F;
            const float fgH = barPixH - fgInset * 2.0F;
            fg->AddRectFilled(ImVec2(barLeft, barTop + fgInset),
                              ImVec2(barLeft + barPixW * ratio, barTop + fgInset + fgH),
                              swatchColor(fillSwatch));
        }
    }

    // -------------------------------------------------------- Crosshair (center)
    if (m_showCrosshair) {
        const float cx = width * 0.5F;
        const float cy = height * 0.5F;
        const ImU32 crossColor = IM_COL32(255, 255, 255, 180);
        draw->AddLine(ImVec2(cx - 10.0F, cy), ImVec2(cx - 3.0F, cy), crossColor, 2.0F);
        draw->AddLine(ImVec2(cx + 3.0F, cy), ImVec2(cx + 10.0F, cy), crossColor, 2.0F);
        draw->AddLine(ImVec2(cx, cy - 10.0F), ImVec2(cx, cy - 3.0F), crossColor, 2.0F);
        draw->AddLine(ImVec2(cx, cy + 3.0F), ImVec2(cx, cy + 10.0F), crossColor, 2.0F);
    }

    // --------------------------------------------------- FPS counter (top-left)
    if (m_showFPS) {
        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        char fpsText[32];
        std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", m_fps);
        draw->AddText(ImVec2(16.0F, 16.0F), IM_COL32(180, 180, 200, 220), fpsText);
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    // ------------------------------------- Low-health vignette (pulses red)
    // Native-only flourish gated off under web parity: the reference has no
    // screen-edge damage vignette (its only low-HP signal is the HP bar
    // itself), and the pulsing border tinted every parity frame-dump's
    // margins, muddying side-by-side comparisons. Flip WebParity::kEnabled
    // off and the effect returns with the rest of the native flavor.
    if (!CatGame::WebParity::kEnabled && m_lowHealthWarning) {
        const float pulse = (std::sin(m_lowHealthPulse * 6.0F) * 0.5F) + 0.5F;
        const ImU32 vignette = IM_COL32(180, 30, 30, static_cast<int>(60 + pulse * 80));
        draw->AddRect(ImVec2(0.0F, 0.0F), ImVec2(width, height), vignette, 0.0F, 0, 40.0F);
    }

    // --------------------------------------- Notifications (top-left column)
    {
        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        float notifY = 80.0F;
        for (const auto& notification : m_notifications) {
            const float fade = std::clamp(1.0F - (notification.elapsed / notification.duration), 0.0F, 1.0F);
            const auto baseColor = getNotificationColor(notification.type);
            const ImU32 color = IM_COL32(
                static_cast<int>(baseColor[0] * 255.0F),
                static_cast<int>(baseColor[1] * 255.0F),
                static_cast<int>(baseColor[2] * 255.0F),
                static_cast<int>(baseColor[3] * 255.0F * fade));
            draw->AddText(ImVec2(24.0F, notifY), color, notification.message.c_str());
            notifY += 26.0F;
        }
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// Data Setters
// ============================================================================

void HUD::setHealth(float current, float max) {
    m_currentHealth = current;
    m_maxHealth = max;

    // Auto-enable low health warning below 30%
    float healthPercent = current / max;
    if (healthPercent < 0.3F && healthPercent > 0.0F) {
        setLowHealthWarning(true);
    } else {
        setLowHealthWarning(false);
    }
}

void HUD::setWave(uint32_t wave) {
    m_currentWave = wave;
}

void HUD::setEnemyCount(uint32_t remaining, uint32_t total) {
    m_remainingEnemies = remaining;
    m_totalEnemies = total;
}

void HUD::setScore(uint32_t score) {
    m_score = score;
}

void HUD::setCombo(uint32_t combo) {
    m_combo = combo;
    if (combo > 0) {
        m_comboDisplayTime = 0.0F;
    }
}

void HUD::setActiveWeapon(const std::string& itemName, uint32_t slotNumber) {
    m_activeWeaponName = itemName;
    m_activeWeaponSlot = slotNumber;
}

void HUD::setCatLevel(uint32_t level) {
    m_catLevel = level;
}

void HUD::setXpProgress(float progress) {
    // Clamp on ingest as well as on render so a stray >1 (e.g. a level-up frame
    // before recalculation) can never overflow the bar fill.
    m_xpProgress = std::clamp(progress, 0.0F, 1.0F);
}

void HUD::setAbilityLine(const std::string& line) {
    m_abilityLine = line;
}

void HUD::setActiveWeaponSkill(int level, int currentXp, int xpToNextLevel) {
    // The HUD derives the card's title + colour from m_activeWeaponName; these
    // are just the raw numbers it cannot know. Level <= 0 hides the card (the
    // web has no skill for the shield). Store as-is; the render path clamps.
    m_weaponSkillLevel = level;
    m_weaponSkillCurrentXp = currentXp;
    m_weaponSkillXpToNext = xpToNextLevel;
}

void HUD::setEnemyBarCamera(const Engine::Transform& cameraTransform,
                            float fovYRadians, float aspect,
                            float nearPlane, float farPlane) {
    m_enemyBarCamera = cameraTransform;
    m_enemyBarFovY = fovYRadians;
    m_enemyBarAspect = aspect;
    m_enemyBarNear = nearPlane;
    m_enemyBarFar = farPlane;
    m_enemyBarCameraValid = true;
}

void HUD::clearEnemyBars() {
    // Cheap: keeps the vector's capacity so per-frame re-adds don't realloc.
    m_enemyBars.clear();
}

void HUD::addEnemyBar(const Engine::vec3& worldPosition, float healthRatio) {
    m_enemyBars.push_back(EnemyBar{worldPosition, healthRatio});
}

// ============================================================================
// Visual Effects
// ============================================================================

void HUD::showDamageIndicator(const std::array<float, 2>& direction, float intensity) {
    DamageIndicator indicator;
    indicator.direction = direction;
    indicator.intensity = intensity;
    indicator.lifetime = 0.0F;

    m_damageIndicators.push_back(indicator);
}

void HUD::showDamageNumber(float damage,
                           const std::array<float, 2>& screenPosition,
                           bool isCritical) {
    DamageNumber number;
    number.amount = damage;
    number.position = screenPosition;
    number.velocity = {0.0F, -50.0F};
    number.lifetime = 0.0F;
    number.isCritical = isCritical;
    number.isHeal = false;

    m_damageNumbers.push_back(number);
}

void HUD::showHealNumber(float amount, const std::array<float, 2>& screenPosition) {
    DamageNumber number;
    number.amount = amount;
    number.position = screenPosition;
    number.velocity = {0.0F, -50.0F};
    number.lifetime = 0.0F;
    number.isCritical = false;
    number.isHeal = true;

    m_damageNumbers.push_back(number);
}

void HUD::setLowHealthWarning(bool enable) {
    m_lowHealthWarning = enable;
    if (enable) {
        m_lowHealthPulse = 0.0F;
    }
}

void HUD::setFPS(float fps) {
    m_fps = fps;
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void HUD::renderHealthBar(CatEngine::Renderer::UIPass& uiPass) {
    float healthPercent = m_currentHealth / m_maxHealth;

    // Health bar background (top-left)
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 20.0F;
    bgQuad.y = 20.0F;
    bgQuad.width = 200.0F;
    bgQuad.height = 30.0F;
    bgQuad.r = 0.2F;
    bgQuad.g = 0.2F;
    bgQuad.b = 0.2F;
    bgQuad.a = 0.8F;
    bgQuad.depth = 0.0F;
    uiPass.DrawQuad(bgQuad);

    // Health bar fill
    CatEngine::Renderer::UIPass::QuadDesc fillQuad;
    fillQuad.x = 20.0F;
    fillQuad.y = 20.0F;
    fillQuad.width = 200.0F * healthPercent;
    fillQuad.height = 30.0F;
    if (healthPercent > 0.5F) {
        fillQuad.r = 0.0F;
        fillQuad.g = 0.8F;
        fillQuad.b = 0.0F;
    } else {
        fillQuad.r = 0.8F;
        fillQuad.g = 0.0F;
        fillQuad.b = 0.0F;
    }
    fillQuad.a = 1.0F;
    fillQuad.depth = 0.1F;
    uiPass.DrawQuad(fillQuad);

    // Health text
    std::string healthText = std::to_string(static_cast<int>(m_currentHealth)) + " / " +
                             std::to_string(static_cast<int>(m_maxHealth));
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = healthText.c_str();
    textDesc.x = 25.0F;
    textDesc.y = 25.0F;
    textDesc.fontSize = 16.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 1.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.2F;
    uiPass.DrawText(textDesc);
}

void HUD::renderWaveCounter(CatEngine::Renderer::UIPass& uiPass) {
    std::string waveText = "WAVE " + std::to_string(m_currentWave);
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = waveText.c_str();
    textDesc.x = (static_cast<float>(m_screenWidth) / 2.0F) - 50.0F;
    textDesc.y = 20.0F;
    textDesc.fontSize = 24.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

void HUD::renderEnemyCounter(CatEngine::Renderer::UIPass& uiPass) {
    std::string enemyText = "Enemies: " + std::to_string(m_remainingEnemies) +
                           " / " + std::to_string(m_totalEnemies);
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = enemyText.c_str();
    textDesc.x = static_cast<float>(m_screenWidth) - 200.0F;
    textDesc.y = 20.0F;
    textDesc.fontSize = 18.0F;
    textDesc.r = 1.0F;
    textDesc.g = 0.5F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

void HUD::renderScore(CatEngine::Renderer::UIPass& uiPass) {
    std::string scoreText = "Score: " + std::to_string(m_score);
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = scoreText.c_str();
    textDesc.x = 20.0F;
    textDesc.y = 60.0F;
    textDesc.fontSize = 18.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 1.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);

    // Render combo if active
    if (m_combo > 1 && m_comboDisplayTime < m_comboFadeTime) {
        float fadeAlpha = 1.0F - (m_comboDisplayTime / m_comboFadeTime);
        std::string comboText = "COMBO x" + std::to_string(m_combo);
        CatEngine::Renderer::UIPass::TextDesc comboDesc;
        comboDesc.text = comboText.c_str();
        comboDesc.x = 20.0F;
        comboDesc.y = 90.0F;
        comboDesc.fontSize = 22.0F;
        comboDesc.r = 1.0F;
        comboDesc.g = 0.8F;
        comboDesc.b = 0.0F;
        comboDesc.a = fadeAlpha;
        comboDesc.depth = 0.0F;
        uiPass.DrawText(comboDesc);
    }
}

void HUD::renderCrosshair(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float centerY = static_cast<float>(m_screenHeight) / 2.0F;
    float size = 10.0F;
    float thickness = 2.0F;

    // Horizontal line
    CatEngine::Renderer::UIPass::QuadDesc hLine;
    hLine.x = centerX - size;
    hLine.y = centerY - (thickness / 2.0F);
    hLine.width = size * 2.0F;
    hLine.height = thickness;
    hLine.r = 1.0F;
    hLine.g = 1.0F;
    hLine.b = 1.0F;
    hLine.a = 0.8F;
    hLine.depth = 0.0F;
    uiPass.DrawQuad(hLine);

    // Vertical line
    CatEngine::Renderer::UIPass::QuadDesc vLine;
    vLine.x = centerX - (thickness / 2.0F);
    vLine.y = centerY - size;
    vLine.width = thickness;
    vLine.height = size * 2.0F;
    vLine.r = 1.0F;
    vLine.g = 1.0F;
    vLine.b = 1.0F;
    vLine.a = 0.8F;
    vLine.depth = 0.0F;
    uiPass.DrawQuad(vLine);
}

void HUD::renderDamageIndicators(CatEngine::Renderer::UIPass& uiPass) {
    float screenCenterX = static_cast<float>(m_screenWidth) / 2.0F;
    float screenCenterY = static_cast<float>(m_screenHeight) / 2.0F;
    float edgeDistance = std::min(static_cast<float>(m_screenWidth),
                                   static_cast<float>(m_screenHeight)) * 0.4F;

    for (const auto& indicator : m_damageIndicators) {
        float alpha = 1.0F - (indicator.lifetime / indicator.maxLifetime);
        alpha *= indicator.intensity;

        // Calculate edge position based on direction
        float angle = std::atan2(indicator.direction[1], indicator.direction[0]);
        float edgeX = screenCenterX + std::cos(angle) * edgeDistance;
        float edgeY = screenCenterY + std::sin(angle) * edgeDistance;

        // Draw damage indicator as red quad
        CatEngine::Renderer::UIPass::QuadDesc indicatorQuad;
        indicatorQuad.x = edgeX - 15.0F;
        indicatorQuad.y = edgeY - 15.0F;
        indicatorQuad.width = 30.0F;
        indicatorQuad.height = 30.0F;
        indicatorQuad.r = 1.0F;
        indicatorQuad.g = 0.0F;
        indicatorQuad.b = 0.0F;
        indicatorQuad.a = alpha;
        indicatorQuad.depth = 0.5F;
        uiPass.DrawQuad(indicatorQuad);
    }
}

void HUD::renderDamageNumbers(CatEngine::Renderer::UIPass& uiPass) {
    for (const auto& number : m_damageNumbers) {
        float alpha = 1.0F - (number.lifetime / number.maxLifetime);

        float r, g, b;
        if (number.isHeal) {
            r = 0.0F; g = 1.0F; b = 0.0F;
        } else if (number.isCritical) {
            r = 1.0F; g = 0.5F; b = 0.0F;
        } else {
            r = 1.0F; g = 1.0F; b = 1.0F;
        }

        std::string text = number.isCritical ?
            std::to_string(static_cast<int>(number.amount)) + "!" :
            std::to_string(static_cast<int>(number.amount));

        float fontSize = number.isCritical ? 28.0F : 20.0F;

        CatEngine::Renderer::UIPass::TextDesc textDesc;
        textDesc.text = text.c_str();
        textDesc.x = number.position[0];
        textDesc.y = number.position[1];
        textDesc.fontSize = fontSize;
        textDesc.r = r;
        textDesc.g = g;
        textDesc.b = b;
        textDesc.a = alpha;
        textDesc.depth = 0.8F;
        uiPass.DrawText(textDesc);
    }
}

void HUD::renderLowHealthWarning(CatEngine::Renderer::UIPass& uiPass) {
    // Pulsing red glow at screen edges using vignette quads
    float pulseIntensity = (std::sin(m_lowHealthPulse) + 1.0F) * 0.5F;
    float alpha = 0.3F * pulseIntensity;

    // Top edge
    CatEngine::Renderer::UIPass::QuadDesc topEdge;
    topEdge.x = 0.0F;
    topEdge.y = 0.0F;
    topEdge.width = static_cast<float>(m_screenWidth);
    topEdge.height = 50.0F;
    topEdge.r = 1.0F;
    topEdge.g = 0.0F;
    topEdge.b = 0.0F;
    topEdge.a = alpha;
    topEdge.depth = 0.9F;
    uiPass.DrawQuad(topEdge);

    // Bottom edge
    CatEngine::Renderer::UIPass::QuadDesc bottomEdge;
    bottomEdge.x = 0.0F;
    bottomEdge.y = static_cast<float>(m_screenHeight) - 50.0F;
    bottomEdge.width = static_cast<float>(m_screenWidth);
    bottomEdge.height = 50.0F;
    bottomEdge.r = 1.0F;
    bottomEdge.g = 0.0F;
    bottomEdge.b = 0.0F;
    bottomEdge.a = alpha;
    bottomEdge.depth = 0.9F;
    uiPass.DrawQuad(bottomEdge);

    // Left edge
    CatEngine::Renderer::UIPass::QuadDesc leftEdge;
    leftEdge.x = 0.0F;
    leftEdge.y = 0.0F;
    leftEdge.width = 50.0F;
    leftEdge.height = static_cast<float>(m_screenHeight);
    leftEdge.r = 1.0F;
    leftEdge.g = 0.0F;
    leftEdge.b = 0.0F;
    leftEdge.a = alpha;
    leftEdge.depth = 0.9F;
    uiPass.DrawQuad(leftEdge);

    // Right edge
    CatEngine::Renderer::UIPass::QuadDesc rightEdge;
    rightEdge.x = static_cast<float>(m_screenWidth) - 50.0F;
    rightEdge.y = 0.0F;
    rightEdge.width = 50.0F;
    rightEdge.height = static_cast<float>(m_screenHeight);
    rightEdge.r = 1.0F;
    rightEdge.g = 0.0F;
    rightEdge.b = 0.0F;
    rightEdge.a = alpha;
    rightEdge.depth = 0.9F;
    uiPass.DrawQuad(rightEdge);
}

void HUD::renderFPS(CatEngine::Renderer::UIPass& uiPass) {
    std::string fpsText = "FPS: " + std::to_string(static_cast<int>(m_fps));
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = fpsText.c_str();
    textDesc.x = static_cast<float>(m_screenWidth) - 100.0F;
    textDesc.y = static_cast<float>(m_screenHeight) - 30.0F;
    textDesc.fontSize = 16.0F;
    textDesc.r = 0.0F;
    textDesc.g = 1.0F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

// ============================================================================
// Private Update Methods
// ============================================================================

void HUD::updateDamageIndicators(float deltaTime) {
    // Update and remove expired indicators
    m_damageIndicators.erase(
        std::remove_if(m_damageIndicators.begin(), m_damageIndicators.end(),
            [deltaTime](DamageIndicator& indicator) {
                indicator.lifetime += deltaTime;
                return indicator.lifetime >= indicator.maxLifetime;
            }),
        m_damageIndicators.end()
    );
}

void HUD::updateDamageNumbers(float deltaTime) {
    // Update positions and remove expired numbers
    for (auto& number : m_damageNumbers) {
        number.lifetime += deltaTime;
        number.position[0] += number.velocity[0] * deltaTime;
        number.position[1] += number.velocity[1] * deltaTime;

        // Slow down over time
        number.velocity[1] *= 0.95F;
    }

    m_damageNumbers.erase(
        std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
            [](const DamageNumber& number) {
                return number.lifetime >= number.maxLifetime;
            }),
        m_damageNumbers.end()
    );
}

// ============================================================================
// Notification System
// ============================================================================

void HUD::showNotification(const std::string& message, float duration, int priority) {
    Notification notification;
    notification.message = message;
    notification.type = NotificationType::Info;
    notification.duration = duration;
    notification.elapsed = 0.0F;
    notification.priority = priority;

    // Insert sorted by priority (higher priority first)
    auto insertPos = std::find_if(m_notifications.begin(), m_notifications.end(),
        [priority](const Notification& n) { return n.priority < priority; });
    m_notifications.insert(insertPos, notification);

    // Limit number of notifications
    if (m_notifications.size() > MAX_NOTIFICATIONS) {
        m_notifications.pop_back();
    }

    Engine::Logger::debug("Notification shown: " + message);
}

void HUD::showNotification(const std::string& message, const std::string& type, float duration) {
    NotificationType notificationType = NotificationType::Info;

    if (type == "success") {
        notificationType = NotificationType::Success;
    } else if (type == "warning") {
        notificationType = NotificationType::Warning;
    } else if (type == "error") {
        notificationType = NotificationType::Error;
    }

    Notification notification;
    notification.message = message;
    notification.type = notificationType;
    notification.duration = duration;
    notification.elapsed = 0.0F;
    if (notificationType == NotificationType::Error) {
        notification.priority = 10;
    } else if (notificationType == NotificationType::Warning) {
        notification.priority = 5;
    } else if (notificationType == NotificationType::Success) {
        notification.priority = 3;
    } else {
        notification.priority = 0;
    }

    // Insert sorted by priority (higher priority first)
    auto insertPos = std::find_if(m_notifications.begin(), m_notifications.end(),
        [&notification](const Notification& n) { return n.priority < notification.priority; });
    m_notifications.insert(insertPos, notification);

    // Limit number of notifications
    if (m_notifications.size() > MAX_NOTIFICATIONS) {
        m_notifications.pop_back();
    }

    Engine::Logger::debug("Notification shown (" + type + "): " + message.c_str());
}

void HUD::clearNotifications() {
    m_notifications.clear();
}

void HUD::updateNotifications(float deltaTime) {
    // Update elapsed time and remove expired notifications
    m_notifications.erase(
        std::remove_if(m_notifications.begin(), m_notifications.end(),
            [deltaTime](Notification& notification) {
                notification.elapsed += deltaTime;
                return notification.elapsed >= notification.duration;
            }),
        m_notifications.end()
    );
}

void HUD::renderNotifications(CatEngine::Renderer::UIPass& uiPass) {
    float yOffset = 60.0F;
    float xPosition = static_cast<float>(m_screenWidth) - 320.0F;
    float notificationHeight = 40.0F;
    float padding = 10.0F;

    for (const auto& notification : m_notifications) {
        // Calculate fade alpha (fade in first 0.2s, fade out last 0.5s)
        float fadeIn = std::min(notification.elapsed / 0.2F, 1.0F);
        float fadeOut = std::min((notification.duration - notification.elapsed) / 0.5F, 1.0F);
        float alpha = fadeIn * fadeOut;

        // Get color based on type
        std::array<float, 4> colorArr = getNotificationColor(notification.type);

        // Draw background
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = xPosition;
        bgQuad.y = yOffset;
        bgQuad.width = 300.0F;
        bgQuad.height = notificationHeight;
        bgQuad.r = 0.1F;
        bgQuad.g = 0.1F;
        bgQuad.b = 0.1F;
        bgQuad.a = 0.8F * alpha;
        bgQuad.depth = 0.7F;
        uiPass.DrawQuad(bgQuad);

        // Draw colored left border
        CatEngine::Renderer::UIPass::QuadDesc borderQuad;
        borderQuad.x = xPosition;
        borderQuad.y = yOffset;
        borderQuad.width = 4.0F;
        borderQuad.height = notificationHeight;
        borderQuad.r = colorArr[0];
        borderQuad.g = colorArr[1];
        borderQuad.b = colorArr[2];
        borderQuad.a = colorArr[3] * alpha;
        borderQuad.depth = 0.75F;
        uiPass.DrawQuad(borderQuad);

        // Draw text
        CatEngine::Renderer::UIPass::TextDesc textDesc;
        textDesc.text = notification.message.c_str();
        textDesc.x = xPosition + 12.0F;
        textDesc.y = yOffset + 10.0F;
        textDesc.fontSize = 16.0F;
        textDesc.r = 1.0F;
        textDesc.g = 1.0F;
        textDesc.b = 1.0F;
        textDesc.a = alpha;
        textDesc.depth = 0.8F;
        uiPass.DrawText(textDesc);

        yOffset += notificationHeight + padding;
    }
}

std::array<float, 4> HUD::getNotificationColor(NotificationType type) {
    switch (type) {
        case NotificationType::Success:
            return {0.2F, 0.8F, 0.2F, 1.0F};
        case NotificationType::Warning:
            return {1.0F, 0.8F, 0.0F, 1.0F};
        case NotificationType::Error:
            return {1.0F, 0.2F, 0.2F, 1.0F};
        case NotificationType::Info:
        default:
            return {0.3F, 0.6F, 1.0F, 1.0F};
    }
}

} // namespace Game
