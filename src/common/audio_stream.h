#pragma once
#include "fifo_queue.h"
#include "types.h"
#include <atomic>
#include <memory>
#include <vector>

// Uses signed 16-bits samples.

class AudioStream
{
public:
  using SampleType = s16;

  static constexpr u32 DefaultInputSampleRate = 44100, DefaultOutputSampleRate = 44100, DefaultBufferSize = 2048,
                       MaxSamples = 32768, FullVolume = 100;

  AudioStream();
  virtual ~AudioStream();

  u32 GetBufferSize() const { return m_buffer_size; }

  bool Reconfigure(u32 input_sample_rate, u32 output_sample_rate,
                   u32 channels, u32 buffer_size);

  void Shutdown();

  void BeginWrite(SampleType** buffer_ptr, u32* num_frames);
  void EndWrite(u32 num_frames);

  static std::unique_ptr<AudioStream> CreateNullAudioStream();

protected:
  virtual void FramesAvailable() = 0;

  ALWAYS_INLINE static SampleType ApplyVolume(SampleType sample, u32 volume)
  {
    return s16((s32(sample) * s32(volume)) / 100);
  }

  bool SetBufferSize(u32 buffer_size);

  u32 m_buffer_size = 0;

  // The libretro core is single-threaded by design: the SPU runs on
  // the same thread (driven by TimingEvents) that eventually calls
  // UploadToFrontend() at end-of-retro_run. Both subclasses
  // (LibretroAudioStream, NullAudioStream) are libretro-only. No
  // synchronization primitive is needed around this FIFO; the
  // previously-present mutex was always uncontended and only added
  // per-write atomic-CAS overhead (~50ns x ~300 SPU updates per
  // frame).
  HeapFIFOQueue<SampleType, MaxSamples> m_buffer;

  u32 m_max_samples = 0;
};
