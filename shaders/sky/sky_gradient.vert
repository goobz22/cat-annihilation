#version 450

// Sky-gradient vertex shader.
//
// 2026-04-25 SHIP-THE-CAT iter (sky gradient follow-up to entity fog):
// emits a single oversized fullscreen triangle from gl_VertexIndex with NO
// vertex buffer or vertex input bindings. The classic three-vertex
// "fullscreen triangle" technique (Bilodeau 2014, popularised in Frostbite
// docs) produces a triangle with corners at NDC (-1,-3), (3,1), (-1,1).
// Rasterization clips that to the standard [-1, 1] square and avoids the
// diagonal seam that a fullscreen quad-as-two-triangles would expose. One
// triangle = no diagonal = no fragments processed twice = strictly
// cheaper than a quad on every GPU.
//
// We deliberately don't bind any vertex buffer (vertexBindingDescriptionCount
// = 0 in the C++ pipeline setup) — this triangle is generated entirely from
// gl_VertexIndex, so a renderer can `vkCmdDraw(cmd, 3, 1, 0, 0)` with
// nothing bound and get a valid sky background.
//
// The fragment shader needs to know its vertical position in the frame to
// blend zenith → horizon, so we forward `uv.y` (0 at top, 1 at bottom)
// rather than re-deriving it from gl_FragCoord. This keeps the gradient
// independent of the Vulkan viewport flip convention — if a future change
// flips Y in the viewport state, only THIS shader knows about it instead
// of leaking that detail into sky_gradient.frag.

layout(location = 0) out vec2 outUv;

void main() {
    // Map gl_VertexIndex -> oversized triangle corners.
    //   0 -> (-1, -3)  (bottom-left, off-screen)
    //   1 -> ( 3,  1)  (top-right, off-screen)
    //   2 -> (-1,  1)  (top-left, on-screen)
    //
    // The uint bit-trick collapses to:
    //   ((idx << 1) & 2)  is 0,2,0  for idx=0,1,2
    //   ( idx       & 2)  is 0,0,2  for idx=0,1,2
    // Subtract 1 to remap [0,2] -> [-1,1] for x; for y the (-3,1,1)
    // pattern needs (idx & 2) * 2 - 1 which is (-1,-1,3) — flip and you
    // get the (3, 1, 1) we want for the y component, which we negate
    // below to land at (-3, 1, 1) in clip Y. That's the slightly
    // different (-1,-3) (3,1) (-1,1) pattern.
    //
    // To keep this readable we use the explicit-table form rather than
    // the bit-trick — three triangle corners, computed once per vertex,
    // is not a hot path and clarity wins over micro-optimisation.
    vec2 corners[3] = vec2[3](
        vec2(-1.0, -3.0),
        vec2( 3.0,  1.0),
        vec2(-1.0,  1.0)
    );
    vec2 corner = corners[gl_VertexIndex];

    // Map clip-space [-1, 1] to UV [0, 1] where
    //   uv.y = 0  -> top of visible frame    -> ZENITH
    //   uv.y = 1  -> bottom of visible frame -> HORIZON
    //
    // Vulkan NDC by default has +Y pointing DOWN in screen space (the spec
    // flipped this from OpenGL — Vulkan's left-handed clip space puts y=-1
    // at the TOP of the framebuffer, y=+1 at the BOTTOM). This engine's
    // ScenePass viewport keeps the Vulkan default (no negative-height
    // viewport flip — see ScenePass::Execute viewport setup), so:
    //   * NDC y = -1 → top of screen    → uv.y must be 0 (ZENITH)
    //   * NDC y = +1 → bottom of screen → uv.y must be 1 (HORIZON)
    // The simple linear remap `(y + 1) * 0.5` produces exactly that.
    //
    // The earlier `(1 - y) * 0.5` form looked correct against an OpenGL
    // mental model but rendered the gradient UPSIDE DOWN in Vulkan
    // (zenith colour appeared at the bottom of the screen, hidden by
    // terrain; horizon colour at the top). Empirically caught by frame-
    // dump column sampling on iteration 2026-04-25T2017Z — top-of-frame
    // pixels read as horizon-colour-plus-sun-halo, not zenith. The fix
    // is a one-character flip but the WHY is worth this comment so a
    // future reviewer touching this file doesn't reintroduce the OpenGL
    // form.
    //
    // Triangle corner UVs after this remap:
    //   corner (-1, -3) -> uv (0,-1)  [off-screen above zenith — clamped on read]
    //   corner ( 3,  1) -> uv (2, 1)  [off-screen past right horizon]
    //   corner (-1,  1) -> uv (0, 1)  [bottom-left horizon]
    // Rasterization barycentric-interpolates these across the visible
    // triangle interior so every on-screen pixel reads a uv.y in [0, 1]
    // mapping correctly top-zenith → bottom-horizon.
    vec2 uv = vec2((corner.x + 1.0) * 0.5,
                   (corner.y + 1.0) * 0.5);
    outUv = uv;

    // Push the triangle to the far plane (z = 1.0 in Vulkan NDC) so it
    // depth-test-fails against any geometry we draw afterwards. The
    // pipeline runs with depthWrite = FALSE so the sky never PUSHES
    // depth, but having z = 1.0 in case some future change re-enables
    // the test means terrain at finite distance still wins (terrain
    // depth values are < 1.0 because the depth-clear sets 1.0 and
    // closer objects produce smaller z/w in the perspective division).
    gl_Position = vec4(corner, 1.0, 1.0);
}
