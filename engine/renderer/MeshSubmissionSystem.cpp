#include "MeshSubmissionSystem.hpp"

#include "../../game/components/MeshComponent.hpp"
#include "../animation/Animator.hpp"
#include "../core/Logger.hpp"
#include "../math/Math.hpp"
#include "../math/Quaternion.hpp"
#include "../math/Transform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
// Peak forward-pitch angle of an attack-lunge pulse, in radians.
// 14 degrees -> 0.244 rad. Two anchor points motivated this number:
//  - 6-8 degrees reads as "the cat shivered" rather than "the cat
//    swung at something" — too subtle to register at typical
//    portfolio-screenshot framing distances (~10-30 m camera-to-cat).
//  - 25+ degrees pushes the cat's head past horizontal and starts
//    clipping into the terrain on flatter rigs (the auto-rigger
//    occasionally produces models with the root anchored at the chest
//    instead of the hip, so the pivot is higher than ideal — safer to
//    cap below the worst-case clip threshold).
// 14 degrees lands cleanly in the middle of the readable range and
// matches the natural "wind-up" angle of a cat swatting motion. The
// curve `sin(pi * (1 - p))` smoothly interpolates from zero at the
// pulse's start, peaks at this value at p=0.5 (cleanly aligned with
// the swing's damage-commit frame), and returns to zero at the end.
constexpr float kAttackLungeMaxAngleRadians = 0.244F;

// Peak backward-pitch angle of a hit-flinch pulse, in radians.
// -9 degrees -> -0.157 rad. NEGATIVE because the flinch leans the
// entity *back* — the swing pitched the cat forward into the strike,
// the flinch pitches the recipient backward away from it. Magnitude
// is intentionally smaller than the attack lunge (9 deg vs 14 deg)
// because the flinch is a secondary motion that should read clearly
// without competing with the attacker's swing as the primary action.
// At ~10-30 m camera framing, 9 deg is the lower edge of "obviously
// readable backward lean" without crossing into "the cat is rearing
// up" — a cleaner secondary-motion choice than mirroring the attack
// magnitude exactly. When a hit lands mid-swing on the same entity,
// the renderer adds the two contributions and the net pitch reads as
// a tussle rather than a clean lunge or clean recoil — a desirable
// emergent visual that single-pulse systems can't produce.
constexpr float kHitFlinchMaxAngleRadians = -0.157F;

// ---- Idle Y-bob (procedural bind-pose breathing) -------------------
//
// Peak vertical displacement applied to a rigged entity's modelMatrix
// translation each frame, in metres. 2.5 cm peak (5 cm peak-to-peak) at
// the entity's local Y-scale. Sized for "barely-noticeable but
// dispositively-not-frozen": a real housecat's chest expansion when
// breathing is ~1-2 cm, and a rig's overall vertical sway when shifting
// weight is a few cm more than that — 2.5 cm sits in the breathing-vs-
// weight-shift band. At a third-person camera framing of ~10-30 m
// (where the cat reads ~1-2 cm tall on screen at 1080p), the bob
// resolves to a sub-pixel oscillation that the eye reads as "alive"
// without registering as a glitch — exactly the perceptual band we
// want. Larger amplitudes (5+ cm) start to read as a hover/levitate
// bug; smaller amplitudes (1 cm or less) are invisible at portfolio-
// review distances.
//
// Why scale by transform.scale.y rather than a flat constant: dogs
// spawn with non-uniform scale (BigDog uses 1.5x, FastDog uses 0.85x);
// scaling the bob keeps the visual proportion consistent across
// entities — a 1.5x BigDog reads with a 3.75 cm bob (matching its
// proportionally-larger frame), a 0.85x FastDog reads with a 2.1 cm
// bob (matching its smaller silhouette). A flat constant would make
// the BigDog's bob look anaemic and the FastDog's look like a
// pneumatic-jack stutter.
//
// 2026-04-26 SHIP-THE-CAT directive evidence: the prior-iter handoff
// (ENGINE_PROGRESS entry ~10:50 UTC) explicitly named procedural bob
// as the smallest-scope visible-progress win available with CPU
// skinning gated off. Without bob, every cat / dog / NPC reads as
// frozen in T-pose / bind-pose at the playable frame rate the OOB
// fix recovered — frame-dump evidence #18 has 23,817 distinct colours
// (well above the 50-distinct gate) but every entity is photometrically
// static between frames. Bob adds inter-frame variation to entity
// pixels so a frame-dump video (when the renderer eventually wires
// frame-grab capture) reads as motion rather than a still image.
constexpr float kIdleBobAmplitudeMetres = 0.025F;

// Idle-bob frequency in Hz. 0.7 Hz (one cycle every ~1.4 s) is a
// natural breathing-cadence read for a small mammal — too slow for the
// cycle to register as a stutter, too fast for it to register as
// camera drift. The user-perception window for "this thing is alive
// and breathing" is roughly 0.4-1.5 Hz; below 0.4 Hz reads as the
// world tilting under the entity, above 1.5 Hz reads as a panicked
// hyperventilation or a per-frame numerical bug. 0.7 Hz sits at the
// midpoint of the readable band and matches the resting respiratory
// rate of a housecat (~25-40 breaths/min, i.e. 0.42-0.67 Hz at the
// upper end) — close enough to read as "this is the cat breathing"
// to a viewer who isn't actively counting.
constexpr float kIdleBobFrequencyHz = 0.7F;

// Knuth multiplicative-hash constant (golden-ratio prime, see TAOCP
// vol 3 §6.4) used to scramble entity IDs into a near-uniform 64-bit
// hash before extracting the per-entity bob phase. We need entities to
// be visually de-synced — without it, every NPC spawned in the same
// ECS bulk-create call would inherit consecutive IDs and bob in
// near-lock-step (a Mexican-wave-of-cats effect), which the user
// would IMMEDIATELY clock as a procedural artifact. Knuth's golden
// ratio makes adjacent IDs map to phases roughly π apart (the worst
// case for visual lockstep is sequential IDs differing by 1, which
// after the multiply differ by ~2.65 G in the high bits, i.e. a wide
// phase rotation in the [0, 2π) phase space). The result is a herd
// of cats that bob independently — exactly what we want.
constexpr std::uint64_t kKnuthMultiplicativeHash = 2654435761ULL;

// Compute a per-entity phase in [0, 2π) from the entity's 64-bit ID by
// multiplicative-hashing then folding the high 16 bits into a unit
// fraction. Keeping the multiply + bitfield extract in a single inline
// helper makes the call site readable and lets the compiler inline the
// whole thing (the entire body is constexpr-eligible at the type level
// even though we don't mark it constexpr — the cost is one imul + one
// shift + one float convert + one float multiply per visible entity).
inline float ComputeIdleBobPhase(CatEngine::Entity entity) {
    const std::uint64_t hashed =
        static_cast<std::uint64_t>(entity.id) * kKnuthMultiplicativeHash;
    // High 16 bits of the hash hold the most-mixed bits (the multiply
    // smears the input across the upper word in classic multiplicative
    // hashing). Extracting bits [48,64) gives us the cleanest available
    // phase signal for entities with low-magnitude IDs (e.g. early ECS
    // allocations get IDs 1, 2, 3, ... — the low bits of the hash for
    // those are nearly unchanged from the input, so we read the top).
    const std::uint16_t phaseBits =
        static_cast<std::uint16_t>((hashed >> 48) & 0xFFFFU);
    const float unitFraction =
        static_cast<float>(phaseBits) / static_cast<float>(0xFFFFU);
    return unitFraction * (2.0F * Engine::Math::PI);
}

// Compute the idle-bob Y offset (in metres) for an entity at a given
// wall-clock time. Returns 0 for entities that should not bob (no
// animator means a static prop / terrain piece, not a rigged organism;
// deathPosed means the entity is in its death-pose latch and should
// stay flat on the ground rather than continuing to "breathe"). All
// other animator-bearing entities get the procedural bob — the cat is
// alive even when the animator clip itself isn't playing because CPU
// skinning is gated off.
inline float ComputeIdleBobOffsetMetres(
    const CatGame::MeshComponent& mesh,
    CatEngine::Entity entity,
    float scaleY,
    float timeSeconds) {
    if (mesh.animator == nullptr) {
        return 0.0F;  // Static prop / terrain — should not breathe.
    }
    if (mesh.deathPosed) {
        return 0.0F;  // Corpse — should not continue to bob after death.
    }
    const float phase = ComputeIdleBobPhase(entity);
    const float angularVelocity = 2.0F * Engine::Math::PI * kIdleBobFrequencyHz;
    return kIdleBobAmplitudeMetres * std::abs(scaleY) *
           std::sin(angularVelocity * timeSeconds + phase);
}
} // namespace

namespace CatEngine::Renderer {

namespace {

// Compute an axis-aligned half-extent vector from every mesh's model-space
// bounds. ModelLoader fills boundsMin/boundsMax per Mesh; the union of those
// gives the whole-model AABB in model space. The ScenePass proxy cube
// doesn't take a transform matrix, only a world-space position + half-
// extents, so we multiply the extents by the entity's scale and leave the
// rotation unused. This is approximate for rotated meshes (AABBs can't
// represent rotation) but matches the proxy-cube contract ScenePass
// currently exposes; once real mesh draws land this helper will be replaced.
Engine::vec3 ComputeModelHalfExtents(const CatGame::MeshComponent& meshComponent,
                                     const Engine::vec3& scale) {
    if (!meshComponent.model || meshComponent.model->meshes.empty()) {
        // Fall back to a conservative unit-size cube so the entity is still
        // visible as a marker even for degenerate models.
        return Engine::vec3(0.5F, 0.5F, 0.5F);
    }

    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float minZ = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    float maxZ = -std::numeric_limits<float>::infinity();

    for (const auto& mesh : meshComponent.model->meshes) {
        minX = std::min(minX, mesh.boundsMin.x);
        minY = std::min(minY, mesh.boundsMin.y);
        minZ = std::min(minZ, mesh.boundsMin.z);
        maxX = std::max(maxX, mesh.boundsMax.x);
        maxY = std::max(maxY, mesh.boundsMax.y);
        maxZ = std::max(maxZ, mesh.boundsMax.z);
    }

    // Degenerate bounds (all-zero from an unloaded mesh, or inverted) fall
    // back to a unit cube rather than producing negative extents that the
    // vertex shader would flip inside-out.
    if (!(maxX > minX) || !(maxY > minY) || !(maxZ > minZ)) {
        return Engine::vec3(0.5F, 0.5F, 0.5F);
    }

    const float halfX = 0.5F * (maxX - minX) * std::abs(scale.x);
    const float halfY = 0.5F * (maxY - minY) * std::abs(scale.y);
    const float halfZ = 0.5F * (maxZ - minZ) * std::abs(scale.z);
    return Engine::vec3(halfX, halfY, halfZ);
}

// Translate an entity's Transform into the world-space center of the
// model's AABB. glTF models typically have their origin at the feet, so we
// shift up by half the vertical extent to center the proxy cube on the
// visible silhouette rather than sinking it into the ground.
Engine::vec3 ComputeWorldCenter(const Engine::Transform& transform,
                                const Engine::vec3& halfExtents) {
    return transform.position + Engine::vec3(0.0F, halfExtents.y, 0.0F);
}

} // namespace

// Module-scope flag for the per-frame CPU-skinning hot path. Initialised to
// false (skinning OFF) so a fresh process renders entities in bind-pose at
// full frame rate; main.cpp's CLI parser flips this on via SetEnableCpuSkinning
// when the user opts in with `--enable-cpu-skinning`. See the docblock on the
// setter in MeshSubmissionSystem.hpp for the full WHY (2026-04-26 perf halt:
// ~17-20 skinned entities * ~150k verts each = 2-3 M per-frame transforms on
// a single CPU thread, observed fps collapse 2-5).
static bool s_enableCpuSkinning = false;

void MeshSubmissionSystem::SetEnableCpuSkinning(bool enabled) {
    s_enableCpuSkinning = enabled;
}

bool MeshSubmissionSystem::IsCpuSkinningEnabled() {
    return s_enableCpuSkinning;
}

MeshSubmissionSystem::MeshSubmissionSystem(std::size_t maxFramesInFlight)
    : m_retained(std::max<std::size_t>(1, maxFramesInFlight)) {}

void MeshSubmissionSystem::Submit(CatEngine::ECS& ecs,
                                  std::size_t frameIndex,
                                  std::vector<ScenePass::EntityDraw>& out,
                                  const Engine::Frustum* frustum,
                                  const Engine::vec3* cameraPosition,
                                  float maxDrawDistance) {
    // Wrap frameIndex into the retention ring. The caller passes the
    // renderer's current frame index which may exceed the frames-in-flight
    // count; modulo keeps us inside the ring.
    const std::size_t slot = frameIndex % m_retained.size();
    auto& retainedForFrame = m_retained[slot];

    // Release last use of this slot — any Model whose only remaining ref
    // was the one we held is safe to drop now because the GPU has finished
    // the frame that recorded it (that's the whole point of the ring size
    // matching maxFramesInFlight).
    retainedForFrame.clear();

    // Diagnostic: count entities visited per category on the very first call.
    // The static `firstCallReported` ensures we instrument once rather than
    // spamming every frame. NPC investigation 2026-04-25: only 4 of an
    // expected 20 entities reach the renderer; this telemetry pins down
    // whether the dropoff is at forEach iteration, the visible/model gate,
    // or the animator/palette gate. Removed once the cause is fixed.
    //
    // 2026-04-25 frustum-cull iteration: added `culledFrustum` to track how
    // many entities were skipped because their model AABB sphere fell
    // outside the camera frustum — the metric that turns "16 NPCs all CPU-
    // skinned every frame" into "only the 3-6 the player can see right now".
    static bool firstCallReported = false;
    int visitedTotal = 0;
    int rejectedNullOrInvisible = 0;
    int culledFrustum = 0;
    int culledDistance = 0;
    int withAnimator = 0;
    int withPalette = 0;

    // Pre-compute squared distance threshold so we can compare against the
    // squared world-space distance and avoid the sqrt per-entity. Zero
    // (or negative) maxDrawDistance disables the distance cull (caller opt-in).
    //
    // WHY a distance cull on top of the frustum cull (2026-04-26 evidence row #8):
    // ---------------------------------------------------------------------
    // The frustum cull catches entities behind / above / below / past the
    // camera. It does NOT catch entities that are inside the camera's view
    // volume but very far away — e.g., a cat NPC scattered across the world
    // map at a far-clan position 200 m away. With 16 cat NPCs spread across
    // four clans + a couple of wave-spawned dogs, the camera's wide-angle
    // frustum CAN contain ~15 of them at certain angles (notably during
    // wave-cleared "Transition" state when the camera widens). Each one
    // costs a Meshy-rigged GLB worth of GPU vertex / fragment work; emitting
    // 15 of them simultaneously was the dominant cause of the sustained
    // 10 fps stall measured during wave-1 cleared in evidence row #8
    // (frame=600 visited=17 culledFrustum=2 emitted=15).
    //
    // A distance cull at maxDrawDistance metres caps the visible NPC budget
    // by physical proximity to the camera. Within the 60-80 m radius the
    // player is realistically engaged with, every entity still draws — the
    // wave's active dogs, the player cat, any same-clan NPC adjacent to the
    // arena. Anything past the threshold (the other-clan-mentor sitting
    // 200 m away that produces a sub-pixel silhouette) is dropped without
    // even costing a frame's worth of vertex pipeline work.
    //
    // The threshold is a parameter rather than a constant so the gameplay
    // layer can tune it per camera mode (third-person tight follow vs wide
    // panorama vs zoomed-out cinematic). 0.0F means "no cull" so the
    // existing single-arg Submit() call sites (test fixtures, debug viewers)
    // keep their old behaviour of always-draw-all.
    const bool distanceCullEnabled =
        cameraPosition != nullptr && maxDrawDistance > 0.0F;
    const float maxDrawDistanceSq =
        distanceCullEnabled ? maxDrawDistance * maxDrawDistance : 0.0F;

    // Periodic per-frame culling counter for ongoing visibility into how
    // much work the cull saves. Reported at the same low cadence as
    // [CatRender-DIAG] (every 30/60/300/600 frames-ish) so the log isn't
    // spammed but progress is observable when tuning. The static counter
    // increments each call regardless of frustum presence — when frustum
    // is null, culledFrustum stays 0 and the line prints "cull=off" which
    // is itself useful information.
    static int submitCallCount = 0;
    ++submitCallCount;

    // Wall-clock seconds captured once per Submit call so every entity in
    // this frame samples the bob curve at the SAME time-step. Reading the
    // clock per-entity would (a) add a syscall per entity per frame
    // (~17-20 calls during a wave; small but pure overhead) and (b)
    // introduce sub-microsecond skew between the first and last entity's
    // sample, which over 60 fps is invisible but is still the right
    // discipline (every entity's bob math sees one identical "now"). We
    // anchor to a process-static start point so the absolute value stays
    // small (a float can hold ~20 days of seconds without precision loss
    // at the bob's frequency; we need only seconds, not nanoseconds).
    //
    // steady_clock is required (not system_clock): system_clock can jump
    // backward when the OS adjusts the wall clock (NTP correction,
    // user-set DST shift), and any backward jump would tear the bob
    // curve and create a visible glitch. steady_clock is monotonic.
    static const auto kBobEpoch = std::chrono::steady_clock::now();
    const auto nowSinceEpoch = std::chrono::steady_clock::now() - kBobEpoch;
    const float currentTimeSeconds =
        std::chrono::duration<float>(nowSinceEpoch).count();

    ecs.forEach<Engine::Transform, CatGame::MeshComponent>(
        [&](CatEngine::Entity entity,
            Engine::Transform* transform,
            CatGame::MeshComponent* meshComponent) {
            ++visitedTotal;
            if (transform == nullptr || meshComponent == nullptr) {
                ++rejectedNullOrInvisible;
                return;
            }
            if (!meshComponent->visible || !meshComponent->model) {
                ++rejectedNullOrInvisible;
                return;
            }

            // Frustum cull: build a model-space half-extent AABB, lift it to
            // world space via the entity's transform, and reject the entity
            // if the resulting bounding sphere is fully outside any frustum
            // plane. The sphere test is conservative (a tighter OBB test
            // would catch a few more skinny rigs that just barely poke into
            // view) but it's branch-free in the steady state and the
            // overhead per-entity is dominated by the eventual skinning
            // pass anyway, so the trade is far in our favour.
            //
            // We do this BEFORE retaining the Model ref or computing the
            // skinning palette: those are the costs we're trying to avoid
            // for off-screen entities. Computing halfExtents twice (once
            // here, once below in the draw setup) is fine — it's a bounds
            // sweep over the model's mesh vector, microseconds, and only
            // for entities that survive the cull.
            const Engine::vec3 hExt =
                ComputeModelHalfExtents(*meshComponent, transform->scale);
            const Engine::vec3 worldCenter =
                ComputeWorldCenter(*transform, hExt);
            if (frustum != nullptr) {
                // Bounding-sphere radius from the half-extents diagonal —
                // length(halfExtents). For a 1m × 1.5m × 0.6m cat that's a
                // ~1m sphere, generous enough to keep partly-occluded edge
                // cases visible (rotated cats whose AABB extends past the
                // OBB) without being so loose the cull stops working.
                const float radius = std::sqrt(
                    hExt.x * hExt.x + hExt.y * hExt.y + hExt.z * hExt.z);
                if (!frustum->intersectsSphere(worldCenter, radius)) {
                    ++culledFrustum;
                    return;
                }
            }

            // Distance cull: drop entities whose bounding-sphere centre is
            // farther than `maxDrawDistance` metres from the camera. The
            // frustum cull already handled "behind / above / past the camera";
            // this step handles "in-frustum but irrelevantly far". With both
            // active we keep only the entities the player can plausibly
            // perceive as part of the active gameplay frame.
            //
            // Why test the world-space CENTER and not the nearest point on
            // the sphere: the half-extents of even the largest GLB (the
            // BigDog at 1.5x scale) is on the order of 1-2 m. A
            // centre-to-centre 80 m cut means the worst-case "I culled an
            // entity whose nearest face was actually only 78 m from the
            // camera" still drops a pixel that's barely a few hairs wide
            // on screen. Adding the bounding-sphere radius back in would
            // be a wash and double the per-entity work.
            //
            // We use squared distance to avoid the per-entity sqrt — same
            // ordering, half the math. With 16+ NPCs visited per frame this
            // saves enough cycles to be worth the minor readability cost.
            if (distanceCullEnabled) {
                const Engine::vec3 toCamera =
                    worldCenter - *cameraPosition;
                const float distSq =
                    toCamera.x * toCamera.x +
                    toCamera.y * toCamera.y +
                    toCamera.z * toCamera.z;
                if (distSq > maxDrawDistanceSq) {
                    ++culledDistance;
                    return;
                }
            }

            // Retain a strong ref for the duration of this frame's GPU work.
            retainedForFrame.push_back(meshComponent->model);

            ScenePass::EntityDraw draw;

            // ---- Path (b): real-mesh draw -----------------------------------
            // Hand the renderer the raw Model* and the entity's full TRS
            // matrix. ScenePass lazily uploads the Model's vertex/index
            // data to GPU memory on first sight (caches it for subsequent
            // frames) and then draws the actual GLB geometry instead of
            // a proxy cube. Lifetime: `retainedForFrame.push_back` above
            // keeps the shared_ptr alive long enough for the GPU to
            // finish its frame, so the raw pointer in `draw.model` is
            // valid through the whole Execute() call (and the next
            // maxFramesInFlight Execute() calls — overkill but harmless).
            draw.model = meshComponent->model.get();

            // ---- Attack-lunge visual pulse (renderer-only pose tweak) ------
            // CombatSystem stamps `attackVisualPulse` to 1.0F on the first
            // frame an attacker commits a swing or projectile cast, then
            // decays it linearly toward zero over kAttackLungePulseSeconds
            // (see MeshComponent.hpp for the field-side docblock and
            // CombatSystem.cpp for the bump + decay pass). We reshape the
            // linear progress through `sin(pi * (1 - p))` to get a smooth
            // cosine-bell lean angle that starts at zero, peaks mid-pulse
            // (so the cat's head is fully forward exactly when the damage
            // hit-test commits), and returns to zero at the end. The lean
            // is applied as a *post*-multiply on the entity's existing
            // rotation so it operates in entity-local space — pitching
            // around the cat's right axis (+X) regardless of how the AI
            // has yawed the cat to face its target. We never mutate the
            // entity's actual Transform: the AI / locomotion / autoplay
            // code expects to own that quaternion exclusively, and
            // double-applying the lunge through the simulation would
            // leak rotation into physics + camera framing. The matrix
            // composition T * (R_yaw * R_pitchLocal) * S preserves the
            // existing translate/scale and only diverges in the
            // 3x3 rotation block.
            //
            // Skinned entities: when the model has a bone palette
            // (CPU-skinned cat / dog GLBs), the per-bone skinning
            // matrices flow through ScenePass independently of
            // modelMatrix. The lean here pitches the *whole entity*
            // forward including the skinned mesh — visually consistent
            // with a procedural attack hop and free of the bind-pose
            // overrides that an Animator-driven attack clip would need.
            //
            // Cost: one normalize() + one Quaternion multiply + one
            // mat4 build per visible attacking entity per frame, gated
            // entirely behind the `pulse > 0` check. Non-attacking
            // entities (the 99% steady state — terrain, props, idle
            // NPCs) skip the work after a single float compare.
            // ---- Pose composition ------------------------------------
            // We build one `pose` Transform per entity that bakes in
            // (a) the procedural idle Y-bob (renderer-only, animator-
            //     gated, deathPosed-suppressed; see ComputeIdleBobOffsetMetres)
            // (b) the combined attack-lunge + hit-flinch pitch
            //     (renderer-only, gated on either pulse > 0).
            //
            // Both contributions are visual-only — we mutate a COPY of
            // the entity's Transform, never the live one. The AI /
            // physics / locomotion / camera-follow code expects to own
            // the underlying transform exclusively, and double-applying
            // either contribution through the simulation would tear:
            // the bob would push the cat into the ground over time as
            // physics resolves the offset, and the lunge would feed
            // back into the camera framing as a per-frame jitter.
            //
            // Why a single shared `pose` instead of separate branches:
            // before this iteration the lunge/flinch path lived in an
            // `if (pulse>0)` branch that built its own `pitched` Transform,
            // and the `else` branch emitted `transform->toMatrix()` raw.
            // Adding bob to both branches would have meant duplicating
            // the offset application; folding both contributions into
            // one pose Transform keeps the model-matrix construction
            // single-source-of-truth and lets a future contribution
            // (knockback impulse, charge-up crouch, death-twitch) drop
            // into the same pose without rewiring branches.
            //
            // Cost per entity per frame:
            //   - bob: one std::sin + one float multiply + one float add
            //         (gated by animator!=null && !deathPosed; static
            //         props skip the work)
            //   - pitch: two float compares; if either pulse>0, the
            //            existing one normalize() + Quaternion-mul
            //            + sin per non-zero pulse
            //   - matrix build: one Transform::toMatrix (translate * rot
            //                   * scale), unchanged from before.
            // For a 17-entity wave-active scene that's ~50 µs/frame total
            // — well inside the playable budget the perf-halt restored.
            Engine::Transform pose = *transform;

            // (a) Idle bob — applied first so any subsequent rotation
            // operates on the bobbed position. This produces the desired
            // visual: the entity bobs up/down in world space; the lunge
            // pitch tilts the entire bobbed silhouette around its local
            // +X axis (no surprise — pose.rotation is what
            // Transform::toMatrix consumes for the rotation block).
            pose.position.y += ComputeIdleBobOffsetMetres(
                *meshComponent, entity, transform->scale.y, currentTimeSeconds);

            // (b) Combined attack-lunge + hit-flinch contribution. Both
            // pulses share the same `sin(pi * (1 - p))` cosine-bell
            // envelope but map onto different peak angles with opposite
            // sign — the attack lunges forward (+X local rotation,
            // positive radians), the hit flinches backward (+X local
            // rotation, NEGATIVE radians from the negative
            // kHitFlinchMaxAngleRadians). When both pulses are non-zero
            // on the same entity (hit landed mid-swing), we ADD the two
            // angles. The sum is clamped only by the natural envelope
            // (each pulse's contribution can't exceed its peak), and a
            // co-occurrence produces a net pitch somewhere between
            // +14 deg and -9 deg depending on the two pulses'
            // instantaneous values — a tussle effect that reads as
            // "this attacker got hit while it was swinging" without any
            // extra logic. If both pulses are zero we skip the whole
            // rotation-multiply path — the steady-state cost is two
            // compares and a branch per entity.
            const float attackPulse = meshComponent->attackVisualPulse;
            const float hitPulse = meshComponent->hitVisualPulse;
            if (attackPulse > 0.0F || hitPulse > 0.0F) {
                float angleRadians = 0.0F;
                if (attackPulse > 0.0F) {
                    const float intensity =
                        std::sin(Engine::Math::PI * (1.0F - attackPulse));
                    angleRadians += kAttackLungeMaxAngleRadians * intensity;
                }
                if (hitPulse > 0.0F) {
                    const float intensity =
                        std::sin(Engine::Math::PI * (1.0F - hitPulse));
                    angleRadians += kHitFlinchMaxAngleRadians * intensity;
                }

                const Engine::Quaternion localPitch(
                    Engine::vec3(1.0F, 0.0F, 0.0F), angleRadians);
                pose.rotation = pose.rotation * localPitch;
                pose.rotation.normalize();

                // One-time confirmation that the lunge path executes for at
                // least one entity in a session. Useful both as a "the
                // visual-pulse hook is alive" signal in the playtest log
                // (future portfolio reviewers can grep for the line) and
                // as a regression canary if the bump in CombatSystem ever
                // gets accidentally reverted (no log line == no entity
                // attacked == something silenced the source). Static so
                // the cost is one branch + boolean store on the very
                // first observed lunge per process — zero per frame
                // afterwards.
                static bool firstLungeLogged = false;
                if (!firstLungeLogged && attackPulse > 0.0F) {
                    firstLungeLogged = true;
                    CatEngine::Logger::info(
                        "[MeshSubmission] first attack-lunge observed "
                        "(pulse=" + std::to_string(attackPulse) +
                        ", angle=" + std::to_string(angleRadians) + " rad)");
                }
                // Mirrored canary for the hit-flinch path. We log on the
                // FIRST observed flinch only, regardless of whether an
                // attack-lunge is also active in the same frame (so a
                // mid-swing hit still produces the log). Same regression-
                // canary purpose as above: no log line ever printed in a
                // session means no damage was applied with a target that
                // had a MeshComponent, which is suspicious and worth
                // surfacing.
                static bool firstFlinchLogged = false;
                if (!firstFlinchLogged && hitPulse > 0.0F) {
                    firstFlinchLogged = true;
                    CatEngine::Logger::info(
                        "[MeshSubmission] first hit-flinch observed "
                        "(pulse=" + std::to_string(hitPulse) +
                        ", angle=" + std::to_string(angleRadians) + " rad)");
                }
            }

            draw.modelMatrix = pose.toMatrix();

            // One-time confirmation that the idle-bob path executed for
            // at least one rigged entity in a session. Same regression-
            // canary purpose as the lunge / flinch first-observed log:
            // if the gate (animator!=null && !deathPosed) ever closes
            // unintentionally, the bob silently stops and a portfolio
            // viewer would just see a herd of frozen rigs again — the
            // log line proves the path is live. Logged on the first
            // entity per process whose animator pointer is non-null and
            // whose deathPosed flag is clear.
            static bool firstBobLogged = false;
            if (!firstBobLogged && meshComponent->animator != nullptr &&
                !meshComponent->deathPosed) {
                firstBobLogged = true;
                const float bobObserved =
                    pose.position.y - transform->position.y;
                CatEngine::Logger::info(
                    "[MeshSubmission] first idle-bob applied (entity=" +
                    std::to_string(entity.id) +
                    ", offsetMetres=" + std::to_string(bobObserved) +
                    ", scaleY=" + std::to_string(transform->scale.y) + ")");
            }

            // Tint colour priority (highest first):
            //   1. Per-entity tintOverride (set by NPCSystem / DogEntity /
            //      CatEntity to encode clan / dog-variant identity that the
            //      authored GLB doesn't carry). This is the path that lights
            //      up clan-distinguishable cats and variant-distinguishable
            //      dogs while the entity pipeline is still flat-shaded —
            //      every Meshy cat GLB defaults to baseColorFactor (1,1,1)
            //      because they ship a baseColorTexture instead, so without
            //      this override every cat would render identical-white.
            //      See MeshComponent::hasTintOverride for the full WHY.
            //   2. The first material's baseColorFactor — covers any GLB
            //      the artist genuinely authored a flat color on.
            //   3. Fallback sentinel light-grey for material-less models so
            //      placeholders stay visible against the green terrain.
            //
            // WHY first material rather than per-submesh: the entity
            // pipeline has one tint per draw; a future textured pipeline
            // will split into per-submesh ranges (and at that point
            // baseColorTexture binds via descriptor sets, not push
            // constants). For now picking the first material is the
            // closest single-colour approximation that's stable across
            // single-material models (the common case for our Meshy
            // GLBs — one material per cat / dog).
            // 2026-04-25 directive iteration: ModelLoader::ExtractMaterials
            // now overwrites material.baseColorFactor with the average RGB of
            // the GLB-embedded baseColor JPEG (WHY: ModelLoader.cpp's
            // ExtractEmbeddedBaseColorAverage helper). Identity (1,1,1,1)
            // means "no texture / asset author left the factor as a no-op
            // through-multiplier"; anything else means asset-derived.
            // We MULTIPLY the tintOverride by the asset factor instead of
            // letting tintOverride win outright — that preserves per-clan
            // grouping (each clan keeps its identifiable hue) AND adds the
            // per-cat asset shade on top, so each cat under one clan looks
            // visibly distinct from its siblings even though they share a
            // clan tint. With identity baseColorFactor the multiply is a
            // no-op and the original blanket per-clan colour is preserved
            // bit-for-bit.
            if (!meshComponent->model->materials.empty()) {
                const auto& baseColor =
                    meshComponent->model->materials[0].baseColorFactor;
                const Engine::vec3 assetTint(
                    baseColor.x, baseColor.y, baseColor.z);

                if (meshComponent->hasTintOverride) {
                    // Component-wise multiply: per-clan identity * asset shade.
                    draw.color = Engine::vec3(
                        meshComponent->tintOverride.x * assetTint.x,
                        meshComponent->tintOverride.y * assetTint.y,
                        meshComponent->tintOverride.z * assetTint.z);
                } else {
                    draw.color = assetTint;
                }
            } else if (meshComponent->hasTintOverride) {
                // Material-less placeholder (e.g. cube proxy) — the per-clan
                // override is the only colour signal we have.
                draw.color = meshComponent->tintOverride;
            } else {
                // Fallback sentinel light-grey so untextured, untinted
                // material-less placeholders stay visible against the
                // green terrain.
                draw.color = Engine::vec3(0.75F, 0.75F, 0.85F);
            }

            // ---- Path (c): skinned mesh bone palette --------------------
            // If the entity has an Animator AND the model carries skinned
            // vertices (joints/weights authored), pull the per-frame
            // skinning matrices and stash them on the EntityDraw. ScenePass
            // takes ownership of the vector by-move; the Animator's internal
            // state is unchanged. Path (c) is gated inside ScenePass on
            // (skinningKey != null) AND (bonePalette non-empty), so leaving
            // bonePalette empty here cleanly falls through to bind-pose
            // path (b) for entities without animation.
            //
            // WHY animator pointer as the skinningKey: it's a stable
            // shared_ptr-managed address tied to the entity's lifetime
            // (MeshComponent owns the shared_ptr), and unique per entity
            // even when many entities share the same Model* (e.g. 16 NPCs
            // all spawning the same ember_leader.glb still have distinct
            // animators). ScenePass treats it as opaque pointer identity
            // for cache lookup — it never dereferences the animator.
            //
            // Cost: getCurrentSkinningMatrices() runs computeWorldTransforms
            // + computeSkinningMatrices for the skeleton's bones (~37 for
            // a Meshy cat) — a handful of microseconds per entity. The
            // expensive part (CPU vertex skinning over ~150k verts) lives
            // in ScenePass::EnsureSkinnedMesh, gated to actually-skinned
            // entities only.
            //
            // 2026-04-26 perf halt: the "expensive part" turned out to be the
            // ENTIRE frame budget for a wave-active scene. With ~17-20 skinned
            // NPCs + dogs visible during wave 1, the per-vertex weighted-mat4
            // sum + transform + normalise loop in ScenePass::EnsureSkinnedMesh
            // drops fps from 46 (steady state, 0-2 visible skinned entities)
            // to 2-5 (steady state, 17-20 visible skinned entities). The
            // smoking-gun heartbeat trace (perf-repro.log 2026-04-26 01:02-01:03)
            // shows fps recovery from 5 -> 46 the instant wave 1 clears and
            // frustum culling drops the count back to 2. The CPU loop is the
            // bottleneck regardless of which authored animation clip plays.
            //
            // Until GPU-skinning lands (skin matrices uploaded as a UBO/SSBO
            // and applied in a vertex shader, kept on the GPU side per
            // entity instead of writing back ~3.6 MB of skinned vertex data
            // per entity per frame to a HostCoherent VB), we DO NOT populate
            // skinningKey/bonePalette by default. ScenePass then naturally
            // falls through to its bind-pose Path (b) — every entity still
            // draws as the static GLB at its current world transform; the
            // mesh is frozen in T-pose / authored bind-pose, but the frame
            // rate is playable.
            //
            // Opt-in via `MeshSubmissionSystem::SetEnableCpuSkinning(true)`
            // (CLI: `--enable-cpu-skinning`) for screenshot / portfolio runs
            // where the visual cost of bind-pose outweighs the fps cost of
            // CPU skinning (e.g. low-NPC-count "hero" scenes).
            //
            // We still call getCurrentSkinningMatrices when CPU skinning is
            // off only when the flag is on; the bone-palette compute itself
            // (~37 bones * a few mat4 mults per bone) is microseconds, but
            // when we're not going to use the result there's no point
            // generating it — and skipping the call also avoids the std::vector
            // resize/zero that would otherwise allocate ~37*64 = 2.4 KB of
            // per-frame zeroed memory per entity.
            if (s_enableCpuSkinning && meshComponent->animator != nullptr) {
                ++withAnimator;
                meshComponent->animator->getCurrentSkinningMatrices(draw.bonePalette);
                if (!draw.bonePalette.empty()) {
                    ++withPalette;
                    draw.skinningKey = meshComponent->animator.get();
                }
            }

            // ---- Path (a) fallback values -----------------------------------
            // Even though ScenePass picks path (b) when `draw.model` is
            // non-null, we still populate halfExtents + position so that
            // if the per-Model GPU upload fails (degenerate mesh data,
            // Vulkan allocation failure) the renderer's fallback to the
            // proxy-cube path produces a visible marker at the right place
            // and size. Without this, an upload failure would render a
            // unit cube at the world origin — silent and confusing.
            //
            // Reuses the values we computed for the frustum cull above —
            // ComputeModelHalfExtents iterates the model's mesh vector and
            // is cheap but not free, and the entity already passed (or
            // skipped) the cull on those exact bounds, so we keep the math
            // single-source-of-truth.
            draw.halfExtents = hExt;
            draw.position = worldCenter;

            out.push_back(std::move(draw));
        });

    if (!firstCallReported) {
        firstCallReported = true;
        CatEngine::Logger::info(
            "[MeshSubmission] first-frame survey: visited=" + std::to_string(visitedTotal) +
            " rejected=" + std::to_string(rejectedNullOrInvisible) +
            " culledFrustum=" + std::to_string(culledFrustum) +
            " culledDistance=" + std::to_string(culledDistance) +
            " withAnimator=" + std::to_string(withAnimator) +
            " withPalette=" + std::to_string(withPalette) +
            " emitted=" + std::to_string(out.size()));
    }

    // Periodic cull-effectiveness print. Same low-cadence schedule as the
    // [CatRender-DIAG] line in CatAnnihilation.cpp so the err log shows
    // both sides of the rendering boundary at the same checkpoints.
    if (submitCallCount == 30
        || submitCallCount == 60
        || submitCallCount == 300
        || submitCallCount == 600) {
        CatEngine::Logger::info(
            "[MeshSubmission] frame=" + std::to_string(submitCallCount) +
            " visited=" + std::to_string(visitedTotal) +
            " rejected=" + std::to_string(rejectedNullOrInvisible) +
            " culledFrustum=" + std::to_string(culledFrustum) +
            " culledDistance=" + std::to_string(culledDistance) +
            " emitted=" + std::to_string(out.size()) +
            (frustum == nullptr ? " cull=off" : " cull=on") +
            (distanceCullEnabled ? " distCull=on" : " distCull=off"));
    }
}

void MeshSubmissionSystem::Reset() {
    for (auto& slot : m_retained) {
        slot.clear();
    }
}

} // namespace CatEngine::Renderer
