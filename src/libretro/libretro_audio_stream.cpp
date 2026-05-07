#include "libretro_audio_stream.h"
#include "libretro_host_interface.h"
#include <algorithm>

#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

LibretroAudioStream::LibretroAudioStream() = default;

LibretroAudioStream::~LibretroAudioStream() = default;

void LibretroAudioStream::UploadToFrontend()
{
  // Drain the FIFO straight into the libretro batch callback. The FIFO is
  // a circular buffer, so up to two contiguous regions may be present
  // when the producer wrapped around since the last drain. Most frames
  // are one region (one callback); wraparound costs one extra callback.
  //
  // Previously this routine bounce-copied every sample through a 32 KiB
  // (65 KiB after the s16 promotion) std::array on the stack and made
  // exactly one callback. The intermediate copy is unnecessary - the
  // libretro frontend reads samples synchronously from the buffer we
  // pass it, and the FIFO storage stays valid until Remove() is called
  // below.
  while (true)
  {
    const u32 num_samples = m_buffer.GetContiguousSize();
    if (num_samples == 0)
      break;

    g_retro_audio_sample_batch_callback(m_buffer.GetReadPointer(),
                                        num_samples / AUDIO_CHANNELS);
    m_buffer.Remove(num_samples);
  }
}

void LibretroAudioStream::FramesAvailable()
{
  // No-op in fast-hook mode (the default). The frontend is fed once per
  // retro_run_frame() via UploadToFrontend(), which keeps audio batched
  // and predictable. Calling the frontend per SPU update (the slow-hook
  // path) is preserved here for parity but should be considered a
  // legacy / debugging mode: it produces dozens to hundreds of tiny
  // batch calls per frame, increases callback overhead, and the
  // resulting audio chunking is sensitive to host scheduling jitter
  // on the libretro side.
  if (g_settings.audio_fast_hook)
    return;

  while (true)
  {
    const u32 num_samples = m_buffer.GetContiguousSize();
    if (num_samples == 0)
      break;

    g_retro_audio_sample_batch_callback(m_buffer.GetReadPointer(),
                                        num_samples / AUDIO_CHANNELS);
    m_buffer.Remove(num_samples);
  }
}
