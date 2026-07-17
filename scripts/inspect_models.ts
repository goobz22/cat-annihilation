#!/usr/bin/env bun
/**
 * inspect_models.ts — the model-iteration tool for the Meshy asset pipeline.
 *
 * WHY: "the meshy designs need a way to properly iterate over them" (owner
 * directive 2026-07-16). Before this tool, knowing whether a GLB was rigged,
 * what animation clips it carried, or whether the engine loader could even
 * parse it meant launching the game and reading logs — a minutes-long loop
 * per model. This script answers all of it in one pass over the asset tree:
 *
 *   bun scripts/inspect_models.ts                 # human table, all models
 *   bun scripts/inspect_models.ts --json          # machine-readable
 *   bun scripts/inspect_models.ts assets/models/cats/rigged  # subtree only
 *
 * Per model it reports:
 *   - size, mesh/primitive/vertex/triangle counts (Meshy auto-rig refuses
 *     >~300k faces, and the engine budget cares)
 *   - nodes, skin presence (joint count, inverseBindMatrices)
 *   - animation clips: name, duration, channel count — "rigged but no
 *     clips" and "clips but no skin" both scream here
 *   - vertex-attribute encodings (JOINTS_0/WEIGHTS_0 componentTypes — the
 *     u8-joints misread that took every character down to a placeholder box
 *     on 2026-07-16 would have been visible here on day one)
 *   - ENGINE-LOADER verdict: the same constraints ModelLoader enforces
 *     (float POSITION/NORMAL/UV, legal joint encodings, joint indices
 *     within the node-count bound), evaluated without launching the game.
 *
 * GLB only (the engine ships GLBs; .gltf+bin pairs are legacy) — a .gltf
 * file is listed with a "legacy gltf (not inspected)" note so it is never
 * silently skipped.
 */

import { readFileSync, readdirSync, statSync } from 'node:fs'
import { join, relative } from 'node:path'

interface ClipInfo {
  name: string
  durationSeconds: number
  channels: number
}

interface PrimitiveEncoding {
  attribute: string
  componentType: number
  ok: boolean
  note: string
}

interface ModelReport {
  file: string
  sizeMB: number
  parsed: boolean
  error?: string
  meshes?: number
  primitives?: number
  vertices?: number
  triangles?: number
  nodes?: number
  skins?: { joints: number; hasInverseBindMatrices: boolean }[]
  clips?: ClipInfo[]
  encodings?: PrimitiveEncoding[]
  engineLoaderVerdict?: 'OK' | 'FAIL'
  engineLoaderIssues?: string[]
}

const COMPONENT_NAMES: Record<number, string> = {
  5120: 'i8', 5121: 'u8', 5122: 'i16', 5123: 'u16', 5125: 'u32', 5126: 'f32',
}

function parseGlbJson(buf: Buffer): { json: any; bin: Buffer | null } {
  if (buf.readUInt32LE(0) !== 0x46546c67) throw new Error('not a GLB (bad magic)')
  const jsonLen = buf.readUInt32LE(12)
  if (buf.readUInt32LE(16) !== 0x4e4f534a) throw new Error('first chunk is not JSON')
  const json = JSON.parse(buf.subarray(20, 20 + jsonLen).toString('utf8'))
  let bin: Buffer | null = null
  const binHeaderOffset = 20 + jsonLen
  if (binHeaderOffset + 8 <= buf.length &&
      buf.readUInt32LE(binHeaderOffset + 4) === 0x004e4942) {
    const binLen = buf.readUInt32LE(binHeaderOffset)
    bin = buf.subarray(binHeaderOffset + 8, binHeaderOffset + 8 + binLen)
  }
  return { json, bin }
}

// Read one accessor's max scalar value from the BIN chunk — used to bound-
// check JOINTS_0 the same way ModelLoader's load-time validation does.
function maxJointIndex(json: any, bin: Buffer | null, accessorIdx: number): number | null {
  if (!bin) return null
  const accessor = json.accessors[accessorIdx]
  const view = json.bufferViews[accessor.bufferView]
  const componentType = accessor.componentType as number
  const bytesPer = componentType === 5121 ? 1 : componentType === 5123 ? 2 : componentType === 5125 ? 4 : 0
  if (bytesPer === 0) return null
  const lanes = 4 // JOINTS_0 is VEC4 by spec
  const start = (view.byteOffset ?? 0) + (accessor.byteOffset ?? 0)
  const stride = view.byteStride ?? bytesPer * lanes
  let max = 0
  for (let i = 0; i < accessor.count; i++) {
    const base = start + i * stride
    for (let lane = 0; lane < lanes; lane++) {
      const off = base + lane * bytesPer
      const v = bytesPer === 1 ? bin.readUInt8(off)
        : bytesPer === 2 ? bin.readUInt16LE(off)
        : bin.readUInt32LE(off)
      if (v > max) max = v
    }
  }
  return max
}

function inspect(path: string): ModelReport {
  const sizeMB = statSync(path).size / (1024 * 1024)
  const report: ModelReport = { file: path, sizeMB: Math.round(sizeMB * 100) / 100, parsed: false }
  if (path.endsWith('.gltf')) {
    report.error = 'legacy gltf (not inspected — engine ships GLBs)'
    return report
  }
  let json: any, bin: Buffer | null
  try {
    ;({ json, bin } = parseGlbJson(readFileSync(path)))
  } catch (parseError) {
    report.error = String(parseError)
    return report
  }
  report.parsed = true
  report.nodes = json.nodes?.length ?? 0
  report.meshes = json.meshes?.length ?? 0
  report.skins = (json.skins ?? []).map((skin: any) => ({
    joints: skin.joints?.length ?? 0,
    hasInverseBindMatrices: skin.inverseBindMatrices !== undefined,
  }))
  report.clips = (json.animations ?? []).map((anim: any) => {
    // Clip duration = max input-accessor max across samplers; glTF stores
    // it in each accessor's `max` field so no BIN reads are needed.
    let duration = 0
    for (const sampler of anim.samplers ?? []) {
      const input = json.accessors[sampler.input]
      if (input?.max?.[0] > duration) duration = input.max[0]
    }
    return {
      name: anim.name ?? '(unnamed)',
      durationSeconds: Math.round(duration * 100) / 100,
      channels: anim.channels?.length ?? 0,
    }
  })

  const issues: string[] = []
  const encodings: PrimitiveEncoding[] = []
  let vertices = 0
  let triangles = 0
  let primitives = 0
  for (const mesh of json.meshes ?? []) {
    for (const prim of mesh.primitives ?? []) {
      primitives++
      const attrs = prim.attributes ?? {}
      if (attrs.POSITION !== undefined) {
        vertices += json.accessors[attrs.POSITION].count
      }
      if (prim.indices !== undefined) {
        triangles += Math.floor(json.accessors[prim.indices].count / 3)
      }
      // The engine loader's componentType contract (ModelLoader.cpp):
      // float-only for geometry attributes, u8/u16/u32 joints, f32 or
      // normalized u8/u16 weights.
      for (const [attr, accessorIdx] of Object.entries(attrs) as [string, number][]) {
        const componentType = json.accessors[accessorIdx].componentType as number
        let ok = true
        let note = COMPONENT_NAMES[componentType] ?? String(componentType)
        if (attr === 'JOINTS_0') {
          ok = [5121, 5123, 5125].includes(componentType)
          const maxIdx = maxJointIndex(json, bin, accessorIdx)
          if (maxIdx !== null) {
            note += ` maxIdx=${maxIdx}`
            if (maxIdx >= (report.nodes ?? 0)) {
              ok = false
              note += ` OUT OF NODE BOUND (${report.nodes})`
            }
          }
        } else if (attr === 'WEIGHTS_0') {
          ok = componentType === 5126 ||
               ([5121, 5123].includes(componentType) && json.accessors[accessorIdx].normalized === true)
        } else if (['POSITION', 'NORMAL', 'TANGENT', 'TEXCOORD_0', 'TEXCOORD_1'].includes(attr)) {
          ok = componentType === 5126
        }
        if (!ok) issues.push(`${mesh.name ?? 'mesh'}/${attr}: illegal encoding ${note}`)
        encodings.push({ attribute: attr, componentType, ok, note })
      }
    }
  }
  report.primitives = primitives
  report.vertices = vertices
  report.triangles = triangles
  report.encodings = encodings

  // Cross-cutting rig sanity: a "rigged" character should have BOTH a skin
  // and clips; either alone is a broken pipeline output.
  if ((report.skins?.length ?? 0) > 0 && (report.clips?.length ?? 0) === 0) {
    issues.push('has skin but ZERO animation clips (rig pipeline emitted no actions)')
  }
  if ((report.clips?.length ?? 0) > 0 && (report.skins?.length ?? 0) === 0) {
    issues.push('has clips but NO skin (animations cannot deform the mesh)')
  }
  for (const skin of report.skins ?? []) {
    if (!skin.hasInverseBindMatrices) {
      issues.push('skin missing inverseBindMatrices (engine skinning needs them)')
    }
  }

  report.engineLoaderIssues = issues
  report.engineLoaderVerdict = issues.length === 0 ? 'OK' : 'FAIL'
  return report
}

function walk(dir: string, out: string[] = []): string[] {
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry)
    const st = statSync(full)
    if (st.isDirectory()) walk(full, out)
    else if (/\.(glb|gltf)$/i.test(entry)) out.push(full)
  }
  return out
}

// Resolve the root arg into the model list. WHY the stat branch: the loop
// iterates the whole asset tree by default, but a single-model iteration
// ("does THIS export now carry a skin?") is the most common inner-loop use
// while re-rigging one file — passing a .glb path directly must inspect just
// that file rather than crash in readdirSync with ENOTDIR (a directory-only
// walk assumes its arg is a directory, which a file path is not).
function collectModels(root: string): string[] {
  if (statSync(root).isDirectory()) return walk(root).sort()
  return /\.(glb|gltf)$/i.test(root) ? [root] : []
}

const argv = process.argv.slice(2)
const asJson = argv.includes('--json')
const rootArg = argv.find((a) => !a.startsWith('--'))
const root = rootArg ?? 'assets/models'

const files = collectModels(root)
const reports = files.map(inspect)

if (asJson) {
  console.log(JSON.stringify(reports, null, 2))
} else {
  for (const r of reports) {
    const rel = relative(process.cwd(), r.file)
    if (!r.parsed) {
      console.log(`\n▫ ${rel}  (${r.sizeMB} MB)  — ${r.error}`)
      continue
    }
    const skinText = (r.skins ?? []).map((s) => `${s.joints}j${s.hasInverseBindMatrices ? '' : ' NO-IBM'}`).join(',') || 'NONE'
    const verdictIcon = r.engineLoaderVerdict === 'OK' ? '✅' : '❌'
    console.log(`\n${verdictIcon} ${rel}  (${r.sizeMB} MB)`)
    console.log(`   verts=${r.vertices} tris=${r.triangles} nodes=${r.nodes} skins=[${skinText}]`)
    if ((r.clips ?? []).length > 0) {
      console.log(`   clips: ${r.clips!.map((c) => `${c.name}(${c.durationSeconds}s/${c.channels}ch)`).join(' ')}`)
    } else {
      console.log('   clips: NONE')
    }
    for (const issue of r.engineLoaderIssues ?? []) {
      console.log(`   ⚠ ${issue}`)
    }
  }
  const failing = reports.filter((r) => r.parsed && r.engineLoaderVerdict === 'FAIL')
  console.log(`\n${reports.length} models scanned, ${failing.length} with issues`)
}

// Exit code contract for gating: 0 = every parsed model OK, 1 = issues.
process.exit(reports.some((r) => r.parsed && r.engineLoaderVerdict === 'FAIL') ? 1 : 0)
