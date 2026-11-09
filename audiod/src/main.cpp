// fidelio-audiod: the audio arbiter (audiod/DESIGN.md). Owns the single audio output; cspot and
// fidelio-raop stream PCM to it over a Unix socket; most-recent-active-source-wins, debounced.
// Output: the AudioQueue on iOS (airplay/src/AudioQueueOutput.h), --pcm-out / --no-audio on the host.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>

#include "AudioOutput.h"
#include "PacedFileOutput.h"
#ifdef __APPLE__
#include "AudioQueueOutput.h"
#endif
#include "Arbiter.h"
#include "Server.h"
#include "ios3_trace.h"

static uint64_t envOr(const char* name, uint64_t v) {
  const char* e = getenv(name);
  return e && *e ? strtoull(e, nullptr, 10) : v;
}

int main(int argc, char** argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  signal(SIGPIPE, SIG_IGN);

  audiod::ServerOptions so;
  audiod::Tunables t;
  // env first (AUDIOD_*_MS), flags override
  t.producingGapMs = envOr("AUDIOD_PRODUCING_GAP_MS", t.producingGapMs);
  t.takeoverMs = envOr("AUDIOD_TAKEOVER_MS", t.takeoverMs);
  t.holdMs = envOr("AUDIOD_HOLD_MS", t.holdMs);
  t.releaseMs = envOr("AUDIOD_RELEASE_MS", t.releaseMs);
  t.idleStopMs = envOr("AUDIOD_IDLE_STOP_MS", t.idleStopMs);
  t.silenceThreshold = (int)envOr("AUDIOD_SILENCE_THRESHOLD", (uint64_t)t.silenceThreshold);
  t.quietReleaseMs = envOr("AUDIOD_QUIET_RELEASE_MS", t.quietReleaseMs);
  so.leadMs = (unsigned)envOr("AUDIOD_LEAD_MS", so.leadMs);
  if (getenv("AUDIOD_SOCK")) so.sockPath = getenv("AUDIOD_SOCK");
  std::string pcmOut;
  double pacedPpm = 0; bool paced = false;  // --pcm-out-paced: host model of the AudioQueue's consumption
  bool noAudio = false, feedSilenceSet = false;
  so.statusDir = "/var/mobile/fidelio";
#ifdef __APPLE__
  AudioQueueSink::Options aq;
#endif

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char* w) -> const char* { if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", w); exit(1); } return argv[++i]; };
    if (a == "-h" || a == "--help") {
      printf("Usage: fidelio-audiod [OPTION]...\n"
             "  --sock PATH              listen here (default %s; env AUDIOD_SOCK)\n"
             "  --pcm-out FILE           write the played PCM to FILE instead of the AudioQueue (host test)\n"
             "  --no-audio               discard the played PCM\n"
             "  --pcm-out-paced PPM      with --pcm-out: consume at 44100*(1+PPM/1e6) frames/s in 46 ms bursts like the\n"
             "                           AudioQueue, so the pump and the backpressure run as on the device (host test)\n"
             "  --no-backpressure        read the active source as fast as it sends (the b14 behaviour; comparison)\n"
             "  --crossfade-ms MS        (default 30) at a source switch / release, ramp the tail about to play to zero\n"
             "                           and the new source's start from zero instead of a hard cut (0 = hard flush)\n"
             "  --audio-route auto|speaker|lineout   (AudioQueue) follow the system route / force the speaker\n"
             "  --on-route-change recreate|restart|none  (AudioQueue) what to do when the route changes\n"
             "  --audio-session auto|on|off   (AudioQueue) auto (default): full AudioSession path (route following,\n"
             "                           interruptions, yield); if AudioSessionInitialize fails it is retried forever (1..15 s\n"
             "                           backoff) while audio plays through a bare queue, and the output upgrades in place when\n"
             "                           it succeeds (cold boot: the daemon starts before the audio session server). on: the\n"
             "                           same, but no audio until the session is up. off: bare queue only, zero AudioSession\n"
             "                           calls (A/B diagnosis only)\n"
             "  --status-dir DIR         write DIR/active-source (default /var/mobile/fidelio); --no-status disables\n"
             "  --event-log FILE         machine-readable transition log: '<ms> <kind> <from> <to> <reason>'\n"
             "  --no-silence / --silence do not / do feed silence between sources (default: on for\n"
             "                           --pcm-out, off for the AudioQueue, which pads itself)\n"
             "  --tick-ms MS             arbitration tick (default 20)\n"
             "  --producing-gap-ms MS    (default 1000) env AUDIOD_PRODUCING_GAP_MS\n"
             "  --takeover-ms MS         (default 400)  env AUDIOD_TAKEOVER_MS\n"
             "  --hold-ms MS             (default 1500) env AUDIOD_HOLD_MS\n"
             "  --release-ms MS          (default 1500) env AUDIOD_RELEASE_MS; no bytes at all for this long = gone\n"
             "  --quiet-release-ms MS    (default 8000) env AUDIOD_QUIET_RELEASE_MS; bytes but nothing loud for this long\n"
             "                           = a paused sender streaming silence (released without a flush)\n"
             "  --idle-stop-ms MS        (default 5000) env AUDIOD_IDLE_STOP_MS\n"
             "  --silence-threshold N    (default 24)   env AUDIOD_SILENCE_THRESHOLD; a frame counts as sound only if\n"
             "                           one |sample| exceeds N (0 = any frame); pre-volume samples (dither floor)\n"
             "  --lead-ms MS             (default 750)  env AUDIOD_LEAD_MS; the producers' SocketSink lead\n", AUDIOD_DEFAULT_SOCK);
      return 0;
    }
    else if (a == "--sock") so.sockPath = next("--sock");
    else if (a == "--pcm-out") pcmOut = next("--pcm-out");
    else if (a == "--no-audio") noAudio = true;
    else if (a == "--pcm-out-paced") { paced = true; pacedPpm = atof(next(a.c_str())); }
    else if (a == "--no-backpressure") so.backpressure = false;
    else if (a == "--crossfade-ms") so.crossfadeMs = (unsigned)atoi(next(a.c_str()));
    else if (a == "--status-dir") so.statusDir = next("--status-dir");
    else if (a == "--no-status") so.statusDir.clear();
    else if (a == "--event-log") so.eventLog = next("--event-log");
    else if (a == "--no-silence") { so.feedSilence = false; feedSilenceSet = true; }
    else if (a == "--silence") { so.feedSilence = true; feedSilenceSet = true; }
#ifdef __APPLE__
    else if (a == "--audio-route") {
      std::string v = next("--audio-route");
      if (v == "auto") aq.route = AudioQueueSink::ROUTE_AUTO;
      else if (v == "speaker") aq.route = AudioQueueSink::ROUTE_SPEAKER;
      else if (v == "lineout") aq.route = AudioQueueSink::ROUTE_LINEOUT;
      else { fprintf(stderr, "--audio-route must be auto|speaker|lineout\n"); return 1; }
    } else if (a == "--on-route-change") {
      std::string v = next("--on-route-change");
      if (v == "recreate") aq.recovery = AudioQueueSink::RECOVER_RECREATE;
      else if (v == "restart") aq.recovery = AudioQueueSink::RECOVER_RESTART;
      else if (v == "none") aq.recovery = AudioQueueSink::RECOVER_NONE;
      else { fprintf(stderr, "--on-route-change must be recreate|restart|none\n"); return 1; }
    } else if (a == "--audio-session") {
      std::string v = next("--audio-session");
      if (v == "auto") aq.session = AudioQueueSink::SESSION_AUTO;
      else if (v == "on") aq.session = AudioQueueSink::SESSION_ON;
      else if (v == "off") aq.session = AudioQueueSink::SESSION_OFF;
      else { fprintf(stderr, "--audio-session must be auto|on|off\n"); return 1; }
    }
#else
    else if (a == "--audio-route" || a == "--on-route-change" || a == "--audio-session") next(a.c_str());  // accepted, no AudioQueue on the host
#endif
    else if (a == "--tick-ms") so.tickMs = atoi(next("--tick-ms"));
    else if (a == "--producing-gap-ms") t.producingGapMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--takeover-ms") t.takeoverMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--hold-ms") t.holdMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--release-ms") t.releaseMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--idle-stop-ms") t.idleStopMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--silence-threshold") t.silenceThreshold = atoi(next(a.c_str()));
    else if (a == "--quiet-release-ms") t.quietReleaseMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--lead-ms") so.leadMs = (unsigned)atoi(next(a.c_str()));
    else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
  }

  std::unique_ptr<AudioOutput> out;
  if (noAudio) out.reset(new NullOutput());
  else if (!pcmOut.empty()) {
    FileOutput* fo = paced ? new PacedFileOutput(pcmOut.c_str(), pacedPpm) : new FileOutput(pcmOut.c_str());
    if (!fo->ok()) { IOS3_TRACE("cannot open %s", pcmOut.c_str()); return 2; }
    out.reset(fo);
  } else {
#ifdef __APPLE__
    IOS3_TRACE("fidelio-audiod: AudioQueue output, --audio-session %s", AudioQueueSink::sessionModeName(aq.session));
    out.reset(new AudioQueueOutput(aq));
    if (!feedSilenceSet) so.feedSilence = false;  // the AudioQueue sink pads silence itself
#else
    (void)feedSilenceSet;
    IOS3_TRACE("host build: use --pcm-out FILE or --no-audio"); return 2;
#endif
  }
  IOS3_TRACE("fidelio-audiod start (output: %s)", noAudio ? "discard" : !pcmOut.empty() ? pcmOut.c_str() : "AudioQueue");

  audiod::Server server(so, t, out.get());
  if (!server.listen()) return 2;
  signal(SIGTERM, audiod::Server::requestStop);
  signal(SIGINT, audiod::Server::requestStop);
  server.run();
  IOS3_TRACE("fidelio-audiod exit");
  return 0;
}
