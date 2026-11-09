#pragma once
// The arbitration FSM of fidelio-audiod (DESIGN.md "Arbitration"), kept free of I/O so it can be
// unit-tested: feed it source events with timestamps, ask it who is active.
//
// Rules implemented (B1.2):
//   loud           an AUDIO frame counts as sound only if its peak |sample| exceeds silenceThreshold
//                  (the dither floor, 24: a paused AirPlay sender streams EXACT zeros; quiet music,
//                  fades and pianissimo stay well above).
//   producing(s)   = loud within producingGapMs
//   takeover       a non-active source whose current loud run is >= takeoverMs old becomes active
//                  when nobody is active or the active source has held for >= holdMs,
//   most-recent    and only if its STREAM started after the active one's. A stream starts on connect,
//                  on the first audio after STOP or FLUSH, and after a gap in bytes >= releaseMs; a
//                  quiet passage or a short network hiccup does not make a background source "newer",
//                  so it cannot steal the speaker without a user action.
//   release        "quiet": no bytes at all for releaseMs (or STOP) -> released, output flushed.
//                  "silent": bytes still arriving but nothing loud for quietReleaseMs -> released
//                  WITHOUT a flush (the buffered audio plays out; a fade is not a pause).
//   resume         a loud frame after >= quietReleaseMs of silence is a new stream (the user pressed
//                  play on a sender that kept streaming zeros): it may take the speaker back.
//   reconnect      a disconnected source is kept as a ghost for ghostMs: a new connection with the
//                  same name inherits its stream recency and, if it was still active, the active
//                  slot (no release, no takeover window): a producer that reconnects after a
//                  send timeout is the same stream, not a newer one. A ghost that never comes back
//                  is released by the quiet rule and forgotten.
//   idle stop      no active source for >= idleStopMs -> the output is stopped (route released).
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace audiod {

struct Tunables {
  uint64_t producingGapMs = 1000;  // B1.1: was 120; tolerate decoder/network hiccups within a track
  uint64_t takeoverMs = 400;
  uint64_t holdMs = 1500;
  uint64_t releaseMs = 1500;       // no bytes at all for this long: the source is gone (covers raop's post-FLUSH re-prime)
  uint64_t quietReleaseMs = 8000;  // bytes arriving but nothing loud for this long: a paused sender streaming silence
  uint64_t idleStopMs = 5000;
  uint64_t ghostMs = 10000;        // a disconnected source's identity survives this long for a same-name reconnect
  int silenceThreshold = 24;       // peak |sample| a frame must exceed to count as sound (0 = any frame); dither floor
};

struct Source {
  int id = 0;            // connection id (unique per process lifetime)
  std::string name;      // from HELLO
  uint8_t priority = 0;
  bool haveLoud = false;           // a loud frame has been seen since the last STOP
  bool haveAudio = false;          // any AUDIO since connect / STOP
  bool newStream = true;           // the next AUDIO starts a new stream (connect, STOP, FLUSH)
  bool connected = true;           // false: a ghost (disconnected, kept for a same-name reconnect)
  uint64_t droppedAtMs = 0;
  uint64_t lastAudioMs = 0;        // last AUDIO received (any content)
  uint64_t lastLoudMs = 0;         // last AUDIO whose peak exceeded silenceThreshold
  uint64_t loudRunSinceMs = 0;     // start of the current loud run (reset after > producingGapMs without sound): takeover debounce
  uint64_t streamSinceMs = 0;      // start of the current stream (recency for most-recent-wins)
  uint64_t bytes = 0;
};

struct Event {
  enum Kind { TAKEOVER, RELEASE, IDLE_STOP, OUTPUT_START, WAKE, HWVOLUME } kind;  // WAKE / HWVOLUME are logged by the Server (B1.9)
  uint64_t atMs;
  std::string from, to;   // source names ("" = none)
  std::string reason;
};

class Arbiter {
 public:
  explicit Arbiter(const Tunables& t) : tun(t) {
    // A quiet-released source that still counts as producing would re-take the output in the same tick.
    if (tun.producingGapMs > tun.releaseMs) tun.producingGapMs = tun.releaseMs;
  }

  // Returns true if the source reconnected (inherited a ghost's recency; if the ghost was active, it is active again).
  bool addSource(int id, const std::string& name, uint8_t priority, uint64_t now);
  void removeSource(int id, uint64_t now);        // disconnect: the source becomes a ghost (released by the quiet rule if active)
  void onAudio(int id, size_t bytes, uint64_t now, bool loud);  // AUDIO frame received; loud = peak > threshold
  void onFlush(int id);                            // FLUSH message: the next audio is a new stream (seek / track change)
  void onStop(int id, uint64_t now);               // STOP message
  // Advance time: evaluates release / takeover / idle stop. Call on every AUDIO and on a timer.
  void tick(uint64_t now);

  int activeId() const { return active; }
  std::string activeName() const;
  bool outputRunning() const { return outputOn; }
  bool producing(const Source& s, uint64_t now) const { return s.haveLoud && now - s.lastLoudMs < tun.producingGapMs; }
  const Source* source(int id) const;
  // Events produced since the last drain (the server logs/forwards them).
  std::vector<Event> drainEvents();
  const Tunables& tunables() const { return tun; }

 private:
  void setActive(int id, uint64_t now, const std::string& reason);
  void release(uint64_t now, const std::string& reason);

  Tunables tun;  // validated in the constructor: producingGapMs <= releaseMs
  std::map<int, Source> sources;
  int active = -1;
  uint64_t activeSinceMs = 0;
  uint64_t activeStreamSinceMs = 0;     // streamSince of the active source when it took over
  uint64_t releasedAtMs = 0;
  bool everActive = false;
  bool outputOn = false;
  std::vector<Event> events;
};

}  // namespace audiod
