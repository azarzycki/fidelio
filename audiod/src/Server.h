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
#include "Settings.h"

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
  bool airplayAbsolute = true; // --airplay-volume-mode absolute|software (B1.13): the AirPlay sender's volume drives the hardware
  int maxVolumePct = 100;      // --max-volume: ceiling for what absolute mode will ever set the hardware to
  int hwCurve = 0;             // --hw-volume-curve: 0 linear (default: the hardware has its own taper), 1 log (60 dB), 2 cubic
  // B1.9 B: wake the display and foreground the NowPlaying app when playback starts from nothing
  bool wakeOnPlay = true;      // --wake-on-play on|off
  std::string wakeBundle = "com.fidelio.nowplaying";
  unsigned wakeMinIntervalMs = 5000;  // debounce: at most one wake per this interval
  // B1.16.10: what the wake does (registry wake_mode, set per device from the NowPlaying Settings tab).
  // true = "launch": wake the screen and bring NowPlaying forward (a locked iPad is unlocked first when
  // unlockOnPlay; iPhone OS 3 opens the app over the lock screen). false = "screen": light the screen
  // only, so a locked device shows its lock screen with the lock plugin (the backstop); no unlock.
  bool wakeLaunch = true;             // --wake-mode launch|screen
  // B1.16.4: when that launch is refused because the device is locked (3), ask SpringBoard to unlock
  // and open unlockUrl (SBSOpenSensitiveURLAndUnlock; works only without a passcode). B1.16.6: the
  // accepted request is the result (device-verified: it unlocks and opens the app; a launch probe keeps
  // answering 3 for seconds afterwards, so it is no longer used). Off = stay on the lock screen.
  bool unlockOnPlay = true;               // --unlock-on-play on|off
  std::string unlockUrl = "nowplaying:";  // --unlock-url: NowPlaying's own URL scheme, so the unlock lands in the app
  // B1.11: a SpringBoard lock-screen bundle enabled while a source plays (default off so a bad
  // bundle cannot affect playback). "" = off. B1.16.4: enabled only when audiod does not unlock the
  // device for that start (already unlocked, unlock off / unavailable / refused).
  std::string lockPlugin;
  // B1.12: tell a source that lost the output to pause (so a background session cannot roll on and
  // steal the output). Sent only to v2 clients; raop reacts via DACP (its own policy).
  bool preemptPause = true;         // --preempt-pause on|off
  unsigned preemptDelayMs = 500;    // wait this long before telling the loser, so a flap never pauses the active source
  // B1.15e: keep the lock screen lit while playing (audiod owns this; it writes <statusDir>/lockplugin.plist,
  // which the lock bundle live-reloads). Not read by audiod itself.
  bool keepScreenOn = false;        // --keep-screen-on on|off
  // B1.16: whether the dock infrared/accessory remote drives playback. audiod only carries this
  // (manifest + settings.conf); the DockRemote tweak inside iapd reads it to decide whether to
  // forward play/next/prev to audiod instead of the iPod app. audiod itself does not act on it.
  bool dockRemote = false;          // --dock-remote on|off
};

class Server {
 public:
  Server(const ServerOptions& o, const Tunables& t, AudioOutput* out, HardwareVolume* hw = nullptr, DisplayWaker* waker = nullptr);
  ~Server();
  bool listen();
  void run();  // until requestStop()
  // B1.14: live settings. reg stays owned by the caller; conf is settings.conf, dir holds the manifest.
  void enableLiveSettings(fidelio::Registry* reg, const std::string& conf, const std::string& dir) { settings = reg; settingsConf = conf; settingsDir = dir; }
  // B1.15: the registry binds to these so the manifest reflects what the daemon actually uses and a
  // settings.conf value is applied to the live config. applyLiveConfig() pushes tunables into the
  // arbiter and recomputes derived values (call after a startup load or a live reload).
  ServerOptions& mutableOptions() { return opts; }
  Tunables& mutableTunables() { return t; }
  void applyLiveConfig();
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
    bool controller = false;    // B1.16: a non-producing "control" client (sends CONTROL, never AUDIO)
    uint64_t preemptDueMs = 0;      // B1.12: send PREEMPTED to this client at/after this time (0 = none pending)
    std::string preemptBy;          // ... naming the source that took over
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
  bool hwReady() const { return hw && hwAvailable; }
  // B1.13: absolute mode can differ per source. A slider-driven source sets the hardware only when
  // its source is in absolute mode and the backend is available.
  bool absoluteActiveFor(const std::string& name) const { return (name == "airplay" ? opts.airplayAbsolute : opts.absoluteVolume) && hwReady(); }
  bool absoluteActive() const { return opts.absoluteVolume && hwReady(); }  // Spotify (kept for the startup log)
  float sliderToLevel(uint16_t slider) const;   // raw slider -> hardware level through opts.hwCurve
  uint16_t levelToSlider(float level) const;    // inverse, for reporting
  void applySlider(Client& c, uint16_t slider); // a user slider move: clamp, set the hardware, log, report a clamp
  void pollHardwareVolume(uint64_t now);        // buttons / other changes -> HWVOLUME to v2 clients
  // B1.16.12: after a SOURCE's set (applySlider only; never an external change), tell the dock the
  // same level (HardwareVolume::tellDock). At most one call per dockMinMs; a set inside that window is
  // held, and the latest held level goes out from the run loop (flushDockForward) when the window ends.
  // An external change drops a held level: the dock set it, or another app did after our set.
  const char* forwardToDock(float level, uint64_t now);  // returns the log word (ok, deferred, ...)
  HardwareVolume::DockResult sendToDock(float level, uint64_t now);
  void flushDockForward(uint64_t now);
  void sendToClient(Client& c, uint32_t type, const void* payload, size_t len);  // non-blocking; a full buffer drops the message
  void sendWelcome(Client& c);
  void sendHwVolumeTo(Client& c);   // B1.10: current hardware level to one (active) source; arms the grace
  void broadcastHwVolume();
  // B1.9 B wake + B1.16.4 unlock + B1.11 lock plugin, for a playback start from nothing
  enum WakeResult { WAKE_NOT_RUN, WAKE_DONE, WAKE_UNLOCKED };
  void onStartFromNone(const Event& e);
  WakeResult maybeWake(const Event& e);
  // B1.14: re-read settings.conf when it changes and apply the live options (and rewrite the manifest).
  void maybeReloadSettings(uint64_t now);
  // B1.16: a controller sent a transport verb -> route it to the active source (or, for PLAY with
  // nothing active, the most-recently-active connected source so a paused Spotify resumes).
  void handleControl(Client& c, const Message& m);
  // B1.12: a source lost the output -> after a flap-filtering delay, tell it (PREEMPTED) if it is
  // still not active. Scheduled on the TAKEOVER, fired from the run loop.
  void schedulePreempt(const Event& e);
  void firePreempts(uint64_t now);
  Client* clientByName(const std::string& name);
  // B1.11: enable the lock-screen bundle for a none -> source start, disable it on the return to none.
  // B1.16.11: `why` is appended to the event's reason ("turned on while playing", ...).
  void enableLockPlugin(const std::string& source, uint64_t atMs, const char* why = nullptr);
  void disableLockPlugin(const char* why = nullptr);
  // B1.16.11: while a source plays, keep the plugin in line with the live setting and with SpringBoard
  // (re-enable after a SpringBoard restart, which forgets an enabled bundle). Called every run-loop pass.
  void syncLockPlugin(uint64_t now);

  ServerOptions opts;
  Tunables t;        // B1.15: the live tunables the registry binds to; mirrored into the arbiter via applyLiveConfig()
  Arbiter arbiter;
  AudioOutput* out;
  HardwareVolume* hw;
  DisplayWaker* waker;
  bool hwAvailable = false;
  float hwLevel = -1;            // last level we know (-1 = unknown)
  uint64_t lastHwPollMs = 0;
  // B1.16.12 dock forward (see forwardToDock). AUDIOD_DOCK_FORWARD_MIN_MS overrides the window (host tests).
  uint64_t dockMinMs = 200;
  uint64_t lastDockMs = 0;
  bool dockCalled = false;      // a call to iapd was made at least once (lastDockMs is valid)
  bool dockPending = false;     // a held level waits for the window to end
  float dockPendingLevel = 0;
  int dockHeld = 0;             // sets held since the last call (for the log)
  uint64_t lastWakeMs = 0;
  std::string lastActiveName;  // B1.16: the last source that held the output (for PLAY-resumes-paused)
  bool lockEnabled = false;  // B1.11: the lock bundle is currently enabled
  // B1.16.11: the name it was enabled under (a live setting change must not disable a different name),
  // whether audiod unlocked the device for the current start (the agreed rule keeps the plugin off then),
  // the last lock_plugin setting seen (-1 = not yet), and SpringBoard-restart detection.
  std::string lockEnabledName;
  bool unlockedThisStart = false;
  int lockSettingSeen = -1;
  int sbPidAtEnable = 0;
  uint64_t lastSbCheckMs = 0, sbNewSeenMs = 0;
  static const uint64_t kSbCheckMs = 2000;   // poll SpringBoard's pid this often while the plugin is enabled
  static const uint64_t kSbSettleMs = 4000;  // and re-enable once a new SpringBoard has been up this long
  bool unlockUnavailableLogged = false;  // B1.16.4: "SBSOpenSensitiveURLAndUnlock not available", once per process
  // B1.14 live settings
  fidelio::Registry* settings = nullptr;
  std::string settingsConf, settingsDir;
  uint64_t lastSettingsCheckMs = 0;
  // B1.10 test hook (AUDIOD_STALL_ACTIVE_MS): gate reads of the active source for this long once it
  // first becomes active, to reproduce the device backpressure stall on the host.
  uint64_t stallActiveMs = 0;
  uint64_t activeSinceMs = 0;
  int stalledActiveId = -1;
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
