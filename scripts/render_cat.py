"""
render_cat.py - Blender headless renderer for cat GLB inspection.

Purpose:
    Claude Code (the agent) can't see 3D models interactively in a browser,
    but it CAN read PNG files. This script renders a cat GLB from multiple
    angles, optionally with bone overlays + weight-paint visualization, so
    the agent can diagnose rigging problems by looking at rendered images.

Usage (headless):
    blender --background --python render_cat.py -- \\
        <input.glb> <output_dir> [--skeleton] [--weights] [--all]

Flags:
    --skeleton   Overlay bone lines in the render (only meaningful for
                 rigged cats - draws each bone as a colored line).
    --weights    Render with vertex colors set to each vert's dominant
                 bone influence. Uncovered verts appear black. Makes
                 weight leakage obvious.
    --all        Render skeleton + weights + plain views together
                 (default is just plain views).
    --views=N    Override number of camera angles. Default 4:
                 front, side (left), three-quarter, top.

Output:
    Writes <output_dir>/<name>_<view>.png files, one per camera angle.
    When multiple modes are requested (plain, skeleton, weights) each gets
    its own file with a mode suffix: <name>_<view>_<mode>.png.

Why render-based instead of live viewport:
    Blender's viewport rendering works headless but needs a compositor
    setup; we use the simpler Cycles/EEVEE final render. Low-resolution
    (512x512) keeps the per-angle cost under 10s, which matters because
    the typical diagnostic session wants 4-8 renders per cat across 17
    cats.
"""

import bpy
import sys
import os
import math
import colorsys
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
        print("Usage: blender --background --python render_cat.py -- "
              "<input.glb> <output_dir> [--skeleton] [--weights] [--all] [--views=N]",
              file=sys.stderr)
        sys.exit(1)

    opts = {
        "input": argv[0],
        "output_dir": argv[1],
        "skeleton": "--skeleton" in argv,
        "weights": "--weights" in argv,
        "all": "--all" in argv,
        "views": 4,
    }
    for arg in argv:
        if arg.startswith("--views="):
            opts["views"] = int(arg.split("=", 1)[1])

    if opts["all"]:
        # `--all` means we emit plain + skeleton + weights in one run so the
        # agent can diff the three modes without three separate invocations.
        opts["skeleton"] = True
        opts["weights"] = True

    os.makedirs(opts["output_dir"], exist_ok=True)
    return opts


# ---------------------------------------------------------------------------
# Scene setup
# ---------------------------------------------------------------------------

def reset_scene():
    bpy.ops.wm.read_homefile(use_empty=True)


def import_glb(path):
    try:
        bpy.ops.import_scene.gltf(filepath=path)
    except Exception as err:
        print(f"ERROR: gltf import failed: {err}", file=sys.stderr)
        sys.exit(2)


def find_mesh_and_armature():
    """Return (joined_mesh, armature_or_None). Meshy sometimes splits meshes;
    we join them so the render shows the whole cat.
    """
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    arm = next((o for o in bpy.data.objects if o.type == 'ARMATURE'), None)
    if not meshes:
        print("ERROR: no mesh found in input", file=sys.stderr)
        sys.exit(2)

    # Join meshes into one for consistent render. Preserves the armature
    # modifier if any mesh was skinned to it.
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    return bpy.context.active_object, arm


def frame_object(mesh_obj):
    """Compute mesh world-bbox center + size for camera placement. Returns
    (center Vector, max_extent float) so we can position the camera at a
    consistent focal distance regardless of mesh scale."""
    corners = [mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box]
    xs = [v.x for v in corners]
    ys = [v.y for v in corners]
    zs = [v.z for v in corners]
    center = Vector((
        (min(xs) + max(xs)) / 2,
        (min(ys) + max(ys)) / 2,
        (min(zs) + max(zs)) / 2,
    ))
    extents = Vector((max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))
    max_extent = max(extents)
    return center, max_extent, extents


def setup_lighting():
    """Neutral 3-point lighting: hemisphere for ambient, warm key, cool fill.
    Matches the browser viewer's lighting so rendered + in-browser views
    read similarly."""
    # World background neutral gray so dark cats read against a mid-tone.
    world = bpy.data.worlds.new("RenderWorld") if not bpy.data.worlds else bpy.data.worlds[0]
    bpy.context.scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get('Background')
    if bg:
        bg.inputs[0].default_value = (0.22, 0.23, 0.26, 1.0)
        bg.inputs[1].default_value = 1.0

    # Key light (above front-right)
    bpy.ops.object.light_add(type='SUN', location=(4, -4, 6))
    key = bpy.context.active_object
    key.data.energy = 3.5
    key.rotation_euler = (math.radians(55), 0, math.radians(45))

    # Fill light (opposite side, dimmer + bluer)
    bpy.ops.object.light_add(type='SUN', location=(-3, 3, 4))
    fill = bpy.context.active_object
    fill.data.energy = 1.3
    fill.data.color = (0.75, 0.85, 1.0)
    fill.rotation_euler = (math.radians(45), 0, math.radians(-135))


def setup_camera(center, max_extent, view_angle):
    """Place a camera at `view_angle` around the cat, looking at `center`.
    view_angle is one of: 'front', 'side', '3q', 'top'.

    Focal distance = max_extent * 2.5 is empirically "cat fills the frame at
    35mm FOV without clipping the tail"."""
    dist = max_extent * 2.5
    vertical = center.z  # aim camera roughly at mid-cat height

    positions = {
        'front': Vector((0, -dist, vertical)),
        'side':  Vector((dist, 0, vertical)),
        '3q':    Vector((dist * 0.7, -dist * 0.7, vertical + max_extent * 0.3)),
        'top':   Vector((0, 0, vertical + dist)),
    }
    pos = positions.get(view_angle, positions['3q']) + Vector((center.x, center.y, 0))

    bpy.ops.object.camera_add(location=pos)
    cam = bpy.context.active_object
    # Point at center
    direction = (center - pos).normalized()
    # Blender's camera default points -Z; compute rotation to face `direction`
    cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = direction.to_track_quat('-Z', 'Y')
    cam.data.lens = 35
    bpy.context.scene.camera = cam
    return cam


# ---------------------------------------------------------------------------
# Weight paint visualization
# ---------------------------------------------------------------------------

def apply_weight_viz(mesh_obj):
    """Replace the mesh's material with a vertex-color material where each
    vertex is colored by its dominant bone (highest-weight vertex group).
    Vertices with zero total weight become black - immediately visible as
    "rigging failed here" regions."""
    if not mesh_obj.vertex_groups:
        print("[render_cat] weight viz skipped - mesh has no vertex groups")
        return

    # Build (vertex_index -> dominant bone index) using groups sorted by index.
    group_index_by_name = {g.name: i for i, g in enumerate(mesh_obj.vertex_groups)}
    n_groups = len(mesh_obj.vertex_groups)
    # Distinct hues per bone via golden-ratio hashing. Matches the browser's
    # weight-viz palette so the two tools agree.
    golden = 0.61803398875
    hues = [((i * golden) % 1.0) for i in range(n_groups)]
    rgb_for_group = [colorsys.hls_to_rgb(h, 0.55, 0.65) for h in hues]

    mesh = mesh_obj.data

    # Create a vertex color layer named "bone_weight_color"
    if not mesh.color_attributes:
        mesh.color_attributes.new(name="bone_weight_color", type='FLOAT_COLOR', domain='POINT')
    color_layer = mesh.color_attributes[0]

    for v in mesh.vertices:
        best_weight = 0.0
        best_group = -1
        for g in v.groups:
            if g.weight > best_weight:
                best_weight = g.weight
                best_group = g.group
        if best_group < 0 or best_weight <= 0:
            color_layer.data[v.index].color = (0.0, 0.0, 0.0, 1.0)
        else:
            r, g, b = rgb_for_group[best_group]
            color_layer.data[v.index].color = (r, g, b, 1.0)

    # Build a simple material that reads the vertex colors.
    mat = bpy.data.materials.new(name="WeightViz")
    mat.use_nodes = True
    nt = mat.node_tree
    # Clear default nodes
    for node in list(nt.nodes):
        nt.nodes.remove(node)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    vcol = nt.nodes.new('ShaderNodeVertexColor')
    vcol.layer_name = color_layer.name
    bsdf.inputs['Roughness'].default_value = 1.0  # matte so hue reads clearly
    nt.links.new(vcol.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    mesh_obj.data.materials.clear()
    mesh_obj.data.materials.append(mat)


# ---------------------------------------------------------------------------
# Skeleton overlay
# ---------------------------------------------------------------------------

def apply_skeleton_visible(arm_obj):
    """Make the armature show as visible bones in the render. Blender by
    default doesn't render bones; we switch the armature to "In Front" and
    bone display to 'STICK' + emissive material so they show up as bright
    colored lines overlaid on the mesh.

    The technique: for each bone, create a thin cylinder along the bone's
    axis. That way the bones actually show up in the rendered image (the
    armature itself doesn't have renderable geometry).
    """
    if arm_obj is None:
        print("[render_cat] skeleton overlay skipped - no armature")
        return

    # Emissive material so bones read against any mesh color.
    mat = bpy.data.materials.new(name="BoneOverlay")
    mat.use_nodes = True
    nt = mat.node_tree
    for node in list(nt.nodes): nt.nodes.remove(node)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    emit = nt.nodes.new('ShaderNodeEmission')
    emit.inputs['Color'].default_value = (0.0, 1.0, 0.35, 1.0)  # bright green
    emit.inputs['Strength'].default_value = 8.0
    nt.links.new(emit.outputs['Emission'], out.inputs['Surface'])

    # For each bone, spawn a thin cylinder from head to tail in world space.
    for bone in arm_obj.data.bones:
        head_world = arm_obj.matrix_world @ bone.head_local
        tail_world = arm_obj.matrix_world @ bone.tail_local
        mid = (head_world + tail_world) / 2
        axis = tail_world - head_world
        length = axis.length
        if length < 0.001:
            continue

        bpy.ops.mesh.primitive_cylinder_add(
            vertices=8, radius=max(0.004, length * 0.03), depth=length, location=mid
        )
        cyl = bpy.context.active_object
        # Rotate cylinder's default Z axis to align with (tail - head).
        cyl.rotation_mode = 'QUATERNION'
        cyl.rotation_quaternion = axis.to_track_quat('Z', 'Y')
        cyl.data.materials.append(mat)


# ---------------------------------------------------------------------------
# Render driver
# ---------------------------------------------------------------------------

def configure_render():
    """Fast EEVEE Next settings (Blender 4.4 default). 512x512 at ~4-6s per
    angle keeps a multi-view pass under a minute. Blender 4.4 only exposes
    BLENDER_EEVEE_NEXT, BLENDER_WORKBENCH, and CYCLES as render engines -
    the old BLENDER_EEVEE enum was removed."""
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE_NEXT'
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'
    scene.display_settings.display_device = 'sRGB'


def render_to(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print(f"[render_cat] wrote {path}")


def render_all_views(center, max_extent, base_name, mode_suffix, opts):
    """Render the 4 canonical angles with the current scene state. mode_suffix
    is appended to the filename (e.g. "_weights", "_skeleton") so diagnostic
    comparisons file cleanly."""
    views = [
        ('front', 'front'),
        ('side',  'side'),
        ('3q',    '3quarter'),
        ('top',   'top'),
    ]
    for angle, label in views[:opts["views"]]:
        # Remove previous camera(s) so we don't accumulate them across views.
        for obj in [o for o in bpy.data.objects if o.type == 'CAMERA']:
            bpy.data.objects.remove(obj, do_unlink=True)
        setup_camera(center, max_extent, angle)
        out_path = os.path.join(
            opts["output_dir"],
            f"{base_name}_{label}{mode_suffix}.png"
        )
        render_to(out_path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    opts = parse_args()
    base_name = os.path.splitext(os.path.basename(opts["input"]))[0]

    # Mode 1: plain mesh render.
    reset_scene()
    import_glb(opts["input"])
    mesh_obj, arm_obj = find_mesh_and_armature()
    center, max_extent, extents = frame_object(mesh_obj)
    print(f"[render_cat] bbox extents: {extents.x:.2f} x {extents.y:.2f} x {extents.z:.2f}")
    print(f"[render_cat] armature: {'yes (' + str(len(arm_obj.data.bones)) + ' bones)' if arm_obj else 'none'}")
    setup_lighting()
    configure_render()
    render_all_views(center, max_extent, base_name, '', opts)

    # Mode 2: skeleton overlay.
    if opts["skeleton"] and arm_obj is not None:
        reset_scene()
        import_glb(opts["input"])
        mesh_obj, arm_obj = find_mesh_and_armature()
        center, max_extent, _ = frame_object(mesh_obj)
        setup_lighting()
        apply_skeleton_visible(arm_obj)
        configure_render()
        render_all_views(center, max_extent, base_name, '_skeleton', opts)

    # Mode 3: weight-paint visualization.
    if opts["weights"]:
        reset_scene()
        import_glb(opts["input"])
        mesh_obj, arm_obj = find_mesh_and_armature()
        center, max_extent, _ = frame_object(mesh_obj)
        setup_lighting()
        apply_weight_viz(mesh_obj)
        configure_render()
        render_all_views(center, max_extent, base_name, '_weights', opts)

    print(f"[render_cat] done: {opts['input']}")


if __name__ == "__main__":
    main()
