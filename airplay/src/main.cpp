// fidelio-raop: AirPlay v1 / RAOP audio receiver for ARMv6 iPhone OS 3.1.3.
// Advertises _raop._tcp via the system mDNSResponder, speaks RTSP, receives RTP audio
// (ALAC or L16, optionally AES-128-CBC), streams the PCM to fidelio-audiod (the arbiter that owns
// the AudioQueue; --direct-audio plays through a local AudioQueue instead), and writes the same
// status files the Fidelio now-playing app reads.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>

#include "AudioOutput.h"
#ifdef __APPLE__
#include "AudioQueueOutput.h"
#endif
#include "SocketSink.h"
#include "audiod/proto.h"
#include "Mdns.h"
#include "RtspServer.h"
#include "StatusFile.h"
#include "ios3_trace.h"

struct Options {
  std::string name = "Fidelio (AirPlay)";
  std::string statusDir = "/var/mobile/fidelio";
  int port = 5000;
  int latencyMs = 1200;
  bool noAudio = false;
  bool help = false;
  std::string pcmOut;  // --pcm-out FILE: write decoded PCM to a file instead of playing
  bool directAudio = false;  // --direct-audio: local AudioQueue instead of fidelio-audiod
  std::string audiodSock = getenv("AUDIOD_SOCK") && *getenv("AUDIOD_SOCK") ? getenv("AUDIOD_SOCK") : AUDIOD_DEFAULT_SOCK;
};

static Options parse(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char* w) -> std::string { if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", w); exit(1); } return argv[++i]; };
    if (a == "-h" || a == "--help") o.help = true;
    else if (a == "--name" || a == "-n") o.name = next("--name");
    else if (a == "--port") o.port = atoi(next("--port").c_str());
    else if (a == "--status-dir") o.statusDir = next("--status-dir");
    else if (a == "--no-status") o.statusDir.clear();
    else if (a == "--no-audio") o.noAudio = true;
    else if (a == "--pcm-out") o.pcmOut = next("--pcm-out");
    else if (a == "--direct-audio") o.directAudio = true;
    else if (a == "--audiod-sock") o.audiodSock = next("--audiod-sock");
    else if (a == "--latency-ms") o.latencyMs = atoi(next("--latency-ms").c_str());
    else { fprintf(stderr, "unknown option %s\n", a.c_str()); exit(1); }
  }
  return o;
}

// Stable fake MAC derived from the name: locally administered, unicast.
static void fakeMac(const std::string& name, uint8_t mac[6]) {
  uint32_t h = 2166136261u;
  for (unsigned char c : name) { h ^= c; h *= 16777619u; }
  mac[0] = 0x02;
  mac[1] = (uint8_t)(h >> 24); mac[2] = (uint8_t)(h >> 16); mac[3] = (uint8_t)(h >> 8); mac[4] = (uint8_t)h;
  mac[5] = (uint8_t)(name.size() * 37 + 0x11);
}

int main(int argc, char** argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  signal(SIGPIPE, SIG_IGN);
  Options o = parse(argc, argv);
  if (o.help) {
    printf("Usage: fidelio-raop [OPTION]...\n"
           "AirPlay v1 / RAOP audio receiver for iPhone OS 3 (ARMv6); plays through fidelio-audiod.\n"
           "  -n, --name NAME       advertised name (default: Fidelio (AirPlay))\n"
           "  --port PORT           RTSP port (default 5000; 0 = any free port)\n"
           "  --latency-ms MS       jitter buffer depth before playback starts (default 1200, max ~3800)\n"
           "  --status-dir DIR      status.json/status.plist/cover.jpg (default /var/mobile/fidelio); --no-status disables\n"
           "  --audiod-sock PATH    fidelio-audiod socket (default %s; env AUDIOD_SOCK)\n"
           "  --direct-audio        play through a local AudioQueue instead of fidelio-audiod (standalone debugging)\n"
           "  --no-audio            decode and discard, for isolation tests\n"
           "  --pcm-out FILE        write the decoded 44.1 kHz/16/stereo PCM to FILE instead of playing\n", AUDIOD_DEFAULT_SOCK);
    return 0;
  }
  IOS3_TRACE("fidelio-raop start: name='%s' port=%d latency=%d ms no-audio=%d status=%s", o.name.c_str(), o.port, o.latencyMs,
             (int)o.noAudio, o.statusDir.empty() ? "(off)" : o.statusDir.c_str());

  std::unique_ptr<AudioOutput> out;
  if (o.noAudio) out.reset(new NullOutput());
  else if (!o.pcmOut.empty()) {
    FileOutput* fo = new FileOutput(o.pcmOut.c_str());
    if (!fo->ok()) { IOS3_TRACE("cannot open %s", o.pcmOut.c_str()); return 2; }
    out.reset(fo);
    IOS3_TRACE("PCM file output: %s", o.pcmOut.c_str());
  } else if (o.directAudio) {
#ifdef __APPLE__
    out.reset(new AudioQueueOutput()); IOS3_TRACE("direct AudioQueue output created");
#else
    IOS3_TRACE("host build: no AudioQueue, use --pcm-out, --no-audio or audiod"); return 2;
#endif
  } else {
    out.reset(new SocketSink("airplay", o.audiodSock));
    IOS3_TRACE("streaming to fidelio-audiod at %s", o.audiodSock.c_str());
  }

  StatusFile status(o.statusDir, o.name);
  status.setState("idle");

  uint8_t mac[6];
  fakeMac(o.name, mac);
  char macStr[16];
  snprintf(macStr, sizeof macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  RtspServer server((uint16_t)o.port, o.name, mac, out.get(), &status, o.latencyMs);
  uint16_t port = server.listen();
  if (!port) { IOS3_TRACE("cannot listen on RTSP port %d", o.port); return 2; }
  IOS3_TRACE("RTSP listening on port %u", port);

  std::map<std::string, std::string> txt = {
      {"txtvers", "1"}, {"ch", "2"},    {"cn", "0,1"},  {"et", "0,1"},  {"sr", "44100"}, {"ss", "16"},
      {"tp", "UDP"},    {"vs", "105.1"}, {"am", "Fidelio1,1"}, {"sv", "false"}, {"da", "true"}, {"sf", "0x4"},
      {"md", "0,1,2"},  {"vn", "65537"}, {"pw", "false"},
  };
  RaopAdvertiser mdns;
  if (!mdns.start(std::string(macStr) + "@" + o.name, port, txt)) IOS3_TRACE("continuing without mDNS (senders must be given the address manually)");

  server.run();  // never returns
  return 0;
}
