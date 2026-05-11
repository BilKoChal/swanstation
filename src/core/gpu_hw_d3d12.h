#pragma once
#include "common/d3d12/shader_cache.h"
#include "common/d3d12/staging_texture.h"
#include "common/d3d12/stream_buffer.h"
#include "common/d3d12/texture.h"
#include "common/dimensional_array.h"
#include "gpu_hw.h"
#include "gpu_hw_shadergen.h"
#include "texture_replacements.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

class GPU_HW_D3D12 : public GPU_HW
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  GPU_HW_D3D12();
  ~GPU_HW_D3D12() override;

  GPURenderer GetRendererType() const override;

  bool Initialize(HostDisplay* host_display) override;
  void Reset(bool clear_vram) override;

  void ResetGraphicsAPIState() override;
  void RestoreGraphicsAPIState() override;
  void UpdateSettings() override;

protected:
  void ClearDisplay() override;
  void UpdateDisplay() override;
  void ReadVRAM(u32 x, u32 y, u32 width, u32 height) override;
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height) override;
  void UpdateVRAMReadTexture() override;
  void UpdateDepthBufferFromMaskBit() override;
  void ClearDepthBuffer() override;
  void SetScissorFromDrawingArea() override;
  void MapBatchVertexPointer(u32 required_vertices) override;
  void UnmapBatchVertexPointer(u32 used_vertices) override;
  void UploadUniformBuffer(const void* data, u32 data_size) override;
  void DrawBatchVertices(BatchRenderMode render_mode, u32 base_vertex, u32 num_vertices) override;

private:
  static constexpr u32 MAX_PUSH_CONSTANTS_SIZE = 64, TEXTURE_REPLACEMENT_BUFFER_SIZE = 64 * 1024 * 1024;
  void SetCapabilities();
  void DestroyResources();

  bool CreateRootSignatures();
  bool CreateSamplers();

  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();

  bool CompilePipelines();
  void DestroyPipelines();

  // Lazy batch-fragment-shader-blob + PSO compile path.
  //
  // GetBatchFragmentShader returns a non-null ComPtr to the
  // ID3DBlob holding the compiled HLSL bytecode for the requested
  // (render_mode, texture_mode, dithering, interlacing) tuple,
  // generating the shader source via m_shadergen and feeding it
  // through m_shader_cache.GetPixelShader on a cache miss. It also
  // handles the Reserved_Direct16Bit (3) -> Direct16Bit (2) and
  // Reserved_RawDirect16Bit (7) -> RawDirect16Bit (6) dedup at the
  // blob array level.
  //
  // GetBatchPipeline returns the ID3D12PipelineState for the full
  // 6-D PSO matrix slot. It builds the gpbuilder state on demand,
  // calling GetBatchFragmentShader for the bound shader, then
  // hands the descriptor to m_shader_cache.GetPipelineState (which
  // hits the on-disk PSO cache where possible, only paying the
  // actual driver compile on cold runs). Reserved_* texture-mode
  // PSOs inherit the canonical PSO ComPtr.
  //
  // Both helpers serialise their cache + array mutations through
  // m_batch_shader_mutex. The fast path is one uncontended lock
  // per DrawBatchVertices call.
  ComPtr<ID3DBlob> GetBatchFragmentShader(u8 render_mode, u8 texture_mode, bool dithering, bool interlacing);
  ComPtr<ID3D12PipelineState> GetBatchPipeline(u8 depth_test, u8 render_mode, u8 texture_mode, u8 transparency_mode,
                                               bool dithering, bool interlacing);

  // Background-thread worker for 'Lazy' precompile mode: walks the
  // full PSO matrix in (depth_test, render_mode, transparency_mode,
  // texture_mode, dithering, interlacing) order and calls
  // GetBatchPipeline on each cell. As with D3D11, the main thread
  // can race ahead and fill any slot it needs at draw time; the
  // worker just observes the filled slot under the lock and moves
  // on.
  void ShaderCompileThreadEntryPoint();
  void StopShaderCompileThread();

  bool CreateTextureReplacementStreamBuffer();
  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, u32 dst_x, u32 dst_y, u32 width, u32 height);

  ComPtr<ID3D12RootSignature> m_batch_root_signature;
  ComPtr<ID3D12RootSignature> m_single_sampler_root_signature;

  D3D12::Texture m_vram_texture;
  D3D12::Texture m_vram_depth_texture;
  D3D12::Texture m_vram_read_texture;
  D3D12::Texture m_vram_readback_texture;
  D3D12::StagingTexture m_vram_readback_staging_texture;
  D3D12::Texture m_display_texture;

  D3D12::DescriptorHandle m_point_sampler;
  D3D12::DescriptorHandle m_linear_sampler;

  D3D12::StreamBuffer m_vertex_stream_buffer;
  D3D12::StreamBuffer m_uniform_stream_buffer;
  D3D12::StreamBuffer m_texture_stream_buffer;
  D3D12::DescriptorHandle m_texture_stream_buffer_srv;

  u32 m_current_uniform_buffer_offset = 0;

  // [depth_test][render_mode][texture_mode][transparency_mode][dithering][interlacing]
  DimensionalArray<ComPtr<ID3D12PipelineState>, 2, 2, 5, 9, 4, 2> m_batch_pipelines;

  // Persistent shader-cache + shadergen instances for the lazy and
  // background-thread compile paths. Both used to be locals in
  // CompilePipelines(); now they live for the lifetime of this GPU
  // backend so the lazy helpers can call into them at draw time.
  // The mutex serialises access to the cache (its index maps are
  // not thread-safe on insert), to m_batch_vertex_shader_blobs /
  // m_batch_fragment_shader_blobs (the shader-bytecode caches used
  // both at precompile and at lazy PSO build time), and to
  // m_batch_pipelines itself. m_shadergen is functionally
  // stateless once constructed but we still pin its lifetime here
  // because GetBatchFragmentShader calls into it under the lock.
  std::mutex m_batch_shader_mutex;
  D3D12::ShaderCache m_shader_cache;
  std::unique_ptr<GPU_HW_ShaderGen> m_shadergen;
  std::thread m_shader_compile_thread;
  std::atomic<bool> m_shader_compile_thread_quit{false};

  // Vertex and fragment shader bytecode blobs for the lazy PSO
  // builder. Used to be locals in CompilePipelines (the previous
  // implementation built the entire matrix synchronously and only
  // needed them for the duration of that function). With lazy /
  // background compile they must outlive CompilePipelines so the
  // PSO-builder helper can fetch the bound bytecode when faulting
  // in a PSO. The fragment-shader matrix is keyed on
  // (render, texture, dithering, interlacing) only - the
  // depth/transparency dimensions of m_batch_pipelines don't enter
  // the fragment shader source.
  DimensionalArray<ComPtr<ID3DBlob>, 2> m_batch_vertex_shader_blobs;            // [textured]
  DimensionalArray<ComPtr<ID3DBlob>, 2, 2, 9, 4> m_batch_fragment_shader_blobs; // [render][texture][dither][interlace]

  // [wrapped][interlaced]
  DimensionalArray<ComPtr<ID3D12PipelineState>, 2, 2> m_vram_fill_pipelines;

  // [depth_test]
  std::array<ComPtr<ID3D12PipelineState>, 2> m_vram_write_pipelines;
  std::array<ComPtr<ID3D12PipelineState>, 2> m_vram_copy_pipelines;

  ComPtr<ID3D12PipelineState> m_vram_readback_pipeline;
  ComPtr<ID3D12PipelineState> m_vram_update_depth_pipeline;

  // [depth_24][interlace_mode]
  DimensionalArray<ComPtr<ID3D12PipelineState>, 3, 2> m_display_pipelines;

  ComPtr<ID3D12PipelineState> m_copy_pipeline;
  D3D12::Texture m_vram_write_replacement_texture;
  D3D12::StreamBuffer m_texture_replacment_stream_buffer;
};
