#pragma once
// fidelio-audiod server: a Unix-socket listener and a single poll() loop that drains every client
// (so a producer never blocks), feeds the arbiter, and writes only the active source's PCM to the
// one AudioOutput. Silence is fed while no source is active until the idle stop.
#include <stdint.h>

#include <map>
#include <string>

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
};

class Server {
 public:
  Server(const ServerOptions& o, const Tunables& t, AudioOutput* out);
  ~Server();
  bool listen();
  void run();  // forever
  static uint64_t nowMs();

 private:
  struct Client {
    int fd;
    int id;
    bool hello = false;
    std::string name;
    FrameParser parser;
  };
  void acceptClient();
  void readClient(Client& c);
  void dropClient(int fd, const char* why);
  void handle(Client& c, const Message& m);
  void afterTick();
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
