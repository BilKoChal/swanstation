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
#include "embedded_spirv/batch_vs_textured_centroid_noperp.inc"
#include "embedded_spirv/batch_vs_textured_centroid_persp.inc"
#include "embedded_spirv/batch_vs_textured_none_noperp.inc"
#include "embedded_spirv/batch_vs_textured_none_persp.inc"
#include "embedded_spirv/batch_vs_textured_sample_noperp.inc"
#include "embedded_spirv/batch_vs_textured_sample_persp.inc"
#include "embedded_spirv/batch_vs_textured_uvlim_centroid_noperp.inc"
#include "embedded_spirv/batch_vs_textured_uvlim_centroid_persp.inc"
#include "embedded_spirv/batch_vs_textured_uvlim_none_noperp.inc"
#include "embedded_spirv/batch_vs_textured_uvlim_none_persp.inc"
#include "embedded_spirv/batch_vs_textured_uvlim_sample_noperp.inc"
#include "embedded_spirv/batch_vs_textured_uvlim_sample_persp.inc"
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
//   attr   = !textured ? 0 : (uv_limits ? 2 : 1)    // 0..2
//   interp = per_sample_shading ? 2 : (msaa ? 1 : 0) // 0..2
//   persp  = noperspective_color ? 1 : 0             // 0..1
//
//   index  = attr * 6 + interp * 2 + persp           // 0..17
//
// Macro keeps the table readable and ensures the symbol and size
// stay in lock-step.
#define BLOB(name) { k_##name, k_##name##_size_bytes }
const EmbeddedShaderBlob k_batch_vs_blobs[18] = {
  // attr = 0 (untextured)
  BLOB(batch_vs_untextured_none_persp),         // [ 0] interp=none,     persp=true
  BLOB(batch_vs_untextured_none_noperp),        // [ 1] interp=none,     persp=false
  BLOB(batch_vs_untextured_centroid_persp),     // [ 2] interp=centroid, persp=true
  BLOB(batch_vs_untextured_centroid_noperp),    // [ 3] interp=centroid, persp=false
  BLOB(batch_vs_untextured_sample_persp),       // [ 4] interp=sample,   persp=true
  BLOB(batch_vs_untextured_sample_noperp),      // [ 5] interp=sample,   persp=false
  // attr = 1 (textured, no UV limits)
  BLOB(batch_vs_textured_none_persp),           // [ 6]
  BLOB(batch_vs_textured_none_noperp),          // [ 7]
  BLOB(batch_vs_textured_centroid_persp),       // [ 8]
  BLOB(batch_vs_textured_centroid_noperp),      // [ 9]
  BLOB(batch_vs_textured_sample_persp),         // [10]
  BLOB(batch_vs_textured_sample_noperp),        // [11]
  // attr = 2 (textured + UV limits)
  BLOB(batch_vs_textured_uvlim_none_persp),     // [12]
  BLOB(batch_vs_textured_uvlim_none_noperp),    // [13]
  BLOB(batch_vs_textured_uvlim_centroid_persp), // [14]
  BLOB(batch_vs_textured_uvlim_centroid_noperp),// [15]
  BLOB(batch_vs_textured_uvlim_sample_persp),   // [16]
  BLOB(batch_vs_textured_uvlim_sample_noperp),  // [17]
};
#undef BLOB

const EmbeddedShaderBlob& GetBatchVertexShaderBlob(bool textured,
                                                   bool uv_limits,
                                                   bool msaa,
                                                   bool per_sample_shading,
                                                   bool noperspective_color)
{
  const unsigned attr   = !textured ? 0u : (uv_limits ? 2u : 1u);
  const unsigned interp = per_sample_shading ? 2u : (msaa ? 1u : 0u);
  const unsigned persp  = noperspective_color ? 1u : 0u;
  const unsigned index  = attr * 6u + interp * 2u + persp;
  return k_batch_vs_blobs[index];
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
