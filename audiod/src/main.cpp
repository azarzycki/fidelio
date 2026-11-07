// fidelio-audiod: the audio arbiter (audiod/DESIGN.md). Owns the single audio output; cspot and
// fidelio-raop stream PCM to it over a Unix socket; most-recent-active-source-wins, debounced.
// Milestone A: host-only (FileOutput / NullOutput). Milestone B swaps in AudioQueueOutput on iOS.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>

#include "AudioOutput.h"
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
  if (getenv("AUDIOD_SOCK")) so.sockPath = getenv("AUDIOD_SOCK");
  std::string pcmOut;
  bool noAudio = false;
  so.statusDir = "/var/mobile/fidelio";

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char* w) -> const char* { if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", w); exit(1); } return argv[++i]; };
    if (a == "-h" || a == "--help") {
      printf("Usage: fidelio-audiod [OPTION]...\n"
             "  --sock PATH              listen here (default %s; env AUDIOD_SOCK)\n"
             "  --pcm-out FILE           write the played PCM to FILE (host test)\n"
             "  --no-audio               discard the played PCM\n"
             "  --status-dir DIR         write DIR/active-source (default /var/mobile/fidelio); --no-status disables\n"
             "  --event-log FILE         machine-readable transition log: '<ms> <kind> <from> <to> <reason>'\n"
             "  --no-silence             do not feed silence between sources\n"
             "  --tick-ms MS             arbitration tick (default 20)\n"
             "  --producing-gap-ms MS    (default 120)  env AUDIOD_PRODUCING_GAP_MS\n"
             "  --takeover-ms MS         (default 400)  env AUDIOD_TAKEOVER_MS\n"
             "  --hold-ms MS             (default 1500) env AUDIOD_HOLD_MS\n"
             "  --release-ms MS          (default 1000) env AUDIOD_RELEASE_MS\n"
             "  --idle-stop-ms MS        (default 5000) env AUDIOD_IDLE_STOP_MS\n", AUDIOD_DEFAULT_SOCK);
      return 0;
    }
    else if (a == "--sock") so.sockPath = next("--sock");
    else if (a == "--pcm-out") pcmOut = next("--pcm-out");
    else if (a == "--no-audio") noAudio = true;
    else if (a == "--status-dir") so.statusDir = next("--status-dir");
    else if (a == "--no-status") so.statusDir.clear();
    else if (a == "--event-log") so.eventLog = next("--event-log");
    else if (a == "--no-silence") so.feedSilence = false;
    else if (a == "--tick-ms") so.tickMs = atoi(next("--tick-ms"));
    else if (a == "--producing-gap-ms") t.producingGapMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--takeover-ms") t.takeoverMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--hold-ms") t.holdMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--release-ms") t.releaseMs = strtoull(next(a.c_str()), nullptr, 10);
    else if (a == "--idle-stop-ms") t.idleStopMs = strtoull(next(a.c_str()), nullptr, 10);
    else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
  }

  std::unique_ptr<AudioOutput> out;
  if (noAudio) out.reset(new NullOutput());
  else if (!pcmOut.empty()) {
    FileOutput* fo = new FileOutput(pcmOut.c_str());
    if (!fo->ok()) { IOS3_TRACE("cannot open %s", pcmOut.c_str()); return 2; }
    out.reset(fo);
  } else {
#ifdef __APPLE__
    IOS3_TRACE("AudioQueue output arrives with Milestone B; use --pcm-out or --no-audio for now"); return 2;
#else
    IOS3_TRACE("host build: use --pcm-out FILE or --no-audio"); return 2;
#endif
  }
  IOS3_TRACE("fidelio-audiod start (milestone A, host output: %s)", noAudio ? "discard" : pcmOut.c_str());

  audiod::Server server(so, t, out.get());
  if (!server.listen()) return 2;
  server.run();
  return 0;
}
