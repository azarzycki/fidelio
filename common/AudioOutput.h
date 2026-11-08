#pragma once
// PCM output interface shared by the producers (fidelio-raop, cspot-ios3) and fidelio-audiod.
// Implementations: SocketSink (common/, streams to audiod; the producers' default), AudioQueueOutput
// (airplay/src, the hardware; audiod's default and the producers' --direct-audio), FileOutput and
// NullOutput below (host tests).
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
  // No more audio for a while (end of playlist, TEARDOWN, audiod's idle stop): a producer sink tells
  // audiod (STOP), the hardware sink stops the queue. The next write() resumes. Optional.
  virtual void idle() {}
  // Periodic housekeeping (audiod calls it every 20 ms): the hardware sink runs its recovery watchdog.
  virtual void tick() {}
  // True once after the hardware sink resumed from an interruption / yield: the caller should drop
  // the audio it buffered meanwhile (it is from before the interruption).
  virtual bool resumed() { return false; }
  // The output is actually consuming audio right now (not paused by an interruption / yield). audiod
  // applies backpressure to the active producer only while this is true.
  virtual bool consuming() { return true; }
};

#include <stdio.h>
#include <vector>
// Writes the interleaved 16-bit host-endian PCM to a file (device: --pcm-out for verification;
// host test build: the only real output). The volume is applied as gain, like the AudioQueue's
// volume parameter would be, so the file holds what the hardware would have played.
class FileOutput : public AudioOutput {
 public:
  explicit FileOutput(const char* path) : f(fopen(path, "wb")) {}
  ~FileOutput() override { if (f) fclose(f); }
  bool ok() const { return f != nullptr; }
  void write(const int16_t* frames, size_t n) override {
    if (!f) return;
    if (volume_ < 0.999f) {
      scaled_.resize(n * 2);
      for (size_t i = 0; i < n * 2; i++) scaled_[i] = (int16_t)(frames[i] * volume_);
      frames = scaled_.data();
    }
    fwrite(frames, 4, n, f); fflush(f); frames_ += n;
  }
  void flush() override { flushes_++; }
  void setVolume(float v) override { volume_ = v < 0 ? 0.f : v > 1 ? 1.f : v; }
  double bufferedSeconds() override { return 0; }
 private:
  FILE* f;
  size_t frames_ = 0;
  int flushes_ = 0;
  float volume_ = 1.0f;
  std::vector<int16_t> scaled_;
};

class NullOutput : public AudioOutput {
 public:
  void write(const int16_t*, size_t) override {}
  void flush() override {}
  void setVolume(float) override {}
  double bufferedSeconds() override { return 0; }
};
