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
#include "embedded_spirv/vram_write_ssbo_fs.inc"
#include "embedded_spirv/vram_write_texbuf_fs.inc"

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
