#include "libretro_audio_stream.h"
#include "core/host_interface.h"
#include <algorithm>

#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

LibretroAudioStream::LibretroAudioStream() = default;

LibretroAudioStream::~LibretroAudioStream() = default;

bool LibretroAudioStream::Reconfigure(u32 input_sample_rate,
                                      u32 output_sample_rate,
                                      u32 channels,
                                      u32 buffer_size)
{
  m_buffer_size = buffer_size;
  return SetBufferSize(buffer_size);
}

void LibretroAudioStream::Shutdown()
{
  m_buffer.Clear();
  m_buffer_size = 0;
}

void LibretroAudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
  // The libretro core is single-threaded; producer (SPU) and consumer
  // (UploadToFrontend) run on the same thread. No synchronisation
  // primitive is required around the FIFO; see the comment in
  // libretro_audio_stream.h for the previous mutex's rationale.
  //
  // BeginWrite is non-blocking: the SPU is told what contiguous space
  // is currently available, capped at m_buffer_size, and EndWrite
  // records the count the caller actually filled. The SPU's loop in
  // spu.cpp Execute() handles short returns by re-issuing
  // BeginWrite/EndWrite until all generated frames are consumed.
  *buffer_ptr = m_buffer.GetWritePointer();
  *num_frames = std::min(m_buffer_size, m_buffer.GetContiguousSpace() / AUDIO_CHANNELS);
}

void LibretroAudioStream::EndWrite(u32 num_frames)
{
  m_buffer.AdvanceTail(num_frames * AUDIO_CHANNELS);

  if (m_silent_mode)
  {
    // Internal-runahead replay: drop everything immediately so the
    // FIFO doesn't fill up across the replay window. We're still
    // ticking the SPU for state correctness, just discarding the
    // resulting audio.
    m_buffer.Remove(m_buffer.GetSize());
    return;
  }

  // No-op in fast-hook mode (the default). The frontend is fed once
  // per retro_run_frame() via UploadToFrontend(), which keeps audio
  // batched and predictable. Calling the frontend per SPU update (the
  // slow-hook path below) is preserved for parity but should be
  // considered a legacy / debugging mode: it produces dozens to
  // hundreds of tiny batch calls per frame, increases callback
  // overhead, and the resulting audio chunking is sensitive to host
  // scheduling jitter on the libretro side.
  if (g_settings.audio_fast_hook)
    return;

  // Slow-hook drain. Same skip-but-still-drain rule as
  // UploadToFrontend; see comment there.
  while (true)
  {
    const u32 num_samples = m_buffer.GetContiguousSize();
    if (num_samples == 0)
      break;

    if (!g_retro_skip_audio_this_frame)
      g_retro_audio_sample_batch_callback(m_buffer.GetReadPointer(),
                                          num_samples / AUDIO_CHANNELS);
    m_buffer.Remove(num_samples);
  }
}

void LibretroAudioStream::UploadToFrontend()
{
  // Drain the FIFO straight into the libretro batch callback. The FIFO
  // is a circular buffer, so up to two contiguous regions may be
  // present when the producer wrapped around since the last drain.
  // Most frames are one region (one callback); wraparound costs one
  // extra callback.
  //
  // Previously this routine bounce-copied every sample through a
  // 32 KiB (65 KiB after the s16 promotion) std::array on the stack
  // and made exactly one callback. The intermediate copy is
  // unnecessary - the libretro frontend reads samples synchronously
  // from the buffer we pass it, and the FIFO storage stays valid until
  // Remove() is called below.
  //
  // When the frontend has cleared RETRO_AV_ENABLE_AUDIO for this frame
  // (single-instance runahead skip frame, fast-forward, etc.), we
  // still need to drain the FIFO so the SPU side doesn't keep
  // accumulating backpressure across the runahead window - we just
  // don't forward anything to the frontend.
  while (true)
  {
    const u32 num_samples = m_buffer.GetContiguousSize();
    if (num_samples == 0)
      break;

    if (!g_retro_skip_audio_this_frame)
      g_retro_audio_sample_batch_callback(m_buffer.GetReadPointer(),
                                          num_samples / AUDIO_CHANNELS);
    m_buffer.Remove(num_samples);
  }
}

bool LibretroAudioStream::SetBufferSize(u32 buffer_size)
{
  const u32 buffer_size_in_samples = buffer_size * AUDIO_CHANNELS;
  // The FIFO has fixed capacity; reject configurations that would not
  // fit two batches' worth of samples (the historical 2x headroom that
  // the now-removed back-pressure code relied on). The check is kept
  // because games exposed via core options can request larger
  // audio_buffer_size values.
  if ((buffer_size_in_samples * 2u) > m_buffer.GetCapacity())
    return false;

  m_buffer_size = buffer_size;
  return true;
}
