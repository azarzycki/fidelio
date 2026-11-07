#pragma once
// The arbitration FSM of fidelio-audiod (DESIGN.md "Arbitration"), kept free of I/O so it can be
// unit-tested: feed it source events with timestamps, ask it who is active.
//
// Rules implemented:
//   producing(s)   = now - s.lastAudio < producingGapMs
//   takeover       a non-active source that has been continuously producing for >= takeoverMs
//                  becomes active when: nobody is active, or the active source has been quiet
//                  >= releaseMs, or the active source has held for >= holdMs.
//   most-recent    among eligible challengers the one that started producing most recently wins
//                  (tie: higher priority). A challenger that started producing BEFORE the current
//                  active source did does not steal it: "most-recent-active-source-wins" means the
//                  newest stream, so two sources that both keep streaming do not ping-pong every
//                  holdMs.
//   release        active quiet >= releaseMs (or STOP / disconnect) -> no active source.
//   idle stop      no active source for >= idleStopMs -> the output is stopped (route released).
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace audiod {

struct Tunables {
  uint64_t producingGapMs = 120;
  uint64_t takeoverMs = 400;
  uint64_t holdMs = 1500;
  uint64_t releaseMs = 1000;
  uint64_t idleStopMs = 5000;
};

struct Source {
  int id = 0;            // connection id (unique per process lifetime)
  std::string name;      // from HELLO
  uint8_t priority = 0;
  bool haveAudio = false;
  uint64_t lastAudioMs = 0;        // last AUDIO received
  uint64_t producingSinceMs = 0;   // start of the current producing run (reset after a gap)
  uint64_t bytes = 0;
};

struct Event {
  enum Kind { TAKEOVER, RELEASE, IDLE_STOP, OUTPUT_START } kind;
  uint64_t atMs;
  std::string from, to;   // source names ("" = none)
  std::string reason;
};

class Arbiter {
 public:
  explicit Arbiter(const Tunables& t) : tun(t) {}

  void addSource(int id, const std::string& name, uint8_t priority, uint64_t now);
  void removeSource(int id, uint64_t now);        // disconnect: releases it if active
  void onAudio(int id, size_t bytes, uint64_t now);  // AUDIO frame received
  void onStop(int id, uint64_t now);               // STOP message
  // Advance time: evaluates release / takeover / idle stop. Call on every AUDIO and on a timer.
  void tick(uint64_t now);

  int activeId() const { return active; }
  std::string activeName() const;
  bool outputRunning() const { return outputOn; }
  bool producing(const Source& s, uint64_t now) const { return s.haveAudio && now - s.lastAudioMs < tun.producingGapMs; }
  const Source* source(int id) const;
  // Events produced since the last drain (the server logs/forwards them).
  std::vector<Event> drainEvents();
  const Tunables& tunables() const { return tun; }

 private:
  void setActive(int id, uint64_t now, const std::string& reason);
  void release(uint64_t now, const std::string& reason);

  Tunables tun;
  std::map<int, Source> sources;
  int active = -1;
  uint64_t activeSinceMs = 0;
  uint64_t activeProducingSinceMs = 0;  // producingSince of the active source when it took over
  uint64_t releasedAtMs = 0;
  bool everActive = false;
  bool outputOn = false;
  std::vector<Event> events;
};

}  // namespace audiod
