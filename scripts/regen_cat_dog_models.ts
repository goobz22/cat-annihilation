// scripts/regen_cat_dog_models.ts
//
// Regenerate valid cat.gltf and dog.gltf.
//
// WHY this exists: the hand-authored cat.gltf/dog.gltf shipped in
// `assets/models/` had a systemic corruption — the embedded base64
// data-URI for the buffer contained ~1.3-1.5KB MORE bytes than the
// declared `buffers[0].byteLength`, and the layout inside the actual
// bytes did not match the declared `bufferViews[].byteOffset` map. The
// visible symptom: ModelLoader::ExtractMeshes correctly refuses to
// load the file (indices read at the declared offset land inside float
// payloads and produce out-of-range index values like 49024 against a
// 24-vertex mesh), so the player cat and every dog render invisibly.
//
// Rather than patch around bad asset data in the loader (which would
// paper over the regression and quietly render garbled meshes), we
// regenerate the two assets procedurally so the engine gets a
// guaranteed-valid glTF 2.0 file every time: single mesh, single
// material, tightly-packed VEC3/VEC3/SCALAR buffer, explicit
// `buffers[0].byteLength` matching the encoded payload exactly.
//
// Geometry is a simple cube-silhouette-of-a-quadruped. It is
// intentionally minimal — this is a game-as-harness asset, not a hero
// model. When proper Meshy-generated rigs land (see README's asset
// pipeline section), this file can be deleted and replaced with
// the gltf-transform output.
//
// Usage (from repo root):
//   bun scripts/regen_cat_dog_models.ts
//
// Re-run after editing the geometry parameters below. The only outputs
// are assets/models/cat.gltf and assets/models/dog.gltf.

import { writeFileSync, mkdirSync, existsSync } from "node:fs";
import path from "node:path";

type Vec3 = [number, number, number];

// -----------------------------------------------------------------------
// Box mesh builder: each call appends a box's 24 vertices (4 per face,
// so per-face normals come out clean with no vertex sharing across
// faces) and 36 indices into running arrays. Positions and normals are
// emitted as little-endian float32s; indices are uint16 LE.
//
// WHY per-face (24 verts not 8): if we share the 8 corners across 6
// faces, every vertex normal is the averaged diagonal of its 3 incident
// faces — the cube ends up smooth-shaded and looks like a rounded
// potato under any directional light. Per-face normals (4 verts × 6
// faces = 24) give the crisp cube look the engine's forward pass is
// tuned for and matches what the shaders expect from the legacy asset.
// -----------------------------------------------------------------------

function pushBox(
  positions: number[],
  normals: number[],
  indices: number[],
  center: Vec3,
  halfExtents: Vec3,
): void {
  const [cx, cy, cz] = center;
  const [hx, hy, hz] = halfExtents;
  const x0 = cx - hx, x1 = cx + hx;
  const y0 = cy - hy, y1 = cy + hy;
  const z0 = cz - hz, z1 = cz + hz;

  // Base vertex index for the 4 corners of each face.
  const baseIndex = positions.length / 3;

  // Order of faces: +X, -X, +Y, -Y, +Z, -Z. Each face emits 4 corners
  // CCW when viewed from outside, so the default Vulkan winding (front
  // = CCW in the engine's shared pipeline state) shows the outside
  // face. The two-triangle fan is (0,1,2) and (0,2,3).
  const faces: { normal: Vec3; verts: Vec3[] }[] = [
    {
      normal: [1, 0, 0],
      verts: [[x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1]],
    },
    {
      normal: [-1, 0, 0],
      verts: [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]],
    },
    {
      normal: [0, 1, 0],
      verts: [[x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0]],
    },
    {
      normal: [0, -1, 0],
      verts: [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]],
    },
    {
      normal: [0, 0, 1],
      verts: [[x1, y0, z1], [x0, y0, z1], [x0, y1, z1], [x1, y1, z1]],
    },
    {
      normal: [0, 0, -1],
      verts: [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0]],
    },
  ];

  for (const face of faces) {
    const faceBase = positions.length / 3;
    for (const v of face.verts) {
      positions.push(v[0], v[1], v[2]);
      normals.push(face.normal[0], face.normal[1], face.normal[2]);
    }
    indices.push(
      faceBase, faceBase + 1, faceBase + 2,
      faceBase, faceBase + 2, faceBase + 3,
    );
  }

  // baseIndex is unused now (we use per-face faceBase above) but kept
  // in scope for readability — the compiler's DCE drops it at emit.
  void baseIndex;
}

// -----------------------------------------------------------------------
// Build a quadruped silhouette from 9 boxes: body, head, 2 ears, 4
// legs, 1 tail. Proportions are tuned so the cat and dog are
// visually distinct at gameplay camera distance (top-down, ~8m away)
// without any texturing.
// -----------------------------------------------------------------------

interface Proportions {
  body: { center: Vec3; halfExtents: Vec3 };
  head: { center: Vec3; halfExtents: Vec3 };
  earL: { center: Vec3; halfExtents: Vec3 };
  earR: { center: Vec3; halfExtents: Vec3 };
  legs: { center: Vec3; halfExtents: Vec3 }[]; // 4 legs, front-L, front-R, back-L, back-R
  tail: { center: Vec3; halfExtents: Vec3 };
  baseColorFactor: [number, number, number, number];
}

const CAT_PROPORTIONS: Proportions = {
  // Slender, upright-ish cat: short body, larger ears, thin tail.
  body: { center: [0, 0.32, 0], halfExtents: [0.09, 0.09, 0.25] },
  head: { center: [0, 0.42, 0.3], halfExtents: [0.1, 0.1, 0.09] },
  earL: { center: [0.06, 0.55, 0.3], halfExtents: [0.03, 0.05, 0.025] },
  earR: { center: [-0.06, 0.55, 0.3], halfExtents: [0.03, 0.05, 0.025] },
  legs: [
    { center: [0.06, 0.1, 0.18], halfExtents: [0.03, 0.1, 0.03] },
    { center: [-0.06, 0.1, 0.18], halfExtents: [0.03, 0.1, 0.03] },
    { center: [0.06, 0.1, -0.18], halfExtents: [0.03, 0.1, 0.03] },
    { center: [-0.06, 0.1, -0.18], halfExtents: [0.03, 0.1, 0.03] },
  ],
  tail: { center: [0, 0.42, -0.3], halfExtents: [0.025, 0.025, 0.15] },
  baseColorFactor: [0.9, 0.7, 0.45, 1.0], // warm tabby tone
};

const DOG_PROPORTIONS: Proportions = {
  // Longer, lower dog: chunkier body, droopy ears, short tail.
  body: { center: [0, 0.3, 0] as Vec3, halfExtents: [0.12, 0.11, 0.32] as Vec3 },
  head: { center: [0, 0.36, 0.36] as Vec3, halfExtents: [0.11, 0.1, 0.12] as Vec3 },
  // Dog ears drape down along the side of the head.
  earL: { center: [0.11, 0.3, 0.36] as Vec3, halfExtents: [0.02, 0.07, 0.05] as Vec3 },
  earR: { center: [-0.11, 0.3, 0.36] as Vec3, halfExtents: [0.02, 0.07, 0.05] as Vec3 },
  legs: [
    { center: [0.08, 0.09, 0.22] as Vec3, halfExtents: [0.035, 0.09, 0.035] as Vec3 },
    { center: [-0.08, 0.09, 0.22] as Vec3, halfExtents: [0.035, 0.09, 0.035] as Vec3 },
    { center: [0.08, 0.09, -0.22] as Vec3, halfExtents: [0.035, 0.09, 0.035] as Vec3 },
    { center: [-0.08, 0.09, -0.22] as Vec3, halfExtents: [0.035, 0.09, 0.035] as Vec3 },
  ],
  tail: { center: [0, 0.38, -0.36] as Vec3, halfExtents: [0.03, 0.03, 0.08] as Vec3 },
  baseColorFactor: [0.4, 0.3, 0.22, 1.0], // dark-brown dog tone
};

function buildMesh(p: Proportions): {
  positions: number[];
  normals: number[];
  indices: number[];
} {
  const positions: number[] = [];
  const normals: number[] = [];
  const indices: number[] = [];

  pushBox(positions, normals, indices, p.body.center, p.body.halfExtents);
  pushBox(positions, normals, indices, p.head.center, p.head.halfExtents);
  pushBox(positions, normals, indices, p.earL.center, p.earL.halfExtents);
  pushBox(positions, normals, indices, p.earR.center, p.earR.halfExtents);
  for (const leg of p.legs) {
    pushBox(positions, normals, indices, leg.center, leg.halfExtents);
  }
  pushBox(positions, normals, indices, p.tail.center, p.tail.halfExtents);

  return { positions, normals, indices };
}

// -----------------------------------------------------------------------
// GLTF emitter. Layout is deliberately the simplest legal form:
//   [POSITIONS (vertexCount * 12)] [NORMALS (vertexCount * 12)] [INDICES (indexCount * 2)]
// One buffer, three bufferViews, three accessors, one mesh, one
// material, one root node with `mesh: 0`. No nodes with child mesh
// references, no skinning, no animations.
// -----------------------------------------------------------------------

function emitGltf(
  name: string,
  positions: number[],
  normals: number[],
  indices: number[],
  baseColorFactor: [number, number, number, number],
): object {
  const vertexCount = positions.length / 3;
  const indexCount = indices.length;

  // Build the raw buffer: positions | normals | padded indices.
  // WHY align indices to 4 bytes even though uint16 is 2-byte: glTF spec
  // says bufferView byteOffset must be a multiple of the component size
  // for the largest accessor using that view. uint16 needs 2-byte align;
  // VEC3 float needs 4-byte align. Our positions and normals are each
  // `vertexCount * 12`, already a multiple of 4. Indices starting at
  // `2 * vertexCount * 12` is automatically 4-aligned.
  const positionBytes = vertexCount * 12;
  const normalBytes = vertexCount * 12;
  const indexBytes = indexCount * 2;
  const totalBytes = positionBytes + normalBytes + indexBytes;

  const buf = Buffer.alloc(totalBytes);
  for (let i = 0; i < positions.length; i++) {
    buf.writeFloatLE(positions[i], i * 4);
  }
  for (let i = 0; i < normals.length; i++) {
    buf.writeFloatLE(normals[i], positionBytes + i * 4);
  }
  for (let i = 0; i < indices.length; i++) {
    buf.writeUInt16LE(indices[i], positionBytes + normalBytes + i * 2);
  }

  const base64 = buf.toString("base64");

  // Min/max bounds for the POSITION accessor — glTF spec requires
  // these, and the engine's CalculateBounds still works without them
  // but render-graph clustered-lighting culling behaves better when
  // the accessor carries them.
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < positions.length; i += 3) {
    if (positions[i] < minX) minX = positions[i];
    if (positions[i] > maxX) maxX = positions[i];
    if (positions[i + 1] < minY) minY = positions[i + 1];
    if (positions[i + 1] > maxY) maxY = positions[i + 1];
    if (positions[i + 2] < minZ) minZ = positions[i + 2];
    if (positions[i + 2] > maxZ) maxZ = positions[i + 2];
  }

  return {
    asset: { version: "2.0", generator: "regen_cat_dog_models.ts" },
    scene: 0,
    scenes: [{ name: "Scene", nodes: [0] }],
    nodes: [{ name, mesh: 0 }],
    meshes: [
      {
        name: `${name}Mesh`,
        primitives: [
          {
            attributes: { POSITION: 0, NORMAL: 1 },
            indices: 2,
            material: 0,
          },
        ],
      },
    ],
    accessors: [
      {
        bufferView: 0,
        componentType: 5126, // FLOAT
        count: vertexCount,
        type: "VEC3",
        min: [minX, minY, minZ],
        max: [maxX, maxY, maxZ],
      },
      {
        bufferView: 1,
        componentType: 5126,
        count: vertexCount,
        type: "VEC3",
      },
      {
        bufferView: 2,
        componentType: 5123, // UNSIGNED_SHORT
        count: indexCount,
        type: "SCALAR",
      },
    ],
    bufferViews: [
      { buffer: 0, byteOffset: 0, byteLength: positionBytes, target: 34962 }, // ARRAY_BUFFER
      { buffer: 0, byteOffset: positionBytes, byteLength: normalBytes, target: 34962 },
      { buffer: 0, byteOffset: positionBytes + normalBytes, byteLength: indexBytes, target: 34963 }, // ELEMENT_ARRAY_BUFFER
    ],
    buffers: [
      {
        byteLength: totalBytes,
        uri: `data:application/octet-stream;base64,${base64}`,
      },
    ],
    materials: [
      {
        name: `${name}Material`,
        pbrMetallicRoughness: {
          baseColorFactor,
          metallicFactor: 0.0,
          roughnessFactor: 0.8,
        },
      },
    ],
  };
}

function writeModel(
  outPath: string,
  label: string,
  props: Proportions,
): void {
  const { positions, normals, indices } = buildMesh(props);
  const gltf = emitGltf(label, positions, normals, indices, props.baseColorFactor);
  const json = JSON.stringify(gltf, null, 2);
  const dir = path.dirname(outPath);
  if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
  writeFileSync(outPath, json, "utf-8");
  console.log(
    `wrote ${outPath}: vertices=${positions.length / 3} triangles=${indices.length / 3} bytes=${json.length}`,
  );
}

// -----------------------------------------------------------------------
// Entry point. Keep this side-effect short so `bun
// scripts/regen_cat_dog_models.ts` reads as a single operation.
// -----------------------------------------------------------------------

// WHY `import.meta.dir` over `new URL(import.meta.url).pathname`: on
// Windows, `pathname` from a `file://` URL yields a leading-slash
// form like `/C:/Users/...` that `node:path` parses as a rooted
// POSIX path — `path.dirname(...)/..` then tries to mkdir `C:`
// which fails with EPERM. `import.meta.dir` is Bun's platform-aware
// shortcut and returns native path separators directly.
const repoRoot = path.resolve(import.meta.dir, "..");
writeModel(path.join(repoRoot, "assets/models/cat.gltf"), "Cat", CAT_PROPORTIONS);
writeModel(path.join(repoRoot, "assets/models/dog.gltf"), "Dog", DOG_PROPORTIONS);
