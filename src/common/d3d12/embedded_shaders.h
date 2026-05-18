// Copyright 2026 LibretroAdmin
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include <cstddef>
#include <cstdint>

// Pre-compiled DXBC blobs for the D3D12 backend.
//
// Each blob is generated offline by tools/regen_d3d12_dxbc.py from the
// matching HLSL source under data/shaders/d3d12/, and checked into
// src/common/d3d12/embedded_dxbc/. Nothing in the build system invokes
// fxc.exe / D3DCompile - the .inc files are consumed as plain C++ arrays.
// See data/shaders/d3d12/README.md for the editing workflow.
//
// This path replaces the runtime HLSL -> DXBC step done in
// D3D11::ShaderCompiler::CompileShader (shared between the D3D11 and D3D12
// backends) for the shaders that have been pre-baked. Until every shader
// has been pre-baked, the runtime path and D3DCompile still cover the
// remainder.
//
// Mirror of src/common/vulkan/embedded_shaders.h for the Vulkan backend.
// As individual shaders get migrated off the runtime ShaderGen path, this
// header gains a pair of extern declarations per blob:
//
//   extern const uint8_t k_<stem>_ps[];
//   extern const size_t k_<stem>_ps_size_bytes;
//
// (or _vs / _cs / _gs depending on stage). Blob variants with /D defines
// get a suffix per the TEMPLATE_VARIANTS table in
// tools/regen_d3d12_dxbc.py.
//
// Constructing a D3D12_SHADER_BYTECODE at the PSO creation site is a
// direct aggregate-initialiser - no wrapper helper needed:
//
//   D3D12_SHADER_BYTECODE bc = { k_<stem>_ps, k_<stem>_ps_size_bytes };
//
namespace D3D12::EmbeddedShaders {

// Fullscreen-quad vertex shader. Emits a fullscreen triangle in NDC
// from SV_VertexID 0..2 via the standard bit-shift trick - equivalent
// to ShaderGen::GenerateScreenQuadVertexShader() in D3D12 mode. Used
// by every non-batch pipeline in the D3D12 backend (vram_fill /
// vram_copy / vram_write / vram_update_depth / vram_readback /
// display / copy). Zero state dependency, so a single pre-baked
// variant covers all call sites.
//
// Source: data/shaders/d3d12/fullscreen_quad.vs.hlsl
extern const uint8_t k_fullscreen_quad_vs[];
extern const size_t k_fullscreen_quad_vs_size_bytes;

// Copy/blit pixel shader. Equivalent to
// ShaderGen::GenerateCopyFragmentShader() in D3D12 mode. Single
// texture sample with a cbuffer-driven source rect; zero state
// dependency (no MULTISAMPLING split, no RESOLUTION_SCALE bake-in,
// no preprocessor variant axes). Used by GetCopyPipeline for full-
// frame blits (presentation copies, downscaling).
//
// Source: data/shaders/d3d12/copy.ps.hlsl
extern const uint8_t k_copy_ps[];
extern const size_t k_copy_ps_size_bytes;

// VRAM-to-VRAM copy pixel shader. Equivalent to
// GPU_HW_ShaderGen::GenerateVRAMCopyFragmentShader() in D3D12 mode.
// Used by GPU_HW_D3D12::GetVRAMCopyPipeline for the VRAM-to-VRAM
// copy fast path (sprite caching, text rendering, menu-system blits).
// Two variants on the PGXP_DEPTH on/off axis - select between them
// based on the runtime m_pgxp_depth_buffer state. The cbuffer carries
// u_resolution_scale (post-e56d4d4 refactor), so the same DXBC serves
// every resolution-scale value.
//
// Source: data/shaders/d3d12/vram_copy.ps.hlsl
extern const uint8_t k_vram_copy_ps_pgxp0[];
extern const size_t k_vram_copy_ps_pgxp0_size_bytes;
extern const uint8_t k_vram_copy_ps_pgxp1[];
extern const size_t k_vram_copy_ps_pgxp1_size_bytes;

// VRAM write pixel shader. Equivalent to
// GPU_HW_ShaderGen::GenerateVRAMWriteFragmentShader(false) in D3D12
// mode. Used by GPU_HW_D3D12::GetVRAMWritePipeline for the CPU->VRAM
// upload path - hit constantly on every game's framebuffer streaming
// path (pre-rendered backgrounds, FMV staging, sprite-page reloads).
// Two variants on PGXP_DEPTH; the shadergen's `use_ssbo` parameter is
// GLSL/Vulkan-only, so for D3D12 only the texture-buffer source path
// gets pre-baked. u_resolution_scale lives in the cbuffer (post-
// 9d2b49d), so one blob per PGXP value serves every resolution scale.
//
// Source: data/shaders/d3d12/vram_write.ps.hlsl
extern const uint8_t k_vram_write_ps_pgxp0[];
extern const size_t k_vram_write_ps_pgxp0_size_bytes;
extern const uint8_t k_vram_write_ps_pgxp1[];
extern const size_t k_vram_write_ps_pgxp1_size_bytes;

// VRAM fill pixel shader. Equivalent to
// GPU_HW_ShaderGen::GenerateVRAMFillFragmentShader(wrapped, interlaced)
// in D3D12 mode. Used by GPU_HW_D3D12::GetVRAMFillPipeline for the
// screen-clear / FillVRAM path. 8 variants total, indexed by the
// product of PGXP_DEPTH x WRAPPED x INTERLACED at PSO selection time;
// the variant suffix encodes those three bits as p{0,1}w{0,1}i{0,1}.
// The runtime call site builds a 2 x 2 x 2 indexed lookup table
// from these externs and picks the right blob using
// (m_pgxp_depth_buffer, wrapped, interlaced).
//
// No u_resolution_scale dependency in the shader body for D3D12
// (fixYCoord is identity on HLSL; only the OpenGL backend's
// shadergen body references VRAM_SIZE here for the y-flip). So no
// scale-refactor was needed before pre-baking.
//
// Source: data/shaders/d3d12/vram_fill.ps.hlsl
extern const uint8_t k_vram_fill_ps_p0w0i0[];
extern const size_t k_vram_fill_ps_p0w0i0_size_bytes;
extern const uint8_t k_vram_fill_ps_p0w0i1[];
extern const size_t k_vram_fill_ps_p0w0i1_size_bytes;
extern const uint8_t k_vram_fill_ps_p0w1i0[];
extern const size_t k_vram_fill_ps_p0w1i0_size_bytes;
extern const uint8_t k_vram_fill_ps_p0w1i1[];
extern const size_t k_vram_fill_ps_p0w1i1_size_bytes;
extern const uint8_t k_vram_fill_ps_p1w0i0[];
extern const size_t k_vram_fill_ps_p1w0i0_size_bytes;
extern const uint8_t k_vram_fill_ps_p1w0i1[];
extern const size_t k_vram_fill_ps_p1w0i1_size_bytes;
extern const uint8_t k_vram_fill_ps_p1w1i0[];
extern const size_t k_vram_fill_ps_p1w1i0_size_bytes;
extern const uint8_t k_vram_fill_ps_p1w1i1[];
extern const size_t k_vram_fill_ps_p1w1i1_size_bytes;

// VRAM update-depth pixel shader. Equivalent to
// GPU_HW_ShaderGen::GenerateVRAMUpdateDepthFragmentShader() in D3D12
// mode. Used by GPU_HW_D3D12::GetVRAMUpdateDepthPipeline for the
// depth-only pass that propagates colour-texture alpha (the PSX
// mask bit) into the depth buffer after every CPU->VRAM upload.
//
// First MSAA texture-binding variant. The msaa0 / msaa1 blobs
// differ in the binding type (Texture2D vs Texture2DMS<float4>),
// the body Load form, and the presence of SV_SampleIndex in the
// entry-point signature. Runtime selection uses m_multisamples > 1
// (same predicate as the shadergen UsingMSAA() helper) so the
// shader's per-sample-shading expectation matches the PSO's MSAA
// configuration.
//
// Source: data/shaders/d3d12/vram_update_depth.ps.hlsl
extern const uint8_t k_vram_update_depth_ps_msaa0[];
extern const size_t k_vram_update_depth_ps_msaa0_size_bytes;
extern const uint8_t k_vram_update_depth_ps_msaa1[];
extern const size_t k_vram_update_depth_ps_msaa1_size_bytes;

} // namespace D3D12::EmbeddedShaders
