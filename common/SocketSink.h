#pragma once
// SocketSink: an AudioOutput (airplay/src/AudioOutput.h) that streams PCM to fidelio-audiod over
// the Unix socket instead of opening the hardware. write() -> AUDIO, flush() -> FLUSH, stop() ->
// STOP. Connects lazily with backoff, sends HELLO, and reconnects + re-HELLOs after a broken pipe
// (audiod restarted). While audiod is unreachable, audio is dropped (never blocks the producer).
#include <stdint.h>

#include <string>

#include "AudioOutput.h"

class SocketSink : public AudioOutput {
 public:
  // name: "spotify" / "airplay" (AUDIOD_NAME_MAX-1 chars max); sockPath: audiod socket.
  SocketSink(const std::string& name, const std::string& sockPath, uint8_t priority = 0);
  ~SocketSink() override;

  void write(const int16_t* frames, size_t numFrames) override;
  void flush() override;
  void setVolume(float linear) override;  // v1: producers keep their own volume; recorded only
  double bufferedSeconds() override { return 0; }
  void stop();  // STOP message (source intentionally stopping); the connection stays open

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
};
