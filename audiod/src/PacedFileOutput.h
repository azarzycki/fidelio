#pragma once
// Host-test model of the AudioQueue: holds the audio it is given in a PcmRing (0.74 s, like the
// sink's), "consumes" it at 44100 * (1 + skew) frames per wall second in 46 ms bursts like the
// queue's buffers, writing what it consumed to the file (silence when starved, like the queue's
// padding), and reports the unconsumed remainder in bufferedSeconds(). crossfade() is the same
// PcmRing operation the real sink performs, so a host test sees the real transition.
#include <stdint.h>
#include <time.h>

#include <vector>

#include "AudioOutput.h"
#include "PcmRing.h"
#include "ios3_trace.h"

class PacedFileOutput : public FileOutput {
 public:
  PacedFileOutput(const char* path, double skewPpm) : FileOutput(path), rate(44100.0 * (1.0 + skewPpm / 1e6)) {}
  ~PacedFileOutput() override { advance(); IOS3_TRACE("paced output: %lu starved bursts (46 ms each) of %lu consumed", starved, bursts); }
  void write(const int16_t* frames, size_t n) override {
    advance();
    ring.feed(reinterpret_cast<const uint8_t*>(frames), n * 4);  // the pump never exceeds the cap; anything beyond is dropped
    lastWrite = last;
  }
  void flush() override { advance(); ring.clear(); lastWrite = 0; }
  void crossfade(size_t fadeFrames) override { advance(); ring.crossfade(fadeFrames); }
  double bufferedSeconds() override { advance(); return ring.fill() / 4 / 44100.0; }

 private:
  static double nowS() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
  // Consume whole 46 ms bursts that are due since the last call; write them to the file.
  void advance() {
    double now = nowS();
    if (last == 0) { last = now; return; }
    due += (now - last) * rate;
    last = now;
    const size_t burst = 2029;
    std::vector<uint8_t> buf(burst * 4);
    while (due >= burst) {
      due -= burst;
      size_t got = ring.take(buf.data(), burst * 4);
      if (got < burst * 4) {
        memset(buf.data() + got, 0, burst * 4 - got);  // the queue pads silence
        if (now - lastWrite < 1.0 && lastWrite != 0) starved++;
      }
      FileOutput::write(reinterpret_cast<const int16_t*>(buf.data()), burst);
      bursts++;
    }
  }
  double rate;
  double due = 0, last = 0, lastWrite = 0;
  PcmRing ring{128 * 1024};
  unsigned long starved = 0, bursts = 0;
};
