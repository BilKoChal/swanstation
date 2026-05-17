// Copyright 2026 LibretroAdmin
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include "../types.h"
#include "vulkan_loader.h"
#include <cstddef>
#include <cstdint>

// Pre-compiled SPIR-V blobs for the Vulkan backend.
//
// Each blob is generated offline by tools/regen_vulkan_spirv.py from the
// matching GLSL source under data/shaders/vulkan/, and checked into
// src/common/vulkan/embedded_spirv/. Nothing in the build system invokes
// glslangValidator - the .inc files are consumed as plain C++ arrays. See
// data/shaders/vulkan/README.md for the editing workflow.
//
// This path replaces the runtime glslang -> SPIR-V step done in
// Vulkan::ShaderCompiler::CompileShader for the shaders that have been
// pre-baked. Until every shader has been pre-baked, the runtime path and
// glslang still cover the remainder.
namespace Vulkan::EmbeddedShaders {

// Screen-quad vertex shader (fullscreen-triangle helper used by VRAM ops,
// display, downsample, etc.). Equivalent to
// ShaderGen::GenerateScreenQuadVertexShader() in Vulkan mode.
//
// The array is sized in the .cpp; consumers see only the bare extern.
extern const uint32_t k_screen_quad_vs[];
extern const size_t k_screen_quad_vs_size_bytes;

// UV-quad vertex shader. Variant of the screen-quad that maps the swept
// triangle into a configurable [u_uv_min..u_uv_max] UV rect via push
// constants. Equivalent to ShaderGen::GenerateUVQuadVertexShader() in
// Vulkan mode.
extern const uint32_t k_uv_quad_vs[];
extern const size_t k_uv_quad_vs_size_bytes;

// Presentation-stage fullscreen-quad VS. Distinct from the GPU_HW screen
// quad above - has a u_src_rect push constant. Used only by
// LibretroVulkanHostDisplay to blit the rendered frame (and the software
// cursor) into the libretro framebuffer.
extern const uint32_t k_present_fullscreen_vs[];
extern const size_t k_present_fullscreen_vs_size_bytes;

// Presentation-stage display FS. Writes the source texture opaque.
extern const uint32_t k_present_display_fs[];
extern const size_t k_present_display_fs_size_bytes;

// Presentation-stage cursor FS. Preserves source alpha for blending.
extern const uint32_t k_present_cursor_fs[];
extern const size_t k_present_cursor_fs_size_bytes;

// Adaptive-downsample blur FS. Single-channel 3x3-ish smoothing of the
// energy texture produced by the mip FS. No specialization constants.
extern const uint32_t k_adaptive_downsample_blur_fs[];
extern const size_t k_adaptive_downsample_blur_fs_size_bytes;

// Adaptive-downsample composite FS. Resolves the mip pyramid into the
// final downsampled output using the per-pixel bias from the blur pass.
// RESOLUTION_SCALE spec constant at id=0; reads two textures at bindings
// 1 (samp0, color pyramid) and 2 (samp1, bias).
extern const uint32_t k_adaptive_downsample_composite_fs[];
extern const size_t k_adaptive_downsample_composite_fs_size_bytes;

// Adaptive-downsample mip FS. One blob serves both the first-pass
// (FIRST_PASS=true: samples a vec3 color texture) and the subsequent mid
// passes (FIRST_PASS=false: feeds back vec4 from a prior mip). FIRST_PASS
// is a bool specialization constant at id=100 supplied at pipeline-
// creation time via Vulkan::SpecConstants.
extern const uint32_t k_adaptive_downsample_mip_fs[];
extern const size_t k_adaptive_downsample_mip_fs_size_bytes;

// Box-sample downsample FS. Averages a RESOLUTION_SCALE x RESOLUTION_SCALE
// block of upscaled texels back to PSX-native resolution. Single sampler
// at binding 1. RESOLUTION_SCALE spec constant at id=0.
extern const uint32_t k_box_sample_downsample_fs[];
extern const size_t k_box_sample_downsample_fs_size_bytes;

// VRAM fill FS. One blob, four-to-eight specialisations:
//   id =   3 PGXP_DEPTH (bool)  - common-knob convention.
//   id = 100 INTERLACED (bool)  - shader-specific.
//   id = 101 WRAPPED    (bool)  - shader-specific.
// No texture binding; reads only its push constant block.
extern const uint32_t k_vram_fill_fs[];
extern const size_t k_vram_fill_fs_size_bytes;

// VRAM copy FS. Single SPIR-V blob; the only non-shader knob (depth_test)
// is a pipeline-state toggle, not a spec constant. Two spec constants:
//   id = 0 RESOLUTION_SCALE (uint) - drives VRAM_SIZE in wrap-around math.
//   id = 3 PGXP_DEPTH       (bool) - depth source selection.
// Single sampler at binding 1 (non-MSAA only; see shadergen TODO).
extern const uint32_t k_vram_copy_fs[];
extern const size_t k_vram_copy_fs_size_bytes;

// VRAM write FS. Two blobs - the SSBO vs uniform-texel-buffer split is a
// structural variant (different descriptor type at binding 0:
// STORAGE_BUFFER vs UNIFORM_TEXEL_BUFFER) so it cannot fold into a spec
// constant. The C++ side selects between them based on
// GPU_HW::m_use_ssbos_for_vram_writes. Both blobs share the same spec
// constants:
//   id = 0 RESOLUTION_SCALE (uint) - native-coord downscale + VRAM_SIZE.
//   id = 3 PGXP_DEPTH       (bool) - depth source selection.
extern const uint32_t k_vram_write_ssbo_fs[];
extern const size_t k_vram_write_ssbo_fs_size_bytes;
extern const uint32_t k_vram_write_texbuf_fs[];
extern const size_t k_vram_write_texbuf_fs_size_bytes;


// Create a VkShaderModule directly from a pre-compiled SPIR-V blob.
//
// This intentionally bypasses Vulkan::ShaderCache: pre-baked SPIR-V is already
// final, so there is nothing to compile and nothing worth caching on disk.
// The caller owns the returned module and must vkDestroyShaderModule it.
// Returns VK_NULL_HANDLE on failure.
VkShaderModule CreateShaderModule(const uint32_t* spv, size_t spv_size_bytes);

} // namespace Vulkan::EmbeddedShaders
