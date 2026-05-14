#pragma once
#include "common/dimensional_array.h"
#include "common/vulkan/staging_texture.h"
#include "common/vulkan/stream_buffer.h"
#include "common/vulkan/texture.h"
#include "core/host_display.h"
#include "gpu_hw.h"
#include "gpu_hw_shadergen.h"
#include "texture_replacements.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <libretro.h>

#define HAVE_VULKAN
#include <libretro_vulkan.h>

class LibretroVulkanHostDisplay final : public HostDisplay
{
public:
  LibretroVulkanHostDisplay();
  ~LibretroVulkanHostDisplay();

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
  static constexpr VkFormat FRAMEBUFFER_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

  struct PushConstants
  {
    float src_rect_left;
    float src_rect_top;
    float src_rect_width;
    float src_rect_height;
  };

  bool CheckFramebufferSize(uint32_t width, uint32_t height);

  VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
  VkPipeline m_cursor_pipeline = VK_NULL_HANDLE;
  VkPipeline m_display_pipeline = VK_NULL_HANDLE;
  VkSampler m_point_sampler = VK_NULL_HANDLE;
  VkSampler m_linear_sampler = VK_NULL_HANDLE;

  Vulkan::Texture m_display_pixels_texture;
  Vulkan::StagingTexture m_upload_staging_texture;
  Vulkan::StagingTexture m_readback_staging_texture;

  retro_hw_render_interface_vulkan* m_ri = nullptr;

  Vulkan::Texture m_frame_texture;
  retro_vulkan_image m_frame_view = {};
  VkFramebuffer m_frame_framebuffer = VK_NULL_HANDLE;
  VkRenderPass m_frame_render_pass = VK_NULL_HANDLE;
};

class GPU_HW_Vulkan : public GPU_HW
{
public:
  GPU_HW_Vulkan();
  ~GPU_HW_Vulkan() override;

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
  static constexpr uint32_t MAX_PUSH_CONSTANTS_SIZE = 64, TEXTURE_REPLACEMENT_BUFFER_SIZE = 64 * 1024 * 1024;
  void SetCapabilities();
  void DestroyResources();

  ALWAYS_INLINE bool InRenderPass() const { return (m_current_render_pass != VK_NULL_HANDLE); }
  void BeginRenderPass(VkRenderPass render_pass, VkFramebuffer framebuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                       const VkClearValue* clear_value = nullptr);
  void BeginVRAMRenderPass();
  void EndRenderPass();
  void ExecuteCommandBuffer(bool wait_for_completion, bool restore_state);

  bool CreatePipelineLayouts();
  bool CreateSamplers();

  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();

  bool CompilePipelines();
  void DestroyPipelines();

  // Lazy batch-fragment-shader-module + PSO compile path. Same
  // three-mode (Disabled / Enabled / Lazy) plumbing as the D3D11
  // and D3D12 backends; see GPUShaderPrecompileMode in core/types.h
  // for the user-facing semantics.
  //
  // Unlike D3D11 / D3D12, Vulkan can't share a refcounted resource
  // wrapper between dispatch matrix slots, because both
  // VkShaderModule and VkPipeline are raw handles destroyed via
  // SafeDestroyShaderModule / SafeDestroyPipeline - two slots
  // holding the same handle would double-call vkDestroy*. So the
  // Reserved_*Direct16Bit dedup is applied at the *helper entry*
  // (texture_mode 3 -> 2 and 7 -> 6 before the array index is
  // computed) rather than by populating both slots. The dup slots
  // for texture_mode 3 / 7 remain VK_NULL_HANDLE for the lifetime
  // of the GPU backend; SafeDestroy* handles VK_NULL_HANDLE
  // gracefully so DestroyPipelines doesn't trip on them.
  VkShaderModule GetBatchFragmentShader(uint8_t render_mode, uint8_t texture_mode, bool dithering, bool interlacing);
  VkPipeline GetBatchPipeline(uint8_t depth_test, uint8_t render_mode, uint8_t texture_mode, uint8_t transparency_mode, bool dithering,
                              bool interlacing);

  // Background-thread worker for 'Lazy' mode: walks the full PSO
  // matrix and calls GetBatchPipeline on each cell. Main thread
  // can race ahead and fault in any slot it actually needs at draw
  // time; the worker observes the filled slot under the lock and
  // moves on. Quit flag checked between cells so DestroyPipelines
  // can stop the worker within at most one PSO compile of latency.
  void ShaderCompileThreadEntryPoint();
  void StopShaderCompileThread();

  bool CreateTextureReplacementStreamBuffer();

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);

  void DownsampleFramebuffer(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferBoxFilter(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferAdaptive(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);

  VkRenderPass m_current_render_pass = VK_NULL_HANDLE;

  VkRenderPass m_vram_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_vram_update_depth_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_display_load_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_display_discard_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_vram_readback_render_pass = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_batch_descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_single_sampler_descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_vram_write_descriptor_set_layout = VK_NULL_HANDLE;

  VkPipelineLayout m_batch_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_no_samplers_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_single_sampler_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_vram_write_pipeline_layout = VK_NULL_HANDLE;

  Vulkan::Texture m_vram_texture;
  Vulkan::Texture m_vram_depth_texture;
  Vulkan::Texture m_vram_read_texture;
  Vulkan::Texture m_vram_readback_texture;
  Vulkan::StagingTexture m_vram_readback_staging_texture;
  Vulkan::Texture m_display_texture;
  bool m_use_ssbos_for_vram_writes = false;

  VkFramebuffer m_vram_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer m_vram_update_depth_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer m_vram_readback_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer m_display_framebuffer = VK_NULL_HANDLE;

  VkSampler m_point_sampler = VK_NULL_HANDLE;
  VkSampler m_linear_sampler = VK_NULL_HANDLE;
  VkSampler m_trilinear_sampler = VK_NULL_HANDLE;

  VkDescriptorSet m_batch_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_vram_copy_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_vram_read_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_vram_write_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_display_descriptor_set = VK_NULL_HANDLE;

  Vulkan::StreamBuffer m_vertex_stream_buffer;
  Vulkan::StreamBuffer m_uniform_stream_buffer;
  Vulkan::StreamBuffer m_texture_stream_buffer;

  uint32_t m_current_uniform_buffer_offset = 0;
  VkBufferView m_texture_stream_buffer_view = VK_NULL_HANDLE;

  // [depth_test][render_mode][texture_mode][transparency_mode][dithering][interlacing]
  //
  // std::atomic<VkPipeline> rather than a plain VkPipeline so the
  // draw path can sample a slot without taking any lock. The slow
  // path (slot still null after the atomic load) takes the compile
  // mutex, re-checks the slot, compiles if still null, publishes
  // back to the slot. The fast path - which is what DrawBatchVertices
  // hits once a slot has been filled either by the precompile worker
  // or by an earlier main-thread fault-in - is a single
  // memory_order_acquire load with no kernel calls and no
  // serialisation against the worker. Without this, the user-visible
  // experience after a texture-filter change was a multi-second
  // hang: the precompile worker holds m_batch_shader_mutex for the
  // duration of each ~50-200 ms vkCreateGraphicsPipelines call, so
  // every concurrent main-thread DrawBatchVertices stalled behind
  // it for one PSO compile, and the runloop never made forward
  // progress until the worker finished the entire 2160-entry
  // matrix.
  DimensionalArray<std::atomic<VkPipeline>, 2, 2, 5, 9, 4, 3> m_batch_pipelines{};

  // Persistent vertex / fragment shader modules and shadergen for
  // the lazy and background-thread compile paths. These used to be
  // locals in CompilePipelines and were leaked on success; now they
  // live for the lifetime of this GPU backend so the lazy PSO
  // builder can reach them at draw time, and DestroyPipelines tears
  // them down properly via SafeDestroyShaderModule.
  //
  // Dup slots for texture_mode 3 / 7 stay VK_NULL_HANDLE; the
  // helper-entry remap (see GetBatchFragmentShader) routes all
  // accesses through the canonical slots 2 / 6.
  //
  // m_batch_shader_mutex serialises the SLOW path of the lazy
  // helpers:
  //   - g_vulkan_shader_cache mutations (its unordered_map indices
  //     aren't thread-safe on insert)
  //   - the VkPipelineCache passed to vkCreateGraphicsPipelines
  //     (Vulkan spec: must be externally synchronised when shared
  //     across threads)
  //   - the writes that publish a newly-compiled VkPipeline /
  //     VkShaderModule back into the matrix slot
  // The FAST path - draw-time lookup of an already-filled slot -
  // does NOT take this mutex; it uses an atomic load on the slot
  // itself. See the comment on m_batch_pipelines above for why
  // this matters.
  std::mutex m_batch_shader_mutex;
  std::unique_ptr<GPU_HW_ShaderGen> m_shadergen;
  std::thread m_shader_compile_thread;
  std::atomic<bool> m_shader_compile_thread_quit{false};

  DimensionalArray<std::atomic<VkShaderModule>, 2> m_batch_vertex_shaders{};              // [textured]
  DimensionalArray<std::atomic<VkShaderModule>, 2, 2, 9, 4> m_batch_fragment_shaders{};   // [render][texture][dither][interlace]

  // [wrapped][interlaced]
  DimensionalArray<VkPipeline, 2, 2> m_vram_fill_pipelines{};

  // [depth_test]
  std::array<VkPipeline, 2> m_vram_write_pipelines{};
  std::array<VkPipeline, 2> m_vram_copy_pipelines{};

  VkPipeline m_vram_readback_pipeline = VK_NULL_HANDLE;
  VkPipeline m_vram_update_depth_pipeline = VK_NULL_HANDLE;

  // [depth_24][interlace_mode]
  DimensionalArray<VkPipeline, 3, 2> m_display_pipelines{};

  // texture replacements
  Vulkan::Texture m_vram_write_replacement_texture;
  Vulkan::StreamBuffer m_texture_replacment_stream_buffer;

  // downsampling
  Vulkan::Texture m_downsample_texture;
  VkRenderPass m_downsample_render_pass = VK_NULL_HANDLE;
  Vulkan::Texture m_downsample_weight_texture;
  VkRenderPass m_downsample_weight_render_pass = VK_NULL_HANDLE;
  VkFramebuffer m_downsample_weight_framebuffer = VK_NULL_HANDLE;

  struct SmoothMipView
  {
    VkImageView image_view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };
  std::vector<SmoothMipView> m_downsample_mip_views;

  VkPipelineLayout m_downsample_pipeline_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_downsample_composite_descriptor_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_downsample_composite_pipeline_layout = VK_NULL_HANDLE;
  VkDescriptorSet m_downsample_composite_descriptor_set = VK_NULL_HANDLE;
  VkPipeline m_downsample_first_pass_pipeline = VK_NULL_HANDLE;
  VkPipeline m_downsample_mid_pass_pipeline = VK_NULL_HANDLE;
  VkPipeline m_downsample_blur_pass_pipeline = VK_NULL_HANDLE;
  VkPipeline m_downsample_composite_pass_pipeline = VK_NULL_HANDLE;
};
