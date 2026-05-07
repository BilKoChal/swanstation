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
  m_buffer_size = buffer_size;

  return SetBufferSize(buffer_size);
}

void AudioStream::Shutdown()
{
  m_buffer.Clear();
  m_buffer_size = 0;
}

void AudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
  // The libretro core is single-threaded; producer (SPU) and consumer
  // (UploadToFrontend) run on the same thread. No synchronization
  // primitive is required around the FIFO; see the comment in
  // audio_stream.h for the previous mutex's rationale.
  //
  // BeginWrite is non-blocking: the SPU is told what contiguous space
  // is currently available, capped at m_buffer_size, and EndWrite
  // records the count the caller actually filled. The SPU's loop in
  // spu.cpp Execute() handles short returns by re-issuing
  // BeginWrite/EndWrite until all generated frames are consumed.
  *buffer_ptr = m_buffer.GetWritePointer();
  *num_frames = std::min(m_buffer_size, m_buffer.GetContiguousSpace() / AUDIO_CHANNELS);
}

void AudioStream::EndWrite(u32 num_frames)
{
  m_buffer.AdvanceTail(num_frames * AUDIO_CHANNELS);
  FramesAvailable();
}

bool AudioStream::SetBufferSize(u32 buffer_size)
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
