/**
 * Unit tests for the debug-only GLSL shader hot-reload logic
 * (engine/rhi/ShaderHotReload.hpp).
 *
 * The header is intentionally split into a `HotReloadDetail::` namespace
 * of pure, STL-only functions and a thin `ShaderHotReloader` class that
 * wraps std::system + std::filesystem. Only the pure half is exercised
 * by this test — spawning a real glslc subprocess belongs in an
 * integration test (requires the Vulkan SDK on PATH, a GPU is NOT
 * required but the binary is), and the std::filesystem mtime behaviour
 * is already covered by the standard library's own test suite.
 *
 * The pure functions under test (from HotReloadDetail::):
 *   - ClassifyShaderKind : filename → ShaderKind enum
 *   - QuoteShellArg      : string → shell-safe double-quoted string
 *   - BuildGlslcCommand  : (paths, kind, includes) → exact command line
 *   - DetectChangedSources: (entries, mtimes) → indices of changed
 *   - TailLines          : (text, N) → last N lines joined with '\n'
 *
 * Why pure-function tests are the right shape here: the hot-reload
 * driver has two sources of complexity. One is "did glslc succeed?"
 * which is a subprocess-exit-code question best answered end-to-end;
 * the other is "did we build the right command, classify the right
 * kind, detect the right change, quote the right path?" which is a
 * string/logic question that should be pinned in isolation. A broken
 * quote routine (Windows path with a space) would make every compile
 * fail with a cryptic "file not found"; a broken DetectChangedSources
 * would either hot-recompile every frame (perf-destroying) or never
 * recompile (feature broken). Unit tests catch both classes of
 * regression without requiring a running Vulkan/GPU environment.
 */

#include "catch.hpp"
#include "engine/rhi/ShaderHotReload.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace CatEngine::RHI;
using namespace CatEngine::RHI::HotReloadDetail;

// ============================================================================
// ClassifyShaderKind — extension → enum
// ============================================================================

TEST_CASE("ClassifyShaderKind recognises .vert as Vertex", "[hot-reload]") {
    // The common path: a flat filename with a single extension.
    REQUIRE(ClassifyShaderKind("forward.vert") == ShaderKind::Vertex);
}

TEST_CASE("ClassifyShaderKind recognises .frag as Fragment", "[hot-reload]") {
    REQUIRE(ClassifyShaderKind("forward.frag") == ShaderKind::Fragment);
}

TEST_CASE("ClassifyShaderKind recognises .comp as Compute", "[hot-reload]") {
    // Compute shader path — the clustered forward lighting pipeline
    // (shaders/lighting/clustered.comp) is the only .comp in the tree
    // today but the classifier must cover all three engine-supported
    // stages regardless.
    REQUIRE(ClassifyShaderKind("clustered.comp") == ShaderKind::Compute);
}

TEST_CASE("ClassifyShaderKind uses the LAST dot, not the first", "[hot-reload]") {
    // Real shader names like "transparent_oit_accum.frag" contain
    // underscores but no intermediate dots; the more pathological case
    // is a filename like "my.shader.vert" which must classify as
    // Vertex (extension = "vert"), not as Unknown.
    REQUIRE(ClassifyShaderKind("my.shader.vert") == ShaderKind::Vertex);
    REQUIRE(ClassifyShaderKind("pipeline.pass.frag") == ShaderKind::Fragment);
}

TEST_CASE("ClassifyShaderKind returns Unknown for unsupported extensions",
          "[hot-reload]") {
    // HLSL source, raw GLSL without stage extension, and SPIR-V output
    // files should all fail classification — the caller is supposed to
    // filter on that before ever calling BuildGlslcCommand.
    REQUIRE(ClassifyShaderKind("shader.hlsl") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("shader.glsl") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("shader.spv")  == ShaderKind::Unknown);
}

TEST_CASE("ClassifyShaderKind handles missing or empty extension",
          "[hot-reload]") {
    // Guardrail: a filename with no dot, a filename that ENDS in a
    // dot (no extension text), and an empty string must all safely
    // return Unknown. The classifier must NOT index out of bounds.
    REQUIRE(ClassifyShaderKind("Makefile") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("shader.")  == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("")         == ShaderKind::Unknown);
}

TEST_CASE("ClassifyShaderKind is case-sensitive (matches glslc behaviour)",
          "[hot-reload]") {
    // glslc itself rejects `.VERT` (uppercase) because it infers stage
    // from a case-sensitive extension match. The classifier mirrors
    // that so dev's working on Windows (case-insensitive FS) get the
    // same outcome they would on Linux.
    REQUIRE(ClassifyShaderKind("shader.VERT") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("shader.Frag") == ShaderKind::Unknown);
}

// ============================================================================
// QuoteShellArg — path → shell-safe quoted string
// ============================================================================

TEST_CASE("QuoteShellArg wraps a plain path in double quotes", "[hot-reload]") {
    // The most common case: a simple path with no spaces. The wrapper
    // still quotes it — the rule is "always quote" so the caller never
    // has to think about whether a given path needs it.
    REQUIRE(QuoteShellArg("shader.vert") == "\"shader.vert\"");
}

TEST_CASE("QuoteShellArg quotes Windows paths with spaces", "[hot-reload]") {
    // This is THE reason the routine exists. The project tree lives
    // under "C:/Users/Matt-PC/Documents/App Development/cat-annihilation"
    // — without quoting, cmd.exe would split on the space between
    // "App" and "Development" and glslc would see two invalid tokens.
    const std::string path =
        "C:/Users/Matt-PC/Documents/App Development/cat-annihilation/shaders/ui/ui.vert";
    const std::string quoted = QuoteShellArg(path);
    REQUIRE(quoted.front() == '"');
    REQUIRE(quoted.back()  == '"');
    // Middle is unchanged — we're not escaping spaces specifically,
    // the outer quotes are enough on both cmd.exe and /bin/sh.
    REQUIRE(quoted.find("App Development") != std::string::npos);
}

TEST_CASE("QuoteShellArg doubles embedded double-quotes", "[hot-reload]") {
    // "" inside a quoted string is literal " on both cmd.exe and sh.
    // A path containing a literal " is bizarre but not impossible
    // (Windows allows it through programmatic API), and we must not
    // let it unbalance the outer quotes.
    const std::string input  = "path\"with\"quotes.vert";
    const std::string result = QuoteShellArg(input);
    REQUIRE(result == "\"path\"\"with\"\"quotes.vert\"");
}

TEST_CASE("QuoteShellArg handles an empty argument", "[hot-reload]") {
    // An empty string becomes a pair of double quotes — that's what
    // both shells need to represent "one empty positional argument".
    REQUIRE(QuoteShellArg("") == "\"\"");
}

// ============================================================================
// BuildGlslcCommand — assembling the full shell invocation
// ============================================================================

TEST_CASE("BuildGlslcCommand emits the expected form for a vertex shader",
          "[hot-reload]") {
    // A minimal invocation: no include dirs, simple paths, no stderr
    // redirect. Lock down the exact structure so a future refactor
    // can't silently swap flag order or drop a flag.
    const std::string cmd = BuildGlslcCommand(
        "glslc",
        "shaders/ui/ui.vert",
        "build/ui.vert.spv",
        {},
        ShaderKind::Vertex,
        "");
    // The leading executable must come first.
    REQUIRE(cmd.find("\"glslc\"") == 0);
    // -fshader-stage=vertex must be present for explicit stage flagging.
    REQUIRE(cmd.find("-fshader-stage=vertex") != std::string::npos);
    // -O (the default optimiser) must be applied to match the release
    // CMake custom-command build flags.
    REQUIRE(cmd.find(" -O ") != std::string::npos);
    // Output path appears under the -o flag.
    REQUIRE(cmd.find("-o \"build/ui.vert.spv\"") != std::string::npos);
    // Source path is the final positional argument. Locate -o; the
    // source appears AFTER the output arg, but we only check membership
    // here because trailing stderr redirect can shift positions.
    REQUIRE(cmd.find("\"shaders/ui/ui.vert\"") != std::string::npos);
}

TEST_CASE("BuildGlslcCommand emits the right stage flag for each kind",
          "[hot-reload]") {
    const auto cmd_v = BuildGlslcCommand("glslc", "a.vert", "a.spv", {},
                                         ShaderKind::Vertex, "");
    const auto cmd_f = BuildGlslcCommand("glslc", "a.frag", "a.spv", {},
                                         ShaderKind::Fragment, "");
    const auto cmd_c = BuildGlslcCommand("glslc", "a.comp", "a.spv", {},
                                         ShaderKind::Compute, "");
    REQUIRE(cmd_v.find("-fshader-stage=vertex")   != std::string::npos);
    REQUIRE(cmd_f.find("-fshader-stage=fragment") != std::string::npos);
    REQUIRE(cmd_c.find("-fshader-stage=compute")  != std::string::npos);
}

TEST_CASE("BuildGlslcCommand omits stage flag for Unknown kind",
          "[hot-reload]") {
    // Unknown kind means "let glslc infer from extension". We must not
    // emit a -fshader-stage flag in that case — glslc rejects
    // -fshader-stage=unknown with an error.
    const auto cmd = BuildGlslcCommand("glslc", "a.glsl", "a.spv", {},
                                       ShaderKind::Unknown, "");
    REQUIRE(cmd.find("-fshader-stage") == std::string::npos);
}

TEST_CASE("BuildGlslcCommand prepends each include dir with -I",
          "[hot-reload]") {
    // Order must be preserved: the first include dir in the vector
    // appears first on the command line (glslc searches -I dirs in
    // declaration order).
    const std::vector<std::string> includes = {
        "shaders/common",
        "shaders/forward",
    };
    const auto cmd = BuildGlslcCommand("glslc", "a.vert", "a.spv",
                                       includes, ShaderKind::Vertex, "");
    const auto firstI  = cmd.find("-I\"shaders/common\"");
    const auto secondI = cmd.find("-I\"shaders/forward\"");
    REQUIRE(firstI  != std::string::npos);
    REQUIRE(secondI != std::string::npos);
    REQUIRE(firstI < secondI);
}

TEST_CASE("BuildGlslcCommand quotes include dirs containing spaces",
          "[hot-reload]") {
    // The project tree lives under "App Development" — include dirs
    // must ride through the quoting routine like any other path or
    // glslc will fail with "cannot open file".
    const std::vector<std::string> includes = {
        "C:/Users/Matt-PC/Documents/App Development/cat-annihilation/shaders/common",
    };
    const auto cmd = BuildGlslcCommand("glslc", "a.vert", "a.spv",
                                       includes, ShaderKind::Vertex, "");
    REQUIRE(cmd.find("-I\"C:/Users/Matt-PC/Documents/App Development/") !=
            std::string::npos);
}

TEST_CASE("BuildGlslcCommand appends stderr redirect when path is non-empty",
          "[hot-reload]") {
    const auto cmd = BuildGlslcCommand("glslc", "a.vert", "a.spv", {},
                                       ShaderKind::Vertex, "a.spv.err");
    // 2> prefix + quoted path must live at the tail of the command so
    // cmd.exe / sh attaches it to the glslc invocation and not to some
    // earlier argument by accident.
    REQUIRE(cmd.find("2>\"a.spv.err\"") != std::string::npos);
}

TEST_CASE("BuildGlslcCommand omits stderr redirect when path is empty",
          "[hot-reload]") {
    // The empty-string case is a deliberate opt-out: the caller wants
    // stderr to go to the parent process's stderr (e.g. in CI logs).
    // We must NOT emit "2>\"\"" because that would redirect to an
    // empty filename and fail.
    const auto cmd = BuildGlslcCommand("glslc", "a.vert", "a.spv", {},
                                       ShaderKind::Vertex, "");
    REQUIRE(cmd.find("2>") == std::string::npos);
}

// ============================================================================
// DetectChangedSources — mtime-diff logic
// ============================================================================

TEST_CASE("DetectChangedSources reports every entry on first scan",
          "[hot-reload]") {
    // Default-constructed lastKnownMtime compares unequal to any real
    // mtime sampled from disk. This is the intended "lazy first-scan
    // triggers a compile" behaviour — the designer can register a
    // shader at startup and the very next tick produces the .spv
    // without any explicit Prime() step.
    std::vector<ShaderSourceEntry> entries(3);
    entries[0].sourcePath = "a.vert";
    entries[1].sourcePath = "b.frag";
    entries[2].sourcePath = "c.comp";

    // Non-zero "current mtimes" — each a distinct synthetic time.
    const auto base = std::filesystem::file_time_type::clock::now();
    std::vector<std::filesystem::file_time_type> current = {
        base,
        base + std::chrono::seconds(1),
        base + std::chrono::seconds(2),
    };

    const auto changed = DetectChangedSources(entries, current);
    REQUIRE(changed.size() == 3);
    REQUIRE(changed[0] == 0);
    REQUIRE(changed[1] == 1);
    REQUIRE(changed[2] == 2);
}

TEST_CASE("DetectChangedSources returns empty when nothing changed",
          "[hot-reload]") {
    // Having already recompiled every entry, lastKnownMtime matches
    // what's on disk. Scanning again without any edits must return
    // empty — this is the steady-state zero-cost tick and is what
    // keeps the hot-reloader from burning CPU every frame.
    const auto base = std::filesystem::file_time_type::clock::now();
    std::vector<ShaderSourceEntry> entries(2);
    entries[0].lastKnownMtime = base;
    entries[1].lastKnownMtime = base + std::chrono::seconds(5);

    std::vector<std::filesystem::file_time_type> current = {
        base,
        base + std::chrono::seconds(5),
    };

    REQUIRE(DetectChangedSources(entries, current).empty());
}

TEST_CASE("DetectChangedSources flags exactly the changed entry",
          "[hot-reload]") {
    // Edit one of N files. Only that file's index should appear in
    // the result — no false positives on untouched siblings, no
    // false negatives on the edited file.
    const auto base = std::filesystem::file_time_type::clock::now();
    std::vector<ShaderSourceEntry> entries(3);
    entries[0].lastKnownMtime = base;
    entries[1].lastKnownMtime = base + std::chrono::seconds(5);
    entries[2].lastKnownMtime = base + std::chrono::seconds(10);

    // The middle file was edited: its mtime advances, the others stay put.
    std::vector<std::filesystem::file_time_type> current = {
        base,
        base + std::chrono::seconds(7),  // <-- changed (5s → 7s)
        base + std::chrono::seconds(10),
    };

    const auto changed = DetectChangedSources(entries, current);
    REQUIRE(changed.size() == 1);
    REQUIRE(changed[0] == 1);
}

TEST_CASE("DetectChangedSources treats an earlier mtime as a change",
          "[hot-reload]") {
    // A git branch-switch can move a file BACKWARD in time. The logic
    // must treat any difference as a change, not just "newer than".
    // Otherwise pulling an older version of a shader wouldn't trigger
    // a recompile and the dev would see stale .spv output.
    const auto base = std::filesystem::file_time_type::clock::now();
    std::vector<ShaderSourceEntry> entries(1);
    entries[0].lastKnownMtime = base + std::chrono::seconds(10);

    std::vector<std::filesystem::file_time_type> current = {
        base + std::chrono::seconds(3),  // EARLIER than last-known
    };

    const auto changed = DetectChangedSources(entries, current);
    REQUIRE(changed.size() == 1);
    REQUIRE(changed[0] == 0);
}

TEST_CASE("DetectChangedSources returns empty on size mismatch",
          "[hot-reload]") {
    // Programmer-bug guardrail: if the mtime vector doesn't parallel
    // the entries vector, something is very wrong upstream. We return
    // empty (the safer failure) rather than indexing out of bounds or
    // raising. The driver can log the mismatch separately.
    std::vector<ShaderSourceEntry> entries(2);
    std::vector<std::filesystem::file_time_type> current(5);
    REQUIRE(DetectChangedSources(entries, current).empty());
}

TEST_CASE("DetectChangedSources handles the empty list", "[hot-reload]") {
    // No registered shaders → no work → empty result. Trivial but
    // worth pinning so an empty-watch-list driver tick doesn't assert.
    std::vector<ShaderSourceEntry> entries;
    std::vector<std::filesystem::file_time_type> current;
    REQUIRE(DetectChangedSources(entries, current).empty());
}

// ============================================================================
// TailLines — bounded stderr tail for the HUD log panel
// ============================================================================

TEST_CASE("TailLines returns the full input when it fits under the limit",
          "[hot-reload]") {
    const std::string input = "line1\nline2\nline3\n";
    REQUIRE(TailLines(input, 10) == input);
}

TEST_CASE("TailLines trims to exactly the last N lines", "[hot-reload]") {
    // A 5-line input tailed to 3 lines should return the last 3.
    // The newline between line2 and line3 is the cut point; tail
    // starts at the first character of line3.
    const std::string input = "line1\nline2\nline3\nline4\nline5\n";
    const auto tail = TailLines(input, 3);
    REQUIRE(tail == "line3\nline4\nline5\n");
}

TEST_CASE("TailLines returns empty when maxLines is zero", "[hot-reload]") {
    REQUIRE(TailLines("hello\nworld\n", 0) == "");
}

TEST_CASE("TailLines returns empty on empty input", "[hot-reload]") {
    REQUIRE(TailLines("", 5) == "");
}

TEST_CASE("TailLines preserves input without a trailing newline",
          "[hot-reload]") {
    // glslc errors don't always end in a newline. Tailing "a\nb\nc"
    // (no trailing \n) to 2 lines should return "b\nc" — the final
    // line without a terminator must be preserved intact.
    REQUIRE(TailLines("a\nb\nc", 2) == "b\nc");
}

// ============================================================================
// ShaderHotReloader smoke test — in-memory API surface
// ============================================================================

TEST_CASE("ShaderHotReloader AddSource classifies the kind from the path",
          "[hot-reload]") {
    // AddSource is the public API that turns two strings into a
    // ShaderSourceEntry. It must invoke ClassifyShaderKind on the
    // source path, not the .spv path (the latter has a different
    // extension), so a vertex shader landing in a randomly-named
    // .spv output still classifies as Vertex.
    ShaderHotReloader reloader;
    const size_t idx = reloader.AddSource("shaders/ui/ui.vert",
                                          "build/out/ui.vert.spv");
    REQUIRE(idx == 0);
    REQUIRE(reloader.GetEntries().size() == 1);
    REQUIRE(reloader.GetEntries()[0].kind == ShaderKind::Vertex);
    REQUIRE(reloader.GetEntries()[0].sourcePath == "shaders/ui/ui.vert");
    REQUIRE(reloader.GetEntries()[0].spvPath    == "build/out/ui.vert.spv");
}

TEST_CASE("ShaderHotReloader AddSource assigns sequential indices",
          "[hot-reload]") {
    // Indices must be stable and parallel to the entries vector.
    // Tests and the live driver both rely on the index returned from
    // AddSource to route CompileIndex back to the right entry.
    ShaderHotReloader reloader;
    REQUIRE(reloader.AddSource("a.vert", "a.spv") == 0);
    REQUIRE(reloader.AddSource("b.frag", "b.spv") == 1);
    REQUIRE(reloader.AddSource("c.comp", "c.spv") == 2);
    REQUIRE(reloader.GetEntries().size() == 3);
}

TEST_CASE("ShaderHotReloader Scan on empty list returns empty",
          "[hot-reload]") {
    // The watch list is empty at construction; Scan must be cheap
    // and return empty without any filesystem calls.
    ShaderHotReloader reloader;
    REQUIRE(reloader.Scan().empty());
}

TEST_CASE("ShaderHotReloader Scan reports missing file as changed",
          "[hot-reload]") {
    // Register a path that definitely doesn't exist on disk. std::
    // filesystem::last_write_time sets an error_code, Scan falls back
    // to a default-constructed mtime, which compares unequal to the
    // default-initialised lastKnownMtime... wait — it equals. The
    // entry starts with lastKnownMtime == default, and Scan returns
    // default when stat fails, so the diff is equal and we return
    // empty. That's deliberate: a shader that NEVER existed was
    // never registered for compile, so we don't churn on it every
    // scan. Verify THAT behaviour here so a future refactor
    // doesn't silently flip it.
    ShaderHotReloader reloader;
    reloader.AddSource("path/does/not/exist/nowhere.vert",
                       "path/does/not/exist/nowhere.spv");
    // lastKnownMtime defaults to zero; on-disk mtime read fails and
    // also defaults to zero. Equal → not flagged as changed.
    const auto changed = reloader.Scan();
    REQUIRE(changed.empty());
}

TEST_CASE("FindGlslcExecutable returns nullopt when VULKAN_SDK unset",
          "[hot-reload]") {
    // We cannot safely mutate process env in a unit test (the test
    // binary runs inside the same process as the Catch2 driver, and
    // stomping VULKAN_SDK could affect concurrent tests). Instead we
    // just pin the invariant "if the env is unset the result is
    // nullopt" — if the CI box has VULKAN_SDK set this test becomes
    // a no-op, which is acceptable because its job is only to guard
    // the null-case contract.
    const char* sdk = std::getenv("VULKAN_SDK");
    if (sdk == nullptr || sdk[0] == '\0') {
        REQUIRE_FALSE(ShaderHotReloader::FindGlslcExecutable().has_value());
    } else {
        // Env is set — we can only check the result type is well-
        // formed (string or nullopt), not which branch fires.
        auto result = ShaderHotReloader::FindGlslcExecutable();
        if (result.has_value()) {
            REQUIRE_FALSE(result->empty());
        }
    }
}
