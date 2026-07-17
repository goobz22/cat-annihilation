#!/usr/bin/env bun
/**
 * verify_rig.ts - Headless per-model "does this rig actually animate?" oracle.
 *
 * Why this exists:
 *   The Meshy-authored character GLBs (player cat, four dog variants) must each
 *   carry a CORRECT skin (joints + inverseBindMatrices) AND real animation clips
 *   that VERIFIABLY deform the skeleton. The failure we keep hitting is silent:
 *   a GLB can load, present a skeleton, even carry named clips, and still be dead
 *   on screen - clips with zero tracks, tracks pointing at bones that no longer
 *   exist, or clips whose keyframes never move a single joint. None of that
 *   throws; the model just stands in bind pose forever. This tool turns every one
 *   of those silent states into a loud, machine-checkable failure so the rigging
 *   pipeline can iterate against a real signal instead of a screenshot.
 *
 *   It loads the GLB EXACTLY the way three.js does at runtime (GLTFLoader), so it
 *   catches problems the raw-binary parsers (test_rigged_cats.ts) cannot: it runs
 *   the animation system end to end - builds a real AnimationMixer, advances it
 *   through each clip, and samples the resulting bone world transforms.
 *
 * What it verifies, per clip:
 *   1. The clip is a THREE.AnimationClip with > 0 tracks, and EVERY track target
 *      resolves to a node that still exists in the scene graph (no orphan tracks -
 *      the "clip references a bone the exporter renamed/dropped" failure).
 *   2. Advancing a mixer through the clip produces REAL bone displacement: sample
 *      10 timesteps, and require the max world-space displacement of any bone to
 *      exceed 1% of the model's bounding-box diagonal. This is the check that
 *      catches an all-static clip (keyframes that hold bind pose forever).
 *   3. No NaN / Inf appears in any sampled bone world matrix (catches degenerate
 *      inverse-bind matrices, zero-length quaternions, divide-by-zero rigs).
 *
 * And once per file, the skin itself:
 *   4. Skinned-mesh sanity: every sampled skinIndex is in range (< that mesh's
 *      bone count) and every sampled skinWeight row sums to ~1 (sample 200
 *      vertices). An out-of-range joint index or an unnormalized/zero weight row
 *      means the GPU skinning would read garbage or leave vertices unweighted.
 *
 * Exit code: 0 only when the file has >= 1 clip AND every clip passes AND the
 * skin passes; 1 otherwise. This is a gate, so "no clips at all" is a failure -
 * an un-animated model is exactly the state the owner directive is fighting.
 *
 * Usage:
 *   bun scripts/verify_rig.ts <model.glb> [--json]
 *
 *   Default output is one human line per clip (with a check/cross and the actual
 *   numbers) plus a skin line and a final verdict. --json emits the full
 *   structured report on stdout instead, for an orchestrator to consume.
 */

import * as path from 'path';
import { promises as fs } from 'fs';
import {
  AnimationMixer,
  Box3,
  Vector3,
  PropertyBinding,
  LoopOnce,
  type Object3D,
  type Bone,
  type SkinnedMesh,
  type AnimationClip,
} from 'three';

// ---------------------------------------------------------------------------
// Headless DOM shim for GLTFLoader texture decoding.
//
// Under bun there is no createImageBitmap / Image / document, so three's
// ImageLoader (which GLTFLoader falls back to when createImageBitmap is absent)
// would otherwise hang or reject when it hits an embedded texture. We do NOT
// care about pixels here - the rig oracle only inspects skeleton, skin buffers,
// and animation tracks, none of which depend on decoded image data. So we hand
// the loader a fake <img> element that reports "loaded" the microtask after its
// src is set. That lets GLTFLoader.parse() resolve its whole dependency graph
// (materials reference textures) without ever touching a real image decoder.
//
// This must be installed before GLTFLoader.parse() runs. ESM hoists the imports
// above this block, but that only DEFINES the loader classes; the shimmed
// globals are read lazily at parse time, which happens later in main().
// ---------------------------------------------------------------------------
function installHeadlessImageShim(): void {
  const urlCtor = globalThis.URL as unknown as {
    createObjectURL?: (blob: unknown) => string;
    revokeObjectURL?: (url: string) => void;
  };
  // URL.createObjectURL exists in bun; revokeObjectURL may not. GLTFLoader calls
  // revoke after the (fake) image loads, so a no-op keeps it from throwing.
  if (typeof urlCtor.revokeObjectURL !== 'function') urlCtor.revokeObjectURL = () => {};
  if (typeof urlCtor.createObjectURL !== 'function') urlCtor.createObjectURL = () => 'blob:stub';

  const anyGlobal = globalThis as unknown as { document?: unknown };
  if (anyGlobal.document) return;
  anyGlobal.document = {
    // three's ImageLoader creates its <img> via document.createElementNS.
    createElementNS: (_namespace: string, tag: string) => {
      if (tag !== 'img') return {};
      const listeners: Record<string, Array<(ev: { type: string }) => void>> = { load: [], error: [] };
      let srcValue = '';
      return {
        addEventListener: (type: string, cb: (ev: { type: string }) => void) => {
          (listeners[type] ||= []).push(cb);
        },
        removeEventListener: () => {},
        set src(value: string) {
          srcValue = value;
          // Fire "load" on the next microtask so ImageLoader's onLoad resolves
          // the texture promise. We never populate width/height because nothing
          // downstream in this tool reads them.
          queueMicrotask(() => listeners.load.forEach((cb) => cb({ type: 'load' })));
        },
        get src() {
          return srcValue;
        },
      };
    },
  };
}
installHeadlessImageShim();

// GLTFLoader is imported dynamically AFTER the shim is installed. Static ESM
// imports are hoisted, and while merely importing the module is harmless, doing
// the import here keeps the "shim first, loader second" ordering obvious and
// robust to any future top-of-module side effects in the loader package.
const { GLTFLoader } = (await import('three-stdlib')) as {
  GLTFLoader: new () => {
    parse: (
      data: ArrayBuffer,
      resourcePath: string,
      onLoad: (gltf: { scene: Object3D; animations: AnimationClip[] }) => void,
      onError: (err: unknown) => void,
    ) => void;
  };
};

// ---------------------------------------------------------------------------
// Report shapes. These are the JSON contract emitted under --json and consumed
// by the orchestrator, so the field names are deliberately explicit.
// ---------------------------------------------------------------------------
interface ClipReport {
  name: string;
  duration: number;
  trackCount: number;
  boundTrackCount: number; // tracks whose target resolves to an existing node
  boneBoundTrackCount: number; // subset of the above whose target is a Bone
  orphanTracks: string[]; // track names whose target node no longer exists
  maxBoneDisplacement: number; // world-space, over 10 samples
  displacementThreshold: number; // 1% of the model bbox diagonal
  moves: boolean; // maxBoneDisplacement > threshold
  hasNaNorInf: boolean; // any NaN/Inf in a sampled bone world matrix
  ok: boolean;
  failReasons: string[];
}

interface SkinMeshReport {
  name: string;
  boneCount: number;
  sampledVertices: number;
  skinIndexOutOfRange: number; // count of sampled verts with an out-of-range joint
  worstSkinIndex: number; // largest joint index seen (for diagnostics)
  weightRowsBad: number; // count of sampled verts whose weights don't sum to ~1
  worstWeightSumError: number; // largest |sum - 1| seen
  ok: boolean;
}

interface SkinReport {
  ok: boolean;
  meshCount: number;
  meshes: SkinMeshReport[];
}

interface ModelReport {
  file: string;
  ok: boolean;
  boneCount: number;
  bboxDiagonal: number;
  clipCount: number;
  skin: SkinReport;
  clips: ClipReport[];
  expectedClips: Record<string, boolean>; // presence of idle/walk/run/attack
  notes: string[];
}

// The pipeline directive names idle/walk/run/attack as the minimum clip set.
// Presence is reported (and warned about) but does NOT by itself flip the exit
// code: the exit contract is strictly "every clip that EXISTS passes + skin
// passes + at least one clip exists". Whole-set completeness is a separate,
// orchestrator-level gate, kept distinct so this tool stays a precise per-clip
// oracle rather than silently conflating "clip X is broken" with "clip X is
// absent". Matching is case-insensitive and substring-based because exporters
// name clips "Walk", "walk_cycle", "RunFast", etc.
const EXPECTED_CLIP_KEYWORDS = ['idle', 'walk', 'run', 'attack'];

const DISPLACEMENT_FRACTION = 0.01; // bone must move > 1% of the bbox diagonal
const DISPLACEMENT_SAMPLES = 10;
const SKIN_VERTEX_SAMPLES = 200;
const WEIGHT_SUM_TOLERANCE = 0.02; // skinWeight row must sum to 1 +/- this

// Load a GLB from disk and hand back the three.js scene + clips. parse() is
// callback-based, so we wrap it in a promise. We slice the underlying
// ArrayBuffer precisely because Buffer views can share a larger pool and
// GLTFLoader reads absolute byte offsets.
async function loadGlb(glbPath: string): Promise<{ scene: Object3D; animations: AnimationClip[] }> {
  const buf = await fs.readFile(glbPath);
  const arrayBuffer = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
  const loader = new GLTFLoader();
  return new Promise((resolve, reject) => {
    loader.parse(
      arrayBuffer as ArrayBuffer,
      '',
      (gltf) => resolve({ scene: gltf.scene, animations: gltf.animations }),
      (err) => reject(err instanceof Error ? err : new Error(String(err))),
    );
  });
}

// Collect every Bone in the scene once. Bones are stable objects, so we can
// hold references and re-read their world transforms after each mixer advance.
function collectBones(root: Object3D): Bone[] {
  const bones: Bone[] = [];
  root.traverse((obj) => {
    if ((obj as Bone).isBone) bones.push(obj as Bone);
  });
  return bones;
}

function collectSkinnedMeshes(root: Object3D): SkinnedMesh[] {
  const meshes: SkinnedMesh[] = [];
  root.traverse((obj) => {
    if ((obj as SkinnedMesh).isSkinnedMesh) meshes.push(obj as SkinnedMesh);
  });
  return meshes;
}

// A track is an orphan when its parsed node name does not resolve to any object
// in the scene graph - i.e. the clip drives a bone the current rig no longer
// has. We use three's own PropertyBinding so the resolution rules match exactly
// what the runtime mixer would do (name, uuid, or sanitized-name lookup).
function classifyTrack(root: Object3D, trackName: string): { node: Object3D | null; isBone: boolean } {
  let parsed: ReturnType<typeof PropertyBinding.parseTrackName>;
  try {
    parsed = PropertyBinding.parseTrackName(trackName);
  } catch {
    return { node: null, isBone: false };
  }
  const node = PropertyBinding.findNode(root, parsed.nodeName) as Object3D | null;
  return { node: node ?? null, isBone: !!node && !!(node as Bone).isBone };
}

// Check the 16 elements of a bone's world matrix for any non-finite value. A
// single NaN here poisons every descendant transform and every skinned vertex.
function matrixHasNaNorInf(bone: Bone): boolean {
  const e = bone.matrixWorld.elements;
  for (let i = 0; i < 16; i++) {
    if (!Number.isFinite(e[i])) return true;
  }
  return false;
}

function verifyClip(
  scene: Object3D,
  bones: Bone[],
  clip: AnimationClip,
  bboxDiagonal: number,
): ClipReport {
  const failReasons: string[] = [];

  // --- Track binding: count bound vs orphan, and how many hit actual bones. ---
  let boundTrackCount = 0;
  let boneBoundTrackCount = 0;
  const orphanTracks: string[] = [];
  for (const track of clip.tracks) {
    const { node, isBone } = classifyTrack(scene, track.name);
    if (node) {
      boundTrackCount++;
      if (isBone) boneBoundTrackCount++;
    } else {
      orphanTracks.push(track.name);
    }
  }
  if (clip.tracks.length === 0) failReasons.push('clip has 0 tracks');
  if (orphanTracks.length > 0) {
    failReasons.push(`${orphanTracks.length} orphan track(s) target missing nodes`);
  }
  if (clip.tracks.length > 0 && boneBoundTrackCount === 0) {
    failReasons.push('no track targets an existing bone');
  }

  // --- Motion + finiteness: advance a fresh mixer through 10 samples. ---
  // A fresh mixer per clip guarantees no pose leaks in from a previous clip.
  // LoopOnce + clampWhenFinished lets us sample the true end pose at t=duration
  // instead of the repeat wrapping back to frame 0.
  const mixer = new AnimationMixer(scene);
  const action = mixer.clipAction(clip);
  action.setLoop(LoopOnce, 1);
  action.clampWhenFinished = true;
  action.play();

  // Per-bone world position at the first sample (clip start). Displacement is
  // measured relative to this so we detect whether the clip MOVES the skeleton
  // over its own timeline, independent of the bind pose.
  const startPositions: Vector3[] = bones.map(() => new Vector3());
  const scratch = new Vector3();
  let maxDisplacement = 0;
  let hasNaNorInf = false;

  const duration = clip.duration;
  for (let s = 0; s < DISPLACEMENT_SAMPLES; s++) {
    // Even samples across [0, duration]. setTime(t) resets each action to 0 and
    // advances by t, so every sample is an absolute, independent evaluation.
    const t = duration > 0 ? (duration * s) / (DISPLACEMENT_SAMPLES - 1) : 0;
    mixer.setTime(t);
    scene.updateMatrixWorld(true);

    for (let b = 0; b < bones.length; b++) {
      const bone = bones[b];
      if (matrixHasNaNorInf(bone)) hasNaNorInf = true;
      bone.getWorldPosition(scratch);
      if (s === 0) {
        startPositions[b].copy(scratch);
      } else {
        const d = scratch.distanceTo(startPositions[b]);
        if (d > maxDisplacement) maxDisplacement = d;
      }
    }
  }
  action.stop();
  mixer.uncacheClip(clip);

  const displacementThreshold = bboxDiagonal * DISPLACEMENT_FRACTION;
  const moves = maxDisplacement > displacementThreshold;
  if (duration <= 0) {
    failReasons.push('clip duration is 0 (cannot animate)');
  } else if (!moves) {
    failReasons.push(
      `static: max bone displacement ${maxDisplacement.toExponential(2)} <= threshold ${displacementThreshold.toExponential(2)}`,
    );
  }
  if (hasNaNorInf) failReasons.push('NaN/Inf in a sampled bone matrix');

  return {
    name: clip.name,
    duration,
    trackCount: clip.tracks.length,
    boundTrackCount,
    boneBoundTrackCount,
    orphanTracks,
    maxBoneDisplacement: maxDisplacement,
    displacementThreshold,
    moves,
    hasNaNorInf,
    ok: failReasons.length === 0,
    failReasons,
  };
}

function verifySkinnedMesh(mesh: SkinnedMesh): SkinMeshReport {
  const boneCount = mesh.skeleton ? mesh.skeleton.bones.length : 0;
  const geometry = mesh.geometry;
  const skinIndex = geometry.getAttribute('skinIndex');
  const skinWeight = geometry.getAttribute('skinWeight');

  // A skinned mesh with no skin attributes cannot deform - treat as fully bad.
  if (!skinIndex || !skinWeight) {
    return {
      name: mesh.name || '(unnamed skinned mesh)',
      boneCount,
      sampledVertices: 0,
      skinIndexOutOfRange: skinIndex ? 0 : 1,
      worstSkinIndex: 0,
      weightRowsBad: skinWeight ? 0 : 1,
      worstWeightSumError: 1,
      ok: false,
    };
  }

  const vertexCount = skinIndex.count;
  const step = Math.max(1, Math.floor(vertexCount / SKIN_VERTEX_SAMPLES));
  let sampled = 0;
  let outOfRange = 0;
  let worstIndex = 0;
  let weightRowsBad = 0;
  let worstSumError = 0;

  for (let i = 0; i < vertexCount && sampled < SKIN_VERTEX_SAMPLES; i += step) {
    sampled++;
    // Each vertex has up to 4 joint influences. getX/Y/Z/W apply the attribute's
    // normalization, so skinWeight comes back as real [0,1] floats even when the
    // GLB stored it as normalized u8/u16.
    const indices = [skinIndex.getX(i), skinIndex.getY(i), skinIndex.getZ(i), skinIndex.getW(i)];
    const weights = [skinWeight.getX(i), skinWeight.getY(i), skinWeight.getZ(i), skinWeight.getW(i)];

    let rowOutOfRange = false;
    for (const idx of indices) {
      if (idx > worstIndex) worstIndex = idx;
      // An influence only matters if it is out of range; index 0 on a zero-weight
      // slot is legitimate padding, but an index >= boneCount would make the GPU
      // read a nonexistent bone matrix regardless of its weight.
      if (!Number.isFinite(idx) || idx < 0 || idx >= boneCount) rowOutOfRange = true;
    }
    if (rowOutOfRange) outOfRange++;

    const sum = weights.reduce((a, w) => a + (Number.isFinite(w) ? w : NaN), 0);
    const sumError = Math.abs(sum - 1);
    if (!Number.isFinite(sum) || sumError > WEIGHT_SUM_TOLERANCE) {
      weightRowsBad++;
      if (Number.isFinite(sumError) && sumError > worstSumError) worstSumError = sumError;
      else if (!Number.isFinite(sum)) worstSumError = 1;
    }
  }

  return {
    name: mesh.name || '(unnamed skinned mesh)',
    boneCount,
    sampledVertices: sampled,
    skinIndexOutOfRange: outOfRange,
    worstSkinIndex: worstIndex,
    weightRowsBad,
    worstWeightSumError: worstSumError,
    ok: outOfRange === 0 && weightRowsBad === 0 && boneCount > 0,
  };
}

async function verifyModel(glbPath: string): Promise<ModelReport> {
  const notes: string[] = [];
  const { scene, animations } = await loadGlb(glbPath);
  scene.updateMatrixWorld(true);

  const bones = collectBones(scene);
  const skinnedMeshes = collectSkinnedMeshes(scene);

  // Bounding-box diagonal is the length scale everything is measured against.
  // Computed at bind pose, before any animation is applied, from the whole
  // scene so it reflects the model's real on-screen size.
  const box = new Box3().setFromObject(scene);
  const size = new Vector3();
  box.getSize(size);
  const bboxDiagonal = size.length();
  if (!Number.isFinite(bboxDiagonal) || bboxDiagonal === 0) {
    notes.push('WARNING: model bounding box is degenerate (zero/NaN diagonal)');
  }

  // --- Skin ---
  const meshReports = skinnedMeshes.map(verifySkinnedMesh);
  const skin: SkinReport = {
    ok: meshReports.length > 0 && meshReports.every((m) => m.ok),
    meshCount: meshReports.length,
    meshes: meshReports,
  };
  if (skinnedMeshes.length === 0) notes.push('WARNING: no skinned meshes in file (nothing to deform)');
  if (bones.length === 0) notes.push('WARNING: no bones in file (no skeleton)');

  // --- Clips ---
  const clipReports = animations.map((clip) => verifyClip(scene, bones, clip, bboxDiagonal));
  if (animations.length === 0) notes.push('FAIL: file contains 0 animation clips (model is un-animated)');

  // --- Expected-clip coverage (informational gate, see EXPECTED_CLIP_KEYWORDS). ---
  const expectedClips: Record<string, boolean> = {};
  for (const keyword of EXPECTED_CLIP_KEYWORDS) {
    const present = animations.some((c) => c.name.toLowerCase().includes(keyword));
    expectedClips[keyword] = present;
    if (!present) notes.push(`NOTE: no clip name contains "${keyword}" (pipeline minimum set is idle/walk/run/attack)`);
  }

  const clipsPass = clipReports.length > 0 && clipReports.every((c) => c.ok);
  const ok = clipsPass && skin.ok;

  return {
    file: glbPath,
    ok,
    boneCount: bones.length,
    bboxDiagonal,
    clipCount: animations.length,
    skin,
    clips: clipReports,
    expectedClips,
    notes,
  };
}

// ---------------------------------------------------------------------------
// Output formatting.
// ---------------------------------------------------------------------------
const GREEN = '\x1b[32m';
const RED = '\x1b[31m';
const DIM = '\x1b[2m';
const RESET = '\x1b[0m';
const PASS = `${GREEN}✅${RESET}`;
const FAIL = `${RED}❌${RESET}`;

function printHuman(report: ModelReport): void {
  console.log(`\n${path.basename(report.file)}`);
  console.log(
    `${DIM}  bones: ${report.boneCount}   skinnedMeshes: ${report.skin.meshCount}   ` +
      `clips: ${report.clipCount}   bboxDiagonal: ${report.bboxDiagonal.toFixed(3)}   ` +
      `motionThreshold(1%): ${(report.bboxDiagonal * DISPLACEMENT_FRACTION).toFixed(4)}${RESET}`,
  );

  console.log('  --- skin ---');
  if (report.skin.meshCount === 0) {
    console.log(`  ${FAIL} skin: no skinned meshes`);
  } else {
    for (const m of report.skin.meshes) {
      const badge = m.ok ? PASS : FAIL;
      console.log(
        `  ${badge} skin[${m.name}]: bones=${m.boneCount} sampled=${m.sampledVertices} ` +
          `idxOutOfRange=${m.skinIndexOutOfRange} (worstIdx=${m.worstSkinIndex}) ` +
          `badWeightRows=${m.weightRowsBad} (worst|sum-1|=${m.worstWeightSumError.toFixed(4)})`,
      );
    }
  }

  console.log('  --- clips ---');
  if (report.clips.length === 0) {
    console.log(`  ${FAIL} no animation clips`);
  } else {
    for (const c of report.clips) {
      const badge = c.ok ? PASS : FAIL;
      const detail =
        `tracks=${c.trackCount} bound=${c.boundTrackCount} bone-bound=${c.boneBoundTrackCount} ` +
        `orphans=${c.orphanTracks.length} maxDisp=${c.maxBoneDisplacement.toFixed(4)} ` +
        `(thr=${c.displacementThreshold.toFixed(4)}) moves=${c.moves} nan/inf=${c.hasNaNorInf}`;
      console.log(`  ${badge} ${c.name} [${c.duration.toFixed(2)}s] ${detail}`);
      if (!c.ok) console.log(`       ${RED}why:${RESET} ${c.failReasons.join('; ')}`);
    }
  }

  const missing = Object.entries(report.expectedClips)
    .filter(([, present]) => !present)
    .map(([k]) => k);
  if (missing.length) {
    console.log(`  ${DIM}expected-clip coverage incomplete: missing ${missing.join(', ')}${RESET}`);
  }
  for (const note of report.notes) console.log(`  ${DIM}${note}${RESET}`);

  console.log(`  ${report.ok ? `${GREEN}VERDICT: PASS` : `${RED}VERDICT: FAIL`}${RESET}\n`);
}

// ---------------------------------------------------------------------------
// CLI entry.
// ---------------------------------------------------------------------------
async function main(): Promise<void> {
  const args = process.argv.slice(2);
  const jsonMode = args.includes('--json');
  const modelArg = args.find((a) => !a.startsWith('--'));

  if (!modelArg) {
    console.error('usage: bun scripts/verify_rig.ts <model.glb> [--json]');
    process.exit(2);
  }

  const glbPath = path.resolve(modelArg);
  try {
    await fs.access(glbPath);
  } catch {
    console.error(`file not found: ${glbPath}`);
    process.exit(2);
  }

  let report: ModelReport;
  try {
    report = await verifyModel(glbPath);
  } catch (err) {
    // A hard load/parse failure is itself a rig failure worth reporting cleanly
    // rather than dumping a stack the orchestrator has to parse.
    const message = err instanceof Error ? err.message : String(err);
    if (jsonMode) {
      console.log(
        JSON.stringify(
          { file: glbPath, ok: false, error: `failed to load/parse: ${message}` },
          null,
          2,
        ),
      );
    } else {
      console.error(`${FAIL} ${path.basename(glbPath)}: failed to load/parse: ${message}`);
    }
    process.exit(1);
  }

  if (jsonMode) console.log(JSON.stringify(report, null, 2));
  else printHuman(report);

  process.exit(report.ok ? 0 : 1);
}

await main();
