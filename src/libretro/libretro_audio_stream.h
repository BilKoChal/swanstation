#pragma once
#include "common/fifo_queue.h"
#include "core/types.h"
#include <cstdint>
#include <vector>

// 16-bit signed audio. The libretro core is single-threaded by design
// (the SPU runs on the same thread driven by TimingEvents that
// eventually calls UploadToFrontend() at end-of-retro_run), so no
// synchronisation primitive is needed around the FIFO. The previously
// present mutex was always uncontended and only added per-write
// atomic-CAS overhead (~50ns x ~300 SPU updates per frame).
//
// This class used to derive from a generic AudioStream base. There has
// only ever been one runtime audio stream in swanstation - this one -
// and the second subclass (NullAudioStream) was only used to discard
// audio during internal-runahead replay. The "discard" behaviour is
// folded in here behind SetSilentMode() instead, dropping the vtable
// dispatch from FramesAvailable / EndWrite hot paths.
class LibretroAudioStream
{
public:
  using SampleType = s16;

  static constexpr u32 DefaultInputSampleRate = 44100, DefaultOutputSampleRate = 44100, DefaultBufferSize = 2048,
                       MaxSamples = 32768, FullVolume = 100;

  LibretroAudioStream();
  ~LibretroAudioStream();

  u32 GetBufferSize() const { return m_buffer_size; }

  // input_sample_rate / output_sample_rate / channels are accepted for
  // signature compatibility with the historical AudioStream base; the
  // libretro frontend does its own resampling, so only buffer_size
  // affects this object.
  bool Reconfigure(u32 input_sample_rate, u32 output_sample_rate,
                   u32 channels, u32 buffer_size);

  void Shutdown();

  void BeginWrite(SampleType** buffer_ptr, u32* num_frames);
  void EndWrite(u32 num_frames);

  void UploadToFrontend();

  // Switch the FIFO into a "drop everything" mode used by the internal
  // runahead replay path: the SPU keeps producing samples for state
  // correctness, but nothing reaches the libretro frontend and the
  // FIFO is drained inside EndWrite so backpressure does not build up
  // across the runahead window.
  void SetSilentMode(bool silent) { m_silent_mode = silent; }
  bool IsSilentMode() const { return m_silent_mode; }

private:
  bool SetBufferSize(u32 buffer_size);

  u32 m_buffer_size = 0;
  bool m_silent_mode = false;

  HeapFIFOQueue<SampleType, MaxSamples> m_buffer;
};
