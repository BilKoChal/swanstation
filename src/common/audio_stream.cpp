#include "audio_stream.h"
#include <algorithm>
#include <cstring>

#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

AudioStream::AudioStream() = default;

AudioStream::~AudioStream()
{
}

bool AudioStream::Reconfigure(u32 input_sample_rate ,
                              u32 output_sample_rate ,
			      u32 channels,
                              u32 buffer_size)
{
  std::unique_lock<std::mutex> buffer_lock(m_buffer_mutex);

  m_buffer_size = buffer_size;

  return SetBufferSize(buffer_size);
}

void AudioStream::Shutdown()
{
  std::unique_lock<std::mutex> lock(m_buffer_mutex);
  m_buffer.Clear();
  m_buffer_size = 0;
}

void AudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
  // The SPU calls BeginWrite/EndWrite from the same thread that drains
  // the buffer (in libretro mode, that is retro_run_frame()). The mutex
  // is therefore always uncontended in single-threaded use; it remains
  // here so that backends with their own audio output threads stay
  // safe.
  //
  // Previously this routine waited on a condition variable whenever
  // `buffer_space < requested_size`. Nothing in the codebase ever
  // notified that CV (it was wait-only), so the SPU could deadlock if
  // the buffer ever filled up. A non-blocking design is safer: in
  // single-threaded operation the buffer never fills (m_max_samples is
  // 2 * buffer_size, currently 8x smaller than the FIFO capacity, and
  // UploadToFrontend drains it every retro_run); on the rare overrun
  // path we simply give the SPU what space is contiguously available
  // and let it write a smaller batch. EndWrite passes the actual
  // count.
  m_buffer_mutex.lock();

  *buffer_ptr = m_buffer.GetWritePointer();
  *num_frames = std::min(m_buffer_size, m_buffer.GetContiguousSpace() / AUDIO_CHANNELS);
}

void AudioStream::EndWrite(u32 num_frames)
{
  m_buffer.AdvanceTail(num_frames * AUDIO_CHANNELS);
  m_buffer_mutex.unlock();
  FramesAvailable();
}

bool AudioStream::SetBufferSize(u32 buffer_size)
{
  const u32 buffer_size_in_samples = buffer_size * AUDIO_CHANNELS;
  const u32 max_samples = buffer_size_in_samples * 2u;
  if (max_samples > m_buffer.GetCapacity())
    return false;

  m_buffer_size = buffer_size;
  m_max_samples = max_samples;
  return true;
}
