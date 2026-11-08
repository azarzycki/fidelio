#pragma once
// Host-test model of the AudioQueue: a FileOutput that "consumes" what it was given at
// 44100 * (1 + skew) frames per wall second, in 46 ms bursts like the queue's buffers, and reports
// the unconsumed remainder in bufferedSeconds(). Lets the arbiter's pump and backpressure be driven
// on the host exactly as the hardware would drive them, with a chosen clock drift.
#include <stdint.h>
#include <time.h>

#include "AudioOutput.h"
#include "ios3_trace.h"

class PacedFileOutput : public FileOutput {
 public:
  PacedFileOutput(const char* path, double skewPpm) : FileOutput(path), rate(44100.0 * (1.0 + skewPpm / 1e6)) {}
  ~PacedFileOutput() override { IOS3_TRACE("paced output: %lu starved bursts (46 ms each) of %lu consumed", starved, bursts); }
  void write(const int16_t* frames, size_t n) override {
    advance();
    FileOutput::write(frames, n);
    written += (double)n;
    lastWrite = last;
  }
  void flush() override { FileOutput::flush(); advance(); consumed = written; lastWrite = 0; }  // the stream ended / was cut: what follows is not starvation
  double bufferedSeconds() override {
    advance();
    double pending = written - consumedQuantized();
    return pending > 0 ? pending / 44100.0 : 0.0;
  }
  double skewRate() const { return rate; }

 private:
  static double nowS() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
  void advance() {
    double now = nowS();
    if (last == 0) { last = now; return; }
    consumed += (now - last) * rate;
    last = now;
    if (consumed > written) {
      // The real queue would pad silence here. Count it only while the stream is alive (a write in
      // the last second): the tail after a source stops is not starvation.
      if (now - lastWrite < 1.0) deficit += consumed - written;
      consumed = written;
    }
    bursts = (unsigned long)(consumed / 2029.0);
    starved = (unsigned long)(deficit / 2029.0);
  }
  double consumedQuantized() const {  // the queue hands back whole 46 ms buffers
    const double q = 2029.0;
    double full = (double)(long long)(consumed / q) * q;
    return full > written ? written : full;
  }
  double rate;
  double written = 0, consumed = 0, last = 0, lastWrite = 0, deficit = 0;
  unsigned long starved = 0, bursts = 0;
};
