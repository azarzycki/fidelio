#pragma once
// fidelio-audiod server: a Unix-socket listener and a single poll() loop that drains every client
// (so a producer never blocks), feeds the arbiter, and writes only the active source's PCM to the
// one AudioOutput. Every source's latest 500 ms (its SocketSink lead) is kept in a per-client ring;
// the active one is pumped into the output only while the output holds < 0.5 s, so the loop never
// blocks on the hardware and a takeover starts with the new source's lead in hand.
// Silence is fed while no source is active until the idle stop (host file output only).
#include <signal.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "Arbiter.h"
#include "AudioOutput.h"
#include "AudiodFraming.h"

namespace audiod {

struct ServerOptions {
  std::string sockPath = AUDIOD_DEFAULT_SOCK;
  std::string statusDir;       // writes <dir>/active-source ("" = off)
  std::string eventLog;        // machine-readable transition log ("" = off)
  bool feedSilence = true;     // keep the output fed with silence while nothing is active
  int tickMs = 20;
  double sinkTargetSec = 0.5;  // pump the active source into the output while it holds less than this
  unsigned leadMs = 750;       // the producers' SocketSink lead: what a takeover starts with (ring trim)
};

class Server {
 public:
  Server(const ServerOptions& o, const Tunables& t, AudioOutput* out);
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
  };
  static const size_t kRingFrames = 66150;  // 1.5 s: the lead plus slack for the active source
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
  void writeActiveSourceFile(const std::string& name);
  void logEvent(const Event& e);

  ServerOptions opts;
  Arbiter arbiter;
  AudioOutput* out;
  int listenFd = -1;
  int nextId = 1;
  std::map<int, Client> clients;  // by fd
  std::string lastActiveWritten = "?";
  uint64_t lastSilenceMs = 0;
  uint64_t startMs = 0;
  FILE* eventFile = nullptr;
};

}  // namespace audiod
