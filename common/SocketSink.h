#pragma once
// SocketSink: an AudioOutput (common/AudioOutput.h) that streams PCM to fidelio-audiod over the
// Unix socket instead of opening the hardware. write() -> AUDIO, flush() -> FLUSH, idle() -> STOP.
// Connects lazily with backoff, sends HELLO, and reconnects + re-HELLOs after a broken pipe
// (audiod restarted). While audiod is unreachable, audio is dropped (never blocks the producer).
//
// Pacing: the producers (cspot's player thread, raop's playerLoop) push as fast as their sink
// accepts and rely on it blocking; the AudioQueue ring did that. write() therefore sleeps so the
// stream stays at most kLeadMs ahead of the monotonic clock, counted from the first write after a
// flush or a gap. bufferedSeconds() reports that lead, which keeps cspot's end-of-track detection
// working. Volume: there is no VOLUME message, so setVolume() is applied as gain before sending.
#include <stdint.h>

#include <string>
#include <vector>

#include "AudioOutput.h"

class SocketSink : public AudioOutput {
 public:
  // name: "spotify" / "airplay" (AUDIOD_NAME_MAX-1 chars max); sockPath: audiod socket.
  SocketSink(const std::string& name, const std::string& sockPath, uint8_t priority = 0);
  ~SocketSink() override;

  void write(const int16_t* frames, size_t numFrames) override;
  void flush() override;
  void setVolume(float linear) override;  // software gain (applied to the PCM before sending)
  double bufferedSeconds() override;      // how far ahead of real time the stream is (0..kLeadMs)
  void idle() override;                   // waits out the lead, then sends STOP; the connection stays open
  void stop() { idle(); }

  static const unsigned kLeadMs = 500;    // how far a producer may run ahead of real time
  static const unsigned kGapResetMs = 500;  // a pause longer than this starts a new timeline

  bool connected() const { return fd >= 0; }
  unsigned long reconnects() const { return reconnectCount; }
  unsigned long dropped() const { return droppedFrames; }

 private:
  bool ensureConnected();  // rate-limited connect attempt + HELLO
  void disconnect(const char* why);
  static uint64_t nowMs();

  std::string name, sockPath;
  uint8_t priority;
  int fd = -1;
  uint64_t nextAttemptMs = 0;
  uint64_t backoffMs = 200;
  unsigned long reconnectCount = 0;
  unsigned long droppedFrames = 0;
  float volume = 1.0f;
  std::vector<int16_t> scaled;  // scratch for the gain
  uint64_t timelineStartMs = 0;  // 0 = no timeline yet
  uint64_t timelineFrames = 0;   // frames sent on the current timeline
  uint64_t lastWriteMs = 0;
  void pace(size_t framesAboutToSend);  // sleeps until sending them keeps the lead <= kLeadMs
  long leadMs(uint64_t now) const;
};
