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
// is the API hand-off; the D3D12 side has no equivalent API call - the
// caller drops the (data, size) pair straight into a D3D12_SHADER_BYTECODE
// aggregate at PSO-creation time. So this TU stays simple: just the .inc
// includes and the namespace.
namespace D3D12::EmbeddedShaders {

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

} // namespace D3D12::EmbeddedShaders
