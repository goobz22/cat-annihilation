"""
rig_quadruped.py — Blender headless re-rigging for cat/dog quadruped GLB meshes.

Meshy's auto-rigger is weak for quadrupeds: bones are placed from a generic
template and weights come from envelope heuristics, which produces stiff and
unnatural deformation at elbows and hips. This script replaces the imported
rig with a hand-designed cat/dog skeleton sized to the mesh's bounding box,
then re-parents with Blender's "automatic weights" (heat diffusion) — which
is the same weighting method production rigs use.

Usage (headless):
    blender --background --python rig_quadruped.py -- <input.glb> <output.glb>

Optional flags (after the -- separator):
    --flip-forward         Reverse the forward direction. Use if the rigged
                           output faces backward (Meshy occasionally exports
                           with -X forward instead of +X).
    --species cat|dog      Adjust proportions (default: cat). dog uses longer
                           leg ratios and a slightly longer tail.

Skeleton layout (parent → child):
    root
    └ hips
      ├ spine → chest → neck → head
      ├ tail_01 → tail_02 → tail_03 → tail_04
      ├ thigh_L/R → shin_L/R → foot_L/R
    chest
      └ shoulder_L/R → upper_arm_L/R → lower_arm_L/R → paw_L/R

Coordinate frame after GLB import:
    Blender uses Z up; Meshy GLBs come in with the body horizontal and
    the longest horizontal extent = body length. The script picks that axis
    as "forward" automatically; --flip-forward reverses the head/tail end.
"""

import bpy
import sys
import math
from mathutils import Vector


# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------

def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []

    if len(argv) < 2:
        print("Usage: blender --background --python rig_quadruped.py -- "
              "<input.glb> <output.glb> [--flip-forward] [--species cat|dog]",
              file=sys.stderr)
        sys.exit(1)

    opts = {
        "input": argv[0],
        "output": argv[1],
        "flip_forward": "--flip-forward" in argv,
        "species": "cat",
    }

    if "--species" in argv:
        i = argv.index("--species")
        if i + 1 < len(argv):
            opts["species"] = argv[i + 1]

    return opts


# ---------------------------------------------------------------------------
# Scene + import
# ---------------------------------------------------------------------------

def reset_scene():
    """Blank the current scene so leftover default cubes / cameras don't
    interfere with bounding-box math or the exported .glb."""
    bpy.ops.wm.read_homefile(use_empty=True)


def import_glb(path):
    """Import a GLB into the current scene. Blender's importer normalises
    glTF's +Y-up to Blender's +Z-up, so downstream bounding-box logic treats
    Z as vertical without extra transforms.

    Raises sys.exit(3) on malformed input files — Blender's importer throws
    IndexError on gltf files with out-of-range vertex indices, which would
    otherwise produce a wall of stack trace. Explicit exit code lets the
    batch runner count the failure and move on to the next file instead of
    halting the whole job.
    """
    try:
        bpy.ops.import_scene.gltf(filepath=path)
    except Exception as err:
        print(f"ERROR: gltf import failed for {path}: {err}", file=sys.stderr)
        sys.exit(3)


def collect_and_join_meshes():
    """Meshy sometimes splits a model across several mesh objects (e.g., body
    + eyes + collar as separate meshes under one empty). Joining everything
    into a single mesh before rigging means one armature modifier, one weight
    set, and one export — simpler and matches how the engine's ModelLoader
    treats the model."""
    mesh_objs = [o for o in bpy.data.objects if o.type == 'MESH']
    if not mesh_objs:
        print("ERROR: no mesh found in input GLB", file=sys.stderr)
        sys.exit(2)

    bpy.ops.object.select_all(action='DESELECT')
    for obj in mesh_objs:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_objs[0]

    if len(mesh_objs) > 1:
        bpy.ops.object.join()

    return bpy.context.active_object


def strip_existing_rig(mesh_obj):
    """Remove Meshy's auto-rig: delete every armature object in the scene and
    drop any Armature modifiers on the mesh. Leaves vertex groups intact —
    they'll be overwritten by parent-with-automatic-weights below."""
    for mod in list(mesh_obj.modifiers):
        if mod.type == 'ARMATURE':
            mesh_obj.modifiers.remove(mod)

    for obj in list(bpy.data.objects):
        if obj.type == 'ARMATURE':
            bpy.data.objects.remove(obj, do_unlink=True)


def cleanup_mesh(mesh_obj):
    """Blender mesh-cleanup pass to improve heat-diffusion solve rate.

    Meshy-generated meshes typically fail heat diffusion because they
    contain:
      - Duplicate vertices at the same world position (seams from the
        marching-cubes surface reconstruction)
      - Inconsistent face normals (half facing inward, half outward)
      - Loose geometry: stray verts/edges from broken triangulation
      - Non-manifold edges shared by >2 faces (e.g., where the collar
        fuses with the body)

    Heat diffusion requires a mostly-manifold mesh to solve, because the
    algorithm integrates heat flow across edges. Duplicate verts make
    unconnected neighbors; inconsistent normals confuse the flow
    direction. None of these edits change the silhouette of the cat -
    they only reconnect the topology so the solver has a connected
    graph to walk.

    Running this pass on every cat in the v2 rig batch took heat
    coverage from 0% to something like 40-60% per cat, which is the
    difference between "envelope fallback" (coarse, topology-ignorant)
    and "heat mostly worked + envelope for the gaps" (far better for
    animation).
    """
    if mesh_obj.type != 'MESH':
        return
    original_vert_count = len(mesh_obj.data.vertices)

    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_obj

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    # Merge duplicates - 0.0001m is small enough not to fuse features we
    # actually want preserved (e.g., collar jewels), large enough to catch
    # seam-duplicates from Meshy's marching-cubes output.
    bpy.ops.mesh.remove_doubles(threshold=0.0001)
    # Recalculate outward so heat's flow direction is consistent.
    bpy.ops.mesh.normals_make_consistent(inside=False)
    # Strip loose geometry (disconnected verts, edges not part of any face).
    bpy.ops.mesh.delete_loose()
    bpy.ops.object.mode_set(mode='OBJECT')

    cleaned_vert_count = len(mesh_obj.data.vertices)
    removed = original_vert_count - cleaned_vert_count
    print(f"[rig_quadruped] mesh cleanup: {original_vert_count} -> "
          f"{cleaned_vert_count} verts ({removed} merged/removed)")


# ---------------------------------------------------------------------------
# Body alignment (rotate mesh so long axis = world X, up = world Z)
# ---------------------------------------------------------------------------


def _dominant_axis(points, iters=40):
    """Principal axis of a vertex cluster via power iteration on the
    covariance matrix. Returns the unit vector along the direction of
    largest variance. No numpy dependency - plain Python over mathutils.
    Vector.
    """
    if len(points) < 3:
        return Vector((1.0, 0.0, 0.0))
    # Mean
    mean = Vector((0.0, 0.0, 0.0))
    for p in points: mean = mean + p
    mean = mean / len(points)
    # Covariance (3x3 symmetric)
    cov00 = cov01 = cov02 = cov11 = cov12 = cov22 = 0.0
    for p in points:
        dx = p.x - mean.x; dy = p.y - mean.y; dz = p.z - mean.z
        cov00 += dx * dx; cov01 += dx * dy; cov02 += dx * dz
        cov11 += dy * dy; cov12 += dy * dz
        cov22 += dz * dz
    # Power iteration
    b = Vector((1.0, 0.3, 0.1))  # non-aligned start so we don't accidentally converge to axis for an axis-aligned cluster
    b.normalize()
    for _ in range(iters):
        nx = cov00 * b.x + cov01 * b.y + cov02 * b.z
        ny = cov01 * b.x + cov11 * b.y + cov12 * b.z
        nz = cov02 * b.x + cov12 * b.y + cov22 * b.z
        b = Vector((nx, ny, nz))
        if b.length < 1e-9:
            return Vector((1.0, 0.0, 0.0))
        b.normalize()
    return b


def align_mesh_to_world(mesh_obj):
    """Rotate the mesh so the body's long axis aligns with world +X and
    the "up" direction aligns with world +Z. Meshy sometimes exports a
    cat rotated so the body's long axis is oblique in world space, which
    then cascades into analyze_bbox picking the wrong axis for forward
    and the anatomy detector flagging legs at wrong positions.

    Mechanism:
      1. PCA on TORSO verts (middle 40-80% of height, filtering paws &
         ear tips) to find the principal body axis.
      2. Build a rotation that sends that axis to world +X, keeping up
         close to world +Z.
      3. Apply the rotation by baking it into the mesh's world matrix
         and then applying the transform to the mesh data. Subsequent
         bbox / anatomy / rig passes see an axis-aligned mesh.

    Why this doesn't unwrap internal spine twist: internal twist is
    a curvature of the body mesh along its own axis, which PCA doesn't
    model. Full unwrap requires per-slice realignment (per-vertex
    deformation). This pass only fixes GROSS orientation - the simple
    "the cat is rotated 30 degrees in world" case. For a cat with a
    genuinely curved spine (Meshy baked a dynamic pose), the spine
    stays curved but the body ends up oriented correctly in world so
    the bbox / forward / up detection and per-cat rig come out sensible.
    """
    verts = mesh_obj.data.vertices
    if not verts:
        return
    # Find vertical (up) axis: whichever world axis has the largest extent
    # differential ABOVE vs BELOW the centroid. For a right-side-up cat,
    # up is Z with ears and back above hips.
    zs = [v.co.z for v in verts]
    ys = [v.co.y for v in verts]
    xs = [v.co.x for v in verts]
    z_ext = max(zs) - min(zs)
    y_ext = max(ys) - min(ys)
    x_ext = max(xs) - min(xs)
    # Use the axis with smallest extent as "candidate up" only if it's
    # much smaller than the others; otherwise default to Z.
    up_guess_axis = 2  # world Z
    z_min = min(zs); z_max = max(zs)
    mid_z = (z_min + z_max) * 0.5
    lo = z_min + (z_max - z_min) * 0.2
    hi = z_min + (z_max - z_min) * 0.8
    torso_points = [v.co for v in verts if lo <= v.co.z <= hi]
    if len(torso_points) < 20:
        # Mesh too small or badly distributed; skip alignment.
        return
    axis = _dominant_axis(torso_points)
    # Collapse vertical component - we only want to realign in XY plane
    # (rotation around Z axis). Rotating in 3D could flip a correctly-
    # upright cat onto its side. Gross "cat is rotated yaw in world"
    # is the realistic Meshy export issue; "cat is sideways or upside
    # down" doesn't happen because Meshy always outputs with feet down.
    axis_xy = Vector((axis.x, axis.y, 0.0))
    if axis_xy.length < 1e-6:
        return
    axis_xy.normalize()
    target = Vector((1.0, 0.0, 0.0))
    # Rotation angle in XY plane
    cos_a = axis_xy.dot(target)
    # signed angle via cross-product z-component
    sin_a = axis_xy.x * target.y - axis_xy.y * target.x
    # That gives the signed angle FROM axis_xy TO target (we rotate body so
    # axis_xy maps onto +X).
    import math
    angle = math.atan2(sin_a, cos_a)
    if abs(angle) < math.radians(3.0):
        # Already within 3 degrees of aligned; don't bother rotating.
        return
    print(f"[rig_quadruped] body alignment: rotating mesh by {math.degrees(angle):.1f}deg about Z "
          f"to align body axis (detected direction {axis_xy.to_tuple(3)}) with world +X")
    # Apply rotation via object transform + transform_apply. Doing this
    # via the object's matrix_world and then baking the transform into
    # the mesh data makes mesh.bound_box, vertex normals, and world
    # matrix all stay consistent - editing v.co directly leaves
    # bound_box cached at pre-rotation values and analyze_bbox reads
    # stale corners.
    from mathutils import Matrix as _Matrix
    rot = _Matrix.Rotation(angle, 4, 'Z')
    # Compose rotation into the object's world matrix (pre-multiply so
    # rotation happens in world space regardless of current object
    # transform).
    mesh_obj.matrix_world = rot @ mesh_obj.matrix_world
    # Select just this mesh and bake transform so vertex data stores
    # the rotated positions and the object returns to identity transform.
    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_obj
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)


# ---------------------------------------------------------------------------
# Bounding-box analysis
# ---------------------------------------------------------------------------

def detect_head_end(mesh_obj, forward_axis):
    """Figure out which end of the body is the HEAD by comparing vertical
    extent near each end. A cat's head has ears + cranium that stick up
    well above the body's torso surface; the tail end only has a thin
    tail hanging roughly level with (or below) the back. So the end with
    the greater +Z peak = head end.

    Returns +1 if head is at the +forward end (standard), -1 if head is
    at the -forward end (Meshy exported the cat facing -forward, and
    the rig needs --flip-forward equivalent).

    Why this matters: rig_quadruped.py places head / neck / ear / jaw
    bones at +forward end and tail bones at -forward end. If the mesh
    is reversed, the head bones end up where the tail IS and vice versa.
    User-facing symptom: selecting "tail_01" in the bone sandbox moves
    the head mesh, because "tail_01" is placed at the head-end of the
    body, which is actually where the head mesh lives.
    """
    ax = {'X': 0, 'Y': 1}[forward_axis]
    mins_mx = [float('inf'), float('inf'), float('inf')]
    maxs_mx = [float('-inf'), float('-inf'), float('-inf')]
    for v in mesh_obj.data.vertices:
        w = mesh_obj.matrix_world @ v.co
        for i in range(3):
            if w[i] < mins_mx[i]: mins_mx[i] = w[i]
            if w[i] > maxs_mx[i]: maxs_mx[i] = w[i]
    mid = (mins_mx[ax] + maxs_mx[ax]) * 0.5
    # Split verts into "near +forward end" vs "near -forward end" and
    # find each half's max Z (up). The end whose max-Z reaches higher
    # above the body axis is the head end (ears).
    z_max_plus = float('-inf')
    z_max_minus = float('-inf')
    for v in mesh_obj.data.vertices:
        w = mesh_obj.matrix_world @ v.co
        if w[ax] > mid:
            if w[2] > z_max_plus: z_max_plus = w[2]
        else:
            if w[2] > z_max_minus: z_max_minus = w[2]
    # Positive result = head at +forward end (good, no flip needed).
    # Negative = head at -forward end, we need to flip.
    if z_max_plus >= z_max_minus:
        detected_sign = +1
    else:
        detected_sign = -1
    print(f"[rig_quadruped] head-end detection: max-Z at +{forward_axis}={z_max_plus:.3f}, "
          f"at -{forward_axis}={z_max_minus:.3f} -> head at sign={detected_sign:+d}")
    return detected_sign


def analyze_bbox(mesh_obj, flip_forward):
    """Compute the mesh's world-space AABB and pick forward / up / side
    orientation. Up is always Z (post-import convention). Forward is the
    longest horizontal axis; side is the remaining one.

    The head/tail orientation is AUTO-DETECTED from vertex geometry
    (detect_head_end) so we don't need to guess whether Meshy exported
    the cat facing +axis or -axis. The flip_forward CLI flag still
    exists as a manual override when auto-detection mispredicts."""
    corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [v.x for v in corners]
    ys = [v.y for v in corners]
    zs = [v.z for v in corners]
    x_extent = max(xs) - min(xs)
    y_extent = max(ys) - min(ys)
    z_extent = max(zs) - min(zs)

    center_x = (min(xs) + max(xs)) * 0.5
    center_y = (min(ys) + max(ys)) * 0.5
    ground_z = min(zs)

    if x_extent >= y_extent:
        length_axis = 'X'
        body_length = x_extent
        body_width = y_extent
        side = Vector((0.0, 1.0, 0.0))
    else:
        length_axis = 'Y'
        body_length = y_extent
        body_width = x_extent
        side = Vector((1.0, 0.0, 0.0))

    # Auto-detect which end of the length axis is the head (ears/cranium
    # extend vertically above the body-top). XOR with the manual flag so
    # --flip-forward still works as an override when auto-detect is wrong.
    detected_sign = detect_head_end(mesh_obj, length_axis)
    final_sign = -detected_sign if flip_forward else detected_sign
    if length_axis == 'X':
        forward = Vector((float(final_sign), 0.0, 0.0))
    else:
        forward = Vector((0.0, float(final_sign), 0.0))

    return {
        "center_x": center_x,
        "center_y": center_y,
        "ground_z": ground_z,
        "body_length": body_length,
        "body_width": body_width,
        "body_height": z_extent,
        "forward": forward,
        "side": side,
        "length_axis": length_axis,
    }


# ---------------------------------------------------------------------------
# Mesh-landmark anatomy detection
# ---------------------------------------------------------------------------
#
# The fixed SPECIES_RATIOS + body_point() approach places every cat's bones
# at the same fractional position along its bbox, regardless of where the
# actual limbs / head / tail sit on the mesh. Cats with different body
# proportions (longer legs, tucked head, upright tail) get bones that
# float in the wrong place, and every downstream fix (envelope fallback,
# side-filtering, heat crossover cleanup) has to work uphill against the
# mismatch.
#
# detect_anatomy() walks the vertex list and extracts concrete 3D
# landmarks:
#    - paw_front_L / paw_front_R / paw_back_L / paw_back_R (lowest verts
#      in each forward x side quadrant - where the paws touch ground)
#    - shoulder_L / shoulder_R / thigh_L / thigh_R (torso attach points
#      above the paws, at body-mass-center height)
#    - hip_center / chest_center (mid-pelvis / mid-shoulder, averaged
#      from their respective L/R pair)
#    - head_centroid (centroid of the most-forward 5% of verts - follows
#      whatever angle the head mesh has, tilted up OR down OR level)
#    - tail_tip (centroid of the most-backward 1% of verts - follows the
#      tail's actual angle whether it's curled up, drooped, or straight)
#    - body_axis_forward (normalized hip->chest direction; used for spine
#      interpolation instead of a fixed axis)
#
# build_armature() then places every bone by interpolating between those
# landmarks, so a cat with its tail straight up gets tail bones pointing
# straight up, and a cat with legs of unequal length gets a shorter rig
# on the shorter side. The template is the bone TOPOLOGY (parent chains
# and names); the positions adapt per-cat.
# ---------------------------------------------------------------------------


def detect_anatomy(mesh_obj, bbox):
    """Extract anatomical landmarks from vertex positions.

    All returned points are in WORLD space. Call this AFTER analyze_bbox
    so we know which axis is forward / side and which sign is head.
    """
    forward = bbox["forward"]
    side    = bbox["side"]
    center_x = bbox["center_x"]
    center_y = bbox["center_y"]
    ground_z = bbox["ground_z"]
    body_length = bbox["body_length"]
    body_width  = bbox["body_width"]
    body_height = bbox["body_height"]

    # Project each vertex onto forward and side axes. Origin for those
    # projections is the bbox horizontal center at ground level; z is
    # kept as absolute world-Z (Blender up) so height thresholds stay
    # intuitive.
    origin = Vector((center_x, center_y, 0.0))
    verts_world = [mesh_obj.matrix_world @ v.co for v in mesh_obj.data.vertices]
    projected = []
    for vw in verts_world:
        rel = vw - origin
        projected.append((rel.dot(forward), rel.dot(side), vw.z, vw))

    # Partition into four quadrants of the body plane (head/tail x L/R).
    # Each entry is (f, s, z, world_vec).
    quads = {'front_L': [], 'front_R': [], 'back_L': [], 'back_R': []}
    for p in projected:
        f, s, _, _ = p
        key = f"{'front' if f > 0 else 'back'}_{'L' if s > 0 else 'R'}"
        quads[key].append(p)

    def centroid_of(pts):
        n = len(pts)
        if n == 0: return None
        sx = sy = sz = 0.0
        for _, _, _, v in pts:
            sx += v.x; sy += v.y; sz += v.z
        return Vector((sx / n, sy / n, sz / n))

    # Paw detection via GLOBAL lowest-Z filter, then partitioning. An
    # earlier per-quadrant-lowest approach failed on cats whose mesh in
    # one quadrant stays high off the ground (e.g. storm_leader's back
    # half; the "lowest Z in back_R quadrant" was a torso vertex 66 cm
    # above the real paws, and the rig placed paw_back_R at chest level).
    # Taking the bottom 3% of verts GLOBALLY first means we only consider
    # genuinely near-ground verts; partitioning those by quadrant after
    # the fact gives each paw its own cluster WITHOUT inventing a false
    # "paw" in a quadrant that has no ground contact. If a quadrant is
    # empty at that stage, the leg is lifted / missing and we fall back
    # to body_pt() for that paw only.
    all_sorted_z = sorted(projected, key=lambda p: p[2])
    low_count = max(60, int(len(projected) * 0.03))
    ground_verts = all_sorted_z[:low_count]
    ground_quads = {'front_L': [], 'front_R': [], 'back_L': [], 'back_R': []}
    for p in ground_verts:
        f, s, _, _ = p
        key = f"{'front' if f > 0 else 'back'}_{'L' if s > 0 else 'R'}"
        ground_quads[key].append(p)

    def paw_centroid(ground_pts, min_count=5):
        """Average the ground-level verts in a quadrant. Returns None if
        the quadrant has too few to be a real paw (leg is lifted / pose
        is asymmetric); caller supplies a fallback."""
        if len(ground_pts) < min_count:
            return None
        return centroid_of(ground_pts)

    paw_front_L = paw_centroid(ground_quads['front_L'])
    paw_front_R = paw_centroid(ground_quads['front_R'])
    paw_back_L  = paw_centroid(ground_quads['back_L'])
    paw_back_R  = paw_centroid(ground_quads['back_R'])

    # Leg attach = torso-height verts directly above the paw (same quadrant,
    # mid-body Z, same forward+side band as the paw). This is where the
    # leg's top bone (shoulder / thigh) anchors into the chest / hips.
    hip_height_z = ground_z + body_height * 0.55
    leg_attach_tol_f = body_length * 0.12  # tolerance for forward-match to paw
    leg_attach_tol_s = body_width  * 0.18  # tolerance for side-match to paw

    def leg_attach(paw, quadrant_pts):
        if not paw or not quadrant_pts: return None
        paw_rel = paw - origin
        paw_f = paw_rel.dot(forward)
        paw_s = paw_rel.dot(side)
        candidates = []
        for p in quadrant_pts:
            f, s, z, _ = p
            if abs(f - paw_f) < leg_attach_tol_f and abs(s - paw_s) < leg_attach_tol_s \
               and abs(z - hip_height_z) < body_height * 0.20:
                candidates.append(p)
        if len(candidates) < 5:
            # Relax: any vert in the quadrant above mid-body height within
            # the forward-band. Catches cats whose torso is wider than the
            # paw footprint (common with prop-bearing mesh).
            for p in quadrant_pts:
                f, _, z, _ = p
                if abs(f - paw_f) < leg_attach_tol_f * 2 and z > hip_height_z - body_height * 0.25:
                    candidates.append(p)
        return centroid_of(candidates) if candidates else None

    shoulder_L = leg_attach(paw_front_L, quads['front_L'])
    shoulder_R = leg_attach(paw_front_R, quads['front_R'])
    thigh_L    = leg_attach(paw_back_L,  quads['back_L'])
    thigh_R    = leg_attach(paw_back_R,  quads['back_R'])

    # Head centroid = most-forward 5% of verts. Weight toward the tip so
    # a cat with a tilted head ends up with head_centroid at the actual
    # nose/muzzle location, not the neck midpoint.
    projected.sort(key=lambda p: -p[0])
    head_cluster_size = max(50, int(len(projected) * 0.05))
    head_centroid = centroid_of(projected[:head_cluster_size])

    # Tail tip = most-backward 1% of verts. Gives the real tail direction
    # even when it's curled upward, drooped, or pointing straight back.
    # Use bottom-of-quadrant check to exclude props (satchels) that might
    # hang behind the hips - tail mesh is narrower than a satchel so
    # requiring forward_coord < -body_length*0.35 AND low horizontal
    # spread (thin) picks the actual tail.
    back_verts = [p for p in projected if p[0] < -body_length * 0.25]
    if back_verts:
        back_verts.sort(key=lambda p: p[0])
        tail_size = max(30, int(len(back_verts) * 0.05))
        tail_tip = centroid_of(back_verts[:tail_size])
    else:
        tail_tip = None

    # Pelvis / chest midpoints: average the L/R leg attaches. Use explicit
    # if/else because `a and b` on Blender Vectors has nonobvious truthiness
    # (non-zero Vectors short-circuit weirdly with the inline conditional)
    # and the earlier one-liner was silently returning thigh_R instead of
    # the midpoint for storm_leader.
    if thigh_L is not None and thigh_R is not None:
        hip_center = Vector((
            (thigh_L.x + thigh_R.x) * 0.5,
            (thigh_L.y + thigh_R.y) * 0.5,
            (thigh_L.z + thigh_R.z) * 0.5,
        ))
    else:
        hip_center = None
    if shoulder_L is not None and shoulder_R is not None:
        chest_center = Vector((
            (shoulder_L.x + shoulder_R.x) * 0.5,
            (shoulder_L.y + shoulder_R.y) * 0.5,
            (shoulder_L.z + shoulder_R.z) * 0.5,
        ))
    else:
        chest_center = None

    # Fallbacks for anything we couldn't detect (small / weird mesh).
    def fallback(length_r, height_r, side_off):
        return (origin
                + forward * ((length_r - 0.5) * body_length)
                + side    * side_off
                + Vector((0.0, 0.0, ground_z + height_r * body_height)))
    # fallback() returns including z; strip the doubled ground_z added above
    # by just re-computing directly here - keeps the closure legible:
    def body_pt(length_r, height_r, side_off):
        base = Vector((center_x, center_y, ground_z))
        pt = base + forward * ((length_r - 0.5) * body_length) + side * side_off
        pt.z = ground_z + height_r * body_height
        return pt

    spread_default = 0.35 * body_width
    if hip_center    is None: hip_center    = body_pt(0.22, 0.55, 0.0)
    if chest_center  is None: chest_center  = body_pt(0.72, 0.55, 0.0)
    if thigh_L       is None: thigh_L       = body_pt(0.22, 0.55, +spread_default)
    if thigh_R       is None: thigh_R       = body_pt(0.22, 0.55, -spread_default)
    if shoulder_L    is None: shoulder_L    = body_pt(0.72, 0.55, +spread_default)
    if shoulder_R    is None: shoulder_R    = body_pt(0.72, 0.55, -spread_default)
    if paw_front_L   is None: paw_front_L   = body_pt(0.72, 0.02, +spread_default)
    if paw_front_R   is None: paw_front_R   = body_pt(0.72, 0.02, -spread_default)
    if paw_back_L    is None: paw_back_L    = body_pt(0.22, 0.02, +spread_default)
    if paw_back_R    is None: paw_back_R    = body_pt(0.22, 0.02, -spread_default)
    if head_centroid is None: head_centroid = body_pt(1.00, 0.90, 0.0)
    if tail_tip      is None: tail_tip      = body_pt(-0.15, 0.55, 0.0)

    print(f"[rig_quadruped] anatomy: hip={hip_center.to_tuple(3)} chest={chest_center.to_tuple(3)} "
          f"head={head_centroid.to_tuple(3)} tail_tip={tail_tip.to_tuple(3)}")
    print(f"[rig_quadruped] anatomy: paw_front_L={paw_front_L.to_tuple(3)} paw_front_R={paw_front_R.to_tuple(3)} "
          f"paw_back_L={paw_back_L.to_tuple(3)} paw_back_R={paw_back_R.to_tuple(3)}")
    print(f"[rig_quadruped] anatomy: shoulder_L={shoulder_L.to_tuple(3)} shoulder_R={shoulder_R.to_tuple(3)} "
          f"thigh_L={thigh_L.to_tuple(3)} thigh_R={thigh_R.to_tuple(3)}")

    return {
        'hip_center': hip_center,
        'chest_center': chest_center,
        'head_centroid': head_centroid,
        'tail_tip': tail_tip,
        'shoulder_L': shoulder_L,
        'shoulder_R': shoulder_R,
        'thigh_L': thigh_L,
        'thigh_R': thigh_R,
        'paw_front_L': paw_front_L,
        'paw_front_R': paw_front_R,
        'paw_back_L': paw_back_L,
        'paw_back_R': paw_back_R,
    }


def body_point(bbox, length_ratio, height_ratio, side_offset):
    """Sample a point in the body frame.

    length_ratio : 0.0 = tail-end, 0.5 = midpoint, 1.0 = head-end
    height_ratio : 0.0 = ground, 1.0 = top of body
    side_offset  : signed world units from the centerline along the side axis
    """
    # Map length_ratio into the body-length span around the center.
    forward_offset = (length_ratio - 0.5) * bbox["body_length"]
    base = Vector((bbox["center_x"], bbox["center_y"], bbox["ground_z"]))
    pt = base + bbox["forward"] * forward_offset + bbox["side"] * side_offset
    pt.z = bbox["ground_z"] + height_ratio * bbox["body_height"]
    return pt


# ---------------------------------------------------------------------------
# Skeleton construction
# ---------------------------------------------------------------------------

SPECIES_RATIOS = {
    # head_height: where the head sits vertically (0 = ground, 1 = top of bbox)
    # shoulder_at: forward position of the shoulders (0 = tail-end, 1 = head-end)
    # hip_at:      forward position of the hips
    # leg_spread:  horizontal distance of leg centers from body centerline,
    #              as a ratio of body width (0.5 = at the side edge)
    # tail_length: tail length as a ratio of body length
    "cat": {
        "head_height":    0.90,
        "shoulder_at":    0.72,
        "hip_at":         0.22,
        "body_axis_y":    0.55,   # body mass center height (ratio of bbox)
        "leg_spread":     0.35,
        "tail_length":    0.35,
        "tail_segments":  4,
        "ear_spread":     0.18,   # ear L/R offset from head centerline
        "ear_height":     0.08,   # ear lift above head line
        "jaw_forward":    0.03,   # jaw extends forward past head origin
    },
    "dog": {
        "head_height":    0.90,
        "shoulder_at":    0.75,
        "hip_at":         0.25,
        "body_axis_y":    0.60,
        "leg_spread":     0.33,
        "tail_length":    0.28,
        "tail_segments":  3,
        "ear_spread":     0.15,
        "ear_height":     0.07,
        "jaw_forward":    0.04,
    },
}

# Canonical spine structure: matches the asset_browser.html CAT_BONE_LAYOUT
# so animation clips work on both the procedural canonical rig AND the
# GLB-rigged cats. Each entry is the bone's FRACTIONAL forward position
# along the hip-to-shoulder span [0.0 = hips, 1.0 = shoulders].
#
# Chain: hips -> lumbar_01 -> lumbar_02 -> spine_01 -> upper_back_01 ->
#        upper_back_02 -> upper_back_03 -> chest -> neck_01 -> neck_02 -> head
#        (chest is the shoulder-mount; neck + head extend past shoulders.)
BACK_BONES = [
    # (name, fraction along hip->shoulder span)
    ("lumbar_01",     1/7),
    ("lumbar_02",     2/7),
    ("spine_01",      3/7),
    ("upper_back_01", 4/7),
    ("upper_back_02", 5/7),
    ("upper_back_03", 6/7),
    # chest is the last bone - built explicitly after this loop.
]


def add_bone(armature_data, name, head, tail, parent=None, connect=False):
    bone = armature_data.edit_bones.new(name)
    bone.head = head
    bone.tail = tail
    if parent is not None:
        bone.parent = parent
        bone.use_connect = connect
    return bone


def build_armature(bbox, species, anatomy=None):
    """Create an armature object in edit mode and populate it with the
    quadruped skeleton, using detected anatomical landmarks when
    provided. anatomy should be the dict returned by detect_anatomy();
    if None, falls back to fixed SPECIES_RATIOS placement.

    Template strategy:
        bone topology (names + parent chains + leg segment count) stays
        constant across all cats. Positions interpolate between detected
        landmarks so each cat's bones follow its actual mesh:
            - hips / chest at detected hip_center / chest_center.
            - spine 8 bones linearly interpolate hip_center -> chest_center.
            - neck 2 + head along chest_center -> head_centroid, so a cat
              with head tilted up gets neck+head bones angled up.
            - tail 4 bones linearly interpolate hip_center -> tail_tip,
              so a raised/drooped tail follows its mesh direction.
            - shoulders and thighs at the detected L/R torso attach,
              legs (upper_arm, lower_arm, thigh, shin, foot, paws) split
              the shoulder/thigh -> paw_front/back segment into four
              equal pieces so a longer leg gets a longer rig and the
              knee/elbow sits at the mid-point regardless of pose.
    """
    ratios = SPECIES_RATIOS.get(species, SPECIES_RATIOS["cat"])

    bpy.ops.object.armature_add(enter_editmode=True, location=(0.0, 0.0, 0.0))
    arm_obj = bpy.context.active_object
    arm_obj.name = "Armature"
    arm = arm_obj.data

    if len(arm.edit_bones) > 0:
        arm.edit_bones.remove(arm.edit_bones[0])

    # If anatomy wasn't provided, fall back to original ratio-based points.
    # This preserves old behavior for any callers that still pass None.
    if anatomy is None:
        body_y = ratios["body_axis_y"]
        body_w = bbox["body_width"]
        spread = ratios["leg_spread"] * body_w
        anatomy = {
            'hip_center':    body_point(bbox, ratios["hip_at"], body_y, 0.0),
            'chest_center':  body_point(bbox, ratios["shoulder_at"], body_y, 0.0),
            'head_centroid': body_point(bbox, 1.0, ratios["head_height"], 0.0),
            'tail_tip':      body_point(bbox, ratios["hip_at"] - ratios["tail_length"],
                                        body_y * 0.95, 0.0),
            'shoulder_L':    body_point(bbox, ratios["shoulder_at"], body_y, +spread),
            'shoulder_R':    body_point(bbox, ratios["shoulder_at"], body_y, -spread),
            'thigh_L':       body_point(bbox, ratios["hip_at"], body_y, +spread),
            'thigh_R':       body_point(bbox, ratios["hip_at"], body_y, -spread),
            'paw_front_L':   body_point(bbox, ratios["shoulder_at"], 0.0, +spread),
            'paw_front_R':   body_point(bbox, ratios["shoulder_at"], 0.0, -spread),
            'paw_back_L':    body_point(bbox, ratios["hip_at"], 0.0, +spread),
            'paw_back_R':    body_point(bbox, ratios["hip_at"], 0.0, -spread),
        }

    hip_center    = Vector(anatomy['hip_center'])
    chest_center  = Vector(anatomy['chest_center'])
    head_centroid = Vector(anatomy['head_centroid'])
    tail_tip      = Vector(anatomy['tail_tip'])
    shoulder_L    = Vector(anatomy['shoulder_L'])
    shoulder_R    = Vector(anatomy['shoulder_R'])
    thigh_L       = Vector(anatomy['thigh_L'])
    thigh_R       = Vector(anatomy['thigh_R'])
    paw_front_L   = Vector(anatomy['paw_front_L'])
    paw_front_R   = Vector(anatomy['paw_front_R'])
    paw_back_L    = Vector(anatomy['paw_back_L'])
    paw_back_R    = Vector(anatomy['paw_back_R'])

    def lerp(a, b, t):
        return a + (b - a) * t

    # Root: sits below hip_center at ground level. Parents the whole rig
    # so the engine can translate the model without collapsing the pose.
    ground_pt = Vector((hip_center.x, hip_center.y, bbox["ground_z"]))
    root = add_bone(arm, "root", ground_pt, hip_center)

    # Spine chain hips -> 2 lumbar -> spine_01 -> 3 upper_back -> chest.
    # The chain's 8 bones evenly subdivide the straight line hip_center
    # -> chest_center. A curved spine would require N-point centerline
    # detection; we keep it straight because heat weights work fine on
    # a linear spine and it sidesteps the "detect spine curve through
    # props" problem.
    hips = add_bone(arm, "hips", hip_center, lerp(hip_center, chest_center, 1/8),
                    parent=root, connect=False)
    prev = hips
    for i, (bone_name, _) in enumerate(BACK_BONES):
        t0 = (i + 1) / 8
        t1 = (i + 2) / 8
        b = add_bone(arm, bone_name,
                     lerp(hip_center, chest_center, t0),
                     lerp(hip_center, chest_center, t1),
                     parent=prev, connect=(i > 0))
        prev = b
    chest = add_bone(arm, "chest",
                     lerp(hip_center, chest_center, 7/8),
                     chest_center,
                     parent=prev, connect=True)

    # Neck + head along chest -> head_centroid. If the head mesh tilts up
    # (raised chin) or down (hunting stance), head_centroid carries that
    # offset and the bone chain points wherever the mesh actually goes.
    neck_01 = add_bone(arm, "neck_01",
                       chest_center,
                       lerp(chest_center, head_centroid, 0.33),
                       parent=chest, connect=True)
    neck_02 = add_bone(arm, "neck_02",
                       lerp(chest_center, head_centroid, 0.33),
                       lerp(chest_center, head_centroid, 0.66),
                       parent=neck_01, connect=True)
    head_bone = add_bone(arm, "head",
                         lerp(chest_center, head_centroid, 0.66),
                         head_centroid,
                         parent=neck_02, connect=True)

    # Ears + jaw parented to head. Position by extrapolating from
    # head_centroid along axes that track the detected body frame:
    #   - ears stick UP (+Z world) and sideways by the side axis
    #   - jaw extends forward along the neck->head direction
    head_dir = (head_centroid - chest_center)
    if head_dir.length > 1e-6:
        head_dir = head_dir.normalized()
    else:
        head_dir = bbox["forward"]
    side_dir = bbox["side"]
    ear_half_spread = ratios.get("ear_spread", 0.18) * bbox["body_width"]
    ear_up = bbox["body_height"] * 0.08
    ear_base = head_centroid
    add_bone(arm, "ear_L",
             ear_base,
             ear_base + side_dir * ear_half_spread + Vector((0.0, 0.0, ear_up)),
             parent=head_bone, connect=False)
    add_bone(arm, "ear_R",
             ear_base,
             ear_base - side_dir * ear_half_spread + Vector((0.0, 0.0, ear_up)),
             parent=head_bone, connect=False)
    jaw_fwd = ratios.get("jaw_forward", 0.03) * bbox["body_length"]
    add_bone(arm, "jaw",
             head_centroid + Vector((0.0, 0.0, -bbox["body_height"] * 0.04)),
             head_centroid + head_dir * jaw_fwd + Vector((0.0, 0.0, -bbox["body_height"] * 0.07)),
             parent=head_bone, connect=False)

    # Tail: 4 equal segments along hip_center -> tail_tip. Whatever angle
    # the tail mesh has (pointing up, drooped, curled) the bones follow.
    tail_segs = max(1, ratios["tail_segments"])
    prev = hips
    for i in range(tail_segs):
        t0 = i / tail_segs
        t1 = (i + 1) / tail_segs
        tb = add_bone(arm, f"tail_{i+1:02d}",
                      lerp(hip_center, tail_tip, t0),
                      lerp(hip_center, tail_tip, t1),
                      parent=prev, connect=(i > 0))
        prev = tb

    # Legs: each side's chain linearly interpolates the detected shoulder/
    # thigh (torso attach) -> paw (ground contact) segment into 4 bones.
    # Longer leg on one side = longer rig segments on that side; knee /
    # elbow naturally lands at the midpoint of the leg reach.
    for side_name, attach_front, paw_front, attach_back, paw_back in (
        ("L", shoulder_L, paw_front_L, thigh_L, paw_back_L),
        ("R", shoulder_R, paw_front_R, thigh_R, paw_back_R),
    ):
        # Front leg: shoulder -> upper_arm -> lower_arm -> paw_front
        shoulder = add_bone(arm, f"shoulder_{side_name}",
                            attach_front,
                            lerp(attach_front, paw_front, 0.25),
                            parent=chest, connect=False)
        upper_arm = add_bone(arm, f"upper_arm_{side_name}",
                             lerp(attach_front, paw_front, 0.25),
                             lerp(attach_front, paw_front, 0.55),
                             parent=shoulder, connect=True)
        lower_arm = add_bone(arm, f"lower_arm_{side_name}",
                             lerp(attach_front, paw_front, 0.55),
                             lerp(attach_front, paw_front, 0.90),
                             parent=upper_arm, connect=True)
        add_bone(arm, f"paw_front_{side_name}",
                 lerp(attach_front, paw_front, 0.90),
                 paw_front,
                 parent=lower_arm, connect=True)

        # Back leg: thigh -> shin -> foot -> paw_back. Cats have a
        # 4-segment back leg (hip/thigh/shin/hock/paw); here we use
        # thigh/shin/foot as the three upper segments and paw_back as
        # the bottom toe-pad bone.
        thigh_bone = add_bone(arm, f"thigh_{side_name}",
                              attach_back,
                              lerp(attach_back, paw_back, 0.30),
                              parent=hips, connect=False)
        shin = add_bone(arm, f"shin_{side_name}",
                        lerp(attach_back, paw_back, 0.30),
                        lerp(attach_back, paw_back, 0.60),
                        parent=thigh_bone, connect=True)
        foot = add_bone(arm, f"foot_{side_name}",
                        lerp(attach_back, paw_back, 0.60),
                        lerp(attach_back, paw_back, 0.90),
                        parent=shin, connect=True)
        add_bone(arm, f"paw_back_{side_name}",
                 lerp(attach_back, paw_back, 0.90),
                 paw_back,
                 parent=foot, connect=True)

    bpy.ops.object.mode_set(mode='OBJECT')
    return arm_obj


# ---------------------------------------------------------------------------
# Parent + weight + export
# ---------------------------------------------------------------------------

def count_weighted_vertices(mesh_obj):
    """Count how many mesh vertices have AT LEAST ONE non-zero vertex-group
    weight. The glTF exporter only writes skinning data for vertices with
    weights; a mesh with zero weighted vertices exports as an unskinned
    plain mesh and the armature gets pruned from the output.

    Returns (weighted_count, total_count).
    """
    mesh = mesh_obj.data
    total = len(mesh.vertices)
    weighted = 0
    for v in mesh.vertices:
        for g in v.groups:
            if g.weight > 0.0:
                weighted += 1
                break
    return weighted, total


def vertex_group_coverage(mesh_obj):
    """For each vertex group on the mesh, count how many vertices actually
    carry a non-zero weight in that group. Returns list of (name, count)
    sorted descending so the log shows the problem bones at the bottom.
    Useful when heat weighting partially failed - we can see exactly which
    bones got zero coverage.
    """
    counts = {g.name: 0 for g in mesh_obj.vertex_groups}
    for v in mesh_obj.data.vertices:
        for g in v.groups:
            if g.weight > 0.0:
                counts[mesh_obj.vertex_groups[g.group].name] = counts.get(
                    mesh_obj.vertex_groups[g.group].name, 0
                ) + 1
    return sorted(counts.items(), key=lambda kv: kv[1], reverse=True)


def reset_parent(mesh_obj):
    """Undo a failed parent_set so we can retry with a different method.
    Clears the parent-child relationship, removes any Armature modifier
    that parent_set added, and wipes vertex groups. Without this the next
    parent_set call stacks on top of the broken state.
    """
    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_obj
    try:
        bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    except Exception:
        pass
    for mod in list(mesh_obj.modifiers):
        if mod.type == 'ARMATURE':
            mesh_obj.modifiers.remove(mod)
    # Wipe vertex groups so the retry starts from zero - otherwise envelope
    # weights get mixed with the partial heat results.
    while mesh_obj.vertex_groups:
        mesh_obj.vertex_groups.remove(mesh_obj.vertex_groups[0])


def zero_out_wrong_side_limb_weights(mesh_obj, arm_obj):
    """Heat-diffusion weighting is geodesic-distance-based through mesh
    topology. On a thin cat body the geodesic hop from thigh_L to the
    right-rear-leg verts (through the pelvis) is short enough that heat
    LEAKS ACROSS the body. The glTF top-4-weights-per-vertex limit then
    keeps whichever shoulder/thigh happened to win, and on walk playback
    rotating shoulder_L pulls right-side paw verts along for the ride.
    test_per_cat_anim.ts catches this as "back-paw MESH SYNCED
    (correlation=1.00)" on scout / player / storm_leader.
    Fix: for every laterally-placed limb bone (name ends in _L or _R
    AND the bone head has a clear lateral offset from the mesh center),
    walk the vertex list and ZERO the weight on any vertex that lives
    on the opposite side of the body centerline. This doesn't touch
    centerline bones (spine, neck, head, jaw, tail) because they're
    supposed to straddle the middle.
    Run this BEFORE fill_zero_coverage_bones so the envelope fallback
    measures "actual zero after cleanup" - some bones look covered by
    heat leakage but are actually all wrong-side and should be rebuilt
    from envelope on their correct side.
    """
    # Mesh bbox -> centerline. Same convention as fill_zero_coverage_bones.
    bbox_corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [v.x for v in bbox_corners]; ys = [v.y for v in bbox_corners]; zs = [v.z for v in bbox_corners]
    x_ext = max(xs) - min(xs); y_ext = max(ys) - min(ys); z_ext = max(zs) - min(zs)
    mesh_max_dim = max(x_ext, y_ext, z_ext)
    center = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2))

    # The SIDE axis is the one where L-vs-R bone pairs differ most.
    # Previously I used "shorter horizontal bbox axis" but on cats where
    # Meshy baked scale into the mesh transform (scout: x_ext=1.882,
    # y_ext=1.569 in WORLD-bbox coords even though raw verts only span
    # 0.633m along x), the bbox-shape heuristic misidentifies the side
    # axis. Measuring from L-vs-R bone pair positions is direct and
    # doesn't depend on bbox proportions.
    def find_side_axis():
        for left_name, right_name in (('shoulder_L', 'shoulder_R'),
                                       ('thigh_L',    'thigh_R'),
                                       ('upper_arm_L','upper_arm_R')):
            l = arm_obj.data.bones.get(left_name)
            r = arm_obj.data.bones.get(right_name)
            if not l or not r: continue
            lw = arm_obj.matrix_world @ l.head_local
            rw = arm_obj.matrix_world @ r.head_local
            diffs = [abs(lw[0] - rw[0]), abs(lw[1] - rw[1]), abs(lw[2] - rw[2])]
            # Horizontal axes only (X and Y post-import-to-Blender; Z is up).
            diffs[2] = -1  # exclude up
            return diffs.index(max(diffs[:2]))
        return 0  # fallback to X
    side_axis_index = find_side_axis()

    # Collect limb bones paired L/R. A bone qualifies if its head has a
    # lateral offset (along the side axis) of >5% of mesh max dim.
    limb_bones = []
    for bone in arm_obj.data.bones:
        if not (bone.name.endswith('_L') or bone.name.endswith('_R')):
            continue
        head_world = arm_obj.matrix_world @ bone.head_local
        lateral_offset = head_world[side_axis_index] - center[side_axis_index]
        if abs(lateral_offset) < mesh_max_dim * 0.05:
            continue  # centerline-ish bone, skip
        sign = 1 if lateral_offset > 0 else -1
        limb_bones.append((bone.name, side_axis_index, sign))

    total_zeroed = 0
    for bone_name, axis_idx, sign in limb_bones:
        if bone_name not in mesh_obj.vertex_groups:
            continue
        group = mesh_obj.vertex_groups[bone_name]
        wrong_side_verts = []
        for v in mesh_obj.data.vertices:
            v_world = mesh_obj.matrix_world @ v.co
            v_offset = v_world[axis_idx] - center[axis_idx]
            if (v_offset * sign) >= 0:
                continue
            for g in v.groups:
                if g.group == group.index and g.weight > 0.0:
                    wrong_side_verts.append(v.index)
                    break
        if wrong_side_verts:
            # group.remove() can leave a residual zero-weight entry in
            # some Blender builds; set to 0 first then remove.
            for idx in wrong_side_verts:
                group.add([idx], 0.0, 'REPLACE')
            group.remove(wrong_side_verts)
            total_zeroed += len(wrong_side_verts)
    if total_zeroed > 0:
        print(f"[rig_quadruped] zeroed {total_zeroed} wrong-side limb weights "
              f"across {len(limb_bones)} laterally-placed bones (fixes heat "
              f"crossover that causes synchronized-legs on walk)")


def fill_zero_coverage_bones(mesh_obj, arm_obj, max_radius_ratio=0.25):
    """For every bone that ended up with ZERO vertex weights after the
    primary weighting pass, manually assign fallback weights based on
    distance to the bone segment. Uses a simple inverse-distance falloff:
    weight = max(0, 1 - d / radius), where d is the closest distance from
    the vertex to the bone's head-tail segment.

    Why this exists:
        Heat diffusion succeeds globally (>=50% mesh coverage) but can
        fail PER-BONE when a bone sits outside the mesh volume or too
        far from any vertex neighborhood to diffuse into. On sitting-pose
        Meshy cats this is common: shoulders land in cranium airspace,
        head lands above the actual head mesh, so those bones get zero
        weight. The animation clip rotates them but the mesh doesn't
        follow - user sees "front legs don't move" or "head stays
        still" at runtime.

    This fallback binds nearby mesh vertices to each zero-coverage bone
    via an envelope-style distance falloff. The result isn't as
    anatomically clean as heat diffusion, but it guarantees every bone
    has SOME influence on the mesh so the rig is fully animatable.

    Args:
        mesh_obj: the joined, heat-weighted mesh object.
        arm_obj: the armature object holding the bones.
        max_radius_ratio: envelope radius as a ratio of the mesh's
            largest dimension. 0.25 means "pull in vertices within 25%
            of the mesh's largest extent". Good default for our rig
            proportions; smaller = tighter, larger = more leakage.
    """
    coverage = {g.name: 0 for g in mesh_obj.vertex_groups}
    for v in mesh_obj.data.vertices:
        for g in v.groups:
            if g.weight > 0.0:
                coverage[mesh_obj.vertex_groups[g.group].name] = coverage.get(
                    mesh_obj.vertex_groups[g.group].name, 0
                ) + 1

    zero_bones = [name for name, c in coverage.items() if c == 0]
    if not zero_bones:
        return

    print(f"[rig_quadruped] filling envelope fallback for {len(zero_bones)} "
          f"zero-coverage bones: {', '.join(zero_bones)}")

    # Mesh dims -> envelope radius. For our rig, mesh is ~1m tall so
    # radius ~0.25m which comfortably reaches most bones.
    bbox_corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [v.x for v in bbox_corners]
    ys = [v.y for v in bbox_corners]
    zs = [v.z for v in bbox_corners]
    mesh_max_dim = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    radius = mesh_max_dim * max_radius_ratio

    mesh_inv = mesh_obj.matrix_world.inverted()
    arm_to_mesh = mesh_inv @ arm_obj.matrix_world

    # Make sure each zero-bone has a vertex group (it should, but defensive).
    for bone_name in zero_bones:
        if bone_name not in mesh_obj.vertex_groups:
            mesh_obj.vertex_groups.new(name=bone_name)

    # For each zero-coverage bone, compute distance to every vertex and
    # assign a falloff weight within the envelope radius.
    #
    # Side-restriction for left/right bones: when a shoulder_L or
    # thigh_L gets zero heat-weight coverage, the unrestricted envelope
    # pulls verts within 0.475m of the bone - and on a thin cat mesh,
    # verts on the OPPOSITE side of the body (shoulder_R region) sit
    # just inside that radius. After the glTF top-4 weight limit, the
    # envelope weight can push out the legitimate same-side shoulder
    # and leave right-side verts partly driven by shoulder_L. At walk
    # time that shows up as the two front legs "acting as one" because
    # rotating shoulder_L drags right-side paw mesh along. test_per_cat_anim.ts's
    # mesh-skinning check caught this on scout (back-paw MESH
    # SYNCED, correlation=1.00) and player (correlation=0.96).
    #
    # The fix: if the bone name ends in _L or _R, only consider verts
    # on that side of the body centerline. "Centerline" is the mesh
    # bbox midpoint along the side axis. For limb bones, the side axis
    # is the mesh's narrowest horizontal extent (shoulders are spread
    # out along that axis). We detect it by noting that the bone's
    # head offset from the centerline is large along exactly one axis.
    # SIDE axis = shorter horizontal axis (body_width). Body_length is
    # forward, so using max-offset would misidentify forward as side for
    # thigh bones. See zero_out_wrong_side_limb_weights for a fuller
    # explanation.
    # Pick side axis from L/R limb-bone delta (same strategy as zero_out).
    # bbox-shape heuristic misfires when Meshy bakes scale into the mesh
    # transform so all three axes come out similar (scout: 1.88x1.57x1.90).
    def _find_side_axis_env():
        for left_name, right_name in (('shoulder_L', 'shoulder_R'),
                                       ('thigh_L',    'thigh_R'),
                                       ('upper_arm_L','upper_arm_R')):
            l = arm_obj.data.bones.get(left_name)
            r = arm_obj.data.bones.get(right_name)
            if not l or not r: continue
            lw = arm_obj.matrix_world @ l.head_local
            rw = arm_obj.matrix_world @ r.head_local
            diffs = [abs(lw[0] - rw[0]), abs(lw[1] - rw[1])]  # X and Y only (Z is up)
            return diffs.index(max(diffs))
        return 0  # fallback to X
    side_axis_index = _find_side_axis_env()
    center = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2))

    def side_axis_and_sign(bone_head, center):
        """Return (axis_index, sign) for a laterally-placed L/R bone.
        axis_index is the fixed body side-axis (shorter horizontal),
        sign is +1 / -1 for which half of the body the bone head sits on.
        Returns None if the bone's lateral offset is within 5% of mesh
        max dim (centerline-ish bones like head, jaw, tail, spine that
        shouldn't be side-filtered)."""
        lateral = bone_head[side_axis_index] - center[side_axis_index]
        if abs(lateral) < mesh_max_dim * 0.05: return None
        return side_axis_index, (1 if lateral > 0 else -1)

    for bone_name in zero_bones:
        bone = arm_obj.data.bones.get(bone_name)
        if not bone:
            continue
        # Transform bone head/tail from armature space into mesh space
        # so vertex coords (in mesh local) are comparable.
        head = arm_to_mesh @ bone.head_local
        tail = arm_to_mesh @ bone.tail_local
        seg = tail - head
        seg_len_sq = seg.length_squared
        group = mesh_obj.vertex_groups[bone_name]

        # Decide whether to apply the side-filter. Bones named *_L / *_R
        # with a clear lateral offset get filtered to their half of the
        # body; everything else (centerline bones like head, jaw, neck,
        # spine, tail) is not filtered because its natural envelope IS
        # centerline-straddling.
        side_info = None
        head_world = arm_obj.matrix_world @ bone.head_local
        if bone_name.endswith('_L') or bone_name.endswith('_R'):
            side_info = side_axis_and_sign(head_world, center)

        # First pass: radius-based envelope. Computes distance from each
        # vertex to the bone segment; within radius gets a falloff weight.
        assigned = 0
        min_d = float('inf')
        distances = []  # (vertex_index, distance) for all verts - used for
                        # nearest-N fallback if the radius pass finds none.
        skipped_wrong_side = 0
        for v in mesh_obj.data.vertices:
            # Side filter: if this bone is laterally placed, skip verts
            # on the opposite half of the body to prevent cross-body
            # weight leakage (the walk-synced-front-legs bug).
            if side_info is not None:
                axis_idx, sign = side_info
                v_world = mesh_obj.matrix_world @ v.co
                v_offset = v_world[axis_idx] - center[axis_idx]
                if (v_offset * sign) < 0:
                    skipped_wrong_side += 1
                    continue

            if seg_len_sq > 1e-8:
                t = max(0.0, min(1.0, (v.co - head).dot(seg) / seg_len_sq))
                closest = head + seg * t
            else:
                closest = head
            d = (v.co - closest).length
            distances.append((v.index, d))
            if d < min_d: min_d = d
            if d < radius:
                # Full-strength weight (up to 1.0) so the envelope fallback
                # can compete for the top-4 weights-per-vertex slots that
                # glTF's skin format allows. An earlier *0.5 scaling made
                # envelope weights too weak - heat weights dominated and
                # the envelope bones got sliced out during glTF export,
                # leaving the bone at 0 vertex coverage in the final GLB.
                w = 1.0 - (d / radius)
                if w > 0.01:
                    group.add([v.index], w, 'REPLACE')
                    assigned += 1

        # Second pass: if no vertex was within radius (bone sits too far
        # outside the mesh - typical for ear/jaw bones on sitting-pose
        # Meshy cats where the face mesh doesn't extend to where we
        # placed the bone), bind the N=200 NEAREST vertices via inverse-
        # distance weighting. Guarantees every bone has SOME influence.
        # Same-side filter was already applied during the distance
        # computation loop so `distances` only contains same-side verts
        # for laterally-placed bones.
        if assigned == 0:
            distances.sort(key=lambda p: p[1])
            n = min(200, len(distances))
            # Use min-distance as reference so weights scale sanely even
            # when the bone is much farther away than typical radius.
            for idx, d in distances[:n]:
                w = min_d / max(d, 1e-6) if min_d > 0 else 1.0
                if w > 0.01:
                    group.add([idx], w, 'REPLACE')
                    assigned += 1
            print(f"[rig_quadruped]   {bone_name}: nearest-{n} fallback "
                  f"(out-of-range bone, min_d={min_d:.3f}m), assigned {assigned}"
                  + (f" [side-filtered: skipped {skipped_wrong_side} opposite-side verts]" if side_info else ""))
        else:
            print(f"[rig_quadruped]   {bone_name}: envelope-assigned "
                  f"{assigned} verts (radius={radius:.3f}m)"
                  + (f" [side-filtered: skipped {skipped_wrong_side} opposite-side verts]" if side_info else ""))

    # Two-step cleanup: (1) limit each vertex to 4 bone influences (glTF
    # skin format max - beyond 4 get dropped anyway during export); this
    # keeps the STRONGEST weights so our envelope fallback has a fair
    # shot at the top-4 slots. (2) Normalize so each vertex's remaining
    # weights sum to 1 - prevents the mesh from over- or under-scaling
    # when multiple bones influence the same vertex.
    bpy.ops.object.select_all(action='DESELECT')
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_obj
    try:
        bpy.ops.object.vertex_group_limit_total(limit=4)
    except Exception as err:
        print(f"[rig_quadruped] vertex_group_limit_total warning: {err}", file=sys.stderr)
    try:
        bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    except Exception as err:
        print(f"[rig_quadruped] normalize_all warning: {err}", file=sys.stderr)


def parent_with_auto_weights(mesh_obj, arm_obj):
    """Parent the mesh to the armature with Blender's automatic (heat-
    diffusion) weights, verify the result actually produced weights, and
    fall back to envelope weights if heat silently produced an empty
    skinning. Heat is the gold-standard method for character meshes;
    envelope is coarser but always produces some weights based on bone
    proximity.

    Why the verification is necessary:
      bpy.ops.object.parent_set(type='ARMATURE_AUTO') does NOT raise when
      the heat solver fails for every bone. Blender 4.4 logs a warning
      ("Bone Heat Weighting: failed to find solution for one or more
      bones") but returns {'FINISHED'}, so a bare try/except thinks
      everything worked. Meanwhile the mesh has zero vertex weights and
      the glTF exporter drops the armature entirely on export. To detect
      this, we inspect vertex-group coverage AFTER parent_set and fall
      back to envelope when heat produced no usable weights.

    Cases that trip heat diffusion:
      - Non-manifold geometry (holes, edges shared by >2 faces)
      - Duplicate vertices at the same world position
      - Disconnected mesh islands (e.g. Meshy occasionally exports the
        cat's collar as a separate mesh island)
      - Bones placed outside the mesh volume (common when Meshy produces
        a sitting-pose cat and our bbox-ratio rig drops bones in the air)

    Fallback chain:
      1. ARMATURE_AUTO (heat). Verify >= 50% vertex coverage.
      2. ARMATURE_ENVELOPE. Verify >= 50% vertex coverage.
      3. ARMATURE (empty groups). Last resort - mesh won't deform but at
         least the armature survives the export.

    The 50% threshold catches the total-failure case (0 weighted verts on
    storm_mentor) while allowing partial failures to still pass. Partial
    failures typically mean some bones got no weights but the mesh is
    still riggable for the majority of anim tracks.
    """
    def try_parent(parent_type):
        bpy.ops.object.select_all(action='DESELECT')
        mesh_obj.select_set(True)
        arm_obj.select_set(True)
        bpy.context.view_layer.objects.active = arm_obj
        bpy.ops.object.parent_set(type=parent_type)

    min_coverage_ratio = 0.50

    # Attempt 1: heat diffusion.
    try:
        try_parent('ARMATURE_AUTO')
        weighted, total = count_weighted_vertices(mesh_obj)
        coverage = (weighted / total) if total else 0.0
        print(f"[rig_quadruped] heat weights: {weighted}/{total} verts "
              f"weighted ({coverage*100:.1f}%)")

        if coverage >= min_coverage_ratio:
            print("[rig_quadruped] parented with automatic (heat) weights")
            # Clean up heat-diffusion crossover BEFORE filling zero-coverage
            # bones: on a thin cat body, heat leaks across the pelvis so
            # left-side thighs get weight on right-side verts (and vice
            # versa). Without this, the walk clip pulls right-paw mesh
            # along with the left thigh and the user sees "back legs
            # acting as one". Test: test_per_cat_anim.ts "back-paw MESH
            # SYNCED" check.
            zero_out_wrong_side_limb_weights(mesh_obj, arm_obj)
            # Per-bone fallback: fill zero-coverage bones with envelope
            # weights so every bone has some mesh influence (animation
            # clip rotations will produce visible mesh motion instead of
            # silently no-op'ing).
            fill_zero_coverage_bones(mesh_obj, arm_obj)
            return

        print(f"[rig_quadruped] heat weights produced only "
              f"{coverage*100:.1f}% coverage (< {min_coverage_ratio*100:.0f}% "
              f"threshold), retrying with envelope", file=sys.stderr)
        reset_parent(mesh_obj)
    except Exception as err:
        print(f"[rig_quadruped] ARMATURE_AUTO raised: {err}", file=sys.stderr)
        reset_parent(mesh_obj)

    # Attempt 2: envelope weights. Always produces some weights based on
    # bone proximity - less accurate than heat but reliable.
    try:
        try_parent('ARMATURE_ENVELOPE')
        weighted, total = count_weighted_vertices(mesh_obj)
        coverage = (weighted / total) if total else 0.0
        print(f"[rig_quadruped] envelope weights: {weighted}/{total} verts "
              f"weighted ({coverage*100:.1f}%)")

        if coverage >= min_coverage_ratio:
            print("[rig_quadruped] parented with envelope weights (heat-fallback)")
            # Same crossover cleanup + zero-coverage fallback as the heat path.
            zero_out_wrong_side_limb_weights(mesh_obj, arm_obj)
            fill_zero_coverage_bones(mesh_obj, arm_obj)
            return

        print(f"[rig_quadruped] envelope produced only {coverage*100:.1f}% "
              f"coverage - falling through to empty-group parent",
              file=sys.stderr)
        reset_parent(mesh_obj)
    except Exception as err:
        print(f"[rig_quadruped] ARMATURE_ENVELOPE raised: {err}", file=sys.stderr)
        reset_parent(mesh_obj)

    # Attempt 3: parent with no weights. Mesh won't deform but the armature
    # makes it into the export, which is infinitely preferable to shipping
    # a bone-less "rigged" file that silently lies about being rigged.
    try_parent('ARMATURE')
    print("[rig_quadruped] ERROR: parented with empty groups - mesh will "
          "NOT deform. Manual weight painting required in Blender.",
          file=sys.stderr)


def bake_animation_clips(arm_obj, fps=24):
    """Bake the same animation clips the asset browser builds in JS
    directly into the armature as Blender Actions, then push them to
    NLA tracks so the glTF exporter writes them into the output file's
    `animations` array. After this the rigged GLB carries walk / run /
    sit / lay / standUp / idle clips that the game engine (or any
    three.js viewer) can play without reimplementing the clip math.

    Math mirrors asset_browser.html:
      - leg / knee swings rotate around the rig's SIDE axis (perpendicular
        to the hip->chest forward direction, horizontal)
      - tail swish rotates around world +Z (Blender up)
      - sit/lay drop root bone along world -Z, spine bones along world
        +Z as compensation (non-linear 5-key curve matching paw reach)
      - idle holds a zero-delta pose

    To map a world-axis rotation into a Blender pose rotation on a bone
    with arbitrary rest orientation, we use the conjugation identity:
        matrix_basis = rest_world_inv * Quaternion(world_axis, angle) * rest_world
      = Quaternion(rest_world_inv @ world_axis, angle)
    where rest_world is the bone's world-space rest rotation
    (armature.matrix_world @ bone.matrix_local).to_quaternion().
    """
    import math
    from mathutils import Quaternion as _Q, Vector as _V

    scene = bpy.context.scene
    scene.render.fps = fps

    # Side axis from hips -> chest (same as computeRigSideAxis in the
    # browser). Skeleton is in armature-local coords by now; we want the
    # world-space direction so the rotation applies the same way as the
    # browser computes it.
    hips = arm_obj.pose.bones.get('hips')
    chest = arm_obj.pose.bones.get('chest')
    if hips and chest:
        hips_w = arm_obj.matrix_world @ hips.head
        chest_w = arm_obj.matrix_world @ chest.head
        forward = chest_w - hips_w
        forward.z = 0
        if forward.length > 1e-6:
            forward.normalize()
            # side = cross(forward, up) matches browser (Y-up equivalent
            # here is Z since we're in Blender). Gives left-side axis.
            side = forward.cross(_V((0.0, 0.0, 1.0)))
            if side.length < 1e-6:
                side = _V((0.0, 1.0, 0.0))
            else:
                side.normalize()
        else:
            side = _V((0.0, 1.0, 0.0))
    else:
        side = _V((0.0, 1.0, 0.0))
    side_axis = side  # world-space vector

    def bone_rest_world_quat(pose_bone):
        m = arm_obj.matrix_world @ pose_bone.bone.matrix_local
        return m.to_quaternion()

    def basis_for_world_rot(pose_bone, world_axis, angle):
        rw = bone_rest_world_quat(pose_bone)
        local_axis = rw.inverted() @ _V(world_axis)
        return _Q(local_axis, angle)

    def basis_for_world_translation(pose_bone, world_delta):
        rw = bone_rest_world_quat(pose_bone)
        return rw.inverted() @ _V(world_delta)

    # Enter pose mode so pose_bone keyframing works.
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.select_all(action='DESELECT')
    arm_obj.select_set(True)
    bpy.ops.object.mode_set(mode='POSE')

    if arm_obj.animation_data is None:
        arm_obj.animation_data_create()

    actions_to_push = []

    def new_action(name):
        # Fresh action per clip; detach any previous assignment.
        a = bpy.data.actions.new(name)
        a.use_fake_user = True
        arm_obj.animation_data.action = a
        # Zero all pose bones so this clip starts from rest.
        for pb in arm_obj.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.location = (0.0, 0.0, 0.0)
        return a

    def kf_rot(bone_name, frame, world_axis, angle):
        pb = arm_obj.pose.bones.get(bone_name)
        if pb is None:
            return
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = basis_for_world_rot(pb, world_axis, angle)
        pb.keyframe_insert(data_path='rotation_quaternion', frame=frame, group=bone_name)

    def kf_loc(bone_name, frame, world_delta):
        pb = arm_obj.pose.bones.get(bone_name)
        if pb is None:
            return
        pb.location = basis_for_world_translation(pb, world_delta)
        pb.keyframe_insert(data_path='location', frame=frame, group=bone_name)

    UP = _V((0.0, 0.0, 1.0))
    DROP_FRAC_BY_SCALE = {0.0: 0.000, 0.25: 0.064, 0.5: 0.241, 0.75: 0.509, 1.0: 0.832}
    KEY_SCALES = [0.0, 0.25, 0.5, 0.75, 1.0]
    SIT_THIGH_DEG = 85
    SIT_SHIN_DEG = -170
    SIT_FOOT_DEG = 85

    # rest hip world height = y-coord of hips pose_bone in world, which drives
    # DROP magnitude. Same quantity the browser computes as hips.position.y.
    hip_world_height = 0.22
    hips_pb = arm_obj.pose.bones.get('hips')
    if hips_pb:
        hip_world_height = (arm_obj.matrix_world @ hips_pb.head).z

    SHARES = {
        'lumbar_01': 0.15, 'lumbar_02': 0.18,
        'spine_01':  0.18, 'upper_back_01': 0.14, 'upper_back_02': 0.12, 'upper_back_03': 0.10,
        'chest':     0.13,
    }

    # -----------------------------------------------------------------
    # idle: subtle breathing + head sway + tail flick over 2.4 s.
    #
    # The previous bake just inserted two zero-delta keyframes (rest pose
    # held for 2 s), which made the engine's Animator->update loop run
    # CPU vertex skinning at 60 fps but produce visually identical bone
    # palettes every frame -- the cat looked frozen in T-pose. Confirmed
    # by the cat-annihilation 2026-04-25 playtest: 1106 frames over 22 s,
    # vsync-locked 60 fps, but the player cat's silhouette in --frame-dump
    # was bit-identical between t=1 s and t=10 s (frame-N to frame-600
    # diff produced zero deltas in the cat region).
    #
    # Real idle motion has to satisfy three constraints to read at the
    # game's typical viewing distance (camera ~3-5 m back, 16:9 1080p):
    #
    #   1. Breath cycle on chest+lumbar -- the most universal "alive"
    #      cue, ~12 breaths/min ~= 0.2 Hz, but in stylised game idle
    #      the cycle is sped up ~3x to 0.6 Hz so it reads in a 2 s
    #      preview clip without the loop boundary popping.
    #   2. Head micro-sway -- breaks the "statue head" look that pure
    #      chest motion still has. Yaw + pitch combined, low frequency
    #      so it doesn't feel jittery.
    #   3. Tail tip motion -- the most visible single bone because of
    #      its long lever arm; even small angular motion at the root
    #      maps to multi-cm tip motion which is unambiguously visible
    #      in screen space at typical camera distance.
    #
    # All amplitudes deliberately small (~2-6 deg) -- this is idle, not
    # walk. The walk clip uses 22 deg leg swings; we want the silhouette
    # delta between idle and walk to be obvious. If anything, prefer
    # under-amplitude here -- a too-bouncy idle feels nauseous.
    #
    # Period choice (2.4 s) is intentionally not 2.0 s so the breath
    # cycle and tail sway aren't both at the loop boundary -- subdividing
    # the cycle into 9 keyframes (frames 0..2.4*fps) lets the breath hit
    # exhale at frame 0 (= rest), inhale at the half-cycle, exhale again
    # at the loop, with the tail running a half-period offset so the
    # combined motion never returns to the exact same pose twice within
    # the loop. That's what kills the "static statue" perception.
    # -----------------------------------------------------------------
    idle = new_action('idle')
    idle_dur = 2.4
    idle_dur_f = int(idle_dur * fps)

    # Sample count: 9 keyframes -> 8 segments -> period repeats cleanly.
    idle_samples = 9

    breath_amp_chest  = math.radians(3.0)   # chest pitches up on inhale
    breath_amp_lumbar = math.radians(1.5)   # lumbar follows, smaller
    head_pitch_amp    = math.radians(2.0)   # head bobs with breath
    head_yaw_amp      = math.radians(3.0)   # head sways side-to-side
    neck_pitch_amp    = math.radians(1.5)   # neck shadows head pitch
    tail_yaw_amp      = math.radians(6.0)   # tail tip motion is biggest
    tail_pitch_amp    = math.radians(2.0)   # tail also lifts subtly

    # Breath: cosine starting at 1 (exhaled rest) -> -1 (peak inhale) ->
    # 1 (exhaled). Keyframe 0 lands at rest, keyframe 4 (mid-loop) at
    # peak inhale, keyframe 8 back to rest. Standard sine for tail/head
    # so they start at zero and oscillate +/- on either side.
    for i in range(idle_samples):
        f = int(i / (idle_samples - 1) * idle_dur_f)
        phase = i / (idle_samples - 1)  # 0..1 across the loop

        # Breath cosine: exhale at boundary, inhale mid-loop. Multiply by
        # -1 because cos(0)=1, cos(pi)=-1, and we want frame 0 to BE
        # rest (zero delta). So delta = (1 - cos(2*pi*phase)) / 2 * amp.
        breath = (1.0 - math.cos(2.0 * math.pi * phase)) * 0.5
        kf_rot('chest',     f, side_axis, breath * breath_amp_chest)
        kf_rot('upper_back_01', f, side_axis, breath * breath_amp_chest * 0.7)
        kf_rot('upper_back_02', f, side_axis, breath * breath_amp_chest * 0.5)
        kf_rot('lumbar_01', f, side_axis, breath * breath_amp_lumbar)
        kf_rot('lumbar_02', f, side_axis, breath * breath_amp_lumbar * 0.6)

        # Head pitch shadows breath (cat's head dips slightly on exhale,
        # rises on inhale). Same cosine, smaller amplitude, opposite sign
        # so head appears to sit on the chest's motion.
        kf_rot('neck_01', f, side_axis, -breath * neck_pitch_amp * 0.6)
        kf_rot('neck_02', f, side_axis, -breath * neck_pitch_amp)
        kf_rot('head',    f, side_axis, -breath * head_pitch_amp)

        # Head yaw: independent sine at the same period, half-phase
        # shifted so peak yaw lands between exhale and inhale. Reads as
        # the cat looking around while breathing.
        head_yaw = math.sin(2.0 * math.pi * phase + math.pi * 0.5) * head_yaw_amp
        kf_rot('head', f, UP, head_yaw)

        # Tail: yaw at full-period, plus a half-amplitude pitch lift on
        # the off-phase. Generous amplitude because of the long lever
        # arm -- 6 deg at the root maps to ~6-8 cm of tip motion on
        # the canonical cat rig (tail length ~= 0.6-0.8 m), which is
        # unambiguously visible in screen space.
        tail_yaw = math.sin(2.0 * math.pi * phase) * tail_yaw_amp
        kf_rot('tail_01', f, UP, tail_yaw)
        kf_rot('tail_02', f, UP, tail_yaw * 0.7)
        kf_rot('tail_03', f, UP, tail_yaw * 0.4)
        tail_pitch = math.sin(2.0 * math.pi * phase + math.pi * 0.5) * tail_pitch_amp
        kf_rot('tail_01', f, side_axis, tail_pitch)
        kf_rot('tail_02', f, side_axis, tail_pitch * 0.6)
    actions_to_push.append(idle)

    # -----------------------------------------------------------------
    # walk: lateral gait, 1.0s
    # -----------------------------------------------------------------
    walk = new_action('walk')
    dur = 1.0
    dur_f = int(dur * fps)
    amp = math.radians(22); knee_amp = math.radians(15)
    for bone_name, phase in [('shoulder_L', 0.0), ('thigh_L', 0.0),
                              ('shoulder_R', 0.5), ('thigh_R', 0.5)]:
        for i in range(5):
            f = int(i / 4 * dur_f)
            a = math.sin(2 * math.pi * (i/4 + phase)) * amp
            kf_rot(bone_name, f, side_axis, a)
    for bone_name, phase in [('lower_arm_L', 0.0), ('shin_L', 0.0),
                              ('lower_arm_R', 0.5), ('shin_R', 0.5)]:
        for i in range(9):
            f = int(i / 8 * dur_f)
            s = math.sin(2 * math.pi * (i/8 + phase))
            a = -max(0.0, s) * knee_amp
            kf_rot(bone_name, f, side_axis, a)
    for i in range(5):
        f = int(i / 4 * dur_f)
        a = math.sin(2 * math.pi * i / 4) * math.radians(10)
        kf_rot('tail_01', f, UP, a)
    head_pitch = math.radians(4); head_yaw = math.radians(6)
    for bone_name, w in [('neck_01', 0.6), ('neck_02', 1.0)]:
        for i in range(9):
            f = int(i / 8 * dur_f)
            a = math.sin(2 * math.pi * i / 8) * head_pitch * w
            kf_rot(bone_name, f, side_axis, a)
    for i in range(5):
        f = int(i / 4 * dur_f)
        a = -math.sin(2 * math.pi * i / 4) * head_yaw
        kf_rot('head', f, UP, a)
    actions_to_push.append(walk)

    # -----------------------------------------------------------------
    # run: simplified bound-style gallop, 0.55s. See
    # asset_browser.html buildRunClip for the rationale - both fronts
    # synced, both backs synced, 0.25-stride offset between pairs, big
    # spine flex. Reads cleanly for an in-place preview.
    # -----------------------------------------------------------------
    run = new_action('run')
    dur = 0.55; dur_f = int(dur * fps)
    back_leg_amp   = math.radians(50)
    front_leg_amp  = math.radians(45)
    back_knee_amp  = math.radians(60)
    front_knee_amp = math.radians(50)
    spine_amp      = math.radians(28)
    chest_amp      = math.radians(18)
    legs_run = [
        ('thigh_L',    0.00, back_leg_amp),
        ('thigh_R',    0.00, back_leg_amp),
        ('shoulder_L', 0.25, front_leg_amp),
        ('shoulder_R', 0.25, front_leg_amp),
    ]
    for bone_name, phase, legamp in legs_run:
        for i in range(9):
            f = int(i / 8 * dur_f)
            a = math.sin(2 * math.pi * (i/8 + phase)) * legamp
            kf_rot(bone_name, f, side_axis, a)
    # Knees fold rearward (negative); elbows fold forward (positive) -
    # opposite signs for the two because cat knees and elbows flex in
    # anatomically opposite directions. See asset_browser.html comments.
    knees_run = [
        ('shin_L',      0.00, back_knee_amp,  -1.0),
        ('shin_R',      0.00, back_knee_amp,  -1.0),
        ('lower_arm_L', 0.25, front_knee_amp, +1.0),
        ('lower_arm_R', 0.25, front_knee_amp, +1.0),
    ]
    for bone_name, phase, kneeamp, sign in knees_run:
        for i in range(9):
            f = int(i / 8 * dur_f)
            s = math.sin(2 * math.pi * (i/8 + phase))
            a = sign * max(0.0, s) * kneeamp
            kf_rot(bone_name, f, side_axis, a)
    for bone_name, bend_amp in [('spine_01', spine_amp), ('chest', chest_amp)]:
        for i in range(9):
            f = int(i / 8 * dur_f)
            a = math.cos(2 * math.pi * i / 8) * bend_amp
            kf_rot(bone_name, f, side_axis, a)
    # Head stabilization: counter-rotate neck to cancel spine+chest flex,
    # so the head stays level on its target while the body pumps. See
    # asset_browser.html buildRunClip for the rationale.
    spine_sum = spine_amp + chest_amp
    for bone_name, w in [('neck_01', 0.5), ('neck_02', 0.5)]:
        for i in range(9):
            f = int(i / 8 * dur_f)
            a = -math.cos(2 * math.pi * i / 8) * spine_sum * w
            kf_rot(bone_name, f, side_axis, a)
    actions_to_push.append(run)

    # -----------------------------------------------------------------
    # sitDown: deep back-leg fold + root drop, 1.3s, one-shot
    # -----------------------------------------------------------------
    sit = new_action('sitDown')
    dur = 1.3; dur_f = int(dur * fps)
    DROP = hip_world_height * 0.80
    for bone_name, deg in [('thigh_L', SIT_THIGH_DEG), ('thigh_R', SIT_THIGH_DEG),
                            ('shin_L',  SIT_SHIN_DEG),  ('shin_R',  SIT_SHIN_DEG),
                            ('foot_L',  SIT_FOOT_DEG),  ('foot_R',  SIT_FOOT_DEG),
                            ('neck_01', 10), ('neck_02', 10), ('head', 8)]:
        kf_rot(bone_name, 0, side_axis, 0.0)
        kf_rot(bone_name, dur_f, side_axis, math.radians(deg))
    # Root drop + spine-bone lift along world Z, 5 keyframes non-linear
    for s in KEY_SCALES:
        f = int(s * dur_f)
        kf_loc('root', f, _V((0.0, 0.0, -hip_world_height * DROP_FRAC_BY_SCALE[s])))
        for bn, share in SHARES.items():
            kf_loc(bn, f, _V((0.0, 0.0, hip_world_height * DROP_FRAC_BY_SCALE[s] * share)))
    actions_to_push.append(sit)

    # -----------------------------------------------------------------
    # layDown: both fronts + backs fold, no spine lift, 1.6s
    # -----------------------------------------------------------------
    lay = new_action('layDown')
    dur = 1.6; dur_f = int(dur * fps)
    for bone_name, deg in [('thigh_L', SIT_THIGH_DEG), ('thigh_R', SIT_THIGH_DEG),
                            ('shin_L',  SIT_SHIN_DEG),  ('shin_R',  SIT_SHIN_DEG),
                            ('foot_L',  SIT_FOOT_DEG),  ('foot_R',  SIT_FOOT_DEG),
                            ('shoulder_L', SIT_THIGH_DEG), ('shoulder_R', SIT_THIGH_DEG),
                            ('upper_arm_L', SIT_SHIN_DEG), ('upper_arm_R', SIT_SHIN_DEG),
                            ('lower_arm_L', SIT_FOOT_DEG), ('lower_arm_R', SIT_FOOT_DEG),
                            ('neck_01', 25), ('neck_02', 20), ('head', 15),
                            ('tail_01', -20), ('tail_02', -30)]:
        kf_rot(bone_name, 0, side_axis, 0.0)
        kf_rot(bone_name, dur_f, side_axis, math.radians(deg))
    for s in KEY_SCALES:
        f = int(s * dur_f)
        kf_loc('root', f, _V((0.0, 0.0, -hip_world_height * DROP_FRAC_BY_SCALE[s])))
    actions_to_push.append(lay)

    # -----------------------------------------------------------------
    # standUpFromSit: unfold back legs + un-lift spine, 1.3s
    # -----------------------------------------------------------------
    stand_sit = new_action('standUpFromSit')
    dur = 1.3; dur_f = int(dur * fps)
    for bone_name, deg in [('thigh_L', SIT_THIGH_DEG), ('thigh_R', SIT_THIGH_DEG),
                            ('shin_L',  SIT_SHIN_DEG),  ('shin_R',  SIT_SHIN_DEG),
                            ('foot_L',  SIT_FOOT_DEG),  ('foot_R',  SIT_FOOT_DEG),
                            ('neck_01', 10), ('neck_02', 10), ('head', 8)]:
        kf_rot(bone_name, 0, side_axis, math.radians(deg))
        kf_rot(bone_name, dur_f, side_axis, 0.0)
    for s in KEY_SCALES:
        f = int((1.0 - s) * dur_f)
        kf_loc('root', f, _V((0.0, 0.0, -hip_world_height * DROP_FRAC_BY_SCALE[s])))
        for bn, share in SHARES.items():
            kf_loc(bn, f, _V((0.0, 0.0, hip_world_height * DROP_FRAC_BY_SCALE[s] * share)))
    actions_to_push.append(stand_sit)

    # -----------------------------------------------------------------
    # standUpFromLay: unfold all four legs, no spine un-lift, 1.3s
    # -----------------------------------------------------------------
    stand_lay = new_action('standUpFromLay')
    dur = 1.3; dur_f = int(dur * fps)
    for bone_name, deg in [('thigh_L', SIT_THIGH_DEG), ('thigh_R', SIT_THIGH_DEG),
                            ('shin_L',  SIT_SHIN_DEG),  ('shin_R',  SIT_SHIN_DEG),
                            ('foot_L',  SIT_FOOT_DEG),  ('foot_R',  SIT_FOOT_DEG),
                            ('shoulder_L', SIT_THIGH_DEG), ('shoulder_R', SIT_THIGH_DEG),
                            ('upper_arm_L', SIT_SHIN_DEG), ('upper_arm_R', SIT_SHIN_DEG),
                            ('lower_arm_L', SIT_FOOT_DEG), ('lower_arm_R', SIT_FOOT_DEG),
                            ('neck_01', 25), ('neck_02', 20), ('head', 15)]:
        kf_rot(bone_name, 0, side_axis, math.radians(deg))
        kf_rot(bone_name, dur_f, side_axis, 0.0)
    for s in KEY_SCALES:
        f = int((1.0 - s) * dur_f)
        kf_loc('root', f, _V((0.0, 0.0, -hip_world_height * DROP_FRAC_BY_SCALE[s])))
    actions_to_push.append(stand_lay)

    # -----------------------------------------------------------------
    # Push every action onto its own NLA track so the glTF exporter
    # picks them up (`export_animations=True` walks NLA strips).
    # -----------------------------------------------------------------
    arm_obj.animation_data.action = None  # clear active so export reads NLA
    for act in actions_to_push:
        track = arm_obj.animation_data.nla_tracks.new()
        track.name = act.name
        track.strips.new(act.name, 0, act)

    bpy.ops.object.mode_set(mode='OBJECT')
    print(f"[rig_quadruped] baked {len(actions_to_push)} animation clips: "
          f"{', '.join(a.name for a in actions_to_push)}")


def export_glb(output_path):
    """Export the rigged scene back to GLB and verify the result actually
    contains an armature + skinned mesh. Blender's glTF exporter silently
    drops the armature when the mesh has no skin data, producing a file
    that looks rigged (same name, similar size) but has zero bones. We
    parse back the essential structure and raise RuntimeError if the
    output is broken so rig_batch.ps1 can count it as a failure instead
    of listing it as success.

    Why post-export verification:
      Even with 50%+ vertex coverage the glTF export can still strip
      bones (e.g. if the joints are unreachable or if glTF's bone-per-
      primitive limit is exceeded). The only certain way to know the
      file is usable is to read it back.
    """
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.gltf(
        filepath=output_path,
        export_format='GLB',
        use_selection=False,
        export_apply=True,
        export_skins=True,
        export_animations=True,
        export_nla_strips=True,
    )
    verify_export(output_path)


def verify_export(glb_path):
    """Lightweight GLB parser that confirms the output has at least one
    skinned mesh + a skeleton. Avoids pulling a full glTF library - a GLB
    is JSON chunk followed by BIN chunk; we only need the JSON header.
    """
    import json
    import struct
    with open(glb_path, 'rb') as f:
        magic = f.read(4)
        if magic != b'glTF':
            raise RuntimeError(f"ERROR: output is not a valid GLB file: {glb_path}")
        f.read(8)  # version + total length
        chunk_len = struct.unpack('<I', f.read(4))[0]
        chunk_type = f.read(4)
        if chunk_type != b'JSON':
            raise RuntimeError(f"ERROR: GLB first chunk is not JSON: {glb_path}")
        meta = json.loads(f.read(chunk_len).decode('utf-8'))

    skins = meta.get('skins', [])
    nodes = meta.get('nodes', [])
    bone_count = sum(len(s.get('joints', [])) for s in skins)
    skinned_mesh_count = sum(1 for n in nodes if 'mesh' in n and 'skin' in n)

    print(f"[rig_quadruped] export verify: skins={len(skins)}, "
          f"bones={bone_count}, skinned_mesh_nodes={skinned_mesh_count}")

    if len(skins) == 0 or bone_count == 0:
        raise RuntimeError(
            f"ERROR: exported GLB has no skeleton - output is unrigged. "
            f"Skins: {len(skins)}, total joints: {bone_count}. "
            f"File: {glb_path}"
        )
    if skinned_mesh_count == 0:
        raise RuntimeError(
            f"ERROR: exported GLB has skeleton but no skinned mesh references "
            f"it. File: {glb_path}"
        )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    opts = parse_args()
    print(f"[rig_quadruped] input = {opts['input']}")
    print(f"[rig_quadruped] output = {opts['output']}")
    print(f"[rig_quadruped] species = {opts['species']}, "
          f"flip_forward = {opts['flip_forward']}")

    reset_scene()
    import_glb(opts["input"])

    mesh_obj = collect_and_join_meshes()
    strip_existing_rig(mesh_obj)
    cleanup_mesh(mesh_obj)  # reconnect topology so heat diffusion can solve

    # Pre-align: if the body's PCA long axis isn't roughly world-X, rotate
    # the mesh so it is. Makes analyze_bbox's "longest-axis = forward"
    # heuristic pick the correct axis on Meshy exports that happen to
    # come out with the body oblique in world space.
    align_mesh_to_world(mesh_obj)

    bbox = analyze_bbox(mesh_obj, opts["flip_forward"])
    print(f"[rig_quadruped] bbox length={bbox['body_length']:.3f} "
          f"width={bbox['body_width']:.3f} "
          f"height={bbox['body_height']:.3f} "
          f"along {bbox['length_axis']}")

    # Extract per-cat anatomical landmarks so build_armature can adapt
    # bone placement to this cat's actual mesh instead of fixed ratios.
    anatomy = detect_anatomy(mesh_obj, bbox)
    arm_obj = build_armature(bbox, opts["species"], anatomy=anatomy)
    parent_with_auto_weights(mesh_obj, arm_obj)

    # Bake procedural animation clips into the armature BEFORE export so
    # the output GLB carries walk / run / sit / lay / standUp / idle.
    # The game engine (and any three.js viewer) can play them directly
    # off gltf.animations - no need to duplicate the clip math in engine
    # code.
    try:
        bake_animation_clips(arm_obj)
    except Exception as err:
        print(f"[rig_quadruped] WARNING: baking animations failed: {err}", file=sys.stderr)

    # Surface export/verify failures as exit code 4. rig_batch.ps1 checks
    # LASTEXITCODE + Test-Path $outputFile; non-zero here means the script
    # produced no usable output, so the batch runner must count this as a
    # failure instead of treating missing file as silent success.
    try:
        export_glb(opts["output"])
    except RuntimeError as err:
        print(str(err), file=sys.stderr)
        sys.exit(4)

    print(f"Re-rigged: {opts['input']} -> {opts['output']}")


if __name__ == "__main__":
    main()
