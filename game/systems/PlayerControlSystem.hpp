#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/core/Input.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/Transform.hpp"

namespace CatGame {

class Terrain;


/**
 * PlayerControlSystem - Handles player input and controls
 *
 * Responsibilities:
 * - Process WASD movement input
 * - Handle mouse look for camera rotation
 * - Third-person camera management
 * - Jump input handling
 * - Attack input handling
 * - Smooth camera follow
 *
 * The system queries for the player entity and updates its movement
 * and combat components based on player input.
 */
class PlayerControlSystem : public CatEngine::System {
public:
    /**
     * Construct player control system
     * @param input Reference to the input system
     * @param priority System execution priority
     */
    explicit PlayerControlSystem(Engine::Input* input, int priority = 0);

    /**
     * Initialize the system
     * @param ecs Pointer to ECS instance
     */
    void init(CatEngine::ECS* ecs) override;

    /**
     * Update player controls each frame
     * @param dt Delta time in seconds
     */
    void update(float dt) override;

    /**
     * Shutdown the system
     */
    void shutdown() override;

    /**
     * Get system name
     */
    const char* getName() const override { return "PlayerControlSystem"; }

    /**
     * Set the player entity to control
     * @param entity The player entity
     */
    void setPlayerEntity(CatEngine::Entity entity);

    /**
     * Get the player entity
     * @return Player entity
     */
    CatEngine::Entity getPlayerEntity() const { return playerEntity_; }

    /**
     * Get camera position
     * @return Camera position in world space
     */
    Engine::vec3 getCameraPosition() const { return cameraPosition_; }

    /**
     * Get camera forward direction
     * @return Camera forward direction
     */
    Engine::vec3 getCameraForward() const { return cameraForward_; }

    /**
     * Get camera transform
     * @return Camera transform
     */
    Engine::Transform getCameraTransform() const;

    /**
     * Set camera offset from player
     * @param offset Camera offset in local space
     */
    void setCameraOffset(const Engine::vec3& offset);

    /**
     * Set mouse sensitivity
     * @param sensitivity Mouse sensitivity multiplier
     */
    void setMouseSensitivity(float sensitivity);

    /**
     * Enable or disable player control
     * @param enabled If true, player can control the entity
     */
    void setControlEnabled(bool enabled);

    /**
     * Set combat system reference for blocking/dodging
     * @param combatSystem Pointer to combat system
     */
    void setCombatSystem(class CombatSystem* combatSystem);

    /**
     * Set elemental magic system reference so autoplay can cast projectile
     * spells (currently water_bolt) alongside melee attacks. The pointer is
     * non-owning and nullable — when it's null, autoplay silently falls back
     * to melee-only behaviour, which matches the pre-wiring default and
     * keeps tests that construct PlayerControlSystem in isolation green.
     *
     * Motivation: the CUDA ribbon-trail renderer (ScenePass iteration 3d)
     * needs live particles to produce a visible trail. Until this wiring
     * landed, the AI cat only melee'd, so ParticleSystem.getRenderData().count
     * was always zero and the ribbon pre-flight log could never confirm
     * live>0. Injecting an occasional spell cast here gives the renderer
     * real VFX geometry to display without forcing the human player down
     * a specific combat pattern in headless smoke runs.
     *
     * @param magicSystem Pointer to elemental magic system (non-owning,
     *                    may be nullptr to disable autoplay casting).
     */
    void setElementalMagicSystem(class ElementalMagicSystem* magicSystem);

    /**
     * Set terrain reference so the player stays on the heightfield instead of
     * clipping against the old hard-coded y=0 ground plane.
     */
    void setTerrain(const Terrain* terrain) { terrain_ = terrain; }

    /**
     * Set forest reference so the player can be pushed out of tree colliders
     * each frame. Without this, the 18,931 tree colliders in PhysicsWorld
     * are invisible to the player movement system — the player walks through
     * trees because nothing checks tree positions vs player position. Mirrors
     * the web port's TerrainCollisionSystem.tsx static-object push-out at
     * src/components/game/terrain/TerrainCollisionSystem.tsx:84-96.
     *
     * Pass nullptr (the default) to disable tree collision entirely (e.g.
     * for a debug "no-clip" mode). The Physics tail no-ops cleanly in that
     * case.
     *
     * 2026-04-26 SURVIVAL-PORT: explicit user directive
     * ("make sure the terrain i cant walk through and really hone in").
     */
    void setForest(const class Forest* forest) { forest_ = forest; }

    /**
     * Enable or disable autoplay AI. When enabled the player ignores
     * keyboard/mouse input and is driven by a minimal "chase nearest enemy,
     * attack in range" policy. Used by the `--autoplay` CLI smoke run and by
     * the portfolio/demo recordings so a reviewer sees real combat instead of
     * a motionless cat being mauled to death.
     *
     * The AI still respects `controlEnabled_` — pausing the game via
     * CatAnnihilation::setGameState still freezes the player, which is what
     * we want for consistency with human-driven play.
     *
     * @param enabled If true, the player is driven by the autoplay AI.
     */
    void setAutoplayMode(bool enabled) { autoplayMode_ = enabled; }

    /**
     * @return true if the autoplay AI is currently steering the player.
     */
    bool isAutoplayMode() const { return autoplayMode_; }

    /**
     * Enable or disable a slow cinematic orbit camera that revolves around
     * the player at the existing cameraOffset_ distance, advancing
     * cameraYaw_ at a fixed rate per second instead of taking input from
     * mouse delta. The pitch stays at whatever the system was last left at
     * (default 0 = horizon-level), so the orbit plane is the cat's
     * shoulders, which keeps the cat torso anchored in frame even as the
     * camera circles.
     *
     * Why this exists (2026-04-25 SHIP-THE-CAT directive): nightly
     * portfolio captures currently freeze the camera in a fixed third-
     * person follow framing — the cat is the same silhouette in every
     * screenshot. With the orbit on, a 25-second autoplay capture rotates
     * through ~115° of arc at the default 0.5 rad/s rate, showing the
     * Meshy-baked PBR fur from front-quarter, side, and rear-quarter
     * angles in a single playtest. Reviewers see the rig + textures
     * actually working, not a pinned silhouette. The orbit is also a
     * trivial diagnostic for "is the cat actually being skinned" —
     * if skinning broke and the cat were a static mesh, the orbit would
     * make that immediately obvious by revealing the bind-pose stiffness
     * from every angle.
     *
     * Defaults to OFF so the existing third-person follow framing is
     * preserved bit-for-bit unless a caller opts in. main.cpp turns it on
     * automatically when `--autoplay` is set (portfolio context); a
     * human player driving with WASD never sees this branch.
     *
     * @param enabled If true, updateCamera() advances cameraYaw_
     *                deterministically and ignores any mouse delta.
     * @param yawRateRadPerSec Angular velocity of the orbit. The default
     *                of 0.5 rad/s ≈ 28.65°/s gives a full revolution every
     *                ~12.6 s — slow enough that successive frames don't
     *                appear to teleport, fast enough that a 25 s capture
     *                shows multiple distinct angles. Negative values
     *                rotate counter-clockwise; zero pins the camera at
     *                the current yaw (still useful as a "freeze the
     *                cinematic orbit" hook).
     */
    void setCinematicOrbit(bool enabled, float yawRateRadPerSec = 0.5F);

    /**
     * @return true when the cinematic orbit camera is currently driving
     *         the per-frame yaw advance (set by setCinematicOrbit).
     */
    bool isCinematicOrbitEnabled() const { return cinematicOrbitEnabled_; }

    /**
     * @brief Enable / disable "camera follows the cat's facing direction".
     *
     * 2026-04-26 SURVIVAL-PORT. When enabled, updateCamera() lerps
     * cameraYaw_ toward the player's transform-yaw every frame, so the
     * camera always sits roughly behind the cat regardless of where the
     * cat turns. When the cat pivots to face a new dog, the camera
     * follows. The default `lagPerSecond` value is tuned so the camera
     * doesn't snap (which would feel jarring) but doesn't drift (which
     * would let the cat run sideways out of frame).
     *
     * Mutually exclusive with cinematic-orbit in the sense that orbit
     * advances cameraYaw_ at a constant rate while follow-yaw drives it
     * from the player — running both at once would oscillate. The
     * autoplay default disables orbit and enables follow-yaw; toggling
     * orbit on via --cinematic-orbit-camera also disables follow-yaw at
     * the call site (game/main.cpp gate).
     *
     * @param enabled        true to track player yaw each frame.
     * @param lagPerSecond   Exponential follow rate. 8.0 → ~0.125 s
     *                       half-life; the camera covers half the
     *                       remaining angle every ~125 ms. Higher values
     *                       feel snappier (good for chase), lower feels
     *                       cinematic. 0 freezes yaw at its current value.
     */
    void setFollowPlayerYaw(bool enabled, float lagPerSecond = 8.0F);

    /**
     * @return true when follow-player-yaw is driving cameraYaw_ each frame.
     */
    bool isFollowPlayerYawEnabled() const { return followPlayerYawEnabled_; }

private:
    // Input processing
    void processMovementInput(float dt);
    void processMouseLook(float dt);
    void processJumpInput();
    void processAttackInput();
    void processBlockInput();
    void processDodgeInput(float dt);

    // Autoplay AI: replaces keyboard/mouse-driven input with a minimal
    // chase-and-attack policy. Implemented alongside the standard input path
    // (not as a sibling System) so the same cooldown/ground/gravity code runs
    // exactly once per frame regardless of who's steering the cat — sharing
    // the one authoritative physics update keeps human and AI behaviour
    // bit-identical outside of direction/attack decisions.
    void updateAutoplay(float dt);

    // Camera management
    void updateCamera(float dt);

    // 2026-04-26 SURVIVAL-PORT — radial XZ push-out of any tree the
    // player overlaps. Called from the Physics tail in
    // {processMovementInput, updateAutoplay} after velocity-integrated
    // position update. No-op when forest_ is null. Mirrors the web
    // port's TerrainCollisionSystem.tsx static-object push (radius
    // tree + 0.5m player, push to the edge of the combined circle).
    void pushOutOfTrees();

    // Dodge double-tap detection
    struct DoubleTapState {
        float lastTapTime = -1.0f;
        float doubleTapWindow = 0.3f;  // 300ms window for double-tap
    };

    DoubleTapState doubleTapW_;
    DoubleTapState doubleTapA_;
    DoubleTapState doubleTapS_;
    DoubleTapState doubleTapD_;
    float currentTime_ = 0.0f;

    // System state
    Engine::Input* input_ = nullptr;
    CatEngine::Entity playerEntity_;
    bool controlEnabled_ = true;
    bool autoplayMode_ = false;    // When true, updateAutoplay() drives the cat.
    class CombatSystem* combatSystem_ = nullptr;
    class ElementalMagicSystem* magicSystem_ = nullptr;  // Non-owning, optional.
    const Terrain* terrain_ = nullptr;

    // Non-owning Forest reference used by the Physics-tail tree-collision
    // push-out (see processTreeCollision() in PlayerControlSystem.cpp).
    // Optional — null = no tree collision check, player walks through trees
    // (the regression we are fixing here).
    const class Forest* forest_ = nullptr;

    // Autoplay spell cadence: a level-1 single-target ranged spell is cast
    // at most every kAutoplayCastInterval seconds, guaranteeing a steady
    // stream of live particle emitters to exercise the ribbon-trail
    // renderer without flooding the GPU ring buffer. 2.5s is a compromise
    // between "visible activity during the 40s smoke playtest" (lands ~15
    // casts) and "doesn't dominate combat pacing so melee still reads as
    // primary" (cooldown is well above each spell's own 1.0–1.5s spell
    // cooldown, and projectile trails of ~0.6s decay cleanly between
    // casts).
    float autoplayCastCooldown_ = 0.0F;
    static constexpr float kAutoplayCastInterval = 2.5F;

    // Autoplay spell cycler: rotates through one level-1 single-target
    // ranged spell per element so each cast exercises a different branch
    // of the per-element particle dispatcher (kHitProfiles[Fire/Ice/
    // Poison/Magic] and kDeathProfiles[...]). Without cycling, autoplay
    // would only ever cast water_bolt → only Ice-tinted bursts ever fire
    // → the dispatcher's other element profiles stay visually unverified
    // for portfolio captures. Cycling means a 40 s playtest at 2.5 s
    // cadence lands ~15 casts spanning all 4 elements ~3-4 times each.
    //
    // Spell selection (all level-1, single-target, range >= 25 m so the
    // 28 m engage gate in updateAutoplay always passes within reach):
    //   index 0: water_bolt   (Water  → Ice    : pale-cyan downward drift)
    //   index 1: wind_gust    (Air    → Magic  : white-purple radial)
    //   index 2: rock_throw   (Earth  → Poison : yellow-green miasma)
    //   index 3: fireball     (Fire   → Fire   : orange-yellow upward sparks)
    //
    // Note: fireball has areaOfEffect = 2.0 so it routes through
    // castAOESpell rather than castProjectileSpell. The AOE damage path
    // currently doesn't apply damage in updateActiveSpells (separate gap
    // for a future iteration) — fireball still casts and creates the
    // particle emitter at the cast point, exercising the visual code, but
    // doesn't deal damage or trigger the per-element burst until the
    // AOE damage path is wired. The other three spells (water_bolt,
    // wind_gust, rock_throw) all route through castProjectileSpell →
    // checkSpellCollisions → applySpellDamage → CombatSystem → per-element
    // burst dispatcher and produce the visual delta this iteration.
    uint32_t autoplayCastIndex_ = 0;

    // Camera parameters.
    //
    // cameraOffset_ is the player-local offset (x,y,z = right, up, back)
    // that is rotated by yaw/pitch to compute the camera's world position.
    // Default values are tuned for a cinematic third-person framing of a
    // ~1 m tall cat:
    //   - Y =  1.5 m above the player's transform origin: the camera sits
    //     above eye-level of the cat so the look-at anchor at player+0.75
    //     produces a steeper natural downtilt (see horizon-fraction WHY
    //     below). The cat still reads as "right in front of you" because
    //     the lookAt in CatAnnihilation::render keeps it dead-centre.
    //   - Z =  2.8 m back: at FOV 60° vertical the cat fills ~30 % of
    //     frame height — Spyro / Ratchet & Clank framing where the
    //     character is the unmistakable centre of attention.
    //   - X =  0: shoulder-cam offsets are tempting but the lookAt fix
    //     in CatAnnihilation::render anchors the view to player.position,
    //     so any horizontal offset would skew the player off-centre. Keep
    //     it 0 unless we explicitly switch to a fixed offset+aim model.
    //
    // History (2026-04-25 iter): the prior pair (0, 2.5, 5.0) was tuned
    // toward a Dark Souls / Sekiro stadium feel, but the playtest PPM
    // diff against the baseline showed entity rasterisation only at the
    // bottom-left/right frame edges (xRange=[201,1010], yRange=[59,959])
    // with zero pixel difference in the centre 200x200 — i.e. the cats
    // were tiny silhouettes nobody could see in a screenshot. Pulling
    // the camera in to (0, 1.2, 2.8) made the cat fill ~30 % of frame
    // height instead of ~17 % — the unmistakable-centre fix that
    // anchored everything since.
    //
    // History (2026-04-26 iter): bumped Y from 1.2 -> 1.5 to deepen the
    // natural downtilt baked in by the lookAt anchor. WHY: the lookAt
    // target in CatAnnihilation::render is player+0.75 (cat torso). With
    // camPos.y = player.y+1.2 the resulting lookAt vector pitched down
    // atan2(0.45, 2.8) = 9.1° below horizon, which on a 60° vertical
    // FOV places the horizon line at (1 - 9.1/30)/2 = 35 % from the top
    // of frame. The clear-sky color (Renderer.cpp:237 = sRGB ~188,188,213
    // post-tone-map) then occupied that same 35 % of pixels, parking
    // cat-verify's topColorPct gate at exactly the 35 % failure threshold.
    // Evidence rows #11 (passes=1, topColorPct=26.1 %) and #12 (passes=0,
    // topColorPct=35.7 %) on the same SHA confirmed the gate was
    // photometric flake noise around a value the camera was producing
    // by design. Lifting Y to 1.5 m drops 0.3 m more onto the lookAt
    // delta (now 0.75 m over 2.8 m horizontal), pitching the camera
    // atan2(0.75, 2.8) = 15.0° below horizon; horizon now lands at
    // (1 - 15/30)/2 = 25 % from the top of frame, so the sky-blue
    // dominant pixel dies down to ~25 % of frame and clears the
    // 35 % topColorPct gate by ~10 pp of margin. The cat framing
    // itself is preserved because the lookAt still re-centres to
    // player+0.75 — only the camera rig elevation changed, the cat
    // still appears dead-centre in frame, just slightly smaller in
    // foreshortening (parallax: the cat sphere subtends a sub-degree
    // smaller angle from the higher camera, undetectable at the
    // ~30 % of-frame-height it occupies).
    //
    // WHY NOT closer (e.g. Z=2.0): the CPU-skinned mesh extents on
    // Meshy rigged variants run to ~1.0 m at the largest leader cats
    // (storm_leader and frost_leader at 200k+ verts ship oversized
    // proportions per the Meshy auto-rigger upper poly bound). A 2.0 m
    // back offset starts clipping the front of the cat into the near
    // plane (0.1 m) at extreme yaw + pitch combinations during an
    // autoplay sprint, which would crop the model worse than the
    // distance fix achieves. 2.8 m is the empirical break-even.
    // 2026-04-26 SURVIVAL-PORT — camera offset matched to web port:
    // src/components/game/BasicScene.tsx:193 uses
    // `<PerspectiveCamera position={[0, 12, 15]} fov={75}>`. The pre-port
    // value (0, 1.5, 2.8) zoomed too tight on the cat — the user
    // explicitly called this out as "way too close to the cat ... should
    // be a proper 3rd person view". 12 up + 15 back = ~19 unit hypot
    // at ~38° pitch, matches the web port shot framing.
    Engine::vec3 cameraOffset_ = Engine::vec3(0.0f, 3.0f, 6.0f);
    Engine::vec3 cameraPosition_ = Engine::vec3(0.0f, 0.0f, 0.0f);
    Engine::vec3 cameraForward_ = Engine::vec3(0.0f, 0.0f, -1.0f);
    float cameraYaw_ = 0.0f;       // Horizontal rotation (radians)

    // Initial pitch: 0.0f (horizon level).
    //
    // History: the prior default of -0.3 rad (-17.2°) was meant to give a
    // gentle "looking-down-at-the-cat" framing, but it interacted badly with
    // the rotated offset + lookAt-the-cat anchor in CatAnnihilation::render.
    // Concretely: rotating cameraOffset (0, 1.2, 2.8) by R_x(-0.3) gives
    // (0, 1.97, 2.32) — camera ends up 1.97 m ABOVE the cat instead of the
    // intended 1.2 m, and the lookAt target at (player+0.75) is then 1.22 m
    // BELOW the camera, forcing the lookAt direction to a -28° downtilt.
    // The 2026-04-25T1928Z playtest frame-dump confirmed the failure mode:
    // a vertical column down the centre of a 1904x993 PPM read pure terrain
    // grass [147,210,129] for ~70 % of the image with the cat occupying
    // a small orange band at y≈686-833 — there was effectively NO sky in
    // frame, contradicting the cameraOffset comment block above which
    // promises "the cat reads as 'right in front of you' instead of looking
    // down on it from above".
    //
    // Setting initial pitch to 0.0 keeps the camera level. The cat's lookAt
    // anchor is at player+0.75 (cat torso, ~75 cm above feet) and the
    // unrotated camera Y is player+1.5 (was 1.2 pre-2026-04-26); the
    // residual -0.75 m drop over 2.8 m of horizontal produces a ~15°
    // downtilt naturally — enough to keep the cat anchored centre-frame
    // and to land the horizon line at ~25 % from the top of frame so the
    // sky-blue clear (Renderer.cpp:237) covers ~25 % of pixels instead of
    // ~35 % (which was parking cat-verify's topColorPct gate at exactly
    // its failure threshold; see cameraOffset_ comment block above for
    // the photometric derivation and evidence rows #11 / #12). The
    // heightfield is 512 m wide; from camera Y≈26 m the line-of-sight
    // at 256 m forward at the new tilt sits at Y≈26 + 256·tan(-15°) ≈
    // -42 m, comfortably below the heightfield's max ridge so the world
    // edge in frame transitions cleanly from terrain to sky without a
    // hard band. The user-facing visible delta is "the cat looks like
    // it's playing on a planet, not floating in a horizon-bisected box."
    float cameraPitch_ = 0.0f;     // Vertical rotation (radians)
    float mouseSensitivity_ = 0.002f;
    float cameraFollowSpeed_ = 10.0f;
    float cameraMinPitch_ = -1.4f;  // ~-80 degrees (looking near-straight-down)
    // cameraMaxPitch_ = 0.5 rad (~28°) lets the player tilt the view up
    // enough to actually see sky, sun-direction lighting and the dynamic
    // day/night skybox (when wired). The prior 0.0 ceiling (horizon-only)
    // pre-dated the camera-pitch fix and made looking at the sky physically
    // impossible — a real limitation now that the upper half of frame
    // renders sky correctly.
    float cameraMaxPitch_ = 0.5f;

    // Camera-bob phase accumulator (radians).
    //
    // Stored on the system, not derived from a global clock, so that scaling
    // the bob frequency with player speed does NOT introduce a phase-jump:
    // we advance the phase by `dt * 2*PI * bobFreq` each frame, and bobFreq
    // can change continuously without a visible snap because it's the
    // DERIVATIVE of phase that depends on speed, not phase itself. If we
    // computed bobY = sin(currentTime * bobFreq) instead, every speed change
    // would teleport the camera to a different point on the sin curve and
    // read as glitchy. See updateCamera() for the full derivation and the
    // SHIP-THE-CAT directive (2026-04-24 18:58) that motivated this.
    float cameraBobPhase_ = 0.0f;

    // Camera-punch state for melee-attack cinematic kick (the "Hi-Fi Rush /
    // Devil May Cry" feel where the camera reacts to the strike instead of
    // tracking it like glassy CCTV footage). Two pieces of state:
    //
    //   cameraPunchTimer_     — seconds REMAINING in the current punch
    //                           animation; counts from kPunchDurationS down
    //                           to 0. Zero when no punch is active. Read
    //                           every frame; the `phase = 1 - timer/dur`
    //                           construction is what drives the asymmetric
    //                           envelope (fast push, slow ease back).
    //
    //   prevAttackCooldown_   — last frame's CombatComponent::attackCooldown.
    //                           CombatComponent::startAttack() spikes
    //                           attackCooldown from 0 (or near-0) to
    //                           getCooldownDuration() in a single frame, so
    //                           a frame-to-frame jump > 0.05s reliably flags
    //                           the start of a fresh attack. Storing it on
    //                           the system (not in the component) keeps the
    //                           detection local — we don't want CombatSystem
    //                           to know about the camera, and we don't want
    //                           camera state polluting CombatComponent.
    //
    // WHY a custom timer rather than reading attackCooldown directly: the
    // cooldown's duration depends on weapon attackSpeed (sword 1.5 atk/s →
    // cd=0.667s, staff 0.8 → 1.25s, bow 1.2 → 0.833s), which would make the
    // punch animation last different times per weapon and break the camera's
    // perceptual rhythm. Locking the punch to a fixed kPunchDurationS keeps
    // the cinematic kick consistent across weapon swaps. The cost is a
    // single-float member.
    //
    // WHY a jump-detector rather than calling a method on CombatComponent
    // when an attack starts: PlayerControlSystem and CombatSystem run in
    // separate update orders and aren't tightly coupled — adding a callback
    // would entangle them and require lifecycle plumbing for system order
    // changes. Polling the cooldown is one read per frame and zero coupling.
    float cameraPunchTimer_ = 0.0f;
    float prevAttackCooldown_ = 0.0f;

    // Cinematic orbit camera state — see setCinematicOrbit() docblock.
    //
    // When cinematicOrbitEnabled_ is true, updateCamera() advances
    // cameraYaw_ by `cinematicOrbitYawRate_ * dt` each frame and skips the
    // mouse-delta yaw injection from processMouseLook(). The result is a
    // slow, deterministic camera revolution around the player at the
    // existing cameraOffset_ distance.
    //
    // WHY this state lives here (rather than in main.cpp or a freestanding
    // CinematicCamera helper): the orbit is fundamentally a yaw-advance
    // override of the existing follow camera, so the cleanest seam is to
    // keep it inside the system that already owns cameraYaw_ /
    // cameraPitch_ / cameraPosition_. A separate helper would either need
    // friend access to those fields (worse encapsulation) or duplicate the
    // rotation+lerp math (worse maintenance). Two members + a one-line
    // updateCamera branch is the minimum surface area.
    //
    // WHY a yaw rate (rad/s) rather than a period (seconds per revolution):
    // the existing math in updateCamera multiplies by `dt`, so an angular
    // velocity composes naturally without an extra divide. Period would
    // mean recomputing 2π/period inside the inner loop, or storing both
    // and keeping them in sync. Yaw rate also flips sign cleanly for
    // counter-clockwise orbits (negative rate) — period would need a
    // separate boolean.
    bool  cinematicOrbitEnabled_  = false;
    float cinematicOrbitYawRate_  = 0.5F;  // rad/s, ~28.6°/s, ~12.6 s per revolution

    // 2026-04-26 SURVIVAL-PORT — follow-player-yaw camera state.
    //
    // When followPlayerYawEnabled_ is true, updateCamera() reads the
    // player's transform yaw (the same atan2-based yaw written by
    // processMovementInput / updateAutoplay when the cat turns to face
    // a target) and lerps cameraYaw_ toward it at exponential rate
    // followPlayerYawLagPerSec_. This sits the camera roughly behind
    // the cat at all times: when the cat pivots to attack a dog on its
    // left, the camera follows.
    //
    // WHY this is a sibling state of cinematicOrbit and not a
    // replacement: the legacy cinematic-orbit demo mode is still useful
    // for portfolio captures (a single fixed yaw-advance produces a
    // clean revolving turntable shot). Having both modes available
    // lets the autoplay default flip to follow-yaw without breaking
    // anyone who launches with --cinematic-orbit-camera explicitly.
    //
    // WHY exponential lerp (lagPerSec) and not linear lerp or instant:
    //   - linear lerp at fixed dt-multiplied rate snaps when dt is
    //     large and crawls when dt is small, which produces uneven
    //     camera tracking under variable frame pacing.
    //   - instant snap (cameraYaw_ = playerYaw) gives the camera
    //     teleport feel — the cat turns and the camera teleports.
    //   - exponential lerp `cameraYaw_ += (target - cameraYaw_) *
    //     (1 - exp(-rate * dt))` is frame-rate independent and feels
    //     natural — the half-life is `ln(2) / rate` regardless of dt.
    bool  followPlayerYawEnabled_   = false;
    float followPlayerYawLagPerSec_ = 8.0F;
    // Movement parameters
    float movementDeadzone_ = 0.1f;

    // Gravity constant
    static constexpr float GRAVITY = -30.0f;  // m/s^2
};

} // namespace CatGame
