/**
 * @file main.cpp
 * @brief Entry point for Cat Annihilation game
 *
 * Initializes engine systems, creates game instance, and runs main loop.
 */

#include "../engine/core/Window.hpp"
#include "../engine/core/Input.hpp"
#include "../engine/core/Timer.hpp"
#include "../engine/core/Logger.hpp"
#include "../engine/math/Math.hpp"
#include "../engine/core/Config.hpp"
#include "../engine/assets/AssetManager.hpp"
#include "../engine/rhi/vulkan/VulkanRHI.hpp"
#include "../engine/rhi/vulkan/VulkanSwapchain.hpp"
#include "../engine/rhi/vulkan/VulkanDevice.hpp"
#include "../engine/renderer/Renderer.hpp"
#include "../engine/renderer/MeshSubmissionSystem.hpp"
#include "../engine/renderer/passes/UIPass.hpp"
#include "../engine/renderer/passes/ScenePass.hpp"
#include "../engine/audio/AudioEngine.hpp"
#include "../engine/cuda/CudaContext.hpp"
#include "../engine/ui/ImGuiLayer.hpp"
#include "../engine/ui/GraphicsSettingsPanel.hpp"
#include "../engine/debug/Profiler.hpp"
#include "../engine/debug/ProfilerOverlay.hpp"
// The shader-hot-reload driver header is always included so the type names
// compile cleanly regardless of the CMake flag, but every construction and
// Tick() call is guarded by CAT_ENGINE_SHADER_HOT_RELOAD below — release
// builds get zero runtime cost (not even a constructor call) as the backlog
// item requires ("Must be compiled out of release builds").
#include "../engine/rhi/ShaderHotReloadDriver.hpp"
// The renderer-side subscriber registry IS compiled unconditionally: it's
// pure STL (no Vulkan, no glslc, no filesystem) and passes need to call
// Register() regardless of the hot-reload flag so their Setup() code path
// doesn't need per-site #ifdefs. In release builds nothing ever calls
// Dispatch, so the registered callbacks sit as dead weight (a map entry +
// two std::function — a few hundred bytes total). See ShaderReloadRegistry.hpp
// header for the rationale on why that's the right trade-off vs a second
// per-pass #ifdef gate.
#include "../engine/rhi/ShaderReloadRegistry.hpp"
#include "config/GameConfig.hpp"
#include "config/GameplayConfig.hpp"
#include "config/BalanceConfig.hpp"
#include "CatAnnihilation.hpp"
#include "systems/PlayerControlSystem.hpp"
#include "systems/WaveSystem.hpp"
#include "systems/leveling_system.hpp"
#include "components/HealthComponent.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <cstdlib>
#include <cctype>

// Command line argument parsing
//
// The non-obvious flags — --autoplay, --max-frames, --exit-after-seconds —
// exist for portfolio / CI / openclaw-nightly validation runs: they let the
// binary prove "the gameplay core loop (wave spawn, ECS, physics, particles,
// render) ticks" without a human clicking past the main menu. The default
// (interactive) path is unaffected: no flags set → MainMenu just like before.
struct CommandLineArgs {
    bool fullscreen = false;
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool enableValidation = false;
    bool showHelp = false;
    std::string configPath = "config.json";

    // --autoplay: after engine + game init succeed, transition straight into
    // arcade mode (startNewGame(false)) instead of sitting on MainMenu. Lets
    // automated runs exercise the Playing-state code path on the first frame.
    bool autoplay = false;

    // --max-frames <N>: if > 0, break the main loop cleanly after rendering
    // this many frames. Used by nightly smoke runs so the binary exits with
    // code 0 after proving the gameplay loop ticked, instead of being SIGKILL'd
    // by the wrapper's coreutils `timeout`. 0 = no cap.
    uint32_t maxFrames = 0;

    // --exit-after-seconds <seconds>: same idea as --max-frames but walltime.
    // 0 = no cap. If both are set, whichever trips first wins.
    float exitAfterSeconds = 0.0f;

    // --log-file <path>: mirror Logger output to a file sink in addition to
    // the default console sink. The nightly playtest runner needs this
    // because launch-on-secondary.ps1 uses Start-Process -WindowStyle Hidden,
    // which discards the child's stdout — so a `| tee cat-playtest.log`
    // redirect by the wrapper captures nothing. Having the engine itself own
    // the file sink means any launch path (ps1 wrapper, direct, scheduled
    // task, debugger attach) produces the same canonical log. Empty = no
    // file sink; behaviour matches pre-2026-04-24 (console only).
    std::string logFilePath;

    // --frame-dump <path>: after the main loop exits (via --max-frames or
    // --exit-after-seconds), read back the last-presented swapchain image
    // and write it to <path> as binary PPM (Netpbm P6). Intended for the
    // headless-render golden-image CI pipeline: a checked-in reference PPM
    // lives at tests/golden/smoke.ppm; a nightly / PR build runs
    //     CatAnnihilation.exe --autoplay --exit-after-seconds 10 \
    //                         --frame-dump build-ninja/smoke.ppm
    // and a Catch2 test dispatches the SSIM comparator to assert the
    // structural similarity stays above a tolerance, catching regressions
    // in any render pass without false-positives on timing / random seeds.
    //
    // Empty = no capture (preserves pre-2026-04-24 behaviour).
    std::string frameDumpPath;

    // --enable-ribbon-trails: gate the new particle ribbon-trail draw path so
    // the golden-image CI test (and any other pixel-sensitive diff) doesn't
    // drift the moment the Vulkan RibbonTrailPass lands. Default false mirrors
    // pre-ribbon behaviour bit-for-bit — no new geometry is submitted, no new
    // pipeline binds happen, the existing particle billboard path is
    // untouched. When true, the renderer additionally issues one indexed
    // triangle-list draw per active ribbon particle using the vertices the
    // CUDA `ribbonTrailBuildKernel` writes into the pass's VkBuffer. See
    // `engine/cuda/particles/RibbonTrailDevice.cuh` for the kernel that
    // produces the vertex data and `shaders/particles/ribbon_trail.{vert,frag}`
    // for the shader pair that consumes it. The flag is read at startup only;
    // changing it mid-run has no effect (the renderer caches the gate state
    // at pass setup). A designer wanting live toggles should use the
    // ImGui GraphicsSettingsPanel instead (planned follow-up — this CLI
    // flag is the CI / smoke-test hook, not the runtime UX hook).
    bool enableRibbonTrails = false;

    // --enable-cpu-skinning: opt into the per-frame CPU vertex-skinning hot
    // path inside MeshSubmissionSystem + ScenePass::EnsureSkinnedMesh.
    //
    // 2026-04-26 perf halt: a 30-second autoplay heartbeat trace measured
    // fps=2-5 with ~17-20 visible skinned NPCs/dogs vs fps=37-46 the moment
    // frustum culling drops the count to <=2. The bottleneck is the
    // ~150k-vertex weighted-mat4 sum + transform + normalise loop that runs
    // on a single CPU thread for EVERY visible skinned entity EVERY frame.
    // With ~3-5M per-frame transforms required to keep all entities animated,
    // there is no CPU-side optimisation that recovers playable fps short of
    // moving skinning to the GPU (a UBO/SSBO bone-palette + a vertex-shader
    // skin path) — which is multi-iteration work.
    //
    // Default OFF therefore renders entities in bind-pose (T-pose / authored
    // rest pose) at full frame rate. Bind-pose is strictly better than 2-5
    // fps for a portfolio playtest: every cat / dog / NPC is visible, the
    // tint / texture / per-clan colour signal is preserved, and combat /
    // particles / camera-shake all still read correctly. The only thing
    // missing is the per-bone deformation that animation clips would have
    // produced — which mostly nobody can see anyway when the camera is
    // 12 m above the player and entities are <1.5 m tall on screen.
    //
    // Opt in (set this flag) for hero shots / portfolio screenshots where
    // a low-NPC-count scene fits in the CPU budget and authored animation
    // clips matter more than fps. The flag is propagated to
    // MeshSubmissionSystem::SetEnableCpuSkinning at startup; the
    // [MeshSubmission] gate state is logged so the playtest log shows
    // unambiguously which path is live.
    // 2026-04-26 SURVIVAL-PORT — split into explicit on/off + resolved
    // value so the autoplay default can flip ON without losing the
    // ability for callers (perf-gate, golden-image runs) to force it
    // off via --no-cpu-skinning. The original perf-halt note above was
    // written when 17-20 skinned NPCs were on screen; survival mode
    // has NPCs disabled (just the player + 3-5 dogs in early waves),
    // which is well within the CPU-skinning budget. Without this
    // default the autoplay cat renders as a sliding T-pose, exactly
    // the "cats legs arent moving when it walks" the user flagged.
    bool enableCpuSkinning            = false;  // resolved after parsing
    bool enableCpuSkinningExplicitOn  = false;
    bool enableCpuSkinningExplicitOff = false;

    // --starting-wave <N>: bypass wave 1 and seed the WaveSystem to spawn
    // wave N as the first wave in autoplay mode. Default 1 = unchanged
    // pre-2026-04-25 behaviour (every nightly playtest spawns wave 1
    // first). Useful for portfolio / golden-image runs that need the
    // boss-wave GLB (`dog_boss.glb`) visible inside a short
    // `--exit-after-seconds 30` capture window — without this, the
    // boss path is unreachable until ~120 s into the run, exceeding
    // the SDK iteration budget. Forwarded to WaveSystem via
    // `game->getWaveSystem()->setInitialWave(N)` after autoplay's
    // `startNewGame(false)` has constructed the wave system.
    //
    // Why a setter (rather than mutating WaveConfig.bossWaveInterval):
    // bossWaveInterval changes the *cadence* (every N waves becomes a
    // boss wave) which would also shift the cadence of regular waves
    // we want intact for normal nightlies. setInitialWave only changes
    // the *starting point*, leaving the cadence at the configured
    // default.
    int startingWave = 1;

    // --day-night-rate <seconds>: cycle period for the sky gradient's
    // dawn → midday → dusk colour-stop interpolation. The value is in
    // wallclock seconds and feeds ScenePass::SetDayCycleSeconds() after
    // pass setup. Default 30 s biases the cycle for portfolio-screenshot
    // visibility — short enough to read inside a 25-second autoplay
    // capture window, long enough to be a "morning → noon → evening"
    // arc rather than a flicker. A value of 0 (or negative) freezes the
    // sky at the midday preset, which is what golden-image CI / SSIM
    // diff runs want for deterministic PPM output. Forwarded to
    // ScenePass after the renderer's BeginFrame plumbing finishes,
    // mirroring how --enable-ribbon-trails forwards to
    // SetRibbonsEnabled().
    float dayNightRateSec = 30.0f;

    // --cinematic-orbit-camera [<rad-per-sec>]: enable a slow camera
    // revolution around the player so portfolio captures showcase the
    // Meshy-baked PBR fur from front-quarter, side, and rear-quarter
    // angles inside a single playtest, rather than a single fixed
    // third-person silhouette per screenshot. Forwarded to
    // PlayerControlSystem::setCinematicOrbit() after the autoplay setup
    // block. The value is in radians per second; default 0.5 rad/s ≈
    // 28.6°/s gives a full revolution every ~12.6 s.
    //
    // CLI semantics:
    //   --cinematic-orbit-camera          → enabled, default rate (0.5)
    //   --cinematic-orbit-camera 0.3      → enabled at 0.3 rad/s
    //   --cinematic-orbit-camera=0.3      → equals form
    //   --no-cinematic-orbit-camera       → force-disable (overrides the
    //                                       autoplay default-on below)
    //
    // Default ON in autoplay (set after parsing in main()): autoplay is
    // exclusively the portfolio / smoke-run context, so the orbit win is
    // free there. A human-driven session never hits this branch (autoplay
    // is opt-in), so manual play feels exactly as before.
    //
    // History (2026-04-25): without cinematic orbit, every nightly
    // playtest frame-dump showed the player cat from the same fixed
    // angle — reviewers (and the user-directive scoreboard) couldn't
    // see the rig + textures we'd just landed. Adding the orbit converts
    // each 25 s capture into a multi-angle reveal.
    bool  cinematicOrbitCamera        = false;  // resolved after parsing
    bool  cinematicOrbitExplicitOn    = false;  // user passed --cinematic-orbit-camera
    bool  cinematicOrbitExplicitOff   = false;  // user passed --no-cinematic-orbit-camera
    float cinematicOrbitYawRate       = 0.5F;
};

// Translate MSYS-style POSIX paths ("/tmp/foo", "/c/Users/...") into native
// Windows paths so the engine's std::ofstream / std::ifstream callers don't
// have to know about MSYS pathconv. The mission prompt and openclaw bridge
// scripts both write paths in the POSIX form (it's what `cat /tmp/...` and
// the launch-on-secondary.ps1 wrapper both consume) — but when the wrapper
// hands the argv to the native exe via PowerShell's
// System.Diagnostics.ProcessStartInfo, no path translation happens. The
// child sees the literal "/tmp/state/iter-N.ppm" string, std::ofstream
// resolves it as drive-relative ("C:\tmp\state\..."), the directory
// doesn't exist, the open silently fails, and --frame-dump produces no
// PPM (the 2026-04-25 ~04:39 UTC playtest hit this exact mode: clean
// exit=0, "[framedump] WritePPM failed for path '/tmp/state/iter-cycler-45s.ppm'"
// in stderr, no on-disk artifact).
//
// The fix lives at the CLI boundary, not inside the engine: engine code
// has no business knowing about MSYS conventions, and other callers
// (in-engine path-building, asset-relative paths) shouldn't pay a per-
// open translation cost. The two CLI flags that take filesystem paths
// (--frame-dump, --log-file) route through this helper so any future
// nightly invocation written to the prompt's documented form works
// unchanged.
//
// Translation rules:
//   /tmp/...       -> $env:TEMP/...   (the user's resolved temp dir)
//   /c/foo/bar     -> C:/foo/bar      (MSYS drive-mounted form)
//   anything else  -> passed through unchanged
//
// Non-Windows builds are a pass-through — the engine compiles on Linux
// CI for the Catch2 unit pass, and there the paths are real.
inline std::string translateMsysPath(const std::string& raw) {
#ifdef _WIN32
    if (raw.size() >= 5 && raw.compare(0, 5, "/tmp/") == 0) {
        const char* tempEnv = std::getenv("TEMP");
        if (tempEnv == nullptr || *tempEnv == '\0') {
            // Fall back to the canonical Windows temp path. This is the
            // documented default per Microsoft when neither TEMP nor TMP
            // are set, which on a default install both resolve to the
            // same %LOCALAPPDATA%\Temp. Keeping the fallback explicit
            // means the translation succeeds even in stripped-down
            // environments (e.g. service-account contexts where env
            // vars haven't been provisioned).
            tempEnv = "C:/Users/Default/AppData/Local/Temp";
        }
        return std::string(tempEnv) + "/" + raw.substr(5);
    }
    // MSYS drive-mounted form: /c/foo -> C:/foo, /d/bar -> D:/bar.
    // Conservatively only match a single ASCII-letter drive followed by
    // '/' so we don't mangle paths that happen to start with "/x/" for
    // non-MSYS reasons.
    if (raw.size() >= 3 && raw[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(raw[1])) && raw[2] == '/') {
        std::string out;
        out.reserve(raw.size() + 1);
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(raw[1]))));
        out.append(":/");
        out.append(raw.substr(3));
        return out;
    }
#endif
    return raw;
}

CommandLineArgs parseCommandLine(int argc, char* argv[]) {
    CommandLineArgs args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            args.showHelp = true;
        } else if (arg == "--fullscreen" || arg == "-f") {
            args.fullscreen = true;
        } else if (arg == "--width" || arg == "-w") {
            if (i + 1 < argc) {
                args.width = static_cast<uint32_t>(std::atoi(argv[++i]));
            }
        } else if (arg == "--height" || arg == "-h") {
            if (i + 1 < argc) {
                args.height = static_cast<uint32_t>(std::atoi(argv[++i]));
            }
        } else if (arg == "--validation" || arg == "-v") {
            args.enableValidation = true;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) {
                args.configPath = argv[++i];
            }
        } else if (arg == "--autoplay" || arg == "-a") {
            args.autoplay = true;
        } else if (arg == "--max-frames") {
            if (i + 1 < argc) {
                args.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
            }
        } else if (arg == "--exit-after-seconds") {
            if (i + 1 < argc) {
                args.exitAfterSeconds = static_cast<float>(std::atof(argv[++i]));
            }
        } else if (arg == "--log-file") {
            // Append-mode file sink; the path is opened by FileSink's
            // constructor with std::ios::app so re-launches in a single
            // nightly sweep accumulate into one file. Callers who want a
            // truncated log should delete the file before launch.
            // translateMsysPath: lets nightly callers say "--log-file
            // /tmp/cat.log" without thinking about whether their launcher
            // applies MSYS path translation — see the helper's docblock
            // for the full rationale.
            if (i + 1 < argc) {
                args.logFilePath = translateMsysPath(argv[++i]);
            }
        } else if (arg == "--frame-dump") {
            // Space-separated form: --frame-dump <path>. The dispatched
            // Renderer::CaptureSwapchainToPPM happens AFTER the main loop
            // exits, so this flag is only meaningful when combined with
            // --max-frames or --exit-after-seconds (otherwise the loop
            // doesn't terminate cleanly and the capture never runs).
            // translateMsysPath here closes the bug the 04:39 UTC playtest
            // surfaced: PowerShell's launch-on-secondary.ps1 hands argv
            // through verbatim with no MSYS conversion, so "/tmp/..." paths
            // failed silently. With the translator the same flag value
            // works whether the caller is bash, PowerShell, cmd, or a
            // scheduled-task host.
            if (i + 1 < argc) {
                args.frameDumpPath = translateMsysPath(argv[++i]);
            }
        } else if (arg.rfind("--frame-dump=", 0) == 0) {
            // Equals form: --frame-dump=<path>. Supported in addition to
            // the space form because the mission prompt documents the
            // flag with the equals syntax and shell quoting is simpler
            // that way when the path contains a space.
            args.frameDumpPath =
                translateMsysPath(arg.substr(std::string("--frame-dump=").size()));
        } else if (arg == "--starting-wave") {
            // Space-separated form: --starting-wave <N>. Clamped to >=1 by
            // WaveSystem::setInitialWave; we additionally guard against
            // negative atoi outputs (e.g. "--starting-wave abc" -> 0)
            // by reading the raw value verbatim and letting the setter
            // canonicalise. No upper bound — wave 25 is as valid as
            // wave 5 from the system's POV.
            if (i + 1 < argc) {
                args.startingWave = std::atoi(argv[++i]);
            }
        } else if (arg.rfind("--starting-wave=", 0) == 0) {
            // Equals form mirrors the --frame-dump= / --log-file= shape so
            // a portfolio capture script can write a single token like
            // `--starting-wave=5` without quoting concerns.
            args.startingWave =
                std::atoi(arg.substr(std::string("--starting-wave=").size()).c_str());
        } else if (arg == "--enable-ribbon-trails") {
            // No value takes a value — presence of the flag turns it on.
            // Absent from parsing = stays at the struct default (false), so
            // pre-existing nightly invocations and the golden-image CI path
            // keep their pre-ribbon pixel output bit-for-bit until a caller
            // opts in.
            args.enableRibbonTrails = true;
        } else if (arg == "--enable-cpu-skinning") {
            // Same shape as --enable-ribbon-trails: presence flips it on,
            // absence preserves the struct default (false). Forwarded after
            // the renderer is up via
            // `MeshSubmissionSystem::SetEnableCpuSkinning(true)` — see the
            // setter docblock in MeshSubmissionSystem.hpp for the WHY.
            args.enableCpuSkinningExplicitOn = true;
        } else if (arg == "--no-cpu-skinning") {
            // Force CPU skinning off even when --autoplay would otherwise
            // turn it on by default. Useful for high-NPC perf captures or
            // if a future change reintroduces the >15-skinned-entity
            // bottleneck and a repro needs the bind-pose fallback.
            args.enableCpuSkinningExplicitOff = true;
        } else if (arg == "--day-night-rate") {
            // Space-separated form: --day-night-rate <seconds>.
            // Plumbed straight into ScenePass::SetDayCycleSeconds() after
            // engine init. Negative or zero freezes the sky at midday for
            // determinism (golden-image CI). std::atof returns 0 on
            // unparseable input ("abc"), which intentionally maps to the
            // freeze case — typo'd values produce a stable known output
            // rather than a random cycle period.
            if (i + 1 < argc) {
                args.dayNightRateSec = static_cast<float>(std::atof(argv[++i]));
            }
        } else if (arg.rfind("--day-night-rate=", 0) == 0) {
            // Equals form mirrors --frame-dump= / --starting-wave= so
            // a single-token argv entry survives shell quoting on
            // PowerShell + bash + cmd hosts uniformly.
            args.dayNightRateSec = static_cast<float>(
                std::atof(arg.substr(std::string("--day-night-rate=").size()).c_str()));
        } else if (arg == "--cinematic-orbit-camera") {
            // Two-form parse: bare `--cinematic-orbit-camera` enables the
            // orbit at the default 0.5 rad/s; an immediately-following
            // numeric token (peek + atof) overrides the rate. The peek
            // path is gated on i+1 < argc AND the next token starting
            // with a digit / sign / dot, so we don't accidentally swallow
            // a sibling flag (`--cinematic-orbit-camera --autoplay` should
            // enable orbit at default rate, not parse "--autoplay" as 0).
            args.cinematicOrbitExplicitOn = true;
            if (i + 1 < argc) {
                const std::string& peek = argv[i + 1];
                if (!peek.empty() &&
                    (std::isdigit(static_cast<unsigned char>(peek[0])) != 0 ||
                     peek[0] == '+' || peek[0] == '-' || peek[0] == '.')) {
                    // Negative-sign-then-digit is a valid float ("-0.5")
                    // for counter-clockwise orbits; sign-only ("--no-cin"
                    // collides with the dedicated --no-cinematic-orbit-camera
                    // branch above so this peek is unambiguous).
                    args.cinematicOrbitYawRate =
                        static_cast<float>(std::atof(peek.c_str()));
                    ++i;  // consume the rate token
                }
            }
        } else if (arg.rfind("--cinematic-orbit-camera=", 0) == 0) {
            args.cinematicOrbitExplicitOn = true;
            args.cinematicOrbitYawRate = static_cast<float>(std::atof(
                arg.substr(std::string("--cinematic-orbit-camera=").size()).c_str()));
        } else if (arg == "--no-cinematic-orbit-camera") {
            // Explicit force-disable. Beats the autoplay-default-on rule
            // that runs after parsing — a caller who passed both
            // --autoplay AND --no-cinematic-orbit-camera gets the legacy
            // fixed-third-person framing.
            args.cinematicOrbitExplicitOff = true;
        }
    }

    // Resolve the cinematic orbit gate AFTER the loop so the autoplay-
    // default-on rule can see the final --autoplay state regardless of
    // argv ordering. Explicit on / explicit off both override the rule.
    // 2026-04-26 SURVIVAL-PORT — same explicit-on/off/autoplay-default
    // shape as cinematic-orbit. NPCs are disabled in survival, so the
    // historical 17-20-skinned-entity perf cliff does not apply; turning
    // skinning on by default makes the player cat's legs actually
    // animate, which the user flagged as broken in playtest.
    if (args.enableCpuSkinningExplicitOff) {
        args.enableCpuSkinning = false;
    } else if (args.enableCpuSkinningExplicitOn) {
        args.enableCpuSkinning = true;
    } else {
        // 2026-04-26 SURVIVAL-PORT iter 3 — DEFAULT OFF in autoplay too.
        // The CPU-skinning path produces a cat lying on its back (legs in
        // the air, paws planted on the ground), confirmed by user
        // playtest. Bind-pose rendering shows the cat upright and
        // recognizable (cat-verify row #107 PPM). Until the orientation
        // bug in the skinning matrix chain is found and fixed, default
        // to bind-pose so the cat at least stands upright. Opt back in
        // with --enable-cpu-skinning when investigating the bug.
        args.enableCpuSkinning = false;
    }

    if (args.cinematicOrbitExplicitOff) {
        args.cinematicOrbitCamera = false;
    } else if (args.cinematicOrbitExplicitOn) {
        args.cinematicOrbitCamera = true;
    } else {
        // 2026-04-26 SURVIVAL-PORT — autoplay used to default the orbit ON
        // because the orbit was a portfolio / smoke-run win for static
        // demos. With the survival-port follow-cat-yaw camera live (see
        // PlayerControlSystem::updateCamera), an orbiting camera fights
        // the yaw-tracking and produces "the cat turns sidways and shit"
        // (verbatim user feedback). Default OFF in all modes; opt back in
        // with --cinematic-orbit-camera for a fixed portfolio capture.
        args.cinematicOrbitCamera = false;
    }

    return args;
}

void printHelp() {
    std::cout << "Cat Annihilation - Survive the Waves\n\n";
    std::cout << "Usage: cat-annihilation [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help, -h                 Show this help message\n";
    std::cout << "  --fullscreen, -f           Start in fullscreen mode\n";
    std::cout << "  --width <pixels>           Set window width (default: 1920)\n";
    std::cout << "  --height <pixels>          Set window height (default: 1080)\n";
    std::cout << "  --validation, -v           Enable Vulkan validation layers\n";
    std::cout << "  --config <path>            Path to config file (default: config.json)\n";
    std::cout << "  --autoplay, -a             Skip main menu, start in arcade mode\n";
    std::cout << "  --max-frames <N>           Exit cleanly after rendering N frames (0 = no cap)\n";
    std::cout << "  --exit-after-seconds <S>   Exit cleanly after S seconds (0 = no cap)\n";
    std::cout << "  --log-file <path>          Mirror Logger output to <path> (in addition to console)\n";
    std::cout << "  --frame-dump <path>        After loop exits, write final swapchain image to <path> (.ppm)\n";
    std::cout << "  --enable-ribbon-trails     Render CUDA-produced ribbon trails for particle systems\n";
    std::cout << "  --enable-cpu-skinning      Run per-frame CPU vertex skinning for animated entities\n";
    std::cout << "                             (default OFF: entities render in bind-pose at full fps;\n";
    std::cout << "                              ON: animated but expect 2-5 fps with full waves until GPU skinning lands)\n";
    std::cout << "  --starting-wave <N>        In autoplay mode, spawn wave N first instead of wave 1 (default 1)\n";
    std::cout << "  --day-night-rate <S>       Sky cycle period in seconds (default 30; 0 freezes at midday for golden-image CI)\n";
    std::cout << "  --cinematic-orbit-camera [<rad/s>]  Slowly orbit the camera around the player so portfolio captures show multiple angles\n";
    std::cout << "                                      (default rate 0.5 rad/s ≈ 12.6 s per revolution; auto-on when --autoplay is set)\n";
    std::cout << "  --no-cinematic-orbit-camera Force the orbit off, even in autoplay (restores the legacy fixed third-person follow framing)\n";
    std::cout << "\n";
    std::cout << "Smoke-test example (60s of live gameplay, then clean exit):\n";
    std::cout << "  CatAnnihilation.exe --autoplay --exit-after-seconds 60\n";
    std::cout << "\n";
    std::cout << "Headless observation example (used by openclaw nightly):\n";
    std::cout << "  CatAnnihilation.exe --autoplay --exit-after-seconds 40 --log-file cat-playtest.log\n";
    std::cout << "\n";
    std::cout << "Golden-image CI example (capture the final frame for SSIM comparison):\n";
    std::cout << "  CatAnnihilation.exe --autoplay --exit-after-seconds 10 --frame-dump build-ninja/smoke.ppm\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    CommandLineArgs cmdArgs = parseCommandLine(argc, argv);

    if (cmdArgs.showHelp) {
        printHelp();
        return 0;
    }

    // Attach a file sink BEFORE the startup banner so the first three header
    // lines (and every subsequent log message, including the Vulkan init
    // chain that sometimes fails) appear in the file. Adding after the
    // banner would lose those lines — and the Vulkan chain is exactly the
    // part we most want captured for post-mortem diagnosis when a nightly
    // autoplay run crashes at init. Exceptions from FileSink's constructor
    // are caught & reported but non-fatal: a missing log-file directory
    // should never prevent the game from launching, just warn the operator.
    if (!cmdArgs.logFilePath.empty()) {
        try {
            Engine::Logger::GetInstance().AddSink(
                std::make_shared<Engine::FileSink>(cmdArgs.logFilePath));
        } catch (const std::exception& e) {
            std::cerr << "[cli] --log-file: failed to open '"
                      << cmdArgs.logFilePath << "': " << e.what() << std::endl;
        }
    }

    // Initialize logger
    Engine::Logger::info("===========================================");
    Engine::Logger::info("  CAT ANNIHILATION - Starting Up");
    Engine::Logger::info("===========================================");
    if (!cmdArgs.logFilePath.empty()) {
        // Logged AFTER the banner so a reader eyeballing the file knows
        // which invocation produced it — the path itself is evidence the
        // CLI flag was honoured end-to-end.
        Engine::Logger::info("[cli] --log-file: mirroring log to '" +
                             cmdArgs.logFilePath + "'");
    }

    // Bring up the asset manager's loader-thread pool before any system
    // attempts to load a model or texture. LoadModel/LoadModelAsync both
    // hard-require an initialized manager — without this call those paths
    // would throw (async) or hit the cache miss path with no loader (sync).
    CatEngine::AssetManager::GetInstance().Initialize();

    // Load game configuration
    Game::GameConfig gameConfig = Game::GameConfig::getDefault();
    if (!gameConfig.load(cmdArgs.configPath)) {
        Engine::Logger::warn("Failed to load config from " + cmdArgs.configPath +
                           ", using defaults");
    } else {
        Engine::Logger::info("Loaded config from " + cmdArgs.configPath);
    }

    // Apply command line overrides
    if (cmdArgs.fullscreen) {
        gameConfig.graphics.fullscreen = true;
    }
    if (cmdArgs.width > 0 && cmdArgs.height > 0) {
        gameConfig.graphics.windowWidth = cmdArgs.width;
        gameConfig.graphics.windowHeight = cmdArgs.height;
    }
    if (cmdArgs.enableValidation) {
        // Enable validation in engine config
    }

    // Validate and clamp config values
    gameConfig.validate();

    // ========================================================================
    // Create Window
    // ========================================================================
    Engine::Window::Config windowConfig;
    windowConfig.title = "Cat Annihilation";
    windowConfig.width = gameConfig.graphics.windowWidth;
    windowConfig.height = gameConfig.graphics.windowHeight;
    windowConfig.fullscreen = gameConfig.graphics.fullscreen;
    windowConfig.vsync = gameConfig.graphics.vsync;
    windowConfig.resizable = true;
    // 2026-04-26 SURVIVAL-PORT — when --autoplay is set, the user is
    // typically not actively at the game window (cat-verify smoke
    // tests, recordings running alongside other apps). Don't steal
    // foreground focus or pull the mouse cursor in front of
    // whatever they were doing. The Window::Config flag below sets
    // GLFW_FOCUSED=FALSE + GLFW_FOCUS_ON_SHOW=FALSE — visible window
    // (so frame-dump still captures) but inert vs. user's current
    // task. User directive: "cat annihiliation is tkaing control of
    // my mouse when you launch it run it in background for your
    // tests so it doesnt do it".
    windowConfig.noFocusSteal = cmdArgs.autoplay;

    Engine::Window window(windowConfig);
    Engine::Logger::info("Window created: " +
                        std::to_string(windowConfig.width) + "x" +
                        std::to_string(windowConfig.height));

    // ========================================================================
    // Create Input System
    // ========================================================================
    Engine::Input input(window.getHandle());
    Engine::Logger::info("Input system initialized");

    // ========================================================================
    // Create RHI and Renderer
    // ========================================================================
    CatEngine::RHI::RHIDesc rhiDesc;
    rhiDesc.enableValidation = cmdArgs.enableValidation;
    rhiDesc.applicationName = "Cat Annihilation";
    rhiDesc.applicationVersion = 1;

    auto rhi = std::make_unique<CatEngine::RHI::VulkanRHI>();
    if (!rhi->Initialize(rhiDesc)) {
        Engine::Logger::error("Failed to initialize Vulkan RHI");
        return 1;
    }
    Engine::Logger::info("Vulkan RHI initialized");

    CatEngine::Renderer::Renderer::Config rendererConfig;
    rendererConfig.width = gameConfig.graphics.windowWidth;
    rendererConfig.height = gameConfig.graphics.windowHeight;
    rendererConfig.enableVSync = gameConfig.graphics.vsync;
    rendererConfig.enableValidation = cmdArgs.enableValidation;
    rendererConfig.maxFramesInFlight = 2;
    rendererConfig.windowHandle = &window;  // Pass window pointer for Vulkan surface creation

    auto renderer = std::make_unique<CatEngine::Renderer::Renderer>(rendererConfig);
    if (!renderer->Initialize(rhi.get())) {
        Engine::Logger::error("Failed to initialize renderer");
        return 1;
    }
    Engine::Logger::info("Renderer initialized");

    // ========================================================================
    // Initialize Dear ImGui (shares the UI render pass with our UIPass).
    // ========================================================================
    Engine::ImGuiLayer imguiLayer;
    {
        auto* vulkanSwapchain = static_cast<CatEngine::RHI::VulkanSwapchain*>(renderer->GetSwapchain());
        auto* vulkanDevice = rhi->GetDevice();
        if (vulkanSwapchain == nullptr || vulkanDevice == nullptr) {
            Engine::Logger::error("ImGui init: missing Vulkan swapchain or device");
            return 1;
        }

        Engine::ImGuiLayer::InitInfo imguiInit{};
        imguiInit.window = window.getHandle();
        imguiInit.instance = rhi->GetInstance();
        imguiInit.physicalDevice = vulkanDevice->GetPhysicalDevice();
        imguiInit.device = vulkanDevice->GetVkDevice();
        imguiInit.graphicsQueueFamily = vulkanDevice->GetQueueFamilyIndices().graphics.value_or(0);
        imguiInit.graphicsQueue = vulkanDevice->GetGraphicsQueue();
        imguiInit.renderPass = vulkanSwapchain->GetUIRenderPass();
        imguiInit.minImageCount = 2;
        imguiInit.imageCount = vulkanSwapchain->GetImageCount();
        imguiInit.regularFontPath = "assets/fonts/OpenSans-Regular.ttf";
        imguiInit.boldFontPath = "assets/fonts/OpenSans-Bold.ttf";

        if (!imguiLayer.Init(imguiInit)) {
            Engine::Logger::error("Failed to initialize ImGui layer");
            return 1;
        }
        Engine::Logger::info("ImGui layer initialized");

        // Hand the layer to the UIPass so it draws inside the existing UI render pass.
        if (auto* uiPass = renderer->GetUIPass()) {
            uiPass->SetImGuiLayer(&imguiLayer);
        }
    }

    // ========================================================================
    // Initialize CUDA Context
    // ========================================================================
    std::unique_ptr<CatEngine::CUDA::CudaContext> cudaContext;
    try {
        cudaContext.reset(new CatEngine::CUDA::CudaContext());
        Engine::Logger::info("CUDA context initialized successfully");
    } catch (const CatEngine::CUDA::CudaException& e) {
        std::cerr << "[main] CUDA initialization failed: " << e.what() << std::endl;
        Engine::Logger::warn("CUDA initialization failed, continuing without GPU acceleration");
        // Continue without CUDA - it's not critical for basic functionality
    } catch (const std::exception& e) {
        std::cerr << "[main] Exception during CUDA init: " << e.what() << std::endl;
        Engine::Logger::warn("CUDA initialization failed, continuing without GPU acceleration");
    }

    // ========================================================================
    // Create Audio System
    // ========================================================================
    auto audioEngine = std::make_unique<CatEngine::AudioEngine>();
    // Audio is non-critical: continuing without it (silent build) is always
    // a valid run. The AudioEngine::initialize() path in
    // engine/audio/AudioEngine.cpp:37-42 deliberately returns false by
    // default per user directive 2026-04-26 ("cat annihilation has sound
    // effects please remove them we dont need them right now when it
    // crashes the noise goes crazy") — the game must NOT exit when that
    // happens or every CAT_AUDIO-unset launch would die at startup before
    // ever reaching the main loop. AudioEngine::isInitialized() guards the
    // playSound / playSound2D paths, and Game::GameAudio::initialize()
    // (CatAnnihilation.cpp:423) tolerates a no-op AudioEngine, so silent
    // mode is end-to-end safe. Logging the failure as warn (not error)
    // keeps it in the playtest log for diagnosis without false-flagging
    // it as a fatal condition.
    if (!audioEngine->initialize()) {
        Engine::Logger::warn("Audio engine disabled (running silent); set CAT_AUDIO=1 to enable");
    } else {
        Engine::Logger::info("Audio engine initialized");
    }

    // Apply audio settings from config
    audioEngine->getMixer().setMasterVolume(gameConfig.audio.masterVolume);
    if (gameConfig.audio.masterMuted) {
        audioEngine->getMixer().setMasterMuted(true);
    }

    // ========================================================================
    // Create Cat Annihilation Game Instance
    // ========================================================================
    auto game = std::make_unique<CatGame::CatAnnihilation>(&input, &window, renderer.get(), audioEngine.get(), &imguiLayer);
    // Wire the config through before initialize() so the menus can bind
    // their Settings panel to it during initializeUI(). Without this the
    // Settings dialog's sliders would mutate file-local storage only and
    // never reach the real audio/video subsystems.
    game->setGameConfig(&gameConfig);
    if (!game->initialize()) {
        Engine::Logger::error("Failed to initialize Cat Annihilation game");
        return 1;
    }
    Engine::Logger::info("Cat Annihilation game initialized successfully");

    // --autoplay: transition directly to arcade Playing state after init.
    // Invoking startNewGame() here (rather than stuffing a key-press event at
    // the UI layer) bypasses the MainMenu entirely — no reliance on the
    // menu's focus/click state machine, no flicker frame where input is still
    // routed to menus, and the first rendered frame already has a live wave.
    // storyMode=false gives us the endless/wave harness, which is the code
    // path the nightly smoke test actually wants to exercise.
    if (cmdArgs.autoplay) {
        Engine::Logger::info("[cli] --autoplay: skipping MainMenu, starting arcade mode");

        // --starting-wave: forward the override BEFORE startNewGame so the
        // WaveSystem's `initialWave_` is set when CatAnnihilation's
        // onEnterPlaying eventually calls `waveSystem_->startWaves()`.
        // The wave system already exists by the time `game->init()`
        // returned (it's constructed in `setupSystems()`), so the
        // setter call resolves cleanly here. Order matters: if we set
        // it AFTER startNewGame, startWaves() has already fired with
        // the default initialWave_ = 1 and the override is a no-op
        // (wavesStarted_ guard early-outs the second call).
        if (cmdArgs.startingWave > 1) {
            if (auto* waveSystem = game->getWaveSystem()) {
                waveSystem->setInitialWave(cmdArgs.startingWave);
                Engine::Logger::info(
                    std::string("[cli] --starting-wave: seeding wave ") +
                    std::to_string(cmdArgs.startingWave) + " as first wave");
            } else {
                Engine::Logger::warn(
                    "[cli] --starting-wave: WaveSystem not available, "
                    "override has no effect");
            }
        }

        game->startNewGame(/*storyMode=*/false);

        // Flip the PlayerControlSystem into autoplay AI mode so the cat
        // actually fights back. Without this, a nightly smoke run sees wave
        // 1 spawn, dogs close the distance, the cat stands still because
        // nobody is pressing keys, and the run ends in a motionless death —
        // which proves "init + spawn + enemy AI" but never proves
        // "combat + kill + wave progression". The autoplay AI drives the
        // same startAttack() / performAttack() path a human click would, so
        // a recording of this path is a representative demo of the game.
        if (auto* playerControl = game->getPlayerControlSystem()) {
            playerControl->setAutoplayMode(true);
            Engine::Logger::info("[cli] --autoplay: PlayerControlSystem AI enabled");

            // Wire the cinematic orbit camera if the resolved gate is on
            // (either the user explicitly opted in, or the autoplay
            // default-on rule turned it on after parsing). Skipping the
            // call when off preserves the legacy fixed-third-person
            // framing bit-for-bit, so the golden-image diff path is
            // unaffected unless a caller asks for the orbit.
            if (cmdArgs.cinematicOrbitCamera) {
                playerControl->setCinematicOrbit(true, cmdArgs.cinematicOrbitYawRate);
                Engine::Logger::info(std::string(
                    "[cli] --cinematic-orbit-camera: enabled (yawRate=") +
                    std::to_string(cmdArgs.cinematicOrbitYawRate) + " rad/s, ~" +
                    std::to_string(2.0F * Engine::Math::PI /
                        (cmdArgs.cinematicOrbitYawRate > 0.001F
                             ? cmdArgs.cinematicOrbitYawRate
                             : 0.001F)) +
                    " s per revolution)");
            } else {
                // 2026-04-26 SURVIVAL-PORT — when orbit is off in autoplay,
                // enable follow-player-yaw so the camera tracks the cat's
                // facing instead of staying world-axis-locked. This is the
                // behaviour the user asked for in playtest: "shift with the
                // direction the cat is facing the world around it so we are
                // always facing forwards". Default lag (8 rad/s, ~125 ms
                // half-life) gives a smooth chase without snap.
                playerControl->setFollowPlayerYaw(true, 8.0F);
                Engine::Logger::info(
                    "[cli] camera: follow-player-yaw enabled (lag=8 rad/s ~125ms half-life)");
            }
        } else {
            Engine::Logger::warn("[cli] --autoplay: PlayerControlSystem not available, cat will be idle");
        }
    }

    // --enable-ribbon-trails: log the gate state at startup so a reader of the
    // playtest log can confirm parseCommandLine honoured the flag without
    // waiting for first-frame rendering evidence. Since iteration 3c landed
    // the ribbon rendering pipeline inside ScenePass (pipeline + vertex/
    // index buffers + shader pair + static test strip filled via the host
    // BuildRibbonStrip math kernel), we now ALSO forward the flag to the
    // live pass so the per-frame draw call honours it — the gate's no
    // longer parse-only. A golden-image CI run that ran yesterday against a
    // pre-ribbon build must still produce the identical PPM when run today
    // because the default is `false` (drawRibbons guard in ScenePass::
    // Execute early-outs before any vkCmd touches the ribbon buffers).
    // Reviewers verifying end-to-end plumbing can still grep the log for
    // "--enable-ribbon-trails: gate=" — that line is preserved.
    Engine::Logger::info(std::string("[cli] --enable-ribbon-trails: gate=") +
                         (cmdArgs.enableRibbonTrails ? "on" : "off"));

    // --enable-cpu-skinning: same parse-only-then-forward shape as
    // --enable-ribbon-trails. The flag drives the per-frame CPU vertex skin
    // path inside MeshSubmissionSystem. Default OFF (entities draw bind-pose)
    // is the only path that meets a playable frame budget when the wave is
    // active (~17-20 visible skinned entities collapse fps to 2-5 on the CPU
    // skin loop — see the setter docblock in MeshSubmissionSystem.hpp for
    // the heartbeat trace). Logging the resolved gate value at startup so a
    // reviewer reading the playtest log can see which skinning path is live
    // without grepping for downstream evidence.
    CatEngine::Renderer::MeshSubmissionSystem::SetEnableCpuSkinning(
        cmdArgs.enableCpuSkinning);
    Engine::Logger::info(std::string("[cli] --enable-cpu-skinning: gate=") +
                         (cmdArgs.enableCpuSkinning
                              ? "on (per-frame CPU vertex skinning, expect 2-5 fps with full waves)"
                              : "off (entities render in bind-pose at full fps; default)"));

    if (auto* scenePass = renderer->GetScenePass()) {
        scenePass->SetRibbonsEnabled(cmdArgs.enableRibbonTrails);
        // 2026-04-25 SHIP-THE-CAT iter (time-of-day cycling): forward the
        // configured cycle period to ScenePass. A value of 0 (or negative)
        // freezes the sky at midday — the deterministic preset the
        // golden-image CI path expects. The default of 30 s gives autoplay
        // captures (typically 25 s) a visible dawn → past-midday arc
        // inside a single playtest window. Logging the resolved value so
        // a reviewer reading the playtest log can see whether the flag
        // landed (mirrors the `[cli] --enable-ribbon-trails: gate=` line
        // that already exists for the same purpose).
        scenePass->SetDayCycleSeconds(cmdArgs.dayNightRateSec);
        Engine::Logger::info(std::string("[cli] --day-night-rate: cycleSeconds=") +
                             std::to_string(cmdArgs.dayNightRateSec) +
                             (cmdArgs.dayNightRateSec > 0.0F
                                  ? " (cycling enabled)"
                                  : " (frozen at midday for determinism)"));
    }

    // ========================================================================
    // Setup Window Callbacks
    // ========================================================================
    window.setResizeCallback([&](Engine::u32 width, Engine::u32 height) {
        Engine::Logger::info("Window resized: " + std::to_string(width) + "x" +
                           std::to_string(height));
        renderer->OnResize(width, height);
    });

    window.setCloseCallback([&]() {
        Engine::Logger::info("Window close requested");
    });

    // ========================================================================
    // Shader hot-reload driver (debug builds only).
    //
    // Watches shaders/**.{vert,frag,comp} for mtime changes and recompiles
    // the affected .spv via glslc as soon as a designer saves. The ReloadCallback
    // logs every detection event (success OR failure with glslc stderr tail)
    // so the developer sees exactly why their edit was rejected without
    // leaving the game. Actually swapping the VkShaderModule in a live
    // pipeline requires a subscriber that owns the VulkanShader* cache +
    // per-pass pipeline rebuild — that wiring is the next iteration and is
    // tracked by the P1 backlog item ("GLSL shader hot-reload (debug build
    // only)"). This iteration's deliverable is the polling driver + the
    // callback interface; the on-disk .spv is updated so the designer can
    // still see the change on the next game restart, which is already a
    // strict improvement over "full rebuild required".
    //
    // The #if gate is the single audit line that proves the backlog's
    // "Must be compiled out of release builds" clause — if CMake doesn't
    // define CAT_ENGINE_SHADER_HOT_RELOAD, the driver variable doesn't even
    // exist in the generated object code.
    // ========================================================================
#if defined(CAT_ENGINE_SHADER_HOT_RELOAD)
    CatEngine::RHI::ShaderHotReloadDriver shaderHotReloadDriver;
    double shaderHotReloadClockSec = 0.0;
    {
        // shaders/ lives next to the binary (copied by the CMake POST_BUILD
        // rule). We reference the relative paths the runtime already uses
        // so a developer can launch from any CWD under build-ninja/ and
        // the driver picks up the same tree the renderer loads.
        const size_t watched = shaderHotReloadDriver.Initialize(
            "shaders",
            "shaders/compiled",
            /*includeDirs=*/{"shaders", "shaders/common"});
        Engine::Logger::info(
            "[hot-reload] watching " + std::to_string(watched) +
            " shader sources in shaders/ (debug build)");

        // Logger-backed subscriber. Every detection event — success or
        // failure — lands in the engine log with the exact source path
        // and (on failure) the glslc stderr tail. That's the minimum
        // useful UX for the driver-only iteration: the developer sees
        // their save was compiled, and on a typo'd edit they see the
        // exact line glslc complained about without having to find the
        // .err file by hand. The stderrTail is already bounded to 12
        // lines by ShaderHotReloader::CompileIndex so this can't spam
        // the log with a runaway error dump.
        shaderHotReloadDriver.AddReloadCallback(
            [](const std::string& sourcePath,
               const std::vector<uint8_t>& spvBytes,
               const CatEngine::RHI::ShaderCompileResult& result) {
                // Route to the renderer-side subscriber registry BEFORE
                // logging so the "applied on N passes" count from the
                // Dispatch result can be woven into the success log line.
                // Passes that opted in (via ShaderReloadRegistry::Get()
                // .Register() from their Setup) receive the fresh SPIR-V
                // bytes and fire their downstream "mark pipelines dirty"
                // hooks here — turning the driver's "recompile and log"
                // into "recompile and see it on screen" end-to-end.
                //
                // On compile failure the registry is still called so
                // subscribersNotified in the result gives an accurate
                // bucket-size count for the log line, but neither apply()
                // nor onReloaded() fires inside Dispatch — the pass keeps
                // its prior-good shader module + pipeline, which is the
                // whole point of the apply/onReloaded split.
                const auto dispatch =
                    CatEngine::RHI::ShaderReloadRegistry::Get().Dispatch(
                        sourcePath, spvBytes, result.ok);

                if (result.ok) {
                    Engine::Logger::info(
                        "[hot-reload] recompiled " + sourcePath +
                        " (" + std::to_string(spvBytes.size()) + " bytes, " +
                        "applied on " + std::to_string(dispatch.applySucceeded) +
                        " of " + std::to_string(dispatch.subscribersNotified) +
                        " subscriber(s))");
                } else {
                    // Warn (not error) because a single bad save shouldn't
                    // make the log look like a crash — the running game
                    // is unaffected, previous-good .spv is still on disk
                    // and every subscribed pass kept its previous-good
                    // VulkanShader module bound to its VkPipeline.
                    Engine::Logger::warn(
                        "[hot-reload] FAILED " + sourcePath +
                        " (exit=" + std::to_string(result.exitCode) + ", " +
                        std::to_string(dispatch.subscribersNotified) +
                        " subscriber(s) unaffected)\n" +
                        result.stderrTail);
                }
            });
    }
#endif

    // ========================================================================
    // Main Game Loop
    // ========================================================================
    Engine::Logger::info("Entering main loop...");

    CatEngine::Timer timer;
    timer.Start();
    float deltaTime = 0.0f;
    float fpsTimer = 0.0f;
    uint32_t frameCount = 0;
    float currentFPS = 0.0f;

    using FrameClock = std::chrono::steady_clock;
    auto nextFrameDeadline = FrameClock::now();

    // Frame / walltime caps for non-interactive runs. When either trips we
    // break out of the loop cleanly so the standard Shutdown path still runs
    // (GPU waits, AssetManager drain, RHI teardown). A SIGKILL-style timeout
    // wrapper would skip that, so `--exit-after-seconds` / `--max-frames`
    // is the right tool for the nightly smoke test.
    const auto runStart = FrameClock::now();
    uint64_t totalRenderedFrames = 0;

    bool running = true;

    // F3 toggles the ImGui profiler overlay. Off by default because the
    // panel covers the top-left of the HUD; a reviewer opting in with F3
    // sees frame-time history + per-scope CPU ms populated from the
    // Profiler singleton's ring buffer. GPU query resolution isn't wired
    // through the render graph yet, so the overlay's GPU table shows a
    // "not resolved yet" hint in this iteration — the CPU side is
    // already the primary signal a reviewer wants.
    bool showProfilerOverlay = false;

    // F4 twins the F3 profiler toggle: both panels are reviewer-aimed
    // surfaces, kept independent so a reviewer can watch frame times
    // (F3) while tweaking settings (F4) without closing one to see the
    // other. Default off so autoplay nightly runs don't pollute the
    // golden frame with ImGui chrome.
    bool showGraphicsSettingsPanel = false;

    while (running && !window.shouldClose()) {
        // Mark the start of a new profiling frame. BeginFrame/EndFrame are
        // what populate the 120-frame ring buffer that the overlay graphs;
        // without this pair the overlay is permanently empty. Safe to call
        // even when the overlay isn't open — the profiler is always-on by
        // default and the user can toggle capture via the overlay's
        // "Capture enabled" checkbox.
        CatEngine::Profiler::Get().BeginFrame();

        // Frame-pacing anchor: captured before work begins so the optional
        // sleep at the bottom of the loop targets a stable cadence instead
        // of drifting with the work duration.
        const auto frameStart = FrameClock::now();

        // CLI-driven exit gates. Checked before the frame does any real work
        // so the shutdown path runs with the loop's state machine idle.
        if (cmdArgs.maxFrames > 0 && totalRenderedFrames >= cmdArgs.maxFrames) {
            Engine::Logger::info("[cli] --max-frames reached (" +
                                 std::to_string(totalRenderedFrames) +
                                 " frames), exiting cleanly");
            running = false;
            break;
        }
        if (cmdArgs.exitAfterSeconds > 0.0f) {
            const float elapsedSec = std::chrono::duration<float>(
                frameStart - runStart).count();
            if (elapsedSec >= cmdArgs.exitAfterSeconds) {
                Engine::Logger::info("[cli] --exit-after-seconds reached (" +
                                     std::to_string(elapsedSec) +
                                     "s), exiting cleanly");
                running = false;
                break;
            }
        }

        // Update timer and get delta time
        deltaTime = static_cast<float>(timer.Update());

        // Poll events
        window.pollEvents();

        // Update input
        input.update();

#if defined(CAT_ENGINE_SHADER_HOT_RELOAD)
        // Shader hot-reload poll. Throttled to ~4 Hz internally, so the
        // per-frame call cost at 60 fps is a single double comparison
        // 15 out of 16 frames — negligible. We accumulate deltaTime into
        // a local clock rather than using steady_clock::now() so the
        // driver's throttle is paused when the engine is paused (e.g.
        // a future debugger-attached breakpoint freeze won't make the
        // driver re-scan when the developer steps past a few frames).
        shaderHotReloadClockSec += static_cast<double>(deltaTime);
        shaderHotReloadDriver.Tick(shaderHotReloadClockSec);
#endif

        // F3: toggle the ImGui profiler overlay. isKeyPressed fires on the
        // rising edge, so holding F3 down doesn't rapidly flip the state.
        if (input.isKeyPressed(Engine::Input::Key::F3)) {
            showProfilerOverlay = !showProfilerOverlay;
            Engine::Logger::info(
                std::string("[profiler-overlay] ") +
                (showProfilerOverlay ? "shown" : "hidden"));
        }

        // F4: graphics settings panel. Independent of F3 — both can be
        // shown at once. Logged at toggle time so the autoplay log shows
        // reviewer interaction beats for post-hoc review.
        if (input.isKeyPressed(Engine::Input::Key::F4)) {
            showGraphicsSettingsPanel = !showGraphicsSettingsPanel;
            Engine::Logger::info(
                std::string("[graphics-settings] ") +
                (showGraphicsSettingsPanel ? "shown" : "hidden"));
        }

        // Start a new ImGui frame so update() can emit widgets via MainMenu etc.
        imguiLayer.BeginFrame();

        // Update game (scoped so the "update" CPU scope shows up in the
        // overlay's per-scope table as the dominant cost).
        {
            CatEngine::ProfileScope _updateScope("update");
            game->update(deltaTime);
        }

        // Draw the profiler overlay AFTER game->update() so its own ImGui
        // emissions go into the current frame's draw list. Draw() is a
        // no-op when showProfilerOverlay is false.
        CatEngine::ProfilerOverlay::Draw(&showProfilerOverlay);

        // Draw the graphics-settings panel in the same window-scope. It
        // binds to gameConfig.graphics so its slider moves land in the
        // struct the main loop's pacing + per-second-log blocks already
        // read on subsequent frames — no apply/commit dance needed for the
        // live-tunable subset (maxFPS, VSync, showFPS).
        CatEngine::GraphicsSettingsPanel::Draw(&showGraphicsSettingsPanel,
                                               gameConfig.graphics);

        // Render
        if (!window.isMinimized()) {
            if (renderer->BeginFrame()) {
                // Render game (world, entities, effects)
                {
                    CatEngine::ProfileScope _renderScope("render");
                    game->render();
                }
                renderer->EndFrame();
                // Count only frames that actually made it to the GPU. The
                // --max-frames cap is about rendered output, not polled
                // input ticks — e.g. a minimized window shouldn't trip the
                // cap early just because pollEvents() is still running.
                ++totalRenderedFrames;
            }
        }

        // Calculate FPS
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 1.0f) {
            currentFPS = static_cast<float>(frameCount) / fpsTimer;
            if (gameConfig.graphics.showFPS) {
                Engine::Logger::info("FPS: " + std::to_string(static_cast<int>(currentFPS)));
            }
            // Playtest heartbeat: one line per second regardless of the
            // showFPS toggle so portfolio viewers (and the nightly openclaw
            // agent) have proof the loop is alive and making progress.
            // Includes state so the log tells a story: Playing → wave 3,
            // Paused, GameOver, etc. Stays well under the 25 lines/frame
            // that the old UIPass/Vulkan debug prints were emitting.
            //
            // During Playing we also emit per-second gameplay signal (wave,
            // enemies remaining, player HP, kills, level+xp) so a reader of
            // the log — human watching the autoplay run or the openclaw
            // nightly agent diffing iteration→iteration — can see progress
            // without bringing the game window up. Picked these specific
            // fields because together they prove the whole core loop is
            // still making forward progress: new waves are being queued
            // (wave changes), combat is doing damage (hp ticks or enemies
            // get killed), and the reward path is wired (xp climbs,
            // occasionally level up). Any subsystem that silently stops
            // working reveals itself as a frozen value here.
            static uint64_t totalFrames = 0;
            totalFrames += frameCount;
            const char* stateName = "null";
            if (game != nullptr) {
                switch (game->getGameState()) {
                    case CatGame::GameState::MainMenu: stateName = "MainMenu"; break;
                    case CatGame::GameState::Playing:  stateName = "Playing";  break;
                    case CatGame::GameState::Paused:   stateName = "Paused";   break;
                    case CatGame::GameState::GameOver: stateName = "GameOver"; break;
                    case CatGame::GameState::Victory:  stateName = "Victory";  break;
                }
            }

            std::string heartbeat =
                "heartbeat: frames=" + std::to_string(totalFrames) +
                " fps=" + std::to_string(static_cast<int>(currentFPS)) +
                " state=" + std::string(stateName);

            // Gameplay-signal appendix is only meaningful while the core
            // loop is actually running. In MainMenu / Paused / GameOver /
            // Victory the wave + enemy + HP numbers are either zero or
            // stale (last-frame-of-Playing) — printing them would muddy
            // the log without adding signal. `game != nullptr` is a
            // defense-in-depth check: the game pointer is constructed
            // before the main loop begins, so in practice this is always
            // true here, but the heartbeat block is deliberately tolerant
            // of partial init for future harness use (e.g. a future
            // `--benchmark-init` path that skips the game subsystem).
            if (game != nullptr &&
                game->getGameState() == CatGame::GameState::Playing) {
                const auto* waveSystem = game->getWaveSystem();
                if (waveSystem != nullptr) {
                    heartbeat += " wave=" + std::to_string(waveSystem->getCurrentWave()) +
                                 " enemies_left=" +
                                 std::to_string(waveSystem->getEnemiesRemaining());
                }

                heartbeat += " kills=" + std::to_string(game->getEnemiesKilled());

                // Player HP: look up the HealthComponent via ECS. Using the
                // raw ECS rather than caching a pointer in the game class
                // because the player entity can be re-created on restart()
                // and any cached pointer would dangle. One ECS lookup per
                // second is cheap relative to per-frame cost.
                auto& ecs = game->getECS();
                auto playerEnt = game->getPlayerEntity();
                if (const auto* health =
                        ecs.getComponent<CatGame::HealthComponent>(playerEnt)) {
                    const int hp = static_cast<int>(health->currentHealth);
                    const int maxHp = static_cast<int>(health->maxHealth);
                    heartbeat += " hp=" + std::to_string(hp) + "/" +
                                 std::to_string(maxHp);
                }

                if (const auto* leveling = game->getLevelingSystem()) {
                    heartbeat += " lvl=" + std::to_string(leveling->getLevel()) +
                                 " xp=" + std::to_string(leveling->getXP()) + "/" +
                                 std::to_string(leveling->getXPToNextLevel());
                }
            }

            Engine::Logger::info(heartbeat);
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // CPU-side frame cap.
        //
        // VSync normally pins the cadence via vkQueuePresentKHR blocking on
        // the compositor. But the settings UI can disable VSync at runtime,
        // and some Vulkan drivers silently fall back to IMMEDIATE mode when
        // the requested present mode isn't available — in both cases the
        // loop would otherwise spin as fast as the CPU can dispatch draws,
        // burning ~100% of a core. When the user wants an explicit target
        // (gameConfig.graphics.maxFPS > 0) OR VSync is off, sleep until the
        // start of the next frame. The default maxFPS=60 fallback applies
        // only when VSync is disabled and the config doesn't specify one,
        // so VSync-on users keep their native refresh rate.
        const bool vsyncActive = gameConfig.graphics.vsync;
        const uint32_t configuredCap = gameConfig.graphics.maxFPS;
        const uint32_t targetFPS = (configuredCap > 0)
            ? configuredCap
            : (vsyncActive ? 0u : 60u);

        if (targetFPS > 0) {
            using namespace std::chrono;
            const auto targetFrameTime =
                duration_cast<FrameClock::duration>(
                    duration<double>(1.0 / static_cast<double>(targetFPS)));

            // Advance the deadline from the previous deadline when possible
            // to avoid jitter accumulation; if we fell far behind (e.g.
            // stall, debugger break), resync to the current time so we
            // don't burn the next N frames catching up.
            nextFrameDeadline += targetFrameTime;
            const auto now = FrameClock::now();
            if (nextFrameDeadline < now) {
                nextFrameDeadline = now + targetFrameTime;
            }
            std::this_thread::sleep_until(nextFrameDeadline);
        } else {
            // Keep the deadline tracking current time so the first frame
            // after a toggle-on doesn't try to catch up through history.
            nextFrameDeadline = frameStart;
        }

        // Close the profiling frame. Paired with BeginFrame at the top of
        // the loop; if an exception/early-break skips this, the ring buffer
        // simply misses one frame of data — not a correctness issue.
        CatEngine::Profiler::Get().EndFrame();
    }

    // ========================================================================
    // Cleanup
    // ========================================================================
    Engine::Logger::info("Shutting down...");

    // --frame-dump: headless-CI-style capture of the final swapchain image
    // before any teardown runs. Must happen HERE, after the main loop has
    // called Renderer::EndFrame() + Present() for its last iteration but
    // BEFORE game->shutdown() / imguiLayer.Shutdown() / renderer->Shutdown()
    // tear down the Vulkan objects the readback depends on (the swapchain
    // image, the VulkanDevice's command pool, the graphics queue).
    //
    // Silently skipping when the flag isn't set preserves every prior
    // playtest's exit path byte-for-byte; the feature is strictly additive.
    // Failure to capture is logged (by the method itself) but is NOT fatal:
    // a broken golden-image path should not mask a clean gameplay run.
    if (!cmdArgs.frameDumpPath.empty()) {
        if (renderer && renderer->CaptureSwapchainToPPM(cmdArgs.frameDumpPath)) {
            Engine::Logger::info("[cli] --frame-dump: captured to '" +
                                 cmdArgs.frameDumpPath + "'");
        } else {
            Engine::Logger::warn("[cli] --frame-dump: capture failed for path '" +
                                 cmdArgs.frameDumpPath + "'");
        }
    }

    // Save configuration
    if (!gameConfig.save(cmdArgs.configPath)) {
        Engine::Logger::warn("Failed to save config");
    }

    // Shutdown game (this will shutdown all game systems)
    game->shutdown();
    game.reset();

    // ImGui owns Vulkan descriptors; tear it down before the device goes away.
    imguiLayer.Shutdown();

    // Shutdown engine systems in reverse order
    audioEngine->shutdown();
    audioEngine.reset();

    renderer->Shutdown();
    renderer.reset();

    rhi->Shutdown();
    rhi.reset();

    // CUDA context cleanup happens automatically in destructor

    // Release any cached models/textures and stop the loader thread pool
    // before main exits. Doing this after the renderer/RHI are already
    // down is safe because the AssetManager only holds CPU-side data and
    // the loader tasks are pure-CPU (parsing + uploads are driven by the
    // renderer, which has already waited on the GPU at this point).
    CatEngine::AssetManager::GetInstance().Shutdown();

    Engine::Logger::info("===========================================");
    Engine::Logger::info("  CAT ANNIHILATION - Shutdown Complete");
    Engine::Logger::info("===========================================");

    return 0;
}
