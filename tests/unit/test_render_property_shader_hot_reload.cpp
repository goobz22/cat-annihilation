/**
 * @file test_render_property_shader_hot_reload.cpp
 * @brief Property tests for engine/rhi/ShaderHotReload.hpp +
 *        engine/rhi/ShaderReloadRegistry.hpp pure helpers.
 *
 * Sibling-but-distinct from tests/unit/test_shader_hot_reload.cpp and
 * tests/unit/test_shader_reload_registry.cpp. Those files pin the
 * deterministic contract on hand-crafted strings. This file shotguns
 * the helpers under random fuzz to surface a quoting / classification
 * edge case the hand-picked cases cannot construct, plus exercises the
 * purity contract — same input twice produces the same output, and the
 * normalisation round-trip is stable.
 *
 * Coverage goals:
 *
 *   1. ClassifyShaderKind is pure: same input → same output, regardless
 *      of call order or interleaving with other classifications.
 *
 *   2. QuoteShellArg is pure and an involution-ish wrapper: stripping
 *      the leading/trailing quote and inverse-escaping reproduces the
 *      original input.
 *
 *   3. BuildGlslcCommand is pure: same arguments → same command line.
 *
 *   4. DetectChangedSources matches its definition: changed iff
 *      entries[i].lastKnownMtime != currentMtimes[i].
 *
 *   5. TailLines: returning N lines of text whose line count <= N is a
 *      no-op; > N returns exactly N lines.
 *
 *   6. NormalizeSourcePath is idempotent: normalising twice produces the
 *      same string. Round-trip: any forward-slash-only input survives
 *      unchanged.
 *
 *   7. Random alphanumeric paths classify consistently with the
 *      documented extension lookup table.
 */

#include "catch.hpp"
#include "engine/rhi/ShaderHotReload.hpp"
#include "engine/rhi/ShaderReloadRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace CatEngine::RHI;
using namespace CatEngine::RHI::HotReloadDetail;
using namespace CatEngine::RHI::ShaderReloadRegistryDetail;

namespace {

constexpr uint32_t kPropertySeed = 0x5043'5043u;

std::string RandomAlphaNumPath(std::mt19937& rng, size_t minLen, size_t maxLen) {
    static const std::string kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_/-.";
    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    std::uniform_int_distribution<size_t> chDist(0, kAlphabet.size() - 1);
    const size_t length = lenDist(rng);
    std::string out;
    out.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(kAlphabet[chDist(rng)]);
    }
    return out;
}

// Recover the original argument from a QuoteShellArg result. Strip the
// outer quotes and collapse "" → " (the only escape the routine emits).
std::string UnquoteShellArg(const std::string& quoted) {
    if (quoted.size() < 2) return quoted;
    if (quoted.front() != '"' || quoted.back() != '"') return quoted;
    const std::string inner = quoted.substr(1, quoted.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
        if (i + 1 < inner.size() && inner[i] == '"' && inner[i + 1] == '"') {
            out.push_back('"');
            ++i; // skip the partner quote
        } else {
            out.push_back(inner[i]);
        }
    }
    return out;
}

} // namespace

// ============================================================================
// PROPERTY 1: ClassifyShaderKind is pure
// ============================================================================

TEST_CASE("ShaderHotReload property: ClassifyShaderKind is pure",
          "[hotreload][property][pure]") {
    // Sample 1000 random alphanumeric paths and confirm calling the
    // classifier multiple times in a row produces the same answer.
    std::mt19937 rng(kPropertySeed);
    for (int trial = 0; trial < 1000; ++trial) {
        const std::string path = RandomAlphaNumPath(rng, 5, 40);
        const ShaderKind k1 = ClassifyShaderKind(path);
        const ShaderKind k2 = ClassifyShaderKind(path);
        const ShaderKind k3 = ClassifyShaderKind(path);
        REQUIRE(k1 == k2);
        REQUIRE(k2 == k3);
    }
}

// ============================================================================
// PROPERTY 2: ClassifyShaderKind matches the documented extension table
// ============================================================================

TEST_CASE("ShaderHotReload property: classifier maps extensions correctly",
          "[hotreload][property][classify]") {
    // Build synthetic paths with each known extension and confirm the
    // returned kind matches the documented table. Repeat across random
    // base names so the classifier is exercised over varied prefixes.
    std::mt19937 rng(kPropertySeed ^ 0xCC55u);
    for (int trial = 0; trial < 200; ++trial) {
        const std::string base = RandomAlphaNumPath(rng, 1, 20);
        REQUIRE(ClassifyShaderKind(base + ".vert") == ShaderKind::Vertex);
        REQUIRE(ClassifyShaderKind(base + ".frag") == ShaderKind::Fragment);
        REQUIRE(ClassifyShaderKind(base + ".comp") == ShaderKind::Compute);
        // Unknown extensions: stay Unknown.
        REQUIRE(ClassifyShaderKind(base + ".unknown_ext_42") == ShaderKind::Unknown);
        REQUIRE(ClassifyShaderKind(base + ".glsl") == ShaderKind::Unknown);
        REQUIRE(ClassifyShaderKind(base + ".hlsl") == ShaderKind::Unknown);
    }
}

// ============================================================================
// PROPERTY 3: ClassifyShaderKind uses the LAST dot
// ============================================================================

TEST_CASE("ShaderHotReload property: classifier respects last-dot rule",
          "[hotreload][property][classify]") {
    // Filename "my.shader.vert" must classify as Vertex (last extension).
    std::mt19937 rng(kPropertySeed ^ 0xC0DEu);
    for (int trial = 0; trial < 100; ++trial) {
        const std::string base = RandomAlphaNumPath(rng, 1, 15);
        REQUIRE(ClassifyShaderKind(base + ".prefix.vert") == ShaderKind::Vertex);
        REQUIRE(ClassifyShaderKind(base + ".a.b.frag") == ShaderKind::Fragment);
        REQUIRE(ClassifyShaderKind(base + ".x.y.z.comp") == ShaderKind::Compute);
    }
}

// ============================================================================
// PROPERTY 4: ClassifyShaderKind handles degenerate filenames safely
// ============================================================================

TEST_CASE("ShaderHotReload property: classifier handles edge filenames",
          "[hotreload][property][classify]") {
    // Empty string, dot-only, no extension, trailing dot.
    REQUIRE(ClassifyShaderKind("") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind(".") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("file") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind("file.") == ShaderKind::Unknown);
    REQUIRE(ClassifyShaderKind(".vert") == ShaderKind::Vertex);
}

// ============================================================================
// PROPERTY 5: QuoteShellArg is pure
// ============================================================================

TEST_CASE("ShaderHotReload property: QuoteShellArg is pure",
          "[hotreload][property][pure]") {
    std::mt19937 rng(kPropertySeed ^ 0xDEAFu);
    for (int trial = 0; trial < 500; ++trial) {
        const std::string arg = RandomAlphaNumPath(rng, 0, 60);
        const std::string q1 = QuoteShellArg(arg);
        const std::string q2 = QuoteShellArg(arg);
        REQUIRE(q1 == q2);
    }
}

// ============================================================================
// PROPERTY 6: QuoteShellArg round-trips via Unquote
// ============================================================================

TEST_CASE("ShaderHotReload property: QuoteShellArg + unquote is identity",
          "[hotreload][property][quoting]") {
    std::mt19937 rng(kPropertySeed ^ 0x4242u);
    for (int trial = 0; trial < 500; ++trial) {
        const std::string original = RandomAlphaNumPath(rng, 0, 60);
        const std::string quoted = QuoteShellArg(original);
        const std::string back = UnquoteShellArg(quoted);
        REQUIRE(back == original);
    }
}

// ============================================================================
// PROPERTY 7: QuoteShellArg starts and ends with a double-quote
// ============================================================================

TEST_CASE("ShaderHotReload property: QuoteShellArg wraps in double quotes",
          "[hotreload][property][quoting]") {
    std::mt19937 rng(kPropertySeed ^ 0x9999u);
    for (int trial = 0; trial < 200; ++trial) {
        const std::string arg = RandomAlphaNumPath(rng, 0, 30);
        const std::string quoted = QuoteShellArg(arg);
        REQUIRE(quoted.size() >= 2);
        REQUIRE(quoted.front() == '"');
        REQUIRE(quoted.back() == '"');
    }
}

// ============================================================================
// PROPERTY 8: QuoteShellArg escapes embedded double-quotes correctly
// ============================================================================

TEST_CASE("ShaderHotReload property: QuoteShellArg doubles embedded quotes",
          "[hotreload][property][quoting]") {
    // The header documents that embedded quotes are escaped via doubling.
    REQUIRE(QuoteShellArg("a\"b") == "\"a\"\"b\"");
    REQUIRE(QuoteShellArg("\"start") == "\"\"\"start\"");
    REQUIRE(QuoteShellArg("end\"") == "\"end\"\"\"");
    REQUIRE(QuoteShellArg("\"\"") == "\"\"\"\"\"\"");
}

// ============================================================================
// PROPERTY 9: BuildGlslcCommand is pure
// ============================================================================

TEST_CASE("ShaderHotReload property: BuildGlslcCommand is pure",
          "[hotreload][property][pure]") {
    std::mt19937 rng(kPropertySeed ^ 0x7777u);
    for (int trial = 0; trial < 100; ++trial) {
        const std::string glslc = RandomAlphaNumPath(rng, 1, 30);
        const std::string source = RandomAlphaNumPath(rng, 1, 30) + ".vert";
        const std::string spv = RandomAlphaNumPath(rng, 1, 30) + ".spv";
        const std::string err = RandomAlphaNumPath(rng, 1, 30) + ".err";
        std::vector<std::string> includes = {
            RandomAlphaNumPath(rng, 1, 20),
            RandomAlphaNumPath(rng, 1, 20),
        };
        const std::string c1 = BuildGlslcCommand(glslc, source, spv, includes,
                                                  ShaderKind::Vertex, err);
        const std::string c2 = BuildGlslcCommand(glslc, source, spv, includes,
                                                  ShaderKind::Vertex, err);
        REQUIRE(c1 == c2);
    }
}

// ============================================================================
// PROPERTY 10: BuildGlslcCommand includes the right stage flag per kind
// ============================================================================

TEST_CASE("ShaderHotReload property: BuildGlslcCommand emits correct stage flag",
          "[hotreload][property][command]") {
    const std::vector<std::string> empty;
    const std::string vert = BuildGlslcCommand("glslc", "in.vert", "out.spv",
                                                empty, ShaderKind::Vertex, "");
    const std::string frag = BuildGlslcCommand("glslc", "in.frag", "out.spv",
                                                empty, ShaderKind::Fragment, "");
    const std::string comp = BuildGlslcCommand("glslc", "in.comp", "out.spv",
                                                empty, ShaderKind::Compute, "");
    REQUIRE(vert.find("-fshader-stage=vertex") != std::string::npos);
    REQUIRE(frag.find("-fshader-stage=fragment") != std::string::npos);
    REQUIRE(comp.find("-fshader-stage=compute") != std::string::npos);
}

// ============================================================================
// PROPERTY 11: BuildGlslcCommand emits one -I per include dir
// ============================================================================

TEST_CASE("ShaderHotReload property: BuildGlslcCommand emits -I per include dir",
          "[hotreload][property][command]") {
    std::vector<std::string> includes = {"a", "b", "c", "d"};
    const std::string cmd = BuildGlslcCommand("glslc", "in.vert", "out.spv",
                                                includes, ShaderKind::Vertex, "");
    // Count the occurrences of " -I".
    size_t count = 0;
    size_t pos = 0;
    while ((pos = cmd.find(" -I", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    REQUIRE(count == includes.size());
}

// ============================================================================
// PROPERTY 12: BuildGlslcCommand contains -O optimisation flag
// ============================================================================

TEST_CASE("ShaderHotReload property: BuildGlslcCommand always passes -O",
          "[hotreload][property][command]") {
    // Header documents -O as the default size-first optimisation, matching
    // build-time CMake. Pin it: every command emitted contains " -O".
    std::mt19937 rng(kPropertySeed ^ 0x1111u);
    for (int trial = 0; trial < 50; ++trial) {
        const std::string source = RandomAlphaNumPath(rng, 1, 20) + ".vert";
        const std::string spv = RandomAlphaNumPath(rng, 1, 20) + ".spv";
        const std::string cmd = BuildGlslcCommand("glslc", source, spv, {},
                                                    ShaderKind::Vertex, "");
        REQUIRE(cmd.find(" -O") != std::string::npos);
    }
}

// ============================================================================
// PROPERTY 13: BuildGlslcCommand respects empty stderr path
// ============================================================================

TEST_CASE("ShaderHotReload property: empty stderrPath suppresses redirect",
          "[hotreload][property][command]") {
    const std::string cmd = BuildGlslcCommand("glslc", "in.vert", "out.spv",
                                                {}, ShaderKind::Vertex, "");
    REQUIRE(cmd.find(" 2>") == std::string::npos);
}

// ============================================================================
// PROPERTY 14: DetectChangedSources matches its definition
// ============================================================================

TEST_CASE("ShaderHotReload property: DetectChangedSources matches != predicate",
          "[hotreload][property][detect]") {
    // Generate parallel entries + currentMtimes; assert the returned
    // index set is exactly those positions where lastKnownMtime !=
    // currentMtimes[i].
    using FileTime = std::filesystem::file_time_type;
    using FileDuration = FileTime::duration;
    std::mt19937 rng(kPropertySeed ^ 0xC0FAu);
    std::uniform_int_distribution<int64_t> tickDist(0, 1'000'000);
    for (int trial = 0; trial < 50; ++trial) {
        std::vector<ShaderSourceEntry> entries;
        std::vector<FileTime> mtimes;
        for (int j = 0; j < 10; ++j) {
            ShaderSourceEntry e;
            e.sourcePath = "shaders/x" + std::to_string(j) + ".vert";
            e.spvPath = "build/x" + std::to_string(j) + ".vert.spv";
            e.kind = ShaderKind::Vertex;
            // Build deterministic time stamps. We construct directly on
            // the file_time_type's native duration so the resulting
            // arithmetic stays well-typed across platforms — file_clock's
            // duration is implementation-defined and may not match
            // std::chrono::nanoseconds (e.g. on Windows it's
            // std::chrono::duration<long long, std::ratio<1, 10'000'000>>).
            e.lastKnownMtime = FileTime{} + FileDuration{tickDist(rng)};
            entries.push_back(e);
            mtimes.push_back(FileTime{} + FileDuration{tickDist(rng)});
        }
        // Compute expected indices.
        std::vector<size_t> expected;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].lastKnownMtime != mtimes[i]) expected.push_back(i);
        }
        const auto actual = DetectChangedSources(entries, mtimes);
        REQUIRE(actual == expected);
    }
}

// ============================================================================
// PROPERTY 15: DetectChangedSources size-mismatch returns empty
// ============================================================================

TEST_CASE("ShaderHotReload property: mismatched-size DetectChangedSources returns empty",
          "[hotreload][property][detect]") {
    std::vector<ShaderSourceEntry> entries(5);
    std::vector<std::filesystem::file_time_type> mtimes(3);
    const auto result = DetectChangedSources(entries, mtimes);
    REQUIRE(result.empty());
}

// ============================================================================
// PROPERTY 16: TailLines short inputs returned verbatim
// ============================================================================

TEST_CASE("ShaderHotReload property: TailLines returns input verbatim when short",
          "[hotreload][property][tail]") {
    // 5-line input with maxLines >= 5 → input returned unchanged.
    const std::string input = "a\nb\nc\nd\ne";
    REQUIRE(TailLines(input, 5) == input);
    REQUIRE(TailLines(input, 10) == input);
    REQUIRE(TailLines(input, 100) == input);
}

// ============================================================================
// PROPERTY 17: TailLines returns exactly N lines when input is long
// ============================================================================

TEST_CASE("ShaderHotReload property: TailLines trims to N lines",
          "[hotreload][property][tail]") {
    // 10-line input, tail 3 → "8\n9\n10" (the last 3 lines).
    const std::string input = "1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
    const std::string tailed = TailLines(input, 3);
    // Count newlines: 3-line tail has 2 internal newlines (last line has
    // no trailing newline).
    size_t newlineCount = std::count(tailed.begin(), tailed.end(), '\n');
    REQUIRE(newlineCount == 2);
    // Last line is "10".
    REQUIRE(tailed.find("10") != std::string::npos);
    REQUIRE(tailed.find("8") != std::string::npos);
    REQUIRE(tailed.find("7") == std::string::npos);
}

// ============================================================================
// PROPERTY 18: TailLines is pure
// ============================================================================

TEST_CASE("ShaderHotReload property: TailLines is pure",
          "[hotreload][property][pure]") {
    std::mt19937 rng(kPropertySeed ^ 0x3737u);
    for (int trial = 0; trial < 100; ++trial) {
        std::string text;
        const int lineCount = std::uniform_int_distribution<int>(1, 30)(rng);
        for (int l = 0; l < lineCount; ++l) {
            text += "line " + std::to_string(l);
            if (l < lineCount - 1) text += '\n';
        }
        const size_t tailN = std::uniform_int_distribution<int>(1, 30)(rng);
        const std::string r1 = TailLines(text, tailN);
        const std::string r2 = TailLines(text, tailN);
        REQUIRE(r1 == r2);
    }
}

// ============================================================================
// PROPERTY 19: TailLines with maxLines = 0 returns empty
// ============================================================================

TEST_CASE("ShaderHotReload property: TailLines with maxLines=0 returns empty",
          "[hotreload][property][tail]") {
    REQUIRE(TailLines("a\nb\nc", 0).empty());
    REQUIRE(TailLines("", 0).empty());
    REQUIRE(TailLines("", 100).empty());
}

// ============================================================================
// PROPERTY 20: NormalizeSourcePath collapses backslashes
// ============================================================================

TEST_CASE("ShaderHotReload property: NormalizeSourcePath collapses backslashes",
          "[hotreload][property][registry][normalize]") {
    // Every '\\' → '/'. Forward-slashes pass through unchanged.
    REQUIRE(NormalizeSourcePath("a\\b\\c") == "a/b/c");
    REQUIRE(NormalizeSourcePath("a/b/c") == "a/b/c");
    REQUIRE(NormalizeSourcePath("") == "");
    REQUIRE(NormalizeSourcePath("\\") == "/");
    REQUIRE(NormalizeSourcePath("/") == "/");
}

// ============================================================================
// PROPERTY 21: NormalizeSourcePath is idempotent
// ============================================================================

TEST_CASE("ShaderHotReload property: NormalizeSourcePath round-trip stable",
          "[hotreload][property][registry][normalize]") {
    std::mt19937 rng(kPropertySeed ^ 0xC9C9u);
    for (int trial = 0; trial < 500; ++trial) {
        // Build a path with random separator choices.
        std::string path;
        const int segments = std::uniform_int_distribution<int>(1, 8)(rng);
        for (int s = 0; s < segments; ++s) {
            path += RandomAlphaNumPath(rng, 1, 8);
            if (s < segments - 1) {
                path += (std::uniform_int_distribution<int>(0, 1)(rng)) ? '/' : '\\';
            }
        }
        const std::string once = NormalizeSourcePath(path);
        const std::string twice = NormalizeSourcePath(once);
        REQUIRE(once == twice);
        // After one normalisation, no backslash remains.
        REQUIRE(once.find('\\') == std::string::npos);
    }
}

// ============================================================================
// PROPERTY 22: NormalizeSourcePath fast path leaves forward-slash input alone
// ============================================================================

TEST_CASE("ShaderHotReload property: normalisation leaves forward-slash input intact",
          "[hotreload][property][registry][normalize]") {
    std::mt19937 rng(kPropertySeed ^ 0x4747u);
    for (int trial = 0; trial < 200; ++trial) {
        // Generate a path that already uses forward slashes only — should
        // come back byte-equal.
        std::string path = RandomAlphaNumPath(rng, 1, 30);
        // Sanitise any backslash that crept in from the random palette.
        for (auto& c : path) if (c == '\\') c = '/';
        const std::string norm = NormalizeSourcePath(path);
        REQUIRE(norm == path);
    }
}

// ============================================================================
// PROPERTY 23: NormalizeSourcePath survives mixed separators
// ============================================================================

TEST_CASE("ShaderHotReload property: NormalizeSourcePath handles mixed input",
          "[hotreload][property][registry][normalize]") {
    REQUIRE(NormalizeSourcePath("foo/bar\\baz") == "foo/bar/baz");
    REQUIRE(NormalizeSourcePath("\\foo/bar\\baz\\") == "/foo/bar/baz/");
    REQUIRE(NormalizeSourcePath("/a/b/c") == "/a/b/c");
    REQUIRE(NormalizeSourcePath("\\\\server\\share") == "//server/share");
}

// ============================================================================
// PROPERTY 24: ShaderHotReloader::AddSource yields consecutive indices
// ============================================================================

TEST_CASE("ShaderHotReload property: AddSource returns sequential indices",
          "[hotreload][property][driver]") {
    ShaderHotReloader r;
    for (size_t i = 0; i < 50; ++i) {
        const size_t idx = r.AddSource(
            "shaders/x" + std::to_string(i) + ".vert",
            "build/x" + std::to_string(i) + ".vert.spv");
        REQUIRE(idx == i);
    }
    REQUIRE(r.GetEntries().size() == 50);
}

// ============================================================================
// PROPERTY 25: AddSource classifies kind from extension
// ============================================================================

TEST_CASE("ShaderHotReload property: AddSource auto-classifies kind",
          "[hotreload][property][driver]") {
    ShaderHotReloader r;
    r.AddSource("shaders/x.vert", "build/x.vert.spv");
    r.AddSource("shaders/x.frag", "build/x.frag.spv");
    r.AddSource("shaders/x.comp", "build/x.comp.spv");
    r.AddSource("shaders/x.unknown", "build/x.unknown.spv");
    REQUIRE(r.GetEntries()[0].kind == ShaderKind::Vertex);
    REQUIRE(r.GetEntries()[1].kind == ShaderKind::Fragment);
    REQUIRE(r.GetEntries()[2].kind == ShaderKind::Compute);
    REQUIRE(r.GetEntries()[3].kind == ShaderKind::Unknown);
}

// ============================================================================
// PROPERTY 26: Scan() returns empty when no entries are registered
// ============================================================================

TEST_CASE("ShaderHotReload property: empty reloader has empty Scan",
          "[hotreload][property][driver]") {
    ShaderHotReloader r;
    REQUIRE(r.Scan().empty());
}

// ============================================================================
// PROPERTY 27: FindGlslcExecutable returns nullopt with no env var
// ============================================================================

TEST_CASE("ShaderHotReload property: FindGlslcExecutable handles missing env safely",
          "[hotreload][property][lookup]") {
    // We don't manipulate the environment from the test (it would race
    // with parallel Catch2 cases). We only assert the call returns
    // SOMETHING — either a value or nullopt — without crashing. Catches
    // a future contributor who deferences a null env var pointer.
    auto result = ShaderHotReloader::FindGlslcExecutable();
    (void)result; // value depends on whether VULKAN_SDK is set in CI
    SUCCEED("FindGlslcExecutable returned without crashing");
}

// ============================================================================
// PROPERTY 28: ShaderReloadRegistry Register/SubscriberCount agree
// ============================================================================

TEST_CASE("ShaderHotReload property: registry SubscriberCount matches Register calls",
          "[hotreload][property][registry][lifecycle]") {
    ShaderReloadRegistry::Get().ClearForTest();
    // Register N callbacks on the same key, confirm SubscriberCount = N.
    std::vector<ShaderReloadRegistry::SubscriptionHandle> handles;
    for (int i = 0; i < 10; ++i) {
        handles.push_back(ShaderReloadRegistry::Get().Register(
            "shaders/property_path.vert",
            [](const std::vector<uint8_t>&) { return true; }));
    }
    REQUIRE(ShaderReloadRegistry::Get().SubscriberCount(
        "shaders/property_path.vert") == 10);

    // Unregister all → SubscriberCount → 0.
    for (auto h : handles) {
        REQUIRE(ShaderReloadRegistry::Get().Unregister(h));
    }
    REQUIRE(ShaderReloadRegistry::Get().SubscriberCount(
        "shaders/property_path.vert") == 0);
    ShaderReloadRegistry::Get().ClearForTest();
}

// ============================================================================
// PROPERTY 29: Registry Register handles are unique across calls
// ============================================================================

TEST_CASE("ShaderHotReload property: registry hands out unique handles",
          "[hotreload][property][registry][handle]") {
    ShaderReloadRegistry::Get().ClearForTest();
    std::vector<size_t> seenValues;
    for (int i = 0; i < 50; ++i) {
        const auto h = ShaderReloadRegistry::Get().Register(
            "shaders/unique_test.vert",
            [](const std::vector<uint8_t>&) { return true; });
        REQUIRE(h.IsValid());
        // No handle should repeat.
        for (size_t v : seenValues) REQUIRE(v != h.value);
        seenValues.push_back(h.value);
    }
    ShaderReloadRegistry::Get().ClearForTest();
}

// ============================================================================
// PROPERTY 30: Registry default-constructed handle is invalid
// ============================================================================

TEST_CASE("ShaderHotReload property: default SubscriptionHandle is invalid",
          "[hotreload][property][registry][handle]") {
    ShaderReloadRegistry::SubscriptionHandle handle;
    REQUIRE_FALSE(handle.IsValid());
    REQUIRE(handle.value == 0);
    // Unregister of invalid handle is idempotent no-op.
    ShaderReloadRegistry::Get().ClearForTest();
    REQUIRE_FALSE(ShaderReloadRegistry::Get().Unregister(handle));
    ShaderReloadRegistry::Get().ClearForTest();
}

// ============================================================================
// PROPERTY 31: Path normalisation through Register/Dispatch is consistent
// ============================================================================

TEST_CASE("ShaderHotReload property: registry path normalisation is bidirectional",
          "[hotreload][property][registry][normalize]") {
    // Register with backslashes, fetch SubscriberCount with forward
    // slashes — must see the registered count. Pinned because a future
    // contributor could accidentally normalise only on one side.
    ShaderReloadRegistry::Get().ClearForTest();
    const auto handle = ShaderReloadRegistry::Get().Register(
        "shaders\\sub\\path.vert",
        [](const std::vector<uint8_t>&) { return true; });
    REQUIRE(handle.IsValid());
    REQUIRE(ShaderReloadRegistry::Get().SubscriberCount(
        "shaders/sub/path.vert") == 1);
    REQUIRE(ShaderReloadRegistry::Get().SubscriberCount(
        "shaders\\sub\\path.vert") == 1);
    ShaderReloadRegistry::Get().Unregister(handle);
    ShaderReloadRegistry::Get().ClearForTest();
}

// ============================================================================
// PROPERTY 32: ShaderCompileResult default-constructs in a sensible state
// ============================================================================

TEST_CASE("ShaderHotReload property: ShaderCompileResult default state is safe",
          "[hotreload][property][result]") {
    ShaderCompileResult r;
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.command.empty());
    REQUIRE(r.stderrTail.empty());
}

// ============================================================================
// PROPERTY 33: ShaderSourceEntry default state
// ============================================================================

TEST_CASE("ShaderHotReload property: ShaderSourceEntry default state",
          "[hotreload][property][entry]") {
    // ShaderSourceEntry's `kind` field has no in-class initialiser, so a
    // default-constructed entry has indeterminate `kind`. The test pins
    // the fields with documented defaults (strings empty, mtime at the
    // epoch). Surfaced contract gap: a follow-up could add
    // `ShaderKind kind = ShaderKind::Unknown;` to the header so default
    // construction is well-defined; this test file is explicitly NOT
    // allowed to edit the header so we test the value-initialised path
    // (which DOES zero-init the kind field) instead.
    ShaderSourceEntry e;
    REQUIRE(e.sourcePath.empty());
    REQUIRE(e.spvPath.empty());
    REQUIRE(e.lastKnownMtime == std::filesystem::file_time_type{});

    // Value-initialise via {}; this triggers zero-init for the POD
    // `kind` member because std::string has a non-trivial default
    // constructor that promotes the struct to value-init semantics.
    ShaderSourceEntry zeroInit{};
    REQUIRE(zeroInit.sourcePath.empty());
    REQUIRE(zeroInit.spvPath.empty());
    REQUIRE(zeroInit.kind == ShaderKind::Unknown);
    REQUIRE(zeroInit.lastKnownMtime == std::filesystem::file_time_type{});
}

// ============================================================================
// PROPERTY 34: Setters on the reloader leave entries intact
// ============================================================================

TEST_CASE("ShaderHotReload property: SetIncludeDirs/SetGlslcPath do not mutate entries",
          "[hotreload][property][driver]") {
    ShaderHotReloader r;
    r.AddSource("shaders/a.vert", "build/a.vert.spv");
    r.AddSource("shaders/b.frag", "build/b.frag.spv");
    const auto before = r.GetEntries();
    r.SetIncludeDirs({"/include/a", "/include/b"});
    r.SetGlslcPath("/some/path/to/glslc");
    const auto after = r.GetEntries();
    REQUIRE(before.size() == after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(before[i].sourcePath == after[i].sourcePath);
        REQUIRE(before[i].spvPath == after[i].spvPath);
        REQUIRE(before[i].kind == after[i].kind);
    }
}
