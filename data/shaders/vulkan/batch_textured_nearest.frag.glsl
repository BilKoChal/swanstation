// Pre-baked Vulkan source template for the TEXTURED + NEAREST-FILTER
// slice of GPU_HW_ShaderGen::GenerateBatchFragmentShader (texture_mode
// != GPUTextureMode::Disabled AND m_texture_filter ==
// GPUTextureFilter::Nearest). Subsequent patches add equivalent
// templates for Bilinear / BilinearBinAlpha, JINC2 / JINC2BinAlpha,
// and xBR / xBRBinAlpha; the remaining "is m_texture_filter Nearest"
// gate in GetBatchFragmentShader on the C++ side picks this template
// for nearest-only sessions.
//
// Five SPIR-V-structural axes for this template:
//
//   - Input interpolation qualifier (none / centroid / sample). 3.
//   - Color input perspective (standard / noperspective). 2.
//   - Dual-source output (1 vs 2 outputs). 2.
//   - PGXP depth output (writes gl_FragDepth or omits it). 2.
//   - UV limits (without / with v_uv_limits flat input). 2.
//
// 3 x 2 x 2 x 2 x 2 = 48 blobs.
//
// Per-call specialisation constants (collapse onto each blob):
//
//   id =   0  RESOLUTION_SCALE              (uint, per-session)
//   id = 100  TRANSPARENCY                  (bool)
//   id = 101  TRANSPARENCY_ONLY_OPAQUE      (bool)
//   id = 102  TRANSPARENCY_ONLY_TRANSPARENT (bool)
//   id = 103  DITHERING                     (bool)
//   id = 104  INTERLACING                   (bool)
//   id = 105  DITHERING_SCALED              (bool, per-session)
//   id = 106  TRUE_COLOR                    (bool, per-session)
//   id = 107  PALETTE_4_BIT                 (bool, derived from
//                                            texture_mode actual mode)
//   id = 108  PALETTE_8_BIT                 (bool, derived from
//                                            texture_mode actual mode)
//   id = 109  RAW_TEXTURE                   (bool, RawTextureBit set
//                                            in texture_mode)
//
// PALETTE is derived inside the shader as PALETTE_4_BIT ||
// PALETTE_8_BIT; only at most one of the two is true for any draw.
// The texture_mode encoding is described in src/core/gpu_types.h.
//
// Bindings: pipeline layout m_batch_pipeline_layout. UBO at set=0
// binding=0 supplies BatchUBOData (texture window + transparency +
// interlacing). Combined-image-sampler at set=0 binding=1 is the
// upscaled m_vram_read_texture, single-sample (texture sampling
// always reads the non-multisampled shadow VRAM regardless of
// m_multisamples).

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
layout(constant_id = 107) const bool PALETTE_4_BIT                 = false;
layout(constant_id = 108) const bool PALETTE_8_BIT                 = false;
layout(constant_id = 109) const bool RAW_TEXTURE                   = false;

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

// ---- Batch UBO -----------------------------------------------------
layout(std140, set = 0, binding = 0) uniform BatchUBOData {
  uvec2 u_texture_window_and;
  uvec2 u_texture_window_or;
  float u_src_alpha_factor;
  float u_dst_alpha_factor;
  uint  u_interlaced_displayed_field;
  bool  u_set_mask_while_drawing;
};

// ---- VRAM atlas sampler --------------------------------------------
layout(set = 0, binding = 1) uniform sampler2D samp0;

// ---- Inputs from the batch VS --------------------------------------
layout(location = 0) in VertexData {
  COLOR_INTERP vec4 v_col0;
  INTERP       vec2 v_tex0;
  flat         uvec4 v_texpage;
#if defined(UV_LIMITS)
  flat         vec4 v_uv_limits;
#endif
};

// ---- Outputs -------------------------------------------------------
layout(location = 0, index = 0) out vec4 o_col0;
#if defined(DUAL_SOURCE)
layout(location = 0, index = 1) out vec4 o_col1;
#endif

// ---- Dithering matrix ----------------------------------------------
const int s_dither_values[16] = int[16](
  -4,  0, -3,  1,
   2, -2,  3, -1,
  -3,  1, -4,  0,
   3, -1,  2, -2
);

uvec3 ApplyDithering(uvec2 coord, uvec3 icol)
{
  uvec2 fc = DITHERING_SCALED
             ? (coord                      & uvec2(3u, 3u))
             : ((coord / RESOLUTION_SCALE) & uvec2(3u, 3u));
  int offset = s_dither_values[fc.y * 4u + fc.x];
  if (TRUE_COLOR)
    return uvec3(clamp(ivec3(icol) + ivec3(offset, offset, offset), 0, 255));
  else
    return uvec3(clamp((ivec3(icol) + ivec3(offset, offset, offset)) >> 3, 0, 31));
}

// ---- Texture-window helpers ----------------------------------------
uvec2 ApplyTextureWindow(uvec2 coords)
{
  uint x = (coords.x & u_texture_window_and.x) | u_texture_window_or.x;
  uint y = (coords.y & u_texture_window_and.y) | u_texture_window_or.y;
  return uvec2(x, y);
}

uvec2 ApplyUpscaledTextureWindow(uvec2 coords)
{
  uvec2 native_coords = coords / RESOLUTION_SCALE;
  uvec2 coords_offset = coords % RESOLUTION_SCALE;
  return (ApplyTextureWindow(native_coords) * RESOLUTION_SCALE) + coords_offset;
}

uvec2 FloatToIntegerCoords(vec2 coords)
{
  // 1x-resolution-scale: round to nearest texel (the +0.5 vertex offset
  // from the batch VS aligned us on a texel centre). >1x: floor, since
  // the vertex offset is pre-applied CPU-side and we want the bottom-
  // left of the upscaled cell.
  return uvec2((RESOLUTION_SCALE == 1u) ? roundEven(coords) : floor(coords));
}

uint RGBA8ToRGBA5551(vec4 v)
{
  uint r = uint(roundEven(v.r * 31.0));
  uint g = uint(roundEven(v.g * 31.0));
  uint b = uint(roundEven(v.b * 31.0));
  uint a = (v.a != 0.0) ? 1u : 0u;
  return r | (g << 5) | (b << 10) | (a << 15);
}

// ---- VRAM atlas sample ---------------------------------------------
vec4 SampleFromVRAM(uvec4 texpage, vec2 coords)
{
  const vec2  rcp_vram_size = vec2(1.0) / vec2(uvec2(1024u, 512u) * RESOLUTION_SCALE);

  bool palette = PALETTE_4_BIT || PALETTE_8_BIT;

  if (palette)
  {
    // 4-bit and 8-bit paletted: address is 4 or 2 native-texel-wide
    // packed cells in the VRAM atlas; the per-subpixel index pulls the
    // 4- or 8-bit palette entry from that cell.
    uvec2 icoord = ApplyTextureWindow(FloatToIntegerCoords(coords));
    uvec2 index_coord = icoord;
    if (PALETTE_4_BIT)
      index_coord.x /= 4u;
    else  // PALETTE_8_BIT
      index_coord.x /= 2u;

    uvec2 vicoord = uvec2(texpage.x + index_coord.x * RESOLUTION_SCALE,
                          texpage.y + index_coord.y * RESOLUTION_SCALE);

    vec4 texel       = texture(samp0, vec2(vicoord) * rcp_vram_size);
    uint vram_value  = RGBA8ToRGBA5551(texel);

    uint palette_index;
    if (PALETTE_4_BIT)
    {
      uint subpixel = icoord.x & 3u;
      palette_index = (vram_value >> (subpixel * 4u)) & 0x0Fu;
    }
    else  // PALETTE_8_BIT
    {
      uint subpixel = icoord.x & 1u;
      palette_index = (vram_value >> (subpixel * 8u)) & 0xFFu;
    }

    uvec2 palette_icoord = uvec2(texpage.z + (palette_index * RESOLUTION_SCALE),
                                 texpage.w);
    return texture(samp0, vec2(palette_icoord) * rcp_vram_size);
  }
  else
  {
    // Direct 16bpp - render-to-texture effects, upscaled VRAM coords.
    uvec2 icoord = ApplyUpscaledTextureWindow(FloatToIntegerCoords(coords));
    uvec2 direct_icoord = uvec2(texpage.x + icoord.x, texpage.y + icoord.y);
    return texture(samp0, vec2(direct_icoord) * rcp_vram_size);
  }
}

// --------------------------------------------------------------------

void main()
{
  uvec3 vertcol = uvec3(v_col0.rgb * vec3(255.0, 255.0, 255.0));

  // INTERLACING field discard (fixYCoord is identity on Vulkan).
  if (INTERLACING)
  {
    if ((uint(gl_FragCoord.y) & 1u) == u_interlaced_displayed_field)
      discard;
  }

  // Texture coords. Palette textures index the VRAM atlas with native-
  // resolution coords; direct-16bpp textures use upscaled coords.
  bool palette = PALETTE_4_BIT || PALETTE_8_BIT;
  vec2 coords = v_tex0;
  if (palette)
    coords /= float(RESOLUTION_SCALE);

#if defined(UV_LIMITS)
  vec4 uv_limits = v_uv_limits;
  if (!palette)
  {
    // Direct-16bpp: extend the UV range to all upscaled pixels so 1-
    // pixel-high polygon-based framebuffer effects (e.g. MegaMan Legends
    // 2 haze) are not downsampled.
    uv_limits = uv_limits * float(RESOLUTION_SCALE);
    uv_limits.zw += float(RESOLUTION_SCALE - 1u);
  }
  vec4 texcol = SampleFromVRAM(v_texpage, clamp(coords, uv_limits.xy, uv_limits.zw));
#else
  vec4 texcol = SampleFromVRAM(v_texpage, coords);
#endif

  // Transparent-texel discard: VRAM rgba 0000h is the "no draw" sentinel.
  if (all(equal(texcol, vec4(0.0, 0.0, 0.0, 0.0))))
    discard;
  float ialpha = 1.0;

  bool semitransparent = (texcol.a >= 0.5);

  uvec3 icolor;
  if (!TRUE_COLOR)
  {
    icolor = uvec3(texcol.rgb * vec3(255.0, 255.0, 255.0)) >> 3;
    if (!RAW_TEXTURE)
    {
      icolor = (icolor * vertcol) >> 4;
      if (DITHERING)
        icolor = ApplyDithering(uvec2(gl_FragCoord.xy), icolor);
      else
        icolor = min(icolor >> 3, uvec3(31u, 31u, 31u));
    }
  }
  else
  {
    icolor = uvec3(texcol.rgb * vec3(255.0, 255.0, 255.0));
    if (!RAW_TEXTURE)
    {
      icolor = (icolor * vertcol) >> 7;
      if (DITHERING)
        icolor = ApplyDithering(uvec2(gl_FragCoord.xy), icolor);
      else
        icolor = min(icolor, uvec3(255u, 255u, 255u));
    }
  }

  // Mask bit: textured polygons mask if either u_set_mask_while_drawing
  // is set or the texel was semitransparent.
  float oalpha = u_set_mask_while_drawing ? 1.0 : (semitransparent ? 1.0 : 0.0);

  // Premultiplied colour.
  float premultiply_alpha = TRANSPARENCY
                            ? (ialpha * (semitransparent ? u_src_alpha_factor : 1.0))
                            : ialpha;

  vec3 color = TRUE_COLOR
               ? ((vec3(icolor) * premultiply_alpha) / vec3(255.0, 255.0, 255.0))
               : (floor(vec3(icolor) * premultiply_alpha) /  vec3(31.0,  31.0,  31.0));

  // Output. Textured path differs from untextured in that the
  // TRANSPARENCY branch is the "TEXTURED && TRANSPARENCY" branch in
  // the shadergen, which also drives the per-texel discard logic for
  // TRANSPARENCY_ONLY_OPAQUE / TRANSPARENCY_ONLY_TRANSPARENT.
  if (TRANSPARENCY)
  {
    if (semitransparent)
    {
      o_col0 = vec4(color, oalpha);
#if defined(DUAL_SOURCE)
      o_col1 = vec4(0.0, 0.0, 0.0, u_dst_alpha_factor / ialpha);
#endif
      if (TRANSPARENCY_ONLY_OPAQUE)
        discard;
    }
    else
    {
      o_col0 = vec4(color, oalpha);
#if defined(DUAL_SOURCE)
      o_col1 = vec4(0.0, 0.0, 0.0, 1.0 - ialpha);
#endif
      if (TRANSPARENCY_ONLY_TRANSPARENT)
        discard;
    }
  }
  else
  {
    o_col0 = vec4(color, oalpha);
#if defined(DUAL_SOURCE)
    o_col1 = vec4(0.0, 0.0, 0.0, 1.0 - ialpha);
#endif
  }

#if !defined(PGXP_DEPTH)
  gl_FragDepth = oalpha * gl_FragCoord.z;
#endif
}
