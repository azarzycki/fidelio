#include "AlacWrapper.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include "ALACBitUtilities.h"
#include "ALACDecoder.h"
#include "ios3_trace.h"

AlacWrapper::AlacWrapper() {}
AlacWrapper::~AlacWrapper() {}

static void put32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v; }

bool AlacWrapper::init(const std::string& fmtp) {
  unsigned v[11];
  int n = sscanf(fmtp.c_str(), "%u %u %u %u %u %u %u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                 &v[8], &v[9], &v[10]);
  if (n != 11) { IOS3_TRACE("ALAC fmtp: expected 11 fields, got %d (%s)", n, fmtp.c_str()); return false; }
  frames = v[0]; bits = v[2]; chans = v[6];
  if (bits != 16 || chans != 2) IOS3_TRACE("ALAC: %u-bit %u-ch stream; the sink is 16-bit stereo", bits, chans);
  // Build the 24-byte big-endian ALACSpecificConfig "magic cookie".
  uint8_t cookie[24];
  put32(cookie + 0, v[0]);
  cookie[4] = (uint8_t)v[1]; cookie[5] = (uint8_t)v[2]; cookie[6] = (uint8_t)v[3];
  cookie[7] = (uint8_t)v[4]; cookie[8] = (uint8_t)v[5]; cookie[9] = (uint8_t)v[6];
  put16(cookie + 10, (uint16_t)v[7]);
  put32(cookie + 12, v[8]);
  put32(cookie + 16, v[9]);
  put32(cookie + 20, v[10]);
  dec.reset(new ALACDecoder());
  int32_t st = dec->Init(cookie, sizeof cookie);
  if (st != 0) { IOS3_TRACE("ALACDecoder::Init -> %d", (int)st); dec.reset(); return false; }
  IOS3_TRACE("ALAC decoder ready: %u frames/packet, %u-bit, %u ch, %u Hz", frames, bits, chans, v[10]);
  return true;
}

uint32_t AlacWrapper::decode(const uint8_t* data, size_t len, int16_t* out, uint32_t maxFrames) {
  if (!dec) return 0;
  BitBuffer bits;
  BitBufferInit(&bits, const_cast<uint8_t*>(data), (uint32_t)len);
  // numSamples must be the configured frame length: the decoder sizes its mix/predictor
  // buffers from the cookie and trusts this value (a larger one overruns them).
  if (maxFrames < frames) return 0;
  uint32_t got = 0;
  int32_t st = dec->Decode(&bits, reinterpret_cast<uint8_t*>(out), frames, chans, &got);
  if (st != 0) return 0;
  return got;
}
