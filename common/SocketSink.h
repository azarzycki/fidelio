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
// working. Volume travels as a VOLUME message (audiod applies it at the output), so audiod's
// silence detection sees the producer's full-scale samples whatever the listening volume.
//
// Thread-safe (B1.2): the producers call write() from their player thread and flush() /
// setVolume() / idle() from the RTSP or Spirc thread; one mutex serializes every method. Sends
// carry a kSendTimeoutMs timeout (and a kSendBufBytes buffer) so a producer never hangs when
// audiod's loop is stuck in a slow CoreAudio call: the write fails, the sink reconnects.
#include <stdint.h>

#include <mutex>
#include <string>
#include <vector>

#include "AudioOutput.h"
#include "AudiodFraming.h"

class SocketSink : public AudioOutput {
 public:
  // name: "spotify" / "airplay" (AUDIOD_NAME_MAX-1 chars max); sockPath: audiod socket.
  SocketSink(const std::string& name, const std::string& sockPath, uint8_t priority = 0);
  ~SocketSink() override;

  void write(const int16_t* frames, size_t numFrames) override;
  void flush() override;
  void setVolume(float linear) override;  // sent as VOLUME (and again after every reconnect)
  // B1.9: the raw Connect slider travels with the gain so audiod can drive the hardware volume
  // ("absolute" mode); apply = a user move (set the hardware), !apply = report-only. Only sent in the
  // 8-byte form once WELCOME proved a v2 server; a v1 server just gets the gain.
  void setVolumeSlider(float linear, uint16_t slider, bool apply = true);
  // B1.9: speak protocol v2 (read WELCOME / HWVOLUME); connect at once instead of on the first write.
  // cspot does this; raop stays v1 (it never reads the socket).
  void enableServerMessages();
  // Non-blocking: reconnect if needed (rate-limited) and read whatever the server sent. Call it
  // periodically (cspot: from the player loop); write() calls it too.
  void poll();
  // What the server told us (valid once hardwareVolumeKnown()): absolute mode and the hardware level.
  bool hardwareVolumeKnown() const { std::lock_guard<std::mutex> l(m); return welcomed; }
  bool hardwareVolumeAbsolute() const { std::lock_guard<std::mutex> l(m); return welcomed && absolute; }
  uint16_t hardwareSlider() const { std::lock_guard<std::mutex> l(m); return hwSlider; }
  // Called (from poll()/write(), without the sink's mutex) when WELCOME arrives and whenever the
  // hardware level changes: (absolute, slider).
  void setHardwareVolumeCallback(void (*cb)(void* user, bool absolute, uint16_t slider), void* user) {
    std::lock_guard<std::mutex> l(m); hwCb = cb; hwUser = user;
  }
  double bufferedSeconds() override;      // how far ahead of real time the stream is (0..kLeadMs)
  void idle() override;                   // waits out the lead, then sends STOP; the connection stays open
  void stop() { idle(); }

  // How far a producer may run ahead of the wall clock (env AUDIOD_LEAD_MS). Since B1.5 the active
  // source is paced by audiod reading it only as fast as the output consumes (i.e. by the hardware,
  // like the local AudioQueue ring used to), so this cap matters for a source audiod is NOT playing:
  // it bounds how much of its own buffer (raop's 1.2 s jitter buffer) it drains ahead while waiting
  // to become active. It must stay below that buffer, and above audiod's downstream capacity
  // (output 0.45 s + staging 40 ms + two small kernel buffers) so backpressure, not this clock, binds.
  static const unsigned kDefaultLeadMs = 750;
  static const unsigned kGapResetMs = 500;     // a pause longer than this starts a new timeline
  void setLeadMs(unsigned ms) { std::lock_guard<std::mutex> l(m); leadMsCap = ms; }

  bool connected() const { std::lock_guard<std::mutex> l(m); return fd >= 0; }
  unsigned long reconnects() const { std::lock_guard<std::mutex> l(m); return reconnectCount; }
  unsigned long dropped() const { std::lock_guard<std::mutex> l(m); return droppedFrames; }
  static const int kSendTimeoutMs = 500;        // a send blocked this long = audiod stuck: drop and reconnect
  static const int kSendBufBytes = 8 * 1024;    // 46 ms: small on purpose, this is where the active producer blocks (B1.5)

 private:
  // the following assume `m` is held
  bool ensureConnected();  // rate-limited connect attempt + HELLO
  void disconnect(const char* why);
  static uint64_t nowMs();

  mutable std::mutex m;
  std::string name, sockPath;
  uint8_t priority;
  int fd = -1;
  uint64_t nextAttemptMs = 0;
  uint64_t backoffMs = 200;
  unsigned long reconnectCount = 0;
  unsigned long droppedFrames = 0;
  float volume = 1.0f;
  uint16_t slider = 0;        // raw Connect position, valid with haveSlider
  bool haveSlider = false;
  bool sliderApply = true;
  bool v2 = false;            // enableServerMessages(): HELLO v2, read replies
  bool welcomed = false;      // WELCOME received on the current connection family
  bool absolute = false;      // ... and it said the hardware is slider-driven
  uint16_t hwSlider = 0;
  void (*hwCb)(void*, bool, uint16_t) = nullptr;
  void* hwUser = nullptr;
  audiod::FrameParser inParser;
  unsigned leadMsCap = kDefaultLeadMs;
  bool sendVolume();
  void drainIncoming();       // m held: reads WELCOME / HWVOLUME, queues pendingReport (the callback runs without the mutex)
  struct HwReport { bool fire = false; bool absolute = false; uint16_t slider = 0; } pendingReport;
  void fireReport();          // m NOT held
  uint64_t timelineStartMs = 0;  // 0 = no timeline yet
  uint64_t timelineFrames = 0;   // frames sent on the current timeline
  uint64_t lastWriteMs = 0;
  void pace(size_t framesAboutToSend);  // sleeps until sending them keeps the lead <= kLeadMs
  long leadMs(uint64_t now) const;
};
