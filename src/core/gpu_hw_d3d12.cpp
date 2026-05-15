#include "gpu_hw_d3d12.h"
#include "common/align.h"
#include "common/d3d11/shader_compiler.h"
#include "common/d3d12/context.h"
#include "common/d3d12/descriptor_heap_manager.h"
#include "common/d3d12/shader_cache.h"
#include "common/d3d12/util.h"
#include "common/log.h"
#include "common/thread_priority.h"
#include "common/timer.h"
#include "gpu_hw_shadergen.h"
#include "host_display.h"
#include "host_interface.h"
#include "system.h"
#define HAVE_D3D12
#include "libretro_d3d.h"
#include <cstring>
Log_SetChannel(GPU_HW_D3D12);

extern retro_environment_t g_retro_environment_callback;
extern retro_video_refresh_t g_retro_video_refresh_callback;

// Index parallels HostDisplayPixelFormat (Unknown, RGBA8, BGRA8, RGB565,
// RGBA5551, Count). Same table layout as gpu_hw_d3d11.cpp; the Unknown
// slot resolves to DXGI_FORMAT_UNKNOWN as a value-0 sentinel.
static constexpr std::array<DXGI_FORMAT, static_cast<uint32_t>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM}};

// =====================================================================
// LibretroD3D12HostDisplay - libretro HostDisplay subclass for the D3D12
// hardware renderer path.
//
// Mirrors LibretroD3D11HostDisplay structurally, with two architectural
// differences driven by the libretro D3D12 hardware interface
// (retro_hw_render_interface_d3d12 in libretro_d3d.h):
//
//   1. Device + queue ownership lives in the frontend. We adopt them
//      via D3D12::Context::CreateForLibretro(); D3D12::Context::Destroy()
//      releases our refs. m_device / m_context style members are absent
//      because everything that needs the device goes through
//      g_d3d12_context, which is what gpu_hw_d3d12.cpp uses anyway.
//
//   2. Present path is the set_texture callback rather than a swapchain
//      blit. Render() composites into m_framebuffer (an RTV we own),
//      transitions it to required_state, calls set_texture, then signals
//      the frontend a HW frame is ready via
//      g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, ...).
//
// The HostDisplay::SetDisplayPixels path (SW pixels uploaded to a
// frontend-readable texture via UpdateSubresource) is not implemented
// for D3D12 in the current libretro frontend integration; the SW
// renderer uses LibretroHostDisplay instead. BeginSetDisplayPixels
// returns false so any caller short-circuits.
// =====================================================================

LibretroD3D12HostDisplay::LibretroD3D12HostDisplay() = default;
LibretroD3D12HostDisplay::~LibretroD3D12HostDisplay() = default;

bool LibretroD3D12HostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* cb)
{
  cb->cache_context = false;
  cb->bottom_left_origin = false;
  cb->context_type = RETRO_HW_CONTEXT_D3D12;
  cb->version_major = 12;
  cb->version_minor = 0;

  return g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, cb);
}

HostDisplay::RenderAPI LibretroD3D12HostDisplay::GetRenderAPI() const
{
  return RenderAPI::D3D12;
}

void* LibretroD3D12HostDisplay::GetRenderDevice() const
{
  return g_d3d12_context ? g_d3d12_context->GetDevice() : nullptr;
}

void* LibretroD3D12HostDisplay::GetRenderContext() const
{
  // D3D12 has no DeviceContext analogue - work is recorded on command
  // lists owned by the Context. Return nullptr; callers either don't
  // need it (D3D12 path) or already special-case the D3D12 RenderAPI.
  return nullptr;
}

bool LibretroD3D12HostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                                                  bool threaded_presentation)
{
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_D3D12 ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_D3D12_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  const retro_hw_render_interface_d3d12* d3d12_ri = reinterpret_cast<const retro_hw_render_interface_d3d12*>(ri);
  if (!d3d12_ri->device || !d3d12_ri->queue || !d3d12_ri->set_texture)
  {
    Log_ErrorPrintf("Missing D3D12 device, queue, or set_texture callback");
    return false;
  }

  if (!D3D12::Context::CreateForLibretro(d3d12_ri->device, d3d12_ri->queue))
  {
    Log_ErrorPrint("Failed to adopt D3D12 device/queue for libretro");
    return false;
  }

  m_set_texture = d3d12_ri->set_texture;
  m_frontend_handle = d3d12_ri->handle;
  m_required_state = d3d12_ri->required_state;
  m_window_info = wi;
  return true;
}

bool LibretroD3D12HostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                                     bool threaded_presentation)
{
  return CreateResources();
}

void LibretroD3D12HostDisplay::DestroyRenderDevice()
{
  ClearSoftwareCursor();
  DestroyResources();

  if (g_d3d12_context)
  {
    g_d3d12_context->WaitForGPUIdle();
    D3D12::Context::Destroy();
  }
}

void LibretroD3D12HostDisplay::ResizeRenderWindow(int32_t new_window_width, int32_t new_window_height)
{
  m_window_info.surface_width = static_cast<uint32_t>(new_window_width);
  m_window_info.surface_height = static_cast<uint32_t>(new_window_height);
}

bool LibretroD3D12HostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
  // Check that the device/queue/handle haven't changed under us.
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_D3D12 ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_D3D12_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  const retro_hw_render_interface_d3d12* d3d12_ri = reinterpret_cast<const retro_hw_render_interface_d3d12*>(ri);
  if (d3d12_ri->device != g_d3d12_context->GetDevice() || d3d12_ri->queue != g_d3d12_context->GetCommandQueue() ||
      d3d12_ri->set_texture != m_set_texture || d3d12_ri->handle != m_frontend_handle)
  {
    Log_ErrorPrintf("D3D12 device/queue/handle changed outside our control");
    return false;
  }

  // required_state is allowed to change across a ChangeRenderWindow -
  // re-cache it in case the frontend renegotiated.
  m_required_state = d3d12_ri->required_state;
  m_window_info = new_wi;
  return true;
}

std::unique_ptr<HostDisplayTexture> LibretroD3D12HostDisplay::CreateTexture(uint32_t width, uint32_t height, uint32_t layers,
                                                                            uint32_t levels, uint32_t samples,
                                                                            HostDisplayPixelFormat format,
                                                                            const void* data, uint32_t data_stride,
                                                                            bool dynamic)
{
  // GPU_HW_D3D12 manages its own textures through D3D12::Texture directly.
  // The HostDisplay::CreateTexture surface is only used by code paths that
  // do not run for the libretro D3D12 backend (software cursor preload,
  // SetDisplayPixels CPU-to-GPU upload), so this returns nullptr the same
  // way LibretroHostDisplay does.
  return nullptr;
}

bool LibretroD3D12HostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const DXGI_FORMAT dfmt = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];
  return dfmt != DXGI_FORMAT_UNKNOWN && g_d3d12_context && g_d3d12_context->SupportsTextureFormat(dfmt);
}

bool LibretroD3D12HostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height,
                                                    void** out_buffer, uint32_t* out_pitch)
{
  // See comment on CreateTexture - this path is not wired for D3D12
  // libretro. The SW renderer uses LibretroHostDisplay.
  return false;
}

void LibretroD3D12HostDisplay::EndSetDisplayPixels()
{
}

bool LibretroD3D12HostDisplay::CreateResources()
{
  // Nothing to do up front - the framebuffer is sized lazily in
  // CheckFramebufferSize when Render() sees the first display frame.
  return true;
}

void LibretroD3D12HostDisplay::DestroyResources()
{
  m_framebuffer.Destroy(false);
}

bool LibretroD3D12HostDisplay::CheckFramebufferSize(uint32_t width, uint32_t height)
{
  if (m_framebuffer.GetWidth() == width && m_framebuffer.GetHeight() == height)
    return true;

  m_framebuffer.Destroy(false);
  return m_framebuffer.Create(width, height, 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
                              DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
}

bool LibretroD3D12HostDisplay::Render()
{
  // No display texture this frame -> send the libretro frame-dupe
  // signal (NULL frame), matching the SW and D3D11 paths. See the
  // equivalent comment in gpu_hw_d3d11.cpp::Render().
  if (!HasDisplayTexture())
  {
    g_retro_video_refresh_callback(nullptr, 0, 0, 0);
    return true;
  }

  const uint32_t resolution_scale = g_host_interface_storage.GetResolutionScale();
  const uint32_t display_width = static_cast<uint32_t>(m_display_width) * resolution_scale;
  const uint32_t display_height = static_cast<uint32_t>(m_display_height) * resolution_scale;

  if (!CheckFramebufferSize(display_width, display_height))
    return false;

  // The GPU_HW_D3D12 path already produced the display image inside its
  // own m_display_texture (or m_vram_texture) and called SetDisplayTexture
  // with a D3D12::Texture* handle. For the libretro D3D12 frontend we
  // do not need to perform an additional blit through a vertex/pixel
  // shader pair the way D3D11 does - the source texture is already the
  // composited display image, so we forward its resource directly to the
  // frontend via set_texture.
  //
  // m_framebuffer therefore acts as a stable holder of an RTV-bindable
  // resource only when the source path itself isn't already in
  // PIXEL_SHADER_RESOURCE form. The current GPU_HW_D3D12::UpdateDisplay
  // call sites guarantee the display texture is left in
  // PIXEL_SHADER_RESOURCE state at end-of-frame, so the simpler model
  // is: transition that texture into m_required_state and hand it over.
  //
  // We keep m_framebuffer around because future work (software cursor
  // composition, lightgun crosshair overlay) will need an owned RTV
  // to draw onto, matching what gpu_hw_d3d11.cpp does. For now it's
  // unused on the present path.
  D3D12::Texture* const display_texture = static_cast<D3D12::Texture*>(const_cast<void*>(m_display_texture_handle));
  display_texture->TransitionToState(m_required_state);

  // Flush our pending command list before handing the texture over -
  // otherwise the frontend may read stale contents.
  g_d3d12_context->ExecuteCommandList(false);

  m_set_texture(m_frontend_handle, display_texture->GetResource(), DXGI_FORMAT_R8G8B8A8_UNORM);
  g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, m_display_texture_view_width,
                                 m_display_texture_view_height, 0);
  return true;
}

GPU_HW_D3D12::GPU_HW_D3D12() = default;

GPU_HW_D3D12::~GPU_HW_D3D12()
{
  if (m_host_display)
  {
    m_host_display->ClearDisplayTexture();
    ResetGraphicsAPIState();
  }

  DestroyResources();
}

bool GPU_HW_D3D12::Initialize(HostDisplay* host_display)
{
  if (host_display->GetRenderAPI() != HostDisplay::RenderAPI::D3D12)
  {
    Log_ErrorPrintf("Host render API is incompatible");
    return false;
  }

  SetCapabilities();

  if (!GPU_HW::Initialize(host_display))
    return false;

  if (!CreateRootSignatures())
  {
    Log_ErrorPrintf("Failed to create root signatures");
    return false;
  }

  if (!CreateSamplers())
  {
    Log_ErrorPrintf("Failed to create samplers");
    return false;
  }

  if (!CreateVertexBuffer())
  {
    Log_ErrorPrintf("Failed to create vertex buffer");
    return false;
  }

  if (!CreateUniformBuffer())
  {
    Log_ErrorPrintf("Failed to create uniform buffer");
    return false;
  }

  if (!CreateTextureBuffer())
  {
    Log_ErrorPrintf("Failed to create texture buffer");
    return false;
  }

  if (!CreateFramebuffer())
  {
    Log_ErrorPrintf("Failed to create framebuffer");
    return false;
  }

  if (!CompilePipelines())
  {
    Log_ErrorPrintf("Failed to compile pipelines");
    return false;
  }

  RestoreGraphicsAPIState();
  UpdateDepthBufferFromMaskBit();
  return true;
}

void GPU_HW_D3D12::Reset(bool clear_vram)
{
  GPU_HW::Reset(clear_vram);

  if (clear_vram)
    ClearFramebuffer();
}

void GPU_HW_D3D12::ResetGraphicsAPIState()
{
  GPU_HW::ResetGraphicsAPIState();
}

void GPU_HW_D3D12::RestoreGraphicsAPIState()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  cmdlist->OMSetRenderTargets(1, &m_vram_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE,
                              &m_vram_depth_texture.GetRTVOrDSVDescriptor().cpu_handle);

  const D3D12_VERTEX_BUFFER_VIEW vbv{m_vertex_stream_buffer.GetGPUPointer(), m_vertex_stream_buffer.GetSize(),
                                     sizeof(BatchVertex)};
  cmdlist->IASetVertexBuffers(0, 1, &vbv);
  cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  cmdlist->SetGraphicsRootSignature(m_batch_root_signature.Get());
  cmdlist->SetGraphicsRootConstantBufferView(0,
                                             m_uniform_stream_buffer.GetGPUPointer() + m_current_uniform_buffer_offset);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_read_texture.GetSRVDescriptor().gpu_handle);
  cmdlist->SetGraphicsRootDescriptorTable(2, m_point_sampler.gpu_handle);

  D3D12::SetViewport(cmdlist, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());

  SetScissorFromDrawingArea();
}

void GPU_HW_D3D12::UpdateSettings()
{
  GPU_HW::UpdateSettings();

  bool framebuffer_changed, shaders_changed;
  UpdateHWSettings(&framebuffer_changed, &shaders_changed);

  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    ReadVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
    ResetGraphicsAPIState();
  }

  // Everything should be finished executing before recreating resources.
  m_host_display->ClearDisplayTexture();
  g_d3d12_context->ExecuteCommandList(true);

  if (framebuffer_changed)
    CreateFramebuffer();

  if (shaders_changed)
  {
    // clear it since we draw a loading screen and it's not in the correct state
    DestroyPipelines();
    CompilePipelines();
  }

  // this has to be done here, because otherwise we're using destroyed pipelines in the same cmdbuffer
  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    UpdateVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT, m_vram_ptr, false, false);
    UpdateDepthBufferFromMaskBit();
    UpdateDisplay();
    ResetGraphicsAPIState();
  }
}

void GPU_HW_D3D12::MapBatchVertexPointer(uint32_t required_vertices)
{
  const uint32_t required_space = required_vertices * sizeof(BatchVertex);
  if (!m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex)))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex));
  }

  m_batch_start_vertex_ptr = static_cast<BatchVertex*>(m_vertex_stream_buffer.GetCurrentHostPointer());
  m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;
  m_batch_end_vertex_ptr = m_batch_start_vertex_ptr + (m_vertex_stream_buffer.GetCurrentSpace() / sizeof(BatchVertex));
  m_batch_base_vertex = m_vertex_stream_buffer.GetCurrentOffset() / sizeof(BatchVertex);
}

void GPU_HW_D3D12::UnmapBatchVertexPointer(uint32_t used_vertices)
{
  if (used_vertices > 0)
    m_vertex_stream_buffer.CommitMemory(used_vertices * sizeof(BatchVertex));

  m_batch_start_vertex_ptr = nullptr;
  m_batch_end_vertex_ptr = nullptr;
  m_batch_current_vertex_ptr = nullptr;
}

void GPU_HW_D3D12::UploadUniformBuffer(const void* data, uint32_t data_size)
{
  if (!m_uniform_stream_buffer.ReserveMemory(data_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    m_uniform_stream_buffer.ReserveMemory(data_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  }

  m_current_uniform_buffer_offset = m_uniform_stream_buffer.GetCurrentOffset();
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_uniform_stream_buffer.CommitMemory(data_size);

  g_d3d12_context->GetCommandList()->SetGraphicsRootConstantBufferView(0, m_uniform_stream_buffer.GetGPUPointer() +
                                                                            m_current_uniform_buffer_offset);
}

void GPU_HW_D3D12::SetCapabilities()
{
  // TODO: Query from device
  const uint32_t max_texture_size = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  const uint32_t max_texture_scale = max_texture_size / VRAM_WIDTH;
  Log_InfoPrintf("Max texture size: %ux%u", max_texture_size, max_texture_size);
  m_max_resolution_scale = max_texture_scale;

  m_max_multisamples = 1;
  for (uint32_t multisamples = 2; multisamples < D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; multisamples++)
  {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS fd = {DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(multisamples)};

    if (SUCCEEDED(g_d3d12_context->GetDevice()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &fd,
                                                                    sizeof(fd))) &&
        fd.NumQualityLevels > 0)
    {
      m_max_multisamples = multisamples;
    }
  }

  m_supports_dual_source_blend = true;
  m_supports_per_sample_shading = true;
  m_supports_disable_color_perspective = true;
  Log_InfoPrintf("Dual-source blend: %s", m_supports_dual_source_blend ? "supported" : "not supported");
  Log_InfoPrintf("Per-sample shading: %s", m_supports_per_sample_shading ? "supported" : "not supported");
  Log_InfoPrintf("Max multisamples: %u", m_max_multisamples);
}

void GPU_HW_D3D12::DestroyResources()
{
  // Everything should be finished executing before recreating resources.
  if (g_d3d12_context)
    g_d3d12_context->ExecuteCommandList(true);

  DestroyFramebuffer();
  DestroyPipelines();

  g_d3d12_context->GetSamplerHeapManager().Free(&m_point_sampler);
  g_d3d12_context->GetSamplerHeapManager().Free(&m_linear_sampler);
  g_d3d12_context->GetDescriptorHeapManager().Free(&m_texture_stream_buffer_srv);

  m_vertex_stream_buffer.Destroy(false);
  m_uniform_stream_buffer.Destroy(false);
  m_texture_stream_buffer.Destroy(false);

  m_single_sampler_root_signature.Reset();
  m_batch_root_signature.Reset();
}

bool GPU_HW_D3D12::CreateRootSignatures()
{
  D3D12::RootSignatureBuilder rsbuilder;
  rsbuilder.SetInputAssemblerFlag();
  rsbuilder.AddCBVParameter(0, D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  m_batch_root_signature = rsbuilder.Create();
  if (!m_batch_root_signature)
    return false;

  rsbuilder.Add32BitConstants(0, MAX_PUSH_CONSTANTS_SIZE / sizeof(uint32_t), D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  m_single_sampler_root_signature = rsbuilder.Create();
  if (!m_single_sampler_root_signature)
    return false;

  return true;
}

bool GPU_HW_D3D12::CreateSamplers()
{
  D3D12_SAMPLER_DESC desc = {};
  D3D12::SetDefaultSampler(&desc);
  desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_point_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_point_sampler.cpu_handle);

  desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_linear_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_linear_sampler.cpu_handle);
  return true;
}

bool GPU_HW_D3D12::CreateFramebuffer()
{
  DestroyFramebuffer();

  // scale vram size to internal resolution
  const uint32_t texture_width = VRAM_WIDTH * m_resolution_scale;
  const uint32_t texture_height = VRAM_HEIGHT * m_resolution_scale;
  const DXGI_FORMAT texture_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  const DXGI_FORMAT depth_format = DXGI_FORMAT_D16_UNORM;

  if (!m_vram_texture.Create(texture_width, texture_height, m_multisamples, texture_format, texture_format,
                             texture_format, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !m_vram_depth_texture.Create(
        texture_width, texture_height, m_multisamples, depth_format, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
        depth_format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
      !m_vram_read_texture.Create(texture_width, texture_height, 1, texture_format, texture_format, DXGI_FORMAT_UNKNOWN,
                                  DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE) ||
      !m_display_texture.Create(texture_width, texture_height, 1, texture_format, texture_format, texture_format,
                                DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !m_vram_readback_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, texture_format, texture_format, texture_format,
                                      DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !m_vram_readback_staging_texture.Create(VRAM_WIDTH / 2, VRAM_HEIGHT, texture_format, false))
  {
    return false;
  }

  D3D12::SetObjectName(m_vram_texture, "VRAM Texture");
  D3D12::SetObjectName(m_vram_depth_texture, "VRAM Depth Texture");
  D3D12::SetObjectName(m_vram_read_texture, "VRAM Read/Sample Texture");
  D3D12::SetObjectName(m_display_texture, "VRAM Display Texture");
  D3D12::SetObjectName(m_vram_read_texture, "VRAM Readback Texture");

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_vram_depth_texture.TransitionToState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  ClearDisplay();
  SetFullVRAMDirtyRectangle();
  return true;
}

void GPU_HW_D3D12::ClearFramebuffer()
{
  static constexpr float clear_color[4] = {};

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->ClearRenderTargetView(m_vram_texture.GetRTVOrDSVDescriptor(), clear_color, 0, nullptr);
  cmdlist->ClearDepthStencilView(m_vram_depth_texture.GetRTVOrDSVDescriptor(), D3D12_CLEAR_FLAG_DEPTH,
                                 m_pgxp_depth_buffer ? 1.0f : 0.0f, 0, 0, nullptr);
  SetFullVRAMDirtyRectangle();
}

void GPU_HW_D3D12::DestroyFramebuffer()
{
  m_vram_read_texture.Destroy(false);
  m_vram_depth_texture.Destroy(false);
  m_vram_texture.Destroy(false);
  m_vram_readback_texture.Destroy(false);
  m_display_texture.Destroy(false);
  m_vram_readback_staging_texture.Destroy(false);
}

bool GPU_HW_D3D12::CreateVertexBuffer()
{
  if (!m_vertex_stream_buffer.Create(VERTEX_BUFFER_SIZE))
    return false;

  D3D12::SetObjectName(m_vertex_stream_buffer.GetBuffer(), "Vertex Stream Buffer");
  return true;
}

bool GPU_HW_D3D12::CreateUniformBuffer()
{
  if (!m_uniform_stream_buffer.Create(UNIFORM_BUFFER_SIZE))
    return false;

  D3D12::SetObjectName(m_vertex_stream_buffer.GetBuffer(), "Uniform Stream Buffer");
  return true;
}

bool GPU_HW_D3D12::CreateTextureBuffer()
{
  if (!m_texture_stream_buffer.Create(VRAM_UPDATE_TEXTURE_BUFFER_SIZE))
    return false;

  if (!g_d3d12_context->GetDescriptorHeapManager().Allocate(&m_texture_stream_buffer_srv))
    return false;

  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Format = DXGI_FORMAT_R16_UINT;
  desc.Buffer.NumElements = VRAM_UPDATE_TEXTURE_BUFFER_SIZE / sizeof(uint16_t);
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  g_d3d12_context->GetDevice()->CreateShaderResourceView(m_texture_stream_buffer.GetBuffer(), &desc,
                                                         m_texture_stream_buffer_srv);

  D3D12::SetObjectName(m_texture_stream_buffer.GetBuffer(), "Texture Stream Buffer");
  return true;
}

bool GPU_HW_D3D12::CompilePipelines()
{
  // Make sure no previous background-compile worker is still alive
  // from a prior CompilePipelines call (UpdateSettings triggers
  // DestroyPipelines -> CompilePipelines, and the worker from the
  // previous incarnation has to be joined before we start a new one
  // so it doesn't keep writing into the matrix the new run is about
  // to fill).
  StopShaderCompileThread();

  // Open the disk-backed shader cache once; on subsequent calls
  // (UpdateSettings -> DestroyPipelines -> CompilePipelines) the
  // instance still holds the index from last time and the disk file
  // hasn't moved, so re-opening would double-count m_shader_index /
  // m_pipeline_index entries and leak the previous RFILE* handles.
  if (!m_shader_cache.IsOpen())
  {
    m_shader_cache.Open(g_host_interface->GetShaderCacheBasePath(), g_d3d12_context->GetDevice(),
                        g_d3d12_context->GetFeatureLevel(), false);
  }
  // Convenience local reference for readability in the rest of this
  // function; the lazy helpers below also reach into m_shader_cache
  // through the member.
  D3D12::ShaderCache& shader_cache = m_shader_cache;

  m_shadergen = std::make_unique<GPU_HW_ShaderGen>(
    m_host_display->GetRenderAPI(), m_resolution_scale, m_multisamples, m_per_sample_shading, m_true_color,
    m_scaled_dithering, m_texture_filtering, m_using_uv_limits, m_pgxp_depth_buffer, m_disable_color_perspective,
    m_supports_dual_source_blend);
  GPU_HW_ShaderGen& shadergen = *m_shadergen;

  // Whether to walk the full batch / PSO matrix synchronously,
  // hand it to a background thread, or skip it entirely. See the
  // comment on GPUShaderPrecompileMode in core/types.h.
  //
  // 'precompile_sync' also gates the non-batch pipelines (VRAM
  // fill / copy / write / update depth / readback, display,
  // copy/blit, and the shared fullscreen-quad vertex shader). On
  // 'Disabled' those are faulted in via the GetXxxPipeline helpers
  // the first time the runloop actually needs them, matching the
  // documented "Disabled = no compilation at init" contract. On
  // 'Lazy' the background thread pre-fills them before walking the
  // batch matrix. On 'Enabled' we still build everything upfront
  // here.
  const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
  const bool precompile_sync = (precompile_mode == GPUShaderPrecompileMode::Enabled);
  // Reachable cell counts for each matrix - see IsBatchShaderReachable
  // in gpu_hw.h. The PSO matrix multiplies by 2 (depth_test) * 5
  // (transparency_mode); these axes don't affect reachability, so the
  // per-(render_mode, texture_mode) skip rule is independent of them.
  const uint32_t reachable_batch_cells = CountReachableBatchShaders(m_supports_dual_source_blend);
  const uint32_t batch_shader_progress_units = precompile_sync ? reachable_batch_cells : 0u;
  const uint32_t batch_pipeline_progress_units =
    precompile_sync ? reachable_batch_cells * 2u * 5u : 0u;
  // Non-batch units only counted when we build them upfront. The
  // fullscreen-quad VS (1), VRAM fill (4), VRAM copy (2), VRAM
  // write (2), VRAM update depth (1), VRAM readback (1), display
  // (6) and copy/blit (1) sum to 18 - one tick per pipeline so the
  // progress bar tracks the actual work being done.
  const uint32_t non_batch_progress_units = precompile_sync ? 18u : 0u;

  ShaderCompileProgressTracker progress("Compiling Pipelines",
                                        2 + batch_shader_progress_units + batch_pipeline_progress_units +
                                          non_batch_progress_units);

  // Vertex shaders are still always compiled synchronously - there
  // are only two (textured / not) and we need them for the lazy
  // PSO builder. Drop the previous local arrays and use the member
  // m_batch_vertex_shader_blobs / m_batch_fragment_shader_blobs so
  // GetBatchPipeline can reach them at draw time.
  auto& batch_vertex_shaders = m_batch_vertex_shader_blobs;
  auto& batch_fragment_shaders = m_batch_fragment_shader_blobs;

  for (uint8_t textured = 0; textured < 2; textured++)
  {
    const std::string vs = shadergen.GenerateBatchVertexShader(static_cast<bool>(textured));
    batch_vertex_shaders[textured] = shader_cache.GetVertexShader(vs);
    if (!batch_vertex_shaders[textured])
      return false;

    progress.Increment();
  }

  // Batch fragment shader matrix and PSO matrix. Behaviour depends
  // on g_settings.gpu_shader_precompile_mode (see core/types.h):
  //
  //   - Enabled : compile every batch fragment shader and every PSO
  //               right here, exactly like the original code path.
  //   - Lazy    : leave the matrices empty for now; spawn a worker
  //               thread at the end of CompilePipelines that fills
  //               them in the background.
  //   - Disabled: leave the matrices empty. GetBatchFragmentShader
  //               and GetBatchPipeline fault each combo in on the
  //               main thread the first time the game draws it.
  //
  // GetBatchFragmentShader / GetBatchPipeline handle the
  // Reserved_*Direct16Bit dedup internally; the precompile loop
  // doesn't need to special-case them - the second call for a
  // duplicate is a cheap pointer copy after the canonical mode has
  // filled the canonical slot.
  //
  // Structurally unreachable cells (reserved texture modes, two-pass
  // fallback render modes with no texture, single-pass dual-source
  // on hardware that lacks it) are skipped via IsBatchShaderReachable.
  if (precompile_sync)
  {
    const bool dual_source = m_supports_dual_source_blend;
    for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
    {
      for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
      {
        if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
          continue;

        for (uint8_t dithering = 0; dithering < 2; dithering++)
        {
          for (uint8_t interlacing = 0; interlacing < 2; interlacing++)
          {
            ComPtr<ID3DBlob> blob = GetBatchFragmentShader(render_mode, texture_mode,
                                                          static_cast<bool>(dithering),
                                                          static_cast<bool>(interlacing));
            if (!blob)
              return false;
            progress.Increment();
          }
        }
      }
    }

    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
      {
        for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
        {
          for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
          {
            if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
              continue;

            for (uint8_t dithering = 0; dithering < 2; dithering++)
            {
              for (uint8_t interlacing = 0; interlacing < 2; interlacing++)
              {
                ComPtr<ID3D12PipelineState> pso =
                  GetBatchPipeline(depth_test, render_mode, texture_mode, transparency_mode,
                                   static_cast<bool>(dithering), static_cast<bool>(interlacing));
                if (!pso)
                  return false;
                progress.Increment();
              }
            }
          }
        }
      }
    }
  }
  // For Lazy and Disabled: the matrices stay empty here, filled on
  // demand by GetBatchFragmentShader / GetBatchPipeline. The
  // background-thread launch for Lazy happens at the end of this
  // function, after the rest of the non-batch pipelines are built.

  // Non-batch pipelines (VRAM fill / copy / write / update depth /
  // readback, display, copy/blit, fullscreen-quad VS). On 'Enabled'
  // build them all upfront here through the lazy helpers so the
  // helpers' GetXxx slot-fill machinery is exercised the same way
  // it would be at draw time. The progress bar continues to tick
  // per pipeline so the user sees the same "1 + 4 + 2 + 2 + 1 + 1
  // + 6 + 1 = 18 unit" count it always did.
  //
  // On 'Lazy' the worker thread reaches these via the same helpers
  // before walking the batch matrix. On 'Disabled' nothing happens
  // here at all - first FillVRAM / CopyVRAM / etc. on the runloop
  // faults the corresponding pipeline in and stores the result for
  // subsequent calls.
  if (precompile_sync)
  {
    if (!GetFullscreenQuadVertexShader())
      return false;
    progress.Increment();

    for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
    {
      for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
      {
        if (!GetVRAMFillPipeline(wrapped, interlaced))
          return false;
        progress.Increment();
      }
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMCopyPipeline(depth_test))
        return false;
      progress.Increment();
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMWritePipeline(depth_test))
        return false;
      progress.Increment();
    }
    if (!GetVRAMUpdateDepthPipeline())
      return false;
    progress.Increment();
    if (!GetVRAMReadbackPipeline())
      return false;
    progress.Increment();
    for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
    {
      for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
      {
        if (!GetDisplayPipeline(depth_24, interlace_mode))
          return false;
        progress.Increment();
      }
    }
    if (!GetCopyPipeline())
      return false;
    progress.Increment();
  }

  if (precompile_mode == GPUShaderPrecompileMode::Lazy)
  {
    // Pre-fill the non-batch pipelines on the main thread BEFORE
    // launching the worker. This is mandatory, not an optimisation -
    // without it the runloop's UpdateDepthBufferFromMaskBit() call at
    // the end of Initialize() (and the first FillVRAM / UpdateDisplay
    // etc. on the first frame after) goes to the corresponding
    // GetXxxPipeline helper, which would have to take
    // m_batch_shader_mutex - the same mutex the worker holds for the
    // entire duration of each batch PSO compile. With 1440 batch PSOs
    // averaging 20-50ms each on a modern GPU, std::mutex's lack of
    // fairness on Windows means the main thread can starve for the
    // entire worker run (30-60 seconds) waiting for a lock window
    // between compiles. The window appears frozen the whole time.
    //
    // Pre-filling the 18 non-batch pipelines here costs Lazy the same
    // few-hundred-ms upfront pause 'Enabled' pays for its non-batch
    // section, which is well under a second on the 5090 and not
    // perceived as a "frozen" window. The worker then only walks the
    // batch matrix - the main thread doesn't generally need batch
    // PSOs until a few frames into gameplay, by which point the
    // worker is past its first few cells and the wait window is brief.
    if (!GetFullscreenQuadVertexShader())
      return false;
    for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
    {
      for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
      {
        if (!GetVRAMFillPipeline(wrapped, interlaced))
          return false;
      }
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMCopyPipeline(depth_test))
        return false;
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMWritePipeline(depth_test))
        return false;
    }
    if (!GetVRAMUpdateDepthPipeline())
      return false;
    if (!GetVRAMReadbackPipeline())
      return false;
    for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
    {
      for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
      {
        if (!GetDisplayPipeline(depth_24, interlace_mode))
          return false;
      }
    }
    if (!GetCopyPipeline())
      return false;

    // Kick off the background batch / PSO fill so gameplay can start
    // immediately. The worker walks the full PSO matrix in
    // (depth_test, render_mode, transparency_mode, texture_mode,
    // dithering, interlacing) order, calling the same
    // GetBatchPipeline the draw path uses; the main thread can race
    // ahead and pre-fill any slot it actually needs at draw time, and
    // the worker's recheck-under-lock pattern just observes the
    // filled slot and moves on. DestroyPipelines signals
    // m_shader_compile_thread_quit and joins.
    m_shader_compile_thread_quit.store(false, std::memory_order_relaxed);
    m_shader_compile_thread = std::thread(&GPU_HW_D3D12::ShaderCompileThreadEntryPoint, this);
  }

  return true;
}

void GPU_HW_D3D12::StopShaderCompileThread()
{
  if (!m_shader_compile_thread.joinable())
    return;

  m_shader_compile_thread_quit.store(true, std::memory_order_relaxed);
  m_shader_compile_thread.join();
  m_shader_compile_thread_quit.store(false, std::memory_order_relaxed);
}

void GPU_HW_D3D12::ShaderCompileThreadEntryPoint()
{
  // Lower this worker's scheduling priority to "below normal" so
  // it doesn't compete with the runloop / CPU emulation / audio
  // threads on CPU-contended systems. Best-effort: if the platform
  // refuses we just keep going at default priority. See
  // common/thread_priority.h for the per-platform mechanics.
  ThreadPriority::LowerCurrentThreadPriority();

  // The non-batch pipelines (VRAM ops, display, copy/blit) are
  // pre-filled by the main thread in CompilePipelines before the
  // worker is launched - see the comment block on the Lazy branch
  // there for why. The worker only walks the batch matrix.
  //
  // Walks the PSO matrix in (depth_test, render_mode,
  // transparency_mode, texture_mode, dithering, interlacing) order
  // - the same order CompilePipelines uses for the Enabled precompile
  // loop - and calls GetBatchPipeline on each cell. GetBatchPipeline
  // internally calls GetBatchFragmentShader for the bound shader, so
  // the worker fills both the fragment-shader-blob array and the PSO
  // array as it goes. The quit flag is checked between cells so
  // DestroyPipelines can bring the worker down within at most one
  // PSO compile worth of latency (D3D12 PSO compiles can be ~50-200
  // ms for the heavier filters - the same cell-level bound applies
  // to ShaderCompileThreadEntryPoint on the D3D11 side).
  //
  // Structurally unreachable cells are skipped via
  // IsBatchShaderReachable.
  const bool dual_source = m_supports_dual_source_blend;
  for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
  {
    for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
    {
      for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
      {
        for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
        {
          if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
            continue;

          for (uint8_t dithering = 0; dithering < 2; dithering++)
          {
            for (uint8_t interlacing = 0; interlacing < 2; interlacing++)
            {
              if (m_shader_compile_thread_quit.load(std::memory_order_relaxed))
                return;

              GetBatchPipeline(depth_test, render_mode, texture_mode, transparency_mode,
                               static_cast<bool>(dithering), static_cast<bool>(interlacing));
            }
          }
        }
      }
    }
  }
}

GPU_HW_D3D12::ComPtr<ID3DBlob> GPU_HW_D3D12::GetBatchFragmentShader(uint8_t render_mode, uint8_t texture_mode, bool dithering,
                                                                    bool interlacing)
{
  // Reserved_*Direct16Bit shader-source dedup is applied at the
  // matrix level. Two slots end up holding the same ComPtr; ID3DBlob
  // is refcounted so sharing the reference across slots is safe and
  // the destructor on DestroyPipelines just drops the refcount twice.
  // The parallel _fastpath atomic array gets the same raw pointer
  // in both slots, which is safe because the ComPtr keeps the blob
  // alive for the lifetime of the GPU backend.
  const uint8_t lookup_mode = (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_Direct16Bit))    ? 2u :
                         (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_RawDirect16Bit)) ? 6u :
                                                                                                      texture_mode;

  // Fast path: lock-free acquire-load. Both the worker (via
  // GetBatchPipeline -> here) and the main thread (same) can read
  // a slot concurrently without serialising against each other or
  // each other's slow-path compiles.
  std::atomic<ID3DBlob*>& fast_slot =
    m_batch_fragment_shader_blobs_fastpath[render_mode][texture_mode][static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)];
  ID3DBlob* existing = fast_slot.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3DBlob> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  // Slow path: compile WITHOUT m_batch_shader_mutex held. The shader
  // cache itself is thread-safe (it has its own internal locking
  // that does NOT span D3DCompile), so multiple threads can compile
  // different shaders here in parallel. Two threads compiling the
  // SAME shader is harmless - both produce identical DXBC and the
  // cache dedups internally via its own double-check.
  if (!m_shadergen)
  {
    Log_ErrorPrint("GetBatchFragmentShader called before CompilePipelines constructed the shadergen");
    return {};
  }
  const std::string fs = m_shadergen->GenerateBatchFragmentShader(
    static_cast<BatchRenderMode>(render_mode), static_cast<GPUTextureMode>(lookup_mode), dithering, interlacing);
  ComPtr<ID3DBlob> fresh_blob = m_shader_cache.GetPixelShader(fs);
  if (!fresh_blob)
  {
    Log_ErrorPrintf("Lazy batch fragment shader compile failed for (rm=%u, tm=%u, d=%u, i=%u)", render_mode,
                    texture_mode, static_cast<uint8_t>(dithering), static_cast<uint8_t>(interlacing));
    return {};
  }

  // Publish step: take the helper mutex briefly to write the
  // canonical slot, the dup slot (for Reserved_* texture modes),
  // and the fast-path atomics. The mutex window is microseconds -
  // no D3DCompile call inside it.
  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);

  existing = fast_slot.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3DBlob> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob>& canonical_slot =
    m_batch_fragment_shader_blobs[render_mode][lookup_mode][static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)];

  if (!canonical_slot)
  {
    canonical_slot = fresh_blob;
    m_batch_fragment_shader_blobs_fastpath[render_mode][lookup_mode][static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)]
      .store(canonical_slot.Get(), std::memory_order_release);
  }

  if (lookup_mode != texture_mode)
  {
    ComPtr<ID3DBlob>& dup_slot =
      m_batch_fragment_shader_blobs[render_mode][texture_mode][static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)];
    if (!dup_slot)
      dup_slot = canonical_slot;
  }

  // Publish the caller's slot.
  fast_slot.store(canonical_slot.Get(), std::memory_order_release);
  return canonical_slot;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetBatchPipeline(uint8_t depth_test, uint8_t render_mode,
                                                                         uint8_t texture_mode, uint8_t transparency_mode,
                                                                         bool dithering, bool interlacing)
{
  // Reserved_*Direct16Bit PSO dedup. Because the only texture_mode-
  // dependent input to the PSO is the bound fragment shader (which
  // is itself dedup'd in GetBatchFragmentShader), the resulting PSO
  // for the Reserved_* modes is bit-identical to the canonical mode
  // and we can share the ComPtr across both slots. The atomic
  // _fastpath array gets the same raw pointer in both slots, kept
  // alive by the ComPtr for the lifetime of the GPU backend.
  const uint8_t lookup_mode = (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_Direct16Bit))    ? 2u :
                         (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_RawDirect16Bit)) ? 6u :
                                                                                                      texture_mode;

  // Fast path: lock-free acquire-load. DrawBatchVertices hits this
  // once a slot is filled (either by the precompile worker or by
  // an earlier main-thread fault-in). No mutex, no contention
  // against the worker.
  std::atomic<ID3D12PipelineState*>& fast_slot =
    m_batch_pipelines_fastpath[depth_test][render_mode][texture_mode][transparency_mode][static_cast<uint8_t>(dithering)]
                              [static_cast<uint8_t>(interlacing)];
  ID3D12PipelineState* existing = fast_slot.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  // Slow path. Compile the PSO WITHOUT m_batch_shader_mutex held.
  // GetBatchFragmentShader is internally thread-safe and so is
  // m_shader_cache.GetPipelineState (via the per-instance mutexes
  // added inside D3D12::ShaderCache). The actual D3DCompile and
  // CreateGraphicsPipelineState calls run lock-free, so the worker
  // compiling slot A doesn't block the main thread compiling
  // slot B. Two threads racing to compile the SAME slot is
  // wasteful but harmless - both produce equivalent PSOs and the
  // double-check below picks the winner.
  ComPtr<ID3DBlob> fs_blob = GetBatchFragmentShader(render_mode, lookup_mode, dithering, interlacing);
  if (!fs_blob)
    return {};

  const bool textured = (static_cast<GPUTextureMode>(lookup_mode) != GPUTextureMode::Disabled);

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_batch_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());

  gpbuilder.AddVertexAttribute("ATTR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(BatchVertex, x));
  gpbuilder.AddVertexAttribute("ATTR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(BatchVertex, color));
  if (textured)
  {
    gpbuilder.AddVertexAttribute("ATTR", 2, DXGI_FORMAT_R32_UINT, 0, offsetof(BatchVertex, u));
    gpbuilder.AddVertexAttribute("ATTR", 3, DXGI_FORMAT_R32_UINT, 0, offsetof(BatchVertex, texpage));
    if (m_using_uv_limits)
      gpbuilder.AddVertexAttribute("ATTR", 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(BatchVertex, uv_limits));
  }

  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(m_batch_vertex_shader_blobs[static_cast<uint8_t>(textured)].Get());
  gpbuilder.SetPixelShader(fs_blob.Get());

  gpbuilder.SetRasterizationState(D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, false);
  gpbuilder.SetDepthState(true, true,
                          (depth_test != 0) ? (m_pgxp_depth_buffer ? D3D12_COMPARISON_FUNC_LESS_EQUAL :
                                                                     D3D12_COMPARISON_FUNC_GREATER_EQUAL) :
                                              D3D12_COMPARISON_FUNC_ALWAYS);
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetMultisamples(m_multisamples);

  if ((static_cast<GPUTransparencyMode>(transparency_mode) != GPUTransparencyMode::Disabled &&
       (static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
        static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque)) ||
      m_texture_filtering != GPUTextureFilter::Nearest)
  {
    gpbuilder.SetBlendState(
      0, true, D3D12_BLEND_ONE,
      m_supports_dual_source_blend ? D3D12_BLEND_SRC1_ALPHA : D3D12_BLEND_SRC_ALPHA,
      (static_cast<GPUTransparencyMode>(transparency_mode) == GPUTransparencyMode::BackgroundMinusForeground &&
       static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
       static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque) ?
        D3D12_BLEND_OP_REV_SUBTRACT :
        D3D12_BLEND_OP_ADD,
      D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD);
  }

  ComPtr<ID3D12PipelineState> fresh_pso = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache);
  if (!fresh_pso)
  {
    Log_ErrorPrintf("Lazy batch PSO compile failed for (dt=%u, rm=%u, tm=%u, tr=%u, d=%u, i=%u)", depth_test,
                    render_mode, texture_mode, transparency_mode, static_cast<uint8_t>(dithering), static_cast<uint8_t>(interlacing));
    return {};
  }

  // Publish step: take the helper mutex briefly to write the
  // canonical slot, the dup slot (for Reserved_* texture modes),
  // and the fast-path atomics. The mutex window is microseconds -
  // no D3DCompile or CreateGraphicsPipelineState inside it.
  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);

  // Double-check the fast slot under the lock.
  existing = fast_slot.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3D12PipelineState>& canonical_slot =
    m_batch_pipelines[depth_test][render_mode][lookup_mode][transparency_mode][static_cast<uint8_t>(dithering)]
                     [static_cast<uint8_t>(interlacing)];

  if (!canonical_slot)
  {
    canonical_slot = fresh_pso;
    D3D12::SetObjectNameFormatted(canonical_slot.Get(), "Batch Pipeline %u,%u,%u,%u,%u,%u", depth_test, render_mode,
                                  lookup_mode, transparency_mode, static_cast<uint8_t>(dithering), static_cast<uint8_t>(interlacing));

    // Publish the canonical raw pointer for future fast-path
    // readers of the canonical slot.
    m_batch_pipelines_fastpath[depth_test][render_mode][lookup_mode][transparency_mode][static_cast<uint8_t>(dithering)]
                              [static_cast<uint8_t>(interlacing)]
                                .store(canonical_slot.Get(), std::memory_order_release);
  }

  if (lookup_mode != texture_mode)
  {
    ComPtr<ID3D12PipelineState>& dup_slot =
      m_batch_pipelines[depth_test][render_mode][texture_mode][transparency_mode][static_cast<uint8_t>(dithering)]
                       [static_cast<uint8_t>(interlacing)];
    if (!dup_slot)
      dup_slot = canonical_slot;
  }

  // Publish the caller's slot.
  fast_slot.store(canonical_slot.Get(), std::memory_order_release);
  return canonical_slot;
}

// ----------------------------------------------------------------------
// Non-batch lazy helpers. Same fast-path / slow-path layout as
// GetBatchPipeline: an acquire-load on an atomic raw-pointer fast
// path, falling back to the ComPtr slot under m_batch_shader_mutex
// on a miss. The fast path is one uncontended atomic load per call
// once the slot is filled.
//
// The shared fullscreen-quad vertex shader has its own helper that
// the PSO helpers fall through to. That keeps each fault-in
// self-contained: the very first non-batch helper call from a
// 'Disabled' session pays for the VS compile, every subsequent call
// reuses it.
// ----------------------------------------------------------------------

GPU_HW_D3D12::ComPtr<ID3DBlob> GPU_HW_D3D12::GetFullscreenQuadVertexShader()
{
  // Single-slot variant of the fast/slow path. Fast path: read the
  // ComPtr under the lock (cheap - just a pointer copy + AddRef).
  // Slow path: compile WITHOUT the lock held (so concurrent
  // non-batch helpers can fault other pipelines while one compiles
  // the shared VS), then publish under the lock with a double-check.
  {
    std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
    if (m_fullscreen_quad_vertex_shader)
      return m_fullscreen_quad_vertex_shader;
  }

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetFullscreenQuadVertexShader called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fresh = m_shader_cache.GetVertexShader(m_shadergen->GenerateScreenQuadVertexShader());
  if (!fresh)
  {
    Log_ErrorPrint("Lazy fullscreen-quad vertex shader compile failed");
    return {};
  }

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  if (!m_fullscreen_quad_vertex_shader)
    m_fullscreen_quad_vertex_shader = fresh;
  return m_fullscreen_quad_vertex_shader;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMFillPipeline(uint8_t wrapped, uint8_t interlaced)
{
  std::atomic<ID3D12PipelineState*>& fast_slot = m_vram_fill_pipelines_fastpath[wrapped][interlaced];
  ID3D12PipelineState* existing = fast_slot.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetVRAMFillPipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(
    m_shadergen->GenerateVRAMFillFragmentShader(static_cast<bool>(wrapped), static_cast<bool>(interlaced)));
  if (!fs)
    return {};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState(true, true, D3D12_COMPARISON_FUNC_ALWAYS);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  // Publish under brief lock; double-check for race winner.
  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = fast_slot.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  ComPtr<ID3D12PipelineState>& slot = m_vram_fill_pipelines[wrapped][interlaced];
  if (!slot)
  {
    slot = fresh;
    D3D12::SetObjectNameFormatted(slot.Get(), "VRAM Fill Pipeline Wrapped=%u,Interlacing=%u", wrapped, interlaced);
  }
  fast_slot.store(slot.Get(), std::memory_order_release);
  return slot;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMCopyPipeline(uint8_t depth_test)
{
  std::atomic<ID3D12PipelineState*>& fast_slot = m_vram_copy_pipelines_fastpath[depth_test];
  ID3D12PipelineState* existing = fast_slot.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetVRAMCopyPipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(m_shadergen->GenerateVRAMCopyFragmentShader());
  if (!fs)
    return {};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState((depth_test != 0), true,
                          (depth_test != 0) ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = fast_slot.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  ComPtr<ID3D12PipelineState>& slot = m_vram_copy_pipelines[depth_test];
  if (!slot)
  {
    slot = fresh;
    D3D12::SetObjectNameFormatted(slot.Get(), "VRAM Copy Pipeline Depth=%u", depth_test);
  }
  fast_slot.store(slot.Get(), std::memory_order_release);
  return slot;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMWritePipeline(uint8_t depth_test)
{
  std::atomic<ID3D12PipelineState*>& fast_slot = m_vram_write_pipelines_fastpath[depth_test];
  ID3D12PipelineState* existing = fast_slot.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetVRAMWritePipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(m_shadergen->GenerateVRAMWriteFragmentShader(false));
  if (!fs)
    return {};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState(true, true,
                          (depth_test != 0) ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = fast_slot.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  ComPtr<ID3D12PipelineState>& slot = m_vram_write_pipelines[depth_test];
  if (!slot)
  {
    slot = fresh;
    D3D12::SetObjectNameFormatted(slot.Get(), "VRAM Write Pipeline Depth=%u", depth_test);
  }
  fast_slot.store(slot.Get(), std::memory_order_release);
  return slot;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMUpdateDepthPipeline()
{
  ID3D12PipelineState* existing = m_vram_update_depth_pipeline_fastpath.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetVRAMUpdateDepthPipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(m_shadergen->GenerateVRAMUpdateDepthFragmentShader());
  if (!fs)
    return {};

  // VRAM update depth differs from the other VRAM ops in three
  // ways: it uses m_batch_root_signature (the regular ops use the
  // single-sampler root sig), it writes to depth only (no render
  // targets - ClearRenderTargets() drops the RT[0] entry the
  // helper would otherwise inherit), and it carries an explicit
  // blend state. Everything else - VS, topology, cull, multisamples,
  // depth-stencil format, depth-state - matches the standard
  // fullscreen-quad pipeline the other VRAM ops use.
  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_batch_root_signature.Get());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState(true, true, D3D12_COMPARISON_FUNC_ALWAYS);
  gpbuilder.SetBlendState(0, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE,
                          D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, 0);
  gpbuilder.ClearRenderTargets();

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = m_vram_update_depth_pipeline_fastpath.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  if (!m_vram_update_depth_pipeline)
  {
    m_vram_update_depth_pipeline = fresh;
    D3D12::SetObjectName(m_vram_update_depth_pipeline.Get(), "VRAM Update Depth Pipeline");
  }
  m_vram_update_depth_pipeline_fastpath.store(m_vram_update_depth_pipeline.Get(), std::memory_order_release);
  return m_vram_update_depth_pipeline;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMReadbackPipeline()
{
  ID3D12PipelineState* existing = m_vram_readback_pipeline_fastpath.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetVRAMReadbackPipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(m_shadergen->GenerateVRAMReadFragmentShader());
  if (!fs)
    return {};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, m_vram_readback_texture.GetFormat());
  gpbuilder.ClearDepthStencilFormat();

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = m_vram_readback_pipeline_fastpath.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  if (!m_vram_readback_pipeline)
  {
    m_vram_readback_pipeline = fresh;
    D3D12::SetObjectName(m_vram_readback_pipeline.Get(), "VRAM Readback Pipeline");
  }
  m_vram_readback_pipeline_fastpath.store(m_vram_readback_pipeline.Get(), std::memory_order_release);
  return m_vram_readback_pipeline;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetDisplayPipeline(uint8_t depth_24, uint8_t interlace_mode)
{
  std::atomic<ID3D12PipelineState*>& fast_slot = m_display_pipelines_fastpath[depth_24][interlace_mode];
  ID3D12PipelineState* existing = fast_slot.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetDisplayPipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(m_shadergen->GenerateDisplayFragmentShader(
    static_cast<bool>(depth_24), static_cast<InterlacedRenderMode>(interlace_mode), m_chroma_smoothing));
  if (!fs)
    return {};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, m_display_texture.GetFormat());

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = fast_slot.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  ComPtr<ID3D12PipelineState>& slot = m_display_pipelines[depth_24][interlace_mode];
  if (!slot)
  {
    slot = fresh;
    D3D12::SetObjectNameFormatted(slot.Get(), "Display Pipeline Depth=%u Interlace=%u", depth_24, interlace_mode);
  }
  fast_slot.store(slot.Get(), std::memory_order_release);
  return slot;
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetCopyPipeline()
{
  ID3D12PipelineState* existing = m_copy_pipeline_fastpath.load(std::memory_order_acquire);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }

  ComPtr<ID3DBlob> vs = GetFullscreenQuadVertexShader();
  if (!vs)
    return {};

  if (!m_shadergen)
  {
    Log_ErrorPrint("GetCopyPipeline called before CompilePipelines constructed the shadergen");
    return {};
  }
  ComPtr<ID3DBlob> fs = m_shader_cache.GetPixelShader(m_shadergen->GenerateCopyFragmentShader());
  if (!fs)
    return {};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(vs.Get());
  gpbuilder.SetPixelShader(fs.Get());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, DXGI_FORMAT_R8G8B8A8_UNORM);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  std::lock_guard<std::mutex> lock(m_batch_shader_mutex);
  existing = m_copy_pipeline_fastpath.load(std::memory_order_relaxed);
  if (existing)
  {
    ComPtr<ID3D12PipelineState> ret;
    ret.Attach(existing);
    existing->AddRef();
    return ret;
  }
  if (!m_copy_pipeline)
  {
    m_copy_pipeline = fresh;
    D3D12::SetObjectName(m_copy_pipeline.Get(), "Copy/Blit Pipeline");
  }
  m_copy_pipeline_fastpath.store(m_copy_pipeline.Get(), std::memory_order_release);
  return m_copy_pipeline;
}

void GPU_HW_D3D12::DestroyPipelines()
{
  // Tear down the background-compile worker before clearing the
  // matrix - otherwise it would be writing into ComPtrs we're about
  // to default-construct.
  StopShaderCompileThread();
  m_shadergen.reset();

  // Clear the atomic fast-path views BEFORE dropping the ComPtr
  // ownership so a hypothetical concurrent reader couldn't see a
  // raw pointer pointing to a just-freed object. By this point the
  // worker is stopped and the runloop isn't drawing (UpdateSettings
  // is the only call site that runs DestroyPipelines, and it's on
  // the runloop thread), so memory_order_relaxed is sufficient.
  m_batch_pipelines_fastpath.enumerate(
    [](std::atomic<ID3D12PipelineState*>& s) { s.store(nullptr, std::memory_order_relaxed); });
  m_batch_fragment_shader_blobs_fastpath.enumerate(
    [](std::atomic<ID3DBlob*>& s) { s.store(nullptr, std::memory_order_relaxed); });

  // Same pattern for the non-batch fast-path mirrors added when
  // these pipelines moved onto the lazy-fault path.
  m_vram_fill_pipelines_fastpath.enumerate(
    [](std::atomic<ID3D12PipelineState*>& s) { s.store(nullptr, std::memory_order_relaxed); });
  for (auto& s : m_vram_copy_pipelines_fastpath)
    s.store(nullptr, std::memory_order_relaxed);
  for (auto& s : m_vram_write_pipelines_fastpath)
    s.store(nullptr, std::memory_order_relaxed);
  m_display_pipelines_fastpath.enumerate(
    [](std::atomic<ID3D12PipelineState*>& s) { s.store(nullptr, std::memory_order_relaxed); });
  m_vram_readback_pipeline_fastpath.store(nullptr, std::memory_order_relaxed);
  m_vram_update_depth_pipeline_fastpath.store(nullptr, std::memory_order_relaxed);
  m_copy_pipeline_fastpath.store(nullptr, std::memory_order_relaxed);

  m_batch_pipelines = {};
  m_vram_fill_pipelines = {};
  m_vram_write_pipelines = {};
  m_vram_copy_pipelines = {};
  m_vram_readback_pipeline.Reset();
  m_vram_update_depth_pipeline.Reset();

  m_batch_fragment_shader_blobs = {};
  m_batch_vertex_shader_blobs = {};

  m_display_pipelines = {};

  // m_copy_pipeline was previously not cleared here - latent leak
  // across UpdateSettings -> DestroyPipelines -> CompilePipelines
  // cycles. The shared fullscreen-quad VS likewise needs to be
  // dropped so the next CompilePipelines pass picks up any
  // shadergen changes (e.g. the GLSL binding layout differing
  // between Vulkan and Direct3D).
  m_copy_pipeline.Reset();
  m_fullscreen_quad_vertex_shader.Reset();
}

bool GPU_HW_D3D12::CreateTextureReplacementStreamBuffer()
{
  if (m_texture_replacment_stream_buffer.IsValid())
    return true;
  if (!m_texture_replacment_stream_buffer.Create(TEXTURE_REPLACEMENT_BUFFER_SIZE))
    return false;
  return true;
}

bool GPU_HW_D3D12::BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width,
                                              uint32_t height)
{
  if (!CreateTextureReplacementStreamBuffer())
    return false;

  if (m_vram_write_replacement_texture.GetWidth() < tex->GetWidth() ||
      m_vram_write_replacement_texture.GetHeight() < tex->GetHeight())
  {
    if (!m_vram_write_replacement_texture.Create(tex->GetWidth(), tex->GetHeight(), 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                 DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
                                                 D3D12_RESOURCE_FLAG_NONE))
      return false;
  }

  const uint32_t copy_pitch = Common::AlignUpPow2<uint32_t>(tex->GetWidth() * sizeof(uint32_t), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
  const uint32_t required_size = copy_pitch * tex->GetHeight();
  if (!m_texture_replacment_stream_buffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    if (!m_texture_replacment_stream_buffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
      return false;
  }

  // buffer -> texture
  const uint32_t sb_offset = m_texture_replacment_stream_buffer.GetCurrentOffset();
  D3D12::Texture::CopyToUploadBuffer(tex->GetPixels(), tex->GetByteStride(), tex->GetHeight(),
                                     m_texture_replacment_stream_buffer.GetCurrentHostPointer(), copy_pitch);
  m_texture_replacment_stream_buffer.CommitMemory(sb_offset);
  m_vram_write_replacement_texture.CopyFromBuffer(0, 0, tex->GetWidth(), tex->GetHeight(), copy_pitch,
                                                  m_texture_replacment_stream_buffer.GetBuffer(), sb_offset);
  m_vram_write_replacement_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // texture -> vram
  const float uniforms[] = {
    0.0f, 0.0f, static_cast<float>(tex->GetWidth()) / static_cast<float>(m_vram_write_replacement_texture.GetWidth()),
    static_cast<float>(tex->GetHeight()) / static_cast<float>(m_vram_write_replacement_texture.GetHeight())};
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_write_replacement_texture.GetSRVDescriptor());
  cmdlist->SetGraphicsRootDescriptorTable(2, m_linear_sampler.gpu_handle);
  ComPtr<ID3D12PipelineState> copy_pso = GetCopyPipeline();
  if (!copy_pso)
    return false;
  cmdlist->SetPipelineState(copy_pso.Get());
  D3D12::SetViewportAndScissor(cmdlist, dst_x, dst_y, width, height);
  cmdlist->DrawInstanced(3, 1, 0, 0);
  RestoreGraphicsAPIState();
  return true;
}

void GPU_HW_D3D12::DrawBatchVertices(BatchRenderMode render_mode, uint32_t base_vertex, uint32_t num_vertices)
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  // Fetch the batch PSO via the lazy helper. In 'Enabled' precompile
  // mode every slot was filled at CompilePipelines time so this is a
  // fast mutex-protected pointer load. In 'Lazy' mode this either
  // gets the already-compiled PSO (background thread reached it
  // first) or compiles it now on the main thread (game raced ahead
  // of the worker). In 'Disabled' mode it always compiles on miss.
  // The mutex serialises both the PSO matrix and the shader-cache
  // mutation; cost is ~20 ns uncontended per modern std::mutex impl.
  //
  // [depth_test][render_mode][texture_mode][transparency_mode][dithering][interlacing]
  const uint8_t depth_test = static_cast<uint8_t>(m_batch.check_mask_before_draw || m_batch.use_depth_buffer);
  ComPtr<ID3D12PipelineState> pipeline =
    GetBatchPipeline(depth_test, static_cast<uint8_t>(render_mode), static_cast<uint8_t>(m_batch.texture_mode),
                     static_cast<uint8_t>(m_batch.transparency_mode), m_batch.dithering, m_batch.interlacing);

  cmdlist->SetPipelineState(pipeline.Get());
  cmdlist->DrawInstanced(num_vertices, 1, base_vertex, 0);
}

void GPU_HW_D3D12::SetScissorFromDrawingArea()
{
  int left, top, right, bottom;
  CalcScissorRect(&left, &top, &right, &bottom);

  D3D12::SetScissor(g_d3d12_context->GetCommandList(), left, top, right - left, bottom - top);
}

void GPU_HW_D3D12::ClearDisplay()
{
  GPU_HW::ClearDisplay();

  m_host_display->ClearDisplayTexture();

  static constexpr float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  g_d3d12_context->GetCommandList()->ClearRenderTargetView(m_display_texture.GetRTVOrDSVDescriptor(), clear_color, 0,
                                                           nullptr);
}

void GPU_HW_D3D12::UpdateDisplay()
{
  GPU_HW::UpdateDisplay();

  {
    m_host_display->SetDisplayParameters(m_crtc_state.display_width, m_crtc_state.display_height,
                                         m_crtc_state.display_origin_left, m_crtc_state.display_origin_top,
                                         m_crtc_state.display_vram_width, m_crtc_state.display_vram_height,
                                         GetDisplayAspectRatio());

    const uint32_t resolution_scale = m_GPUSTAT.display_area_color_depth_24 ? 1 : m_resolution_scale;
    const uint32_t vram_offset_x = m_crtc_state.display_vram_left;
    const uint32_t vram_offset_y = m_crtc_state.display_vram_top;
    const uint32_t scaled_vram_offset_x = vram_offset_x * resolution_scale;
    const uint32_t scaled_vram_offset_y = vram_offset_y * resolution_scale;
    const uint32_t display_width = m_crtc_state.display_vram_width;
    const uint32_t display_height = m_crtc_state.display_vram_height;
    const uint32_t scaled_display_width = display_width * resolution_scale;
    const uint32_t scaled_display_height = display_height * resolution_scale;
    const InterlacedRenderMode interlaced = GetInterlacedRenderMode();

    if (IsDisplayDisabled())
    {
      m_host_display->ClearDisplayTexture();
    }
    else if (!m_GPUSTAT.display_area_color_depth_24 && interlaced == InterlacedRenderMode::None &&
             !IsUsingMultisampling() && (scaled_vram_offset_x + scaled_display_width) <= m_vram_texture.GetWidth() &&
             (scaled_vram_offset_y + scaled_display_height) <= m_vram_texture.GetHeight())
    {
      m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      m_host_display->SetDisplayTexture(&m_vram_texture, HostDisplayPixelFormat::RGBA8, m_vram_texture.GetWidth(),
                                        m_vram_texture.GetHeight(), scaled_vram_offset_x, scaled_vram_offset_y,
                                        scaled_display_width, scaled_display_height);
    }
    else
    {
      const uint32_t reinterpret_field_offset = (interlaced != InterlacedRenderMode::None) ? GetInterlacedDisplayField() : 0;
      const uint32_t reinterpret_start_x = m_crtc_state.regs.X * resolution_scale;
      const uint32_t reinterpret_crop_left = (m_crtc_state.display_vram_left - m_crtc_state.regs.X) * resolution_scale;
      const uint32_t uniforms[4] = {reinterpret_start_x, scaled_vram_offset_y + reinterpret_field_offset,
                               reinterpret_crop_left, reinterpret_field_offset};

      ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
      m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
      m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

      cmdlist->OMSetRenderTargets(1, &m_display_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
      cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
      cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
      cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_texture.GetSRVDescriptor());
      ComPtr<ID3D12PipelineState> display_pso =
        GetDisplayPipeline(static_cast<uint8_t>(m_GPUSTAT.display_area_color_depth_24), static_cast<uint8_t>(interlaced));
      if (!display_pso)
        return;
      cmdlist->SetPipelineState(display_pso.Get());
      D3D12::SetViewportAndScissor(cmdlist, 0, 0, scaled_display_width, scaled_display_height);
      cmdlist->DrawInstanced(3, 1, 0, 0);

      m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

      m_host_display->SetDisplayTexture(&m_display_texture, HostDisplayPixelFormat::RGBA8, m_display_texture.GetWidth(),
                                        m_display_texture.GetHeight(), 0, 0, scaled_display_width,
                                        scaled_display_height);

      RestoreGraphicsAPIState();
    }
  }
}

void GPU_HW_D3D12::ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
  if (IsUsingSoftwareRendererForReadbacks())
  {
    ReadSoftwareRendererVRAM(x, y, width, height);
    return;
  }

  // Get bounds with wrap-around handled.
  const Common::Rectangle<uint32_t> copy_rect = GetVRAMTransferBounds(x, y, width, height);
  const uint32_t encoded_width = (copy_rect.GetWidth() + 1) / 2;
  const uint32_t encoded_height = copy_rect.GetHeight();

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_vram_readback_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  // Encode the 24-bit texture as 16-bit.
  const uint32_t uniforms[4] = {copy_rect.left, copy_rect.top, copy_rect.GetWidth(), copy_rect.GetHeight()};
  cmdlist->OMSetRenderTargets(1, &m_vram_readback_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_texture.GetSRVDescriptor());
  ComPtr<ID3D12PipelineState> readback_pso = GetVRAMReadbackPipeline();
  if (!readback_pso)
    return;
  cmdlist->SetPipelineState(readback_pso.Get());
  D3D12::SetViewportAndScissor(cmdlist, 0, 0, encoded_width, encoded_height);
  cmdlist->DrawInstanced(3, 1, 0, 0);

  m_vram_readback_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  // Stage the readback.
  m_vram_readback_staging_texture.CopyFromTexture(m_vram_readback_texture, 0, 0, 0, 0, 0, encoded_width,
                                                  encoded_height);

  // And copy it into our shadow buffer (will execute command buffer and stall).
  m_vram_readback_staging_texture.ReadPixels(0, 0, encoded_width, encoded_height,
                                             &m_vram_shadow[copy_rect.top * VRAM_WIDTH + copy_rect.left],
                                             VRAM_WIDTH * sizeof(uint16_t));

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
  if (IsUsingSoftwareRendererForReadbacks())
    FillSoftwareRendererVRAM(x, y, width, height, color);

  // TODO: Use fast clear
  GPU_HW::FillVRAM(x, y, width, height, color);

  const VRAMFillUBOData uniforms = GetVRAMFillUBOData(x, y, width, height, color);
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  const bool wrapped = IsVRAMFillOversized(x, y, width, height);
  const bool interlaced = IsInterlacedRenderingEnabled();
  if (!wrapped && !interlaced)
  {
    const D3D12_RECT rc = {static_cast<LONG>(x * m_resolution_scale), static_cast<LONG>(y * m_resolution_scale),
                           static_cast<LONG>((x + width) * m_resolution_scale),
                           static_cast<LONG>((y + height) * m_resolution_scale)};
    cmdlist->ClearRenderTargetView(m_vram_texture.GetRTVOrDSVDescriptor(), uniforms.u_fill_color, 1, &rc);
    cmdlist->ClearDepthStencilView(m_vram_depth_texture.GetRTVOrDSVDescriptor(), D3D12_CLEAR_FLAG_DEPTH,
                                   uniforms.u_fill_color[3], 0, 1, &rc);
    return;
  }

  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), &uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, g_d3d12_context->GetNullSRVDescriptor());
  ComPtr<ID3D12PipelineState> fill_pso =
    GetVRAMFillPipeline(static_cast<uint8_t>(IsVRAMFillOversized(x, y, width, height)),
                        static_cast<uint8_t>(IsInterlacedRenderingEnabled()));
  if (!fill_pso)
    return;
  cmdlist->SetPipelineState(fill_pso.Get());

  const Common::Rectangle<uint32_t> bounds(GetVRAMTransferBounds(x, y, width, height));
  D3D12::SetViewportAndScissor(cmdlist, bounds.left * m_resolution_scale, bounds.top * m_resolution_scale,
                               bounds.GetWidth() * m_resolution_scale, bounds.GetHeight() * m_resolution_scale);

  cmdlist->DrawInstanced(3, 1, 0, 0);

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask)
{
  if (IsUsingSoftwareRendererForReadbacks())
    UpdateSoftwareRendererVRAM(x, y, width, height, data, set_mask, check_mask);

  const Common::Rectangle<uint32_t> bounds = GetVRAMTransferBounds(x, y, width, height);
  GPU_HW::UpdateVRAM(bounds.left, bounds.top, bounds.GetWidth(), bounds.GetHeight(), data, set_mask, check_mask);

  if (!check_mask)
  {
    const TextureReplacementTexture* rtex = g_texture_replacements.GetVRAMWriteReplacement(width, height, data);
    if (rtex && BlitVRAMReplacementTexture(rtex, x * m_resolution_scale, y * m_resolution_scale,
                                           width * m_resolution_scale, height * m_resolution_scale))
    {
      return;
    }
  }

  const uint32_t data_size = width * height * sizeof(uint16_t);
  const uint32_t alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; // ???
  if (!m_texture_stream_buffer.ReserveMemory(data_size, alignment))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    if (!m_texture_stream_buffer.ReserveMemory(data_size, alignment))
      return;
  }

  const uint32_t start_index = m_texture_stream_buffer.GetCurrentOffset() / sizeof(uint16_t);
  std::memcpy(m_texture_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_texture_stream_buffer.CommitMemory(data_size);

  const VRAMWriteUBOData uniforms = GetVRAMWriteUBOData(x, y, width, height, start_index, set_mask, check_mask);

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), &uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_texture_stream_buffer_srv);
  ComPtr<ID3D12PipelineState> write_pso = GetVRAMWritePipeline(static_cast<uint8_t>(check_mask));
  if (!write_pso)
    return;
  cmdlist->SetPipelineState(write_pso.Get());

  // the viewport should already be set to the full vram, so just adjust the scissor
  const Common::Rectangle<uint32_t> scaled_bounds = bounds * m_resolution_scale;
  D3D12::SetScissor(cmdlist, scaled_bounds.left, scaled_bounds.top, scaled_bounds.GetWidth(),
                    scaled_bounds.GetHeight());

  cmdlist->DrawInstanced(3, 1, 0, 0);

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height)
{
  if (IsUsingSoftwareRendererForReadbacks())
    CopySoftwareRendererVRAM(src_x, src_y, dst_x, dst_y, width, height);

  if (UseVRAMCopyShader(src_x, src_y, dst_x, dst_y, width, height) || IsUsingMultisampling())
  {
    const Common::Rectangle<uint32_t> src_bounds = GetVRAMTransferBounds(src_x, src_y, width, height);
    const Common::Rectangle<uint32_t> dst_bounds = GetVRAMTransferBounds(dst_x, dst_y, width, height);
    if (m_vram_dirty_rect.Intersects(src_bounds))
      UpdateVRAMReadTexture();
    IncludeVRAMDirtyRectangle(dst_bounds);

    const VRAMCopyUBOData uniforms(GetVRAMCopyUBOData(src_x, src_y, dst_x, dst_y, width, height));
    const Common::Rectangle<uint32_t> dst_bounds_scaled(dst_bounds * m_resolution_scale);

    ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
    cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
    cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), &uniforms, 0);
    cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_read_texture.GetSRVDescriptor());
    ComPtr<ID3D12PipelineState> copy_pso = GetVRAMCopyPipeline(static_cast<uint8_t>(m_GPUSTAT.check_mask_before_draw));
    if (!copy_pso)
      return;
    cmdlist->SetPipelineState(copy_pso.Get());
    D3D12::SetViewportAndScissor(cmdlist, dst_bounds_scaled.left, dst_bounds_scaled.top, dst_bounds_scaled.GetWidth(),
                                 dst_bounds_scaled.GetHeight());
    cmdlist->DrawInstanced(3, 1, 0, 0);

    RestoreGraphicsAPIState();

    if (m_GPUSTAT.check_mask_before_draw)
      m_current_depth++;

    return;
  }

  if (m_vram_dirty_rect.Intersects(Common::Rectangle<uint32_t>::FromExtents(src_x, src_y, width, height)))
    UpdateVRAMReadTexture();

  GPU_HW::CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);

  src_x *= m_resolution_scale;
  src_y *= m_resolution_scale;
  dst_x *= m_resolution_scale;
  dst_y *= m_resolution_scale;
  width *= m_resolution_scale;
  height *= m_resolution_scale;

  const D3D12_TEXTURE_COPY_LOCATION src = {m_vram_read_texture.GetResource(),
                                           D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  const D3D12_TEXTURE_COPY_LOCATION dst = {m_vram_texture.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  const D3D12_BOX src_box = {src_x, src_y, 0u, src_x + width, src_y + height, 1u};

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_DEST);
  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);

  g_d3d12_context->GetCommandList()->CopyTextureRegion(&dst, dst_x, dst_y, 0, &src, &src_box);

  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void GPU_HW_D3D12::UpdateVRAMReadTexture()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  const auto scaled_rect = m_vram_dirty_rect * m_resolution_scale;

  if (m_vram_texture.IsMultisampled())
  {
    m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_RESOLVE_DEST);
    cmdlist->ResolveSubresource(m_vram_read_texture, 0, m_vram_texture, 0, m_vram_texture.GetFormat());
  }
  else
  {
    const D3D12_TEXTURE_COPY_LOCATION src = {m_vram_texture.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    const D3D12_TEXTURE_COPY_LOCATION dst = {m_vram_read_texture.GetResource(),
                                             D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    const D3D12_BOX src_box = {scaled_rect.left, scaled_rect.top, 0u, scaled_rect.right, scaled_rect.bottom, 1u};
    m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_DEST);
    cmdlist->CopyTextureRegion(&dst, scaled_rect.left, scaled_rect.top, 0, &src, &src_box);
  }

  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  GPU_HW::UpdateVRAMReadTexture();
}

void GPU_HW_D3D12::UpdateDepthBufferFromMaskBit()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  cmdlist->OMSetRenderTargets(0, nullptr, FALSE, &m_vram_depth_texture.GetRTVOrDSVDescriptor().cpu_handle);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_texture.GetSRVDescriptor());
  ComPtr<ID3D12PipelineState> update_depth_pso = GetVRAMUpdateDepthPipeline();
  if (!update_depth_pso)
    return;
  cmdlist->SetPipelineState(update_depth_pso.Get());
  D3D12::SetViewportAndScissor(cmdlist, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
  cmdlist->DrawInstanced(3, 1, 0, 0);

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::ClearDepthBuffer()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->ClearDepthStencilView(m_vram_depth_texture.GetRTVOrDSVDescriptor(), D3D12_CLEAR_FLAG_DEPTH,
                                 m_pgxp_depth_buffer ? 1.0f : 0.0f, 0, 0, nullptr);
}

std::unique_ptr<GPU> GPU::CreateHardwareD3D12Renderer()
{
  return std::make_unique<GPU_HW_D3D12>();
}
