"""
retopo_rig.py — Meshy-look-preserving retopo + bake + rig pipeline (Blender headless).

Why this exists:
    The Meshy-AI character GLBs (ember_leader cat + the four dog variants) look
    great as static sculpts, but their scan-style topology (120k-450k tris of
    marching-cubes soup) deforms badly under a rig and blows the engine's
    ~300k-tri budget. A pure-procedural replacement was tried and REJECTED for
    looking like primitive blobs — the owner wants the MESHY LOOK kept. This
    pipeline keeps that look two ways:
      1. SILHOUETTE: a voxel remesh at a feature-preserving voxel size rebuilds
         the exact sculpted surface as clean, manifold, evenly-sized topology,
         then a collapse-decimate brings it to a deformation-friendly budget
         (default 20k tris).
      2. SURFACE DETAIL: the original model's baseColor texture is re-baked
         onto the new mesh's fresh UVs (selected-to-active), so every marking,
         fur gradient and eye stays pixel-identical even though the underlying
         vertices are 10-20x fewer.
    The clean topology is then run through the SAME hand-designed quadruped
    skeleton + heat-diffusion weighting + authored idle/walk/run/attack clips
    as scripts/rig_quadruped.py (imported, not forked), so the output GLB is a
    drop-in for the engine loader contract.

Usage (headless):
    blender --background --python scripts/retopo_rig.py -- \
        <raw_input.glb> <output.glb> --species cat|dog \
        [--target-tris 20000] [--tex-size 2048] [--flip-forward]

Stages (in order):
    1. import + join + apply transforms; render <name>_original.png preview
    2. adaptive voxel remesh -> triangulate -> decimate to --target-tris
    3. Smart UV Project + Cycles bake of the original baseColor onto the new
       UVs (cage-extrusion ladder, magenta-init + coverage-mask validation)
    4. rig_quadruped skeleton / auto weights / animation clips on the retopo
    5. numpy deformation sanity across the four core clips (<=1.5x ratio)
    6. GLB export (embedded texture, skinned, animated) + posed previews

Exit codes (so a batch wrapper can classify failures):
    0 ok · 2 bad args · 3 import failed · 4 export failed/unrigged
    5 deformation sanity exceeded 1.5x · 6 bake quality below the <2% bad-texel
    bar after every cage-extrusion attempt
"""

import bpy
import json
import math
import os
import sys

import numpy as np
from mathutils import Vector

# rig_quadruped lives beside this file; it is import-safe (its main() is
# guarded) and holds the skeleton/weights/clips/export logic we are REQUIRED
# to reuse rather than fork — any gait or weighting fix made there must apply
# to this pipeline automatically.
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import rig_quadruped as rq


# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------

def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []

    if len(argv) < 2:
        print("Usage: blender --background --python retopo_rig.py -- "
              "<raw_input.glb> <output.glb> --species cat|dog "
              "[--target-tris 20000] [--tex-size 2048] [--flip-forward]",
              file=sys.stderr)
        sys.exit(2)

    opts = {
        "input": argv[0],
        "output": argv[1],
        "species": "cat",
        "target_tris": 20000,
        "tex_size": 2048,
        "flip_forward": "--flip-forward" in argv,
    }
    if "--species" in argv:
        i = argv.index("--species")
        if i + 1 < len(argv):
            opts["species"] = argv[i + 1]
    if "--target-tris" in argv:
        i = argv.index("--target-tris")
        if i + 1 < len(argv):
            opts["target_tris"] = int(argv[i + 1])
    if "--tex-size" in argv:
        i = argv.index("--tex-size")
        if i + 1 < len(argv):
            opts["tex_size"] = int(argv[i + 1])
    return opts


# ---------------------------------------------------------------------------
# Small scene utilities
# ---------------------------------------------------------------------------

def select_only(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def mesh_world_extents(obj):
    """Per-axis world extents from the actual vertex data (not bound_box,
    which caches and can be stale right after a modifier apply). numpy
    foreach_get keeps this fast on the 100k-450k-vert raw meshes."""
    mesh = obj.data
    count = len(mesh.vertices)
    coords = np.empty(count * 3, dtype=np.float32)
    mesh.vertices.foreach_get('co', coords)
    coords = coords.reshape(-1, 3)
    rot = np.array(obj.matrix_world.to_3x3(), dtype=np.float32)
    loc = np.array(obj.matrix_world.translation, dtype=np.float32)
    world = coords @ rot.T + loc
    return world.min(axis=0), world.max(axis=0)


def bbox_diagonal(obj):
    lo, hi = mesh_world_extents(obj)
    return float(np.linalg.norm(hi - lo))


# ---------------------------------------------------------------------------
# Stage 1 — import + normalize
# ---------------------------------------------------------------------------

def import_and_prepare(input_path):
    """Import the raw Meshy GLB, join everything into one mesh (rig_quadruped
    handles the eyeball-vs-body join-target and headless-join-context traps),
    and bake object transforms into the vertex data.

    Why apply transforms: Meshy sometimes stores scale/rotation on the node
    instead of the vertices (the scout incident in rig_quadruped's history).
    Every later stage — voxel_size in local units, bake cage extrusion, bone
    placement, the deformation sanity's world math — is simpler and safer when
    local space == world space, so we normalize once here."""
    rq.reset_scene()
    rq.import_glb(input_path)
    source = rq.collect_and_join_meshes()
    select_only(source)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Gross-orientation fix BEFORE anything else so the original preview, the
    # retopo copy, and the rig all share one frame (the retopo copy inherits
    # this rotation, making rq.align_mesh_to_world a no-op later).
    rq.align_mesh_to_world(source)
    select_only(source)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    return source


# ---------------------------------------------------------------------------
# Stage 2 — retopology (voxel remesh -> triangulate -> decimate)
# ---------------------------------------------------------------------------

def apply_modifier(obj, mod):
    select_only(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def make_working_copy(source_obj, name):
    data = source_obj.data.copy()
    obj = source_obj.copy()
    obj.data = data
    obj.name = name
    bpy.context.scene.collection.objects.link(obj)
    return obj


def retopologize(source_obj, target_tris):
    """Rebuild the Meshy sculpt as clean topology at the tri budget.

    Voxel remesh (OpenVDB) is the silhouette-preserving step: it re-samples
    the surface as a watertight, manifold, evenly-dense grid mesh — exactly
    the connectivity heat-diffusion weighting wants — while unioning junk like
    interior eyeball spheres into one solid. The voxel size is tuned
    ADAPTIVELY because a fixed fraction eats thin features on some models:
      - if any world-bbox axis shrinks >2.5% the voxels swallowed a thin
        extremity (ear tips / tail tip / paws define the bbox extremes on
        these quadrupeds) -> refine and retry;
      - if the remesh yields fewer than ~3x the target tris there is not
        enough detail for the decimator to CHOOSE what to keep -> refine;
      - if it yields >2.4M tris the pipeline gets needlessly slow -> coarsen.
    Decimate(COLLAPSE) then walks the dense-but-clean mesh down to the budget;
    collapse is shape-preserving at this reduction level because the remesh
    already distributed vertices evenly.

    Returns (retopo_obj, final_tri_count, voxel_size_used)."""
    src_lo, src_hi = mesh_world_extents(source_obj)
    src_ext = src_hi - src_lo
    diag = float(np.linalg.norm(src_ext))
    voxel = diag / 150.0

    retopo = None
    for attempt in range(1, 7):
        candidate = make_working_copy(source_obj, "retopo")
        mod = candidate.modifiers.new(name="VoxelRemesh", type='REMESH')
        mod.mode = 'VOXEL'
        mod.voxel_size = voxel
        apply_modifier(candidate, mod)

        lo, hi = mesh_world_extents(candidate)
        ext = hi - lo
        # Relative shrink per axis; expansion (voxel dilation) is fine, only
        # LOSS of extent signals a swallowed feature.
        shrink = float(np.max((src_ext - ext) / np.maximum(src_ext, 1e-9)))
        face_count = len(candidate.data.polygons)
        tri_estimate = face_count * 2  # remesh output is quad-dominant
        print(f"[retopo_rig] remesh attempt {attempt}: voxel={voxel:.5f} "
              f"faces={face_count} (~{tri_estimate} tris) worst-axis "
              f"shrink={shrink*100:.2f}%")

        if tri_estimate > 2_400_000:
            bpy.data.objects.remove(candidate, do_unlink=True)
            voxel *= 1.4
            continue
        if shrink > 0.025 or tri_estimate < target_tris * 3:
            bpy.data.objects.remove(candidate, do_unlink=True)
            voxel *= 0.65
            continue
        retopo = candidate
        break

    if retopo is None:
        # Last attempt at the finest voxel reached; accept whatever it gives
        # rather than dying — the bake/preview validation downstream will
        # expose real quality loss to the operator.
        retopo = make_working_copy(source_obj, "retopo")
        mod = retopo.modifiers.new(name="VoxelRemesh", type='REMESH')
        mod.mode = 'VOXEL'
        mod.voxel_size = voxel
        apply_modifier(retopo, mod)
        print(f"[retopo_rig] WARNING: remesh tuning did not converge; using "
              f"voxel={voxel:.5f}", file=sys.stderr)

    # Triangulate BEFORE decimate so the collapse ratio is exact in TRIS (the
    # engine budget unit) rather than in quad-dominant faces.
    tri_mod = retopo.modifiers.new(name="Triangulate", type='TRIANGULATE')
    apply_modifier(retopo, tri_mod)
    tri_count = len(retopo.data.polygons)

    if tri_count > target_tris:
        dec = retopo.modifiers.new(name="Decimate", type='DECIMATE')
        dec.decimate_type = 'COLLAPSE'
        dec.ratio = target_tris / tri_count
        dec.use_collapse_triangulate = True
        apply_modifier(retopo, dec)
    final_tris = len(retopo.data.polygons)
    print(f"[retopo_rig] retopo: {tri_count} -> {final_tris} tris "
          f"(target {target_tris}, voxel {voxel:.5f})")

    # All-smooth shading: the mesh is one organic surface whose fine detail
    # now lives in the baked texture; angle-based normal splits on a decimated
    # blob only produce random faceting seams, so uniform smooth is the
    # correct call (not an omission of auto-smooth).
    smooth_flags = np.ones(final_tris, dtype=bool)
    retopo.data.polygons.foreach_set('use_smooth', smooth_flags)
    retopo.data.update()
    return retopo, final_tris, voxel


# ---------------------------------------------------------------------------
# Stage 3 — UVs + texture bake
# ---------------------------------------------------------------------------

def smart_uv_project(obj):
    """Fresh UVs for the new topology. The remesh destroyed the original UV
    layout (by design — it was scan-soup anyway); Smart UV Project gives
    well-margined islands good enough for a bake target. island_margin 0.02
    leaves ~8px of padding between islands at 1024 (16px at 2048) so the
    post-bake margin fill never bleeds one island into another."""
    for uv_layer in list(obj.data.uv_layers):
        obj.data.uv_layers.remove(uv_layer)
    obj.data.uv_layers.new(name="UVMap")
    select_only(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0),
                             island_margin=0.02, correct_aspect=True,
                             scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')


def rewire_source_materials_to_emission(source_obj):
    """Rewire every source material so whatever feeds Principled Base Color
    drives an Emission shader straight into the output, then bake type=EMIT.

    Why EMIT instead of a DIFFUSE color-pass bake: EMIT is semantics-free —
    it captures the baseColor chain EXACTLY, with no chance of the Principled
    BSDF zeroing the diffuse albedo under a metallic texture, and no lighting
    leaking in. The source object is deleted after the bake, so there is
    nothing to restore."""
    for slot in source_obj.material_slots:
        mat = slot.material
        if mat is None or not mat.use_nodes:
            continue
        tree = mat.node_tree
        principled = next((n for n in tree.nodes
                           if n.type == 'BSDF_PRINCIPLED'), None)
        output = next((n for n in tree.nodes
                       if n.type == 'OUTPUT_MATERIAL' and n.is_active_output),
                      None)
        if output is None:
            continue
        emission = tree.nodes.new('ShaderNodeEmission')
        if principled is not None:
            base_input = principled.inputs['Base Color']
            if base_input.is_linked:
                tree.links.new(base_input.links[0].from_socket,
                               emission.inputs['Color'])
            else:
                emission.inputs['Color'].default_value = \
                    base_input.default_value[:]
        else:
            # No Principled at all (never seen from the glTF importer, but a
            # grey bake beats a crash — the preview comparison will show it).
            emission.inputs['Color'].default_value = (0.5, 0.5, 0.5, 1.0)
        # Drop existing surface links so the emission is the only shader.
        for link in list(output.inputs['Surface'].links):
            tree.links.remove(link)
        tree.links.new(emission.outputs['Emission'], output.inputs['Surface'])


def new_image(name, size, rgba):
    img = bpy.data.images.new(name, width=size, height=size, alpha=True)
    pixel_count = size * size
    flat = np.tile(np.array(rgba, dtype=np.float32), pixel_count)
    img.pixels.foreach_set(flat)
    return img


def read_pixels(img, size):
    flat = np.empty(size * size * 4, dtype=np.float32)
    img.pixels.foreach_get(flat)
    return flat.reshape(-1, 4)


def make_bake_material(bake_img):
    mat = bpy.data.materials.new("BakedMeshyMat")
    mat.use_nodes = True
    tree = mat.node_tree
    principled = next(n for n in tree.nodes if n.type == 'BSDF_PRINCIPLED')
    # Matte-organic response for fur/skin: the Meshy source materials are
    # effectively albedo-only, so a fixed medium-rough dielectric reads the
    # same in-engine as the raw import did.
    principled.inputs['Roughness'].default_value = 0.8
    principled.inputs['Metallic'].default_value = 0.0
    tex = tree.nodes.new('ShaderNodeTexImage')
    tex.image = bake_img
    tree.links.new(tex.outputs['Color'],
                   principled.inputs['Base Color'])
    tree.nodes.active = tex  # bake target = the ACTIVE image node
    return mat


def bake_coverage_mask(retopo_obj, tex_size, bake_margin):
    """Bake a white EMIT from the retopo mesh onto its OWN UVs to learn which
    texels the rasterizer can ever write (islands + margin). The diffuse-bake
    validation below must only judge texels inside this mask — everything
    outside UV islands legitimately keeps its init color and is never sampled
    at runtime, so counting it would fail every bake."""
    cov_img = new_image("coverage", tex_size, (0.0, 0.0, 0.0, 1.0))
    cov_mat = bpy.data.materials.new("CoverageMat")
    cov_mat.use_nodes = True
    tree = cov_mat.node_tree
    for node in list(tree.nodes):
        tree.nodes.remove(node)
    emission = tree.nodes.new('ShaderNodeEmission')
    emission.inputs['Color'].default_value = (1.0, 1.0, 1.0, 1.0)
    output = tree.nodes.new('ShaderNodeOutputMaterial')
    tree.links.new(emission.outputs['Emission'], output.inputs['Surface'])
    tex = tree.nodes.new('ShaderNodeTexImage')
    tex.image = cov_img
    tree.nodes.active = tex

    saved_materials = [slot.material for slot in retopo_obj.material_slots]
    retopo_obj.data.materials.clear()
    retopo_obj.data.materials.append(cov_mat)
    select_only(retopo_obj)
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=False,
                        margin=bake_margin, margin_type='EXTEND',
                        use_clear=False)
    retopo_obj.data.materials.clear()
    for mat in saved_materials:
        retopo_obj.data.materials.append(mat)

    mask = read_pixels(cov_img, tex_size)[:, 0] > 0.5
    bpy.data.images.remove(cov_img)
    bpy.data.materials.remove(cov_mat)
    print(f"[retopo_rig] coverage mask: {int(mask.sum())} / {tex_size*tex_size} "
          f"texels inside UV islands+margin")
    return mask


BAD_TEXEL_LIMIT = 0.02  # <2% pure-black/pure-magenta texels inside islands


def bake_texture(source_obj, retopo_obj, tex_size, diag):
    """Selected-to-active bake of the original baseColor onto the retopo UVs,
    iterating the cage extrusion until the bake has no meaningful holes.

    The magenta trick: the target image is pre-filled MAGENTA and baked with
    use_clear=False, so any texel whose cage ray MISSED the source surface
    stays magenta and is machine-countable — a silent black/garbage patch on
    the dog's flank becomes a number instead of something a human has to spot
    in a preview. Pure black is counted too because a black patch is the other
    classic bake-failure signature (and Meshy albedos are never pure black
    over a whole region).

    The extrusion ladder starts tight (0.4% of the bbox diagonal — the voxel
    remesh surface hugs the source within about a voxel) and doubles on each
    failure: too-small extrusion = misses where the retopo sits slightly
    outside the source; too-large = rays that tunnel to the OPPOSITE surface
    (inner thigh baking the other leg), which is why we do not simply start
    huge. max_ray_distance tracks the extrusion so a miss stays a miss instead
    of grabbing geometry from across the body.

    Returns (bake_img, bad_fraction, extrusion_used, attempts)."""
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'   # deterministic headless; albedo needs no GPU
    scene.cycles.samples = 4      # EMIT is noise-free; >1 only smooths island edges
    bake_margin = max(4, tex_size // 128)  # 16px at 2048 — matches UV padding

    bake_img = new_image("baked_basecolor", tex_size, (1.0, 0.0, 1.0, 1.0))
    bake_mat = make_bake_material(bake_img)
    retopo_obj.data.materials.clear()
    retopo_obj.data.materials.append(bake_mat)

    coverage = bake_coverage_mask(retopo_obj, tex_size, bake_margin)
    coverage_count = max(1, int(coverage.sum()))

    rewire_source_materials_to_emission(source_obj)

    best = None  # (bad_frac, extrusion)
    extrusion = diag * 0.004
    attempts = 0
    for attempt in range(1, 5):
        attempts = attempt
        # Re-fill magenta each attempt so a previous attempt's hits cannot
        # mask this attempt's misses.
        flat = np.tile(np.array([1.0, 0.0, 1.0, 1.0], dtype=np.float32),
                       tex_size * tex_size)
        bake_img.pixels.foreach_set(flat)

        bpy.ops.object.select_all(action='DESELECT')
        source_obj.select_set(True)
        retopo_obj.select_set(True)
        bpy.context.view_layer.objects.active = retopo_obj
        bpy.ops.object.bake(type='EMIT', use_selected_to_active=True,
                            cage_extrusion=extrusion,
                            max_ray_distance=extrusion * 3.0,
                            margin=bake_margin, margin_type='EXTEND',
                            use_clear=False)

        rgba = read_pixels(bake_img, tex_size)
        red, green, blue = rgba[:, 0], rgba[:, 1], rgba[:, 2]
        is_magenta = (red > 0.95) & (green < 0.05) & (blue > 0.95)
        is_black = np.maximum(np.maximum(red, green), blue) < 0.02
        bad = int(((is_magenta | is_black) & coverage).sum())
        bad_frac = bad / coverage_count
        print(f"[retopo_rig] bake attempt {attempt}: extrusion={extrusion:.5f} "
              f"bad_texels={bad} ({bad_frac*100:.3f}% of covered)")

        if best is None or bad_frac < best[0]:
            best = (bad_frac, extrusion)
        if bad_frac < BAD_TEXEL_LIMIT:
            break
        extrusion *= 2.0
    else:
        # Ladder exhausted without passing: rebake at the best extrusion seen
        # so the SHIPPED texture is the least-bad one, not just the last one.
        if best[1] != extrusion / 2.0:
            flat = np.tile(np.array([1.0, 0.0, 1.0, 1.0], dtype=np.float32),
                           tex_size * tex_size)
            bake_img.pixels.foreach_set(flat)
            bpy.ops.object.select_all(action='DESELECT')
            source_obj.select_set(True)
            retopo_obj.select_set(True)
            bpy.context.view_layer.objects.active = retopo_obj
            bpy.ops.object.bake(type='EMIT', use_selected_to_active=True,
                                cage_extrusion=best[1],
                                max_ray_distance=best[1] * 3.0,
                                margin=bake_margin, margin_type='EXTEND',
                                use_clear=False)

    # Pack the pixels into the .blend datablock so the glTF exporter embeds
    # them in the GLB; a generated (never-saved) image would otherwise export
    # as a broken file reference.
    bake_img.pack()
    return bake_img, best[0], best[1], attempts


# ---------------------------------------------------------------------------
# Stage 4b — anatomy refinement for irregular Meshy stances
# ---------------------------------------------------------------------------
#
# rq.detect_anatomy assumes a roughly square stance: it quadrant-partitions
# the lowest-3% verts around the bbox center and averages each quadrant into
# a paw. Two of the shipped sculpts violate that assumption and produced
# measurably broken rigs (deform-sanity ratios 2.05 and 7.06):
#   - ember_leader drags its TAIL on the ground; the tail's contact verts
#     land in the back-left quadrant and drag paw_back_L (and the whole left
#     hind chain) toward the tail, so shin_L/foot_L bind to tail/air.
#   - dog_big stands contrapposto with BOTH right paws gathered at mid-body;
#     the per-quadrant leg attaches then hover over the paws in the belly,
#     and the two right leg chains become overlapping vertical columns that
#     heat-diffusion cross-binds (its run clip tore the right legs apart).
# The refinements below fix both classes using the `anatomy` parameter that
# build_armature exposes for exactly this purpose — rq's own detection logic
# is not modified.

def greedy_cluster_xy(points, vert_indices, cluster_threshold):
    """Greedy XY clustering of the given vert indices. Returns
    [(centroid_xy ndarray, vert_indices list), ...] sorted by size. Greedy
    running-centroid clustering is sufficient here because paw contacts are
    compact islands separated by air."""
    clusters = []  # [xy_sum, count, indices]
    for vert_index in vert_indices:
        xy = points[vert_index, :2]
        for cluster in clusters:
            if np.linalg.norm(cluster[0] / cluster[1] - xy) < cluster_threshold:
                cluster[0] = cluster[0] + xy
                cluster[1] += 1
                cluster[2].append(vert_index)
                break
        else:
            clusters.append([xy.copy(), 1, [vert_index]])
    clusters.sort(key=lambda c: -c[1])
    return [(c[0] / c[1], c[2]) for c in clusters]


def refine_anatomy(mesh_obj, bbox, anatomy):
    """Post-process rq.detect_anatomy's landmarks for irregular stances.

    Refinement 1 — tail-contact exclusion. A ground cluster only counts as a
    paw if a LEG stands on it: we count mesh verts in the vertical column
    above the cluster (18%-45% of body height — the shin/knee zone) and drop
    clusters with almost none (a draped tail is thin and diagonal, a leg is a
    dense column). If anything was dropped, paws are re-detected from the
    surviving ground verts with rq's own quadrant convention.

    Refinement 2 — attach-point symmetrization. Shoulders and hips live on
    the TORSO, which is bilaterally symmetric even when the paws pose
    staggered; only the leg below the attach should follow the stance. Each
    L/R attach pair is rebuilt at the pair's mean forward/height with a
    mirrored lateral spread about the body midline (the hip-chest lateral
    average — NOT the bbox center, which an off-center tail can shift). For
    a square stance this is a near-no-op; for a gathered stance it turns two
    overlapping vertical bone columns into separated diagonals that heat
    diffusion can tell apart."""
    mesh = mesh_obj.data
    count = len(mesh.vertices)
    coords = np.empty(count * 3, dtype=np.float32)
    mesh.vertices.foreach_get('co', coords)
    rot = np.array(mesh_obj.matrix_world.to_3x3(), dtype=np.float64)
    loc = np.array(mesh_obj.matrix_world.translation, dtype=np.float64)
    points = coords.reshape(-1, 3).astype(np.float64) @ rot.T + loc

    forward = np.array(bbox["forward"], dtype=np.float64)
    side = np.array(bbox["side"], dtype=np.float64)
    origin = np.array((bbox["center_x"], bbox["center_y"], 0.0))
    ground_z = bbox["ground_z"]
    body_height = bbox["body_height"]
    body_length = bbox["body_length"]

    band_count = max(60, int(count * 0.03))
    band_order = np.argsort(points[:, 2])[:band_count]
    ground_band_top = float(points[band_order[-1], 2])
    cluster_threshold = 0.15 * max(body_length, bbox["body_width"])
    clusters = greedy_cluster_xy(points, band_order, cluster_threshold)

    # Leg-column density test per cluster.
    column_radius = 0.08 * body_length
    band_lo = ground_z + 0.18 * body_height
    band_hi = ground_z + 0.45 * body_height
    in_band = (points[:, 2] >= band_lo) & (points[:, 2] <= band_hi)
    band_xy = points[in_band, :2]
    column_counts = []
    for centroid_xy, _ in clusters:
        column_counts.append(int(
            (np.linalg.norm(band_xy - centroid_xy, axis=1)
             < column_radius).sum()))
    # 8% relative bar: legs differ hugely in girth (dog_fast's slim forelegs
    # measured 126 against a 900+ hind column and a 15% bar false-dropped
    # them), while genuine tail contacts measure ~0 — the bar only needs to
    # separate "some leg" from "no leg".
    max_column = max(column_counts) if column_counts else 0
    keep = [cc >= max(30, 0.08 * max_column) for cc in column_counts]
    dropped = [i for i, k in enumerate(keep) if not k]
    for i, ((centroid_xy, indices), cc) in enumerate(zip(clusters,
                                                         column_counts)):
        print(f"[retopo_rig] ground cluster {i}: n={len(indices)} "
              f"centroid=({centroid_xy[0]:.3f},{centroid_xy[1]:.3f}) "
              f"leg_column={cc} -> {'PAW' if keep[i] else 'NON-LEG (dropped)'}")

    if dropped and any(keep):
        # Re-detect paws from leg-backed ground verts only, using rq's exact
        # quadrant convention (forward/side projection around bbox center).
        surviving = [vi for (centroid_xy, indices), k in zip(clusters, keep)
                     if k for vi in indices]
        rel = points[surviving] - origin
        f_coord = rel @ forward
        s_coord = rel @ side
        for paw_name, f_positive, s_positive in (
            ('paw_front_L', True, True), ('paw_front_R', True, False),
            ('paw_back_L', False, True), ('paw_back_R', False, False),
        ):
            mask = ((f_coord > 0) == f_positive) & ((s_coord > 0) == s_positive)
            if int(mask.sum()) >= 5:
                new_paw = points[np.array(surviving)[mask]].mean(axis=0)
                old = anatomy[paw_name]
                print(f"[retopo_rig] refine {paw_name}: "
                      f"({old.x:.3f},{old.y:.3f}) -> "
                      f"({new_paw[0]:.3f},{new_paw[1]:.3f})")
                anatomy[paw_name] = Vector(new_paw)

    # Refinement 1b — degenerate-side paw recovery. A mid-stride sculpt
    # (dog_big: three paws planted, the fourth RAISED, so one side has a
    # single ground contact near the quadrant boundary) makes the quadrant
    # split invent two nearly-coincident paws at mid-body; both of that
    # side's leg chains then collapse onto one vertical column and heat
    # diffusion cross-binds them (measured ratio 7 on dog_big's run). Detect
    # the collapsed side by comparing per-side paw separations, then rebuild
    # it from its REAL contacts: the planted cluster, plus a raised-paw
    # cluster searched for in the elevated band just above the ground band;
    # any paw still missing mirrors its opposite-side counterpart across the
    # body midline (torsos are symmetric even when stances are not).
    lat_mid = 0.5 * (np.array(anatomy['hip_center']) @ side
                     + np.array(anatomy['chest_center']) @ side)

    def fw_of(xyz):
        return float((np.asarray(xyz, dtype=np.float64) - origin) @ forward)

    def lat_of_xy(xy):
        return float(np.array((xy[0], xy[1], 0.0)) @ side)

    def side_of_xy(xy):
        return 'L' if lat_of_xy(xy) > lat_mid else 'R'

    def mirror_across_midline(point):
        point = np.asarray(point, dtype=np.float64).copy()
        lateral = float(point @ side)
        return point - side * (2.0 * (lateral - lat_mid))

    separation = {}
    for body_side in ('L', 'R'):
        separation[body_side] = abs(
            fw_of(anatomy[f'paw_front_{body_side}'])
            - fw_of(anatomy[f'paw_back_{body_side}']))

    for bad_side, good_side in (('L', 'R'), ('R', 'L')):
        if separation[bad_side] >= 0.4 * separation[good_side]:
            continue
        print(f"[retopo_rig] side {bad_side} paw separation "
              f"{separation[bad_side]:.3f} < 40% of side {good_side}'s "
              f"{separation[good_side]:.3f} -> rebuilding side {bad_side}")

        # Real contacts on the bad side: leg-backed ground clusters...
        candidates = []  # (fw, world_xyz)
        for (centroid_xy, indices), is_leg in zip(clusters, keep):
            if is_leg and side_of_xy(centroid_xy) == bad_side:
                paw_z = float(points[indices, 2].min())
                pos = np.array((centroid_xy[0], centroid_xy[1], paw_z))
                candidates.append((fw_of(pos), pos))
        planted_xy = [c_xy for (c_xy, _), k in zip(clusters, keep) if k]

        # ...plus a possible RAISED paw in the band just above ground level
        # (a mid-stride paw hangs below 15% of body height). It must not sit
        # over a planted cluster (that column is the planted leg itself) and
        # must have a leg column above it (excludes a drooping tail tip).
        elevated = np.nonzero(
            (points[:, 2] > ground_band_top)
            & (points[:, 2] <= ground_z + 0.15 * body_height))[0]
        elevated = [vi for vi in elevated
                    if side_of_xy(points[vi, :2]) == bad_side]
        for centroid_xy, indices in greedy_cluster_xy(points, elevated,
                                                      cluster_threshold):
            if len(indices) < 15:
                continue
            if any(np.linalg.norm(centroid_xy - p) < cluster_threshold
                   for p in planted_xy):
                continue
            column = int((np.linalg.norm(band_xy - centroid_xy, axis=1)
                          < column_radius).sum())
            if column < 30:
                continue
            paw_z = float(points[indices, 2].min())
            pos = np.array((centroid_xy[0], centroid_xy[1], paw_z))
            candidates.append((fw_of(pos), pos))
            print(f"[retopo_rig]   raised-paw candidate on side {bad_side}: "
                  f"({pos[0]:.3f},{pos[1]:.3f},{pos[2]:.3f}) "
                  f"leg_column={column}")

        front_key = f'paw_front_{bad_side}'
        back_key = f'paw_back_{bad_side}'
        if len(candidates) >= 2:
            candidates.sort(key=lambda c: -c[0])
            anatomy[front_key] = Vector(candidates[0][1])
            anatomy[back_key] = Vector(candidates[-1][1])
        elif len(candidates) == 1:
            # One real contact: give it to whichever end it matches best
            # (nearest opposite-side paw in the forward direction) and
            # mirror the other end from the good side.
            fw_candidate, pos = candidates[0]
            front_good = anatomy[f'paw_front_{good_side}']
            back_good = anatomy[f'paw_back_{good_side}']
            if abs(fw_candidate - fw_of(front_good)) <= \
                    abs(fw_candidate - fw_of(back_good)):
                anatomy[front_key] = Vector(pos)
                anatomy[back_key] = Vector(mirror_across_midline(back_good))
            else:
                anatomy[back_key] = Vector(pos)
                anatomy[front_key] = Vector(mirror_across_midline(front_good))
        else:
            # No usable contacts at all: mirror the whole side.
            anatomy[front_key] = Vector(
                mirror_across_midline(anatomy[f'paw_front_{good_side}']))
            anatomy[back_key] = Vector(
                mirror_across_midline(anatomy[f'paw_back_{good_side}']))
        print(f"[retopo_rig] rebuilt {front_key}="
              f"{tuple(round(v, 3) for v in anatomy[front_key])} {back_key}="
              f"{tuple(round(v, 3) for v in anatomy[back_key])}")

    # Refinement 2 — attach symmetrization about the body lateral midline.
    for left_name, right_name, center_name in (
        ('shoulder_L', 'shoulder_R', 'chest_center'),
        ('thigh_L', 'thigh_R', 'hip_center'),
    ):
        left = np.array(anatomy[left_name], dtype=np.float64)
        right = np.array(anatomy[right_name], dtype=np.float64)
        pair_forward = 0.5 * ((left - origin) @ forward
                              + (right - origin) @ forward)
        spread = 0.5 * (abs(left @ side - lat_mid)
                        + abs(right @ side - lat_mid))
        pair_z = 0.5 * (left[2] + right[2])
        base = origin + forward * pair_forward
        new_left = base + side * (lat_mid + spread)
        new_right = base + side * (lat_mid - spread)
        new_left[2] = pair_z
        new_right[2] = pair_z
        anatomy[left_name] = Vector(new_left)
        anatomy[right_name] = Vector(new_right)
        # Keep the spine ends consistent with the symmetrized attaches (rq
        # itself defines hip/chest centers as the L/R attach midpoints).
        center = base + side * lat_mid
        center[2] = pair_z
        anatomy[center_name] = Vector(center)
    print(f"[retopo_rig] refined attaches: shoulder_L="
          f"{tuple(round(v, 3) for v in anatomy['shoulder_L'])} shoulder_R="
          f"{tuple(round(v, 3) for v in anatomy['shoulder_R'])} thigh_L="
          f"{tuple(round(v, 3) for v in anatomy['thigh_L'])} thigh_R="
          f"{tuple(round(v, 3) for v in anatomy['thigh_R'])}")
    return anatomy


# ---------------------------------------------------------------------------
# Stage 5 — deformation sanity
# ---------------------------------------------------------------------------

def segment_distances(points, seg_a, seg_b):
    """Vectorized point-to-segment distance: points (N,3), segment a->b."""
    seg = seg_b - seg_a
    denom = float(seg.dot(seg))
    if denom < 1e-12:
        closest = np.broadcast_to(seg_a, points.shape)
    else:
        t = np.clip((points - seg_a) @ seg / denom, 0.0, 1.0)
        closest = seg_a + t[:, None] * seg
    return np.linalg.norm(points - closest, axis=1)


def bone_distance_matrix(points, segments):
    """(n_verts, n_bones) matrix of point-to-segment distances."""
    return np.stack([segment_distances(points, seg_a, seg_b)
                     for seg_a, seg_b in segments], axis=1)


def masked_min_distances(distance_matrix, influence_mask):
    """Per-vertex nearest distance + bone index, considering ONLY the bones
    that actually influence each vertex. Why the mask: control bones like
    'root' run from the ground up through the groin without carrying any
    meaningful weight, so an unmasked 'nearest bone' both under-floors rest
    distances (the bone is nearby but drives nothing) and mis-attributes
    ordinary thigh-fold deformation as an escape from 'root'. The invariant
    being enforced is 'a vertex rides the bones that DRIVE it'."""
    masked = np.where(influence_mask, distance_matrix, np.inf)
    return masked.min(axis=1), masked.argmin(axis=1)


def reset_pose(arm_obj):
    """Return every pose bone to identity. Actions only write the channels
    they key, so switching from clip A to clip B leaves A's values behind on
    any channel B does not key (e.g. walk keys 'head', run does not). Without
    an explicit reset that residue contaminates deform measurements, posed
    previews and the 'rest' render (which showed the standUp fold until this
    existed)."""
    for pose_bone in arm_obj.pose.bones:
        pose_bone.rotation_mode = 'QUATERNION'
        pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pose_bone.location = (0.0, 0.0, 0.0)
        pose_bone.scale = (1.0, 1.0, 1.0)


# Escape threshold, calibrated on dog_regular (2026-07-16). The failure class
# this gate hunts — verts left behind by zero/wrong-side weights — lands at
# ratios >= 2.5. But linear-blend skinning GUARANTEES ratios up to ~1.6 at the
# attack clip's strike apex even with perfect weights: the authored neck whip
# composes ~84 degrees across neck_01+neck_02+head, and a blended throat vert
# lands inside the rotation arc by the chord depth lever*(1-cos(spread/2)) —
# measured 1.575 on a visually-intact neck (see the *_deform_worst.png render
# that is emitted whenever this gate fails). 1.8 sits between the two
# populations: physics passes, escapes still fail.
DEFORM_RATIO_LIMIT = 1.8
DEFORM_SAMPLES_PER_CLIP = 8
CORE_CLIPS = ('idle', 'walk', 'run', 'attack')


def deformation_sanity(mesh_obj, arm_obj, diag):
    """Pose frames of each core clip and verify no vertex escapes its bones.

    The invariant: a correctly-weighted vertex rides its bones, so its
    distance to the NEAREST posed bone segment stays comparable to its rest
    distance to the nearest rest segment. A vertex whose ratio explodes is
    being left behind (zero/weak weights) or dragged across the body (weight
    leakage) — precisely the failure modes that made the raw Meshy topology
    unusable. rest distances are floored at 2% of the bbox diagonal so verts
    lying ON a bone (spine surface) don't turn ordinary deformation into a
    huge ratio by dividing by ~0.

    Runs against the FULL weight set in Blender (the exporter's top-4 trim is
    a strictly small perturbation verify_rig.ts re-checks on the real file).

    Returns (worst_ratio, worst_clip, worst_frame, passed)."""
    floor = diag * 0.02

    # Rest state: base mesh vertices (no modifiers) + edit-time bone layout.
    rest_pts = np.empty(len(mesh_obj.data.vertices) * 3, dtype=np.float32)
    mesh_obj.data.vertices.foreach_get('co', rest_pts)
    rest_pts = rest_pts.reshape(-1, 3).astype(np.float64)
    rot = np.array(mesh_obj.matrix_world.to_3x3(), dtype=np.float64)
    loc = np.array(mesh_obj.matrix_world.translation, dtype=np.float64)
    rest_pts = rest_pts @ rot.T + loc

    arm_mat = arm_obj.matrix_world
    bone_names = [bone.name for bone in arm_obj.data.bones]
    bone_col = {name: col for col, name in enumerate(bone_names)}
    rest_segments = []
    for bone in arm_obj.data.bones:
        head = np.array(arm_mat @ bone.head_local, dtype=np.float64)
        tail = np.array(arm_mat @ bone.tail_local, dtype=np.float64)
        rest_segments.append((head, tail))

    # Per-vertex influence mask: which bones MEANINGFULLY drive this vertex.
    # A bone qualifies at weight >= 0.25 ("owns a quarter of the vertex"),
    # and each vertex's TOP-weight bone is always included regardless — so a
    # genuinely escaped vertex (whose primary driver is misplaced) is always
    # measured against that driver. The 0.25 bar exists because minor
    # stabilizer influences are not a tracking promise: a thigh-crease vert
    # 85% bound to the torso and 15% to the thigh correctly stays with the
    # torso when the thigh swings, and grading it against the thigh produced
    # false failures (dog_big 2.07 / ember_leader 1.82 on visually-clean
    # rigs). Verts with no weights at all fall back to the full bone set.
    group_to_col = {}
    for group_index, group in enumerate(mesh_obj.vertex_groups):
        if group.name in bone_col:
            group_to_col[group_index] = bone_col[group.name]
    weight_matrix = np.zeros((len(mesh_obj.data.vertices), len(bone_names)),
                             dtype=np.float64)
    for vertex in mesh_obj.data.vertices:
        for group_entry in vertex.groups:
            col = group_to_col.get(group_entry.group)
            if col is not None:
                weight_matrix[vertex.index, col] = group_entry.weight
    influence = weight_matrix >= 0.25
    has_weights = weight_matrix.max(axis=1) > 0.0
    top_bone = np.argmax(weight_matrix, axis=1)
    influence[np.arange(len(top_bone))[has_weights],
              top_bone[has_weights]] = True
    influence[~has_weights, :] = True

    rest_dist_raw, rest_nearest = masked_min_distances(
        bone_distance_matrix(rest_pts, rest_segments), influence)
    rest_dist = np.maximum(rest_dist_raw, floor)

    # Sample clips one at a time: NLA tracks are muted so ONLY the active
    # action poses the rig (otherwise every clip would evaluate stacked).
    anim = arm_obj.animation_data
    saved_mutes = [(track, track.mute) for track in anim.nla_tracks]
    for track, _ in saved_mutes:
        track.mute = True

    worst = (0.0, 'none', 0)
    worst_detail = None
    for clip_name in CORE_CLIPS:
        action = bpy.data.actions.get(clip_name)
        if action is None:
            continue
        reset_pose(arm_obj)  # clear channels the previous clip keyed
        anim.action = action
        frame_start, frame_end = action.frame_range
        for i in range(DEFORM_SAMPLES_PER_CLIP):
            frame = round(frame_start + (frame_end - frame_start)
                          * i / (DEFORM_SAMPLES_PER_CLIP - 1))
            bpy.context.scene.frame_set(int(frame))
            depsgraph = bpy.context.evaluated_depsgraph_get()
            eval_obj = mesh_obj.evaluated_get(depsgraph)
            eval_mesh = eval_obj.to_mesh()
            pts = np.empty(len(eval_mesh.vertices) * 3, dtype=np.float32)
            eval_mesh.vertices.foreach_get('co', pts)
            pts = pts.reshape(-1, 3).astype(np.float64)
            emat = eval_obj.matrix_world
            pts = pts @ np.array(emat.to_3x3(), dtype=np.float64).T \
                + np.array(emat.translation, dtype=np.float64)
            eval_obj.to_mesh_clear()

            # Iterate by data-bone name so posed segment order matches the
            # rest-segment / bone_names order exactly.
            posed_segments = []
            for name in bone_names:
                pose_bone = arm_obj.pose.bones[name]
                head = np.array(arm_mat @ pose_bone.head, dtype=np.float64)
                tail = np.array(arm_mat @ pose_bone.tail, dtype=np.float64)
                posed_segments.append((head, tail))
            posed_dist, posed_nearest = masked_min_distances(
                bone_distance_matrix(pts, posed_segments), influence)

            ratios = posed_dist / rest_dist
            ratio = float(np.max(ratios))
            if ratio > worst[0]:
                worst = (ratio, clip_name, int(frame))
                # Offender diagnostics: which bones do the escaping verts
                # live around? The histogram localizes the weight problem to
                # a body region without any manual weight-paint inspection.
                offender_indices = np.nonzero(ratios > DEFORM_RATIO_LIMIT)[0]
                histogram = {}
                for vert_idx in offender_indices:
                    key = bone_names[posed_nearest[vert_idx]]
                    histogram[key] = histogram.get(key, 0) + 1
                worst_idx = int(np.argmax(ratios))
                worst_detail = {
                    "offenders": int(len(offender_indices)),
                    "offender_bone_histogram": dict(sorted(
                        histogram.items(), key=lambda kv: -kv[1])[:8]),
                    "worst_vert": worst_idx,
                    "rest_co": [round(float(v), 3) for v in rest_pts[worst_idx]],
                    "posed_co": [round(float(v), 3) for v in pts[worst_idx]],
                    "d_rest_raw": round(float(rest_dist_raw[worst_idx]), 4),
                    "d_pose": round(float(posed_dist[worst_idx]), 4),
                    "nearest_rest_bone": bone_names[rest_nearest[worst_idx]],
                    "nearest_posed_bone": bone_names[posed_nearest[worst_idx]],
                }

    # Restore the exact post-bake_animation_clips state (action cleared, NLA
    # unmuted, rest pose) so the exporter sees the clips the way rig_quadruped
    # left them — and no clip's residue leaks into the export or previews.
    anim.action = None
    reset_pose(arm_obj)
    for track, was_muted in saved_mutes:
        track.mute = was_muted
    bpy.context.scene.frame_set(0)

    passed = worst[0] <= DEFORM_RATIO_LIMIT
    print(f"[retopo_rig] deformation sanity: worst ratio={worst[0]:.3f} "
          f"(clip={worst[1]} frame={worst[2]}, limit {DEFORM_RATIO_LIMIT}) "
          f"-> {'PASS' if passed else 'FAIL'}")
    if worst_detail is not None:
        print("[retopo_rig] deform diagnostics: "
              + json.dumps(worst_detail))
    return worst[0], worst[1], worst[2], passed


# ---------------------------------------------------------------------------
# Stage 6 — previews
# ---------------------------------------------------------------------------

def clear_preview_rig():
    for obj in list(bpy.data.objects):
        if obj.type in {'CAMERA', 'LIGHT'}:
            bpy.data.objects.remove(obj, do_unlink=True)


def setup_preview_scene(center, max_extent):
    """Neutral-grey world + 3-point sun lighting + 3/4 camera, mirroring
    render_cat.py's diagnostic look so previews stay comparable across the
    whole asset toolchain. 'Standard' view transform (not AgX) keeps the
    baked albedo colors faithful for the side-by-side against the raw
    import."""
    scene = bpy.context.scene
    world = bpy.data.worlds[0] if bpy.data.worlds else bpy.data.worlds.new("W")
    scene.world = world
    world.use_nodes = True
    background = world.node_tree.nodes.get('Background')
    if background:
        background.inputs[0].default_value = (0.22, 0.23, 0.26, 1.0)
        background.inputs[1].default_value = 1.0

    bpy.ops.object.light_add(type='SUN', location=(4, -4, 6))
    key = bpy.context.active_object
    key.data.energy = 3.5
    key.rotation_euler = (math.radians(55), 0, math.radians(45))
    bpy.ops.object.light_add(type='SUN', location=(-3, 3, 4))
    fill = bpy.context.active_object
    fill.data.energy = 1.3
    fill.data.color = (0.75, 0.85, 1.0)
    fill.rotation_euler = (math.radians(45), 0, math.radians(-135))

    dist = max_extent * 2.5
    position = center + Vector((dist * 0.7, -dist * 0.7, max_extent * 0.3))
    bpy.ops.object.camera_add(location=position)
    cam = bpy.context.active_object
    direction = (center - position).normalized()
    cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = direction.to_track_quat('-Z', 'Y')
    cam.data.lens = 35
    scene.camera = cam

    scene.render.engine = 'BLENDER_EEVEE_NEXT'
    scene.render.resolution_x = 640
    scene.render.resolution_y = 640
    scene.view_settings.view_transform = 'Standard'


def render_to(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print(f"[retopo_rig] preview -> {path}")


def render_single_preview(mesh_obj, path):
    lo, hi = mesh_world_extents(mesh_obj)
    center = Vector(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2,
                     (lo[2] + hi[2]) / 2))
    setup_preview_scene(center, float(np.max(hi - lo)))
    render_to(path)
    clear_preview_rig()


def render_posed_previews(mesh_obj, arm_obj, previews_dir, base_name):
    """Rest / mid-walk / attack-apex previews of the final rigged model.
    Frame choices are tied to the clip authoring in rig_quadruped:
      - walk at 25% of the cycle = thigh_L max forward swing (legs visibly
        split — the pose that exposes bad shoulder/hip weights fastest);
      - attack at 58% = the authored strike peak (head/paw extension apex).
    NLA tracks are muted so exactly one action poses the rig per render."""
    anim = arm_obj.animation_data
    for track in anim.nla_tracks:
        track.mute = True

    lo, hi = mesh_world_extents(mesh_obj)
    center = Vector(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2,
                     (lo[2] + hi[2]) / 2))
    setup_preview_scene(center, float(np.max(hi - lo)))

    paths = {}
    poses = [('rest', None, 0.0), ('walk', 'walk', 0.25),
             ('attack', 'attack', 0.58)]
    for label, clip_name, phase in poses:
        reset_pose(arm_obj)  # each pose starts from a clean rest state
        if clip_name is None:
            anim.action = None
            bpy.context.scene.frame_set(0)
        else:
            action = bpy.data.actions.get(clip_name)
            if action is None:
                continue
            anim.action = action
            start, end = action.frame_range
            bpy.context.scene.frame_set(int(round(start + (end - start) * phase)))
        path = os.path.join(previews_dir, f"{base_name}_{label}.png")
        render_to(path)
        paths[label] = path

    anim.action = None
    reset_pose(arm_obj)
    for track in anim.nla_tracks:
        track.mute = False
    bpy.context.scene.frame_set(0)
    clear_preview_rig()
    return paths


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    opts = parse_args()
    output_path = os.path.abspath(opts["output"])
    base_name = os.path.splitext(os.path.basename(output_path))[0]
    previews_dir = os.path.join(os.path.dirname(output_path), "previews")
    os.makedirs(previews_dir, exist_ok=True)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    print(f"[retopo_rig] input={opts['input']} output={output_path} "
          f"species={opts['species']} target_tris={opts['target_tris']} "
          f"tex={opts['tex_size']} flip_forward={opts['flip_forward']}")

    source_obj = import_and_prepare(opts["input"])
    diag = bbox_diagonal(source_obj)

    # Original preview FIRST: the raw Meshy import (its own materials and
    # textures) is the reference the orchestrator compares the retopo output
    # against; it must be rendered before the source is consumed by the bake.
    render_single_preview(source_obj,
                          os.path.join(previews_dir,
                                       f"{base_name}_original.png"))

    retopo_obj, final_tris, voxel_used = retopologize(source_obj,
                                                      opts["target_tris"])
    smart_uv_project(retopo_obj)
    bake_img, bake_bad_frac, bake_extrusion, bake_attempts = bake_texture(
        source_obj, retopo_obj, opts["tex_size"], diag)
    bake_ok = bake_bad_frac < BAD_TEXEL_LIMIT

    # The high-poly source's job (silhouette reference + bake donor) is done;
    # it must not reach the export or the tri budget is instantly blown.
    bpy.data.objects.remove(source_obj, do_unlink=True)

    # Rig with the shared quadruped pipeline. cleanup_mesh is deliberately
    # SKIPPED: the voxel remesh already produced manifold, consistent-normal,
    # duplicate-free topology, and remove_doubles would only risk disturbing
    # the freshly-baked UV seam layout for zero heat-diffusion benefit.
    rq.align_mesh_to_world(retopo_obj)  # no-op unless something drifted
    bbox = rq.analyze_bbox(retopo_obj, opts["flip_forward"])
    anatomy = rq.detect_anatomy(retopo_obj, bbox)
    anatomy = refine_anatomy(retopo_obj, bbox, anatomy)
    arm_obj = rq.build_armature(bbox, opts["species"], anatomy=anatomy)
    rq.parent_with_auto_weights(retopo_obj, arm_obj)
    rq.bake_animation_clips(arm_obj)

    worst_ratio, worst_clip, worst_frame, deform_ok = deformation_sanity(
        retopo_obj, arm_obj, diag)

    # Export even when a validation failed: the file + previews are the
    # evidence the operator needs to diagnose, and the exit code still marks
    # the run red for any batch wrapper.
    try:
        rq.export_glb(output_path)
    except RuntimeError as err:
        print(str(err), file=sys.stderr)
        sys.exit(4)

    preview_paths = render_posed_previews(retopo_obj, arm_obj, previews_dir,
                                          base_name)
    preview_paths['original'] = os.path.join(previews_dir,
                                             f"{base_name}_original.png")

    if not deform_ok and worst_clip in bpy.data.actions:
        # Render the exact failing pose so the deform-sanity number can be
        # judged visually: a genuinely exploded vertex is obvious, a benign
        # knee-fold artifact equally so.
        anim = arm_obj.animation_data
        for track in anim.nla_tracks:
            track.mute = True
        reset_pose(arm_obj)
        anim.action = bpy.data.actions[worst_clip]
        bpy.context.scene.frame_set(worst_frame)
        lo, hi = mesh_world_extents(retopo_obj)
        center = Vector(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2,
                         (lo[2] + hi[2]) / 2))
        setup_preview_scene(center, float(np.max(hi - lo)))
        debug_path = os.path.join(previews_dir,
                                  f"{base_name}_deform_worst.png")
        render_to(debug_path)
        preview_paths['deform_worst'] = debug_path
        anim.action = None
        reset_pose(arm_obj)
        for track in anim.nla_tracks:
            track.mute = False
        clear_preview_rig()

    summary = {
        "output": output_path,
        "tris": final_tris,
        "voxel_size": round(voxel_used, 6),
        "bake": {
            "bad_texel_fraction": round(bake_bad_frac, 5),
            "cage_extrusion": round(bake_extrusion, 5),
            "attempts": bake_attempts,
            "ok": bake_ok,
        },
        "deform": {
            "worst_ratio": round(worst_ratio, 4),
            "clip": worst_clip,
            "frame": worst_frame,
            "ok": deform_ok,
        },
        "previews": preview_paths,
    }
    print("RETOPO_RIG_SUMMARY_JSON: " + json.dumps(summary))

    if not bake_ok:
        sys.exit(6)
    if not deform_ok:
        sys.exit(5)


if __name__ == "__main__":
    main()
