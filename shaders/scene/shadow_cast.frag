#version 450

// Depth-only shadow-caster fragment shader.
//
// The shadow render pass has ZERO color attachments — only a depth attachment
// the rasteriser fills automatically — so this stage emits nothing. A pipeline
// that rasterises with no fragment shader is legal in Vulkan for a depth-only
// pass, but supplying an explicit empty stage keeps every driver and every
// validation-layer configuration happy (some flag the frag-less pipeline), and
// mirrors the precedent set by the engine's existing shadow_depth.frag. If a
// future iteration adds alpha-cutout foliage, the discard test would live here.
void main() {}
