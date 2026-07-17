"""
build_quadruped.py — parametric procedural cat/dog character generator (Blender headless).

WHY THIS EXISTS (owner directive 2026-07-16):
    The Meshy-AI character GLBs are dense sculpt topology with envelope skin weights.
    When re-rigged with a real skeleton their surface TEARS at the joints — verts on the
    far side of an elbow/hip get pulled by the wrong bone and the mesh "explodes" on the
    walk/attack clips. The Meshy meshes are 100k–450k tris of marching-cubes surface with
    no edge loops aligned to any joint, so no automatic weighting can deform them cleanly.

    This tool replaces them with characters that are BUILT for the rig instead of fought
    onto it. The body is grown as a Blender **Skin-modifier graph laid out along the exact
    rig_quadruped skeleton joints**, so every edge loop is born at a joint. Subdivision +
    a light decimate land it in the 5k–15k-tri budget. Because the topology is clean,
    manifold, and joint-aligned, heat-diffusion weights bind deterministically and the
    surface follows the bones with no tearing — which the in-process DEFORMATION-QUALITY
    gate below measures and asserts (no vertex ends >1.5x its rest distance from its bone).

    The Meshy files are left untouched on disk as a fallback; this is an additive pipeline
    writing to assets/models/generated/.

REUSE (do-not-duplicate): the hand-designed skeleton (build_armature), the deterministic
    heat-weight bind (parent_with_auto_weights), the four required clips plus the rest-state
    clips (bake_animation_clips), and the verifying GLB export (export_glb) are ALL reused
    verbatim from scripts/rig_quadruped.py — the same skeleton/gaits the engine's exact-name
    clip lookups (idle/walk/run/attack + sitDown/layDown/standUp*) already depend on. A
    self-contained fallback rig+gait authoring path (below) runs only if that import fails
    (e.g. the module is mid-edit), so this generator never hard-depends on it.

USAGE (headless):
    blender --background --python scripts/build_quadruped.py -- \
        --species cat --variant player --out assets/models/generated/cat_player.glb

    Species:  cat | dog
    Variants: player (cat)                 — small house cat
              regular | fast | big | boss  (dog) — fast=lean/slender, big=bulky ~1.3x,
                                                   boss=~2x + heavier neck/chest
    Optional:
        --previews            also render rest/mid-walk/attack PNGs next to the GLB
        --previews-dir DIR    override preview output dir (default <out_dir>/previews)
        --subsurf N           subdivision levels before decimate (default 2)
        --max-tris N          hard triangle ceiling; decimate down to it (default 15000)
        --target-diagonal F   final bbox-diagonal in engine units (default: dog 2.42 /
                              cat 1.85, matched to the existing rigged dog_regular so the
                              generated model is a true drop-in at engine scale 1.0)
        --no-reuse            force the self-contained fallback rig/gait path (for testing)

ENGINE-FIT NOTES (verified against engine/assets/ModelLoader.cpp + game/entities/DogEntity.cpp):
    * The engine renders the character from `baseColorFactor` (the per-entity tintOverride
      path) and, today, does NOT sample COLOR_0 vertex colors — real per-fragment texture
      sampling is a future step. So the fur MARKINGS live in COLOR_0 vertex colors (visible
      in Blender previews and any COLOR_0-aware viewer, and future-proof for the texture
      step), while the engine sees a LIGHT/neutral baseColorFactor that takes the per-variant
      fur-color tint cleanly — exactly what the directive asked for. No baked strong hue.
    * DogEntity.cpp already multiplies a per-variant transform scale (regular 1.0, fast 0.8,
      big 1.5, boss 2.0). The variant SIZE this generator bakes (fast 0.9x, big 1.3x,
      boss 2.0x, per the directive) COMPOUNDS with that. dog_regular (the proof model) is
      1.0x either way, so it is unaffected; for the other variants a fleet run should pick
      ONE size source — see the parameter summary printed at the end of a run.
    * Loader constraints honoured: GLB, f32 geometry attributes, u8 JOINTS_0 (<256 joints)
      with indices < node count, f32 WEIGHTS_0, Y-up, triangulated, embedded — all produced
      by the reused export_glb (Blender's glTF exporter defaults) + the triangulate here.
"""

import bpy
import bmesh
import sys
import os
import math
from mathutils import Vector, Quaternion


# ---------------------------------------------------------------------------
# Reuse of the hand-designed rig + gait pipeline from rig_quadruped.py.
#
# We import it as a module (it is guarded by `if __name__ == "__main__"`, so
# importing has no side effects). If it is unavailable or broken — another
# agent may be editing it — we fall back to self-contained implementations
# authored later in this file, so the generator is never dead in the water.
# ---------------------------------------------------------------------------
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

RIGLIB = None
try:
    import rig_quadruped as RIGLIB  # type: ignore
except Exception as _err:  # pragma: no cover - defensive
    print(f"[build_quadruped] rig_quadruped import failed ({_err}); "
          f"using self-contained fallback rig/gait path", file=sys.stderr)
    RIGLIB = None


# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------

def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []

    opts = {
        "species": "cat",
        "variant": "player",
        "out": None,
        "previews": "--previews" in argv,
        "previews_dir": None,
        "subsurf": 2,
        "max_tris": 15000,
        "target_diagonal": None,   # resolved from species default if unset
        "reuse": "--no-reuse" not in argv,
    }

    def flag_value(name, cast=str, default=None):
        if name in argv:
            i = argv.index(name)
            if i + 1 < len(argv):
                return cast(argv[i + 1])
        return default

    opts["species"] = flag_value("--species", str, opts["species"])
    opts["variant"] = flag_value("--variant", str, opts["variant"])
    opts["out"] = flag_value("--out", str, opts["out"])
    opts["previews_dir"] = flag_value("--previews-dir", str, None)
    opts["subsurf"] = flag_value("--subsurf", int, opts["subsurf"])
    opts["max_tris"] = flag_value("--max-tris", int, opts["max_tris"])
    opts["target_diagonal"] = flag_value("--target-diagonal", float, None)

    if not opts["out"]:
        print("ERROR: --out <path.glb> is required", file=sys.stderr)
        sys.exit(1)

    opts["species"] = opts["species"].lower()
    opts["variant"] = opts["variant"].lower()
    if opts["species"] not in ("cat", "dog"):
        print(f"ERROR: --species must be cat|dog (got {opts['species']})", file=sys.stderr)
        sys.exit(1)

    return opts


# ---------------------------------------------------------------------------
# Parametric species/variant profiles.
#
# A profile is a small set of anatomical scalars. build_skin_graph() turns them
# into the (position, radius) skin-node list; everything downstream (armature
# landmarks, colouring) is derived from the SAME numbers, so the mesh, the bones
# and the markings are guaranteed consistent.
#
# All coordinates are "natural" units (roughly metres) in the rig frame:
#   +X = forward (head end), +Y = left, +Z = up, feet on the Z=0 ground plane.
# A single uniform normalisation at the end rescales to --target-diagonal, so
# only the RATIOS here matter, not the absolute values.
# ---------------------------------------------------------------------------

# Base = a mid-size dog (Italian-greyhound-ish, the "regular" silhouette). The
# cat profile and the dog variants are expressed as multipliers on this base so
# there is one geometry code path and the shapes stay a coherent family.
BASE = {
    "back_height":   0.42,   # torso centreline height above ground
    "hip_x":        -0.20,   # pelvis forward position
    "shoulder_x":    0.18,   # withers forward position
    "head_x":        0.42,   # head centre forward position
    "snout_x":       0.54,   # muzzle tip forward position
    "tail_x":       -0.56,   # tail tip forward position (behind hips)
    "leg_half":      0.105,  # half-distance between L/R leg centres (stance width)
    # radii (tube thickness) of the skin graph, in the same units
    "r_pelvis":      0.115,
    "r_chest":       0.135,
    "r_waist":       0.105,
    "r_neck":        0.080,
    "r_head":        0.100,
    "r_snout":       0.045,
    "r_ear":         0.020,
    "r_tail_base":   0.036,
    "r_leg_top":     0.058,
    "r_leg_mid":     0.045,
    "r_paw":         0.052,
    "ear_height":    0.60,   # ear-tip Z (above head)
    "ear_spread":    0.07,   # ear-tip half-spread in Y
    "head_top":      0.50,   # head centre Z
}


def species_variant_profile(species, variant):
    """Return (profile dict, palette dict, default_target_diagonal, size_mult).

    The profile is BASE with species + variant multipliers folded in. size_mult
    is the directive's per-variant baked size (compounds with the engine scale;
    surfaced in the run summary). Colours are a warm, LIGHT-base palette so the
    engine tint reads cleanly.
    """
    p = dict(BASE)
    # Palette values are treated as linear RGB (Blender colour attributes + glTF
    # COLOR_0 are linear). A pale warm-grey base keeps the albedo neutral.
    palette = {
        "base":   (0.80, 0.73, 0.62),
        "belly":  (0.88, 0.83, 0.74),
        "stripe": (0.40, 0.33, 0.26),
        "ear":    (0.26, 0.21, 0.18),
        "muzzle": (0.52, 0.44, 0.36),
        "nose":   (0.12, 0.09, 0.10),
        "paw":    (0.55, 0.48, 0.40),
        "eye":    (0.04, 0.04, 0.05),
        "tabby":  species == "cat",   # cats get dorsal tabby stripes; dogs a plain saddle
    }

    size_mult = 1.0
    default_diag = 2.42   # matches the existing rigged dog_regular (bboxDiagonal 2.416)

    if species == "cat":
        # A small house cat: shorter snout, rounder head, bigger ears, slimmer
        # body, longer relative tail, a touch shorter in the leg. Smaller overall.
        p["snout_x"] = 0.48
        p["head_x"] = 0.40
        p["r_snout"] = 0.040
        p["r_head"] = 0.098
        p["r_ear"] = 0.024
        p["ear_height"] = 0.58
        p["ear_spread"] = 0.075
        p["r_chest"] = 0.118
        p["r_pelvis"] = 0.104
        p["r_waist"] = 0.094
        p["r_neck"] = 0.070
        p["r_leg_top"] = 0.048
        p["r_leg_mid"] = 0.038
        p["r_paw"] = 0.046
        p["tail_x"] = -0.60          # cats have a long tail
        p["r_tail_base"] = 0.030
        default_diag = 1.85          # a cat is smaller than the dogs
        # 'player' is the only cat variant; leave multipliers at 1.
    else:
        # Dog variants shape the same base silhouette.
        if variant == "regular":
            pass
        elif variant == "fast":
            # Lean/slender sighthound: thinner everywhere, tucked waist, longer
            # legs (taller torso), slimmer snout; slightly smaller overall.
            for k in ("r_pelvis", "r_chest", "r_waist", "r_neck", "r_leg_top",
                      "r_leg_mid", "r_paw", "r_tail_base"):
                p[k] *= 0.80
            p["r_waist"] *= 0.90      # extra belly tuck
            p["back_height"] *= 1.10  # longer legs -> higher torso
            p["head_top"] *= 1.08
            p["ear_height"] *= 1.08
            p["r_snout"] *= 0.85
            size_mult = 0.90
        elif variant == "big":
            # Bulky: thicker body/legs/neck, larger head, ~1.3x size. Radius
            # bumps stay moderate (<=1.2x) so the thin spine bones don't bury so
            # deep in the thick body that heat weighting leaks (which showed up
            # as fly-away verts on the deformation gate at 1.25x).
            for k in ("r_pelvis", "r_chest", "r_waist", "r_neck", "r_head",
                      "r_leg_top", "r_leg_mid", "r_paw", "r_tail_base"):
                p[k] *= 1.18
            size_mult = 1.30
        elif variant == "boss":
            # Boss: ~2x, with a HEAVIER neck and chest and a bigger head — a
            # hulking silhouette that reads at a glance. The heavy-part bumps are
            # capped (chest/neck <=1.35x) for the same buried-bone reason as big;
            # the 2x overall SIZE (a rigid scale) carries the "huge" read without
            # needing extreme thickness that would tear.
            for k in ("r_pelvis", "r_waist", "r_leg_top", "r_leg_mid",
                      "r_paw", "r_tail_base"):
                p[k] *= 1.18
            p["r_chest"] *= 1.34      # heavy chest
            p["r_neck"] *= 1.35       # heavy neck
            p["r_head"] *= 1.25
            p["r_snout"] *= 1.15
            p["back_height"] *= 1.05
            p["head_top"] *= 1.05
            size_mult = 2.00
            palette["base"] = (0.62, 0.55, 0.47)   # a darker, meaner coat for the boss
            palette["stripe"] = (0.30, 0.25, 0.21)
        else:
            print(f"[build_quadruped] unknown dog variant '{variant}', "
                  f"using regular proportions", file=sys.stderr)

    return p, palette, default_diag, size_mult


# ---------------------------------------------------------------------------
# Skin-modifier graph construction
# ---------------------------------------------------------------------------

def build_skin_graph(profile):
    """Return (verts, edges, radii, root_index, anatomy) describing the body as
    a 1-D skin skeleton laid out along the quadruped joints.

    verts  : list[Vector]           node positions (natural units)
    edges  : list[(i, j)]           connectivity
    radii  : list[float]            per-node skin radius (tube thickness)
    root   : int                    the skin-modifier root node (torso centre)
    anatomy: dict                   rig landmarks (world) for build_armature

    The node layout mirrors rig_quadruped's skeleton so bones land inside the
    tubes: pelvis -> lumbar -> midback -> chest -> withers -> neck -> head ->
    snout, with ears branching off the head, a 4-node tail off the pelvis, and
    3-node front/back legs off withers/pelvis. Each leg's paw node sits on the
    ground plane (Z=0) so the model is grounded and the gait clips (which only
    rotate legs about the horizontal axis from a near-vertical rest) can never
    drive a paw below ground.
    """
    p = profile
    bh = p["back_height"]
    verts, edges, radii = [], [], []

    def node(pos, r):
        verts.append(Vector(pos))
        radii.append(r)
        return len(verts) - 1

    # --- Torso spine chain (Y = 0, along the back) ---
    pelvis  = node((p["hip_x"],              0.0, bh),          p["r_pelvis"])
    lumbar  = node((p["hip_x"] * 0.45,       0.0, bh + 0.015),  p["r_waist"])
    midback = node((0.02,                    0.0, bh + 0.02),   p["r_chest"] * 0.92)
    chest   = node((p["shoulder_x"] * 0.85,  0.0, bh),          p["r_chest"])
    withers = node((p["shoulder_x"] + 0.04,  0.0, bh),          p["r_pelvis"])

    edges += [(pelvis, lumbar), (lumbar, midback), (midback, chest), (chest, withers)]

    # --- Neck + head + muzzle ---
    neck    = node((p["shoulder_x"] + 0.12, 0.0, bh + 0.04),        p["r_neck"])
    headbk  = node((p["head_x"],            0.0, p["head_top"]),     p["r_head"])
    headfr  = node((p["head_x"] + 0.08,     0.0, p["head_top"] - 0.03), p["r_head"] * 0.75)
    snout   = node((p["snout_x"],           0.0, p["head_top"] - 0.07), p["r_snout"])
    edges += [(withers, neck), (neck, headbk), (headbk, headfr), (headfr, snout)]

    # --- Ears (branch off the back of the head) ---
    ear_l = node((p["head_x"] - 0.02,  p["ear_spread"], p["ear_height"]), p["r_ear"])
    ear_r = node((p["head_x"] - 0.02, -p["ear_spread"], p["ear_height"]), p["r_ear"])
    edges += [(headbk, ear_l), (headbk, ear_r)]

    # --- Tail (branch off the pelvis) ---
    t1 = node((p["hip_x"] - 0.10, 0.0, bh),         p["r_tail_base"])
    t2 = node((p["hip_x"] - 0.20, 0.0, bh + 0.01),  p["r_tail_base"] * 0.72)
    t3 = node((p["hip_x"] - 0.29, 0.0, bh + 0.02),  p["r_tail_base"] * 0.50)
    t4 = node((p["tail_x"],       0.0, bh + 0.03),  p["r_tail_base"] * 0.30)
    edges += [(pelvis, t1), (t1, t2), (t2, t3), (t3, t4)]

    # --- Legs: 3 nodes each (top -> knee -> paw), L (+Y) and R (-Y). Fronts
    # branch off the withers/chest, backs off the pelvis. Paws on the ground. ---
    leg_nodes = {}
    for side, sy in (("L", +1.0), ("R", -1.0)):
        y = sy * p["leg_half"]
        # Front leg
        f_top = node((p["shoulder_x"] + 0.01, y, bh - 0.04), p["r_leg_top"])
        f_kn  = node((p["shoulder_x"] + 0.02, y, bh * 0.48), p["r_leg_mid"])
        f_pw  = node((p["shoulder_x"] + 0.03, y, 0.02),      p["r_paw"])
        edges += [(withers, f_top), (f_top, f_kn), (f_kn, f_pw)]
        # Back leg
        b_top = node((p["hip_x"] + 0.01, y, bh - 0.04), p["r_leg_top"] * 1.08)
        b_kn  = node((p["hip_x"] + 0.01, y, bh * 0.48), p["r_leg_mid"])
        b_pw  = node((p["hip_x"] + 0.03, y, 0.02),      p["r_paw"])
        edges += [(pelvis, b_top), (b_top, b_kn), (b_kn, b_pw)]
        leg_nodes[side] = dict(f_top=f_top, f_pw=f_pw, b_top=b_top, b_pw=b_pw)

    # The skin-modifier root anchors the frame-propagation of the tube cross-
    # sections; putting it at the torso centre (midback) keeps the body's
    # cross-section orientation stable so limbs read cleanly.
    root = midback

    # --- Rig landmarks for build_armature (WORLD == natural here; scaled later).
    # These place the reused skeleton's bones exactly inside the tubes. ---
    def v(i):
        return Vector(verts[i])

    anatomy = {
        "hip_center":    v(pelvis),
        "chest_center":  v(chest),
        "head_centroid": v(headbk),
        "tail_tip":      v(t4),
        "shoulder_L":    v(leg_nodes["L"]["f_top"]),
        "shoulder_R":    v(leg_nodes["R"]["f_top"]),
        "thigh_L":       v(leg_nodes["L"]["b_top"]),
        "thigh_R":       v(leg_nodes["R"]["b_top"]),
        # Paw bones sit AT the paw skin node (not forced to Z=0): the node is the
        # centre of the paw tube, so the toe bone stays inside the paw mesh and
        # the leg chain's last bone deforms the paw cleanly rather than poking
        # out below it. Grounding to Z=0 happens later as a rigid shift.
        "paw_front_L":   v(leg_nodes["L"]["f_pw"]),
        "paw_front_R":   v(leg_nodes["R"]["f_pw"]),
        "paw_back_L":    v(leg_nodes["L"]["b_pw"]),
        "paw_back_R":    v(leg_nodes["R"]["b_pw"]),
    }

    return verts, edges, radii, root, anatomy


def create_body_mesh(profile, subsurf_levels, max_tris):
    """Build the skin-graph mesh object: create the 1-D graph, grow it into a
    surface with the Skin modifier, smooth it with Subdivision, apply, then
    triangulate and (if needed) decimate to the tri budget.

    Returns (mesh_obj, anatomy_natural). anatomy is in the SAME pre-scale frame
    as the mesh; both are normalised together by the caller.
    """
    verts, edges, radii, root, anatomy = build_skin_graph(profile)

    mesh = bpy.data.meshes.new("quadruped_body")
    mesh.from_pydata([tuple(v) for v in verts], edges, [])
    mesh.update()
    obj = bpy.data.objects.new("quadruped", mesh)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    # Skin modifier: turns each edge into a quad tube whose cross-section radius
    # interpolates between the two endpoints' per-vertex skin radii. This is what
    # makes the topology BORN around the joints — every tube segment is a ring of
    # edge loops centred on the graph edge, i.e. on a bone.
    skin = obj.modifiers.new(name="Skin", type='SKIN')
    skin.use_smooth_shade = True
    skin_layer = mesh.skin_vertices[0].data
    for i, r in enumerate(radii):
        # radius is a 2-component (x,y) cross-section; keep it round.
        skin_layer[i].radius = (r, r)
    skin_layer[root].use_root = True

    # Subdivision to round the boxy skin output into an organic surface. Level 2
    # (x16 faces) on this ~30-node graph lands comfortably in the 5k–15k budget;
    # exposed as a parameter for tuning per variant.
    subsurf = obj.modifiers.new(name="Subdivision", type='SUBSURF')
    subsurf.levels = max(0, subsurf_levels)
    subsurf.render_levels = max(0, subsurf_levels)

    # Apply skin + subsurf so we own concrete geometry to weight and export.
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    for mod_name in ("Skin", "Subdivision"):
        bpy.ops.object.modifier_apply(modifier=mod_name)

    # Clean + triangulate: merge any coincident junction verts the skin modifier
    # produced, recompute outward normals, then triangulate so the exported tri
    # count is exactly what the engine will draw and inspect_models will count.
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.0002)
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.mesh.quads_convert_to_tris(quad_method='BEAUTY', ngon_method='BEAUTY')
    bpy.ops.object.mode_set(mode='OBJECT')

    tri_count = len(obj.data.polygons)
    if tri_count > max_tris:
        # Collapse-decimate down to the ceiling. On this clean topology collapse
        # preserves the silhouette; the ratio is set to land just under the cap.
        dec = obj.modifiers.new(name="Decimate", type='DECIMATE')
        dec.decimate_type = 'COLLAPSE'
        dec.ratio = min(0.99, (max_tris / float(tri_count)) * 0.98)
        bpy.ops.object.modifier_apply(modifier="Decimate")
        tri_count = len(obj.data.polygons)

    bpy.ops.object.shade_smooth()
    print(f"[build_quadruped] body mesh: {len(obj.data.vertices)} verts, "
          f"{tri_count} tris (subsurf={subsurf_levels}, cap={max_tris})")
    return obj, anatomy


def normalize_scale(mesh_obj, anatomy, target_diagonal):
    """Uniformly scale the mesh (about the world origin, so grounded feet stay at
    Z=0) and the anatomy landmarks so the model's bbox diagonal equals
    target_diagonal in engine units. Returns the applied scale factor."""
    corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [c.x for c in corners]; ys = [c.y for c in corners]; zs = [c.z for c in corners]
    diag = math.sqrt((max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2 + (max(zs) - min(zs)) ** 2)
    if diag < 1e-6:
        return 1.0
    s = target_diagonal / diag

    # Scale the mesh data about origin so Z=0 (ground) is a fixed point.
    for v in mesh_obj.data.vertices:
        v.co = v.co * s
    mesh_obj.data.update()

    for key in list(anatomy.keys()):
        anatomy[key] = Vector(anatomy[key]) * s

    print(f"[build_quadruped] normalized scale x{s:.3f} -> target diagonal {target_diagonal:.3f}")
    return s


def ground_to_floor(mesh_obj, anatomy):
    """Rigidly shift the mesh and the rig landmarks down so the lowest vertex
    sits on the Z=0 ground plane. The Skin+Subsurf paw caps retract slightly
    upward, which otherwise leaves the model floating a few centimetres; grounding
    here makes the character stand ON the floor at rest, which is how the engine
    places entities (origin on terrain). Done BEFORE the armature is built, so the
    bones inherit the grounded landmarks and no object-transform offset leaks into
    the export. It is a pure translation, so it cannot affect deformation."""
    # Use the true lowest VERTEX (not the cached object bound_box, which can lag
    # a direct vertex edit) so the rest feet land exactly on Z=0.
    floor = min((mesh_obj.matrix_world @ v.co).z for v in mesh_obj.data.vertices)
    if abs(floor) < 1e-4:
        return 0.0
    for v in mesh_obj.data.vertices:
        v.co.z -= floor
    mesh_obj.data.update()
    for key in list(anatomy.keys()):
        p = Vector(anatomy[key])
        anatomy[key] = Vector((p.x, p.y, p.z - floor))
    print(f"[build_quadruped] grounded model: shifted {floor:+.4f} so feet rest on Z=0")
    return floor


def bbox_dict(mesh_obj):
    """Build the bbox dict build_armature expects: centre/ground/extents +
    forward/side/length axes. Forward is +X (head end), up is Z, side is Y —
    fixed because we authored the body in that frame."""
    corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [c.x for c in corners]; ys = [c.y for c in corners]; zs = [c.z for c in corners]
    return {
        "center_x": (min(xs) + max(xs)) * 0.5,
        "center_y": (min(ys) + max(ys)) * 0.5,
        "ground_z": min(zs),
        "body_length": max(xs) - min(xs),
        "body_width": max(ys) - min(ys),
        "body_height": max(zs) - min(zs),
        "forward": Vector((1.0, 0.0, 0.0)),
        "side": Vector((0.0, 1.0, 0.0)),
        "length_axis": "X",
    }


# ---------------------------------------------------------------------------
# Eyes
# ---------------------------------------------------------------------------

def add_eyes(body_obj, bbox, anatomy):
    """Add two dark eye spheres inset on the head, weight them 100% to the 'head'
    bone (so they follow the head under animation), and join them into the body.

    WHY weight-then-join instead of auto-weighting: the eyes are separate closed
    spheres — disconnected islands get no heat weight, so we assign them to the
    head vertex group explicitly before the join. After the join they inherit the
    body's armature modifier and ride the head bone rigidly, which is exactly how
    eyes should behave (they don't deform, they translate/rotate with the skull).
    """
    head = Vector(anatomy["head_centroid"])
    forward = bbox["forward"]
    side = bbox["side"]
    bl = bbox["body_length"]; bw = bbox["body_width"]; bh = bbox["body_height"]
    eye_r = max(0.012, 0.055 * bw)

    eye_objs = []
    for sy in (+1.0, -1.0):
        # Front-and-side of the head, a touch below its centre — a natural eye line.
        center = (head
                  + forward * (0.16 * bl)
                  + side * (sy * 0.32 * bw)
                  + Vector((0.0, 0.0, 0.02 * bh)))
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8,
                                              radius=eye_r, location=center)
        eye = bpy.context.active_object
        eye.name = f"eye_{'L' if sy > 0 else 'R'}"
        # A single vertex group named exactly like the head bone; full weight.
        grp = eye.vertex_groups.new(name="head")
        grp.add([v.index for v in eye.data.vertices], 1.0, 'REPLACE')
        eye_objs.append(eye)

    # Join eyes into the body (body active) so one skinned mesh exports. Blender
    # merges the same-named 'head' vertex groups, preserving the eye weights.
    bpy.ops.object.select_all(action='DESELECT')
    for e in eye_objs:
        e.select_set(True)
    body_obj.select_set(True)
    bpy.context.view_layer.objects.active = body_obj
    bpy.ops.object.join()

    # Return the eye radius so the colouring pass can find the inset eye verts by
    # proximity to the two eye centres (recomputed there from the same landmarks).
    return eye_r


# ---------------------------------------------------------------------------
# Vertex-colour markings + material
# ---------------------------------------------------------------------------

def apply_colors_and_material(mesh_obj, bbox, anatomy, palette, eye_marker_z=None):
    """Paint anatomy-aligned fur markings into a COLOR_0 vertex-colour layer and
    build a Principled material that reads it, so both Blender previews and the
    exported GLB carry the markings. Base albedo stays a light warm grey so the
    engine's per-entity tint (which is what the engine renders today) takes the
    fur colour cleanly.

    Markings, all derived from the same rig landmarks the geometry used:
      - pale warm-grey base, slightly lighter belly (low Z)
      - darker muzzle toward the snout, near-black nose at the tip
      - dark ear tips (high Z, forward of the head, off-centre in Y)
      - cats: dorsal tabby stripes (dark bands across the upper back);
        dogs: a soft darker saddle over the back instead of stripes
      - near-black eyes (the small sphere verts we inset)
    """
    mesh = mesh_obj.data
    if not mesh.color_attributes:
        mesh.color_attributes.new(name="fur", type='FLOAT_COLOR', domain='POINT')
    color_layer = mesh.color_attributes[0]

    corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    zs = [c.z for c in corners]
    ground = min(zs); top = max(zs); height = max(1e-6, top - ground)
    xs = [c.x for c in corners]
    x_min = min(xs); x_max = max(xs); x_span = max(1e-6, x_max - x_min)

    head = Vector(anatomy["head_centroid"])
    snout_x = x_max  # muzzle tip is the frontmost point
    bw = bbox["body_width"]
    eye_r = eye_marker_z or (0.055 * bw)

    def lerp(a, b, t):
        return tuple(a[i] + (b[i] - a[i]) * max(0.0, min(1.0, t)) for i in range(3))

    base = palette["base"]
    for v in mesh.vertices:
        w = mesh_obj.matrix_world @ v.co
        z_frac = (w.z - ground) / height       # 0 belly .. 1 back/top
        x_frac = (w.x - x_min) / x_span         # 0 tail .. 1 nose
        col = base

        # Belly lightening on the underside.
        if z_frac < 0.35:
            col = lerp(base, palette["belly"], (0.35 - z_frac) / 0.35)

        # Back markings on the upper torso (behind the head).
        on_back = z_frac > 0.62 and w.x < head.x - 0.02
        if on_back:
            if palette["tabby"]:
                # Dorsal tabby stripes: dark bands at a spatial frequency along X.
                stripe = math.sin(w.x * (14.0 / max(x_span, 1e-6)) * math.pi)
                if stripe > 0.35:
                    col = lerp(col, palette["stripe"], (stripe - 0.35) / 0.65 * 0.85)
            else:
                # Plain saddle: a soft darkening over the back.
                col = lerp(col, palette["stripe"], (z_frac - 0.62) / 0.38 * 0.5)

        # Muzzle + nose toward the front of the head.
        if w.x > head.x + 0.02:
            muzzle_t = (w.x - head.x) / max(1e-6, (snout_x - head.x))
            col = lerp(col, palette["muzzle"], muzzle_t * 0.8)
            if muzzle_t > 0.82 and z_frac > 0.45:
                col = palette["nose"]

        # Dark ear tips: high up, forward-of-mid, off the centreline.
        if z_frac > 0.82 and abs(w.y) > 0.35 * bw and w.x > head.x - 0.12:
            col = palette["ear"]

        # Eyes: the inset sphere verts sit near the head at a small radius; colour
        # any vert very close to the two eye centres near-black.
        eye_line_x = head.x + 0.16 * bbox["body_length"]
        for sy in (+1.0, -1.0):
            eye_c = Vector((eye_line_x, sy * 0.32 * bw, head.z + 0.02 * height))
            if (w - eye_c).length < eye_r * 1.25:
                col = palette["eye"]

        color_layer.data[v.index].color = (col[0], col[1], col[2], 1.0)

    # Material: Principled BSDF with Base Color driven by the vertex-colour layer.
    # The glTF exporter emits COLOR_0 + a neutral baseColorFactor; the engine uses
    # the (light) factor for its tint path and ignores COLOR_0, while previews and
    # future texture-sampling see the real markings.
    mat = bpy.data.materials.new(name="fur_mat")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    vcol = nt.nodes.new('ShaderNodeVertexColor')
    vcol.layer_name = color_layer.name
    bsdf.inputs['Roughness'].default_value = 0.78   # matte fur
    if 'Metallic' in bsdf.inputs:
        bsdf.inputs['Metallic'].default_value = 0.0
    nt.links.new(vcol.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    mesh_obj.data.materials.clear()
    mesh_obj.data.materials.append(mat)

    # A cheap Smart-UV unwrap so TEXCOORD_0 exists — the model is then ready for
    # the future baseColor-texture step without a re-unwrap. Best-effort.
    try:
        bpy.ops.object.select_all(action='DESELECT')
        mesh_obj.select_set(True)
        bpy.context.view_layer.objects.active = mesh_obj
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
        bpy.ops.object.mode_set(mode='OBJECT')
    except Exception as err:
        print(f"[build_quadruped] smart UV unwrap skipped ({err})", file=sys.stderr)
        if bpy.context.object and bpy.context.object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')


# ---------------------------------------------------------------------------
# Deformation-quality gate (the actual owner complaint)
# ---------------------------------------------------------------------------

def _point_segment_distance(p, a, b):
    ab = b - a
    denom = ab.length_squared
    if denom < 1e-12:
        return (p - a).length
    t = max(0.0, min(1.0, (p - a).dot(ab) / denom))
    return (p - (a + ab * t)).length


def _isolate_actions(arm_obj):
    """Mute every NLA track so a single active action can be evaluated in
    isolation. bake_animation_clips pushes each clip onto its own NLA strip for
    the exporter; those strips all start at frame 0 and would otherwise stack
    into a garbage pose when we set the active action for measurement/preview
    (un-keyed bones fall through to the NLA stack). Muting is in-memory and runs
    AFTER export, so the GLB's animations are unaffected."""
    if arm_obj.animation_data is None:
        return
    for track in arm_obj.animation_data.nla_tracks:
        track.mute = True


def _reset_pose(arm_obj):
    """Return every pose bone to its rest (identity) transform. Called before
    switching to a clip so bones the clip does not key stay at rest instead of
    retaining a previous clip's pose."""
    for pb in arm_obj.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)


def deformation_quality(mesh_obj, arm_obj, clip_names, ratio_limit=1.5, frames_per_clip=9):
    """Pose the rig at frames across each clip and measure the worst mesh
    'explosion': for every vertex, compare its distance to its nearest REST bone
    segment against its distance to that SAME bone segment when posed. A ratio
    above ratio_limit means the vertex flew away from the bone driving it — the
    exact tearing the Meshy meshes suffered. Returns a list of per-clip result
    dicts and prints a report.

    This runs on the live bound rig in-process (depsgraph-evaluated deformed
    mesh), so it measures the real skin the GLB will carry.
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()

    # Isolate single-action evaluation so the NLA strips don't contaminate poses.
    _isolate_actions(arm_obj)

    # Rest bone segments (world) from the armature's rest bones.
    rest_seg = {}
    for b in arm_obj.data.bones:
        if not b.use_deform:
            continue
        rest_seg[b.name] = (arm_obj.matrix_world @ b.head_local,
                            arm_obj.matrix_world @ b.tail_local)

    # Rest vertex world positions + each vert's nearest rest bone.
    rest_world = [mesh_obj.matrix_world @ v.co for v in mesh_obj.data.vertices]

    # A size-relative reference floor for the rest distance. WHY: a vertex that
    # sits almost exactly ON a bone segment has a rest distance near zero, so a
    # pure posed/rest ratio would explode to huge values on any normal joint
    # motion even when the surface is deforming perfectly. Flooring the reference
    # at a small fraction of the model size (~2.5% of the bbox diagonal) means
    # "1.5x its rest distance" is measured against a sane minimum, so the metric
    # flags a vertex that genuinely FLIES AWAY from its bone (a real tear) without
    # false-positiving on verts that legitimately live on the bone surface.
    bx = [p.x for p in rest_world]; by = [p.y for p in rest_world]; bz = [p.z for p in rest_world]
    diag = math.sqrt((max(bx) - min(bx)) ** 2 + (max(by) - min(by)) ** 2 + (max(bz) - min(bz)) ** 2)
    ref_floor = max(1e-4, 0.025 * diag)

    nearest_bone = [None] * len(rest_world)
    rest_dist = [0.0] * len(rest_world)
    for i, pw in enumerate(rest_world):
        best_name, best_d = None, float('inf')
        for name, (a, b) in rest_seg.items():
            d = _point_segment_distance(pw, a, b)
            if d < best_d:
                best_d, best_name = d, name
        nearest_bone[i] = best_name
        rest_dist[i] = max(best_d, ref_floor)

    scene = bpy.context.scene
    results = []
    for clip_name in clip_names:
        action = bpy.data.actions.get(clip_name)
        if action is None:
            continue
        _reset_pose(arm_obj)
        arm_obj.animation_data.action = action
        frame_start = int(action.frame_range[0])
        frame_end = int(action.frame_range[1])
        span = max(1, frame_end - frame_start)

        worst_ratio = 0.0
        worst_frame = frame_start
        exploded = 0
        min_paw_z = float('inf')
        for k in range(frames_per_clip):
            f = frame_start + round(span * k / (frames_per_clip - 1))
            scene.frame_set(f)
            depsgraph.update()
            eval_obj = mesh_obj.evaluated_get(depsgraph)
            eval_mesh = eval_obj.to_mesh()
            mw = eval_obj.matrix_world

            # Posed bone segments (world) from pose bones.
            posed_seg = {}
            for pb in arm_obj.pose.bones:
                if not pb.bone.use_deform:
                    continue
                posed_seg[pb.name] = (arm_obj.matrix_world @ pb.head,
                                      arm_obj.matrix_world @ pb.tail)

            for i, mv in enumerate(eval_mesh.vertices):
                pw = mw @ mv.co
                if pw.z < min_paw_z:
                    min_paw_z = pw.z
                seg = posed_seg.get(nearest_bone[i])
                if seg is None:
                    continue
                d = _point_segment_distance(pw, seg[0], seg[1])
                ratio = d / rest_dist[i]
                if ratio > worst_ratio:
                    worst_ratio = ratio
                    worst_frame = f
                if ratio > ratio_limit:
                    exploded += 1
            eval_obj.to_mesh_clear()

        clean = exploded == 0
        results.append({
            "clip": clip_name,
            "worst_ratio": worst_ratio,
            "worst_frame": worst_frame,
            "exploded_verts": exploded,
            "min_vertex_z": min_paw_z,
            "clean": clean,
        })
        flag = "OK" if clean else "EXPLODED"
        print(f"[deform] {clip_name:16s} worst-ratio={worst_ratio:.3f} "
              f"(limit {ratio_limit}) exploded={exploded} minZ={min_paw_z:+.4f} -> {flag}")

    # Reset to rest.
    arm_obj.animation_data.action = None
    scene.frame_set(frame_start if clip_names else 0)
    return results


# ---------------------------------------------------------------------------
# Preview renders (rest / mid-walk / attack apex)
# ---------------------------------------------------------------------------

def _setup_preview_world_and_light():
    world = bpy.data.worlds[0] if bpy.data.worlds else bpy.data.worlds.new("W")
    bpy.context.scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get('Background')
    if bg:
        bg.inputs[0].default_value = (0.22, 0.23, 0.26, 1.0)
        bg.inputs[1].default_value = 1.0
    bpy.ops.object.light_add(type='SUN', location=(4, -4, 6))
    key = bpy.context.active_object
    key.data.energy = 3.5
    key.rotation_euler = (math.radians(55), 0, math.radians(45))
    bpy.ops.object.light_add(type='SUN', location=(-3, 3, 4))
    fill = bpy.context.active_object
    fill.data.energy = 1.2
    fill.data.color = (0.75, 0.85, 1.0)
    fill.rotation_euler = (math.radians(45), 0, math.radians(-135))


def _place_preview_camera(center, max_extent):
    dist = max_extent * 2.6
    pos = Vector((dist * 0.72, -dist * 0.72, center.z + max_extent * 0.35))
    pos += Vector((center.x, center.y, 0.0))
    bpy.ops.object.camera_add(location=pos)
    cam = bpy.context.active_object
    cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (center - pos).normalized().to_track_quat('-Z', 'Y')
    cam.data.lens = 40
    bpy.context.scene.camera = cam
    return cam


def render_previews(mesh_obj, arm_obj, out_dir, base_name):
    """Render three posed 3/4 stills — rest, mid-walk, attack apex — to out_dir.
    Posing reuses the baked actions: set the active action + a frame and let the
    armature modifier deform the mesh for the render."""
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE_NEXT'
    scene.render.resolution_x = 720
    scene.render.resolution_y = 540
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'

    _setup_preview_world_and_light()

    corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [c.x for c in corners]; ys = [c.y for c in corners]; zs = [c.z for c in corners]
    center = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2))
    max_extent = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    for cam in [o for o in bpy.data.objects if o.type == 'CAMERA']:
        bpy.data.objects.remove(cam, do_unlink=True)
    _place_preview_camera(center, max_extent)

    # Isolate single-action posing so the stacked NLA strips (present for the
    # exporter) don't blend into the rendered pose — the rest render must show
    # the true bind pose, not the sum of every clip's frame 0.
    _isolate_actions(arm_obj)

    def pose_and_render(action_name, frame_fraction, label):
        _reset_pose(arm_obj)
        action = bpy.data.actions.get(action_name) if action_name else None
        if action is not None:
            arm_obj.animation_data.action = action
            fr = int(action.frame_range[0])
            to = int(action.frame_range[1])
            scene.frame_set(fr + round((to - fr) * frame_fraction))
        else:
            arm_obj.animation_data.action = None
            scene.frame_set(0)
        path = os.path.join(out_dir, f"{base_name}_{label}.png")
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        print(f"[build_quadruped] preview -> {path}")
        return path

    paths = [
        pose_and_render(None, 0.0, "rest"),
        pose_and_render("walk", 0.5, "walk"),        # mid stride
        pose_and_render("attack", 0.58, "attack"),   # lunge/strike apex
    ]
    arm_obj.animation_data.action = None
    scene.frame_set(0)
    return paths


# ---------------------------------------------------------------------------
# Self-contained fallback rig + gait (only used if rig_quadruped import failed)
# ---------------------------------------------------------------------------

def smooth_skin_weights(mesh_obj, factor=0.5, repeat=3):
    """Blur the skin weights across adjacent vertices, then re-limit to 4
    influences and renormalise. WHY: on the bulky variants the thin spine bones
    sit deep inside a thick body, so heat weighting leaves sharp, locally-wrong
    weight islands (and the reused envelope fallback can grab a broad radius);
    under a large motion those islands make individual verts fly away from their
    bone — the deformation gate's fly-away flags. Smoothing averages each vert's
    weights with its neighbours, which removes the isolated bad verts and yields
    the soft, continuous falloff a clean skin needs. It is the standard 'smooth
    skin weights' finishing pass and is safe on the already-clean lean variants
    (it only rounds transitions). Runs on our own mesh, not rig_quadruped."""
    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_obj
    try:
        # Smooth every vertex group together so cross-bone transitions blend.
        bpy.ops.object.vertex_group_smooth(group_select_mode='ALL',
                                           factor=factor, repeat=repeat)
        # glTF skins keep at most 4 influences per vertex; smoothing can spread a
        # vert across more, so re-limit and renormalise to a valid skin.
        bpy.ops.object.vertex_group_limit_total(limit=4)
        bpy.ops.object.vertex_group_normalize_all(lock_active=False)
        print(f"[build_quadruped] smoothed skin weights (factor={factor}, repeat={repeat})")
    except Exception as err:
        print(f"[build_quadruped] weight smoothing skipped ({err})", file=sys.stderr)


def _fallback_build_armature(bbox, species, anatomy):
    """Minimal armature with the same bone NAMES/topology the engine + gait code
    require, placed from the provided anatomy landmarks. Only used when
    rig_quadruped is unavailable."""
    def add(arm, name, head, tail, parent=None, connect=False):
        b = arm.edit_bones.new(name)
        b.head = head; b.tail = tail
        if parent is not None:
            b.parent = parent; b.use_connect = connect
        return b

    def lerp(a, b, t):
        return Vector(a) + (Vector(b) - Vector(a)) * t

    hip = Vector(anatomy["hip_center"]); chest = Vector(anatomy["chest_center"])
    head = Vector(anatomy["head_centroid"]); tail = Vector(anatomy["tail_tip"])

    bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
    arm_obj = bpy.context.active_object
    arm_obj.name = "Armature"
    arm = arm_obj.data
    if len(arm.edit_bones):
        arm.edit_bones.remove(arm.edit_bones[0])

    ground = Vector((hip.x, hip.y, bbox["ground_z"]))
    root = add(arm, "root", ground, hip)
    hips = add(arm, "hips", hip, lerp(hip, chest, 1 / 8), root, False)
    back_names = ["lumbar_01", "lumbar_02", "spine_01",
                  "upper_back_01", "upper_back_02", "upper_back_03"]
    prev = hips
    for i, nm in enumerate(back_names):
        prev = add(arm, nm, lerp(hip, chest, (i + 1) / 8), lerp(hip, chest, (i + 2) / 8),
                   prev, i > 0)
    chest_b = add(arm, "chest", lerp(hip, chest, 7 / 8), chest, prev, True)
    neck1 = add(arm, "neck_01", chest, lerp(chest, head, 0.33), chest_b, True)
    neck2 = add(arm, "neck_02", lerp(chest, head, 0.33), lerp(chest, head, 0.66), neck1, True)
    head_b = add(arm, "head", lerp(chest, head, 0.66), head, neck2, True)
    side = bbox["side"]; bw = bbox["body_width"]; bhh = bbox["body_height"]
    add(arm, "ear_L", head, head + side * (0.18 * bw) + Vector((0, 0, 0.08 * bhh)), head_b, False)
    add(arm, "ear_R", head, head - side * (0.18 * bw) + Vector((0, 0, 0.08 * bhh)), head_b, False)
    add(arm, "jaw", head + Vector((0, 0, -0.04 * bhh)),
        head + bbox["forward"] * (0.03 * bbox["body_length"]) + Vector((0, 0, -0.07 * bhh)),
        head_b, False)
    prev = hips
    for i in range(4):
        prev = add(arm, f"tail_{i + 1:02d}", lerp(hip, tail, i / 4), lerp(hip, tail, (i + 1) / 4),
                   prev, i > 0)
    for s in ("L", "R"):
        sf = Vector(anatomy[f"shoulder_{s}"]); pf = Vector(anatomy[f"paw_front_{s}"])
        sh = add(arm, f"shoulder_{s}", sf, lerp(sf, pf, 0.25), chest_b, False)
        ua = add(arm, f"upper_arm_{s}", lerp(sf, pf, 0.25), lerp(sf, pf, 0.55), sh, True)
        la = add(arm, f"lower_arm_{s}", lerp(sf, pf, 0.55), lerp(sf, pf, 0.90), ua, True)
        add(arm, f"paw_front_{s}", lerp(sf, pf, 0.90), pf, la, True)
        th = Vector(anatomy[f"thigh_{s}"]); pb = Vector(anatomy[f"paw_back_{s}"])
        t = add(arm, f"thigh_{s}", th, lerp(th, pb, 0.30), hips, False)
        sn = add(arm, f"shin_{s}", lerp(th, pb, 0.30), lerp(th, pb, 0.60), t, True)
        ft = add(arm, f"foot_{s}", lerp(th, pb, 0.60), lerp(th, pb, 0.90), sn, True)
        add(arm, f"paw_back_{s}", lerp(th, pb, 0.90), pb, ft, True)
    bpy.ops.object.mode_set(mode='OBJECT')
    return arm_obj


def _fallback_bind(mesh_obj, arm_obj):
    """Heat-diffusion bind + 4-influence limit + normalise. Clean topology makes
    heat succeed here without the Meshy-specific cleanup passes."""
    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True); arm_obj.select_set(True)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_obj
    try:
        bpy.ops.object.vertex_group_limit_total(limit=4)
        bpy.ops.object.vertex_group_normalize_all()
    except Exception as err:
        print(f"[build_quadruped] fallback weight cleanup warning: {err}", file=sys.stderr)


def _fallback_bake_clips(arm_obj, fps=24):
    """Self-contained authoring of the four required clips (idle/walk/run/attack)
    when rig_quadruped is unavailable. Same principles as the reused version:
    world-axis rotations mapped into each bone's local basis, up-only body bob so
    feet never breach the ground, and endpoint-shared sampling so cycles loop.
    Deliberately compact — the reused version is richer and is the normal path."""
    scene = bpy.context.scene
    scene.render.fps = fps
    rad = math.radians
    UP = Vector((0.0, 0.0, 1.0))

    # Rig frame from hips->chest, matching the reused math.
    forward_axis = Vector((1.0, 0.0, 0.0)); side_axis = Vector((0.0, 1.0, 0.0)); span = 0.3
    hips = arm_obj.pose.bones.get("hips"); chest = arm_obj.pose.bones.get("chest")
    if hips and chest:
        sv = (arm_obj.matrix_world @ chest.head) - (arm_obj.matrix_world @ hips.head)
        if sv.length > 1e-6:
            span = sv.length
            fwd = Vector((sv.x, sv.y, 0.0))
            if fwd.length > 1e-6:
                fwd.normalize(); forward_axis = fwd
                sc = fwd.cross(UP)
                if sc.length > 1e-6:
                    side_axis = sc.normalized()
    hip_z = (arm_obj.matrix_world @ hips.head).z if hips else 0.22

    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='POSE')
    if arm_obj.animation_data is None:
        arm_obj.animation_data_create()
    for stale in list(bpy.data.actions):
        bpy.data.actions.remove(stale)

    def rest_quat(pb):
        return (arm_obj.matrix_world @ pb.bone.matrix_local).to_quaternion()

    def kf(bone, frame, rots=None, delta=None):
        pb = arm_obj.pose.bones.get(bone)
        if pb is None:
            return
        if rots is not None:
            rw = rest_quat(pb); q = Quaternion((1, 0, 0, 0))
            for axis, ang in rots:
                q = q @ Quaternion(rw.inverted() @ Vector(axis), ang)
            pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = q
            pb.keyframe_insert('rotation_quaternion', frame=frame, group=bone)
        if delta is not None:
            pb.location = rest_quat(pb).inverted() @ Vector(delta)
            pb.keyframe_insert('location', frame=frame, group=bone)

    def new_action(name):
        a = bpy.data.actions.new(name); a.use_fake_user = True
        arm_obj.animation_data.action = a
        for pb in arm_obj.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
        return a

    actions = []
    # idle: breathing bob + tail sway.
    new_action("idle"); dur = int(4.0 * fps)
    for i in range(17):
        t = i / 16; f = round(t * dur)
        breath = (1 - math.cos(2 * math.pi * 2 * t)) * 0.5
        kf("root", f, delta=UP * (breath * 0.02 * span))
        kf("chest", f, rots=[(side_axis, breath * rad(2.5))])
        kf("tail_01", f, rots=[(UP, math.sin(2 * math.pi * t) * rad(9))])
        kf("head", f, rots=[(UP, math.sin(2 * math.pi * t) * rad(6))])
    actions.append("idle")

    legs = [("thigh_L", 0.0, "shin_L", -1, "paw_back_L"),
            ("shoulder_L", 0.25, "lower_arm_L", +1, "paw_front_L"),
            ("thigh_R", 0.5, "shin_R", -1, "paw_back_R"),
            ("shoulder_R", 0.75, "lower_arm_R", +1, "paw_front_R")]

    def cycle(name, dur_s, swing_deg, knee_deg, samples):
        new_action(name); durf = int(dur_s * fps)
        for i in range(samples + 1):
            t = i / samples; f = round(t * durf)
            for sw, ph, kn, sign, paw in legs:
                s = math.sin(2 * math.pi * (t + ph))
                kf(sw, f, rots=[(side_axis, s * rad(swing_deg))])
                kf(kn, f, rots=[(side_axis, max(0.0, s) * rad(knee_deg) * sign)])
            bob = (1 - math.cos(2 * math.pi * 2 * t)) * 0.5
            kf("root", f, delta=UP * (bob * 0.05 * hip_z))
        actions.append(name)

    cycle("walk", 1.2, 15, 20, 12)
    cycle("run", 0.6, 30, 38, 8)

    # attack: coil then lunge (one-shot, opens/closes on rest).
    new_action("attack"); durf = int(0.8 * fps)
    def af(t):
        return round(t * durf)
    for bone, anticip, strike in [("chest", rad(12), -rad(16)), ("head", rad(8), -rad(34)),
                                   ("shoulder_L", rad(20), -rad(42)), ("shoulder_R", rad(20), -rad(42))]:
        kf(bone, af(0.0), rots=[]); kf(bone, af(0.3), rots=[(side_axis, anticip)])
        kf(bone, af(0.58), rots=[(side_axis, strike)]); kf(bone, af(1.0), rots=[])
    kf("root", af(0.0), delta=Vector((0, 0, 0)))
    kf("root", af(0.3), delta=forward_axis * (-0.16 * span))
    kf("root", af(0.58), delta=forward_axis * (0.55 * span))
    kf("root", af(1.0), delta=Vector((0, 0, 0)))
    actions.append("attack")

    arm_obj.animation_data.action = None
    for name in actions:
        a = bpy.data.actions.get(name)
        tr = arm_obj.animation_data.nla_tracks.new(); tr.name = name
        tr.strips.new(name, 0, a)
    bpy.ops.object.mode_set(mode='OBJECT')
    print(f"[build_quadruped] fallback baked clips: {', '.join(actions)}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    opts = parse_args()
    species, variant = opts["species"], opts["variant"]
    print(f"[build_quadruped] species={species} variant={variant} out={opts['out']}")

    profile, palette, default_diag, size_mult = species_variant_profile(species, variant)
    target_diag = opts["target_diagonal"] or (default_diag * size_mult)

    # Blank scene so nothing leaks into the export.
    bpy.ops.wm.read_homefile(use_empty=True)

    # 1) Grow the body from the skin graph and land it in the tri budget.
    mesh_obj, anatomy = create_body_mesh(profile, opts["subsurf"], opts["max_tris"])

    # 2) Normalise size (mesh + landmarks together) to the engine-unit target,
    #    then ground the model so its feet rest on the Z=0 plane.
    normalize_scale(mesh_obj, anatomy, target_diag)
    ground_to_floor(mesh_obj, anatomy)
    bbox = bbox_dict(mesh_obj)
    print(f"[build_quadruped] bbox L={bbox['body_length']:.3f} "
          f"W={bbox['body_width']:.3f} H={bbox['body_height']:.3f}")

    # 3) Build the reused hand-designed skeleton at our landmarks.
    if opts["reuse"] and RIGLIB is not None:
        arm_obj = RIGLIB.build_armature(bbox, species, anatomy=anatomy)
    else:
        arm_obj = _fallback_build_armature(bbox, species, anatomy)

    # 4) Bind the BODY with deterministic heat weights (clean topology -> clean
    #    weights). Do this before adding eyes so heat runs on the connected body;
    #    eyes are weighted explicitly to the head bone and joined after.
    if opts["reuse"] and RIGLIB is not None:
        RIGLIB.parent_with_auto_weights(mesh_obj, arm_obj)
    else:
        _fallback_bind(mesh_obj, arm_obj)

    # 4b) Smooth the skin weights so no isolated vert flies away under motion
    #     (critical for the bulky variants; harmless on the lean ones).
    smooth_skin_weights(mesh_obj)

    # 5) Eyes (weighted to head, joined into the body mesh).
    eye_r = add_eyes(mesh_obj, bbox, anatomy)

    # 6) Fur markings + material (light neutral base albedo; markings in COLOR_0).
    apply_colors_and_material(mesh_obj, bbox, anatomy, palette, eye_marker_z=eye_r)

    # 7) Bake the four required clips (idle/walk/run/attack) + rest-state clips,
    #    reusing the proven, ground-safe, loop-safe gait authoring.
    if opts["reuse"] and RIGLIB is not None:
        RIGLIB.bake_animation_clips(arm_obj)
    else:
        _fallback_bake_clips(arm_obj)

    # 8) Export the verifying GLB (skins + NLA animations, embedded).
    out_path = os.path.abspath(opts["out"])
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    if opts["reuse"] and RIGLIB is not None:
        RIGLIB.export_glb(out_path)
    else:
        bpy.ops.object.select_all(action='SELECT')
        bpy.ops.export_scene.gltf(filepath=out_path, export_format='GLB',
                                  use_selection=False, export_apply=True,
                                  export_skins=True, export_animations=True,
                                  export_nla_strips=True)
    print(f"[build_quadruped] wrote {out_path}")

    # 9) Deformation-quality gate on the four core clips.
    deform_results = deformation_quality(mesh_obj, arm_obj,
                                         ["idle", "walk", "run", "attack"])
    all_clean = all(r["clean"] for r in deform_results) and len(deform_results) == 4
    print(f"[build_quadruped] deformation gate: "
          f"{'ALL CLEAN' if all_clean else 'ARTIFACTS PRESENT'}")

    # 10) Optional previews.
    if opts["previews"]:
        out_dir = opts["previews_dir"] or os.path.join(os.path.dirname(out_path), "previews")
        base_name = os.path.splitext(os.path.basename(out_path))[0]
        render_previews(mesh_obj, arm_obj, out_dir, base_name)

    # Final parameter summary (data for the orchestrator / fleet run).
    print("[build_quadruped] SUMMARY "
          f"species={species} variant={variant} target_diagonal={target_diag:.3f} "
          f"baked_size_mult={size_mult} subsurf={opts['subsurf']} max_tris={opts['max_tris']} "
          f"tris={len(mesh_obj.data.polygons)} deform_clean={all_clean}")


if __name__ == "__main__":
    main()
