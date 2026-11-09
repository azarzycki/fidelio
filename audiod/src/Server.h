#pragma once
// fidelio-audiod server: a Unix-socket listener and a single poll() loop that drains every client
// (so a producer never blocks), feeds the arbiter, and writes only the active source's PCM to the
// one AudioOutput. Every source's latest lead is kept in a per-client ring; the active one is pumped
// into the output only while the output holds < 0.5 s, so the loop never blocks on the hardware and
// a takeover starts with the new source's lead in hand. Backpressure (B1.5): the ACTIVE source's
// socket is read only while its staging ring is nearly empty and the output is consuming, so its
// producer blocks on the (small) kernel socket buffers exactly as it used to block on the local
// AudioQueue ring: whoever is playing is paced by true consumption, not by a wall clock. Inactive
// sources are always drained (and overwritten in their ring) so they never block.
// Silence is fed while no source is active until the idle stop (host file output only).
#include <signal.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "Arbiter.h"
#include "AudioOutput.h"
#include "AudiodFraming.h"
#include "DisplayWake.h"
#include "HardwareVolume.h"

namespace audiod {

struct ServerOptions {
  std::string sockPath = AUDIOD_DEFAULT_SOCK;
  std::string statusDir;       // writes <dir>/active-source ("" = off)
  std::string eventLog;        // machine-readable transition log ("" = off)
  bool feedSilence = true;     // keep the output fed with silence while nothing is active
  int tickMs = 20;
  double sinkTargetSec = 0.45;  // pump the active source into the output while it holds less than this (< SocketSink lead - staging - kernel buffers)
  unsigned leadMs = 750;       // the producers' SocketSink lead: what a takeover starts with (ring trim)
  bool backpressure = true;    // read the active source only as fast as the output consumes (B1.5)
  unsigned crossfadeMs = 30;   // source switch / release: ramp the tail about to play and the new source's start (0 = hard flush)
  // B1.9 A: a slider-driven source (Spotify) sets the device's real output volume instead of a software gain
  bool absoluteVolume = true;  // --spotify-volume absolute|software (falls back to software when the hardware API is unavailable)
  int maxVolumePct = 100;      // --max-volume: ceiling for what absolute mode will ever set the hardware to
  int hwCurve = 0;             // --hw-volume-curve: 0 linear (default: the hardware has its own taper), 1 log (60 dB), 2 cubic
  // B1.9 B: wake the display and foreground the NowPlaying app when playback starts from nothing
  bool wakeOnPlay = true;      // --wake-on-play on|off
  std::string wakeBundle = "com.fidelio.nowplaying";
  unsigned wakeMinIntervalMs = 5000;  // debounce: at most one wake per this interval
};

class Server {
 public:
  Server(const ServerOptions& o, const Tunables& t, AudioOutput* out, HardwareVolume* hw = nullptr, DisplayWaker* waker = nullptr);
  ~Server();
  bool listen();
  void run();  // until requestStop()
  static uint64_t nowMs();
  static void requestStop(int signo);  // signal-handler safe

 private:
  struct Client {
    int fd;
    int id;
    bool hello = false;
    std::string name;
    FrameParser parser;
    std::vector<int16_t> ring;  // kRingFrames stereo frames: the source's latest audio
    size_t ringHead = 0, ringFrames = 0;
    unsigned long overflowFrames = 0;
    float volume = 1.0f;        // from VOLUME; applied to the output while this source is active
    uint8_t version = 1;        // HELLO protocol version (2 = reads WELCOME / HWVOLUME)
    bool sliderDriven = false;  // sent a VOLUME with AUDIOD_VOL_SLIDER: in absolute mode its gain is unity and the slider drives the hardware
    uint16_t slider = 0;
    bool sendFailedLogged = false;
  };
  static const size_t kRingFrames = 88200;  // 2 s: an inactive source's lead (up to SocketSink's cap) plus slack
  static const size_t kStagingFrames = 1764;  // 40 ms: with backpressure the active source's ring is only a staging area
  size_t leadFrames = 33075;                // opts.leadMs in frames
  static volatile sig_atomic_t stopRequested;
  void acceptClient();
  void readClient(Client& c);
  void dropClient(int fd, const char* why);
  void handle(Client& c, const Message& m);
  void afterTick();
  void pump();  // active source's ring -> output, while the output has room
  void ringAppend(Client& c, const int16_t* frames, size_t n);
  Client* clientById(int id);
  size_t fadeFrames() const;
  void writeActiveSourceFile(const std::string& name);
  void logEvent(const Event& e);
  // B1.9 A
  bool absoluteActive() const { return opts.absoluteVolume && hw && hwAvailable; }
  float sliderToLevel(uint16_t slider) const;   // raw slider -> hardware level through opts.hwCurve
  uint16_t levelToSlider(float level) const;    // inverse, for reporting
  void applySlider(Client& c, uint16_t slider); // a user slider move: clamp, set the hardware, log, report a clamp
  void pollHardwareVolume(uint64_t now);        // buttons / other changes -> HWVOLUME to v2 clients
  void sendToClient(Client& c, uint32_t type, const void* payload, size_t len);  // non-blocking; a full buffer drops the message
  void sendWelcome(Client& c);
  void broadcastHwVolume();
  // B1.9 B
  void maybeWake(const Event& e);

  ServerOptions opts;
  Arbiter arbiter;
  AudioOutput* out;
  HardwareVolume* hw;
  DisplayWaker* waker;
  bool hwAvailable = false;
  float hwLevel = -1;            // last level we know (-1 = unknown)
  uint64_t lastHwPollMs = 0;
  uint64_t lastWakeMs = 0;
  int listenFd = -1;
  int nextId = 1;
  std::map<int, Client> clients;  // by fd
  std::string lastActiveWritten = "?";
  uint64_t lastActiveWriteMs = 0;
  uint64_t lastSilenceMs = 0;
  uint64_t startMs = 0;
  FILE* eventFile = nullptr;
};

}  // namespace audiod
