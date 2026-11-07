// Host-only test helper: reads 352-frame blocks of 16-bit LE stereo PCM on stdin, writes
// <uint32 LE length><ALAC packet> on stdout, using Apple's reference ALACEncoder with the same
// parameters CenturyPlay / iTunes announce (frameLength 352, 16-bit, 2 ch, 44100 Hz).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ALACAudioTypes.h"
#include "ALACEncoder.h"

int main() {
  const uint32_t frames = 352;
  AudioFormatDescription in{}, out{};
  in.mSampleRate = 44100; in.mFormatID = kALACFormatLinearPCM;
  in.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;  // little-endian host
  in.mBytesPerPacket = 4; in.mFramesPerPacket = 1; in.mBytesPerFrame = 4; in.mChannelsPerFrame = 2; in.mBitsPerChannel = 16;
  out.mSampleRate = 44100; out.mFormatID = kALACFormatAppleLossless; out.mFormatFlags = 1;  // 1 = 16-bit source
  out.mFramesPerPacket = frames; out.mChannelsPerFrame = 2;
  ALACEncoder enc;
  enc.SetFrameSize(frames);
  if (enc.InitializeEncoder(out) != 0) { fprintf(stderr, "InitializeEncoder failed\n"); return 1; }
  unsigned char pcm[frames * 4];
  unsigned char pkt[frames * 4 + 64];
  while (fread(pcm, 1, sizeof pcm, stdin) == sizeof pcm) {
    int32_t n = sizeof pcm;
    if (enc.Encode(in, out, pcm, pkt, &n) != 0) { fprintf(stderr, "Encode failed\n"); return 1; }
    uint32_t len = (uint32_t)n;
    fwrite(&len, 4, 1, stdout);
    fwrite(pkt, 1, len, stdout);
    fflush(stdout);
  }
  return 0;
}
