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

// (No .inc includes yet - this foundation patch lays down the storage TU.
// Subsequent patches add HLSL sources under data/shaders/d3d12/ and the
// matching generated .inc under src/common/d3d12/embedded_dxbc/, each
// included from inside this namespace block.)

} // namespace D3D12::EmbeddedShaders
