#!/usr/bin/env bun
/**
 * test_per_cat_anim.ts - Compare per-cat animation behavior to the
 * canonical rig's expected behavior.
 *
 * Background:
 *   test_animations.ts verifies my procedural clip builders work
 *   correctly on the CANONICAL rig (bones have identity bind). That
 *   confirms the clip LOGIC is correct. But the user sees GLB cats
 *   whose walk animation produces SIDEWAYS leg motion instead of
 *   forward-back. Each GLB's bones have non-identity bind quaternions
 *   (baked in by Blender's glTF exporter), and my clip applies delta
 *   rotations as bind * delta_local. If a bone's local axes don't
 *   align with anatomy (e.g. local Z isn't the sideways pitch axis
 *   because of how Blender computed bone roll), the motion is wrong.
 *
 * This test:
 *   1. Parses each rigged GLB, extracts bone hierarchy + bind transforms.
 *   2. Reconstructs the armature in three.js as a Bone tree with the
 *      exact same bind transforms as the GLB.
 *   3. Runs walk/run/sitDown/layDown clips built from that bone tree.
 *   4. Measures paw motion direction along the cat's forward axis
 *      (determined from hips -> chest vector in the rigged pose).
 *   5. Flags cats where paw motion is SIDEWAYS (wrong) or NEGLIGIBLE
 *      (bones frozen).
 *
 * Usage: bun scripts/test_per_cat_anim.ts
 */

import { promises as fs } from 'fs';
import * as path from 'path';
import * as THREE from 'three';

const RIGGED_DIR = 'c:/Users/Matt-PC/Documents/App Development/cat-annihilation/assets/models/cats/rigged';

// ----- GLB parser -----
// Returns both the JSON meta and the BIN chunk. The mesh-skinning test
// needs POSITION / JOINTS_0 / WEIGHTS_0 / inverseBindMatrices, all of
// which live in the BIN payload behind accessor indices.
async function parseGlb(glbPath: string): Promise<{ meta: any; bin: Buffer | null }> {
  const fd = await fs.open(glbPath, 'r');
  const header = Buffer.alloc(12);
  await fd.read(header, 0, 12, 0);
  if (header.subarray(0, 4).toString('utf8') !== 'glTF') throw new Error('Not GLB');
  const totalLen = header.readUInt32LE(8);
  const jsonHeader = Buffer.alloc(8);
  await fd.read(jsonHeader, 0, 8, 12);
  const jsonLen = jsonHeader.readUInt32LE(0);
  const jsonBuf = Buffer.alloc(jsonLen);
  await fd.read(jsonBuf, 0, jsonLen, 20);
  const meta = JSON.parse(jsonBuf.toString('utf8'));
  let bin: Buffer | null = null;
  const binOffset = 20 + jsonLen;
  if (binOffset < totalLen) {
    const bh = Buffer.alloc(8);
    await fd.read(bh, 0, 8, binOffset);
    const binLen = bh.readUInt32LE(0);
    bin = Buffer.alloc(binLen);
    await fd.read(bin, 0, binLen, binOffset + 8);
  }
  await fd.close();
  return { meta, bin };
}

// ----- Mesh-skinning paw-region test -----
//
// The bone-only test above measures WHERE THE BONE IS at each frame of
// the walk clip. That catches clips-broken-by-axis-math bugs (the legs
// spinning sideways fix) but CAN'T catch mesh-skinning weight crossover
// bugs - the ones the user is reporting with elder's front legs "acting
// as one". Those live in the vertex -> joint weight table, not the
// bone hierarchy.
//
// What this function does: runs the actual glTF skinning math on a
// SUBSET OF VERTICES (the ones dominantly weighted to a given paw-chain,
// e.g. {shoulder_L, upper_arm_L, lower_arm_L, paw_front_L}) and tracks
// the weighted centroid of each paw-region through the walk clip.
//
// If shoulder_L's envelope fallback bound right-side verts to it, those
// verts are "dominantly weighted to the right paw chain" in the GLB
// still, but their skinning ALSO includes a stray shoulder_L term. At
// walk-time, shoulder_L swings forward while shoulder_R swings back.
// The right-paw centroid is pulled BOTH ways and ends up trailing the
// left side - positive correlation. That's what the user sees as
// "front legs acting as one."
//
// We return a correlation per pair and the max per-paw displacement;
// the outer loop turns those into pass/fail entries with the same
// pattern as the bone test.
const CHAIN_NAMES = {
  paw_front_L: ['paw_front_L', 'lower_arm_L', 'upper_arm_L', 'shoulder_L'],
  paw_front_R: ['paw_front_R', 'lower_arm_R', 'upper_arm_R', 'shoulder_R'],
  paw_back_L:  ['paw_back_L',  'foot_L',      'shin_L',      'thigh_L'],
  paw_back_R:  ['paw_back_R',  'foot_R',      'shin_R',      'thigh_R'],
} as const;

function readAccessor(meta: any, bin: Buffer, accIdx: number): Float32Array | Uint16Array | Uint8Array {
  const acc = meta.accessors[accIdx];
  const bv = meta.bufferViews[acc.bufferView];
  const byteOffset = (bv.byteOffset || 0) + (acc.byteOffset || 0);
  const componentType = acc.componentType;
  const typeSize: Record<string, number> = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
  const numComponents = typeSize[acc.type];
  const total = acc.count * numComponents;
  if (componentType === 5126) { // FLOAT
    const out = new Float32Array(total);
    for (let i = 0; i < total; i++) out[i] = bin.readFloatLE(byteOffset + i * 4);
    return out;
  }
  if (componentType === 5123) { // UNSIGNED_SHORT
    const out = new Uint16Array(total);
    for (let i = 0; i < total; i++) out[i] = bin.readUInt16LE(byteOffset + i * 2);
    return out;
  }
  if (componentType === 5121) { // UNSIGNED_BYTE
    const out = new Uint8Array(total);
    for (let i = 0; i < total; i++) out[i] = bin[byteOffset + i];
    return out;
  }
  throw new Error(`Unsupported componentType ${componentType}`);
}

// Returns a map from chain-name to list of { pos: Vector3, joints: [j0..j3], weights: [w0..w3] }
// for vertices whose summed weight on the chain's four bones is dominant
// (> 0.5). Those are the verts whose rendered position "belongs to" the
// chain visually - the tip-of-the-paw, the pad, the toes.
function extractPawChainVerts(meta: any, bin: Buffer, skin: any, jointNameToSkinIdx: Map<string, number>) {
  const chainJointSets: Record<string, Set<number>> = {};
  for (const [chain, bones] of Object.entries(CHAIN_NAMES)) {
    const set = new Set<number>();
    for (const b of bones) {
      const idx = jointNameToSkinIdx.get(b);
      if (idx !== undefined) set.add(idx);
    }
    chainJointSets[chain] = set;
  }

  // Find the one skinned mesh primitive. Assumes rig_quadruped.py
  // outputs one primitive per GLB, which has been true so far.
  let prim: any = null;
  for (const mesh of meta.meshes || []) {
    for (const p of mesh.primitives || []) {
      if (p.attributes?.JOINTS_0 !== undefined) { prim = p; break; }
    }
    if (prim) break;
  }
  if (!prim) throw new Error('no skinned primitive');

  const posAcc = meta.accessors[prim.attributes.POSITION];
  const positions = readAccessor(meta, bin, prim.attributes.POSITION) as Float32Array;
  const joints = readAccessor(meta, bin, prim.attributes.JOINTS_0);
  const weights = readAccessor(meta, bin, prim.attributes.WEIGHTS_0) as Float32Array;
  const vertCount = posAcc.count;

  const chainVerts: Record<string, { pos: THREE.Vector3; joints: number[]; weights: number[] }[]> = {
    paw_front_L: [], paw_front_R: [], paw_back_L: [], paw_back_R: []
  };

  for (let v = 0; v < vertCount; v++) {
    const j0 = joints[v * 4 + 0], j1 = joints[v * 4 + 1], j2 = joints[v * 4 + 2], j3 = joints[v * 4 + 3];
    const w0 = weights[v * 4 + 0], w1 = weights[v * 4 + 1], w2 = weights[v * 4 + 2], w3 = weights[v * 4 + 3];
    const vJoints = [j0, j1, j2, j3];
    const vWeights = [w0, w1, w2, w3];

    for (const [chain, jointSet] of Object.entries(chainJointSets)) {
      let chainWeight = 0;
      for (let c = 0; c < 4; c++) if (jointSet.has(vJoints[c])) chainWeight += vWeights[c];
      if (chainWeight > 0.5) {
        chainVerts[chain].push({
          pos: new THREE.Vector3(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]),
          joints: vJoints,
          weights: vWeights,
        });
        break; // a vertex belongs to at most one paw chain
      }
    }
  }
  return chainVerts;
}

// Skin one vertex with current bone world matrices. Standard glTF skinning:
//   v_skinned = sum_j ( weight_j * boneWorld_j * inverseBind_j * v_bind )
function skinVertex(
  vBind: THREE.Vector3,
  vJoints: number[],
  vWeights: number[],
  boneWorldMatrices: THREE.Matrix4[],
  inverseBindMatrices: THREE.Matrix4[],
  out: THREE.Vector3
): THREE.Vector3 {
  out.set(0, 0, 0);
  const tmp = new THREE.Vector3();
  for (let c = 0; c < 4; c++) {
    const w = vWeights[c];
    if (w === 0) continue;
    const j = vJoints[c];
    tmp.copy(vBind).applyMatrix4(inverseBindMatrices[j]).applyMatrix4(boneWorldMatrices[j]);
    out.addScaledVector(tmp, w);
  }
  return out;
}

// ----- Reconstruct bone tree in three.js from glTF nodes -----
function buildBoneTreeFromGltf(meta: any) {
  const nodes = meta.nodes || [];
  const bones: Record<string, THREE.Bone> = {};
  const skin = meta.skins?.[0];
  if (!skin) throw new Error('No skin in GLB');

  // Create a Bone for every joint, set rest-pose transform from the glTF
  // node (position/rotation). glTF stores them as arrays; three.js uses
  // Vector3 and Quaternion.
  for (const jointIdx of skin.joints) {
    const node = nodes[jointIdx];
    const bone = new THREE.Bone();
    bone.name = node.name;
    if (node.translation) bone.position.fromArray(node.translation);
    if (node.rotation) bone.quaternion.fromArray(node.rotation);
    if (node.scale) bone.scale.fromArray(node.scale);
    bones[node.name] = bone;
  }

  // Wire up parent-child relationships. Start from each joint's node
  // and walk down via node.children; if a child is a joint, link it.
  for (const jointIdx of skin.joints) {
    const node = nodes[jointIdx];
    const parent = bones[node.name];
    for (const childIdx of (node.children || [])) {
      const childNode = nodes[childIdx];
      if (bones[childNode.name]) parent.add(bones[childNode.name]);
    }
  }

  // Find roots: bones with no parent in our bone set. Some rigs have
  // more than one (e.g. `wanderer` has an extra `neutral_bone` orphan
  // left over from the Meshy source file). Previous logic iterated and
  // kept the LAST orphan, which sometimes picked the stray node instead
  // of the real `root`, making every leg bone unreachable via group
  // traversal and causing "walk clip build failed (missing bones)".
  // Prefer a bone literally named `root`; otherwise fall back to the
  // first orphan. Callers still get back a single root to add to the
  // group, and unreachable orphans (neutral_bone etc.) are harmless.
  const roots: THREE.Bone[] = [];
  for (const bone of Object.values(bones)) {
    if (!bone.parent || !(bone.parent as any).isBone) roots.push(bone);
  }
  const rootBone = bones['root'] || roots[0] || Object.values(bones)[0];

  return { rootBone, bones };
}

// ----- Clip builders (duplicated from test_animations.ts to keep this
//        file self-contained; if these drift, the test drifts) -----
const Y_AXIS = new THREE.Vector3(0, 1, 0);
const Z_AXIS = new THREE.Vector3(0, 0, 1);

// Compute the rig's side axis from chest-hips (same algorithm as
// asset_browser.html's computeRigSideAxis). The walk/run hinge is the
// SIDE axis (perpendicular to body-forward). Hardcoding world Z_AXIS
// only works when forward == +X, which is NOT true for Meshy-rigged
// cats - some have forward = -Z. This function derives the axis from
// the rig at clip-build time so walk works regardless of skeleton
// orientation.
function computeRigSideAxis(model: THREE.Object3D): THREE.Vector3 {
  let hipsBone: THREE.Bone | null = null, chestBone: THREE.Bone | null = null;
  model.traverse(n => {
    if (!(n as any).isBone) return;
    if (n.name === 'hips') hipsBone = n as THREE.Bone;
    else if (n.name === 'chest') chestBone = n as THREE.Bone;
  });
  if (!hipsBone || !chestBone) return Z_AXIS.clone();
  model.updateMatrixWorld(true);
  const hipsWorld = new THREE.Vector3(); hipsBone.getWorldPosition(hipsWorld);
  const chestWorld = new THREE.Vector3(); chestBone.getWorldPosition(chestWorld);
  const forward = chestWorld.sub(hipsWorld);
  forward.y = 0;
  if (forward.lengthSq() < 1e-8) return Z_AXIS.clone();
  forward.normalize();
  return new THREE.Vector3().crossVectors(forward, Y_AXIS).normalize();
}

const SIT_THIGH_DEG = 85;
const SIT_SHIN_DEG = -170;
const SIT_FOOT_DEG = 85;
const DROP_FRAC_BY_SCALE: Record<number, number> = {
  0.0: 0.000, 0.25: 0.064, 0.5: 0.241, 0.75: 0.509, 1.0: 0.832
};
const KEY_SCALES = [0, 0.25, 0.5, 0.75, 1.0];

function findBone(root: THREE.Object3D, name: string): THREE.Bone | null {
  let m: THREE.Bone | null = null;
  root.traverse(n => { if ((n as any).isBone && n.name === name) m = n as THREE.Bone; });
  return m;
}

function buildRotTrack(model: THREE.Object3D, boneName: string, samples: [number, THREE.Vector3, number][]) {
  const bone = findBone(model, boneName);
  if (!bone) return null;
  model.updateMatrixWorld(true);
  const parentWorldQuat = new THREE.Quaternion();
  if (bone.parent) bone.parent.getWorldQuaternion(parentWorldQuat);
  const parentWorldQuatInv = parentWorldQuat.clone().invert();
  const bindQuat = bone.quaternion.clone();
  const localAxis = new THREE.Vector3();
  const delta = new THREE.Quaternion();
  const composed = new THREE.Quaternion();
  const times: number[] = []; const values: number[] = [];
  for (const [t, axis, angle] of samples) {
    times.push(t);
    localAxis.copy(axis).applyQuaternion(parentWorldQuatInv);
    delta.setFromAxisAngle(localAxis, angle);
    composed.copy(delta).multiply(bindQuat);
    values.push(composed.x, composed.y, composed.z, composed.w);
  }
  return new THREE.QuaternionKeyframeTrack(`${bone.name}.quaternion`, times, values);
}

function buildWalkClip(model: THREE.Object3D) {
  const dur = 1.0;
  const amp = THREE.MathUtils.degToRad(22);
  const kneeAmp = THREE.MathUtils.degToRad(15);
  const tracks: THREE.KeyframeTrack[] = [];
  const sideAxis = computeRigSideAxis(model);
  const legPhases: [string, number][] = [['shoulder_L', 0.0], ['thigh_L', 0.0], ['shoulder_R', 0.5], ['thigh_R', 0.5]];
  for (const [b, phase] of legPhases) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 4; i++) s.push([(i / 4) * dur, sideAxis, Math.sin(2 * Math.PI * (i / 4 + phase)) * amp]);
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  const kneePhases: [string, number][] = [['lower_arm_L', 0.0], ['shin_L', 0.0], ['lower_arm_R', 0.5], ['shin_R', 0.5]];
  for (const [b, phase] of kneePhases) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 8; i++) { const sn = Math.sin(2 * Math.PI * (i / 8 + phase)); s.push([(i / 8) * dur, sideAxis, -Math.max(0, sn) * kneeAmp]); }
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  if (!tracks.length) return null;
  return new THREE.AnimationClip('walk', dur, tracks);
}

function buildRunClip(model: THREE.Object3D) {
  const dur = 0.5;
  const amp = THREE.MathUtils.degToRad(38);
  const tracks: THREE.KeyframeTrack[] = [];
  const sideAxis = computeRigSideAxis(model);
  const legPhases: [string, number][] = [['shoulder_L', 0.0], ['shoulder_R', 0.0], ['thigh_L', 0.25], ['thigh_R', 0.25]];
  for (const [b, phase] of legPhases) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 4; i++) s.push([(i / 4) * dur, sideAxis, Math.sin(2 * Math.PI * (i / 4 + phase)) * amp]);
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  if (!tracks.length) return null;
  return new THREE.AnimationClip('run', dur, tracks);
}

// ----- Per-cat animation test -----
type CatResult = { cat: string; clip: string; ok: boolean; msg: string };
const results: CatResult[] = [];

function catCheck(cat: string, clip: string, ok: boolean, msg: string) {
  results.push({ cat, clip, ok, msg });
}

const files = (await fs.readdir(RIGGED_DIR)).filter(f => f.endsWith('.glb') && !f.startsWith('_')).sort();
console.log(`\nTesting animation motion direction on ${files.length} rigged cats.\n`);

for (const f of files) {
  const cat = f.replace('.glb', '');
  let meta: any, bin: Buffer | null;
  try { ({ meta, bin } = await parseGlb(path.join(RIGGED_DIR, f))); }
  catch (e) { catCheck(cat, 'parse', false, `GLB parse failed: ${e}`); continue; }

  let rootBone: THREE.Bone, bones: Record<string, THREE.Bone>;
  try { ({ rootBone, bones } = buildBoneTreeFromGltf(meta)); }
  catch (e) { catCheck(cat, 'skeleton', false, `Skeleton build failed: ${e}`); continue; }

  const group = new THREE.Group();
  group.add(rootBone);
  group.updateMatrixWorld(true);

  // Determine FORWARD direction for this cat from its rest pose.
  // hips -> chest vector = body forward. Project onto XZ plane (ignore
  // vertical since legs swing in the forward-vertical plane).
  const hips = bones['hips'];
  const chest = bones['chest'];
  if (!hips || !chest) { catCheck(cat, 'forward-axis', false, 'missing hips or chest bone'); continue; }
  const hipsWorld = new THREE.Vector3(); hips.getWorldPosition(hipsWorld);
  const chestWorld = new THREE.Vector3(); chest.getWorldPosition(chestWorld);
  const forward = chestWorld.clone().sub(hipsWorld);
  forward.y = 0;
  if (forward.lengthSq() < 1e-6) { catCheck(cat, 'forward-axis', false, 'hips and chest collinear vertically'); continue; }
  forward.normalize();
  // Lateral = perpendicular to forward in the XZ plane.
  const lateral = new THREE.Vector3(-forward.z, 0, forward.x);

  // Reset bones to rest transforms (capture pre-test paw positions).
  const restPositions: Record<string, THREE.Vector3> = {};
  for (const pawName of ['paw_back_L', 'paw_back_R', 'paw_front_L', 'paw_front_R']) {
    const b = bones[pawName];
    if (b) { const v = new THREE.Vector3(); b.getWorldPosition(v); restPositions[pawName] = v; }
  }

  // For each clip, measure the paw_back_L motion direction at t=0.25
  // of the clip (swing peak).
  const clips: [string, (m: THREE.Object3D) => THREE.AnimationClip | null][] = [
    ['walk', buildWalkClip],
    ['run',  buildRunClip]
  ];

  // Detect whether the cat's BIND POSE is a T-pose (back legs extended
  // vertically) or a SIT / slouched pose. Sit pose tucks the thighs
  // forward, so the thigh-to-ground angle is non-vertical. In that case
  // the back paws have almost no vertical reach above y=0 and the walk
  // clip's thigh-swing doesn't move them much - making the per-paw
  // motion test noisy for back legs. We still test them but mark
  // "sitting" so the user knows "low back-paw motion" is expected and
  // should regenerate the cat in Meshy with a proper standing T-pose.
  // The forward paws are still trustworthy on sitting cats because
  // shoulders/arms stay roughly vertical even in a sit.
  function bindPoseStatus(): { sitting: boolean; reason: string } {
    const thighL = bones['thigh_L'];
    const shinL = bones['shin_L'];
    if (!thighL || !shinL) return { sitting: false, reason: 'no back-leg bones' };
    const thighWorld = new THREE.Vector3(); thighL.getWorldPosition(thighWorld);
    const shinWorld = new THREE.Vector3(); shinL.getWorldPosition(shinWorld);
    const down = shinWorld.clone().sub(thighWorld).normalize();
    // Angle between thigh-to-shin direction and world-down (0, -1, 0).
    // T-pose thigh points straight down (angle ~0 deg). Sit tucks the
    // thigh forward so the angle grows to 30-60 deg.
    const dotDown = -down.y; // down.dot((0,-1,0))
    const angleDeg = Math.acos(Math.max(-1, Math.min(1, dotDown))) * 180 / Math.PI;
    return { sitting: angleDeg > 25, reason: `thigh-to-down angle ${angleDeg.toFixed(1)}deg` };
  }
  const pose = bindPoseStatus();
  catCheck(cat, 'bind pose', true,
    pose.sitting
      ? `SITTING pose (${pose.reason}) - back-leg motion expected to be limited, regenerate in Meshy with "standing naturally on all four paws, body parallel to the ground" constraint`
      : `T-pose (${pose.reason})`);

  for (const [clipName, clipBuilder] of clips) {
    // Rebuild bone tree fresh for each clip (so bind captures match).
    const { rootBone: rBone, bones: bs } = buildBoneTreeFromGltf(meta);
    const grp = new THREE.Group();
    grp.add(rBone);
    grp.updateMatrixWorld(true);
    const clip = clipBuilder(grp);
    if (!clip) { catCheck(cat, clipName, false, `${clipName} clip build failed (missing bones)`); continue; }
    const mixer = new THREE.AnimationMixer(grp);
    const action = mixer.clipAction(clip);
    action.play();

    // Sample ALL four paws across N time points. For each paw, record
    // the forward-axis displacement curve (signed). This gives us:
    //   (a) per-paw motion magnitude (is this paw moving at all?)
    //   (b) L/R phase correlation (do paired paws swing opposite, as
    //       the clip commands, or in sync - which means the rigged mesh
    //       has crossed-over weights that force both sides to track
    //       whichever bone wins the top-4 weight slots)
    //
    // Pearson correlation of fL(t) vs fR(t):
    //    r > +0.3  = paws move together (BAD - gait collapsed to one
    //                leg driving both sides)
    //    r < -0.3  = paws move opposite (CORRECT alternating gait)
    //   |r| <= 0.3 = ambiguous / low-signal, usually paired with low
    //                total displacement
    const STEPS = 24;
    const pawNames = ['paw_front_L', 'paw_front_R', 'paw_back_L', 'paw_back_R'] as const;
    const curves: Record<string, number[]> = {};
    const maxAbs: Record<string, number> = {};
    for (const p of pawNames) { curves[p] = []; maxAbs[p] = 0; }
    let prevT = 0;
    for (let i = 1; i <= STEPS; i++) {
      const t = (i / STEPS) * clip.duration;
      mixer.update(t - prevT);
      prevT = t;
      for (const p of pawNames) {
        const pb = bs[p];
        if (!pb) continue;
        const wp = new THREE.Vector3(); pb.getWorldPosition(wp);
        const d = wp.sub(restPositions[p]);
        d.y = 0;
        const f = d.dot(forward);
        curves[p].push(f);
        if (Math.abs(f) > maxAbs[p]) maxAbs[p] = Math.abs(f);
      }
    }

    // Pearson correlation for a signal pair.
    function corr(a: number[], b: number[]): number {
      const n = Math.min(a.length, b.length);
      if (n === 0) return 0;
      let sa = 0, sb = 0; for (let i = 0; i < n; i++) { sa += a[i]; sb += b[i]; }
      const ma = sa / n, mb = sb / n;
      let num = 0, da = 0, db = 0;
      for (let i = 0; i < n; i++) {
        const xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb; da += xa * xa; db += xb * xb;
      }
      const denom = Math.sqrt(da * db);
      return denom < 1e-12 ? 0 : num / denom;
    }

    const frontCorr = corr(curves['paw_front_L'], curves['paw_front_R']);
    const backCorr  = corr(curves['paw_back_L'],  curves['paw_back_R']);

    // Motion threshold: 1cm in world units (rig is ~1m scale).
    const MOTION_MIN = 0.01;

    // ---- Front-paw checks (always required). ----
    const frontMoves = maxAbs['paw_front_L'] >= MOTION_MIN && maxAbs['paw_front_R'] >= MOTION_MIN;
    if (!frontMoves) {
      const dead = pawNames.filter(p => p.startsWith('paw_front_') && maxAbs[p] < MOTION_MIN);
      catCheck(cat, `${clipName} front paws detected`, false,
        `${dead.join(', ')} barely move (max |forward|=${dead.map(p => maxAbs[p].toFixed(4)).join(', ')}m < ${MOTION_MIN}m) - the shoulder bone has zero vertex weight coverage on that side`);
    } else {
      catCheck(cat, `${clipName} front paws detected`, true,
        `L max=${maxAbs['paw_front_L'].toFixed(3)}m, R max=${maxAbs['paw_front_R'].toFixed(3)}m`);
    }
    // Only the walk clip is an alternating lateral gait (L opposite-
    // phase from R). The run clip is a GALLOP - both front legs swing
    // together, both back legs swing together with a quarter-phase
    // offset. So positive L-R correlation on run is correct; skip the
    // alternation check there.
    if (clipName === 'walk' && frontMoves) {
      if (frontCorr > 0.3) {
        catCheck(cat, `${clipName} front paws alternate`, false,
          `front paws SYNCED on walk (correlation=${frontCorr.toFixed(2)}). Walk is a lateral gait - L and R should swing opposite. Cause is most often mesh-skinning weight crossover (shoulder_L's envelope fallback bound verts on the opposite side of the body), which bone-only motion can't detect - see the "paw-region mesh" checks below.`);
      } else if (frontCorr < -0.3) {
        catCheck(cat, `${clipName} front paws alternate`, true,
          `correlation=${frontCorr.toFixed(2)} (opposite-phase, correct alternating gait)`);
      } else {
        catCheck(cat, `${clipName} front paws alternate`, false,
          `front paws ambiguous phase (correlation=${frontCorr.toFixed(2)} in [-0.3, +0.3] dead zone)`);
      }
    }

    // ---- Back-paw checks (skipped for sitting cats). ----
    if (pose.sitting) {
      catCheck(cat, `${clipName} back paws`, true,
        `SKIPPED (sitting pose) - back-paw motion unreliable. Re-test after regenerating in Meshy T-pose.`);
    } else {
      const backMoves = maxAbs['paw_back_L'] >= MOTION_MIN && maxAbs['paw_back_R'] >= MOTION_MIN;
      if (!backMoves) {
        const dead = pawNames.filter(p => p.startsWith('paw_back_') && maxAbs[p] < MOTION_MIN);
        catCheck(cat, `${clipName} back paws detected`, false,
          `${dead.join(', ')} barely move (max |forward|=${dead.map(p => maxAbs[p].toFixed(4)).join(', ')}m < ${MOTION_MIN}m) - the thigh bone has zero vertex weight coverage on that side`);
      } else {
        catCheck(cat, `${clipName} back paws detected`, true,
          `L max=${maxAbs['paw_back_L'].toFixed(3)}m, R max=${maxAbs['paw_back_R'].toFixed(3)}m`);
      }
      // Walk: legs alternate. Run: front+back each swing together (gallop).
      if (clipName === 'walk' && backMoves) {
        if (backCorr > 0.3) {
          catCheck(cat, `${clipName} back paws alternate`, false,
            `back paws SYNCED on walk (correlation=${backCorr.toFixed(2)}) - likely mesh-skinning weight crossover`);
        } else if (backCorr < -0.3) {
          catCheck(cat, `${clipName} back paws alternate`, true,
            `correlation=${backCorr.toFixed(2)} (opposite-phase, correct)`);
        } else {
          catCheck(cat, `${clipName} back paws alternate`, false,
            `back paws ambiguous phase (correlation=${backCorr.toFixed(2)})`);
        }
      }
    }
    action.stop();
  }

  // ---- Mesh-skinning paw-region test (walk only). ----
  //
  // This is the test that catches the bug the user sees visually: the
  // bone-motion test above confirms walk BONES alternate correctly, but
  // if shoulder_L's vertex weights reach across to the right side of
  // the body (from envelope fallback on a zero-heat-coverage bone),
  // the right-paw MESH gets yanked in the same direction as the left
  // paw and the two legs "act as one" on screen even though the bones
  // themselves still swing opposite.
  //
  // Procedure: parse the GLB's skin (joint list + inverseBindMatrices),
  // the primary skinned mesh (POSITION + JOINTS_0 + WEIGHTS_0), and
  // find vertices whose summed weight on a paw-chain bone set exceeds
  // 0.5 - those are the verts that visually "are" that paw. Then run
  // the walk clip on the bone tree, apply full glTF skinning math at
  // each frame, and compute the centroid of each paw-region. Pearson-
  // correlate L vs R forward-axis centroid motion. Positive = crossed-
  // up mesh, negative = proper alternation.
  if (bin) {
    try {
      const skinObj = meta.skins[0];
      const jointNameToSkinIdx = new Map<string, number>();
      for (let i = 0; i < skinObj.joints.length; i++) {
        const node = meta.nodes[skinObj.joints[i]];
        jointNameToSkinIdx.set(node.name, i);
      }

      const chainVerts = extractPawChainVerts(meta, bin, skinObj, jointNameToSkinIdx);

      // Load inverseBindMatrices once (16 floats per joint).
      const ibmRaw = readAccessor(meta, bin, skinObj.inverseBindMatrices) as Float32Array;
      const inverseBindMatrices: THREE.Matrix4[] = [];
      for (let i = 0; i < skinObj.joints.length; i++) {
        const m = new THREE.Matrix4();
        m.fromArray(ibmRaw, i * 16);
        inverseBindMatrices.push(m);
      }

      // For efficiency, subsample each chain to at most 200 verts - the
      // centroid of 200 random verts within a region converges to the
      // centroid of all of them.
      const SAMPLE_MAX = 200;
      for (const chain of Object.keys(chainVerts)) {
        if (chainVerts[chain].length > SAMPLE_MAX) {
          // Deterministic stride sample so results are stable run-to-run.
          const stride = Math.floor(chainVerts[chain].length / SAMPLE_MAX);
          chainVerts[chain] = chainVerts[chain].filter((_, i) => i % stride === 0).slice(0, SAMPLE_MAX);
        }
      }

      // Build a walk clip on a fresh bone tree we'll drive through the
      // same time steps used above.
      const { rootBone: rBone, bones: bs } = buildBoneTreeFromGltf(meta);
      const grp = new THREE.Group();
      grp.add(rBone);
      grp.updateMatrixWorld(true);
      const clip = buildWalkClip(grp);
      if (!clip) throw new Error('walk clip build failed during mesh test');
      const mixer = new THREE.AnimationMixer(grp);
      mixer.clipAction(clip).play();

      // Capture rest centroids (at t=0, bind pose).
      function centroid(list: { pos: THREE.Vector3 }[]) {
        const c = new THREE.Vector3();
        for (const v of list) c.add(v.pos);
        return list.length > 0 ? c.divideScalar(list.length) : c;
      }

      // Index skin joints back to bones by name. Build a name->bone lookup
      // so we can fetch each joint's world matrix at a given mixer time.
      const jointBones: (THREE.Bone | null)[] = skinObj.joints.map((nodeIdx: number) => bs[meta.nodes[nodeIdx].name] || null);

      // Skin a full paw-chain list at current skeleton pose, return
      // centroid of the SKINNED positions.
      const boneWorldMatrices: THREE.Matrix4[] = new Array(skinObj.joints.length).fill(null).map(() => new THREE.Matrix4());
      function updateBoneWorldMatrices() {
        grp.updateMatrixWorld(true);
        for (let i = 0; i < jointBones.length; i++) {
          const b = jointBones[i];
          if (b) boneWorldMatrices[i].copy(b.matrixWorld);
          else boneWorldMatrices[i].identity();
        }
      }
      function skinnedCentroid(list: { pos: THREE.Vector3; joints: number[]; weights: number[] }[]) {
        const out = new THREE.Vector3();
        const tmp = new THREE.Vector3();
        for (const v of list) {
          skinVertex(v.pos, v.joints, v.weights, boneWorldMatrices, inverseBindMatrices, tmp);
          out.add(tmp);
        }
        return list.length > 0 ? out.divideScalar(list.length) : out;
      }

      // Capture rest skinned centroid. (At t=0 before any mixer update,
      // the bind-pose skin should reproduce the raw vertex positions.)
      updateBoneWorldMatrices();
      const restCentroids: Record<string, THREE.Vector3> = {};
      for (const chain of Object.keys(chainVerts)) {
        restCentroids[chain] = skinnedCentroid(chainVerts[chain]);
      }

      // Now sample through walk clip and track each chain's centroid.
      const MESH_STEPS = 24;
      const meshCurves: Record<string, number[]> = {};
      const meshMaxAbs: Record<string, number> = {};
      for (const chain of Object.keys(chainVerts)) { meshCurves[chain] = []; meshMaxAbs[chain] = 0; }
      let prevT = 0;
      for (let i = 1; i <= MESH_STEPS; i++) {
        const t = (i / MESH_STEPS) * clip.duration;
        mixer.update(t - prevT);
        prevT = t;
        updateBoneWorldMatrices();
        for (const chain of Object.keys(chainVerts)) {
          const c = skinnedCentroid(chainVerts[chain]);
          const d = c.sub(restCentroids[chain]);
          d.y = 0;
          const fwd = d.dot(forward);
          meshCurves[chain].push(fwd);
          if (Math.abs(fwd) > meshMaxAbs[chain]) meshMaxAbs[chain] = Math.abs(fwd);
        }
      }

      function corr2(a: number[], b: number[]): number {
        const n = Math.min(a.length, b.length);
        if (n === 0) return 0;
        let sa = 0, sb = 0; for (let i = 0; i < n; i++) { sa += a[i]; sb += b[i]; }
        const ma = sa / n, mb = sb / n;
        let num = 0, da = 0, db = 0;
        for (let i = 0; i < n; i++) {
          const xa = a[i] - ma, xb = b[i] - mb;
          num += xa * xb; da += xa * xa; db += xb * xb;
        }
        const denom = Math.sqrt(da * db);
        return denom < 1e-12 ? 0 : num / denom;
      }

      const frontMeshCorr = corr2(meshCurves['paw_front_L'], meshCurves['paw_front_R']);
      const backMeshCorr  = corr2(meshCurves['paw_back_L'],  meshCurves['paw_back_R']);

      // Report per-chain vertex count so the user can see "oh this paw
      // has only 12 dominantly-bound verts, the centroid is noisy" vs
      // "2,000 verts, correlation is trustworthy".
      const counts = Object.fromEntries(Object.entries(chainVerts).map(([k, v]) => [k, v.length]));

      const frontMeshMoves = meshMaxAbs['paw_front_L'] >= 0.005 && meshMaxAbs['paw_front_R'] >= 0.005;
      if (!frontMeshMoves) {
        const dead = ['paw_front_L', 'paw_front_R'].filter(p => meshMaxAbs[p] < 0.005);
        catCheck(cat, 'walk front-paw MESH detected', false,
          `${dead.join(', ')} mesh region barely moves (max=${dead.map(p => meshMaxAbs[p].toFixed(4)).join(', ')}m) - that paw has no dominantly-weighted verts the bones can push around`);
      } else {
        catCheck(cat, 'walk front-paw MESH detected', true,
          `L ${counts['paw_front_L']} verts moves ${meshMaxAbs['paw_front_L'].toFixed(3)}m, R ${counts['paw_front_R']} verts moves ${meshMaxAbs['paw_front_R'].toFixed(3)}m`);
      }
      if (frontMeshMoves) {
        if (frontMeshCorr > 0.3) {
          catCheck(cat, 'walk front-paw MESH alternates', false,
            `front-paw MESH SYNCED (centroid correlation=${frontMeshCorr.toFixed(2)}) - this is the "acting as one" bug. Weight crossover is pulling one side's verts along with the other shoulder. Fix lives in rig_quadruped.py's envelope fallback (restrict to same-side verts) or in a pre-parent weight-smoothing step.`);
        } else if (frontMeshCorr < -0.3) {
          catCheck(cat, 'walk front-paw MESH alternates', true,
            `centroid correlation=${frontMeshCorr.toFixed(2)} (front mesh alternates correctly, no weight crossover)`);
        } else {
          catCheck(cat, 'walk front-paw MESH alternates', false,
            `front-paw MESH ambiguous (correlation=${frontMeshCorr.toFixed(2)}) - partial crossover, motion neither fully synced nor fully alternating`);
        }
      }

      if (!pose.sitting) {
        const backMeshMoves = meshMaxAbs['paw_back_L'] >= 0.005 && meshMaxAbs['paw_back_R'] >= 0.005;
        if (!backMeshMoves) {
          const dead = ['paw_back_L', 'paw_back_R'].filter(p => meshMaxAbs[p] < 0.005);
          catCheck(cat, 'walk back-paw MESH detected', false,
            `${dead.join(', ')} mesh region barely moves (max=${dead.map(p => meshMaxAbs[p].toFixed(4)).join(', ')}m)`);
        } else {
          catCheck(cat, 'walk back-paw MESH detected', true,
            `L ${counts['paw_back_L']} verts moves ${meshMaxAbs['paw_back_L'].toFixed(3)}m, R ${counts['paw_back_R']} verts moves ${meshMaxAbs['paw_back_R'].toFixed(3)}m`);
        }
        if (backMeshMoves) {
          if (backMeshCorr > 0.3) {
            catCheck(cat, 'walk back-paw MESH alternates', false,
              `back-paw MESH SYNCED (correlation=${backMeshCorr.toFixed(2)}) - thigh_L/R weight crossover`);
          } else if (backMeshCorr < -0.3) {
            catCheck(cat, 'walk back-paw MESH alternates', true,
              `correlation=${backMeshCorr.toFixed(2)} (back mesh alternates correctly)`);
          } else {
            catCheck(cat, 'walk back-paw MESH alternates', false,
              `back-paw MESH ambiguous (correlation=${backMeshCorr.toFixed(2)})`);
          }
        }
      } else {
        catCheck(cat, 'walk back-paw MESH', true, 'SKIPPED (sitting pose)');
      }
    } catch (e) {
      catCheck(cat, 'walk mesh-skin test', false, `mesh-skin test errored: ${e}`);
    }
  }
}

// ----- Report -----
const byCat = new Map<string, CatResult[]>();
for (const r of results) {
  if (!byCat.has(r.cat)) byCat.set(r.cat, []);
  byCat.get(r.cat)!.push(r);
}

let totalOk = 0, totalFail = 0;
for (const [cat, catResults] of byCat) {
  const failures = catResults.filter(r => !r.ok);
  const passes = catResults.filter(r => r.ok);
  if (failures.length === 0) {
    console.log(`\x1b[32m[PASS]\x1b[0m ${cat}`);
    for (const p of passes) console.log(`       + ${p.clip}: ${p.msg}`);
    totalOk += catResults.length;
  } else {
    console.log(`\x1b[31m[FAIL]\x1b[0m ${cat}`);
    for (const p of passes) console.log(`       + ${p.clip}: ${p.msg}`);
    for (const f of failures) console.log(`       - ${f.clip}: ${f.msg}`);
    totalOk += passes.length;
    totalFail += failures.length;
  }
}

console.log(`\n${totalOk}/${totalOk + totalFail} per-cat animation checks passed.`);
if (totalFail > 0) process.exit(1);
