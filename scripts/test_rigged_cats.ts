#!/usr/bin/env bun
/**
 * test_rigged_cats.ts - Diagnose per-cat rigging issues that the
 * canonical-rig test_animations.ts can't catch.
 *
 * What this covers that the canonical test doesn't:
 *  1. Each rigged GLB actually HAS every expected bone (naming match).
 *  2. Each leg / key-spine bone has NON-ZERO vertex weight coverage
 *     (heat-diffusion can silently fail per-bone - the bone exists
 *     but no mesh vertices are bound to it, so rotating it doesn't
 *     deform the mesh). This was why front legs on shadowwhisker
 *     don't move during walk while thunderstrike's do.
 *  3. Rest-pose bbox min.y > -0.005 for each mesh - ground-penetration
 *     check. If the bind pose already puts geometry below y=0 relative
 *     to the model origin, the asset-browser's position.sub(box.min.y)
 *     won't be enough to raise paws above the viewer's ground disc.
 *
 * Runs as: bun scripts/test_rigged_cats.ts
 */

import { promises as fs } from 'fs';
import * as path from 'path';

const RIGGED_DIR = 'c:/Users/Matt-PC/Documents/App Development/cat-annihilation/assets/models/cats/rigged';

// Canonical bone list - MUST match asset_browser.html EXPECTED_BONES_CAT
const EXPECTED_BONES = new Set([
  'root', 'hips',
  'lumbar_01', 'lumbar_02',
  'spine_01', 'upper_back_01', 'upper_back_02', 'upper_back_03', 'chest',
  'neck_01', 'neck_02', 'head',
  'ear_L', 'ear_R', 'jaw',
  'tail_01', 'tail_02', 'tail_03', 'tail_04',
  'shoulder_L', 'upper_arm_L', 'lower_arm_L', 'paw_front_L',
  'shoulder_R', 'upper_arm_R', 'lower_arm_R', 'paw_front_R',
  'thigh_L', 'shin_L', 'foot_L', 'paw_back_L',
  'thigh_R', 'shin_R', 'foot_R', 'paw_back_R'
]);

// Bones that MUST have vertex weight coverage for animation to work.
// If shoulder_L has zero coverage, the walk clip rotates it but no mesh
// vertices follow - front-left leg looks frozen.
const WEIGHT_CRITICAL_BONES = [
  'shoulder_L', 'shoulder_R',
  'upper_arm_L', 'upper_arm_R',
  'thigh_L', 'thigh_R',
  'shin_L', 'shin_R',
  'head', 'hips', 'chest',
  'neck_01', 'neck_02',
  'tail_01'
];

// Parse a GLB binary: header (12 bytes) + JSON chunk (len + 'JSON' + data)
// + BIN chunk (len + 'BIN\0' + data). Return { meta, bin }.
async function parseGlb(glbPath: string) {
  const fd = await fs.open(glbPath, 'r');
  const header = Buffer.alloc(12);
  await fd.read(header, 0, 12, 0);
  const magic = header.subarray(0, 4).toString('utf8');
  if (magic !== 'glTF') throw new Error(`Not a GLB: ${glbPath}`);
  const totalLen = header.readUInt32LE(8);

  // JSON chunk header
  const jsonHeader = Buffer.alloc(8);
  await fd.read(jsonHeader, 0, 8, 12);
  const jsonLen = jsonHeader.readUInt32LE(0);
  const jsonTag = jsonHeader.subarray(4, 8).toString('utf8');
  if (jsonTag !== 'JSON') throw new Error(`First chunk not JSON: ${glbPath}`);
  const jsonBuf = Buffer.alloc(jsonLen);
  await fd.read(jsonBuf, 0, jsonLen, 20);
  const meta = JSON.parse(jsonBuf.toString('utf8'));

  // BIN chunk (optional)
  let bin: Buffer | null = null;
  const binOffset = 20 + jsonLen;
  if (binOffset < totalLen) {
    const binHeader = Buffer.alloc(8);
    await fd.read(binHeader, 0, 8, binOffset);
    const binLen = binHeader.readUInt32LE(0);
    bin = Buffer.alloc(binLen);
    await fd.read(bin, 0, binLen, binOffset + 8);
  }
  await fd.close();
  return { meta, bin, totalLen };
}

// For each skinned mesh + each joint, count how many vertices have
// non-zero weight. Returns Map<bone_name, vertex_count_with_weight>.
function perBoneWeightCoverage(meta: any, bin: Buffer): Map<string, number> {
  const coverage = new Map<string, number>();
  if (!meta.skins || meta.skins.length === 0) return coverage;

  // Build bone-index -> bone-name lookup from the skin's joint list.
  const skin = meta.skins[0];
  const jointNames: string[] = skin.joints.map((nodeIdx: number) => meta.nodes[nodeIdx].name);

  // Each skinned mesh primitive has JOINTS_0 + WEIGHTS_0 accessors.
  // Walk primitives and tally per-bone vertex counts.
  for (const mesh of meta.meshes || []) {
    for (const prim of mesh.primitives || []) {
      const jointAttrIdx = prim.attributes?.JOINTS_0;
      const weightAttrIdx = prim.attributes?.WEIGHTS_0;
      if (jointAttrIdx === undefined || weightAttrIdx === undefined) continue;

      const jointAcc = meta.accessors[jointAttrIdx];
      const weightAcc = meta.accessors[weightAttrIdx];
      const jointBV = meta.bufferViews[jointAcc.bufferView];
      const weightBV = meta.bufferViews[weightAcc.bufferView];

      // Read bytes. JOINTS_0 is usually UNSIGNED_BYTE or UNSIGNED_SHORT
      // (componentType 5121 or 5123). WEIGHTS_0 is usually FLOAT (5126).
      const count = jointAcc.count;
      const jointOffset = (jointBV.byteOffset || 0) + (jointAcc.byteOffset || 0);
      const weightOffset = (weightBV.byteOffset || 0) + (weightAcc.byteOffset || 0);

      for (let i = 0; i < count; i++) {
        for (let c = 0; c < 4; c++) {
          let joint = 0;
          if (jointAcc.componentType === 5121) joint = bin[jointOffset + i * 4 + c];
          else if (jointAcc.componentType === 5123) joint = bin.readUInt16LE(jointOffset + i * 8 + c * 2);
          const weight = bin.readFloatLE(weightOffset + i * 16 + c * 4);
          if (weight > 0) {
            const name = jointNames[joint];
            coverage.set(name, (coverage.get(name) || 0) + 1);
          }
        }
      }
    }
  }
  return coverage;
}

// Compute mesh bbox from POSITION accessor min/max (all primitives).
function meshBBox(meta: any) {
  const mins = [Infinity, Infinity, Infinity];
  const maxs = [-Infinity, -Infinity, -Infinity];
  for (const mesh of meta.meshes || []) {
    for (const prim of mesh.primitives || []) {
      const posIdx = prim.attributes?.POSITION;
      if (posIdx === undefined) continue;
      const acc = meta.accessors[posIdx];
      if (!acc.min || !acc.max) continue;
      for (let i = 0; i < 3; i++) {
        mins[i] = Math.min(mins[i], acc.min[i]);
        maxs[i] = Math.max(maxs[i], acc.max[i]);
      }
    }
  }
  return { mins, maxs, size: [maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]] };
}

// ----- Test runner -----
type CheckResult = { cat: string; name: string; ok: boolean; msg: string };
const results: CheckResult[] = [];

function check(cat: string, name: string, ok: boolean, msg: string) {
  results.push({ cat, name, ok, msg });
}

const files = (await fs.readdir(RIGGED_DIR)).filter(f => f.endsWith('.glb')).sort();

console.log(`\nDiagnosing ${files.length} rigged cat GLBs at tight tolerance.\n`);

for (const f of files) {
  const cat = f.replace('.glb', '');
  const { meta, bin } = await parseGlb(path.join(RIGGED_DIR, f));

  // 1. Check bone presence
  const skin = meta.skins?.[0];
  const jointNames: Set<string> = new Set(skin?.joints?.map((n: number) => meta.nodes[n].name) || []);
  const missing: string[] = [];
  for (const expected of EXPECTED_BONES) if (!jointNames.has(expected)) missing.push(expected);
  check(cat, 'all expected bones present', missing.length === 0,
        missing.length === 0 ? `${jointNames.size} bones found` : `MISSING: ${missing.join(', ')}`);

  // 2. Check per-bone weight coverage on critical bones
  if (bin) {
    const coverage = perBoneWeightCoverage(meta, bin);
    const uncovered: string[] = [];
    for (const boneName of WEIGHT_CRITICAL_BONES) {
      const count = coverage.get(boneName) || 0;
      if (count === 0) uncovered.push(boneName);
    }
    check(cat, 'critical bones have vertex weights', uncovered.length === 0,
          uncovered.length === 0 ? `all ${WEIGHT_CRITICAL_BONES.length} critical bones have weights`
                                 : `ZERO WEIGHT on: ${uncovered.join(', ')} (these bones can rotate but the mesh won't follow)`);
  }

  // 3. Ground penetration: mesh bbox min.y should be non-negative
  const bbox = meshBBox(meta);
  // Accept slight negative (mesh may have sub-mm float imprecision);
  // flag only meaningful penetration (>5mm).
  const penetration = bbox.mins[1] < -0.005 ? -bbox.mins[1] : 0;
  check(cat, 'mesh does not start below ground', penetration === 0,
        penetration === 0 ? `bbox min.y = ${bbox.mins[1].toFixed(4)}m (rest above ground)`
                          : `mesh starts ${penetration.toFixed(4)}m BELOW y=0 - viewer will render inside ground disc`);
}

// ----- Report -----
const byCat = new Map<string, CheckResult[]>();
for (const r of results) {
  if (!byCat.has(r.cat)) byCat.set(r.cat, []);
  byCat.get(r.cat)!.push(r);
}

let totalOk = 0, totalFail = 0;
const allFailures: CheckResult[] = [];
for (const [cat, catResults] of byCat) {
  const failures = catResults.filter(r => !r.ok);
  if (failures.length === 0) {
    console.log(`\x1b[32m[PASS]\x1b[0m ${cat}`);
    totalOk += catResults.length;
  } else {
    console.log(`\x1b[31m[FAIL]\x1b[0m ${cat}`);
    for (const f of failures) console.log(`       - ${f.name}: ${f.msg}`);
    totalOk += catResults.length - failures.length;
    totalFail += failures.length;
    allFailures.push(...failures);
  }
}

console.log(`\n${totalOk}/${totalOk + totalFail} checks passed across ${files.length} cats.`);
if (totalFail > 0) {
  console.log(`\n=== Summary by failure type ===`);
  const byType = new Map<string, string[]>();
  for (const f of allFailures) {
    if (!byType.has(f.name)) byType.set(f.name, []);
    byType.get(f.name)!.push(`${f.cat}: ${f.msg}`);
  }
  for (const [type, instances] of byType) {
    console.log(`\n${type} (${instances.length} cats):`);
    for (const i of instances.slice(0, 10)) console.log(`  ${i}`);
    if (instances.length > 10) console.log(`  ...and ${instances.length - 10} more`);
  }
  process.exit(1);
}
