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

// Create a VkShaderModule directly from a pre-compiled SPIR-V blob.
//
// This intentionally bypasses Vulkan::ShaderCache: pre-baked SPIR-V is already
// final, so there is nothing to compile and nothing worth caching on disk.
// The caller owns the returned module and must vkDestroyShaderModule it.
// Returns VK_NULL_HANDLE on failure.
VkShaderModule CreateShaderModule(const uint32_t* spv, size_t spv_size_bytes);

} // namespace Vulkan::EmbeddedShaders
