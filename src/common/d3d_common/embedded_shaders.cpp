// Copyright 2026 LibretroAdmin
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "embedded_shaders.h"

// Storage for the embedded DXBC blobs. Each .inc supplies a definition of
// the array and the matching size constant declared in embedded_shaders.h.
// The .inc files MUST be included inside the namespace block below so the
// names resolve to the header's extern declarations.
//
// Mirror of src/common/vulkan/embedded_shaders.cpp. The Vulkan side wraps
// its embedded blobs in a CreateShaderModule helper because vkCreateShaderModule
// is the API hand-off; the D3D paths have no equivalent API call - the
// caller drops the (data, size) pair straight into either a
// D3D12_SHADER_BYTECODE aggregate at PSO-creation time (D3D12) or an
// ID3D11Device::CreatePixelShader(bytecode, size, ...) call (D3D11).
// So this TU stays simple: just the .inc includes and the namespace.
namespace D3DCommon::EmbeddedShaders {

#include "embedded_dxbc/fullscreen_quad_vs.inc"
#include "embedded_dxbc/copy_ps.inc"
#include "embedded_dxbc/vram_copy_ps_pgxp0.inc"
#include "embedded_dxbc/vram_copy_ps_pgxp1.inc"
#include "embedded_dxbc/vram_write_ps_pgxp0.inc"
#include "embedded_dxbc/vram_write_ps_pgxp1.inc"
#include "embedded_dxbc/vram_fill_ps_p0w0i0.inc"
#include "embedded_dxbc/vram_fill_ps_p0w0i1.inc"
#include "embedded_dxbc/vram_fill_ps_p0w1i0.inc"
#include "embedded_dxbc/vram_fill_ps_p0w1i1.inc"
#include "embedded_dxbc/vram_fill_ps_p1w0i0.inc"
#include "embedded_dxbc/vram_fill_ps_p1w0i1.inc"
#include "embedded_dxbc/vram_fill_ps_p1w1i0.inc"
#include "embedded_dxbc/vram_fill_ps_p1w1i1.inc"
#include "embedded_dxbc/vram_update_depth_ps_msaa0.inc"
#include "embedded_dxbc/vram_update_depth_ps_msaa1.inc"
#include "embedded_dxbc/vram_read_ps_m1.inc"
#include "embedded_dxbc/vram_read_ps_m2.inc"
#include "embedded_dxbc/vram_read_ps_m4.inc"
#include "embedded_dxbc/vram_read_ps_m8.inc"
#include "embedded_dxbc/vram_read_ps_m16.inc"
#include "embedded_dxbc/vram_read_ps_m32.inc"
#include "embedded_dxbc/display_ps_d0i0c0m01.inc"
#include "embedded_dxbc/display_ps_d0i0c0m02.inc"
#include "embedded_dxbc/display_ps_d0i0c0m04.inc"
#include "embedded_dxbc/display_ps_d0i0c0m08.inc"
#include "embedded_dxbc/display_ps_d0i0c0m16.inc"
#include "embedded_dxbc/display_ps_d0i0c0m32.inc"
#include "embedded_dxbc/display_ps_d0i1c0m01.inc"
#include "embedded_dxbc/display_ps_d0i1c0m02.inc"
#include "embedded_dxbc/display_ps_d0i1c0m04.inc"
#include "embedded_dxbc/display_ps_d0i1c0m08.inc"
#include "embedded_dxbc/display_ps_d0i1c0m16.inc"
#include "embedded_dxbc/display_ps_d0i1c0m32.inc"
#include "embedded_dxbc/display_ps_d0i2c0m01.inc"
#include "embedded_dxbc/display_ps_d0i2c0m02.inc"
#include "embedded_dxbc/display_ps_d0i2c0m04.inc"
#include "embedded_dxbc/display_ps_d0i2c0m08.inc"
#include "embedded_dxbc/display_ps_d0i2c0m16.inc"
#include "embedded_dxbc/display_ps_d0i2c0m32.inc"
#include "embedded_dxbc/display_ps_d1i0c0m01.inc"
#include "embedded_dxbc/display_ps_d1i0c0m02.inc"
#include "embedded_dxbc/display_ps_d1i0c0m04.inc"
#include "embedded_dxbc/display_ps_d1i0c0m08.inc"
#include "embedded_dxbc/display_ps_d1i0c0m16.inc"
#include "embedded_dxbc/display_ps_d1i0c0m32.inc"
#include "embedded_dxbc/display_ps_d1i0c1m01.inc"
#include "embedded_dxbc/display_ps_d1i0c1m02.inc"
#include "embedded_dxbc/display_ps_d1i0c1m04.inc"
#include "embedded_dxbc/display_ps_d1i0c1m08.inc"
#include "embedded_dxbc/display_ps_d1i0c1m16.inc"
#include "embedded_dxbc/display_ps_d1i0c1m32.inc"
#include "embedded_dxbc/display_ps_d1i1c0m01.inc"
#include "embedded_dxbc/display_ps_d1i1c0m02.inc"
#include "embedded_dxbc/display_ps_d1i1c0m04.inc"
#include "embedded_dxbc/display_ps_d1i1c0m08.inc"
#include "embedded_dxbc/display_ps_d1i1c0m16.inc"
#include "embedded_dxbc/display_ps_d1i1c0m32.inc"
#include "embedded_dxbc/display_ps_d1i1c1m01.inc"
#include "embedded_dxbc/display_ps_d1i1c1m02.inc"
#include "embedded_dxbc/display_ps_d1i1c1m04.inc"
#include "embedded_dxbc/display_ps_d1i1c1m08.inc"
#include "embedded_dxbc/display_ps_d1i1c1m16.inc"
#include "embedded_dxbc/display_ps_d1i1c1m32.inc"
#include "embedded_dxbc/display_ps_d1i2c0m01.inc"
#include "embedded_dxbc/display_ps_d1i2c0m02.inc"
#include "embedded_dxbc/display_ps_d1i2c0m04.inc"
#include "embedded_dxbc/display_ps_d1i2c0m08.inc"
#include "embedded_dxbc/display_ps_d1i2c0m16.inc"
#include "embedded_dxbc/display_ps_d1i2c0m32.inc"
#include "embedded_dxbc/display_ps_d1i2c1m01.inc"
#include "embedded_dxbc/display_ps_d1i2c1m02.inc"
#include "embedded_dxbc/display_ps_d1i2c1m04.inc"
#include "embedded_dxbc/display_ps_d1i2c1m08.inc"
#include "embedded_dxbc/display_ps_d1i2c1m16.inc"
#include "embedded_dxbc/display_ps_d1i2c1m32.inc"

#include "embedded_dxbc/batch_untextured_ps_d0_none_p0.inc"
#include "embedded_dxbc/batch_untextured_ps_d0_none_p1.inc"
#include "embedded_dxbc/batch_untextured_ps_d0_centroid_p0.inc"
#include "embedded_dxbc/batch_untextured_ps_d0_centroid_p1.inc"
#include "embedded_dxbc/batch_untextured_ps_d0_sample_p0.inc"
#include "embedded_dxbc/batch_untextured_ps_d0_sample_p1.inc"
#include "embedded_dxbc/batch_untextured_ps_d1_none_p0.inc"
#include "embedded_dxbc/batch_untextured_ps_d1_none_p1.inc"
#include "embedded_dxbc/batch_untextured_ps_d1_centroid_p0.inc"
#include "embedded_dxbc/batch_untextured_ps_d1_centroid_p1.inc"
#include "embedded_dxbc/batch_untextured_ps_d1_sample_p0.inc"
#include "embedded_dxbc/batch_untextured_ps_d1_sample_p1.inc"

#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d0_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d0_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d0_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d0_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d0_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d0_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d1_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d1_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d1_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d1_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d1_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r0_d1_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d0_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d0_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d0_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d0_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d0_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d0_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d1_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d1_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d1_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d1_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d1_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p0r1_d1_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d0_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d0_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d0_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d0_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d0_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d0_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d1_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d1_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d1_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d1_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d1_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r0_d1_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d0_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d0_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d0_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d0_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d0_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d0_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d1_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d1_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d1_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d1_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d1_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p4r1_d1_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d0_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d0_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d0_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d0_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d0_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d0_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d1_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d1_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d1_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d1_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d1_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r0_d1_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d0_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d0_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d0_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d0_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d0_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d0_sample_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d1_centroid_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d1_centroid_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d1_none_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d1_none_n1.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d1_sample_n0.inc"
#include "embedded_dxbc/batch_textured_nearest_ps_p8r1_d1_sample_n1.inc"

#include "embedded_dxbc/adaptive_downsample_blur_ps.inc"
#include "embedded_dxbc/adaptive_downsample_composite_ps_s16.inc"
#include "embedded_dxbc/adaptive_downsample_composite_ps_s2.inc"
#include "embedded_dxbc/adaptive_downsample_composite_ps_s4.inc"
#include "embedded_dxbc/adaptive_downsample_composite_ps_s8.inc"
#include "embedded_dxbc/adaptive_downsample_mip_ps_f0.inc"
#include "embedded_dxbc/adaptive_downsample_mip_ps_f1.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s10.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s11.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s12.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s13.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s14.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s15.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s16.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s2.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s3.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s4.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s5.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s6.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s7.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s8.inc"
#include "embedded_dxbc/box_sample_downsample_ps_s9.inc"

// ---- Batch FS pre-bake pickers -------------------------------------
//
// Selection logic for the pre-baked batch FS blobs above. Each
// function returns the matching Bytecode struct from a static
// table; the table is statically initialised so there's no
// per-call work beyond the index computation. See embedded_shaders.h
// for the picker function declarations + axis documentation.
//
// Shared between the D3D11 and D3D12 backends - the DXBC bytecode
// is consumed identically (D3D12 wraps in D3D12_SHADER_BYTECODE
// for ID3D12PipelineState; D3D11 calls
// ID3D11Device::CreatePixelShader(bytecode, size, ...)).

Bytecode PickBatchUntexturedFS(
    bool use_dual_source, uint32_t multisamples, bool per_sample_shading,
    bool disable_color_perspective)
{
  // 0 = none, 1 = centroid, 2 = sample. Mirror of
  // ShaderGen::GetInterpolationQualifier: sample wins over centroid
  // when per-sample-shading is enabled.
  const unsigned interp_idx =
    per_sample_shading ? 2u : ((multisamples > 1u) ? 1u : 0u);
  const unsigned persp_idx = disable_color_perspective ? 1u : 0u;

  // 3-D table: [dual_source][interp][persp]. Nested brace-init keeps
  // the structural correspondence with the regen tool's
  // TEMPLATE_VARIANTS table visible to readers - the
  // outermost-to-innermost iteration order matches the alphabetical
  // .inc filenames. Each entry is a
  // D3D12_SHADER_BYTECODE { pShaderBytecode, BytecodeLength }
  // aggregate; the extern declarations in
  // src/common/d3d_common/embedded_shaders.h supply both halves.
  static const Bytecode k_blobs[2][3][2] = {
    // dual = 0
    {
      // interp = none
      {{k_batch_untextured_ps_d0_none_p0,
        k_batch_untextured_ps_d0_none_p0_size_bytes},
       {k_batch_untextured_ps_d0_none_p1,
        k_batch_untextured_ps_d0_none_p1_size_bytes}},
      // interp = centroid
      {{k_batch_untextured_ps_d0_centroid_p0,
        k_batch_untextured_ps_d0_centroid_p0_size_bytes},
       {k_batch_untextured_ps_d0_centroid_p1,
        k_batch_untextured_ps_d0_centroid_p1_size_bytes}},
      // interp = sample
      {{k_batch_untextured_ps_d0_sample_p0,
        k_batch_untextured_ps_d0_sample_p0_size_bytes},
       {k_batch_untextured_ps_d0_sample_p1,
        k_batch_untextured_ps_d0_sample_p1_size_bytes}},
    },
    // dual = 1
    {
      // interp = none
      {{k_batch_untextured_ps_d1_none_p0,
        k_batch_untextured_ps_d1_none_p0_size_bytes},
       {k_batch_untextured_ps_d1_none_p1,
        k_batch_untextured_ps_d1_none_p1_size_bytes}},
      // interp = centroid
      {{k_batch_untextured_ps_d1_centroid_p0,
        k_batch_untextured_ps_d1_centroid_p0_size_bytes},
       {k_batch_untextured_ps_d1_centroid_p1,
        k_batch_untextured_ps_d1_centroid_p1_size_bytes}},
      // interp = sample
      {{k_batch_untextured_ps_d1_sample_p0,
        k_batch_untextured_ps_d1_sample_p0_size_bytes},
       {k_batch_untextured_ps_d1_sample_p1,
        k_batch_untextured_ps_d1_sample_p1_size_bytes}},
    },
  };

  return k_blobs[use_dual_source ? 1 : 0][interp_idx][persp_idx];
}


// Textured + Nearest-filter batch FS pre-baked variant picker.
// Returns the matching DXBC blob from the 72-entry table at
// src/common/d3d12/embedded_dxbc/batch_textured_nearest_ps_*.inc,
// indexed by the 4 axes the regen tool bakes:
//
//   texture_mode      6 combos (Palette4Bit / Palette8Bit /
//                     Direct16Bit each in non-raw / raw form). The
//                     Reserved_Direct16Bit / Reserved_RawDirect16Bit
//                     enum values are deduped to their non-Reserved
//                     counterparts at the GPU_HW_D3D12 caller via
//                     the `lookup_mode` parameter.
//   use_dual_source   driven by the same call-site formula as the
//                     untextured picker - the caller computes once
//                     and hands the bit to both this picker AND the
//                     PSO blend-state setup that references SRC1_*.
//   interp            (per_sample_shading > MSAA > none; mirror of
//                      ShaderGen::GetInterpolationQualifier)
//   noperspective     (m_disable_color_perspective)
//
// As with the untextured slice (c01f8ae), the former TRANSPARENCY
// axis (4 BatchRenderMode enum values) is now read from
// u_render_mode in the cbuffer - the DXBC is invariant across
// render_mode post-c532a34, so a single set of 72 variants covers
// all 4 render_modes.
Bytecode PickBatchTexturedNearestFS(
    uint8_t lookup_mode, bool use_dual_source, uint32_t multisamples,
    bool per_sample_shading, bool disable_color_perspective)
{
  // texture_mode dim: derived from lookup_mode (Reserved_* already
  // deduped by the caller). lookup_mode is a 3-bit value with
  //   bits 0..1 = actual_mode (0 = Palette4Bit, 1 = Palette8Bit,
  //               2 = Direct16Bit; 3 is Reserved_Direct16Bit but
  //               the caller maps it to 2 before invoking us)
  //   bit  2    = RawTextureBit
  // The 6 reachable combos map to a 6-slot 1-D table:
  //   0: p0r0 Direct16Bit          (actual=2, raw=0)
  //   1: p0r1 RawDirect16Bit       (actual=2, raw=1)
  //   2: p4r0 Palette4Bit          (actual=0, raw=0)
  //   3: p4r1 RawPalette4Bit       (actual=0, raw=1)
  //   4: p8r0 Palette8Bit          (actual=1, raw=0)
  //   5: p8r1 RawPalette8Bit       (actual=1, raw=1)
  const uint8_t actual_mode = lookup_mode & 0x3u;
  const bool raw = (lookup_mode & 0x4u) != 0;
  unsigned tm_idx;
  if (actual_mode == 2u)        // Direct16Bit family
    tm_idx = raw ? 1u : 0u;
  else if (actual_mode == 0u)   // Palette4Bit family
    tm_idx = raw ? 3u : 2u;
  else                          // actual_mode == 1u (Palette8Bit family)
    tm_idx = raw ? 5u : 4u;

  // 0 = none, 1 = centroid, 2 = sample. Same shape as the untextured
  // picker - mirror of ShaderGen::GetInterpolationQualifier.
  const unsigned interp_idx =
    per_sample_shading ? 2u : ((multisamples > 1u) ? 1u : 0u);
  const unsigned persp_idx = disable_color_perspective ? 1u : 0u;

  static const Bytecode k_blobs[6][2][3][2] = {
    // tm = p0r0 (Direct16Bit)
    {
      // no dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p0r0_d0_none_n0,
          k_batch_textured_nearest_ps_p0r0_d0_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r0_d0_none_n1,
          k_batch_textured_nearest_ps_p0r0_d0_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p0r0_d0_centroid_n0,
          k_batch_textured_nearest_ps_p0r0_d0_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r0_d0_centroid_n1,
          k_batch_textured_nearest_ps_p0r0_d0_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p0r0_d0_sample_n0,
          k_batch_textured_nearest_ps_p0r0_d0_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r0_d0_sample_n1,
          k_batch_textured_nearest_ps_p0r0_d0_sample_n1_size_bytes}},
      },
      // dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p0r0_d1_none_n0,
          k_batch_textured_nearest_ps_p0r0_d1_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r0_d1_none_n1,
          k_batch_textured_nearest_ps_p0r0_d1_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p0r0_d1_centroid_n0,
          k_batch_textured_nearest_ps_p0r0_d1_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r0_d1_centroid_n1,
          k_batch_textured_nearest_ps_p0r0_d1_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p0r0_d1_sample_n0,
          k_batch_textured_nearest_ps_p0r0_d1_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r0_d1_sample_n1,
          k_batch_textured_nearest_ps_p0r0_d1_sample_n1_size_bytes}},
      },
    },
    // tm = p0r1 (RawDirect16Bit)
    {
      // no dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p0r1_d0_none_n0,
          k_batch_textured_nearest_ps_p0r1_d0_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r1_d0_none_n1,
          k_batch_textured_nearest_ps_p0r1_d0_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p0r1_d0_centroid_n0,
          k_batch_textured_nearest_ps_p0r1_d0_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r1_d0_centroid_n1,
          k_batch_textured_nearest_ps_p0r1_d0_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p0r1_d0_sample_n0,
          k_batch_textured_nearest_ps_p0r1_d0_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r1_d0_sample_n1,
          k_batch_textured_nearest_ps_p0r1_d0_sample_n1_size_bytes}},
      },
      // dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p0r1_d1_none_n0,
          k_batch_textured_nearest_ps_p0r1_d1_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r1_d1_none_n1,
          k_batch_textured_nearest_ps_p0r1_d1_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p0r1_d1_centroid_n0,
          k_batch_textured_nearest_ps_p0r1_d1_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r1_d1_centroid_n1,
          k_batch_textured_nearest_ps_p0r1_d1_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p0r1_d1_sample_n0,
          k_batch_textured_nearest_ps_p0r1_d1_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p0r1_d1_sample_n1,
          k_batch_textured_nearest_ps_p0r1_d1_sample_n1_size_bytes}},
      },
    },
    // tm = p4r0 (Palette4Bit)
    {
      // no dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p4r0_d0_none_n0,
          k_batch_textured_nearest_ps_p4r0_d0_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r0_d0_none_n1,
          k_batch_textured_nearest_ps_p4r0_d0_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p4r0_d0_centroid_n0,
          k_batch_textured_nearest_ps_p4r0_d0_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r0_d0_centroid_n1,
          k_batch_textured_nearest_ps_p4r0_d0_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p4r0_d0_sample_n0,
          k_batch_textured_nearest_ps_p4r0_d0_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r0_d0_sample_n1,
          k_batch_textured_nearest_ps_p4r0_d0_sample_n1_size_bytes}},
      },
      // dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p4r0_d1_none_n0,
          k_batch_textured_nearest_ps_p4r0_d1_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r0_d1_none_n1,
          k_batch_textured_nearest_ps_p4r0_d1_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p4r0_d1_centroid_n0,
          k_batch_textured_nearest_ps_p4r0_d1_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r0_d1_centroid_n1,
          k_batch_textured_nearest_ps_p4r0_d1_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p4r0_d1_sample_n0,
          k_batch_textured_nearest_ps_p4r0_d1_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r0_d1_sample_n1,
          k_batch_textured_nearest_ps_p4r0_d1_sample_n1_size_bytes}},
      },
    },
    // tm = p4r1 (RawPalette4Bit)
    {
      // no dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p4r1_d0_none_n0,
          k_batch_textured_nearest_ps_p4r1_d0_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r1_d0_none_n1,
          k_batch_textured_nearest_ps_p4r1_d0_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p4r1_d0_centroid_n0,
          k_batch_textured_nearest_ps_p4r1_d0_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r1_d0_centroid_n1,
          k_batch_textured_nearest_ps_p4r1_d0_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p4r1_d0_sample_n0,
          k_batch_textured_nearest_ps_p4r1_d0_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r1_d0_sample_n1,
          k_batch_textured_nearest_ps_p4r1_d0_sample_n1_size_bytes}},
      },
      // dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p4r1_d1_none_n0,
          k_batch_textured_nearest_ps_p4r1_d1_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r1_d1_none_n1,
          k_batch_textured_nearest_ps_p4r1_d1_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p4r1_d1_centroid_n0,
          k_batch_textured_nearest_ps_p4r1_d1_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r1_d1_centroid_n1,
          k_batch_textured_nearest_ps_p4r1_d1_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p4r1_d1_sample_n0,
          k_batch_textured_nearest_ps_p4r1_d1_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p4r1_d1_sample_n1,
          k_batch_textured_nearest_ps_p4r1_d1_sample_n1_size_bytes}},
      },
    },
    // tm = p8r0 (Palette8Bit)
    {
      // no dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p8r0_d0_none_n0,
          k_batch_textured_nearest_ps_p8r0_d0_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r0_d0_none_n1,
          k_batch_textured_nearest_ps_p8r0_d0_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p8r0_d0_centroid_n0,
          k_batch_textured_nearest_ps_p8r0_d0_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r0_d0_centroid_n1,
          k_batch_textured_nearest_ps_p8r0_d0_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p8r0_d0_sample_n0,
          k_batch_textured_nearest_ps_p8r0_d0_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r0_d0_sample_n1,
          k_batch_textured_nearest_ps_p8r0_d0_sample_n1_size_bytes}},
      },
      // dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p8r0_d1_none_n0,
          k_batch_textured_nearest_ps_p8r0_d1_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r0_d1_none_n1,
          k_batch_textured_nearest_ps_p8r0_d1_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p8r0_d1_centroid_n0,
          k_batch_textured_nearest_ps_p8r0_d1_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r0_d1_centroid_n1,
          k_batch_textured_nearest_ps_p8r0_d1_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p8r0_d1_sample_n0,
          k_batch_textured_nearest_ps_p8r0_d1_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r0_d1_sample_n1,
          k_batch_textured_nearest_ps_p8r0_d1_sample_n1_size_bytes}},
      },
    },
    // tm = p8r1 (RawPalette8Bit)
    {
      // no dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p8r1_d0_none_n0,
          k_batch_textured_nearest_ps_p8r1_d0_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r1_d0_none_n1,
          k_batch_textured_nearest_ps_p8r1_d0_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p8r1_d0_centroid_n0,
          k_batch_textured_nearest_ps_p8r1_d0_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r1_d0_centroid_n1,
          k_batch_textured_nearest_ps_p8r1_d0_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p8r1_d0_sample_n0,
          k_batch_textured_nearest_ps_p8r1_d0_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r1_d0_sample_n1,
          k_batch_textured_nearest_ps_p8r1_d0_sample_n1_size_bytes}},
      },
      // dual
      {
        // interp = none
        {{k_batch_textured_nearest_ps_p8r1_d1_none_n0,
          k_batch_textured_nearest_ps_p8r1_d1_none_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r1_d1_none_n1,
          k_batch_textured_nearest_ps_p8r1_d1_none_n1_size_bytes}},
        // interp = centroid
        {{k_batch_textured_nearest_ps_p8r1_d1_centroid_n0,
          k_batch_textured_nearest_ps_p8r1_d1_centroid_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r1_d1_centroid_n1,
          k_batch_textured_nearest_ps_p8r1_d1_centroid_n1_size_bytes}},
        // interp = sample
        {{k_batch_textured_nearest_ps_p8r1_d1_sample_n0,
          k_batch_textured_nearest_ps_p8r1_d1_sample_n0_size_bytes},
         {k_batch_textured_nearest_ps_p8r1_d1_sample_n1,
          k_batch_textured_nearest_ps_p8r1_d1_sample_n1_size_bytes}},
      },
    },
  };

  return k_blobs[tm_idx][use_dual_source ? 1 : 0][interp_idx][persp_idx];
}

// ---- Downsample pre-bake pickers -----------------------------------
//
// Tiny tables - 1, 2, 4, and 15 slots respectively. The
// resolution_scale -> table-index encoding for the composite
// picker is a 4-entry switch (pow2-only), for the box picker
// it's `scale - 2` (linear over [2, 16]). Out-of-range inputs
// clamp to the nearest in-range slot in release builds; debug
// builds also assert via the static array indexing (caller
// is expected to filter via GetDownsampleMode upstream).

Bytecode PickAdaptiveDownsampleBlurFS()
{
  return Bytecode{k_adaptive_downsample_blur_ps,
                  k_adaptive_downsample_blur_ps_size_bytes};
}

Bytecode PickAdaptiveDownsampleMipFS(bool first_pass)
{
  static const Bytecode k_blobs[2] = {
    {k_adaptive_downsample_mip_ps_f0, k_adaptive_downsample_mip_ps_f0_size_bytes},
    {k_adaptive_downsample_mip_ps_f1, k_adaptive_downsample_mip_ps_f1_size_bytes},
  };
  return k_blobs[first_pass ? 1 : 0];
}

Bytecode PickAdaptiveDownsampleCompositeFS(uint32_t resolution_scale)
{
  // Reachable values: {2, 4, 8, 16}. Table indexed 0..3.
  //   2  -> 0
  //   4  -> 1
  //   8  -> 2
  //   16 -> 3
  static const Bytecode k_blobs[4] = {
    {k_adaptive_downsample_composite_ps_s2,  k_adaptive_downsample_composite_ps_s2_size_bytes},
    {k_adaptive_downsample_composite_ps_s4,  k_adaptive_downsample_composite_ps_s4_size_bytes},
    {k_adaptive_downsample_composite_ps_s8,  k_adaptive_downsample_composite_ps_s8_size_bytes},
    {k_adaptive_downsample_composite_ps_s16, k_adaptive_downsample_composite_ps_s16_size_bytes},
  };

  unsigned idx;
  switch (resolution_scale)
  {
    case 2u:  idx = 0u; break;
    case 4u:  idx = 1u; break;
    case 8u:  idx = 2u; break;
    case 16u: idx = 3u; break;
    default:
      // Caller failed to enforce Adaptive's pow2-in-[2,16] contract.
      // Clamp to the closest in-range index that still produces a
      // visually reasonable result. <=2 -> 2x; >=16 -> 16x.
      idx = (resolution_scale <= 2u)  ? 0u :
            (resolution_scale <= 4u)  ? 1u :
            (resolution_scale <= 8u)  ? 2u : 3u;
      break;
  }
  return k_blobs[idx];
}

Bytecode PickBoxSampleDownsampleFS(uint32_t resolution_scale)
{
  // Reachable values: [2, 16]. Table indexed 0..14, idx = scale - 2.
  static const Bytecode k_blobs[15] = {
    {k_box_sample_downsample_ps_s2,  k_box_sample_downsample_ps_s2_size_bytes},
    {k_box_sample_downsample_ps_s3,  k_box_sample_downsample_ps_s3_size_bytes},
    {k_box_sample_downsample_ps_s4,  k_box_sample_downsample_ps_s4_size_bytes},
    {k_box_sample_downsample_ps_s5,  k_box_sample_downsample_ps_s5_size_bytes},
    {k_box_sample_downsample_ps_s6,  k_box_sample_downsample_ps_s6_size_bytes},
    {k_box_sample_downsample_ps_s7,  k_box_sample_downsample_ps_s7_size_bytes},
    {k_box_sample_downsample_ps_s8,  k_box_sample_downsample_ps_s8_size_bytes},
    {k_box_sample_downsample_ps_s9,  k_box_sample_downsample_ps_s9_size_bytes},
    {k_box_sample_downsample_ps_s10, k_box_sample_downsample_ps_s10_size_bytes},
    {k_box_sample_downsample_ps_s11, k_box_sample_downsample_ps_s11_size_bytes},
    {k_box_sample_downsample_ps_s12, k_box_sample_downsample_ps_s12_size_bytes},
    {k_box_sample_downsample_ps_s13, k_box_sample_downsample_ps_s13_size_bytes},
    {k_box_sample_downsample_ps_s14, k_box_sample_downsample_ps_s14_size_bytes},
    {k_box_sample_downsample_ps_s15, k_box_sample_downsample_ps_s15_size_bytes},
    {k_box_sample_downsample_ps_s16, k_box_sample_downsample_ps_s16_size_bytes},
  };

  // Clamp to [2, 16] -> table-index [0, 14] in release builds.
  // Scale=1 is unreachable here per GetDownsampleMode
  // (gpu_hw.cpp:399), but defend anyway.
  const uint32_t clamped = (resolution_scale < 2u)  ? 2u :
                           (resolution_scale > 16u) ? 16u : resolution_scale;
  return k_blobs[clamped - 2u];
}

} // namespace D3DCommon::EmbeddedShaders
