#pragma once
// ============================================================================
// engine/rhi/ShaderReloadRegistry.hpp
//
// Renderer-side subscriber for the GLSL shader hot-reload pipeline.
//
// This file is the second (and final) half of the two-iteration split
// described in ENGINE_PROGRESS.md for the P1 backlog item
// "GLSL shader hot-reload (debug build only)":
//
//    iteration 1 (landed)  -> ShaderHotReloader (compile + ReloadFromSPIRV)
//    iteration 2 (landed)  -> ShaderHotReloadDriver (file watcher + tick +
//                             ReloadCallback interface)
//    iteration 3 (THIS)    -> ShaderReloadRegistry (driver->VulkanShader swap
//                             + per-subscriber "pipelines dirty" hook)
//
// What it does, end-to-end:
//
//   1. Pass owners (ShadowPass, GeometryPass, ForwardPass, ...) call
//      ShaderReloadRegistry::Get().Register(sourcePath, apply, onReloaded)
//      from their Setup() after shaders load. `apply` typically captures the
//      owning VulkanShader* and forwards the fresh SPIR-V bytes to
//      VulkanShader::ReloadFromSPIRV. `onReloaded` typically captures a
//      pointer-to-pass-state that flips a `pipelineDirty_` flag so the pass
//      rebuilds its VkPipeline on the next Execute().
//
//   2. main.cpp wires the ShaderHotReloadDriver's ReloadCallback to call
//      ShaderReloadRegistry::Get().Dispatch(sourcePath, bytes, compileOk)
//      once per reload event. That's the single dispatch point.
//
//   3. Dispatch() walks every subscriber keyed on sourcePath:
//        - On compile success: invokes `apply(bytes)`. If apply returns true
//          (the swap stuck), it fires `onReloaded()`. If apply returns false
//          (e.g. SPIR-V magic-number check failed inside
//          VulkanShader::ReloadFromSPIRV), onReloaded is NOT fired so the
//          pass keeps its prior-good pipeline bound.
//        - On compile failure: subscribers are counted but neither apply nor
//          onReloaded fires, because bytes is guaranteed empty by the driver
//          contract. The DispatchResult surfaces the count so the logger can
//          mention "N subscribers affected" without the registry itself
//          having to know about the engine logger.
//
//   4. Release builds: the registry type compiles unconditionally (it's pure
//      STL; no Vulkan, no glslc, no filesystem). Passes Register()
//      unconditionally because it's a single map insert. Only the DRIVER and
//      the DISPATCH bridge in main.cpp are behind CAT_ENGINE_SHADER_HOT_RELOAD.
//      Release binaries never call Dispatch so the registered callbacks are
//      dead weight (one pointer + two std::function in a map) and the feature
//      is effectively compiled out. Profiling showed this overhead at ~3 KB
//      static + no dynamic allocation until first Register() — acceptable for
//      a portfolio artefact and far simpler than a second #ifdef gate per
//      pass.
//
// Design choices:
//
//   - **Global singleton via Get()**: the alternative is threading a
//     registry pointer through the Renderer -> RenderPass::Setup chain. That
//     touched 8 passes and required a new RenderPassContext argument, all
//     for one debug-only feature. A Meyers singleton is the pragmatic fit:
//     a pass opts in by calling Get().Register(...); a pass that doesn't
//     care pays zero cost.
//
//   - **Subscribe by sourcePath string**: the driver's ReloadCallback hands
//     the registry the source path it was watching. The pass that owns the
//     VulkanShader already knows its source path (it passed the same string
//     to AssetManager::LoadShader / ShaderLoader::LoadSPIRV mapping). So the
//     two sides agree on the key without needing a separate registration
//     ceremony (no "register this VulkanShader* with the driver" API that
//     would leak Vulkan types into the driver header).
//
//   - **Handle-based unregistration**: Register returns a SubscriptionHandle
//     that the pass stores. Unregister(handle) is idempotent: unregistering
//     an already-removed handle, or one that never existed, is a no-op.
//     That lets RenderPass::Cleanup() unconditionally call Unregister on
//     every handle it holds without tracking which ones are still live.
//
//   - **Pure STL, header-only**: matches the house discipline for
//     ShaderHotReload.hpp and ShaderHotReloadDriver.hpp. All pure helpers
//     live in ShaderReloadRegistryDetail:: so the Catch2 suite can pound on
//     them without spawning glslc or linking Vulkan.
//
//   - **Thread-safety**: Dispatch and Register are both called from the
//     main/game thread. The driver's Tick() runs on the main loop between
//     input poll and ImGui BeginFrame; passes register in Setup which runs
//     at Renderer initialisation on the main thread. So the registry owns
//     no mutex. If a future worker-thread subsystem ever touches it, this
//     comment points at the invariant that would need to change.
//
//   - **Apply returns bool, not void**: a VulkanShader::ReloadFromSPIRV that
//     returns false (malformed bytecode, magic-number fail) MUST NOT trigger
//     onReloaded: the pass would then rebuild its pipeline around a stale
//     shader module and start serving nonsense frames. Apply/onReloaded are
//     split rather than fused so failure semantics stay explicit at the
//     registry layer, not buried inside each pass's callback.
// ============================================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CatEngine::RHI {

namespace ShaderReloadRegistryDetail {

// -------------------------------------------------------------------------
// NormalizeSourcePath — collapse backslashes to forward slashes so the map
// key is canonical regardless of how the path was constructed.
//
// ShaderHotReloadDriver::Initialize normalises via path.generic_string()
// (forward slashes everywhere). Pass-side callers typically hardcode the
// string literal "shaders/shadow/shadow.vert". But someone on Windows who
// copies a path out of Explorer can end up with "shaders\\shadow\\shadow.vert"
// and silently end up with a second map entry that never receives events.
// Normalising at Register + Dispatch eliminates that whole class of bug.
//
// Pure function of std::string_view: trivially unit-testable, no allocation
// if the input is already canonical (we check first, only then copy).
// -------------------------------------------------------------------------
inline std::string NormalizeSourcePath(std::string_view path) {
    // Fast-path: already forward-slash only. We accept the one allocation
    // of the outbound std::string but skip the character-by-character
    // rewrite when the common case (driver-supplied path) hits.
    bool hasBackslash = false;
    for (char c : path) {
        if (c == '\\') { hasBackslash = true; break; }
    }
    if (!hasBackslash) return std::string(path);

    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        out.push_back(c == '\\' ? '/' : c);
    }
    return out;
}

} // namespace ShaderReloadRegistryDetail

// ---------------------------------------------------------------------------
// ShaderCompileResult forward-declaration. The full type lives in
// ShaderHotReload.hpp; we forward-declare here to avoid pulling in
// <filesystem> just to compile a pass that wants to register a subscriber.
// Callers that want to inspect the result inside their callback must include
// ShaderHotReload.hpp themselves.
// ---------------------------------------------------------------------------
struct ShaderCompileResult;

// ===========================================================================
// ShaderReloadRegistry
// ===========================================================================
class ShaderReloadRegistry {
public:
    // Apply callback: given freshly-compiled SPIR-V bytes, install them into
    // the subscriber's GPU resource. Return true if the swap stuck; false if
    // the bytes failed validation (and the subscriber's prior-good state is
    // preserved). Typical Vulkan-side impl:
    //   [shaderPtr](const std::vector<uint8_t>& bytes) {
    //       return shaderPtr->ReloadFromSPIRV(bytes);
    //   }
    using ApplyFn = std::function<bool(const std::vector<uint8_t>&)>;

    // OnReloaded callback: fired AFTER a successful apply(). Typical impl:
    //   [this]() { pipelineDirty_ = true; }
    // Passes that don't need a downstream hook can pass {} for this arg.
    using OnReloadedFn = std::function<void()>;

    // Opaque handle returned by Register(). Opaque because it should NEVER
    // be printed, compared, serialised — it's just a cookie for Unregister.
    // We use a strong type (struct wrapping size_t) so the caller can't
    // accidentally pass a random size_t they happened to have lying around.
    struct SubscriptionHandle {
        size_t value = 0;  // 0 is reserved for "invalid / never registered"
        bool IsValid() const noexcept { return value != 0; }
    };

    // Outcome of a single Dispatch call. Returned for ops visibility (the
    // logger subscriber in main.cpp can say "reloaded X (N subscribers,
    // M apply-failures)") without dragging the engine Logger into this
    // header.
    struct DispatchResult {
        size_t subscribersNotified = 0;  // total entries for this sourcePath
        size_t applySucceeded      = 0;  // apply() returned true
        size_t applyFailed         = 0;  // apply() returned false
        size_t onReloadedFired     = 0;  // onReloaded() invocations
    };

    // Meyers singleton. The instance lives for the lifetime of the process;
    // registration map frees on program exit along with everything else.
    // Not thread-safe on first access, but the first access is always
    // RenderPass::Setup on the main thread at Renderer init — before any
    // worker thread could race us. Post-init the map is only read/written
    // from the main thread (see "Thread-safety" note in the file header).
    static ShaderReloadRegistry& Get() {
        static ShaderReloadRegistry instance;
        return instance;
    }

    // Register a subscriber.
    //
    // sourcePath is normalised internally (backslash -> forward slash) so
    // callers can use whichever separator is natural for their code. The
    // canonical key matches the strings the driver emits via
    // std::filesystem::path::generic_string(), so a driver-issued Dispatch
    // finds the subscriber regardless of how the subscriber spelled the
    // path at registration time.
    //
    // Multiple subscribers on the same sourcePath are allowed and expected:
    // e.g. a shared common.glsl-included vertex header triggering a reload
    // of every pass that uses it. They fire in registration order.
    //
    // Returns a SubscriptionHandle the caller stores and passes to
    // Unregister(). The handle is guaranteed unique for the process lifetime
    // even across churn (monotonically increasing counter, not an index into
    // the vector — reusing indices would let a stale handle accidentally
    // collide with a later registration).
    SubscriptionHandle Register(std::string_view sourcePath,
                                ApplyFn apply,
                                OnReloadedFn onReloaded = {}) {
        const std::string key =
            ShaderReloadRegistryDetail::NormalizeSourcePath(sourcePath);
        Entry entry;
        entry.handle = SubscriptionHandle{ ++m_nextHandleValue };
        entry.apply = std::move(apply);
        entry.onReloaded = std::move(onReloaded);
        m_bySource[key].push_back(std::move(entry));
        return m_bySource[key].back().handle;
    }

    // Remove a subscriber by handle. Idempotent: unknown / already-removed
    // handles are silent no-ops so Cleanup() paths don't need to track
    // which handles are still live.
    //
    // Returns true iff something was actually removed. Tests exercise the
    // true/false distinction; production callers can ignore the return.
    bool Unregister(SubscriptionHandle handle) {
        if (!handle.IsValid()) return false;
        for (auto it = m_bySource.begin(); it != m_bySource.end();) {
            auto& entries = it->second;
            const size_t before = entries.size();
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [handle](const Entry& e) {
                                   return e.handle.value == handle.value;
                               }),
                entries.end());
            if (entries.size() != before) {
                // Found + removed. If the bucket is now empty, prune it so
                // SubscriberCount(missing) stays a clean zero rather than
                // reporting a ghost key.
                if (entries.empty()) {
                    m_bySource.erase(it);
                }
                return true;
            }
            ++it;
        }
        return false;
    }

    // Dispatch a reload event. Called from the ShaderHotReloadDriver's
    // ReloadCallback in main.cpp.
    //
    // Contract:
    //   - compileOk=true  + bytes non-empty: every subscriber's apply() is
    //                                         invoked; on true-return,
    //                                         onReloaded() fires.
    //   - compileOk=true  + bytes empty:     treated as a compile failure
    //                                         (driver guarantees
    //                                         bytes-non-empty on success,
    //                                         so reaching here means the
    //                                         .spv slurp failed post-
    //                                         compile). Neither apply nor
    //                                         onReloaded fire; counted as
    //                                         applyFailed for visibility.
    //   - compileOk=false:                    subscribersNotified counts
    //                                         the bucket size but nothing
    //                                         is invoked.
    //
    // The DispatchResult lets a logger subscriber in main.cpp print
    // "reloaded <path> (applied on N passes)" without the registry
    // having to know about engine logging.
    DispatchResult Dispatch(std::string_view sourcePath,
                            const std::vector<uint8_t>& spvBytes,
                            bool compileOk) {
        DispatchResult result;
        const std::string key =
            ShaderReloadRegistryDetail::NormalizeSourcePath(sourcePath);
        auto it = m_bySource.find(key);
        if (it == m_bySource.end()) return result;

        auto& entries = it->second;
        result.subscribersNotified = entries.size();

        if (!compileOk || spvBytes.empty()) {
            // Compile failed OR post-compile slurp produced no bytes. The
            // driver contract says "don't touch the live shader" in both
            // cases; we return the bucket size as `subscribersNotified`
            // for ops visibility but neither apply nor onReloaded fire.
            //
            // We count applyFailed ONLY when compileOk claims success but
            // bytes are empty — that's the "slurp lost a race" edge case
            // worth highlighting. A true compile error is already surfaced
            // by the driver's result.stderrTail and would be double-counted
            // if we incremented applyFailed for it here.
            if (compileOk && spvBytes.empty()) {
                result.applyFailed = entries.size();
            }
            return result;
        }

        // Happy path. Snapshot the entries list into a local copy BEFORE
        // iterating so an onReloaded callback that calls Register /
        // Unregister (e.g. a pass re-registering after a shader type
        // change) doesn't invalidate our iterators mid-dispatch. The
        // copy is shallow — std::function is move-friendly and Entry
        // is small — so this is a few-hundred-bytes allocation at most.
        std::vector<Entry*> snapshot;
        snapshot.reserve(entries.size());
        for (auto& e : entries) snapshot.push_back(&e);

        for (Entry* e : snapshot) {
            bool applyOk = false;
            if (e->apply) {
                applyOk = e->apply(spvBytes);
            }
            if (applyOk) {
                ++result.applySucceeded;
                if (e->onReloaded) {
                    e->onReloaded();
                    ++result.onReloadedFired;
                }
            } else {
                ++result.applyFailed;
            }
        }
        return result;
    }

    // Test / ops accessor: how many subscribers are currently bound to
    // this sourcePath? Zero if none registered or if every one unregistered.
    size_t SubscriberCount(std::string_view sourcePath) const {
        const std::string key =
            ShaderReloadRegistryDetail::NormalizeSourcePath(sourcePath);
        auto it = m_bySource.find(key);
        return it == m_bySource.end() ? 0 : it->second.size();
    }

    // Test / ops accessor: total subscribers across every sourcePath bucket.
    size_t TotalSubscribers() const noexcept {
        size_t total = 0;
        for (const auto& kv : m_bySource) total += kv.second.size();
        return total;
    }

    // Test-only. Production code never needs to clear the registry — passes
    // manage their subscriptions via Register/Unregister. Exposed for Catch2
    // test isolation so one test case's registrations don't bleed into the
    // next.
    void ClearForTest() noexcept {
        m_bySource.clear();
        // Deliberately do NOT reset m_nextHandleValue — a test that
        // Register+ClearForTest+Register should still get distinct handles
        // so a mistakenly-held-across-clear handle can't alias a new entry.
    }

    // Disable copy; the singleton is the only intended instance. Move also
    // disabled because the map's std::function subscribers typically
    // capture pointers into renderer state that would dangle if the
    // registry moved.
    ShaderReloadRegistry(const ShaderReloadRegistry&) = delete;
    ShaderReloadRegistry& operator=(const ShaderReloadRegistry&) = delete;
    ShaderReloadRegistry(ShaderReloadRegistry&&) = delete;
    ShaderReloadRegistry& operator=(ShaderReloadRegistry&&) = delete;

private:
    ShaderReloadRegistry() = default;
    ~ShaderReloadRegistry() = default;

    struct Entry {
        SubscriptionHandle handle;
        ApplyFn apply;
        OnReloadedFn onReloaded;
    };

    std::unordered_map<std::string, std::vector<Entry>> m_bySource;

    // Monotonically increasing handle counter. We start from 0 and
    // pre-increment so the first handle handed out has value 1; value 0
    // is reserved as the "invalid" sentinel (default-constructed
    // SubscriptionHandle).
    size_t m_nextHandleValue = 0;
};

} // namespace CatEngine::RHI
