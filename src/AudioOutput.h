#pragma once
// PCM output interface of the RAOP receiver. PHASE 1 implements it with the AudioQueue sink
// (AudioQueueOutput) and a discarding NullOutput (--no-audio). PHASE 2 swaps in a socket to
// the fidelio-audiod arbiter without touching the RAOP code.
#include <stddef.h>
#include <stdint.h>

class AudioOutput {
 public:
  virtual ~AudioOutput() {}
  // Interleaved stereo 16-bit host-endian frames at 44.1 kHz. May block for pacing.
  virtual void write(const int16_t* frames, size_t numFrames) = 0;
  // Drop everything queued (FLUSH / TEARDOWN).
  virtual void flush() = 0;
  // Linear 0..1 gain.
  virtual void setVolume(float linear) = 0;
  // Seconds of audio still queued downstream (for the status position and end detection).
  virtual double bufferedSeconds() = 0;
};

#include <stdio.h>
// Writes the raw interleaved 16-bit host-endian PCM to a file (device: --pcm-out for verification;
// host test build: the only real output).
class FileOutput : public AudioOutput {
 public:
  explicit FileOutput(const char* path) : f(fopen(path, "wb")) {}
  ~FileOutput() override { if (f) fclose(f); }
  bool ok() const { return f != nullptr; }
  void write(const int16_t* frames, size_t n) override { if (f) { fwrite(frames, 4, n, f); fflush(f); frames_ += n; } }
  void flush() override { flushes_++; }
  void setVolume(float v) override { volume_ = v; }
  double bufferedSeconds() override { return 0; }
 private:
  FILE* f;
  size_t frames_ = 0;
  int flushes_ = 0;
  float volume_ = -1;
};

class NullOutput : public AudioOutput {
 public:
  void write(const int16_t*, size_t) override {}
  void flush() override {}
  void setVolume(float) override {}
  double bufferedSeconds() override { return 0; }
};
