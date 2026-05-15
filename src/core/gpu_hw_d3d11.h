#pragma once
#include "common/d3d11/shader_cache.h"
#include "common/d3d11/staging_texture.h"
#include "common/d3d11/stream_buffer.h"
#include "common/d3d11/texture.h"
#include "gpu_hw.h"
#include "gpu_hw_shadergen.h"
#include "host_display.h"
#include "texture_replacements.h"
#include <array>
#include <atomic>
#include <d3d11.h>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <wrl/client.h>
#include <libretro.h>

class LibretroD3D11HostDisplay final : public HostDisplay
{
public:
  LibretroD3D11HostDisplay();
  ~LibretroD3D11HostDisplay();

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
  void RenderSoftwareCursor(int32_t left, int32_t top, int32_t width, int32_t height, HostDisplayTexture* texture_handle);

  void RenderDisplay(int32_t left, int32_t top, int32_t width, int32_t height, void* texture_handle, uint32_t texture_width,
                     int32_t texture_height, int32_t texture_view_x, int32_t texture_view_y, int32_t texture_view_width,
                     int32_t texture_view_height);

private:
  static constexpr uint32_t DISPLAY_UNIFORM_BUFFER_SIZE = 16;

  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  bool CheckFramebufferSize(uint32_t width, uint32_t height);

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;

  ComPtr<ID3D11RasterizerState> m_display_rasterizer_state;
  ComPtr<ID3D11DepthStencilState> m_display_depth_stencil_state;
  ComPtr<ID3D11BlendState> m_display_blend_state;
  ComPtr<ID3D11BlendState> m_software_cursor_blend_state;
  ComPtr<ID3D11VertexShader> m_display_vertex_shader;
  ComPtr<ID3D11PixelShader> m_display_pixel_shader;
  ComPtr<ID3D11PixelShader> m_display_alpha_pixel_shader;
  ComPtr<ID3D11SamplerState> m_point_sampler;
  ComPtr<ID3D11SamplerState> m_linear_sampler;

  D3D11::Texture m_display_pixels_texture;
  D3D11::StreamBuffer m_display_uniform_buffer;
  D3D11::AutoStagingTexture m_readback_staging_texture;

  D3D11::Texture m_framebuffer;
};

class GPU_HW_D3D11 : public GPU_HW
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  GPU_HW_D3D11();
  ~GPU_HW_D3D11() override;

  bool Initialize(HostDisplay* host_display) override;
  void Reset(bool clear_vram) override;
  bool DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display) override;

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
  // Currently we don't stream uniforms, instead just re-map the buffer every time and let the driver take care of it.
  static constexpr uint32_t MAX_UNIFORM_BUFFER_SIZE = 64;

  void SetCapabilities();
  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();
  bool CreateStateObjects();
  void DestroyStateObjects();

  bool CompileShaders();
  void DestroyShaders();

  // Lazy batch-fragment-shader compile path. The shader matrix
  // starts all-null; GetBatchPixelShader fills a slot on demand
  // (the first time the game dispatches a draw using that
  // combination) and returns a non-owning pointer to the underlying
  // ID3D11PixelShader. It also handles the texture-mode dedup
  // (3->2 and 7->6) inline so the duplicate slots share their
  // canonical ComPtr.
  //
  // Two-level synchronisation:
  //   - FAST path (slot already filled): a single atomic
  //     acquire-load from m_batch_pixel_shader_fastpath. No mutex,
  //     no kernel call, no serialisation against the background
  //     precompile worker. This is what DrawBatchVertices hits in
  //     steady state.
  //   - SLOW path (slot null after the atomic load): runs
  //     shadergen + m_shader_cache.GetPixelShader (HLSL -> DXBC
  //     via D3DCompile + CreatePixelShader) WITHOUT
  //     m_batch_shader_mutex held. m_shader_cache is internally
  //     thread-safe (separate mutex around the index, NOT held
  //     across D3DCompile). After the compile completes,
  //     m_batch_shader_mutex is taken briefly to publish into the
  //     matrix under a double-check; the mutex window is now
  //     publish-only (microseconds), not compile-spanning
  //     (50-200 ms).
  //
  // ID3D11PixelShader* itself is free-threaded for the consumer
  // side (PSSetShader), so DrawBatchVertices can use the raw
  // pointer returned here without further locking.
  ID3D11PixelShader* GetBatchPixelShader(uint8_t render_mode, uint8_t texture_mode, bool dithering, bool interlacing);

  // Background-thread worker for 'Lazy' mode: walks the entire
  // (render_mode, texture_mode, dithering, interlacing) matrix and
  // calls GetBatchPixelShader on each slot. The main thread can
  // race ahead and pre-populate any slots it actually needs; the
  // worker just skips already-filled slots.
  void ShaderCompileThreadEntryPoint();
  void StopShaderCompileThread();

  void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
  void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
  void SetViewportAndScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

  void DrawUtilityShader(ID3D11PixelShader* shader, const void* uniforms, uint32_t uniforms_size);

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);

  void DownsampleFramebuffer(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferAdaptive(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferBoxFilter(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;

  // downsample texture - used for readbacks at >1xIR.
  D3D11::Texture m_vram_texture;
  D3D11::Texture m_vram_depth_texture;
  ComPtr<ID3D11DepthStencilView> m_vram_depth_view;
  D3D11::Texture m_vram_read_texture;
  D3D11::Texture m_vram_encoding_texture;
  D3D11::Texture m_display_texture;

  D3D11::StreamBuffer m_vertex_stream_buffer;

  D3D11::StreamBuffer m_uniform_stream_buffer;

  D3D11::StreamBuffer m_texture_stream_buffer;

  D3D11::StagingTexture m_vram_readback_texture;

  ComPtr<ID3D11ShaderResourceView> m_texture_stream_buffer_srv_r16ui;

  ComPtr<ID3D11RasterizerState> m_cull_none_rasterizer_state;
  ComPtr<ID3D11RasterizerState> m_cull_none_rasterizer_state_no_msaa;

  ComPtr<ID3D11DepthStencilState> m_depth_disabled_state;
  ComPtr<ID3D11DepthStencilState> m_depth_test_always_state;
  ComPtr<ID3D11DepthStencilState> m_depth_test_less_state;
  ComPtr<ID3D11DepthStencilState> m_depth_test_greater_state;

  ComPtr<ID3D11BlendState> m_blend_disabled_state;
  ComPtr<ID3D11BlendState> m_blend_no_color_writes_state;

  ComPtr<ID3D11SamplerState> m_point_sampler_state;
  ComPtr<ID3D11SamplerState> m_linear_sampler_state;
  ComPtr<ID3D11SamplerState> m_trilinear_sampler_state;

  std::array<ComPtr<ID3D11BlendState>, 5> m_batch_blend_states; // [transparency_mode]
  ComPtr<ID3D11InputLayout> m_batch_input_layout;
  std::array<ComPtr<ID3D11VertexShader>, 2> m_batch_vertex_shaders; // [textured]

  // Batch pixel shader matrix. The ComPtr array owns the
  // reference; the parallel atomic-raw-pointer array exists so
  // DrawBatchVertices can sample a slot without taking any lock.
  //
  // m_batch_pixel_shaders holds the COM ownership and is only
  // written under m_batch_shader_mutex (in GetBatchPixelShader's
  // PUBLISH step - the mutex is held only for the slot-store and
  // race-loser detection, NOT across the slow D3DCompile +
  // CreatePixelShader work, which runs lock-free). The shader
  // stays alive for the lifetime of this GPU backend (until
  // DestroyShaders walks the ComPtr array), so the raw pointer
  // published into m_batch_pixel_shader_fastpath is valid until
  // then.
  //
  // The Reserved_*Direct16Bit dedup at the matrix level copies
  // the ComPtr (sharing one shader across two slots) and copies
  // the raw pointer too. SafeDestroy is via ComPtr-reset on the
  // owning array; the atomic array is just a view.
  //
  // Without this design the fast path took the same mutex the
  // background precompile worker holds during 50-200 ms HLSL
  // compiles, so concurrent main-thread draws would stall behind
  // the worker for one whole shader compile per draw - which on
  // Lazy mode meant the runloop locked up entirely before the BIOS
  // screen as the worker walked the 144-entry batch matrix on its
  // first run. With this design DrawBatchVertices is lock-free on
  // cache-hit AND on race the slow path doesn't block the worker
  // either; the worker can compile in the background unmolested.
  std::array<std::array<std::array<std::array<ComPtr<ID3D11PixelShader>, 2>, 2>, 9>, 4>
    m_batch_pixel_shaders; // [render_mode][texture_mode][dithering][interlacing]
  std::array<std::array<std::array<std::array<std::atomic<ID3D11PixelShader*>, 2>, 2>, 9>, 4>
    m_batch_pixel_shader_fastpath{}; // [render_mode][texture_mode][dithering][interlacing]

  // Persistent shader cache and shadergen instances for the lazy
  // / background-thread compile path. Both used to be locals in
  // CompileShaders(); now they live for the lifetime of this GPU
  // backend so GetBatchPixelShader can call into them at draw time.
  // m_batch_shader_mutex serialises the SLOW path of
  // GetBatchPixelShader (cache mutation, ComPtr-array write, and
  // the atomic-raw-pointer publish). The FAST path -
  // DrawBatchVertices looking up an already-filled slot - reads
  // m_batch_pixel_shader_fastpath with an atomic acquire-load and
  // does not take this mutex; that's what keeps the runloop
  // running while the background precompile worker is compiling
  // other cells. The shadergen is stateless once constructed - all
  // its parameters are baked in at construction - so reads under
  // the mutex are safe; we re-construct it in CompileShaders
  // whenever settings change and the matrix needs re-filling.
  std::mutex m_batch_shader_mutex;
  D3D11::ShaderCache m_shader_cache;
  std::unique_ptr<GPU_HW_ShaderGen> m_shadergen;
  std::thread m_shader_compile_thread;
  std::atomic<bool> m_shader_compile_thread_quit{false};

  ComPtr<ID3D11VertexShader> m_screen_quad_vertex_shader;
  ComPtr<ID3D11VertexShader> m_uv_quad_vertex_shader;
  ComPtr<ID3D11PixelShader> m_copy_pixel_shader;
  std::array<std::array<ComPtr<ID3D11PixelShader>, 2>, 2> m_vram_fill_pixel_shaders;  // [wrapped][interlaced]
  ComPtr<ID3D11PixelShader> m_vram_read_pixel_shader;
  ComPtr<ID3D11PixelShader> m_vram_write_pixel_shader;
  ComPtr<ID3D11PixelShader> m_vram_copy_pixel_shader;
  ComPtr<ID3D11PixelShader> m_vram_update_depth_pixel_shader;
  std::array<std::array<ComPtr<ID3D11PixelShader>, 3>, 2> m_display_pixel_shaders; // [depth_24][interlaced]

  D3D11::Texture m_vram_replacement_texture;

  // downsampling
  ComPtr<ID3D11PixelShader> m_downsample_first_pass_pixel_shader;
  ComPtr<ID3D11PixelShader> m_downsample_mid_pass_pixel_shader;
  ComPtr<ID3D11PixelShader> m_downsample_blur_pass_pixel_shader;
  ComPtr<ID3D11PixelShader> m_downsample_composite_pixel_shader;
  D3D11::Texture m_downsample_texture;
  D3D11::Texture m_downsample_weight_texture;
  std::vector<std::pair<ComPtr<ID3D11ShaderResourceView>, ComPtr<ID3D11RenderTargetView>>> m_downsample_mip_views;
};
