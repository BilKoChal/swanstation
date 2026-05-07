#include "null_audio_stream.h"

#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

NullAudioStream::NullAudioStream() = default;

NullAudioStream::~NullAudioStream() = default;

void NullAudioStream::FramesAvailable()
{
  // Drop everything that's available as soon as it arrives.
  // The base AudioStream is single-threaded; no lock needed.
  m_buffer.Remove(m_buffer.GetSize());
}

std::unique_ptr<AudioStream> AudioStream::CreateNullAudioStream()
{
  return std::make_unique<NullAudioStream>();
}
