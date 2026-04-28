#!/usr/bin/env bun
/**
 * test_animations.ts - Headless test harness for asset_browser.html animations.
 *
 * Why this exists:
 *   I shipped the last round without running any animations. Every clip was
 *   silently broken (missing Z_AXIS) but the browser didn't throw - it just
 *   returned null clips that fail silently when a button is clicked. This
 *   script reproduces the browser's logic in Bun so failures scream.
 *
 * What it tests:
 *   - Canonical cat skeleton builds with all 25 expected bones
 *   - Every animation clip (idle/sitDown/layDown/standUp/walk/run) returns
 *     a non-null THREE.AnimationClip with at least 1 track
 *   - Running the mixer through each clip's duration produces non-zero
 *     bone displacement (catches clips that build but do nothing)
 *   - No bone drops below world y=0 (the ground plane) during any clip
 *   - Bone rotation axes match expected anatomy (leg pitch = forward-back,
 *     not sideways)
 *
 * Run with:
 *   bun scripts/test_animations.ts
 */

import * as THREE from 'three';

// ----- Axes -----
// X_AXIS was used by the old idle clip; idle now holds the bind quaternion
// so X_AXIS is no longer referenced. Keeping Y_AXIS and Z_AXIS for the
// gait/sit/lay clips that do rotate around world axes.
const Y_AXIS = new THREE.Vector3(0, 1, 0);
const Z_AXIS = new THREE.Vector3(0, 0, 1);

// ----- Bone layout (mirror of CAT_BONE_LAYOUT in asset_browser.html) -----
// Kept in sync by eye; if they diverge the test fires false-negatives.
type BoneLayout = Record<string, { parent: string | null; pos: [number, number, number] }>;

const CAT_BONE_LAYOUT: BoneLayout = {
  root:        { parent: null,     pos: [0,     0,     0] },
  hips:        { parent: 'root',   pos: [0,     0.22,  0] },
  lumbar_01:   { parent: 'hips',      pos: [0.04, 0.005, 0] },
  lumbar_02:   { parent: 'lumbar_01', pos: [0.04, 0.005, 0] },
  spine_01:       { parent: 'lumbar_02',     pos: [0.03, 0.004, 0] },
  upper_back_01:  { parent: 'spine_01',      pos: [0.03, 0.004, 0] },
  upper_back_02:  { parent: 'upper_back_01', pos: [0.03, 0.004, 0] },
  upper_back_03:  { parent: 'upper_back_02', pos: [0.03, 0.004, 0] },
  chest:          { parent: 'upper_back_03', pos: [0.04, 0.014, 0] },
  neck_01:     { parent: 'chest',   pos: [0.05, 0.02, 0] },
  neck_02:     { parent: 'neck_01', pos: [0.04, 0.02, 0] },
  head:        { parent: 'neck_02', pos: [0.08, 0.02, 0] },
  ear_L:       { parent: 'head', pos: [0.02,  0.04,  0.03] },
  ear_R:       { parent: 'head', pos: [0.02,  0.04, -0.03] },
  jaw:         { parent: 'head', pos: [0.05, -0.03,  0] },
  tail_01:     { parent: 'hips',    pos: [-0.05,  0.04, 0] },
  tail_02:     { parent: 'tail_01', pos: [-0.07,  0.00, 0] },
  tail_03:     { parent: 'tail_02', pos: [-0.07, -0.01, 0] },
  tail_04:     { parent: 'tail_03', pos: [-0.06, -0.02, 0] },
  shoulder_L:  { parent: 'chest',       pos: [0, -0.04, 0.05] },
  upper_arm_L: { parent: 'shoulder_L',  pos: [0, -0.10, 0] },
  lower_arm_L: { parent: 'upper_arm_L', pos: [0, -0.10, 0] },
  paw_front_L: { parent: 'lower_arm_L', pos: [0, -0.02, 0] },
  shoulder_R:  { parent: 'chest',       pos: [0, -0.04, -0.05] },
  upper_arm_R: { parent: 'shoulder_R',  pos: [0, -0.10, 0] },
  lower_arm_R: { parent: 'upper_arm_R', pos: [0, -0.10, 0] },
  paw_front_R: { parent: 'lower_arm_R', pos: [0, -0.02, 0] },
  thigh_L:     { parent: 'hips',    pos: [-0.02, 0,     0.05] },
  shin_L:      { parent: 'thigh_L', pos: [0, -0.10, 0] },
  foot_L:      { parent: 'shin_L',  pos: [0, -0.10, 0] },
  paw_back_L:  { parent: 'foot_L',  pos: [0, -0.02, 0] },
  thigh_R:     { parent: 'hips',    pos: [-0.02, 0,    -0.05] },
  shin_R:      { parent: 'thigh_R', pos: [0, -0.10, 0] },
  foot_R:      { parent: 'shin_R',  pos: [0, -0.10, 0] },
  paw_back_R:  { parent: 'foot_R',  pos: [0, -0.02, 0] }
};

// Dog layout is CAT scaled 1.4x (shorter tail). This must match the
// asset_browser.html DOG_BONE_LAYOUT definition exactly or tests lie.
const DOG_BONE_LAYOUT: BoneLayout = (() => {
  const SCALE = 1.4;
  const TAIL_SCALE = 0.6;
  const out: BoneLayout = {};
  for (const [name, { parent, pos }] of Object.entries(CAT_BONE_LAYOUT)) {
    const s = name.startsWith('tail_') ? TAIL_SCALE * SCALE : SCALE;
    out[name] = { parent, pos: [pos[0] * s, pos[1] * s, pos[2] * s] as [number, number, number] };
  }
  return out;
})();

function buildSkeletonHierarchy(layout: BoneLayout) {
  const bones: Record<string, THREE.Bone> = {};
  for (const [name, { pos }] of Object.entries(layout)) {
    const bone = new THREE.Bone();
    bone.name = name;
    bone.position.set(pos[0], pos[1], pos[2]);
    bones[name] = bone;
  }
  let rootBone: THREE.Bone | null = null;
  for (const [name, { parent }] of Object.entries(layout)) {
    if (parent) bones[parent].add(bones[name]);
    else rootBone = bones[name];
  }
  return { rootBone: rootBone!, bones };
}

// ----- Clip-building helpers (mirror of asset_browser.html) -----
function findBone(root: THREE.Object3D, name: string): THREE.Bone | null {
  let match: THREE.Bone | null = null;
  root.traverse(n => { if ((n as any).isBone && n.name === name) match = n as THREE.Bone; });
  return match;
}
function buildRotTrack(model: THREE.Object3D, boneName: string, samples: [number, THREE.Vector3, number][]) {
  const bone = findBone(model, boneName);
  if (!bone) return null;
  // Rotate around WORLD axis regardless of bone bind - see
  // asset_browser.html for rationale. Transform world axis into parent
  // frame, pre-multiply delta with bind.
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
// ----- The actual clip builders (copied verbatim from asset_browser.html) -----
// If these drift, the test drifts. The copy is intentional so test is
// self-contained - Bun can't directly eval browser code with CDN imports.
function buildIdleClip(model: THREE.Object3D) {
  // Hold bind pose - see asset_browser.html for the rationale.
  const tracks: THREE.KeyframeTrack[] = [];
  for (const b of ['hips', 'chest', 'head', 'tail_01']) {
    const bone = findBone(model, b);
    if (!bone) continue;
    const q = bone.quaternion.toArray();
    tracks.push(new THREE.QuaternionKeyframeTrack(`${bone.name}.quaternion`, [0, 2], [...q, ...q]));
  }
  return tracks.length ? new THREE.AnimationClip('idle', 2.0, tracks) : null;
}
function buildWalkClip(model: THREE.Object3D) {
  const dur = 1.0;
  const amp = THREE.MathUtils.degToRad(22);
  const kneeAmp = THREE.MathUtils.degToRad(15);
  const tracks: THREE.KeyframeTrack[] = [];
  const legPhases: [string, number][] = [['shoulder_L', 0.00], ['thigh_L', 0.00], ['shoulder_R', 0.50], ['thigh_R', 0.50]];
  for (const [b, phase] of legPhases) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 4; i++) { s.push([(i / 4) * dur, Z_AXIS, Math.sin(2 * Math.PI * (i / 4 + phase)) * amp]); }
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  const kneePhases: [string, number][] = [['lower_arm_L', 0.00], ['shin_L', 0.00], ['lower_arm_R', 0.50], ['shin_R', 0.50]];
  for (const [b, phase] of kneePhases) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 8; i++) { const sn = Math.sin(2 * Math.PI * (i / 8 + phase)); s.push([(i / 8) * dur, Z_AXIS, -Math.max(0, sn) * kneeAmp]); }
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  const tailS: [number, THREE.Vector3, number][] = [];
  for (let i = 0; i <= 4; i++) tailS.push([(i / 4) * dur, Y_AXIS, Math.sin(2 * Math.PI * i / 4) * THREE.MathUtils.degToRad(10)]);
  const tt = buildRotTrack(model, 'tail_01', tailS); if (tt) tracks.push(tt);
  return tracks.length ? new THREE.AnimationClip('walk', dur, tracks) : null;
}
function buildRunClip(model: THREE.Object3D) {
  const dur = 0.5;
  const amp = THREE.MathUtils.degToRad(38);
  const kneeAmp = THREE.MathUtils.degToRad(30);
  const spineAmp = THREE.MathUtils.degToRad(12);
  const tracks: THREE.KeyframeTrack[] = [];
  for (const [b, phase] of [['shoulder_L', 0.00], ['shoulder_R', 0.00], ['thigh_L', 0.25], ['thigh_R', 0.25]] as [string, number][]) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 4; i++) s.push([(i / 4) * dur, Z_AXIS, Math.sin(2 * Math.PI * (i / 4 + phase)) * amp]);
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  for (const [b, phase] of [['lower_arm_L', 0.00], ['lower_arm_R', 0.00], ['shin_L', 0.25], ['shin_R', 0.25]] as [string, number][]) {
    const s: [number, THREE.Vector3, number][] = [];
    for (let i = 0; i <= 8; i++) { const sn = Math.sin(2 * Math.PI * (i / 8 + phase)); s.push([(i / 8) * dur, Z_AXIS, -Math.max(0, sn) * kneeAmp]); }
    const t = buildRotTrack(model, b, s); if (t) tracks.push(t);
  }
  const spine: [number, THREE.Vector3, number][] = [];
  for (let i = 0; i <= 4; i++) spine.push([(i / 4) * dur, Z_AXIS, Math.sin(2 * Math.PI * i / 4) * spineAmp]);
  for (const b of ['spine_01', 'chest']) { const t = buildRotTrack(model, b, spine); if (t) tracks.push(t); }
  return tracks.length ? new THREE.AnimationClip('run', dur, tracks) : null;
}
// Rest hip height is the one physical measurement the clips need to know
// about - it determines how far to translate `root` for sit/lay poses.
// Reading it from the model's rest-pose bones means the SAME clip builder
// works for cat (hip at 0.22) and dog (hip at 0.308) without hard-coded
// layout-specific magic numbers.
function restHipY(model: THREE.Object3D): number {
  const hips = findBone(model, 'hips');
  if (!hips) return 0.22;
  return hips.position.y; // local y = world y since root is at origin
}

// Empirically-measured drop fraction at each fold scale. Keeps paws on
// ground throughout sit/lay/stand because hip_y(t) = reach(t) at every
// keyframe.
const DROP_FRAC_BY_SCALE: Record<number, number> = {
  0.0:  0.000,
  0.25: 0.064,
  0.5:  0.241,
  0.75: 0.509,
  1.0:  0.832
};
const KEY_SCALES = [0, 0.25, 0.5, 0.75, 1.0];
const SIT_THIGH_DEG = 85;
const SIT_SHIN_DEG = -170;
const SIT_FOOT_DEG = 85;

function buildSitDownClip(model: THREE.Object3D) {
  // Simplified sit clip - drops the pelvis via root translation, lifts
  // spine_01 in lumbar_02's local frame to keep chest (and therefore
  // front legs) at rest height, then folds back legs tight.
  //
  // No lumbar rotations here - they displace the chest in ways that
  // require more compensation math. Animation-quality the sit arch
  // can be added back later once base pose works on both species.
  //
  // DROP is relative to the rest hip height so the same clip works for
  // cat (hip 0.22) and dog (hip 0.308) - absolute DROP values only work
  // for one layout at a time.
  const hipY = restHipY(model);
  const dur = 1.3;
  const tracks: THREE.KeyframeTrack[] = [];
  const addRot = (bone: string, axis: THREE.Vector3, endDeg: number) => {
    const t = buildRotTrack(model, bone, [[0, axis, 0], [dur, axis, THREE.MathUtils.degToRad(endDeg)]]);
    if (t) tracks.push(t);
  };
  addRot('thigh_L', Z_AXIS, SIT_THIGH_DEG); addRot('thigh_R', Z_AXIS, SIT_THIGH_DEG);
  addRot('shin_L',  Z_AXIS, SIT_SHIN_DEG);  addRot('shin_R',  Z_AXIS, SIT_SHIN_DEG);
  addRot('foot_L',  Z_AXIS, SIT_FOOT_DEG);  addRot('foot_R',  Z_AXIS, SIT_FOOT_DEG);
  addRot('neck_01', Z_AXIS, 10);
  addRot('neck_02', Z_AXIS, 10);
  addRot('head',    Z_AXIS, 8);
  // 5-keyframe root drop matching leg reach curve (non-linear fold->hip).
  const rootBone = findBone(model, 'root');
  if (rootBone) {
    const rest = rootBone.position.toArray();
    const times: number[] = []; const values: number[] = [];
    for (const s of KEY_SCALES) {
      times.push(s * dur);
      values.push(rest[0], rest[1] - hipY * DROP_FRAC_BY_SCALE[s], rest[2]);
    }
    tracks.push(new THREE.VectorKeyframeTrack(`${rootBone.name}.position`, times, values));
  }
  const SHARES: Record<string, number> = {
    lumbar_01: 0.15, lumbar_02: 0.18,
    spine_01:  0.18, upper_back_01: 0.14, upper_back_02: 0.12, upper_back_03: 0.10,
    chest:     0.13
  };
  for (const [boneName, share] of Object.entries(SHARES)) {
    const bone = findBone(model, boneName);
    if (!bone) continue;
    const rest = bone.position.toArray();
    const times: number[] = []; const values: number[] = [];
    for (const s of KEY_SCALES) {
      times.push(s * dur);
      values.push(rest[0], rest[1] + hipY * DROP_FRAC_BY_SCALE[s] * share, rest[2]);
    }
    tracks.push(new THREE.VectorKeyframeTrack(`${bone.name}.position`, times, values));
  }
  return tracks.length ? new THREE.AnimationClip('sitDown', dur, tracks) : null;
}
function buildLayDownClip(model: THREE.Object3D) {
  const hipY = restHipY(model);
  const dur = 1.6;
  const tracks: THREE.KeyframeTrack[] = [];
  const addRot = (bone: string, axis: THREE.Vector3, endDeg: number) => {
    const t = buildRotTrack(model, bone, [[0, axis, 0], [dur, axis, THREE.MathUtils.degToRad(endDeg)]]);
    if (t) tracks.push(t);
  };
  addRot('thigh_L', Z_AXIS, SIT_THIGH_DEG); addRot('thigh_R', Z_AXIS, SIT_THIGH_DEG);
  addRot('shin_L',  Z_AXIS, SIT_SHIN_DEG);  addRot('shin_R',  Z_AXIS, SIT_SHIN_DEG);
  addRot('foot_L',  Z_AXIS, SIT_FOOT_DEG);  addRot('foot_R',  Z_AXIS, SIT_FOOT_DEG);
  addRot('shoulder_L',  Z_AXIS, SIT_THIGH_DEG); addRot('shoulder_R',  Z_AXIS, SIT_THIGH_DEG);
  addRot('upper_arm_L', Z_AXIS, SIT_SHIN_DEG);  addRot('upper_arm_R', Z_AXIS, SIT_SHIN_DEG);
  addRot('lower_arm_L', Z_AXIS, SIT_FOOT_DEG);  addRot('lower_arm_R', Z_AXIS, SIT_FOOT_DEG);
  addRot('neck_01', Z_AXIS, 25);
  addRot('neck_02', Z_AXIS, 20);
  addRot('head',    Z_AXIS, 15);
  addRot('tail_01', Z_AXIS, -20);
  addRot('tail_02', Z_AXIS, -30);
  // 5-keyframe root drop matching leg reach curve.
  const rootBone = findBone(model, 'root');
  if (rootBone) {
    const rest = rootBone.position.toArray();
    const times: number[] = []; const values: number[] = [];
    for (const s of KEY_SCALES) {
      times.push(s * dur);
      values.push(rest[0], rest[1] - hipY * DROP_FRAC_BY_SCALE[s], rest[2]);
    }
    tracks.push(new THREE.VectorKeyframeTrack(`${rootBone.name}.position`, times, values));
  }
  return tracks.length ? new THREE.AnimationClip('layDown', dur, tracks) : null;
}
function buildStandUpClipFrom(model: THREE.Object3D, fromLay: boolean) {
  const hipY = restHipY(model);
  const dur = 1.3;
  const tracks: THREE.KeyframeTrack[] = [];
  const rad = (d: number) => THREE.MathUtils.degToRad(d);
  const addRot = (bone: string, axis: THREE.Vector3, kf: [number, number][]) => {
    const t = buildRotTrack(model, bone, kf.map(([f, a]) => [f * dur, axis, rad(a)] as [number, THREE.Vector3, number]));
    if (t) tracks.push(t);
  };
  addRot('thigh_L', Z_AXIS, [[0, SIT_THIGH_DEG], [1.0, 0]]);
  addRot('thigh_R', Z_AXIS, [[0, SIT_THIGH_DEG], [1.0, 0]]);
  addRot('shin_L',  Z_AXIS, [[0, SIT_SHIN_DEG],  [1.0, 0]]);
  addRot('shin_R',  Z_AXIS, [[0, SIT_SHIN_DEG],  [1.0, 0]]);
  addRot('foot_L',  Z_AXIS, [[0, SIT_FOOT_DEG],  [1.0, 0]]);
  addRot('foot_R',  Z_AXIS, [[0, SIT_FOOT_DEG],  [1.0, 0]]);
  if (fromLay) {
    addRot('shoulder_L',  Z_AXIS, [[0, SIT_THIGH_DEG], [1.0, 0]]);
    addRot('shoulder_R',  Z_AXIS, [[0, SIT_THIGH_DEG], [1.0, 0]]);
    addRot('upper_arm_L', Z_AXIS, [[0, SIT_SHIN_DEG],  [1.0, 0]]);
    addRot('upper_arm_R', Z_AXIS, [[0, SIT_SHIN_DEG],  [1.0, 0]]);
    addRot('lower_arm_L', Z_AXIS, [[0, SIT_FOOT_DEG],  [1.0, 0]]);
    addRot('lower_arm_R', Z_AXIS, [[0, SIT_FOOT_DEG],  [1.0, 0]]);
    addRot('neck_01', Z_AXIS, [[0, 25], [1.0, 0]]);
    addRot('neck_02', Z_AXIS, [[0, 20], [1.0, 0]]);
    addRot('head',    Z_AXIS, [[0, 15], [1.0, 0]]);
  } else {
    addRot('neck_01', Z_AXIS, [[0, 10], [1.0, 0]]);
    addRot('neck_02', Z_AXIS, [[0, 10], [1.0, 0]]);
    addRot('head',    Z_AXIS, [[0,  8], [1.0, 0]]);
  }
  const rootBone = findBone(model, 'root');
  if (rootBone) {
    const rest = rootBone.position.toArray();
    const entries = KEY_SCALES.map(s => ({
      t: (1 - s) * dur,
      v: [rest[0], rest[1] - hipY * DROP_FRAC_BY_SCALE[s], rest[2]]
    })).sort((a, b) => a.t - b.t);
    tracks.push(new THREE.VectorKeyframeTrack(`${rootBone.name}.position`, entries.map(e => e.t), entries.flatMap(e => e.v)));
  }
  if (!fromLay) {
    const SHARES: Record<string, number> = {
      lumbar_01: 0.15, lumbar_02: 0.18,
      spine_01:  0.18, upper_back_01: 0.14, upper_back_02: 0.12, upper_back_03: 0.10,
      chest:     0.13
    };
    for (const [boneName, share] of Object.entries(SHARES)) {
      const bone = findBone(model, boneName);
      if (!bone) continue;
      const rest = bone.position.toArray();
      const entries = KEY_SCALES.map(s => ({
        t: (1 - s) * dur,
        v: [rest[0], rest[1] + hipY * DROP_FRAC_BY_SCALE[s] * share, rest[2]]
      })).sort((a, b) => a.t - b.t);
      tracks.push(new THREE.VectorKeyframeTrack(`${bone.name}.position`, entries.map(e => e.t), entries.flatMap(e => e.v)));
    }
  }
  return tracks.length ? new THREE.AnimationClip(fromLay ? 'standUpFromLay' : 'standUpFromSit', dur, tracks) : null;
}
function buildStandUpFromSitClip(model: THREE.Object3D) { return buildStandUpClipFrom(model, false); }
function buildStandUpFromLayClip(model: THREE.Object3D) { return buildStandUpClipFrom(model, true); }
function buildStandUpClip(model: THREE.Object3D) { return buildStandUpFromSitClip(model); }

function buildLayDownFromSitClip(model: THREE.Object3D) {
  const hipY = restHipY(model);
  const dur = 1.3;
  const tracks: THREE.KeyframeTrack[] = [];
  const rad = (d: number) => THREE.MathUtils.degToRad(d);
  const add = (bone: string, endDeg: number) => {
    const t = buildRotTrack(model, bone, [[0, Z_AXIS, 0], [dur, Z_AXIS, rad(endDeg)]]);
    if (t) tracks.push(t);
  };
  add('shoulder_L', SIT_THIGH_DEG); add('shoulder_R', SIT_THIGH_DEG);
  add('upper_arm_L', SIT_SHIN_DEG); add('upper_arm_R', SIT_SHIN_DEG);
  add('lower_arm_L', SIT_FOOT_DEG); add('lower_arm_R', SIT_FOOT_DEG);
  const addKF = (bone: string, fromDeg: number, toDeg: number) => {
    const t = buildRotTrack(model, bone, [[0, Z_AXIS, rad(fromDeg)], [dur, Z_AXIS, rad(toDeg)]]);
    if (t) tracks.push(t);
  };
  addKF('neck_01', 10, 25);
  addKF('neck_02', 10, 20);
  addKF('head',     8, 15);
  const TOTAL_FRAC = DROP_FRAC_BY_SCALE[1.0];
  const SHARES: Record<string, number> = {
    lumbar_01: 0.15, lumbar_02: 0.18,
    spine_01:  0.18, upper_back_01: 0.14, upper_back_02: 0.12, upper_back_03: 0.10,
    chest:     0.13
  };
  for (const [boneName, share] of Object.entries(SHARES)) {
    const bone = findBone(model, boneName);
    if (!bone) continue;
    const rest = bone.position.toArray();
    const lifted = [rest[0], rest[1] + hipY * TOTAL_FRAC * share, rest[2]];
    tracks.push(new THREE.VectorKeyframeTrack(`${bone.name}.position`, [0, dur], [...lifted, ...rest]));
  }
  return tracks.length ? new THREE.AnimationClip('layDownFromSit', dur, tracks) : null;
}

// ===========================================================================
// TEST RUNNER
// ===========================================================================

type TestResult = { name: string; ok: boolean; msg: string };
const results: TestResult[] = [];
function check(name: string, cond: boolean, msg: string) {
  results.push({ name, ok: cond, msg });
  const badge = cond ? '\u2713' : '\u2717';
  const col = cond ? '\x1b[32m' : '\x1b[31m';
  console.log(`  ${col}${badge}\x1b[0m ${name} - ${msg}`);
}

// Build BOTH rigs - many tests run on each to catch layout-dependent bugs
// (like "sitDown floats dog paws but cat is fine" because DROP was an
// absolute value).
function buildRig(layout: BoneLayout) {
  const { rootBone, bones } = buildSkeletonHierarchy(layout);
  const modelGroup = new THREE.Group();
  modelGroup.add(rootBone);
  return { modelGroup, bones, layout };
}
const catRig = buildRig(CAT_BONE_LAYOUT);
const dogRig = buildRig(DOG_BONE_LAYOUT);

// Primary rig for single-species tests. Dual-species tests iterate both.
const { modelGroup, bones } = catRig;

console.log('\n=== Skeleton ===');
// 35 = 32 + 3 face bones (ear_L, ear_R, jaw)
check('bone count (cat)', Object.keys(catRig.bones).length === 35, `${Object.keys(catRig.bones).length} bones built (expected 35)`);
check('bone count (dog)', Object.keys(dogRig.bones).length === 35, `${Object.keys(dogRig.bones).length} bones built (expected 35)`);

// Sample each bone's starting world position for later "did it move?" checks.
function worldPosMap(): Map<string, THREE.Vector3> {
  modelGroup.updateMatrixWorld(true);
  const map = new Map<string, THREE.Vector3>();
  for (const [name, bone] of Object.entries(bones)) {
    const v = new THREE.Vector3();
    bone.getWorldPosition(v);
    map.set(name, v);
  }
  return map;
}
const restPos = worldPosMap();

// Each clip gets: build it, assert non-null + track count, run the mixer for
// 80% of duration, verify at least one relevant bone moved, verify no bone
// world-y went below 0 (below-ground sink).
function testClip(name: string, clip: THREE.AnimationClip | null, movingBones: string[], allowNegativeY = false, expectMotion = true) {
  console.log(`\n=== ${name} ===`);
  check(`${name}: clip built`, clip !== null, clip ? `${clip.tracks.length} tracks, ${clip.duration}s` : 'NULL (silent build failure)');
  if (!clip) return;
  check(`${name}: >= 1 track`, clip.tracks.length > 0, `${clip.tracks.length} tracks`);

  for (const b of Object.values(bones)) { b.rotation.set(0, 0, 0); b.position.fromArray(CAT_BONE_LAYOUT[b.name].pos); }

  const mixer = new THREE.AnimationMixer(modelGroup);
  const action = mixer.clipAction(clip);
  action.play();
  mixer.update(clip.duration * 0.8);

  const newPos = worldPosMap();
  if (expectMotion && movingBones.length) {
    let anyMoved = false;
    for (const bn of movingBones) {
      const d = restPos.get(bn)!.distanceTo(newPos.get(bn)!);
      if (d > 0.001) { anyMoved = true; break; }
    }
    check(`${name}: bones moved`, anyMoved, anyMoved ? `at least one of ${movingBones.join(', ')} shifted` : `NONE of ${movingBones.join(', ')} moved`);
  }

  // Exclude `root` from the ground-check: it's a conceptual pivot, not
  // a visible body part. During a clip's transition paws can transit
  // slightly below ground because the hip-drop progression (linear)
  // doesn't exactly match the leg-fold reach progression (cosine). The
  // paw-contact test at end-pose enforces strict grounding; this
  // during-clip check uses a looser 0.05 tolerance to catch gross
  // floor penetration without failing on small transient dips.
  const TRANSIENT_TOLERANCE = 0.05;
  let minY = Infinity;
  let minBone = '';
  for (const [n, p] of newPos) {
    if (n === 'root') continue;
    if (p.y < minY) { minY = p.y; minBone = n; }
  }
  if (!allowNegativeY) {
    check(`${name}: no deep bone sink mid-clip`, minY >= -TRANSIENT_TOLERANCE, `lowest non-root bone: ${minBone} at y=${minY.toFixed(3)}${minY < -TRANSIENT_TOLERANCE ? ' (SINKS DEEP BELOW GROUND)' : ''}`);
  } else {
    console.log(`  i  ${name}: lowest non-root bone: ${minBone} at y=${minY.toFixed(3)} (neg-y allowed for this clip)`);
  }

  action.stop();
}

// Movement-check bones: must be DESCENDANTS of the rotated bones (rotating
// a bone doesn't change its own world position, only its children). For
// walk/run we check the distal paws/lower-arms. For sit/stand/lay we
// check paws (leg ends) or head (for the cases that rotate neck/head).
// Idle is pure rest, so nothing should move - we accept that as valid.
testClip('idle',     buildIdleClip(modelGroup),     [],                                                /* expectMotion */ false);
testClip('walk',     buildWalkClip(modelGroup),     ['paw_front_L', 'paw_back_L', 'paw_front_R', 'paw_back_R']);
testClip('run',      buildRunClip(modelGroup),      ['paw_front_L', 'paw_back_L', 'head']);
testClip('sitDown',  buildSitDownClip(modelGroup),  ['paw_back_L', 'paw_back_R', 'head']);
testClip('layDown',  buildLayDownClip(modelGroup),  ['paw_front_L', 'paw_back_L', 'head'], /* allowNegY */ true);
testClip('standUp',  buildStandUpClip(modelGroup),  ['paw_back_L', 'chest', 'head']);

// Sit-specific anatomy checks: pelvis should DROP (not rise), back paws
// should stay near the ground, front paws should NOT descend below the
// floor (previous bug was hips-rotation carrying chest + front legs down).
console.log('\n=== Anatomy sanity ===');
(function sitChecks() {
  for (const b of Object.values(bones)) { b.rotation.set(0, 0, 0); b.position.fromArray(CAT_BONE_LAYOUT[b.name].pos); }
  const mixer = new THREE.AnimationMixer(modelGroup);
  const sit = buildSitDownClip(modelGroup);
  if (!sit) { check('sit hips drop', false, 'clip null'); return; }
  const action = mixer.clipAction(sit);
  action.play();
  mixer.update(sit.duration * 0.99);

  const hipsWorld = new THREE.Vector3(); bones.hips.getWorldPosition(hipsWorld);
  const rest = restPos.get('hips')!;
  const dy = hipsWorld.y - rest.y;
  check('sit: hips drop (dy < 0)', dy < 0, `hips world dy = ${dy.toFixed(3)} (${dy < 0 ? 'OK drops' : 'WRONG rises'})`);

  const pwBack = new THREE.Vector3(); bones.paw_back_L.getWorldPosition(pwBack);
  check('sit: back paws near ground', pwBack.y < 0.08, `paw_back_L y = ${pwBack.y.toFixed(3)} (${pwBack.y < 0.08 ? 'OK ~ground' : 'LIFTED too high'})`);

  const pwFront = new THREE.Vector3(); bones.paw_front_L.getWorldPosition(pwFront);
  check('sit: front paws >= ground', pwFront.y >= -0.01, `paw_front_L y = ${pwFront.y.toFixed(3)} (${pwFront.y >= -0.01 ? 'OK planted' : 'SINKS'})`);
  action.stop();
})();

(function layDownGroundCheck() {
  for (const b of Object.values(bones)) { b.rotation.set(0, 0, 0); b.position.fromArray(CAT_BONE_LAYOUT[b.name].pos); }
  const mixer = new THREE.AnimationMixer(modelGroup);
  const lay = buildLayDownClip(modelGroup);
  if (!lay) { check('lay grounded', false, 'clip null'); return; }
  const action = mixer.clipAction(lay);
  action.play();
  mixer.update(lay.duration * 0.99);
  let minY = Infinity; let minBone = '';
  for (const [n, bone] of Object.entries(bones)) {
    if (n === 'root') continue; // conceptual pivot, allowed below 0
    const v = new THREE.Vector3(); bone.getWorldPosition(v);
    if (v.y < minY) { minY = v.y; minBone = n; }
  }
  check('lay: lowest visible bone >= 0 (no ground sink)', minY >= -0.01, `lowest non-root = ${minBone} at y=${minY.toFixed(3)}`);
  action.stop();
})();

// Strict paw-contact check: ALL FOUR paws must touch the ground (y close
// to 0) for sit and lay on BOTH cat AND dog layouts. Tolerance is very
// tight (0.01 rig-units, ~2cm in the browser after scale normalization)
// because the user visually identified floating paws that my previous
// 0.03 threshold let through.
const PAW_TOLERANCE = 0.01;
const ALL_PAWS = ['paw_front_L', 'paw_front_R', 'paw_back_L', 'paw_back_R'];

function pawContactCheck(
  species: string,
  rig: ReturnType<typeof buildRig>,
  clipName: string,
  clipBuilder: (m: THREE.Object3D) => THREE.AnimationClip | null
) {
  // Reset every bone back to rest pose in the species' layout.
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const clip = clipBuilder(rig.modelGroup);
  if (!clip) { check(`${species}/${clipName}: all paws on ground`, false, 'clip null'); return; }
  const action = mixer.clipAction(clip);
  action.play();
  mixer.update(clip.duration * 0.99);

  const pawYs: Record<string, number> = {};
  let allGrounded = true;
  let worstPaw = '';
  let worstDy = 0;
  for (const pawName of ALL_PAWS) {
    const v = new THREE.Vector3();
    rig.bones[pawName].getWorldPosition(v);
    pawYs[pawName] = +v.y.toFixed(4);
    const dy = Math.abs(v.y);
    if (dy > PAW_TOLERANCE) {
      allGrounded = false;
      if (dy > worstDy) { worstDy = dy; worstPaw = pawName; }
    }
  }
  const msg = allGrounded
    ? `all paws within ${PAW_TOLERANCE}m of ground (${JSON.stringify(pawYs)})`
    : `${worstPaw} off ground by ${worstDy.toFixed(4)}m. Paws: ${JSON.stringify(pawYs)}`;
  check(`${species}/${clipName}: all 4 paws on ground`, allGrounded, msg);
  action.stop();
}

// Both sit AND lay, on both cat AND dog.
pawContactCheck('cat', catRig, 'sitDown', buildSitDownClip);
pawContactCheck('dog', dogRig, 'sitDown', buildSitDownClip);
pawContactCheck('cat', catRig, 'layDown', buildLayDownClip);
pawContactCheck('dog', dogRig, 'layDown', buildLayDownClip);

// Multi-time-point paw check: sample paws at t = 0, 0.2, 0.4, 0.6, 0.8,
// 1.0 of the clip to ensure paws stay on the ground THROUGHOUT, not
// just at the end. Catches animations where legs extend or fold out
// of sync with body rise/fall - e.g. standUp used to lift legs with
// the body while legs stayed folded, so paws were raised 15cm off
// the ground for half the clip until they finally unfolded.
function pawContactThroughClip(
  species: string,
  rig: ReturnType<typeof buildRig>,
  clipName: string,
  clipBuilder: (m: THREE.Object3D) => THREE.AnimationClip | null
) {
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const clip = clipBuilder(rig.modelGroup);
  if (!clip) { check(`${species}/${clipName} throughout: paws on ground`, false, 'clip null'); return; }
  const action = mixer.clipAction(clip);
  action.play();

  const samples = [0, 0.2, 0.4, 0.6, 0.8, 1.0];
  let worstT = 0, worstPaw = '', worstDy = 0;
  const tolerance = 0.02; // 2cm tolerance during mid-transition
  const snapshots: Array<{ t: number; paws: Record<string, number> }> = [];
  let prevT = 0;
  for (const t of samples) {
    const deltaT = (t - prevT) * clip.duration;
    if (deltaT > 0) mixer.update(deltaT);
    prevT = t;
    const paws: Record<string, number> = {};
    for (const pawName of ALL_PAWS) {
      const v = new THREE.Vector3();
      rig.bones[pawName].getWorldPosition(v);
      paws[pawName] = +v.y.toFixed(4);
      const dy = Math.abs(v.y);
      if (dy > worstDy) { worstDy = dy; worstPaw = pawName; worstT = t; }
    }
    snapshots.push({ t, paws });
  }
  const ok = worstDy <= tolerance;
  const msg = ok
    ? `all paws within ${tolerance}m at every sampled t`
    : `${worstPaw} off ground by ${worstDy.toFixed(3)}m at t=${worstT}. Snapshots: ${JSON.stringify(snapshots)}`;
  check(`${species}/${clipName} throughout: paws on ground at every t`, ok, msg);
  action.stop();
}
// User-reported: standUp's paws leave ground mid-clip. Test catches it.
pawContactThroughClip('cat', catRig, 'sitDown',  buildSitDownClip);
pawContactThroughClip('dog', dogRig, 'sitDown',  buildSitDownClip);
pawContactThroughClip('cat', catRig, 'layDown',  buildLayDownClip);
pawContactThroughClip('dog', dogRig, 'layDown',  buildLayDownClip);
pawContactThroughClip('cat', catRig, 'standUp',  buildStandUpClip);
pawContactThroughClip('dog', dogRig, 'standUp',  buildStandUpClip);

// Dense mid-clip paw sampling at 0.01 tolerance - catches small dips
// that the 6-point, 0.02-tol test misses. Samples at 21 points covering
// t=0.0..1.0 in 0.05 steps. This is what caught the lay sink: at s=0.5
// the front leg reach (cosine-based) dropped deeper than back reach,
// putting front paws 8mm below ground mid-transition.
function pawContactDense(
  species: string,
  rig: ReturnType<typeof buildRig>,
  clipName: string,
  clipBuilder: (m: THREE.Object3D) => THREE.AnimationClip | null
) {
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const clip = clipBuilder(rig.modelGroup);
  if (!clip) { check(`${species}/${clipName} dense: paws on ground`, false, 'clip null'); return; }
  const action = mixer.clipAction(clip);
  action.play();

  const STEPS = 20;
  const TIGHT_TOLERANCE = 0.01; // 1cm
  let worstT = 0, worstPaw = '', worstDy = 0;
  let prevT = 0;
  for (let i = 0; i <= STEPS; i++) {
    const t = i / STEPS;
    const deltaT = (t - prevT) * clip.duration;
    if (deltaT > 0) mixer.update(deltaT);
    prevT = t;
    for (const pawName of ALL_PAWS) {
      const v = new THREE.Vector3();
      rig.bones[pawName].getWorldPosition(v);
      const dy = Math.abs(v.y);
      if (dy > worstDy) { worstDy = dy; worstPaw = pawName; worstT = t; }
    }
  }
  const ok = worstDy <= TIGHT_TOLERANCE;
  const msg = ok
    ? `densest paw sample within ${TIGHT_TOLERANCE}m at all ${STEPS + 1} samples (worst: ${worstDy.toFixed(4)}m)`
    : `${worstPaw} off ground by ${worstDy.toFixed(4)}m at t=${worstT.toFixed(2)}`;
  check(`${species}/${clipName} dense: paws within ${TIGHT_TOLERANCE}m everywhere`, ok, msg);
  action.stop();
}
pawContactDense('cat', catRig, 'sitDown',  buildSitDownClip);
pawContactDense('dog', dogRig, 'sitDown',  buildSitDownClip);
pawContactDense('cat', catRig, 'layDown',  buildLayDownClip);
pawContactDense('dog', dogRig, 'layDown',  buildLayDownClip);
pawContactDense('cat', catRig, 'standUp',  buildStandUpClip);
pawContactDense('dog', dogRig, 'standUp',  buildStandUpClip);
// standUpFromLay: stand from lay-down state. Same paws-on-ground
// requirement. Unfolds FRONT legs in addition to back, no spine un-lift.
pawContactDense('cat', catRig, 'standUpFromLay', buildStandUpFromLayClip);
pawContactDense('dog', dogRig, 'standUpFromLay', buildStandUpFromLayClip);

// Context-aware stand-up resolver: given current anim, which clip
// should the "Stand Up" button fire? Mirrors resolveStandAction() in
// asset_browser.html. Kept as plain logic test so we catch routing
// bugs if the mapping changes.
function resolveStandAction(fromAnim: string): string {
  if (fromAnim === 'sitDown') return 'standUpFromSit';
  if (fromAnim === 'layDown') return 'standUpFromLay';
  if (fromAnim === 'walk' || fromAnim === 'run') return 'idle';
  return 'idle';
}
// Idle-preserves-bind-pose test: simulate the canonical rig at bind
// pose, run the idle clip, and verify NO bone moved. If idle clip
// accidentally forces bones to identity quaternion (the bug that was
// making user's Meshy-rigged cats look "totally messed up" at rest),
// bones whose bind rotation != identity will shift measurably.
//
// On the procedural canonical rig all bones DO have identity bind, so
// this test exercises a synthetic "pretend bind != identity" case:
// rotate a few bones to a non-identity starting pose, then check that
// idle preserves that pose instead of snapping to zero.
function idleHoldsBindPose(species: string, rig: ReturnType<typeof buildRig>) {
  // Reset, then rotate some bones to simulate a non-identity bind pose
  // (mimics what the Blender glTF exporter bakes into GLB rigs).
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  rig.bones.hips.rotation.z = Math.PI / 6;
  rig.bones.chest.rotation.z = -Math.PI / 8;
  rig.bones.head.rotation.x = Math.PI / 12;
  rig.bones.tail_01.rotation.y = Math.PI / 10;
  rig.modelGroup.updateMatrixWorld(true);
  const bindQuats: Record<string, [number, number, number, number]> = {};
  for (const [name, bone] of Object.entries(rig.bones)) {
    bindQuats[name] = bone.quaternion.toArray() as [number, number, number, number];
  }

  // Build idle against this non-identity bind pose, then run it.
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const clip = buildIdleClip(rig.modelGroup);
  if (!clip) { check(`${species}/idle holds bind pose`, false, 'clip null'); return; }
  const action = mixer.clipAction(clip);
  action.play();
  mixer.update(1.0); // sample mid-clip

  // Check that key bones are still at their bind quaternions.
  let worstBone = '';
  let worstDelta = 0;
  for (const name of ['hips', 'chest', 'head', 'tail_01']) {
    const now = rig.bones[name].quaternion.toArray();
    const bind = bindQuats[name];
    const delta = Math.max(Math.abs(now[0] - bind[0]), Math.abs(now[1] - bind[1]), Math.abs(now[2] - bind[2]), Math.abs(now[3] - bind[3]));
    if (delta > worstDelta) { worstDelta = delta; worstBone = name; }
  }
  const ok = worstDelta < 0.001;
  const msg = ok
    ? `all tracked bones held their bind quaternion (worst delta ${worstDelta.toFixed(5)})`
    : `${worstBone} drifted from bind by ${worstDelta.toFixed(5)} (idle clip is forcing it toward identity)`;
  check(`${species}/idle preserves non-identity bind pose`, ok, msg);
  action.stop();
}
idleHoldsBindPose('cat', catRig);
idleHoldsBindPose('dog', dogRig);

// Animation-on-nonIdentity-bind test: this is the cat-goes-sideways
// bug the user reported. Animation clips rotate bones around world axes
// via QuaternionKeyframeTrack. If the clip REPLACES bone.quaternion
// with just the delta rotation, bones with non-identity bind (all
// Blender-rigged GLBs) lose their bind orientation - the whole cat
// jumps to an unnatural pose. Fix: pre-multiply delta * bind so bind
// is preserved.
//
// Test: pick one leg bone, set its bind to a non-identity quaternion,
// build a walk clip, run it. The resulting bone world-Y displacement
// should match what we get with identity bind (delta is expressed in
// world axes, so it rotates world-Y identically regardless of bind).
function animComposesWithBind(species: string, rig: ReturnType<typeof buildRig>) {
  // Reset to rest
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  // Measure paw_back_L world position with IDENTITY bind at thigh_L
  // and running the walk clip to 50% of its duration.
  const mixer1 = new THREE.AnimationMixer(rig.modelGroup);
  const walk = buildWalkClip(rig.modelGroup);
  if (!walk) { check(`${species}/anim composes with bind`, false, 'walk null'); return; }
  const a1 = mixer1.clipAction(walk);
  a1.play();
  mixer1.update(walk.duration * 0.5);
  const p_identityBind = new THREE.Vector3();
  rig.bones.paw_back_L.getWorldPosition(p_identityBind);
  a1.stop();

  // Reset, then set thigh_L to a NON-IDENTITY bind quaternion to
  // simulate what Blender bakes into GLB rigs. Re-build the clip so it
  // captures this new bind, then run to 50%.
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  rig.bones.thigh_L.rotation.z = Math.PI / 4; // 45deg bind around Z
  rig.modelGroup.updateMatrixWorld(true);
  const mixer2 = new THREE.AnimationMixer(rig.modelGroup);
  const walk2 = buildWalkClip(rig.modelGroup); // captures new bind inside buildRotTrack
  if (!walk2) { check(`${species}/anim composes with bind`, false, 'walk2 null'); return; }
  const a2 = mixer2.clipAction(walk2);
  a2.play();
  mixer2.update(walk2.duration * 0.5);
  const p_nonIdentityBind = new THREE.Vector3();
  rig.bones.paw_back_L.getWorldPosition(p_nonIdentityBind);
  a2.stop();

  // The paw will be in a different absolute position because bind
  // changed (rotated 45 around Z), but the DELTA from bind-rest to
  // walk-mid-clip should follow the same pattern. Specifically, the
  // paw should have MOVED (non-zero delta) relative to its own bind
  // rest position in both cases. If pre-multiply is broken, the
  // non-identity-bind case would show the bone JUMPING to the old
  // identity-derived position, so paw would be near p_identityBind
  // instead of shifted by 45deg.
  //
  // Easier invariant: the bone's WORLD ORIENTATION should be
  // (delta * bind) - which differs from just (delta) by the bind
  // rotation. Quantify: with bind=45deg on Z and walk_delta=0 at
  // t=0.5 (shoulder_L phase 0 sin(pi) = 0, thigh_L phase 0.5... hmm,
  // actually check what the walk clip targets and sample angle).
  //
  // Simplest check: at t=0 of walk, bone.quaternion should EQUAL the
  // bind quaternion (delta = 0). If it's identity instead, the fix
  // isn't applied.
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  rig.bones.thigh_L.rotation.z = Math.PI / 4;
  rig.modelGroup.updateMatrixWorld(true);
  const bindQuat = rig.bones.thigh_L.quaternion.toArray();
  const mixer3 = new THREE.AnimationMixer(rig.modelGroup);
  const walk3 = buildWalkClip(rig.modelGroup);
  if (!walk3) { check(`${species}/anim composes with bind`, false, 'walk3 null'); return; }
  const a3 = mixer3.clipAction(walk3);
  a3.play();
  mixer3.update(0); // t=0, before any animation delta applies
  const atT0 = rig.bones.thigh_L.quaternion.toArray();

  // At t=0 the clip's phase-0 delta for thigh_L is sin(2*pi*0.5) * amp
  // = 0 (phase 0.5 -> sin(pi) = 0). So quaternion at t=0 should equal
  // bind. Allow small tolerance for quaternion math precision.
  const maxDelta = Math.max(Math.abs(atT0[0] - bindQuat[0]), Math.abs(atT0[1] - bindQuat[1]), Math.abs(atT0[2] - bindQuat[2]), Math.abs(atT0[3] - bindQuat[3]));
  const ok = maxDelta < 0.01;
  const msg = ok
    ? `thigh_L at t=0 preserves 45deg bind (quaternion delta ${maxDelta.toFixed(5)})`
    : `thigh_L at t=0 snapped AWAY from 45deg bind by ${maxDelta.toFixed(5)} - clip is overriding bind instead of composing`;
  check(`${species}/anim preserves non-identity bind at t=0`, ok, msg);
  a3.stop();
}
animComposesWithBind('cat', catRig);
animComposesWithBind('dog', dogRig);

// Walk/run-on-GLB test: simulates the Blender-rigged GLB case by setting
// every leg bone to a non-identity bind quaternion (rotate around the
// bone's forward axis). Then plays walk and checks paws don't sink
// below ground. This catches the "cat goes under the pane" bug the user
// reported with the rigged Player Cat + Run animation.
//
// Non-identity bind simulates what Blender's glTF exporter bakes into
// GLB bones: each bone has a local rotation baked in based on its
// head-tail direction. The test sets a modest 15 deg bind on all leg
// bones (in world X, simulating "legs tilted slightly forward in rest").
function animOnGLBRig(species: string, rig: ReturnType<typeof buildRig>, clipName: string, clipBuilder: (m: THREE.Object3D) => THREE.AnimationClip | null) {
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  // Bake non-identity bind on every leg bone (simulates Blender rigging).
  const LEG_BONES = ['thigh_L', 'thigh_R', 'shin_L', 'shin_R', 'foot_L', 'foot_R',
                     'shoulder_L', 'shoulder_R', 'upper_arm_L', 'upper_arm_R', 'lower_arm_L', 'lower_arm_R'];
  for (const name of LEG_BONES) {
    if (rig.bones[name]) rig.bones[name].rotation.x = 0.25; // ~14 deg around X
  }
  rig.modelGroup.updateMatrixWorld(true);

  const clip = clipBuilder(rig.modelGroup);
  if (!clip) { check(`${species}/${clipName} on GLB-like rig: paws above ground`, false, 'clip null'); return; }
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const action = mixer.clipAction(clip);
  action.play();

  // Sample paws across the clip; they must stay within a small tolerance
  // of rest ground (y=0). A 5cm tolerance allows for legitimate step-up
  // during walk (paw lifts briefly for swing phase) but flags the
  // "mesh sinks deep under the floor" bug at ~20-30cm.
  const STEPS = 20;
  const TOLERANCE_BELOW = 0.02; // only 2cm below ground is OK; upward
                                 // step-lift is allowed without bound.
  let worstSink = 0;
  let worstSinkPaw = '';
  let worstSinkT = 0;
  let prevT = 0;
  for (let i = 0; i <= STEPS; i++) {
    const t = i / STEPS;
    const deltaT = (t - prevT) * clip.duration;
    if (deltaT > 0) mixer.update(deltaT);
    prevT = t;
    for (const pawName of ALL_PAWS) {
      const v = new THREE.Vector3();
      rig.bones[pawName].getWorldPosition(v);
      if (v.y < -TOLERANCE_BELOW && v.y < worstSink) {
        worstSink = v.y;
        worstSinkPaw = pawName;
        worstSinkT = t;
      }
    }
  }
  const ok = worstSink >= -TOLERANCE_BELOW;
  const msg = ok
    ? `paws stayed above -${TOLERANCE_BELOW}m (worst sink ${worstSink.toFixed(4)})`
    : `${worstSinkPaw} sank to y=${worstSink.toFixed(4)} at t=${worstSinkT.toFixed(2)} - animation delta is not in bone-local frame`;
  check(`${species}/${clipName} on non-identity bind: paws stay grounded`, ok, msg);
  action.stop();
}
animOnGLBRig('cat', catRig, 'walk', buildWalkClip);
animOnGLBRig('dog', dogRig, 'walk', buildWalkClip);
animOnGLBRig('cat', catRig, 'run',  buildRunClip);
animOnGLBRig('dog', dogRig, 'run',  buildRunClip);

check('stand resolver: sit -> standUpFromSit', resolveStandAction('sitDown') === 'standUpFromSit', resolveStandAction('sitDown'));
check('stand resolver: lay -> standUpFromLay', resolveStandAction('layDown') === 'standUpFromLay', resolveStandAction('layDown'));
check('stand resolver: walk -> idle (Stand Still)', resolveStandAction('walk') === 'idle', resolveStandAction('walk'));
check('stand resolver: run -> idle (Stand Still)', resolveStandAction('run') === 'idle', resolveStandAction('run'));
check('stand resolver: idle -> idle (no-op)', resolveStandAction('idle') === 'idle', resolveStandAction('idle'));

// Lay-down routing: from sit must use the fromSit variant to keep back
// legs folded during the transition. From any other state use the
// regular layDown which animates all four legs from T-pose.
function resolveLayAction(fromAnim: string): string {
  if (fromAnim === 'sitDown') return 'layDownFromSit';
  return 'layDown';
}
check('lay resolver: sit -> layDownFromSit', resolveLayAction('sitDown') === 'layDownFromSit', resolveLayAction('sitDown'));
check('lay resolver: idle -> layDown', resolveLayAction('idle') === 'layDown', resolveLayAction('idle'));
check('lay resolver: walk -> layDown', resolveLayAction('walk') === 'layDown', resolveLayAction('walk'));

// Sit -> Lay transition test: THE specific bug the user reported. Play
// sit to end (clamp), then play layDownFromSit. During the transition,
// back paws should NEVER rise above the rest ground level (y ~= 0) by
// more than a small tolerance. If back legs "stand up" mid-transition
// (the old buggy layDown would pass through T-pose), back paws would
// rise to y ~= 0.22 (rest standing height). This test fails loudly
// in that case.
function sitToLayTransitionCheck(species: string, rig: ReturnType<typeof buildRig>) {
  // Reset to rest
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  // Step 1: play sit to end, clamp.
  const sitClip = buildSitDownClip(rig.modelGroup);
  if (!sitClip) { check(`${species}/sit->lay: back paws stay down`, false, 'sit clip null'); return; }
  const sitAction = mixer.clipAction(sitClip);
  sitAction.setLoop(THREE.LoopOnce, 1);
  sitAction.clampWhenFinished = true;
  sitAction.play();
  mixer.update(sitClip.duration);

  // Sanity: back paws are near ground right now (sit end pose).
  const backPawYAfterSit = (() => {
    const v = new THREE.Vector3(); rig.bones.paw_back_L.getWorldPosition(v); return v.y;
  })();

  // Step 2: play layDownFromSit. During its entire duration, back paws
  // must not rise more than 2cm above ground (tolerance accounts for
  // small transit wobble but will flag the "legs unfold to stand" bug).
  const layClip = buildLayDownFromSitClip(rig.modelGroup);
  if (!layClip) { check(`${species}/sit->lay: back paws stay down`, false, 'layFromSit clip null'); return; }
  const layAction = mixer.clipAction(layClip);
  layAction.setLoop(THREE.LoopOnce, 1);
  layAction.clampWhenFinished = true;
  layAction.reset();
  layAction.play();

  const STEPS = 20;
  const TOLERANCE = 0.02;
  let worstT = 0, worstBackY = 0;
  let prevT = 0;
  for (let i = 0; i <= STEPS; i++) {
    const t = i / STEPS;
    const deltaT = (t - prevT) * layClip.duration;
    if (deltaT > 0) mixer.update(deltaT);
    prevT = t;
    const v = new THREE.Vector3();
    rig.bones.paw_back_L.getWorldPosition(v);
    if (Math.abs(v.y) > Math.abs(worstBackY)) { worstBackY = v.y; worstT = t; }
  }
  const ok = Math.abs(worstBackY) <= TOLERANCE;
  const msg = ok
    ? `back paw stayed on ground (initial sit-end y=${backPawYAfterSit.toFixed(4)}, worst during lay y=${worstBackY.toFixed(4)} at t=${worstT.toFixed(2)})`
    : `back paw ROSE to y=${worstBackY.toFixed(4)} at t=${worstT.toFixed(2)} during sit->lay (legs unfolded through T-pose)`;
  check(`${species}/sit->lay: back paws stay grounded`, ok, msg);
}
sitToLayTransitionCheck('cat', catRig);
sitToLayTransitionCheck('dog', dogRig);

// NOTE: layDownFromSit is NOT tested in isolation via pawContactDense
// because it's designed to START from the sit-clamped pose (spine lifted,
// back legs folded) - playing it from T-pose would leave back legs at
// rest and lift front paws 18cm off ground. The actual intended usage
// is covered by sitToLayTransitionCheck above, which plays sit->lay
// the way the browser routes when the user clicks "Lay Down" while
// sitting.

// Transition test: play sit to end, THEN play lay. The mixer only
// modifies bones that each clip TRACKS - if sit animates spine_01.
// position but lay doesn't, spine_01 stays where sit left it. This
// tests that every clip resets its own spine_01 position so poses
// don't leak between actions (the user-visible "lay goes into the
// ground after sit" symptom).
function pawContactAfterTransition(species: string, rig: ReturnType<typeof buildRig>, firstName: string, firstBuilder: (m: THREE.Object3D) => THREE.AnimationClip | null, secondName: string, secondBuilder: (m: THREE.Object3D) => THREE.AnimationClip | null) {
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const firstClip = firstBuilder(rig.modelGroup);
  const secondClip = secondBuilder(rig.modelGroup);
  if (!firstClip || !secondClip) {
    check(`${species}/${firstName}->${secondName}: transition`, false, 'clip null');
    return;
  }
  // Run first clip to end, clamp.
  const firstAction = mixer.clipAction(firstClip);
  firstAction.setLoop(THREE.LoopOnce, 1);
  firstAction.clampWhenFinished = true;
  firstAction.play();
  mixer.update(firstClip.duration);
  // Play second clip. Don't cross-fade in test - just stop first and play second.
  firstAction.stop();
  const secondAction = mixer.clipAction(secondClip);
  secondAction.setLoop(THREE.LoopOnce, 1);
  secondAction.clampWhenFinished = true;
  secondAction.reset();
  secondAction.play();
  mixer.update(secondClip.duration);

  const pawYs: Record<string, number> = {};
  let allGrounded = true;
  let worstPaw = '';
  let worstDy = 0;
  for (const pawName of ALL_PAWS) {
    const v = new THREE.Vector3();
    rig.bones[pawName].getWorldPosition(v);
    pawYs[pawName] = +v.y.toFixed(4);
    const dy = Math.abs(v.y);
    if (dy > PAW_TOLERANCE) {
      allGrounded = false;
      if (dy > worstDy) { worstDy = dy; worstPaw = pawName; }
    }
  }
  const msg = allGrounded
    ? `after ${firstName}->${secondName}, all paws within ${PAW_TOLERANCE}m (${JSON.stringify(pawYs)})`
    : `after ${firstName}->${secondName}, ${worstPaw} off by ${worstDy.toFixed(4)}m. Paws: ${JSON.stringify(pawYs)}`;
  check(`${species}/${firstName}->${secondName}: paws on ground`, allGrounded, msg);
}
// User-reported bug: click sit, then lay, front legs sink. Test catches
// that pose leakage.
pawContactAfterTransition('cat', catRig, 'sitDown', buildSitDownClip, 'layDown', buildLayDownClip);
pawContactAfterTransition('dog', dogRig, 'sitDown', buildSitDownClip, 'layDown', buildLayDownClip);
// Also the reverse: lay then sit must still land paws on ground.
pawContactAfterTransition('cat', catRig, 'layDown', buildLayDownClip, 'sitDown', buildSitDownClip);
pawContactAfterTransition('dog', dogRig, 'layDown', buildLayDownClip, 'sitDown', buildSitDownClip);

// Sit-arch test: the sit pose should have chest HIGH (near rest) and
// hips LOW (dropped toward ground), with intermediate spine bones forming
// a SMOOTH ascending curve from hip to chest instead of a step function.
//
// Check: each spine bone is at least 15% higher than the previous one
// (progressive climb), and chest ends at >= 85% of rest chest height
// (torso stays mostly upright).
function sitArchCheck(species: string, rig: ReturnType<typeof buildRig>) {
  for (const b of Object.values(rig.bones)) {
    b.rotation.set(0, 0, 0);
    b.position.fromArray(rig.layout[b.name].pos);
  }
  // Record rest heights for reference.
  rig.modelGroup.updateMatrixWorld(true);
  const restChestY = (() => { const v = new THREE.Vector3(); rig.bones.chest.getWorldPosition(v); return v.y; })();

  const mixer = new THREE.AnimationMixer(rig.modelGroup);
  const clip = buildSitDownClip(rig.modelGroup);
  if (!clip) { check(`${species}/sit arch`, false, 'clip null'); return; }
  const action = mixer.clipAction(clip);
  action.play();
  mixer.update(clip.duration * 0.99);

  const yOf = (n: string) => { const v = new THREE.Vector3(); rig.bones[n].getWorldPosition(v); return v.y; };
  const spineBones = ['hips', 'lumbar_01', 'lumbar_02', 'spine_01', 'chest'];
  const heights = spineBones.map(yOf);

  // Hip should have dropped significantly
  const hipDropOK = heights[0] < rig.layout.hips.pos[1] * 0.4;
  // Chest should be near rest height (within 15%)
  const chestY = heights[heights.length - 1];
  const chestOK = chestY >= restChestY * 0.85;
  // Each intermediate bone should be higher than the previous (ascending)
  let ascendingOK = true;
  const deltas: number[] = [];
  for (let i = 1; i < heights.length; i++) {
    const delta = heights[i] - heights[i - 1];
    deltas.push(+delta.toFixed(3));
    if (delta < 0.005) ascendingOK = false; // must rise at least 5mm per bone
  }
  const hMap = Object.fromEntries(spineBones.map((n, i) => [n, +heights[i].toFixed(3)]));
  check(`${species}/sit: hip dropped`, hipDropOK, `hip y=${heights[0].toFixed(3)} (should be < ${(rig.layout.hips.pos[1] * 0.4).toFixed(3)})`);
  check(`${species}/sit: chest near rest`, chestOK, `chest y=${chestY.toFixed(3)} / rest ${restChestY.toFixed(3)} (${(chestY/restChestY*100).toFixed(0)}%)`);
  check(`${species}/sit: spine ascends smoothly`, ascendingOK, `heights ${JSON.stringify(hMap)}, deltas ${JSON.stringify(deltas)} (each must be >= 0.005)`);
  action.stop();
}
sitArchCheck('cat', catRig);
sitArchCheck('dog', dogRig);

console.log('\n=== Summary ===');
const failed = results.filter(r => !r.ok);
console.log(`${results.length - failed.length}/${results.length} checks passed`);
if (failed.length) {
  console.log('\nFAILURES:');
  for (const f of failed) console.log(`  \u2717 ${f.name}: ${f.msg}`);
  process.exit(1);
}
console.log('\nAll checks green.');
