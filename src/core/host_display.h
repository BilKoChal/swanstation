#pragma once
#include "common/rectangle.h"
#include "common/window_info.h"
#include "types.h"
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

enum class HostDisplayPixelFormat : u32
{
  Unknown,
  RGBA8,
  BGRA8,
  RGB565,
  RGBA5551,
  Count
};

// An abstracted RGBA8 texture.
class HostDisplayTexture
{
public:
  virtual ~HostDisplayTexture();

  virtual void* GetHandle() const = 0;
  virtual u32 GetWidth() const = 0;
  virtual u32 GetHeight() const = 0;
  virtual u32 GetLayers() const = 0;
  virtual u32 GetLevels() const = 0;
  virtual u32 GetSamples() const = 0;
  virtual HostDisplayPixelFormat GetFormat() const = 0;
};

// Interface to the frontend's renderer.
class HostDisplay
{
public:
  enum class RenderAPI
  {
    None,
    D3D11,
    D3D12,
    Vulkan,
    OpenGL,
    OpenGLES
  };

  virtual ~HostDisplay();

  ALWAYS_INLINE s32 GetWindowWidth() const { return static_cast<s32>(m_window_info.surface_width); }
  ALWAYS_INLINE s32 GetWindowHeight() const { return static_cast<s32>(m_window_info.surface_height); }

  // Position is relative to the top-left corner of the window.
  ALWAYS_INLINE s32 GetMousePositionX() const { return m_mouse_position_x; }
  ALWAYS_INLINE s32 GetMousePositionY() const { return m_mouse_position_y; }
  ALWAYS_INLINE void SetMousePosition(s32 x, s32 y)
  {
    m_mouse_position_x = x;
    m_mouse_position_y = y;
  }

  // Lightgun position cached per frame at controller-update time, in
  // libretro's normalized [-0x7FFF, 0x7FFF] screen coordinates. The HW
  // renderers consume these directly so they don't have to re-poll
  // input from inside the render path - see retro_run_frame() in
  // libretro_host_interface.cpp where they are populated. Reading
  // input twice per frame is undefined per the libretro spec
  // (frontends may return stale or fresh values), so renderers must
  // never call g_retro_input_state_callback themselves.
  ALWAYS_INLINE s16 GetLightgunRawX() const { return m_lightgun_raw_x; }
  ALWAYS_INLINE s16 GetLightgunRawY() const { return m_lightgun_raw_y; }
  ALWAYS_INLINE bool IsLightgunOffscreen() const { return m_lightgun_offscreen; }
  ALWAYS_INLINE void SetLightgunState(s16 raw_x, s16 raw_y, bool offscreen)
  {
    m_lightgun_raw_x = raw_x;
    m_lightgun_raw_y = raw_y;
    m_lightgun_offscreen = offscreen;
  }

  virtual RenderAPI GetRenderAPI() const = 0;
  virtual void* GetRenderDevice() const = 0;
  virtual void* GetRenderContext() const = 0;

  virtual bool CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                                  bool threaded_presentation) = 0;
  virtual bool InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                      bool threaded_presentation) = 0;
  virtual void DestroyRenderDevice() = 0;
  virtual bool ChangeRenderWindow(const WindowInfo& wi) = 0;
  virtual bool CreateResources() = 0;
  virtual void DestroyResources() = 0;
  virtual void RenderSoftwareCursor() = 0;

  /// Call when the window size changes externally to recreate any resources.
  virtual void ResizeRenderWindow(s32 new_window_width, s32 new_window_height) = 0;

  /// Creates an abstracted RGBA8 texture. If dynamic, the texture can be updated with UpdateTexture() below.
  virtual std::unique_ptr<HostDisplayTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                            HostDisplayPixelFormat format, const void* data,
                                                            u32 data_stride, bool dynamic = false) = 0;

  /// Returns false if the window was completely occluded.
  virtual bool Render() = 0;

  const void* GetDisplayTextureHandle() const { return m_display_texture_handle; }
  s32 GetDisplayWidth() const { return m_display_width; }
  s32 GetDisplayHeight() const { return m_display_height; }
  float GetDisplayAspectRatio() const { return m_display_aspect_ratio; }

  void ClearDisplayTexture()
  {
    m_display_texture_handle = nullptr;
    m_display_texture_width = 0;
    m_display_texture_height = 0;
    m_display_texture_view_x = 0;
    m_display_texture_view_y = 0;
    m_display_texture_view_width = 0;
    m_display_texture_view_height = 0;
  }

  void SetDisplayTexture(void* texture_handle, HostDisplayPixelFormat texture_format, s32 texture_width,
                         s32 texture_height, s32 view_x, s32 view_y, s32 view_width, s32 view_height)
  {
    m_display_texture_handle = texture_handle;
    m_display_texture_format = texture_format;
    m_display_texture_width = texture_width;
    m_display_texture_height = texture_height;
    m_display_texture_view_x = view_x;
    m_display_texture_view_y = view_y;
    m_display_texture_view_width = view_width;
    m_display_texture_view_height = view_height;
  }

  void SetDisplayParameters(s32 display_width, s32 display_height, s32 active_left, s32 active_top, s32 active_width,
                            s32 active_height, float display_aspect_ratio)
  {
    m_display_width = display_width;
    m_display_height = display_height;
    m_display_active_left = active_left;
    m_display_active_top = active_top;
    m_display_active_width = active_width;
    m_display_active_height = active_height;
    m_display_aspect_ratio = display_aspect_ratio;
  }

  static u32 GetDisplayPixelFormatSize(HostDisplayPixelFormat format);

  virtual bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const = 0;

  virtual bool BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                                     u32* out_pitch) = 0;
  virtual void EndSetDisplayPixels() = 0;
  virtual bool SetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, const void* buffer, u32 pitch);

  /// Sets the software cursor to the specified texture. Ownership of the texture is transferred.
  void SetSoftwareCursor(std::unique_ptr<HostDisplayTexture> texture, float scale = 1.0f);

  /// Sets the software cursor to the specified image.
  bool SetSoftwareCursor(const void* pixels, u32 width, u32 height, u32 stride, float scale = 1.0f);

  /// Disables the software cursor.
  void ClearSoftwareCursor();

  /// Helper function for computing the draw rectangle in a larger window.
  std::tuple<s32, s32, s32, s32> CalculateDrawRect(s32 window_width, s32 window_height, s32 top_margin,
                                                   bool apply_aspect_ratio = true) const;

  /// Helper function for converting window coordinates to display coordinates.
  std::tuple<float, float> ConvertWindowCoordinatesToDisplayCoordinates(s32 window_x, s32 window_y, s32 window_width,
                                                                        s32 window_height, s32 top_margin) const;

protected:
  ALWAYS_INLINE bool HasSoftwareCursor() const { return static_cast<bool>(m_cursor_texture); }
  ALWAYS_INLINE bool HasDisplayTexture() const { return (m_display_texture_handle != nullptr); }

  void CalculateDrawRect(s32 window_width, s32 window_height, float* out_left, float* out_top, float* out_width,
                         float* out_height, float* out_left_padding, float* out_top_padding, float* out_scale,
                         float* out_x_scale, bool apply_aspect_ratio = true) const;

  std::tuple<s32, s32, s32, s32> CalculateSoftwareCursorDrawRect() const;
  std::tuple<s32, s32, s32, s32> CalculateSoftwareCursorDrawRect(s32 cursor_x, s32 cursor_y) const;

  WindowInfo m_window_info;

  s32 m_mouse_position_x = 0;
  s32 m_mouse_position_y = 0;

  // Cached per-frame libretro lightgun state - see SetLightgunState().
  s16  m_lightgun_raw_x = 0;
  s16  m_lightgun_raw_y = 0;
  bool m_lightgun_offscreen = true;

  s32 m_display_width = 0;
  s32 m_display_height = 0;
  s32 m_display_active_left = 0;
  s32 m_display_active_top = 0;
  s32 m_display_active_width = 0;
  s32 m_display_active_height = 0;
  float m_display_aspect_ratio = 1.0f;

  void* m_display_texture_handle = nullptr;
  HostDisplayPixelFormat m_display_texture_format = HostDisplayPixelFormat::Count;
  s32 m_display_texture_width = 0;
  s32 m_display_texture_height = 0;
  s32 m_display_texture_view_x = 0;
  s32 m_display_texture_view_y = 0;
  s32 m_display_texture_view_width = 0;
  s32 m_display_texture_view_height = 0;

  std::unique_ptr<HostDisplayTexture> m_cursor_texture;
  float m_cursor_texture_scale = 1.0f;
};
