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

// (No extern declarations yet - this foundation patch lays down the
// directory layout, regen script, and namespace scaffolding. Subsequent
// patches add HLSL sources, generated .inc files, and the matching
// extern declarations here.)

} // namespace D3D12::EmbeddedShaders
