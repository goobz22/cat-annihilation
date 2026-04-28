#pragma once
// ============================================================================
// engine/rhi/ShaderHotReloadDriver.hpp
//
// Debug-only GLSL shader hot-reload DRIVER. The compile + swap layer
// (ShaderHotReloader + VulkanShader::ReloadFromSPIRV) landed in a prior
// iteration — this file is the handoff described in ENGINE_PROGRESS.md's
// **Next:** line: "file watcher driver tick + pipeline-cache invalidation
// callback. Builds directly on this iteration's ShaderHotReloader::Scan()
// and VulkanShader::ReloadFromSPIRV."
//
// What this driver does:
//
//   1. On Initialize(sourcesDir, compiledDir, includeDirs):
//        - Recursively enumerates every .vert / .frag / .comp under sourcesDir.
//        - Registers each one with the underlying ShaderHotReloader, mapped to
//          a sibling .spv path under compiledDir (flat, NAME.EXT.spv — matches
//          the CMake add_custom_command rule in CMakeLists.txt line ~660 so the
//          driver-written .spv is bit-compatible with the build-time output).
//        - Primes each entry's lastKnownMtime to "now on disk" so the FIRST
//          Tick after boot is a no-op; a designer's Ctrl+S is the first real
//          detection event.
//
//   2. On Tick(nowSec), throttled to ~4 Hz (SetIntervalSec() configurable):
//        - Scan() for mtime changes.
//        - For each changed index: CompileIndex() shells out to glslc.
//        - On success: slurp the freshly-written .spv bytes and fire every
//          registered ReloadCallback. Subscribers (the rendering subsystem
//          that owns a VulkanShader*) can then call ReloadFromSPIRV() on
//          the matching shader and mark its downstream pipelines dirty.
//        - On failure: fire the same callback path with ok=false + the
//          stderrTail so a reviewer sees the exact glslc diagnostic in the
//          HUD log without losing the running shader.
//
//   3. Stays completely quiet in release builds via the
//      CAT_ENGINE_SHADER_HOT_RELOAD gate in CMakeLists.txt — release binaries
//      don't ship glslc, don't fork subprocesses, don't even touch the
//      filesystem for shader mtimes.
//
// What this driver deliberately does NOT do (acceptance bar of this iteration,
// matches the two-iteration split called out in ENGINE_PROGRESS.md):
//
//   - It does not own a VulkanShader* registry. The rendering subsystem owns
//     the shader handles; the driver hands it the new bytes via the callback
//     and the subsystem routes them to the right VulkanShader::ReloadFromSPIRV.
//     That keeps the driver Vulkan-agnostic and unit-testable without a GPU.
//   - It does not invalidate VkPipelines. That's a per-pass concern (each
//     pass's Setup() builds its own pipelines) and lives in the subscriber.
//     The ReloadCallback is how a subscriber knows "your shader just changed,
//     mark downstream pipelines dirty and rebuild them lazily".
//
// Every non-trivial helper in HotReloadDriverDetail:: is a pure function with
// no Vulkan / no filesystem side effects where possible, so the Catch2 test
// suite can pound on them without spawning glslc or forking subprocesses.
// ============================================================================

#include "ShaderHotReload.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace CatEngine::RHI {

namespace HotReloadDriverDetail {

// -------------------------------------------------------------------------
// ShouldTick — throttle decision.
//
// Given a monotonic "seconds since some epoch" clock, decide whether enough
// wall-time has elapsed since `lastTickSec` to warrant re-scanning. The
// driver calls this once per frame; on a 60 fps client with the default
// 0.25s interval, ~15 frames out of every 16 short-circuit out.
//
// We treat a negative `lastTickSec` as "never ticked" and return true so
// the first call after Initialize kicks a scan immediately. That matches
// the expectation in the main loop: "tick() once per frame, first call
// may actually scan, later calls may short-circuit".
//
// intervalSec ≤ 0 means "every call". A caller that sets interval to 0
// during benchmarking wants per-frame scanning and shouldn't be penalised
// with a stale-by-one-frame result.
// -------------------------------------------------------------------------
inline bool ShouldTick(double nowSec, double lastTickSec, double intervalSec) noexcept {
    if (lastTickSec < 0.0) return true;
    if (intervalSec <= 0.0) return true;
    return (nowSec - lastTickSec) >= intervalSec;
}

// -------------------------------------------------------------------------
// MakeSpvPath — derive the compiled-output path for a given source.
//
// The house rule (CMake add_custom_command in CMakeLists.txt) is: every
// shader in shaders/ (regardless of subdir) writes its .spv into a FLAT
// output directory as NAME.EXT.spv. So shaders/forward/forward.vert →
// shaders/compiled/forward.vert.spv. We mirror that rule here so the
// driver-written .spv overwrites the exact file the game loader picks up
// at boot — no second index file to maintain, no subdir mirroring bug
// where the loader looks in shaders/compiled/forward/ and the driver
// wrote to shaders/compiled/.
//
// Pure function of two strings: no filesystem touch, trivially testable.
// -------------------------------------------------------------------------
inline std::string MakeSpvPath(std::string_view compiledDir,
                               std::string_view sourcePath) {
    // Take everything after the last slash OR backslash of sourcePath as
    // the filename. Don't use std::filesystem::path here — it would
    // normalise separators in a platform-dependent way and we want a
    // predictable UTF-8 string result on every platform.
    const size_t slash = sourcePath.find_last_of("/\\");
    const std::string_view filename = (slash == std::string_view::npos)
        ? sourcePath
        : sourcePath.substr(slash + 1);

    std::string out;
    out.reserve(compiledDir.size() + 1 + filename.size() + 4);
    out.append(compiledDir);
    // Append a forward slash if compiledDir doesn't already end in one.
    // We emit forward slashes unconditionally — cat-annihilation project
    // convention (see CLAUDE.md: "Forward slashes in paths"); the Vulkan
    // / fstream / std::filesystem APIs on Windows all accept both.
    if (!compiledDir.empty()) {
        const char last = compiledDir.back();
        if (last != '/' && last != '\\') out.push_back('/');
    }
    out.append(filename);
    out.append(".spv");
    return out;
}

// -------------------------------------------------------------------------
// SlurpBinaryFile — read a .spv (or any binary) file into a byte vector.
//
// Returns empty on any failure (missing, permission-denied, read error).
// The driver's contract with subscribers is that an empty bytes vector +
// result.ok=false means "don't touch the live shader, display the error".
//
// Opens in binary mode (std::ios::binary) so CRLF translation on Windows
// doesn't corrupt the SPIR-V bytecode. Uses seekg/tellg to size the read
// up front so we allocate the full vector once — no push_back per byte.
// -------------------------------------------------------------------------
inline std::vector<uint8_t> SlurpBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    const std::streamsize size = file.tellg();
    // tellg can legitimately return -1 on a streaming/non-seekable source
    // (e.g. a named pipe that someone redirected into the shader watch
    // list). Treat that as "can't determine size" and bail. A real .spv
    // on-disk always has a stat'able size.
    if (size < 0) return {};
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return {};
    }
    return bytes;
}

// -------------------------------------------------------------------------
// IsShaderSourceExtension — is this filename a stage we compile?
//
// Pure lowercase-match on the last ".xyz" of the filename. The reloader
// already has ClassifyShaderKind for the same purpose, but that returns
// an enum; here we only need a yes/no for the directory-enumeration
// filter, and doing it with a small inline func keeps the call site
// obvious. Kept in the Driver detail namespace (not HotReloadDetail)
// because it's a driver-layer concern — the reloader doesn't enumerate
// directories at all.
// -------------------------------------------------------------------------
inline bool IsShaderSourceExtension(std::string_view filename) noexcept {
    const size_t dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size()) return false;
    const std::string_view ext = filename.substr(dot + 1);
    return ext == "vert" || ext == "frag" || ext == "comp";
}

} // namespace HotReloadDriverDetail

// ===========================================================================
// ShaderHotReloadDriver — the live polling driver.
//
// Owns a ShaderHotReloader, a throttle clock, and a subscriber list. Tick()
// is safe to call every frame: if the interval hasn't elapsed it returns
// zero immediately. On a tick that fires, it scans, compiles each changed
// entry, and routes the result through every registered callback.
//
// The class is header-only (pure STL + ShaderHotReload.hpp) so it can live
// under #ifdef CAT_ENGINE_SHADER_HOT_RELOAD without dragging in any new
// .cpp symbol that might accidentally slip past the gate.
// ===========================================================================
class ShaderHotReloadDriver {
public:
    // Callback signature: (sourcePath, spvBytes, compileResult).
    //
    // - sourcePath is the path that was watched (same string the subscriber
    //   used at registration — subscribers key their VulkanShader* map on
    //   this string).
    // - spvBytes is the freshly-compiled bytecode on success. Empty on
    //   failure: subscribers must NOT call ReloadFromSPIRV with it.
    // - compileResult is the full ShaderCompileResult — subscribers can log
    //   result.command (the exact glslc line) or result.stderrTail.
    using ReloadCallback = std::function<void(const std::string& sourcePath,
                                               const std::vector<uint8_t>& spvBytes,
                                               const ShaderCompileResult& result)>;

    // Set up the watcher. Returns the number of shader entries registered
    // (0 if sourcesDir doesn't exist — the driver stays idle in that case
    // and Tick() is a harmless no-op, so a mis-configured path never
    // crashes the game loop).
    //
    // includeDirs is forwarded to glslc as -I flags. The house pattern is
    // to pass {"shaders", "shaders/common"} so any shader can #include
    // utility headers from shaders/common/.
    size_t Initialize(const std::string& sourcesDir,
                      const std::string& compiledDir,
                      std::vector<std::string> includeDirs = {}) {
        m_sourcesDir  = sourcesDir;
        m_compiledDir = compiledDir;

        std::error_code ec;
        if (!std::filesystem::exists(sourcesDir, ec) || ec) {
            // Mis-configured path or the shaders dir wasn't deployed
            // (happens in some CI layouts where tests run from build/
            // without the shader tree copied over). Leave m_reloader
            // empty so Tick() short-circuits.
            return 0;
        }

        // Recursive directory enumeration. We could hand-roll this with
        // std::filesystem::recursive_directory_iterator but keeping the
        // logic inline here lets us swallow the per-entry error_code
        // cleanly — a broken symlink in the shader tree shouldn't kill
        // the whole walk, it should just drop that entry from the watch
        // list. std::filesystem is std since C++17 and is compiled in
        // everywhere we target; no external dependency.
        for (const auto& dirent :
             std::filesystem::recursive_directory_iterator(
                 sourcesDir,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec)) {
            if (ec) { ec.clear(); continue; }
            if (!dirent.is_regular_file(ec)) { ec.clear(); continue; }
            const std::string filename = dirent.path().filename().string();
            if (!HotReloadDriverDetail::IsShaderSourceExtension(filename)) continue;

            // Normalise to forward slashes so the log output is consistent
            // across platforms and the driver's "am I watching X?" queries
            // don't get tripped up by a stray backslash on Windows.
            std::string sourcePath = dirent.path().generic_string();
            std::string spvPath    = HotReloadDriverDetail::MakeSpvPath(
                compiledDir, sourcePath);
            m_reloader.AddSource(std::move(sourcePath), std::move(spvPath));
        }

        m_reloader.SetIncludeDirs(std::move(includeDirs));

        // Prime lastKnownMtime so the first Tick() after boot doesn't
        // think all 28 shaders are "new" and recompile the whole tree —
        // which would be redundant (CMake already compiled them at build
        // time) and would burn multi-hundred-ms of glslc time before the
        // first frame.
        m_reloader.PrimeMtimes();

        m_lastTickSec = -1.0;  // negative => first ShouldTick() returns true
        return m_reloader.GetEntries().size();
    }

    // Subscribe to reload events. Callbacks fire in registration order on
    // the same thread that calls Tick() — the main loop thread. No
    // thread-safety is promised; Tick() is expected to run between
    // game->update() and game->render() where no other thread is
    // touching the renderer.
    void AddReloadCallback(ReloadCallback cb) {
        m_callbacks.push_back(std::move(cb));
    }

    void SetIntervalSec(double s) noexcept { m_intervalSec = s; }
    double GetIntervalSec() const noexcept { return m_intervalSec; }

    // Called once per frame. nowSec is a monotonic engine wall-clock that
    // only increases; the main loop accumulates deltaTime into it.
    //
    // Returns the count of compile attempts this tick (0 most ticks).
    //
    // Tick() catches std::filesystem exceptions from Scan() and
    // SlurpBinaryFile() to keep a transient filesystem error (antivirus
    // locking the file, network drive blip) from bringing down the
    // engine. On an exception we skip this tick and log nothing — the
    // next tick retries cleanly.
    size_t Tick(double nowSec) {
        if (m_reloader.GetEntries().empty()) return 0;
        if (!HotReloadDriverDetail::ShouldTick(nowSec, m_lastTickSec, m_intervalSec)) {
            return 0;
        }
        m_lastTickSec = nowSec;

        std::vector<size_t> changed;
        try {
            changed = m_reloader.Scan();
        } catch (const std::exception&) {
            // Transient filesystem error — bail this tick, next tick
            // retries. We intentionally don't log here because the main
            // loop may be running at 60 Hz and we don't want to spam.
            return 0;
        }
        if (changed.empty()) return 0;

        for (size_t idx : changed) {
            const ShaderCompileResult result = m_reloader.CompileIndex(idx);
            const auto& entry = m_reloader.GetEntries()[idx];

            // Slurp the freshly-written .spv only on success. On failure
            // we hand the subscriber an empty vector so it can't
            // accidentally install a truncated / stale .spv.
            std::vector<uint8_t> bytes;
            if (result.ok) {
                try {
                    bytes = HotReloadDriverDetail::SlurpBinaryFile(entry.spvPath);
                } catch (const std::exception&) {
                    // Slurp failed despite the compile succeeding —
                    // rare (we JUST wrote the file) but possible if an
                    // antivirus grabs the fd. Leave bytes empty and
                    // surface to the subscriber.
                }
            }

            for (auto& cb : m_callbacks) {
                cb(entry.sourcePath, bytes, result);
            }
        }
        return changed.size();
    }

    // Test-only accessor. Production callers go through the callbacks.
    const ShaderHotReloader& GetReloader() const { return m_reloader; }

private:
    ShaderHotReloader             m_reloader;
    std::vector<ReloadCallback>   m_callbacks;
    std::string                   m_sourcesDir;
    std::string                   m_compiledDir;
    // 0.25 s = 4 Hz. Picked because (a) it's below human-perceivable save
    // latency (Ctrl+S → reload → visual confirmation inside one sprint
    // of frames), (b) it's above filesystem polling noise — mtime
    // resolution on NTFS / ext4 is 1 ms which is way finer than we need,
    // (c) at 60 fps the engine does ~15 short-circuited Tick() calls
    // between actual scans, and the short-circuit is a single double
    // comparison, (d) a compile that takes >250 ms is rare enough that
    // two consecutive ticks with the same source would be a sign of a
    // genuinely pathological shader and worth letting the user see the
    // stall in the log rather than hiding it with a longer interval.
    double                        m_intervalSec = 0.25;
    // Negative sentinel = "never ticked"; first ShouldTick() returns
    // true so the initial frame's call actually runs a scan (which
    // reports empty because PrimeMtimes() ran at Initialize, so the
    // first-frame scan cost is minimal).
    double                        m_lastTickSec = -1.0;
};

} // namespace CatEngine::RHI
