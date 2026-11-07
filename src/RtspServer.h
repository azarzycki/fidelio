#pragma once
// RAOP RTSP server: accepts connections on the RTSP port and runs one handler thread per
// connection. One audio session at a time; a second ANNOUNCE while busy gets 453.
#include <netinet/in.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "RaopCrypto.h"
#include "RaopSession.h"

class AudioOutput;
class StatusFile;

struct RtspRequest {
  std::string method, uri, version;
  std::map<std::string, std::string> headers;  // lower-case keys
  std::vector<uint8_t> body;
  std::string header(const char* name) const;
};

class RtspServer {
 public:
  RtspServer(uint16_t port, const std::string& name, const uint8_t hwaddr[6], AudioOutput* out, StatusFile* status, int latencyMs);
  // Binds the listening socket; returns the port actually bound (0 on failure).
  uint16_t listen();
  // Runs the accept loop forever.
  void run();

 private:
  struct Conn {
    int fd;
    sockaddr_in local, peer;
    RtspServer* server;
    bool ownsSession = false;
    std::string sessionUri;
  };
  static void* connEntry(void* arg);
  void handleConnection(Conn* c);
  bool readRequest(int fd, std::string& pending, RtspRequest& req);
  void send(int fd, const RtspRequest& req, int code, const std::map<std::string, std::string>& hdr, const std::string& body,
            const char* contentType = nullptr);
  void handle(Conn* c, const RtspRequest& req);
  void onAnnounce(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h);
  void onSetup(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h);
  void onRecord(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h);
  void onFlush(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h);
  void onSetParameter(Conn* c, const RtspRequest& req, int& code);
  void onTeardown(Conn* c);
  void endSession(Conn* c, const char* why);

  uint16_t port;
  std::string name;
  uint8_t hwaddr[6];
  AudioOutput* out;
  StatusFile* status;
  int latencyMs;
  int listenFd = -1;
  RaopCrypto crypto;

  std::mutex sessionMutex;
  std::unique_ptr<RaopSession> session;  // the active one, owned by one connection
  Conn* sessionOwner = nullptr;
  float lastVolumeDb = -20.0f;
};
