#ifndef AUDIOSINK_H
#define AUDIOSINK_H
// Same interface as bell's AudioSink.h (cspot), so AudioQueueSink.{h,cpp} is shared verbatim.
#include <cstdint>
#include <cstdlib>

class AudioSink {
 public:
  AudioSink() {}
  virtual ~AudioSink() {}
  virtual void feedPCMFrames(const uint8_t* buffer, size_t bytes) = 0;
  virtual void volumeChanged(uint16_t volume) {}
  virtual bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) { return false; }
  bool softwareVolumeControl = true;
  bool usign = false;
};

#endif
