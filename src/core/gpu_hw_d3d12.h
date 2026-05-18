#pragma once
#include "common/d3d12/shader_cache.h"
#include "common/d3d12/staging_texture.h"
#include "common/d3d12/stream_buffer.h"
#include "common/d3d12/texture.h"
#include "common/dimensional_array.h"
#include "gpu_hw.h"
#include "gpu_hw_shadergen.h"
#include "host_display.h"
#include "texture_replacements.h"
#include <array>
#include <atomic>
#include <d3d12.h>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <wrl/client.h>
#include <libretro.h>

class LibretroD3D12HostDisplay final : public HostDisplay
{
public:
  LibretroD3D12HostDisplay();
  ~LibretroD3D12HostDisplay();

  static bool RequestHardwareRendererContext(retro_hw_render_callback* cb);

  RenderAPI GetRenderAPI() const override;
  void* GetRenderDevice() const override;
  void* GetRenderContext() const override;

  bool CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                          bool threaded_presentation) override;
  bool InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                              bool threaded_presentation) override;
  void DestroyRenderDevice() override;

  void ResizeRenderWindow(int32_t new_window_width, int32_t new_window_height) override;

  bool ChangeRenderWindow(const WindowInfo& new_wi) override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(uint32_t width, uint32_t height, uint32_t layers, uint32_t levels, uint32_t samples,
                                                    HostDisplayPixelFormat format, const void* data, uint32_t data_stride,
                                                    bool dynamic = false) override;
  bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const override;
  bool BeginSetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height, void** out_buffer,
                             uint32_t* out_pitch) override;
  void EndSetDisplayPixels() override;

  bool Render() override;

protected:
  bool CreateResources() override;
  void DestroyResources() override;

private:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  bool CheckFramebufferSize(uint32_t width, uint32_t height);

  // retro_hw_render_interface_d3d12::set_texture callback - the frontend
  // reads from this resource in m_required_state to compose the final
  // output frame. We hand it the framebuffer texture (post our display
  // blit) at the end of Render().
  void (*m_set_texture)(void* handle, ID3D12Resource* texture, DXGI_FORMAT format) = nullptr;
  void* m_frontend_handle = nullptr;
  D3D12_RESOURCE_STATES m_required_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  D3D12::Texture m_framebuffer;
};

class GPU_HW_D3D12 : public GPU_HW
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  GPU_HW_D3D12();
  ~GPU_HW_D3D12() override;

  bool Initialize(HostDisplay* host_display) override;
  void Reset(bool clear_vram) override;

  void ResetGraphicsAPIState() override;
  void RestoreGraphicsAPIState() override;
  void UpdateSettings() override;

protected:
  void ClearDisplay() override;
  void UpdateDisplay() override;
  void ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
  void FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) override;
  void UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask) override;
  void CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height) override;
  void UpdateVRAMReadTexture() override;
  void UpdateDepthBufferFromMaskBit() override;
  void ClearDepthBuffer() override;
  void SetScissorFromDrawingArea() override;
  void MapBatchVertexPointer(uint32_t required_vertices) override;
  void UnmapBatchVertexPointer(uint32_t used_vertices) override;
  void UploadUniformBuffer(const void* data, uint32_t data_size) override;
  void DrawBatchVertices(BatchRenderMode render_mode, uint32_t base_vertex, uint32_t num_vertices) override;

private:
  static constexpr uint32_t MAX_PUSH_CONSTANTS_SIZE = 64, TEXTURE_REPLACEMENT_BUFFER_SIZE = 64 * 1024 * 1024;
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
  void ClearDisplayPipelines();

  // Lazy batch-fragment-shader-blob + PSO compile path.
  //
  // GetBatchFragmentShader returns a non-null ComPtr to the
  // ID3DBlob holding the compiled HLSL bytecode for the requested
  // (filter, render_mode, texture_mode, dithering, interlacing)
  // tuple, generating the shader source via a temporary shadergen
  // bound to that filter and feeding it through
  // m_shader_cache.GetPixelShader on a cache miss. The temporary
  // shadergen lets the worker / main thread fault in a non-current
  // filter's sub-cube without having to reconstruct m_shadergen
  // (which stays bound to the runtime-current m_texture_filtering
  // for the non-batch helpers below). It also handles the
  // Reserved_Direct16Bit (3) -> Direct16Bit (2) and
  // Reserved_RawDirect16Bit (7) -> RawDirect16Bit (6) dedup at the
  // blob array level.
  //
  // GetBatchPipeline returns the ID3D12PipelineState for the full
  // 7-D PSO matrix slot (filter added as the outermost dimension to
  // the existing 6-D layout - mirrors the Vulkan dim-cache shape).
  // It builds the gpbuilder state on demand, calling
  // GetBatchFragmentShader for the bound shader with the same
  // filter, then hands the descriptor to
  // m_shader_cache.GetPipelineState (which hits the on-disk PSO
  // cache where possible, only paying the actual driver compile on
  // cold runs). Reserved_* texture-mode PSOs inherit the canonical
  // PSO ComPtr.
  //
  // Both helpers serialise their cache + array mutations through
  // m_batch_shader_mutex. The fast path is one uncontended lock
  // per DrawBatchVertices call.
  ComPtr<ID3DBlob> GetBatchFragmentShader(GPUTextureFilter filter, uint8_t render_mode, uint8_t texture_mode, bool dithering, bool interlacing);
  ComPtr<ID3D12PipelineState> GetBatchPipeline(GPUTextureFilter filter, uint8_t depth_test, uint8_t render_mode, uint8_t texture_mode, uint8_t transparency_mode,
                                               bool dithering, bool interlacing);

  // Lazy non-batch PSO compile path.
  //
  // The non-batch pipelines (VRAM fill / copy / write / update depth
  // / readback, display, copy/blit) used to be compiled
  // unconditionally at CompilePipelines time regardless of the
  // gpu_shader_precompile_mode setting. Even with 'Disabled' the
  // user would pay the cost of building 17 PSOs + the fullscreen
  // quad vertex shader at GPU init - on a cold cache that is
  // hundreds of ms of D3DCompile + driver PSO assembly. The
  // 'Disabled' contract is meant to be "skip all compilation at
  // init time, fault everything in on first use", so these helpers
  // bring the non-batch pipelines under the same lazy-fault
  // umbrella as the batch matrix.
  //
  // Each helper follows the same fast-path / slow-path layout as
  // GetBatchPipeline: an acquire-load on an atomic raw-pointer
  // fast-path array, falling back to the ComPtr slot under
  // m_batch_shader_mutex on a miss. The atomic fast path means
  // the runloop hot path - FillVRAM / CopyVRAM / UpdateVRAM /
  // UpdateDisplay / etc. - takes no lock once the slot is filled.
  // The single-PSO ones use a bare std::atomic + ComPtr pair.
  //
  // GetFullscreenQuadVertexShader returns the pre-baked DXBC blob for
  // the screen-quad VS. No lazy compile, no caching state - the blob
  // is statically linked from src/common/d3d12/embedded_dxbc/. Every
  // non-batch pipeline in this backend uses it as the VS stage; see
  // src/common/d3d12/embedded_shaders.h for the underlying declaration.
  D3D12_SHADER_BYTECODE GetFullscreenQuadVertexShader();
  ComPtr<ID3D12PipelineState> GetVRAMFillPipeline(uint8_t wrapped, uint8_t interlaced);
  ComPtr<ID3D12PipelineState> GetVRAMCopyPipeline(uint8_t depth_test);
  ComPtr<ID3D12PipelineState> GetVRAMWritePipeline(uint8_t depth_test);
  ComPtr<ID3D12PipelineState> GetVRAMUpdateDepthPipeline();
  ComPtr<ID3D12PipelineState> GetVRAMReadbackPipeline();
  ComPtr<ID3D12PipelineState> GetDisplayPipeline(uint8_t depth_24, uint8_t interlace_mode);
  ComPtr<ID3D12PipelineState> GetCopyPipeline();

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
  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);

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

  uint32_t m_current_uniform_buffer_offset = 0;

  // Batch PSO matrix. The ComPtr array owns the reference; the
  // parallel atomic-raw-pointer array exists so DrawBatchVertices
  // can sample a slot without taking any lock. Same split as the
  // D3D11 batch pixel shader matrix: m_batch_pipelines holds the
  // COM ownership and is only written under m_batch_shader_mutex
  // (in GetBatchPipeline's slow path); m_batch_pipelines_fastpath
  // is the atomic view the runloop reads on the fast path.
  //
  // filter is the outermost dimension. With the cbuffer-refactor
  // patch (7b575a3) the HLSL is invariant under
  // resolution_scale / true_color / scaled_dithering, but it is
  // STILL dependent on texture filter - the FilteredSampleFromVRAM
  // helper is emitted differently for Nearest vs Bilinear vs
  // JINC2 vs xBR vs the BinAlpha variants. Dimensioning over filter
  // lets a filter toggle skip the DestroyPipelines round trip in
  // UpdateSettings: the previous filter's sub-cube remains valid
  // and reachable, and switching back to it later is an atomic
  // load on an already-filled slot. Mirrors the same shape the
  // Vulkan backend has carried for filter / true_color /
  // scaled_dithering (Vulkan keeps true_color and scaled_dithering
  // as dims too because those are VkSpecializationInfo spec
  // constants and each combination needs a distinct VkPipeline;
  // D3D12 doesn't because they're cbuffer fields now).
  //
  // GPUTextureFilter::Count = 7 -> the outermost dim size.
  //
  // [filter][depth_test][render_mode][texture_mode][transparency_mode][dithering][interlacing]
  DimensionalArray<ComPtr<ID3D12PipelineState>, 2, 2, 5, 9, 4, 2, 7> m_batch_pipelines;
  DimensionalArray<std::atomic<ID3D12PipelineState*>, 2, 2, 5, 9, 4, 2, 7> m_batch_pipelines_fastpath{};

  // m_batch_shader_mutex serialises the SLOW path of the lazy
  // helpers (cache mutation, ComPtr-array write, atomic-raw-pointer
  // publish). The FAST path - DrawBatchVertices looking up an
  // already-filled PSO slot, or GetBatchPipeline looking up an
  // already-compiled fragment shader - reads the corresponding
  // _fastpath atomic array with an acquire-load and does not take
  // this mutex. That decoupling is what keeps the runloop running
  // while the background precompile worker is compiling other
  // cells. m_shadergen is functionally stateless once constructed
  // but we still pin its lifetime here because the helpers' slow
  // path calls into it under the lock.
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
  //
  // Same fast/slow-path split as m_batch_pipelines above:
  // m_batch_fragment_shader_blobs owns the ComPtr; the parallel
  // _fastpath array is the lock-free view.
  DimensionalArray<ComPtr<ID3DBlob>, 2> m_batch_vertex_shader_blobs;            // [textured]
  DimensionalArray<ComPtr<ID3DBlob>, 2, 2, 9, 4, 7> m_batch_fragment_shader_blobs; // [filter][render][texture][dither][interlace]
  DimensionalArray<std::atomic<ID3DBlob*>, 2, 2, 9, 4, 7> m_batch_fragment_shader_blobs_fastpath{};

  // [wrapped][interlaced]
  DimensionalArray<ComPtr<ID3D12PipelineState>, 2, 2> m_vram_fill_pipelines;
  DimensionalArray<std::atomic<ID3D12PipelineState*>, 2, 2> m_vram_fill_pipelines_fastpath{};

  // [depth_test]
  std::array<ComPtr<ID3D12PipelineState>, 2> m_vram_write_pipelines;
  std::array<ComPtr<ID3D12PipelineState>, 2> m_vram_copy_pipelines;
  std::array<std::atomic<ID3D12PipelineState*>, 2> m_vram_write_pipelines_fastpath{};
  std::array<std::atomic<ID3D12PipelineState*>, 2> m_vram_copy_pipelines_fastpath{};

  ComPtr<ID3D12PipelineState> m_vram_readback_pipeline;
  ComPtr<ID3D12PipelineState> m_vram_update_depth_pipeline;
  std::atomic<ID3D12PipelineState*> m_vram_readback_pipeline_fastpath{nullptr};
  std::atomic<ID3D12PipelineState*> m_vram_update_depth_pipeline_fastpath{nullptr};

  // [depth_24][interlace_mode]
  DimensionalArray<ComPtr<ID3D12PipelineState>, 3, 2> m_display_pipelines;
  DimensionalArray<std::atomic<ID3D12PipelineState*>, 3, 2> m_display_pipelines_fastpath{};

  ComPtr<ID3D12PipelineState> m_copy_pipeline;
  std::atomic<ID3D12PipelineState*> m_copy_pipeline_fastpath{nullptr};

  D3D12::Texture m_vram_write_replacement_texture;
  D3D12::StreamBuffer m_texture_replacment_stream_buffer;
};
