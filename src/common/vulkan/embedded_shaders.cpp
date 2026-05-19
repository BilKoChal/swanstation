// Copyright 2026 LibretroAdmin
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "embedded_shaders.h"
#include "../log.h"
#include "context.h"
#include "util.h"
Log_SetChannel(Vulkan::EmbeddedShaders);

// Storage for the embedded SPIR-V blobs. Each .inc supplies a definition of
// the array and the matching size constant declared in embedded_shaders.h.
// The .inc files MUST be included inside the namespace block below so the
// names resolve to the header's extern declarations.
namespace Vulkan::EmbeddedShaders {

#include "embedded_spirv/adaptive_downsample_blur_fs.inc"
#include "embedded_spirv/adaptive_downsample_composite_fs.inc"
#include "embedded_spirv/adaptive_downsample_mip_fs.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_centroid_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_none_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_bilinear_fs_sample_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_centroid_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_none_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_jinc2_fs_sample_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_dual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_dual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_dual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_dual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_nodual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_nodual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_nodual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_noperp_nodual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_dual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_dual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_dual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_dual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_nodual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_nodual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_nodual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_centroid_persp_nodual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_dual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_dual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_dual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_dual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_nodual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_nodual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_nodual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_noperp_nodual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_dual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_dual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_dual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_dual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_nodual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_nodual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_nodual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_none_persp_nodual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_dual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_dual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_dual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_dual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_nodual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_nodual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_nodual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_noperp_nodual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_dual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_dual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_dual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_dual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_nodual_pgxpoff_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_nodual_pgxpoff_uv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_nodual_pgxpon_nouv.inc"
#include "embedded_spirv/batch_textured_nearest_fs_sample_persp_nodual_pgxpon_uv.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_centroid_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_none_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_textured_xbr_fs_sample_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_centroid_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_none_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_none_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_none_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_none_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_none_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_none_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_none_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_none_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_sample_noperp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_sample_noperp_dual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_sample_noperp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_sample_noperp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_sample_persp_dual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_sample_persp_dual_pgxpon.inc"
#include "embedded_spirv/batch_untextured_fs_sample_persp_nodual_pgxpoff.inc"
#include "embedded_spirv/batch_untextured_fs_sample_persp_nodual_pgxpon.inc"
#include "embedded_spirv/batch_vs_textured_centroid_noperp.inc"
#include "embedded_spirv/batch_vs_textured_centroid_persp.inc"
#include "embedded_spirv/batch_vs_textured_none_noperp.inc"
#include "embedded_spirv/batch_vs_textured_none_persp.inc"
#include "embedded_spirv/batch_vs_textured_sample_noperp.inc"
#include "embedded_spirv/batch_vs_textured_sample_persp.inc"
#include "embedded_spirv/batch_vs_untextured_centroid_noperp.inc"
#include "embedded_spirv/batch_vs_untextured_centroid_persp.inc"
#include "embedded_spirv/batch_vs_untextured_none_noperp.inc"
#include "embedded_spirv/batch_vs_untextured_none_persp.inc"
#include "embedded_spirv/batch_vs_untextured_sample_noperp.inc"
#include "embedded_spirv/batch_vs_untextured_sample_persp.inc"
#include "embedded_spirv/box_sample_downsample_fs.inc"
#include "embedded_spirv/display_fs.inc"
#include "embedded_spirv/display_msaa_fs.inc"
#include "embedded_spirv/present_cursor_fs.inc"
#include "embedded_spirv/present_display_fs.inc"
#include "embedded_spirv/present_fullscreen_vs.inc"
#include "embedded_spirv/screen_quad_vs.inc"
#include "embedded_spirv/uv_quad_vs.inc"
#include "embedded_spirv/vram_copy_fs.inc"
#include "embedded_spirv/vram_fill_fs.inc"
#include "embedded_spirv/vram_readback_fs.inc"
#include "embedded_spirv/vram_readback_msaa_fs.inc"
#include "embedded_spirv/vram_update_depth_fs.inc"
#include "embedded_spirv/vram_update_depth_msaa_fs.inc"
#include "embedded_spirv/vram_write_ssbo_fs.inc"
#include "embedded_spirv/vram_write_texbuf_fs.inc"

// Batch VS blob lookup table. Index encoding (must match
// GetBatchVertexShaderBlob below):
//
//   attr   = textured ? 1 : 0                          // 0..1
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0)   // 0..2
//   persp  = noperspective_color ? 1 : 0               // 0..1
//
//   index  = attr * 6 + interp * 2 + persp             // 0..11
//
// Macro keeps the table readable and ensures the symbol and size
// stay in lock-step.
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_vs_blobs[12] = {
  // attr = 0 (untextured)
  BLOB(batch_vs_untextured_none_persp),         // [ 0] interp=none,     persp=true
  BLOB(batch_vs_untextured_none_noperp),        // [ 1] interp=none,     persp=false
  BLOB(batch_vs_untextured_centroid_persp),     // [ 2] interp=centroid, persp=true
  BLOB(batch_vs_untextured_centroid_noperp),    // [ 3] interp=centroid, persp=false
  BLOB(batch_vs_untextured_sample_persp),       // [ 4] interp=sample,   persp=true
  BLOB(batch_vs_untextured_sample_noperp),      // [ 5] interp=sample,   persp=false
  // attr = 1 (textured, always with UV limits since the UV_LIMITS-
  // routing commit lifted the axis to the FS-side u_uv_limits
  // cbuffer scalar - the VS now unconditionally emits a_uv_limits +
  // v_uv_limits when textured, the FS decides at runtime whether to
  // consume it).
  BLOB(batch_vs_textured_none_persp),           // [ 6]
  BLOB(batch_vs_textured_none_noperp),          // [ 7]
  BLOB(batch_vs_textured_centroid_persp),       // [ 8]
  BLOB(batch_vs_textured_centroid_noperp),      // [ 9]
  BLOB(batch_vs_textured_sample_persp),         // [10]
  BLOB(batch_vs_textured_sample_noperp),        // [11]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchVertexShaderBlob(bool textured,
                                                   bool msaa,
                                                   bool per_sample_shading,
                                                   bool noperspective_color)
{
  const unsigned attr   = textured ? 1u : 0u;
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned index  = attr * 6u + interp * 2u + persp;
  return k_batch_vs_blobs[index];
}

// Batch FS untextured blob table. Index encoding (must match
// GetBatchUntexturedFragmentShaderBlob below):
//
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0)   (0..2)
//   persp  = noperspective_color ? 1 : 0               (0..1)
//   dual   = dual_source ? 1 : 0                       (0..1)
//   pgxp   = pgxp_depth ? 1 : 0                        (0..1)
//
//   index  = interp * 8 + persp * 4 + dual * 2 + pgxp  (0..23)
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_untextured_fs_blobs[24] = {
  // interp = 0 (none)
  BLOB(batch_untextured_fs_none_persp_nodual_pgxpoff),    // [ 0]
  BLOB(batch_untextured_fs_none_persp_nodual_pgxpon),     // [ 1]
  BLOB(batch_untextured_fs_none_persp_dual_pgxpoff),      // [ 2]
  BLOB(batch_untextured_fs_none_persp_dual_pgxpon),       // [ 3]
  BLOB(batch_untextured_fs_none_noperp_nodual_pgxpoff),   // [ 4]
  BLOB(batch_untextured_fs_none_noperp_nodual_pgxpon),    // [ 5]
  BLOB(batch_untextured_fs_none_noperp_dual_pgxpoff),     // [ 6]
  BLOB(batch_untextured_fs_none_noperp_dual_pgxpon),      // [ 7]
  // interp = 1 (centroid)
  BLOB(batch_untextured_fs_centroid_persp_nodual_pgxpoff),  // [ 8]
  BLOB(batch_untextured_fs_centroid_persp_nodual_pgxpon),   // [ 9]
  BLOB(batch_untextured_fs_centroid_persp_dual_pgxpoff),    // [10]
  BLOB(batch_untextured_fs_centroid_persp_dual_pgxpon),     // [11]
  BLOB(batch_untextured_fs_centroid_noperp_nodual_pgxpoff), // [12]
  BLOB(batch_untextured_fs_centroid_noperp_nodual_pgxpon),  // [13]
  BLOB(batch_untextured_fs_centroid_noperp_dual_pgxpoff),   // [14]
  BLOB(batch_untextured_fs_centroid_noperp_dual_pgxpon),    // [15]
  // interp = 2 (sample)
  BLOB(batch_untextured_fs_sample_persp_nodual_pgxpoff),  // [16]
  BLOB(batch_untextured_fs_sample_persp_nodual_pgxpon),   // [17]
  BLOB(batch_untextured_fs_sample_persp_dual_pgxpoff),    // [18]
  BLOB(batch_untextured_fs_sample_persp_dual_pgxpon),     // [19]
  BLOB(batch_untextured_fs_sample_noperp_nodual_pgxpoff), // [20]
  BLOB(batch_untextured_fs_sample_noperp_nodual_pgxpon),  // [21]
  BLOB(batch_untextured_fs_sample_noperp_dual_pgxpoff),   // [22]
  BLOB(batch_untextured_fs_sample_noperp_dual_pgxpon),    // [23]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchUntexturedFragmentShaderBlob(bool msaa,
                                                               bool per_sample_shading,
                                                               bool noperspective_color,
                                                               bool dual_source,
                                                               bool pgxp_depth)
{
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned dual   = dual_source ? 1u : 0u;
  const unsigned pgxp   = pgxp_depth ? 1u : 0u;
  const unsigned index  = interp * 8u + persp * 4u + dual * 2u + pgxp;
  return k_batch_untextured_fs_blobs[index];
}


// Batch FS textured-Nearest blob table. Index encoding (must match
// GetBatchTexturedNearestFragmentShaderBlob below):
//
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0)        (0..2)
//   persp  = noperspective_color ? 1 : 0                    (0..1)
//   dual   = dual_source ? 1 : 0                            (0..1)
//   pgxp   = pgxp_depth ? 1 : 0                             (0..1)
//   uv     = uv_limits ? 1 : 0                              (0..1)
//
//   index  = interp*16 + persp*8 + dual*4 + pgxp*2 + uv     (0..47)
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_textured_nearest_fs_blobs[48] = {
  BLOB(batch_textured_nearest_fs_none_persp_nodual_pgxpoff_nouv), // [ 0]
  BLOB(batch_textured_nearest_fs_none_persp_nodual_pgxpoff_uv), // [ 1]
  BLOB(batch_textured_nearest_fs_none_persp_nodual_pgxpon_nouv), // [ 2]
  BLOB(batch_textured_nearest_fs_none_persp_nodual_pgxpon_uv), // [ 3]
  BLOB(batch_textured_nearest_fs_none_persp_dual_pgxpoff_nouv), // [ 4]
  BLOB(batch_textured_nearest_fs_none_persp_dual_pgxpoff_uv), // [ 5]
  BLOB(batch_textured_nearest_fs_none_persp_dual_pgxpon_nouv), // [ 6]
  BLOB(batch_textured_nearest_fs_none_persp_dual_pgxpon_uv), // [ 7]
  BLOB(batch_textured_nearest_fs_none_noperp_nodual_pgxpoff_nouv), // [ 8]
  BLOB(batch_textured_nearest_fs_none_noperp_nodual_pgxpoff_uv), // [ 9]
  BLOB(batch_textured_nearest_fs_none_noperp_nodual_pgxpon_nouv), // [10]
  BLOB(batch_textured_nearest_fs_none_noperp_nodual_pgxpon_uv), // [11]
  BLOB(batch_textured_nearest_fs_none_noperp_dual_pgxpoff_nouv), // [12]
  BLOB(batch_textured_nearest_fs_none_noperp_dual_pgxpoff_uv), // [13]
  BLOB(batch_textured_nearest_fs_none_noperp_dual_pgxpon_nouv), // [14]
  BLOB(batch_textured_nearest_fs_none_noperp_dual_pgxpon_uv), // [15]
  BLOB(batch_textured_nearest_fs_centroid_persp_nodual_pgxpoff_nouv), // [16]
  BLOB(batch_textured_nearest_fs_centroid_persp_nodual_pgxpoff_uv), // [17]
  BLOB(batch_textured_nearest_fs_centroid_persp_nodual_pgxpon_nouv), // [18]
  BLOB(batch_textured_nearest_fs_centroid_persp_nodual_pgxpon_uv), // [19]
  BLOB(batch_textured_nearest_fs_centroid_persp_dual_pgxpoff_nouv), // [20]
  BLOB(batch_textured_nearest_fs_centroid_persp_dual_pgxpoff_uv), // [21]
  BLOB(batch_textured_nearest_fs_centroid_persp_dual_pgxpon_nouv), // [22]
  BLOB(batch_textured_nearest_fs_centroid_persp_dual_pgxpon_uv), // [23]
  BLOB(batch_textured_nearest_fs_centroid_noperp_nodual_pgxpoff_nouv), // [24]
  BLOB(batch_textured_nearest_fs_centroid_noperp_nodual_pgxpoff_uv), // [25]
  BLOB(batch_textured_nearest_fs_centroid_noperp_nodual_pgxpon_nouv), // [26]
  BLOB(batch_textured_nearest_fs_centroid_noperp_nodual_pgxpon_uv), // [27]
  BLOB(batch_textured_nearest_fs_centroid_noperp_dual_pgxpoff_nouv), // [28]
  BLOB(batch_textured_nearest_fs_centroid_noperp_dual_pgxpoff_uv), // [29]
  BLOB(batch_textured_nearest_fs_centroid_noperp_dual_pgxpon_nouv), // [30]
  BLOB(batch_textured_nearest_fs_centroid_noperp_dual_pgxpon_uv), // [31]
  BLOB(batch_textured_nearest_fs_sample_persp_nodual_pgxpoff_nouv), // [32]
  BLOB(batch_textured_nearest_fs_sample_persp_nodual_pgxpoff_uv), // [33]
  BLOB(batch_textured_nearest_fs_sample_persp_nodual_pgxpon_nouv), // [34]
  BLOB(batch_textured_nearest_fs_sample_persp_nodual_pgxpon_uv), // [35]
  BLOB(batch_textured_nearest_fs_sample_persp_dual_pgxpoff_nouv), // [36]
  BLOB(batch_textured_nearest_fs_sample_persp_dual_pgxpoff_uv), // [37]
  BLOB(batch_textured_nearest_fs_sample_persp_dual_pgxpon_nouv), // [38]
  BLOB(batch_textured_nearest_fs_sample_persp_dual_pgxpon_uv), // [39]
  BLOB(batch_textured_nearest_fs_sample_noperp_nodual_pgxpoff_nouv), // [40]
  BLOB(batch_textured_nearest_fs_sample_noperp_nodual_pgxpoff_uv), // [41]
  BLOB(batch_textured_nearest_fs_sample_noperp_nodual_pgxpon_nouv), // [42]
  BLOB(batch_textured_nearest_fs_sample_noperp_nodual_pgxpon_uv), // [43]
  BLOB(batch_textured_nearest_fs_sample_noperp_dual_pgxpoff_nouv), // [44]
  BLOB(batch_textured_nearest_fs_sample_noperp_dual_pgxpoff_uv), // [45]
  BLOB(batch_textured_nearest_fs_sample_noperp_dual_pgxpon_nouv), // [46]
  BLOB(batch_textured_nearest_fs_sample_noperp_dual_pgxpon_uv), // [47]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchTexturedNearestFragmentShaderBlob(bool msaa,
                                                                    bool per_sample_shading,
                                                                    bool noperspective_color,
                                                                    bool dual_source,
                                                                    bool pgxp_depth,
                                                                    bool uv_limits)
{
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned dual   = dual_source ? 1u : 0u;
  const unsigned pgxp   = pgxp_depth ? 1u : 0u;
  const unsigned uv     = uv_limits ? 1u : 0u;
  const unsigned index  = interp * 16u + persp * 8u + dual * 4u + pgxp * 2u + uv;
  return k_batch_textured_nearest_fs_blobs[index];
}

// Batch FS textured-Bilinear / BilinearBinAlpha blob table. Index
// encoding (must match GetBatchTexturedBilinearFragmentShaderBlob
// below):
//
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0)    (0..2)
//   persp  = noperspective_color ? 1 : 0                (0..1)
//   dual   = dual_source ? 1 : 0                        (0..1)
//   pgxp   = pgxp_depth ? 1 : 0                         (0..1)
//
//   index  = interp*8 + persp*4 + dual*2 + pgxp         (0..23)
//
// BINALPHA (id=110) is a per-call spec constant, not a structural
// axis - it just toggles the final ialpha quantisation step.
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_textured_bilinear_fs_blobs[24] = {
  BLOB(batch_textured_bilinear_fs_none_persp_nodual_pgxpoff), // [ 0]
  BLOB(batch_textured_bilinear_fs_none_persp_nodual_pgxpon), // [ 1]
  BLOB(batch_textured_bilinear_fs_none_persp_dual_pgxpoff), // [ 2]
  BLOB(batch_textured_bilinear_fs_none_persp_dual_pgxpon), // [ 3]
  BLOB(batch_textured_bilinear_fs_none_noperp_nodual_pgxpoff), // [ 4]
  BLOB(batch_textured_bilinear_fs_none_noperp_nodual_pgxpon), // [ 5]
  BLOB(batch_textured_bilinear_fs_none_noperp_dual_pgxpoff), // [ 6]
  BLOB(batch_textured_bilinear_fs_none_noperp_dual_pgxpon), // [ 7]
  BLOB(batch_textured_bilinear_fs_centroid_persp_nodual_pgxpoff), // [ 8]
  BLOB(batch_textured_bilinear_fs_centroid_persp_nodual_pgxpon), // [ 9]
  BLOB(batch_textured_bilinear_fs_centroid_persp_dual_pgxpoff), // [10]
  BLOB(batch_textured_bilinear_fs_centroid_persp_dual_pgxpon), // [11]
  BLOB(batch_textured_bilinear_fs_centroid_noperp_nodual_pgxpoff), // [12]
  BLOB(batch_textured_bilinear_fs_centroid_noperp_nodual_pgxpon), // [13]
  BLOB(batch_textured_bilinear_fs_centroid_noperp_dual_pgxpoff), // [14]
  BLOB(batch_textured_bilinear_fs_centroid_noperp_dual_pgxpon), // [15]
  BLOB(batch_textured_bilinear_fs_sample_persp_nodual_pgxpoff), // [16]
  BLOB(batch_textured_bilinear_fs_sample_persp_nodual_pgxpon), // [17]
  BLOB(batch_textured_bilinear_fs_sample_persp_dual_pgxpoff), // [18]
  BLOB(batch_textured_bilinear_fs_sample_persp_dual_pgxpon), // [19]
  BLOB(batch_textured_bilinear_fs_sample_noperp_nodual_pgxpoff), // [20]
  BLOB(batch_textured_bilinear_fs_sample_noperp_nodual_pgxpon), // [21]
  BLOB(batch_textured_bilinear_fs_sample_noperp_dual_pgxpoff), // [22]
  BLOB(batch_textured_bilinear_fs_sample_noperp_dual_pgxpon), // [23]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchTexturedBilinearFragmentShaderBlob(bool msaa,
                                                                     bool per_sample_shading,
                                                                     bool noperspective_color,
                                                                     bool dual_source,
                                                                     bool pgxp_depth)
{
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned dual   = dual_source ? 1u : 0u;
  const unsigned pgxp   = pgxp_depth ? 1u : 0u;
  const unsigned index  = interp * 8u + persp * 4u + dual * 2u + pgxp;
  return k_batch_textured_bilinear_fs_blobs[index];
}

// Batch FS textured-JINC2 / JINC2BinAlpha blob table. Index encoding (same as
// Bilinear / matches GetJinc2... helper below):
//
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0)    (0..2)
//   persp  = noperspective_color ? 1 : 0                (0..1)
//   dual   = dual_source ? 1 : 0                        (0..1)
//   pgxp   = pgxp_depth ? 1 : 0                         (0..1)
//
//   index  = interp*8 + persp*4 + dual*2 + pgxp         (0..23)
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_textured_jinc2_fs_blobs[24] = {
  BLOB(batch_textured_jinc2_fs_none_persp_nodual_pgxpoff), // [ 0]
  BLOB(batch_textured_jinc2_fs_none_persp_nodual_pgxpon), // [ 1]
  BLOB(batch_textured_jinc2_fs_none_persp_dual_pgxpoff), // [ 2]
  BLOB(batch_textured_jinc2_fs_none_persp_dual_pgxpon), // [ 3]
  BLOB(batch_textured_jinc2_fs_none_noperp_nodual_pgxpoff), // [ 4]
  BLOB(batch_textured_jinc2_fs_none_noperp_nodual_pgxpon), // [ 5]
  BLOB(batch_textured_jinc2_fs_none_noperp_dual_pgxpoff), // [ 6]
  BLOB(batch_textured_jinc2_fs_none_noperp_dual_pgxpon), // [ 7]
  BLOB(batch_textured_jinc2_fs_centroid_persp_nodual_pgxpoff), // [ 8]
  BLOB(batch_textured_jinc2_fs_centroid_persp_nodual_pgxpon), // [ 9]
  BLOB(batch_textured_jinc2_fs_centroid_persp_dual_pgxpoff), // [10]
  BLOB(batch_textured_jinc2_fs_centroid_persp_dual_pgxpon), // [11]
  BLOB(batch_textured_jinc2_fs_centroid_noperp_nodual_pgxpoff), // [12]
  BLOB(batch_textured_jinc2_fs_centroid_noperp_nodual_pgxpon), // [13]
  BLOB(batch_textured_jinc2_fs_centroid_noperp_dual_pgxpoff), // [14]
  BLOB(batch_textured_jinc2_fs_centroid_noperp_dual_pgxpon), // [15]
  BLOB(batch_textured_jinc2_fs_sample_persp_nodual_pgxpoff), // [16]
  BLOB(batch_textured_jinc2_fs_sample_persp_nodual_pgxpon), // [17]
  BLOB(batch_textured_jinc2_fs_sample_persp_dual_pgxpoff), // [18]
  BLOB(batch_textured_jinc2_fs_sample_persp_dual_pgxpon), // [19]
  BLOB(batch_textured_jinc2_fs_sample_noperp_nodual_pgxpoff), // [20]
  BLOB(batch_textured_jinc2_fs_sample_noperp_nodual_pgxpon), // [21]
  BLOB(batch_textured_jinc2_fs_sample_noperp_dual_pgxpoff), // [22]
  BLOB(batch_textured_jinc2_fs_sample_noperp_dual_pgxpon), // [23]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchTexturedJINC2FragmentShaderBlob(bool msaa,
                                                                  bool per_sample_shading,
                                                                  bool noperspective_color,
                                                                  bool dual_source,
                                                                  bool pgxp_depth)
{
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned dual   = dual_source ? 1u : 0u;
  const unsigned pgxp   = pgxp_depth ? 1u : 0u;
  const unsigned index  = interp * 8u + persp * 4u + dual * 2u + pgxp;
  return k_batch_textured_jinc2_fs_blobs[index];
}

// Batch FS textured-xBR / xBRBinAlpha blob table. Index encoding (same as
// Bilinear / matches GetXbr... helper below):
//
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0)    (0..2)
//   persp  = noperspective_color ? 1 : 0                (0..1)
//   dual   = dual_source ? 1 : 0                        (0..1)
//   pgxp   = pgxp_depth ? 1 : 0                         (0..1)
//
//   index  = interp*8 + persp*4 + dual*2 + pgxp         (0..23)
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_textured_xbr_fs_blobs[24] = {
  BLOB(batch_textured_xbr_fs_none_persp_nodual_pgxpoff), // [ 0]
  BLOB(batch_textured_xbr_fs_none_persp_nodual_pgxpon), // [ 1]
  BLOB(batch_textured_xbr_fs_none_persp_dual_pgxpoff), // [ 2]
  BLOB(batch_textured_xbr_fs_none_persp_dual_pgxpon), // [ 3]
  BLOB(batch_textured_xbr_fs_none_noperp_nodual_pgxpoff), // [ 4]
  BLOB(batch_textured_xbr_fs_none_noperp_nodual_pgxpon), // [ 5]
  BLOB(batch_textured_xbr_fs_none_noperp_dual_pgxpoff), // [ 6]
  BLOB(batch_textured_xbr_fs_none_noperp_dual_pgxpon), // [ 7]
  BLOB(batch_textured_xbr_fs_centroid_persp_nodual_pgxpoff), // [ 8]
  BLOB(batch_textured_xbr_fs_centroid_persp_nodual_pgxpon), // [ 9]
  BLOB(batch_textured_xbr_fs_centroid_persp_dual_pgxpoff), // [10]
  BLOB(batch_textured_xbr_fs_centroid_persp_dual_pgxpon), // [11]
  BLOB(batch_textured_xbr_fs_centroid_noperp_nodual_pgxpoff), // [12]
  BLOB(batch_textured_xbr_fs_centroid_noperp_nodual_pgxpon), // [13]
  BLOB(batch_textured_xbr_fs_centroid_noperp_dual_pgxpoff), // [14]
  BLOB(batch_textured_xbr_fs_centroid_noperp_dual_pgxpon), // [15]
  BLOB(batch_textured_xbr_fs_sample_persp_nodual_pgxpoff), // [16]
  BLOB(batch_textured_xbr_fs_sample_persp_nodual_pgxpon), // [17]
  BLOB(batch_textured_xbr_fs_sample_persp_dual_pgxpoff), // [18]
  BLOB(batch_textured_xbr_fs_sample_persp_dual_pgxpon), // [19]
  BLOB(batch_textured_xbr_fs_sample_noperp_nodual_pgxpoff), // [20]
  BLOB(batch_textured_xbr_fs_sample_noperp_nodual_pgxpon), // [21]
  BLOB(batch_textured_xbr_fs_sample_noperp_dual_pgxpoff), // [22]
  BLOB(batch_textured_xbr_fs_sample_noperp_dual_pgxpon), // [23]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchTexturedXBRFragmentShaderBlob(bool msaa,
                                                                  bool per_sample_shading,
                                                                  bool noperspective_color,
                                                                  bool dual_source,
                                                                  bool pgxp_depth)
{
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned dual   = dual_source ? 1u : 0u;
  const unsigned pgxp   = pgxp_depth ? 1u : 0u;
  const unsigned index  = interp * 8u + persp * 4u + dual * 2u + pgxp;
  return k_batch_textured_xbr_fs_blobs[index];
}

VkShaderModule CreateShaderModule(const uint32_t* spv, size_t spv_size_bytes)
{
  if (!spv || spv_size_bytes == 0)
  {
    Log_ErrorPrint("CreateShaderModule called with empty SPIR-V blob");
    return VK_NULL_HANDLE;
  }
  if ((spv_size_bytes % sizeof(uint32_t)) != 0)
  {
    Log_ErrorPrintf("CreateShaderModule SPIR-V size %zu is not a multiple of 4", spv_size_bytes);
    return VK_NULL_HANDLE;
  }

  const VkShaderModuleCreateInfo ci = {
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    nullptr,
    0,
    spv_size_bytes,
    spv,
  };

  VkShaderModule mod = VK_NULL_HANDLE;
  VkResult res = vkCreateShaderModule(g_vulkan_context->GetDevice(), &ci, nullptr, &mod);
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkCreateShaderModule() failed for embedded SPIR-V: ");
    return VK_NULL_HANDLE;
  }
  return mod;
}

} // namespace Vulkan::EmbeddedShaders
