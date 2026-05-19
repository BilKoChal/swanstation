// Copyright 2026 LibretroAdmin
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include <cstddef>
#include <cstdint>

// Pre-compiled DXBC blobs shared between the D3D11 and D3D12 backends.
//
// Each blob is generated offline by tools/regen_d3d12_dxbc.py from the
// matching HLSL source under data/shaders/d3d12/, and checked into
// src/common/d3d_common/embedded_dxbc/. Nothing in the build system
// invokes fxc.exe / D3DCompile - the .inc files are consumed as plain
// C++ arrays. See data/shaders/d3d12/README.md for the editing
// workflow.
//
// fxc emits DXBC bytecode at the ps_5_0 / vs_5_0 / cs_5_0 / gs_5_0
// targets that both D3D11 (CreatePixelShader from raw bytecode, same
// signature as src/common/display.hlsl.h's pre-baked precedent) and
// D3D12 (ID3D12PipelineState's D3D12_SHADER_BYTECODE aggregate)
// consume identically. The HLSL in data/shaders/d3d12/ uses no
// API-version-specific constructs - register binding declarations
// (cbuffer at b0, Texture2D at t0, SamplerState at s0) are honoured
// by both backends and live in the DXBC reflection metadata.
//
// This path replaces the runtime HLSL -> DXBC step done in
// D3D11::ShaderCompiler::CompileShader (shared between the D3D11 and
// D3D12 backends) for the shaders that have been pre-baked. Until
// every shader has been pre-baked, the runtime path and D3DCompile
// still cover the remainder.
//
// Mirror of src/common/vulkan/embedded_shaders.h for the Vulkan
// backend. As individual shaders get migrated off the runtime
// ShaderGen path, this header gains a pair of extern declarations
// per blob:
//
//   extern const uint8_t k_<stem>_ps[];
//   extern const size_t k_<stem>_ps_size_bytes;
//
// (or _vs / _cs / _gs depending on stage). Blob variants with /D
// defines get a suffix per the TEMPLATE_VARIANTS table in
// tools/regen_d3d12_dxbc.py.
//
// Constructing a D3D12_SHADER_BYTECODE at the PSO creation site is a
// direct aggregate-initialiser - no wrapper helper needed:
//
//   D3D12_SHADER_BYTECODE bc = { k_<stem>_ps, k_<stem>_ps_size_bytes };
//
// On D3D11, the matching call is
// ID3D11Device::CreatePixelShader(bc.pShaderBytecode, bc.BytecodeLength,
// nullptr, ...), wrapped by
// D3D11::ShaderCompiler::CreatePixelShader(device, bytecode, size).
//
namespace D3DCommon::EmbeddedShaders {

// Backend-neutral (data, size) aggregate for the pre-baked DXBC
// blobs. Both backends consume the same byte stream but wrap it
// differently at the API hand-off:
//   D3D12: D3D12_SHADER_BYTECODE { pShaderBytecode, BytecodeLength }
//          passed to GraphicsPipelineBuilder::SetPixelShader at PSO
//          creation time.
//   D3D11: ID3D11Device::CreatePixelShader(bytecode, size, nullptr,
//          &out) wrapped by D3D11::ShaderCompiler::CreatePixelShader.
//
// Picker functions below return this struct. Each caller converts
// to the native API type at the call site - we deliberately don't
// pull d3d12.h or d3d11.h into this header.
struct Bytecode {
  const uint8_t* data;
  size_t size;
};

// ---- Batch FS pre-bake pickers -------------------------------------
//
// Each picker covers one slice of the batch FS variant matrix.
// Mirror of the runtime shadergen output for the same slice
// (texture_mode + filter combo). Inputs are the 4 session-level
// flags that the pre-baked variant matrix is indexed by:
//
//   use_dual_source            from the shadergen formula
//                              m_supports_dual_source_blend &&
//                              ((render_mode != TransparencyDisabled
//                                && render_mode != OnlyOpaque) ||
//                               filter != Nearest)
//                              Same bit drives the PSO blend state's
//                              SRC1_* references; pre-compute at the
//                              caller and pass both consumers.
//   multisamples               raw m_multisamples (1 / 2 / 4 / 8 /
//                              16 / 32). The picker collapses to
//                              (multisamples > 1) for the interp
//                              qualifier choice.
//   per_sample_shading         raw m_per_sample_shading. Wins over
//                              the multisamples-driven centroid
//                              qualifier when set.
//   disable_color_perspective  raw m_disable_color_perspective.
//                              Selects the noperspective variant.
//
// The textured-Nearest picker additionally takes lookup_mode (the
// post-Reserved-dedup texture_mode value, 0/1/2/4/5/6 reachable).
// The Reserved_Direct16Bit / Reserved_RawDirect16Bit dedup must be
// applied by the caller before invoking; the picker assumes 3 and
// 7 are unreachable.

// Untextured batch FS variant picker. Selects from the 12 blobs at
// embedded_dxbc/batch_untextured_ps_*.inc.
Bytecode PickBatchUntexturedFS(bool use_dual_source, uint32_t multisamples,
                                bool per_sample_shading, bool disable_color_perspective);

// Textured + Nearest-filter batch FS variant picker. Selects from
// the 72 blobs at embedded_dxbc/batch_textured_nearest_ps_*.inc.
Bytecode PickBatchTexturedNearestFS(uint8_t lookup_mode, bool use_dual_source,
                                     uint32_t multisamples, bool per_sample_shading,
                                     bool disable_color_perspective);

// --------------------------------------------------------------------

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

// VRAM read pixel shader. Equivalent to
// GPU_HW_ShaderGen::GenerateVRAMReadFragmentShader() in D3D12
// mode post-2980961 cbuffer routing. Used by
// GPU_HW_D3D12::GetVRAMReadbackPipeline for the upscaled-VRAM-
// to-native-16bpp encode pass that backs screenshot capture,
// libretro readback, and save-state staging.
//
// First MSAA-count cardinality variant. Six values of
// MULTISAMPLES (1, 2, 4, 8, 16, 32). m1 takes the Texture2D
// path with a single Load; m2..m32 use Texture2DMS<float4>
// with an [unroll] loop over the sample count - each value
// produces a different DXBC because the unroll count varies.
// Body size scales linearly with the unroll: m1 = 3.6 KB,
// m32 = 12.6 KB. Total embedded bytecode footprint ~ 38 KB
// across the 6 blobs.
//
// Runtime selection in GetVRAMReadbackPipeline uses a switch
// on m_multisamples - values outside {1, 2, 4, 8, 16, 32}
// shouldn't occur in practice (GPU drivers only expose
// power-of-2 MSAA counts as having quality levels > 0, and
// the UI dropdown is restricted to those values) but the
// switch falls back to the m1 blob with a warn log if one
// ever does.
//
// Source: data/shaders/d3d12/vram_read.ps.hlsl
extern const uint8_t k_vram_read_ps_m1[];
extern const size_t k_vram_read_ps_m1_size_bytes;
extern const uint8_t k_vram_read_ps_m2[];
extern const size_t k_vram_read_ps_m2_size_bytes;
extern const uint8_t k_vram_read_ps_m4[];
extern const size_t k_vram_read_ps_m4_size_bytes;
extern const uint8_t k_vram_read_ps_m8[];
extern const size_t k_vram_read_ps_m8_size_bytes;
extern const uint8_t k_vram_read_ps_m16[];
extern const size_t k_vram_read_ps_m16_size_bytes;
extern const uint8_t k_vram_read_ps_m32[];
extern const size_t k_vram_read_ps_m32_size_bytes;

// Display pixel shader. Equivalent to
// GPU_HW_ShaderGen::GenerateDisplayFragmentShader(depth_24bit,
// interlace_mode, smooth_chroma) in D3D12 mode post-e64fc28
// cbuffer routing. Used by GPU_HW_D3D12::GetDisplayPipeline for
// the every-frame scanout pass - the per-frame hot shader.
//
// 54 variants total across 4 axes:
//   DEPTH_24BIT (2) x interlace_mode (3) x SMOOTH_CHROMA (2,
//   collapsed to 1 when DEPTH_24BIT=0 since chroma is dead code
//   there) x MULTISAMPLES (6: 1/2/4/8/16/32).
//
// Variant suffix convention: d{0,1}i{0,1,2}c{0,1}m{01,02,04,08,
// 16,32}. interlace_mode maps to the (INTERLACED, INTERLEAVED)
// bool pair: i0=(0,0), i1=(1,1), i2=(1,0). The d=0 set has 18
// entries (no chroma dim); d=1 has 36 (both chroma values).
//
// Runtime selection in GetDisplayPipeline picks via two arrays:
//   k_display_d0[interlace_mode][ms_idx]                  (3 x 6)
//   k_display_d1[interlace_mode][smooth_chroma][ms_idx]   (3 x 2 x 6)
// so the chroma dimension is structurally absent on the d=0
// path. ms_idx is log2(m_multisamples), or 0 fallback for
// unexpected values (the libretro UI dropdown only exposes
// power-of-2 values and the GPU-driver capability detection
// only reports power-of-2 counts as supported, so the fallback
// shouldn't fire in practice).
//
// Source: data/shaders/d3d12/display.ps.hlsl
extern const uint8_t k_display_ps_d0i0c0m01[];
extern const size_t k_display_ps_d0i0c0m01_size_bytes;
extern const uint8_t k_display_ps_d0i0c0m02[];
extern const size_t k_display_ps_d0i0c0m02_size_bytes;
extern const uint8_t k_display_ps_d0i0c0m04[];
extern const size_t k_display_ps_d0i0c0m04_size_bytes;
extern const uint8_t k_display_ps_d0i0c0m08[];
extern const size_t k_display_ps_d0i0c0m08_size_bytes;
extern const uint8_t k_display_ps_d0i0c0m16[];
extern const size_t k_display_ps_d0i0c0m16_size_bytes;
extern const uint8_t k_display_ps_d0i0c0m32[];
extern const size_t k_display_ps_d0i0c0m32_size_bytes;
extern const uint8_t k_display_ps_d0i1c0m01[];
extern const size_t k_display_ps_d0i1c0m01_size_bytes;
extern const uint8_t k_display_ps_d0i1c0m02[];
extern const size_t k_display_ps_d0i1c0m02_size_bytes;
extern const uint8_t k_display_ps_d0i1c0m04[];
extern const size_t k_display_ps_d0i1c0m04_size_bytes;
extern const uint8_t k_display_ps_d0i1c0m08[];
extern const size_t k_display_ps_d0i1c0m08_size_bytes;
extern const uint8_t k_display_ps_d0i1c0m16[];
extern const size_t k_display_ps_d0i1c0m16_size_bytes;
extern const uint8_t k_display_ps_d0i1c0m32[];
extern const size_t k_display_ps_d0i1c0m32_size_bytes;
extern const uint8_t k_display_ps_d0i2c0m01[];
extern const size_t k_display_ps_d0i2c0m01_size_bytes;
extern const uint8_t k_display_ps_d0i2c0m02[];
extern const size_t k_display_ps_d0i2c0m02_size_bytes;
extern const uint8_t k_display_ps_d0i2c0m04[];
extern const size_t k_display_ps_d0i2c0m04_size_bytes;
extern const uint8_t k_display_ps_d0i2c0m08[];
extern const size_t k_display_ps_d0i2c0m08_size_bytes;
extern const uint8_t k_display_ps_d0i2c0m16[];
extern const size_t k_display_ps_d0i2c0m16_size_bytes;
extern const uint8_t k_display_ps_d0i2c0m32[];
extern const size_t k_display_ps_d0i2c0m32_size_bytes;
extern const uint8_t k_display_ps_d1i0c0m01[];
extern const size_t k_display_ps_d1i0c0m01_size_bytes;
extern const uint8_t k_display_ps_d1i0c0m02[];
extern const size_t k_display_ps_d1i0c0m02_size_bytes;
extern const uint8_t k_display_ps_d1i0c0m04[];
extern const size_t k_display_ps_d1i0c0m04_size_bytes;
extern const uint8_t k_display_ps_d1i0c0m08[];
extern const size_t k_display_ps_d1i0c0m08_size_bytes;
extern const uint8_t k_display_ps_d1i0c0m16[];
extern const size_t k_display_ps_d1i0c0m16_size_bytes;
extern const uint8_t k_display_ps_d1i0c0m32[];
extern const size_t k_display_ps_d1i0c0m32_size_bytes;
extern const uint8_t k_display_ps_d1i0c1m01[];
extern const size_t k_display_ps_d1i0c1m01_size_bytes;
extern const uint8_t k_display_ps_d1i0c1m02[];
extern const size_t k_display_ps_d1i0c1m02_size_bytes;
extern const uint8_t k_display_ps_d1i0c1m04[];
extern const size_t k_display_ps_d1i0c1m04_size_bytes;
extern const uint8_t k_display_ps_d1i0c1m08[];
extern const size_t k_display_ps_d1i0c1m08_size_bytes;
extern const uint8_t k_display_ps_d1i0c1m16[];
extern const size_t k_display_ps_d1i0c1m16_size_bytes;
extern const uint8_t k_display_ps_d1i0c1m32[];
extern const size_t k_display_ps_d1i0c1m32_size_bytes;
extern const uint8_t k_display_ps_d1i1c0m01[];
extern const size_t k_display_ps_d1i1c0m01_size_bytes;
extern const uint8_t k_display_ps_d1i1c0m02[];
extern const size_t k_display_ps_d1i1c0m02_size_bytes;
extern const uint8_t k_display_ps_d1i1c0m04[];
extern const size_t k_display_ps_d1i1c0m04_size_bytes;
extern const uint8_t k_display_ps_d1i1c0m08[];
extern const size_t k_display_ps_d1i1c0m08_size_bytes;
extern const uint8_t k_display_ps_d1i1c0m16[];
extern const size_t k_display_ps_d1i1c0m16_size_bytes;
extern const uint8_t k_display_ps_d1i1c0m32[];
extern const size_t k_display_ps_d1i1c0m32_size_bytes;
extern const uint8_t k_display_ps_d1i1c1m01[];
extern const size_t k_display_ps_d1i1c1m01_size_bytes;
extern const uint8_t k_display_ps_d1i1c1m02[];
extern const size_t k_display_ps_d1i1c1m02_size_bytes;
extern const uint8_t k_display_ps_d1i1c1m04[];
extern const size_t k_display_ps_d1i1c1m04_size_bytes;
extern const uint8_t k_display_ps_d1i1c1m08[];
extern const size_t k_display_ps_d1i1c1m08_size_bytes;
extern const uint8_t k_display_ps_d1i1c1m16[];
extern const size_t k_display_ps_d1i1c1m16_size_bytes;
extern const uint8_t k_display_ps_d1i1c1m32[];
extern const size_t k_display_ps_d1i1c1m32_size_bytes;
extern const uint8_t k_display_ps_d1i2c0m01[];
extern const size_t k_display_ps_d1i2c0m01_size_bytes;
extern const uint8_t k_display_ps_d1i2c0m02[];
extern const size_t k_display_ps_d1i2c0m02_size_bytes;
extern const uint8_t k_display_ps_d1i2c0m04[];
extern const size_t k_display_ps_d1i2c0m04_size_bytes;
extern const uint8_t k_display_ps_d1i2c0m08[];
extern const size_t k_display_ps_d1i2c0m08_size_bytes;
extern const uint8_t k_display_ps_d1i2c0m16[];
extern const size_t k_display_ps_d1i2c0m16_size_bytes;
extern const uint8_t k_display_ps_d1i2c0m32[];
extern const size_t k_display_ps_d1i2c0m32_size_bytes;
extern const uint8_t k_display_ps_d1i2c1m01[];
extern const size_t k_display_ps_d1i2c1m01_size_bytes;
extern const uint8_t k_display_ps_d1i2c1m02[];
extern const size_t k_display_ps_d1i2c1m02_size_bytes;
extern const uint8_t k_display_ps_d1i2c1m04[];
extern const size_t k_display_ps_d1i2c1m04_size_bytes;
extern const uint8_t k_display_ps_d1i2c1m08[];
extern const size_t k_display_ps_d1i2c1m08_size_bytes;
extern const uint8_t k_display_ps_d1i2c1m16[];
extern const size_t k_display_ps_d1i2c1m16_size_bytes;
extern const uint8_t k_display_ps_d1i2c1m32[];
extern const size_t k_display_ps_d1i2c1m32_size_bytes;

// Batch fragment shader, untextured slice. Equivalent to
// GPU_HW_ShaderGen::GenerateBatchFragmentShader invoked with
// texture_mode == GPUTextureMode::Disabled. Used by
// GPU_HW_D3D12::GetBatchPipeline's untextured branch in place of
// the runtime shadergen + D3DCompile path for the same slice. First
// batch FS template to be pre-baked; the four textured filter
// templates (Nearest / Bilinear / JINC2 / xBR) land in subsequent
// commits.
//
// Variant axes (4, all `-D` to fxc; post-c532a34 TRANSPARENCY-routing):
//
//   * USE_DUAL_SOURCE (0/1):  controls o_col1 declaration.
//   * INTERP_CENTROID:        emits `centroid` qualifier on inputs.
//   * INTERP_SAMPLE:          emits `sample` qualifier (wins over
//                             INTERP_CENTROID if both set; we never
//                             emit a variant where both are 1).
//   * NOPERSP (0/1):          adds `noperspective` to v_col0.
//
// 2 (dual) x 3 (interp: none/centroid/sample) x 2 (persp) = 12 blobs.
// Was 24 pre-c532a34 - the former TRANSPARENCY axis collapsed onto
// the runtime branch on u_render_mode at the body's premultiply and
// dual_source o_col1 sites. MSAA cardinality does not multiply this
// slice (untextured FS has no LOAD_TEXTURE_MS unroll); the
// interpolation qualifier captures the relevant MSAA contribution
// via INTERP_CENTROID/INTERP_SAMPLE.
//
// Runtime selection at the GetBatchPipeline call site:
//   dual_idx         = use_dual_source ? 1 : 0       (mirror of
//                       shadergen formula: m_supports_dual_source_blend &&
//                       ((transparency != Disabled && transparency != OnlyOpaque) ||
//                        m_texture_filter != Nearest))
//   interp_idx       = m_per_sample_shading ? 2 :
//                      (m_multisamples > 1 ? 1 : 0)
//   persp_idx        = m_disable_color_perspective ? 1 : 0
//
// Source: data/shaders/d3d12/batch_untextured.ps.hlsl
extern const uint8_t k_batch_untextured_ps_d0_none_p0[];
extern const size_t k_batch_untextured_ps_d0_none_p0_size_bytes;
extern const uint8_t k_batch_untextured_ps_d0_none_p1[];
extern const size_t k_batch_untextured_ps_d0_none_p1_size_bytes;
extern const uint8_t k_batch_untextured_ps_d0_centroid_p0[];
extern const size_t k_batch_untextured_ps_d0_centroid_p0_size_bytes;
extern const uint8_t k_batch_untextured_ps_d0_centroid_p1[];
extern const size_t k_batch_untextured_ps_d0_centroid_p1_size_bytes;
extern const uint8_t k_batch_untextured_ps_d0_sample_p0[];
extern const size_t k_batch_untextured_ps_d0_sample_p0_size_bytes;
extern const uint8_t k_batch_untextured_ps_d0_sample_p1[];
extern const size_t k_batch_untextured_ps_d0_sample_p1_size_bytes;
extern const uint8_t k_batch_untextured_ps_d1_none_p0[];
extern const size_t k_batch_untextured_ps_d1_none_p0_size_bytes;
extern const uint8_t k_batch_untextured_ps_d1_none_p1[];
extern const size_t k_batch_untextured_ps_d1_none_p1_size_bytes;
extern const uint8_t k_batch_untextured_ps_d1_centroid_p0[];
extern const size_t k_batch_untextured_ps_d1_centroid_p0_size_bytes;
extern const uint8_t k_batch_untextured_ps_d1_centroid_p1[];
extern const size_t k_batch_untextured_ps_d1_centroid_p1_size_bytes;
extern const uint8_t k_batch_untextured_ps_d1_sample_p0[];
extern const size_t k_batch_untextured_ps_d1_sample_p0_size_bytes;
extern const uint8_t k_batch_untextured_ps_d1_sample_p1[];
extern const size_t k_batch_untextured_ps_d1_sample_p1_size_bytes;

// Batch fragment shader, textured + Nearest-filter slice. Equivalent
// to GPU_HW_ShaderGen::GenerateBatchFragmentShader invoked with
// texture_mode != GPUTextureMode::Disabled AND m_texture_filter ==
// GPUTextureFilter::Nearest. Used by GPU_HW_D3D12::GetBatchPipeline's
// textured-Nearest branch in place of the runtime shadergen +
// D3DCompile path for the same slice. Second batch FS template
// pre-baked, after batch_untextured. The remaining three textured
// filter templates (Bilinear / JINC2 / xBR) land in subsequent
// commits.
//
// Variant axes (4 dimensions, 6+2+3+2 = 72 blobs):
//
//   * Texture mode (6 combos via PALETTE_4_BIT / PALETTE_8_BIT /
//     RAW_TEXTURE -D macros). Palette4Bit / Palette8Bit are mutually
//     exclusive in the shadergen #elif chain - no variant has both
//     set. Reserved_Direct16Bit / Reserved_RawDirect16Bit dedup to
//     Direct16Bit / RawDirect16Bit at the C++ picker level.
//   * USE_DUAL_SOURCE (0/1)
//   * INTERP_CENTROID / INTERP_SAMPLE (mutually exclusive tri-state)
//   * NOPERSP (0/1)
//
// 6 (tex_mode) x 2 (dual) x 3 (interp) x 2 (persp) = 72 blobs.
// MSAA cardinality does not multiply this slice - the batch FS reads
// from the single-sample shadow VRAM regardless of m_multisamples.
//
// Runtime selection at the GetBatchPipeline call site:
//   tm_idx           = derived from lookup_mode (Reserved_* deduped):
//                       0: p0r0 (Direct16Bit)
//                       1: p0r1 (RawDirect16Bit)
//                       2: p4r0 (Palette4Bit)
//                       3: p4r1 (RawPalette4Bit)
//                       4: p8r0 (Palette8Bit)
//                       5: p8r1 (RawPalette8Bit)
//   dual_idx         = use_dual_source ? 1 : 0
//   interp_idx       = m_per_sample_shading ? 2 :
//                      (m_multisamples > 1 ? 1 : 0)
//   persp_idx        = m_disable_color_perspective ? 1 : 0
//
// Source: data/shaders/d3d12/batch_textured_nearest.ps.hlsl
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d0_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d0_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d0_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d0_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d0_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d0_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d0_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d0_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d0_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d0_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d0_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d0_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d1_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d1_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d1_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d1_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d1_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d1_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d1_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d1_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d1_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d1_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r0_d1_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r0_d1_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d0_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d0_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d0_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d0_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d0_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d0_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d0_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d0_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d0_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d0_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d0_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d0_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d1_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d1_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d1_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d1_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d1_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d1_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d1_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d1_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d1_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d1_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p0r1_d1_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p0r1_d1_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d0_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d0_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d0_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d0_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d0_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d0_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d0_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d0_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d0_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d0_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d0_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d0_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d1_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d1_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d1_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d1_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d1_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d1_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d1_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d1_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d1_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d1_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r0_d1_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r0_d1_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d0_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d0_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d0_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d0_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d0_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d0_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d0_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d0_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d0_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d0_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d0_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d0_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d1_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d1_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d1_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d1_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d1_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d1_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d1_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d1_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d1_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d1_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p4r1_d1_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p4r1_d1_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d0_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d0_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d0_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d0_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d0_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d0_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d0_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d0_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d0_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d0_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d0_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d0_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d1_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d1_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d1_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d1_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d1_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d1_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d1_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d1_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d1_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d1_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r0_d1_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r0_d1_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d0_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d0_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d0_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d0_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d0_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d0_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d0_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d0_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d0_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d0_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d0_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d0_sample_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d1_centroid_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d1_centroid_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d1_centroid_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d1_centroid_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d1_none_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d1_none_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d1_none_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d1_none_n1_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d1_sample_n0[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d1_sample_n0_size_bytes;
extern const uint8_t k_batch_textured_nearest_ps_p8r1_d1_sample_n1[];
extern const size_t k_batch_textured_nearest_ps_p8r1_d1_sample_n1_size_bytes;


} // namespace D3DCommon::EmbeddedShaders
