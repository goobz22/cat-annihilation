/**
 * Mock CUDA::ParticleSystem — stub implementations for the no-GPU test build.
 *
 * WHY this file exists
 * --------------------
 * The real `CatEngine::CUDA::ParticleSystem::{addEmitter, removeEmitter,
 * updateEmitter, getEmitter}` live in `engine/cuda/particles/ParticleSystem.cu`.
 * The CI-safe test build (see tests/CMakeLists.txt: `USE_MOCK_GPU=1`,
 * `TESTING_MODE=1`, no CUDA link) deliberately skips every `.cu` file so
 * contributors can iterate on game-system logic on machines without CUDA
 * Toolkit or without a dev command prompt.
 *
 * However, `game/systems/elemental_magic.cpp` keeps a
 * `std::shared_ptr<CatEngine::CUDA::ParticleSystem>` member and calls the four
 * emitter methods above. Those references survive to the link stage, and
 * without a translation unit defining them the linker fails with LNK2019
 * (`integration_tests.exe`, `unit_tests.exe`).
 *
 * The currently-enabled tests (`test_leveling_system`, `test_story_mode`) do
 * not exercise ElementalMagicSystem, so empty stubs are sufficient — they
 * satisfy the linker without pretending to simulate GPU particle state that
 * nothing in the mock-GPU suite can observe. If a future unit test is added
 * that actually exercises the elemental-magic particle path, replace these
 * stubs with a proper fake that tracks emitter lifecycle.
 *
 * We deliberately define these in the ORIGINAL `CatEngine::CUDA` namespace
 * (not a mock namespace): the linker looks for the mangled symbol of the
 * real class, and the only way to provide that symbol is to define the
 * methods on the real class. This is the standard "link-time mock" pattern
 * and is safe because `engine/cuda/particles/ParticleSystem.cu` is NOT in
 * the test translation set — so there is no ODR conflict.
 */

#include "../../engine/cuda/particles/ParticleSystem.hpp"

namespace CatEngine {
namespace CUDA {

uint32_t ParticleSystem::addEmitter(const ParticleEmitter& /*emitter*/) {
    // Return a non-zero id so callers that treat 0 as "invalid" don't trip.
    // Real GPU build hands out monotonically-increasing ids; the mock keeps
    // it constant because no test reads the value back.
    return 1;
}

void ParticleSystem::removeEmitter(uint32_t /*emitterId*/) {
    // No-op: the test build has no GPU-side emitter table to mutate.
}

void ParticleSystem::updateEmitter(uint32_t /*emitterId*/, const ParticleEmitter& /*emitter*/) {
    // No-op for the same reason as removeEmitter.
}

ParticleEmitter* ParticleSystem::getEmitter(uint32_t /*emitterId*/) {
    // Returning nullptr matches the real API's contract ("not found") and
    // forces any test that relies on emitter state to be added together with
    // a proper fake — it won't silently succeed against a stale stub.
    return nullptr;
}

void ParticleSystem::triggerBurst(uint32_t /*emitterId*/) {
    // No-op in the mock build — same rationale as removeEmitter/updateEmitter:
    // the test linker needs the symbol to exist, but the test suite has no
    // GPU-side emitter table for the burst flag to flip. If a future test
    // exercises burst-vs-continuous emission logic (e.g. counting how many
    // particles are spawned during a cast-pop) this stub must graduate to a
    // proper fake that records invocations on a side table.
}

} // namespace CUDA
} // namespace CatEngine
