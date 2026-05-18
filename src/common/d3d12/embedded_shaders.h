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

} // namespace D3D12::EmbeddedShaders
