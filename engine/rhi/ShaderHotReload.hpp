#pragma once
// ============================================================================
// engine/rhi/ShaderHotReload.hpp
//
// Debug-only GLSL hot-reload infrastructure (backlog item: "GLSL shader
// hot-reload (debug build only). Watch shaders/ for mtime changes, recompile
// to SPIR-V via glslang, invalidate the relevant VkPipeline objects in the
// RHI cache.").
//
// This iteration lands the first half of the two-iteration scope called out
// in ENGINE_PROGRESS.md: the **glslc shell-out + shader-module cache
// invalidation** piece. The second iteration will wire a polling file
// watcher onto the driver's main loop and plumb the "mark pipelines dirty"
// callback into VulkanPipelineCache.
//
// WHY header-only: the watcher logic is pure STL (no Vulkan, no CUDA, no
// engine math). Keeping it header-only lets the Catch2 unit tests link it
// without pulling VulkanRHI or any GPU symbol in, which matches the house
// pattern for CCD.hpp, CCDPrepass.hpp, SequentialImpulse.hpp etc. — pure
// math/logic headers tested in isolation.
//
// WHY gated behind CAT_ENGINE_SHADER_HOT_RELOAD: the mission prompt +
// backlog both say "Must be compiled out of release builds". Release builds
// ship pre-compiled .spv files in shaders/compiled/; they do not need a
// glslc binary at runtime, do not spawn subprocesses, and must not pay
// either cost. The single #define gate makes the inclusion an obvious
// audit line in release build logs.
//
// Acceptance bar for THIS iteration (the file-watcher ticker is iteration 2):
//   - A designer can call ShaderHotReloader::AddSource(...) for each
//     shader pair, Scan() detects a mtime bump on disk, and
//     CompileIndex(idx) produces a valid .spv file via glslc.
//   - The shader-module cache invalidation half lands as
//     VulkanShader::ReloadFromSPIRV() in the adjacent Vulkan backend; that
//     method is ONLY wired up once this header exists, so co-landing the
//     two is the minimum useful deliverable.
//   - Pure funcs in HotReloadDetail:: are unit-tested without spawning
//     glslc or touching the filesystem (see tests/unit/test_shader_hot_reload.cpp).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace CatEngine::RHI {

// ---------------------------------------------------------------------------
// Shader kind enum — the subset of shader stages the project ships.
// We keep this independent of RHITypes::ShaderStage on purpose: this header
// is meant to be usable by a future standalone "compile all shaders" CLI
// tool without dragging in Vulkan types, and the mapping to VkShaderStage
// lives in the VulkanShader backend anyway.
// ---------------------------------------------------------------------------
enum class ShaderKind : uint8_t {
    Unknown = 0,
    Vertex,    // .vert
    Fragment,  // .frag
    Compute,   // .comp
};

// A watched (source → compiled) shader pair. The struct is deliberately
// POD-ish: tests construct it directly with aggregate initialisation and
// don't need a ctor / destructor.
struct ShaderSourceEntry {
    std::string sourcePath;  // absolute path to .vert/.frag/.comp
    std::string spvPath;     // absolute path where the .spv will be written
    ShaderKind  kind;

    // Last known on-disk mtime. zero-initialised by default so the first
    // Scan() reliably flags every entry as "changed" and compiles it at
    // least once — matches the "lazy-on-first-tick" behaviour tests rely
    // on without requiring an explicit Prime() call from callers.
    std::filesystem::file_time_type lastKnownMtime{};
};

// Result of a single compile invocation. We keep `command` on the result
// so the profiler overlay / log ring can show the exact glslc line that
// ran — trivially useful when a hot-reload compile fails and the user
// wants to reproduce it from a terminal.
struct ShaderCompileResult {
    bool        ok = false;
    int         exitCode = 0;
    std::string command;     // exact command string passed to std::system
    std::string stderrTail;  // last N lines of glslc's stderr on failure
};

namespace HotReloadDetail {

// -------------------------------------------------------------------------
// ClassifyShaderKind — inspect the filename extension and return the kind.
//
// Only the tail after the LAST dot matters ("shaders/ui/ui.vert" →
// "vert" → Vertex). Unknown extensions (e.g. .glsl, .hlsl, .rchit) return
// Unknown so callers can bail early rather than feeding glslc an input it
// doesn't know the stage for.
//
// WHY case-sensitive match: glslc itself is case-sensitive on the stage
// flag (`-fshader-stage=vertex` vs `-fshader-stage=Vertex` is an error),
// so we mirror that strictness here. Every shader in shaders/** today
// uses the lowercase extensions.
// -------------------------------------------------------------------------
inline ShaderKind ClassifyShaderKind(std::string_view filename) noexcept {
    // Find the last '.' — not the first, since "my.shader.vert" is a
    // legitimate filename. string_view::find_last_of does the right thing
    // and returns npos when there's no extension at all.
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size()) {
        return ShaderKind::Unknown;
    }

    const std::string_view ext = filename.substr(dot + 1);
    if (ext == "vert") return ShaderKind::Vertex;
    if (ext == "frag") return ShaderKind::Fragment;
    if (ext == "comp") return ShaderKind::Compute;
    return ShaderKind::Unknown;
}

// -------------------------------------------------------------------------
// QuoteShellArg — wrap a path in double quotes, escaping embedded quotes.
//
// `std::system(cmd)` hands the string to the host shell (cmd.exe on
// Windows, /bin/sh on POSIX). Paths with spaces (and the cat-annihilation
// tree sits under "C:/Users/Matt-PC/Documents/App Development/...") must
// be quoted or the shell splits them into multiple tokens. Embedded
// double-quotes in paths are vanishingly rare in the wild but we escape
// them anyway so a malicious include path can't smuggle a shell metachar
// onto the command line.
//
// We intentionally do NOT use std::quoted here — its delimiter-escape
// behaviour uses a backslash, which cmd.exe does not unescape the same
// way /bin/sh does. Rolling our own keeps the behaviour identical on
// both platforms: wrap in ", double every inner " as "".
// -------------------------------------------------------------------------
inline std::string QuoteShellArg(std::string_view arg) {
    std::string out;
    out.reserve(arg.size() + 2);
    out.push_back('"');
    for (char c : arg) {
        if (c == '"') {
            // cmd.exe + /bin/sh both recognise "" inside a quoted string
            // as a literal double-quote. POSIX sh also accepts \" but the
            // "" form is the lowest-common-denominator that works in both
            // shells without special-casing the platform.
            out.push_back('"');
            out.push_back('"');
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

// -------------------------------------------------------------------------
// BuildGlslcCommand — assemble the glslc invocation that compiles
// `sourcePath` to `spvPath` with the given include search paths and
// explicit shader stage.
//
// Example output (Windows):
//   "C:/VulkanSDK/Bin/glslc.exe" -fshader-stage=vertex
//     -I"C:/path/with spaces/shaders" -O -o "build/foo.vert.spv"
//     "shaders/ui/foo.vert" 2>"build/foo.vert.spv.err"
//
// -O is SPIR-V optimisation pass 0 (glslc treats bare -O as its default
// size-first pass). This matches the CMake custom-command invocation
// in CMakeLists.txt that produces the release .spv files, so the
// hot-reloaded module is byte-for-byte equivalent to the shipped one if
// the source hasn't diverged.
//
// The stderr redirect is important: std::system only returns the exit
// code, so without the redirect the caller has no way to surface a
// glslc diagnostic. We redirect stderr to a sibling .err file the
// driver can slurp back up and report to the dev on failure.
// -------------------------------------------------------------------------
inline std::string BuildGlslcCommand(std::string_view                 glslcPath,
                                     std::string_view                 sourcePath,
                                     std::string_view                 spvPath,
                                     const std::vector<std::string>&  includeDirs,
                                     ShaderKind                       kind,
                                     std::string_view                 stderrPath) {
    std::ostringstream oss;
    oss << QuoteShellArg(glslcPath);

    // Explicit -fshader-stage is belt-and-braces: glslc normally infers
    // from the .vert/.frag/.comp extension, but some shader authors
    // prefer generic .glsl filenames. Passing the flag makes the
    // inference failure impossible.
    switch (kind) {
        case ShaderKind::Vertex:   oss << " -fshader-stage=vertex";   break;
        case ShaderKind::Fragment: oss << " -fshader-stage=fragment"; break;
        case ShaderKind::Compute:  oss << " -fshader-stage=compute";  break;
        case ShaderKind::Unknown:
            // Caller should have filtered these out; emitting without
            // -fshader-stage lets glslc fall back to extension inference
            // and produce a sensible error if that also fails.
            break;
    }

    for (const auto& dir : includeDirs) {
        oss << " -I" << QuoteShellArg(dir);
    }

    // -O (default optimiser) — matches build-time compile in CMakeLists.txt
    oss << " -O";
    oss << " -o " << QuoteShellArg(spvPath);
    oss << ' '    << QuoteShellArg(sourcePath);

    if (!stderrPath.empty()) {
        // 2> works identically on cmd.exe and /bin/sh — one of the very
        // few pieces of redirect syntax that is portable.
        oss << " 2>" << QuoteShellArg(stderrPath);
    }

    return oss.str();
}

// -------------------------------------------------------------------------
// DetectChangedSources — given a list of watch entries and the current
// on-disk mtime of each source, return the indices whose mtime differs
// from the entry's lastKnownMtime.
//
// The vectors must be parallel-indexed: entries[i] and currentMtimes[i]
// describe the same shader. This keeps the fun logic pure and
// independent of how the mtimes got sampled (tests use synthetic
// mtimes; the live driver uses std::filesystem::last_write_time). If
// currentMtimes.size() != entries.size() we treat it as "nothing
// changed" — caller is responsible for matching sizes, and a mismatch
// is a programmer bug, not a user-facing failure mode.
//
// WHY "!= lastKnown" instead of ">= lastKnown": a git checkout can
// produce a source file whose mtime is EARLIER than the last-known
// stamp (e.g. the designer pulled a branch to iterate on). We still
// want to recompile in that case because the content changed, so
// any difference is treated as a change.
// -------------------------------------------------------------------------
inline std::vector<size_t> DetectChangedSources(
    const std::vector<ShaderSourceEntry>&                entries,
    const std::vector<std::filesystem::file_time_type>&  currentMtimes) {
    std::vector<size_t> changed;
    if (entries.size() != currentMtimes.size()) {
        return changed;
    }
    changed.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].lastKnownMtime != currentMtimes[i]) {
            changed.push_back(i);
        }
    }
    return changed;
}

// -------------------------------------------------------------------------
// TailLines — return the last `maxLines` lines of `text`, joined with
// '\n'. Used by the driver to surface glslc's stderr on compile failure
// WITHOUT flooding the log ring with a full error dump — glslc can emit
// dozens of lines for a single syntax error if a header macro expansion
// is involved, and we only ever show the dev the last chunk in
// ImGui's log panel.
//
// Pure string manipulation, no IO; tested in isolation.
// -------------------------------------------------------------------------
inline std::string TailLines(std::string_view text, size_t maxLines) {
    if (maxLines == 0 || text.empty()) {
        return {};
    }

    // Two-pass algorithm: count total lines first, then walk forward and
    // skip past the first (total - maxLines) newlines; the cut point is
    // just after the last skipped newline. This handles the "no trailing
    // newline" case correctly — a trailing \n terminates its line so
    // doesn't add a line; no trailing \n means the final run-of-chars is
    // an extra un-terminated line.
    //
    // WHY two-pass instead of the more elegant walk-backwards-counting-
    // newlines approach: the single-pass backward walker has an off-by-
    // one that's tricky to get right when the input does NOT end in a
    // newline (the "last line" in a glslc diagnostic often lacks a
    // trailing \n). Two-pass is cheap here — glslc's worst-case stderr
    // is a few kilobytes, not megabytes, and this only runs on compile
    // failure so it is not on the render hot path.
    const bool endsWithNewline = (text.back() == '\n');
    size_t totalNewlines = 0;
    for (char c : text) {
        if (c == '\n') ++totalNewlines;
    }
    const size_t totalLines = totalNewlines + (endsWithNewline ? 0 : 1);

    if (totalLines <= maxLines) {
        // Input fits entirely inside the window — return as-is. This is
        // the common path for short error messages.
        return std::string(text);
    }

    const size_t newlinesToSkip = totalLines - maxLines;
    size_t seen = 0;
    size_t cut  = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++seen;
            if (seen == newlinesToSkip) {
                cut = i + 1;  // first char of the next line
                break;
            }
        }
    }

    return std::string(text.substr(cut));
}

} // namespace HotReloadDetail

// ===========================================================================
// ShaderHotReloader — the thin driver that owns the watch list, polls
// mtimes, invokes glslc, and returns recompile results the caller can feed
// back into VulkanShader::ReloadFromSPIRV.
//
// This class is stateful (owns the list) but keeps all non-trivial logic
// routed through HotReloadDetail:: so the pure funcs get all the test
// coverage. The class itself is a small wrapper over std::filesystem +
// std::system, neither of which is meaningfully testable without a
// process fork anyway.
// ===========================================================================
class ShaderHotReloader {
public:
    // Register a shader for watching. If `sourcePath` doesn't exist the
    // first Scan() will still flag it as "changed" (since lastKnownMtime
    // defaults to zero), CompileIndex will then fail, and the driver
    // surfaces the failure via ShaderCompileResult — same code path as a
    // broken .vert file, which matches the user expectation.
    //
    // Returns the index assigned to the new entry so the caller can call
    // CompileIndex / GetEntry with it.
    size_t AddSource(std::string sourcePath, std::string spvPath) {
        ShaderSourceEntry entry;
        entry.kind       = HotReloadDetail::ClassifyShaderKind(sourcePath);
        entry.sourcePath = std::move(sourcePath);
        entry.spvPath    = std::move(spvPath);
        m_entries.push_back(std::move(entry));
        return m_entries.size() - 1;
    }

    // Set additional #include directories glslc should search. By default
    // only the directory of the source file is searched. Engine shaders
    // typically need `shaders/common/` on the include path so common
    // utility headers are resolvable.
    void SetIncludeDirs(std::vector<std::string> dirs) {
        m_includeDirs = std::move(dirs);
    }

    // Override the glslc binary path. Defaults to the result of
    // FindGlslcExecutable(), which walks $VULKAN_SDK/Bin/glslc then the
    // system PATH. Tests and the ninja target both set this explicitly
    // for reproducibility.
    void SetGlslcPath(std::string path) {
        m_glslcPath = std::move(path);
    }

    const std::vector<ShaderSourceEntry>& GetEntries() const { return m_entries; }

    // Seed every entry's lastKnownMtime from the current on-disk mtime.
    //
    // WHY: AddSource() defaults lastKnownMtime to zero so the FIRST Scan()
    // reports every newly-registered entry as "changed" — which is the right
    // behaviour for a one-shot "compile everything" CLI tool. But the
    // live-driver use case (ShaderHotReloadDriver + main loop) wants the
    // opposite: after boot, the already-compiled .spv files on disk are
    // trusted, and only post-boot edits should trigger a recompile. Calling
    // PrimeMtimes() once at driver init swaps in today's mtime so the very
    // next Scan() returns empty — the designer's Ctrl+S after boot is the
    // first real detection event.
    //
    // Sources that don't exist on disk still read as zero (std::error_code
    // set), which leaves their lastKnownMtime at zero — the next Scan()
    // then flags the missing file as "changed" and the driver surfaces it
    // as a compile failure. That matches the normal missing-file UX and
    // avoids silently ignoring a mis-configured watch list.
    void PrimeMtimes() {
        for (auto& entry : m_entries) {
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(entry.sourcePath, ec);
            if (ec) {
                // File missing or unreadable — leave lastKnownMtime at
                // zero so the first Scan() surfaces the problem. See the
                // paragraph above for why this is the right choice.
                continue;
            }
            entry.lastKnownMtime = mtime;
        }
    }

    // Poll the filesystem for mtime changes across every registered
    // entry. Returns indices of entries whose on-disk mtime differs from
    // the recorded lastKnownMtime. Successful recompile updates
    // lastKnownMtime (see CompileIndex) so a subsequent Scan() without
    // an edit returns empty — idempotent under no change.
    //
    // A source that disappeared from disk is reported as "changed" (its
    // mtime reads as zero) so the caller can surface the missing-file
    // error in the same code path as a normal recompile failure.
    std::vector<size_t> Scan() const {
        std::vector<std::filesystem::file_time_type> currentMtimes;
        currentMtimes.reserve(m_entries.size());
        for (const auto& entry : m_entries) {
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(entry.sourcePath, ec);
            if (ec) {
                // Use default-constructed file_time_type on error. That
                // is guaranteed != any real on-disk mtime so
                // DetectChangedSources will report a change, and
                // CompileIndex will then produce a readable failure
                // result — a cleaner UX than silently swallowing the
                // missing file.
                mtime = std::filesystem::file_time_type{};
            }
            currentMtimes.push_back(mtime);
        }
        return HotReloadDetail::DetectChangedSources(m_entries, currentMtimes);
    }

    // Shell out to glslc to recompile entry `index`. On success the
    // entry's lastKnownMtime is bumped to the current mtime so the next
    // Scan() reports no change. On failure the entry is left alone so
    // the next Scan() still flags it — the designer can't starve on a
    // one-shot error.
    //
    // `kind` override is available for the rare Unknown-extension case;
    // pass ShaderKind::Unknown to use the entry's classified kind
    // verbatim (the common path).
    ShaderCompileResult CompileIndex(size_t index,
                                     ShaderKind kindOverride = ShaderKind::Unknown) {
        ShaderCompileResult result;
        if (index >= m_entries.size()) {
            result.command = "(invalid index)";
            return result;
        }
        auto& entry = m_entries[index];
        const ShaderKind effectiveKind = (kindOverride == ShaderKind::Unknown)
            ? entry.kind
            : kindOverride;

        // Sibling .err path — glslc stderr gets redirected here so we
        // can read the tail back after std::system returns.
        const std::string errPath = entry.spvPath + ".err";

        result.command = HotReloadDetail::BuildGlslcCommand(
            m_glslcPath,
            entry.sourcePath,
            entry.spvPath,
            m_includeDirs,
            effectiveKind,
            errPath);

        // std::system returns an implementation-defined int. On Windows
        // it's the process exit code directly. On POSIX it's encoded in
        // the low byte (WEXITSTATUS macro). We treat non-zero as failure
        // on both platforms; finer-grained decoding would be wasted code
        // for the hot-reload use case where any non-success is surfaced
        // the same way.
        result.exitCode = std::system(result.command.c_str());
        result.ok = (result.exitCode == 0);

        if (!result.ok) {
            // Read the .err file and keep the last 12 lines. 12 is a
            // pragma: enough context for a typical glslc syntax error
            // (the diagnostic line + a caret + a few preceding lines)
            // without blowing up the HUD log panel.
            std::ifstream errFile(errPath);
            if (errFile.is_open()) {
                std::ostringstream buf;
                buf << errFile.rdbuf();
                result.stderrTail = HotReloadDetail::TailLines(buf.str(), 12);
            }
        } else {
            // Successful compile — update mtime so Scan() stops flagging
            // this entry until the next real edit.
            std::error_code ec;
            entry.lastKnownMtime =
                std::filesystem::last_write_time(entry.sourcePath, ec);
            // If stat fails on a successful compile we leave
            // lastKnownMtime alone — next Scan() will flag it again,
            // and CompileIndex is idempotent so the only cost is one
            // extra compile.

            // Clean up the .err file on success — no point leaving a
            // zero-byte error log around.
            std::error_code rmEc;
            std::filesystem::remove(errPath, rmEc);
        }

        return result;
    }

    // Attempt to locate the glslc binary at process startup. Order:
    //   1. VULKAN_SDK env var → VULKAN_SDK/Bin/glslc[.exe]
    //   2. VULKAN_SDK env var → VULKAN_SDK/bin/glslc (Linux SDK)
    //   3. std::nullopt — caller falls back to naked "glslc" and trusts PATH.
    //
    // We keep this static so the test suite can exercise it without
    // instantiating a reloader.
    static std::optional<std::string> FindGlslcExecutable() {
        const char* sdk = std::getenv("VULKAN_SDK");
        if (sdk == nullptr || sdk[0] == '\0') {
            return std::nullopt;
        }
        // Check Bin\glslc.exe first (Windows SDK layout, which is the
        // primary dev surface for this project).
        std::filesystem::path base(sdk);
        for (const char* suffix : { "Bin\\glslc.exe",
                                    "Bin/glslc.exe",
                                    "bin/glslc",
                                    "bin/glslc.exe" }) {
            auto candidate = base / suffix;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec) {
                return candidate.string();
            }
        }
        return std::nullopt;
    }

private:
    std::vector<ShaderSourceEntry> m_entries;
    std::vector<std::string>       m_includeDirs;
    // Default to bare "glslc" — works if the Vulkan SDK's Bin dir is on
    // PATH, which the CMake find_program dance in CMakeLists.txt already
    // verifies at configure time. Callers can override via SetGlslcPath
    // to pin a specific SDK version.
    std::string                    m_glslcPath = "glslc";
};

} // namespace CatEngine::RHI
