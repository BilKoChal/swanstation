// Pre-baked Vulkan source template for
// GPU_HW_ShaderGen::GenerateBatchVertexShader.
//
// Batch VS is the first of the two batch shaders, and its state space
// has THREE structurally distinct axes that SPIR-V decorations cannot
// be specialised across:
//
//   - Vertex attribute layout. 2 input attributes (untextured), 4
//     (textured without UV limits) or 5 (textured with UV limits).
//     The 'in <type> <name>' declarations are part of the SPIR-V
//     module interface, not a spec-constant-controlled knob.
//
//   - Output interpolation qualifier. The fragment shader pairs with
//     these and the qualifiers compile to OpMemberDecorate Sample /
//     Centroid in the output VertexData block. There are three
//     mutually exclusive states (no qualifier, centroid, sample),
//     selected per session by UsingMSAA() and UsingPerSampleShading().
//
//   - Color output perspective. The 'noperspective' qualifier on
//     v_col0 compiles to OpMemberDecorate NoPerspective, again not
//     spec-constant-controllable. Two states, selected per session by
//     m_disable_color_perspective.
//
// Total structural variant count: 3 (attribute layouts) x 3
// (interpolation) x 2 (perspective) = 18. Body-level knobs that do
// not affect decorations (PGXP_DEPTH, RESOLUTION_SCALE) are handled
// as specialisation constants on every blob.
//
// This file is the SINGLE GLSL source. The regen tool compiles it
// eighteen times with different -D combinations to produce the
// matching .inc blobs; the manifest of variants lives in
// tools/regen_vulkan_spirv.py. The C++ side picks one of the 18 at
// pipeline-create time via the helper in embedded_shaders.cpp.
//
// Spec constants (every blob):
//   constant_id = 0  RESOLUTION_SCALE (uint, common-knob convention).
//                    Drives the 1x-resolution vertex-offset gate, the
//                    upscaled texcoord scaling, and the texpage
//                    base-coordinate scaling.
//   constant_id = 3  PGXP_DEPTH       (bool, common-knob convention).
//                    Selects between mask-bit Z (a_pos.z) and the
//                    PGXP-replayed perspective-correct depth
//                    (a_pos.w).
//
// Bindings: vertex inputs only - no descriptor sets, no push
// constants. The pipeline layout is m_batch_pipeline_layout which
// declares one UBO at set=0 binding=0 for the FS; the VS does not
// read it.

#version 450 core

// ---- Specialisation constants (body-level knobs) -------------------
layout(constant_id = 0) const uint RESOLUTION_SCALE = 1u;
layout(constant_id = 3) const bool PGXP_DEPTH       = false;

// ---- Interpolation qualifier macros --------------------------------
#if defined(INTERP_SAMPLE)
#  define INTERP sample
#elif defined(INTERP_CENTROID)
#  define INTERP centroid
#else
#  define INTERP
#endif

#if defined(NOPERSP)
#  define COLOR_INTERP noperspective INTERP
#else
#  define COLOR_INTERP INTERP
#endif

// ---- Vertex inputs (attribute layout axis) -------------------------
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_col0;
#if defined(TEXTURED)
layout(location = 2) in uint a_texcoord;
layout(location = 3) in uint a_texpage;
#  if defined(UV_LIMITS)
layout(location = 4) in vec4 a_uv_limits;
#  endif
#endif

// ---- Vertex outputs (interpolation + perspective axis) -------------
layout(location = 0) out VertexData {
  COLOR_INTERP vec4 v_col0;
#if defined(TEXTURED)
  INTERP vec2 v_tex0;
  flat uvec4 v_texpage;
#  if defined(UV_LIMITS)
  flat vec4 v_uv_limits;
#  endif
#endif
};

// --------------------------------------------------------------------

void main()
{
  // 1x-resolution sub-pixel offset matches the shadergen original;
  // the >1x case has the offset pre-applied in CPU-side vertex prep,
  // so the shader uses 0 there.
  float vertex_offset = (RESOLUTION_SCALE == 1u) ? 0.5 : 0.0;

  // PSX VRAM addressing is 0..1023 horizontally, mapped to NDC -1..+1.
  float pos_x = ((a_pos.x + vertex_offset) /  512.0) - 1.0;
  float pos_y = ((a_pos.y + vertex_offset) / -256.0) + 1.0;

  // Vulkan flips Y vs GL/D3D; shadergen does this unconditionally
  // under #if API_VULKAN. We are Vulkan-only here so it is also
  // unconditional.
  pos_y = -pos_y;

  // Depth source selection. With PGXP enabled and the depth-buffer
  // option on, the PGXP path replays a perspective-correct depth in
  // a_pos.w and the mask-bit Z slot (a_pos.z) is ignored. Otherwise
  // use the conventional mask-bit Z.
  float pos_z = PGXP_DEPTH ? a_pos.w : a_pos.z;
  float pos_w = a_pos.w;

  gl_Position = vec4(pos_x * pos_w, pos_y * pos_w, pos_z * pos_w, pos_w);

  v_col0 = a_col0;

#if defined(TEXTURED)
  // Texture coordinates are packed into a single uint per vertex
  // (lower 16 bits = U, upper 16 bits = V) and scaled by the upscale
  // factor to address into the upscaled VRAM atlas.
  v_tex0 = vec2(float((a_texcoord & 0xFFFFu) * RESOLUTION_SCALE),
                float((a_texcoord >> 16)     * RESOLUTION_SCALE));

  // a_texpage is similarly packed:
  //   bits  0..3   page base X  (in 64-texel native units)
  //   bit   4      page base Y  (256-texel native unit, 0 or 1)
  //   bits 16..21  CLUT X       (in 16-texel native units)
  //   bits 22..30  CLUT Y       (native rows)
  v_texpage.x = ( a_texpage        & 15u ) * 64u  * RESOLUTION_SCALE;
  v_texpage.y = ((a_texpage >>  4) &  1u ) * 256u * RESOLUTION_SCALE;
  v_texpage.z = ((a_texpage >> 16) & 63u ) * 16u  * RESOLUTION_SCALE;
  v_texpage.w = ((a_texpage >> 22) & 511u)        * RESOLUTION_SCALE;

#  if defined(UV_LIMITS)
  // UV limits are sent normalised; expand to 0..255 PSX-native here.
  v_uv_limits = a_uv_limits * vec4(255.0, 255.0, 255.0, 255.0);
#  endif
#endif
}
