#pragma once
// Thin wrapper over Apple's open-source ALACDecoder, configured from the SDP fmtp line.
#include <stdint.h>

#include <memory>
#include <vector>

class ALACDecoder;

class AlacWrapper {
 public:
  AlacWrapper();
  ~AlacWrapper();
  // fmtp: "352 0 16 40 10 14 2 255 0 0 44100" = frameLength compatibleVersion bitDepth pb mb kb
  // numChannels maxRun maxFrameBytes avgBitRate sampleRate
  bool init(const std::string& fmtp);
  uint32_t frameLength() const { return frames; }
  uint32_t channels() const { return chans; }
  // Decodes one packet into interleaved 16-bit host-endian samples; returns frames decoded (0 on error).
  uint32_t decode(const uint8_t* data, size_t len, int16_t* out, uint32_t maxFrames);

 private:
  std::unique_ptr<ALACDecoder> dec;
  uint32_t frames = 352, chans = 2, bits = 16;
};
