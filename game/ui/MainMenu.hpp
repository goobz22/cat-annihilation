#ifndef GAME_UI_MAIN_MENU_HPP
#define GAME_UI_MAIN_MENU_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include "../config/WebParityConfig.hpp"
#include <functional>
#include <vector>
#include <string>
#include <array>

namespace Engine {
    class ImGuiLayer;
    class Window;
}
namespace CatEngine::Renderer { class Renderer; }

namespace Game {

class GameAudio;
class GameConfig;

/**
 * @brief Main Menu screen — the web-parity pre-game flow.
 *
 * Mirrors the web reference's two-step pre-game flow
 * (src/components/ui/GameModeSelection.tsx): a MODE-SELECT page (survival /
 * story mode cards, plus the native-desktop Continue / Settings / Quit
 * extras the web doesn't have), then a CUSTOMIZE page ("Customize Your
 * Cat" fur-swatch picker) that the survival card opens. The run only
 * starts from the customize page's START GAME button, exactly like the
 * web's confirmSurvivalMode.
 */
class MainMenu {
public:
    /**
     * @brief Menu button callback type
     */
    using ButtonCallback = std::function<void()>;

    /**
     * @brief Which pre-game page the menu is showing.
     *
     * Maps 1:1 onto the web's showCustomization state
     * (GameModeSelection.tsx:16/175): mode select first, customize second.
     * The web's third page (clan selection) is deliberately absent — it
     * only exists on the story path, which is P3-deferred (PARITY_MATRIX.md).
     */
    enum class MenuPage {
        ModeSelect,
        Customize
    };

    explicit MainMenu(Engine::Input& input, GameAudio& audio);
    ~MainMenu();

    /**
     * @brief Initialize main menu
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown main menu
     */
    void shutdown();

    /**
     * @brief Update main menu (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render main menu
     * @param uiPass UIPass to use for 2D drawing
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     */
    void render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight);

    /**
     * @brief Handle input
     */
    void handleInput();

    // ========================================================================
    // Button Callbacks
    // ========================================================================

    /**
     * @brief Set callback that starts a new run.
     *
     * Fired by the customize page's START GAME button (and its Enter/Space
     * keyboard shortcut) — the mode-select Survival card only navigates to
     * that page, mirroring the web flow. Existing consumers keep working
     * unchanged: the wiring point and signature are identical, the button
     * that pulls the trigger just moved one page deeper.
     */
    void setStartGameCallback(ButtonCallback callback) {
        m_startGameCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Continue button
     */
    void setContinueCallback(ButtonCallback callback) {
        m_continueCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Settings button
     */
    void setSettingsCallback(ButtonCallback callback) {
        m_settingsCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Quit button
     */
    void setQuitCallback(ButtonCallback callback) {
        m_quitCallback = std::move(callback);
    }

    /**
     * @brief Set callback for the "Reset Progress" button.
     *
     * Web parity: GameModeSelection.tsx's Reset Progress control clears the
     * persisted game state (clearAllProgress → localStorage wipe) then
     * reloads (tsx:35-43). The native equivalent — wiping any leveling /
     * save state — lives in the GAME LAYER (which owns LevelingSystem and
     * the save path), not in the menu, so MainMenu exposes this seam instead
     * of reaching across ownership. If no callback is wired the button still
     * renders and runs its two-click confirm UX, but the confirm is INERT
     * (there is no persisted native leveling save to clear today) — a
     * deliberate "mechanism present, wiring deferred" state, documented so a
     * future game-layer owner can attach the real reset without touching the
     * menu draw code.
     */
    void setResetProgressCallback(ButtonCallback callback) {
        m_resetProgressCallback = std::move(callback);
    }

    // ========================================================================
    // State
    // ========================================================================

    /**
     * @brief Enable/disable Continue button (based on save existence)
     * @param hasSave true if save file exists
     */
    void setHasSaveGame(bool hasSave) { m_hasSaveGame = hasSave; }

    /**
     * @brief Which pre-game page is currently showing.
     */
    [[nodiscard]] MenuPage getCurrentPage() const { return m_currentPage; }

    /**
     * @brief True once the player confirmed a fur colour via START GAME.
     *
     * False for runs that bypass the customize page (Continue, --autoplay),
     * so the game layer knows whether the tint below is a real player
     * choice or should be left off entirely.
     */
    [[nodiscard]] bool hasSelectedFurColor() const { return m_furColorConfirmed; }

    /**
     * @brief Linear-decoded rgb of the chosen fur swatch.
     *
     * The swatch table (WebParityConfig kFurSwatches,
     * GameModeSelection.tsx:162) stores the web's sRGB hex bytes; this
     * decodes each channel to LINEAR because the consumer is the entity
     * tint push constant, which the shader multiplies in linear space —
     * see srgbChannelToLinear's rationale in WebParityConfig.hpp.
     */
    void getSelectedFurLinear(float& r, float& g, float& b) const;

    /**
     * @brief True once the player confirmed an eye colour via START GAME.
     *
     * Shares the single confirm latch with the fur colour: the web sets the
     * whole CatCustomization object (fur + eyes + pattern + …) in ONE call
     * inside confirmSurvivalMode (GameModeSelection.tsx:71), so there is no
     * state in which the fur is confirmed but the eyes are not. Returning
     * the same flag keeps that invariant explicit rather than tracking two
     * booleans that can never legitimately disagree.
     */
    [[nodiscard]] bool hasSelectedEyeColor() const { return m_furColorConfirmed; }

    /**
     * @brief Linear-decoded rgb of the chosen eye swatch.
     *
     * Same decode path as getSelectedFurLinear (WebParityConfig
     * srgbChannelToLinear): the eye swatch table stores the web's sRGB hex
     * bytes and the consumer would multiply the tint in linear space.
     *
     * NOTE — downstream limitation, not a bug in this accessor: the native
     * Meshy cat ships a BAKED eye texture, so the player-entity whole-body
     * tint (which recolours fur) cannot repaint just the eyes
     * (CatAnnihilation.cpp:2949-2950 records this). Making the picked eye
     * colour VISIBLE needs a separable eye material/mask on the model or a
     * shader that isolates the eye texels — an asset/shader change outside
     * the menu. Until then this value is a truthful, latched player choice
     * the picker exposes (mirroring the web's eyeColor), ready for whichever
     * subsystem gains the ability to apply it.
     */
    void getSelectedEyeLinear(float& r, float& g, float& b) const;

    /**
     * @brief Set version string to display
     * @param version Version string (e.g., "v1.0.0")
     */
    void setVersionString(const std::string& version) { m_versionString = version; }

    /**
     * @brief Attach the ImGui layer (used for fonts). Optional.
     */
    void setImGuiLayer(Engine::ImGuiLayer* imguiLayer) { m_imguiLayer = imguiLayer; }

    /**
     * @brief Wire the settings panel to the real engine systems.
     *
     * Passing non-null pointers here makes the sliders / checkboxes in the
     * Settings dialog actually affect runtime state (volumes, VSync,
     * fullscreen, sensitivity). Leaving any pointer null disables that one
     * row — useful for harnesses that construct a MainMenu without a
     * full engine backend.
     *
     * GameConfig is used both as the source of initial values (first open)
     * and the sink on Close (settings are persisted when the panel closes).
     */
    void setSettingsBindings(Engine::Window* window,
                             CatEngine::Renderer::Renderer* renderer,
                             GameConfig* gameConfig) {
        m_settingsWindow = window;
        m_settingsRenderer = renderer;
        m_settingsConfig = gameConfig;
    }

private:
    /**
     * @brief Menu button structure.
     *
     * Post-presentation-rebuild (2026-07-17) this is a CALLBACK REGISTRY, not
     * a drawn widget: the web menu (GameModeSelection.tsx) is a card layout,
     * not a vertical button stack, so renderModeSelectPage draws the two mode
     * CARDS + a footer directly (ImGui InvisibleButton hit-testing) rather
     * than iterating this list for geometry. The list is still built in
     * initialize() because the game layer wires callbacks through it
     * (setContinueCallback/setQuitCallback), and holding those lambdas here
     * keeps the Continue/Settings entries — which parity HIDES from the menu
     * (they have no web analog) — wired for a future non-parity build without
     * a second storage path. text/subtitle/hint feed the cards; the geometry
     * fields are vestigial for the plain-button era and left at their
     * defaults.
     */
    struct MenuButton {
        std::string text;
        std::string subtitle; // second line inside a mode card; empty = plain button
        std::string hint;     // small third line (e.g. "Coming soon"); empty = none
        bool enabled = true;
        bool hovered = false;
        std::array<float, 2> position = {0.0F, 0.0F};
        std::array<float, 2> size = {0.0F, 0.0F};
        ButtonCallback callback;
    };

    /**
     * @brief Render background (the web's deep-navy backdrop, no stars).
     */
    void renderBackground(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief ImGui body of the mode-select page — the web's dark card on a
     * navy backdrop: header, two side-by-side mode cards, development-status
     * banner, and a Reset/Quit footer. Emits into the already-begun
     * full-screen overlay window.
     */
    void renderModeSelectPage(float width, float height);

    /**
     * @brief ImGui body of the "Customize Your Cat" page (fur swatch grid
     * + BACK / START GAME). Emits into the same overlay window.
     */
    void renderCustomizePage(float width, float height);

    /**
     * @brief Confirm the customize page: latch the fur choice, reset the
     * menu to mode-select for the next visit, and fire m_startGameCallback.
     */
    void confirmStartGame();

    Engine::Input& m_input;
    GameAudio& m_audio;

    // Button callback registry (see MenuButton doc — not a drawn widget list).
    std::vector<MenuButton> m_buttons;

    // Callbacks
    ButtonCallback m_startGameCallback;
    ButtonCallback m_continueCallback;
    ButtonCallback m_settingsCallback;
    ButtonCallback m_quitCallback;
    ButtonCallback m_resetProgressCallback;

    // State
    bool m_hasSaveGame = false;
    std::string m_versionString = "v1.0.0";

    // Pre-game flow state (web parity: GameModeSelection.tsx). The fur
    // index defaults to the web's initially-highlighted swatch; the
    // confirmed flag only latches when START GAME fires, mirroring the
    // web where setPlayerCustomization runs inside confirmSurvivalMode —
    // not on every swatch click.
    MenuPage m_currentPage = MenuPage::ModeSelect;
    int m_selectedFurIndex = CatGame::WebParity::kDefaultFurSwatchIndex;
    // Eye swatch defaults to the web's initially-highlighted eye colour
    // (#4CAF50 / index 0), same convention as the fur index above. Confirmed
    // by the SAME m_furColorConfirmed latch — see hasSelectedEyeColor().
    int m_selectedEyeIndex = CatGame::WebParity::kDefaultEyeSwatchIndex;
    bool m_furColorConfirmed = false;

    // Reset-Progress two-click confirm latch (web parity:
    // GameModeSelection.tsx:36-42 arms a "click again to confirm" state that
    // auto-clears after 3 s). First click arms it; a second click while armed
    // fires m_resetProgressCallback; the timer disarms it otherwise.
    bool m_resetConfirmPending = false;
    float m_resetConfirmTimer = 0.0F;

    // Screen dimensions (cached during render)
    uint32_t m_screenWidth = 1920;
    uint32_t m_screenHeight = 1080;

    bool m_initialized = false;

    // Optional ImGui layer (not owned). When set, render() builds widgets via ImGui.
    Engine::ImGuiLayer* m_imguiLayer = nullptr;

    // Settings-panel bindings (not owned). Any of these may be null if a
    // given row isn't wired yet; drawSettingsPanel() guards every use.
    Engine::Window* m_settingsWindow = nullptr;
    CatEngine::Renderer::Renderer* m_settingsRenderer = nullptr;
    GameConfig* m_settingsConfig = nullptr;
};

} // namespace Game

#endif // GAME_UI_MAIN_MENU_HPP
