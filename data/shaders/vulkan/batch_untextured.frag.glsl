// Pre-baked Vulkan source template for the UNTEXTURED slice of
// GPU_HW_ShaderGen::GenerateBatchFragmentShader (texture_mode ==
// GPUTextureMode::Disabled). The textured slice is several times
// larger and will land in subsequent patches, one or two texture
// filter values at a time; this template only emits SPIR-V valid for
// pipelines whose lookup_mode is Disabled (the renderer's untextured
// path - solid-shaded polygons, no VRAM sampling).
//
// Structural axes for the untextured batch FS:
//
//   - Input interpolation qualifier on v_col0. None / centroid /
//     sample, matching the batch VS pair this FS is bound with. (3
//     variants.)
//   - Color input perspective. Standard / noperspective. (2 variants.)
//   - Dual-source color output. With dual-source-blend hardware the
//     transparency=TransparentAndOpaque or =OnlyTransparent render
//     modes also write a second colour value at location 0 index 1
//     that drives SRC1_COLOR / SRC1_ALPHA in the blend equation. The
//     output declaration count is structural - the pipeline blend
//     state references location-0-index-1 or it does not. (2 variants.)
//   - PGXP depth output. With PGXP Depth Buffer enabled the FS does
//     not declare gl_FragDepth (rasterizer-interpolated depth from the
//     VS-replayed a_pos.w is used directly); without it, the FS writes
//     gl_FragDepth = oalpha * v_pos.z. Splitting this structurally
//     preserves early-Z on the PGXP-enabled path. (2 variants.)
//
// Total untextured structural blobs: 3 x 2 x 2 x 2 = 24.
//
// Per-call and per-session knobs that DO NOT affect SPIR-V structure
// collapse into specialisation constants on every blob:
//
//   constant_id =   0  RESOLUTION_SCALE             (uint)
//   constant_id = 100  TRANSPARENCY                 (bool)
//   constant_id = 101  TRANSPARENCY_ONLY_OPAQUE     (bool)
//   constant_id = 102  TRANSPARENCY_ONLY_TRANSPARENT(bool)
//   constant_id = 103  DITHERING                    (bool)
//   constant_id = 104  INTERLACING                  (bool)
//   constant_id = 105  DITHERING_SCALED             (bool)
//   constant_id = 106  TRUE_COLOR                   (bool)
//
// The TRANSPARENCY trio encodes the per-call BatchRenderMode enum
// (which has four values: TransparencyDisabled, TransparentAndOpaque,
// OnlyOpaque, OnlyTransparent). Only valid combinations are passed by
// the C++ caller, see embedded_shaders.cpp / gpu_hw_vulkan.cpp for the
// mapping.
//
// Bindings: pipeline layout m_batch_pipeline_layout. The dynamic UBO
// at set=0 binding=0 supplies the BatchUBOData fields read here
// (u_src_alpha_factor, u_dst_alpha_factor, u_interlaced_displayed_field,
// u_set_mask_while_drawing). u_texture_window_* are present in the
// UBO but unused in the untextured path.

#version 450 core

// ---- Specialisation constants --------------------------------------
layout(constant_id =   0) const uint RESOLUTION_SCALE              = 1u;
layout(constant_id = 100) const bool TRANSPARENCY                  = false;
layout(constant_id = 101) const bool TRANSPARENCY_ONLY_OPAQUE      = false;
layout(constant_id = 102) const bool TRANSPARENCY_ONLY_TRANSPARENT = false;
layout(constant_id = 103) const bool DITHERING                     = false;
layout(constant_id = 104) const bool INTERLACING                   = false;
layout(constant_id = 105) const bool DITHERING_SCALED              = false;
layout(constant_id = 106) const bool TRUE_COLOR                    = false;

// ---- Interpolation qualifier macros (structural) -------------------
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

// ---- Batch UBO -----------------------------------------------------
// Layout matches WriteBatchUniformBuffer in the shadergen. Field order
// is significant: the C++ side packs in this exact order.
layout(std140, set = 0, binding = 0) uniform BatchUBOData {
  uvec2 u_texture_window_and;
  uvec2 u_texture_window_or;
  float u_src_alpha_factor;
  float u_dst_alpha_factor;
  uint  u_interlaced_displayed_field;
  bool  u_set_mask_while_drawing;
};

// ---- Inputs from the batch VS --------------------------------------
layout(location = 0) in VertexData {
  COLOR_INTERP vec4 v_col0;
};

// ---- Outputs (DUAL_SOURCE axis) ------------------------------------
layout(location = 0, index = 0) out vec4 o_col0;
#if defined(DUAL_SOURCE)
layout(location = 0, index = 1) out vec4 o_col1;
#endif

// PGXP_DEPTH axis: when PGXP_DEPTH is set the FS does NOT declare
// gl_FragDepth (rasterizer depth from the VS-replayed a_pos.w is used
// directly, and early-Z stays available). When PGXP_DEPTH is unset we
// fall through to gl_FragDepth as in the shadergen original. gl_FragDepth
// is a built-in so there is no explicit declaration either way - we
// simply gate the write below.

// ---- Dithering matrix (CONSTANT in shadergen) ----------------------
// Same 4x4 Bayer values as DITHER_MATRIX in core/types.h.
const int s_dither_values[16] = int[16](
  -4,  0, -3,  1,
   2, -2,  3, -1,
  -3,  1, -4,  0,
   3, -1,  2, -2
);

uvec3 ApplyDithering(uvec2 coord, uvec3 icol)
{
  uvec2 fc = DITHERING_SCALED
             ? (coord                     & uvec2(3u, 3u))
             : ((coord / RESOLUTION_SCALE) & uvec2(3u, 3u));
  int offset = s_dither_values[fc.y * 4u + fc.x];

  if (TRUE_COLOR)
    return uvec3(clamp(ivec3(icol) + ivec3(offset, offset, offset), 0, 255));
  else
    return uvec3(clamp((ivec3(icol) + ivec3(offset, offset, offset)) >> 3, 0, 31));
}

void main()
{
  uvec3 vertcol = uvec3(v_col0.rgb * vec3(255.0, 255.0, 255.0));

  // INTERLACING discard (untextured can still hit a field-strobed draw).
  // fixYCoord on Vulkan is the identity function so we use gl_FragCoord.y
  // directly.
  if (INTERLACING)
  {
    if ((uint(gl_FragCoord.y) & 1u) == u_interlaced_displayed_field)
      discard;
  }

  // Untextured fragment: all pixels are semitransparent by convention,
  // colour comes from the interpolated vertex colour, ialpha is unity.
  uvec3 icolor = vertcol;
  float ialpha = 1.0;

  if (DITHERING)
  {
    icolor = ApplyDithering(uvec2(gl_FragCoord.xy), icolor);
  }
  else
  {
    if (!TRUE_COLOR)
      icolor >>= 3;
  }

  // Mask bit: untextured polygons clear the mask bit unless the
  // 'set mask while drawing' command bit is set.
  float oalpha = float(u_set_mask_while_drawing);

  // Premultiply alpha so the colour output can carry the mask in alpha.
  float premultiply_alpha = TRANSPARENCY ? (ialpha * u_src_alpha_factor) : ialpha;

  vec3 color = TRUE_COLOR
               ? ((vec3(icolor) * premultiply_alpha) / vec3(255.0, 255.0, 255.0))
               : (floor(vec3(icolor) * premultiply_alpha) /  vec3(31.0,  31.0,  31.0));

  // Output. With dual-source blending o_col1 carries the destination
  // alpha factor; without, only o_col0 is written. The matching
  // pipeline blend state references SRC1_* or it does not, set by
  // GetBatchPipeline on the C++ side.
  o_col0 = vec4(color, oalpha);
#if defined(DUAL_SOURCE)
  if (TRANSPARENCY)
    o_col1 = vec4(0.0, 0.0, 0.0, u_dst_alpha_factor / ialpha);
  else
    o_col1 = vec4(0.0, 0.0, 0.0, 1.0 - ialpha);
#endif

  // Untextured + transparency-only-opaque should never produce
  // visible pixels - the original discards them after the colour
  // output. This branch should be unreachable for untextured (the
  // shadergen documents it as "We shouldn't be rendering opaque
  // geometry only when untextured"), but the original code did not
  // assert this so we mirror its silence rather than redirect.

#if !defined(PGXP_DEPTH)
  gl_FragDepth = oalpha * gl_FragCoord.z;
#endif
}
